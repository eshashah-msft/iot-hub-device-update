/**
 * @file command_helper.c
 * @brief A helper library for inter-agent commands support.
 *
 * @copyright Copyright (c) Microsoft Corp.
 * Licensed under the MIT License.
 */

#include "aduc/command_helper.h"
#include "aduc/logging.h"
#include "aduc/permission_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h> // getgrnm
#include <pthread.h> // pthread_*
#include <stdbool.h> // bool
#include <stdio.h> // getline
#include <stdlib.h> // free
#include <string.h> // strlen
#include <sys/stat.h> // mkfifo
#include <unistd.h> // sleep
#include "aduc/string_c_utils.h" // ADUC_Safe_StrCopyN

// keep this last to avoid interfering with system headers
#include "aduc/aduc_banned.h"

#define MAX_COMMAND_ARRAY_SIZE 1 // !< For version 1.0, we're supporting only 1 command.
#define COMMAND_MAX_LEN 64 // !< Max command length including NULL
#define DELAY_BETWEEN_FAILED_OPERATION_SECONDS 10 // !< delay allowed between failed operations

static pthread_mutex_t g_commandQueueMutex = PTHREAD_MUTEX_INITIALIZER; // !< Static defintion for the mutex to be used for communciating with the command threads
static pthread_t g_commandListenerThread; // !<  Static handle for the listener thread for routing info back from the child process
static bool g_commandListenerThreadCreated = false; // !< Static boolean switch to tell if the listener thread has been created
static bool g_terminate_thread_request = false; // !< Static boolean switch to tell if the thread needs to be terminated

/**
 * @brief Callback for reprocessing updates as they come in
 * @param command the command to be reprocessed / executed
 * @param context the context to be used for calling back into / accessing ADUC member values
 * @returns true on success; false otherwise
*/
bool ADUC_OnReprocessUpdate(const char* command, void* context);

static ADUC_Command* g_commands[MAX_COMMAND_ARRAY_SIZE] = {}; // !< Static list of commands being exectued of MAX_COMMAND_ARRAY_SIZE

/**
 * @brief Register command.
 *
 * @param command An ADUC_Command information.
 * @return int If success, returns index of the registered command. Otherwise, returns -1.
 */
int RegisterCommand(ADUC_Command* command)
{
    pthread_mutex_lock(&g_commandQueueMutex);
    int res = -1;
    // Find an empty slot to register a new command.
    for (int i = 0; i < MAX_COMMAND_ARRAY_SIZE; i++)
    {
        if (g_commands[i] == NULL)
        {
            Log_Info("Command register at slot#%d", i);
            g_commands[i] = command;
            res = i;
            goto done;
        }
    }

    Log_Error("No space available for command.");
done:
    pthread_mutex_unlock(&g_commandQueueMutex);
    return res;
}

/**
 * @brief Unregister command.
 *
 * @param command Pointer to a command to unregister.
 * @return bool If success, return true. Otherwise, returns false.
 */
bool UnregisterCommand(ADUC_Command* command)
{
    bool res = false;
    pthread_mutex_lock(&g_commandQueueMutex);
    for (int i = 0; i < MAX_COMMAND_ARRAY_SIZE; i++)
    {
        if (g_commands[i] == command)
        {
            Log_Info("Unregister command from stop#%d", i);
            g_commands[i] = NULL;
            res = true;
            goto done;
        }
    }
    Log_Warn("Command not found.");

done:
    pthread_mutex_unlock(&g_commandQueueMutex);
    return res;
}

/**
 * @brief Create a FIFO named pipe file.
 *
 * @return bool Returns true if success.
 */
static bool TryCreateFIFOPipe()
{
    // Try to create file if doesn't exist.
    struct stat st;
    if (stat(ADUC_COMMANDS_FIFO_NAME, &st) == -1)
    {
        // Create FIFO pipe for commands.
        // Only write to pipe
        if (mkfifo(ADUC_COMMANDS_FIFO_NAME, S_IRGRP | S_IWGRP | S_IRUSR | S_IWUSR) != 0)
        {
            int error_no = errno;
            switch (error_no)
            {
            case EACCES:
                Log_Error("No permission");
                break;
            case EDQUOT:
                Log_Error("The user's quota of disk blocks or inodes on the filesystem has been exhausted.");
                break;

            case EEXIST:
                Log_Error("pathname already exists.");
                break;

            case ENAMETOOLONG:
                Log_Error("Path or file name is too long.");
                break;

            case ENOENT:
                Log_Error("A directory component in pathname does not exist. (%s)", ADUC_COMMANDS_FIFO_NAME);
                break;

            case ENOSPC:
                Log_Error("The directory or filesystem has no room for the new file.");
                break;

            case ENOTDIR:
                Log_Error("A component used as a directory in pathname is not, in fact, a directory.");
                break;

            case EROFS:
                Log_Error("Pathname refers to a read-only filesystem.");
                break;

            default:
                Log_Error("Cannot create named pipe. errno '%d'.", error_no);
                break;
            }
            return false;
        }
    }

    Log_Info("Command FIFO file created successfully.");
    return true;
}

/**
 * @brief Perform following security checks:
 *     - The FIFO pipe owners must be adu:adu.
 *     - The calling process' effective group must be 'root' or 'adu'.
 *
 * @return bool
 */
static bool SecurityChecks()
{
    if (!(PermissionUtils_CheckOwnership(ADUC_COMMANDS_FIFO_NAME, ADUC_FILE_USER, ADUC_FILE_GROUP)))
    {
        Log_Error("Security error: '%s' has invalid owners.", ADUC_COMMANDS_FIFO_NAME);
        return false;
    }

    // Verify current user
    struct group* grp = getgrnam(ADUC_FILE_GROUP);
    if (grp == NULL)
    {
        // Failed to get 'adu' group information, bail.
        Log_Error("Cannot get '%s' group info.", ADUC_FILE_GROUP);
        return false;
    }

    gid_t gid = getegid();
    if (gid != 0 /* root */
        && gid != grp->gr_gid /* adu */)
    {
        return false;
    }

    return true;
}

/**
 * @brief
 *
 * @return void*
 */
static void* ADUC_CommandListenerThread(void* unused)
{
    bool threadCreated = false;
    int fileDescriptor = 0;

    (void)unused; // avoid unused parameter warning

    if (!TryCreateFIFOPipe() || !SecurityChecks())
    {
        goto done;
    }

    threadCreated = true;
    char commandLine[COMMAND_MAX_LEN];

    do
    {
        // Open file for read, if needed.
        if (fileDescriptor <= 0)
        {
            fileDescriptor = open(ADUC_COMMANDS_FIFO_NAME, O_RDONLY);
            if (fileDescriptor <= 0)
            {
                Log_Error("Cannot open '%s' for read.", ADUC_COMMANDS_FIFO_NAME);
                sleep(DELAY_BETWEEN_FAILED_OPERATION_SECONDS);
                continue;
            }
        }

        Log_Info("Wait for command...");
        // By default, read() is blocked, until at least one writer open a file descriptor.
        // For simplicity, we are leveraging this behavior instead of loop+sleep or 'select()' or 'poll()'.
        ssize_t readSize = read(fileDescriptor, commandLine, sizeof(commandLine));
        if (readSize < 0)
        {
            // An error occurred.
            Log_Warn("Read error (error:%d).", errno);
            // Close current file descriptor and retry in next 'DELAY_BETWEEN_FAILED_OPERATION_SECONDS' seconds.
            close(fileDescriptor);
            fileDescriptor = -1;
            sleep(DELAY_BETWEEN_FAILED_OPERATION_SECONDS);
            continue;
        }

        if (readSize == 0)
        {
            // EOF, in this case, no more data written to the pipe.
            // Close and reopen the reader (above), to reset the block state.
            // Note: regardless of fclose() result, fileDescriptor is no longer valid.
            close(fileDescriptor);
            fileDescriptor = -1;
            continue;
        }

        if (readSize < sizeof(commandLine))
        {
            // Bad input size. Discard this...
            Log_Warn(
                "Received command with invalid size (%d bytes, expected %d). Ignored.", readSize, sizeof(commandLine));
            continue;
        }

        // Process command.
        pthread_mutex_lock(&g_commandQueueMutex);
        const ADUC_Command* matchedCommand = NULL;
        for (int i = 0; i < MAX_COMMAND_ARRAY_SIZE; i++)
        {
            if (g_commands[i] != NULL)
            {
                size_t commandTextLen = strlen(g_commands[i]->commandText);
                if (readSize < commandTextLen)
                {
                    continue;
                }

                if (strcmp(commandLine, g_commands[i]->commandText) == 0)
                {
                    matchedCommand = g_commands[i];
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_commandQueueMutex);

        if (matchedCommand == NULL)
        {
            Log_Warn("Unsupported command received. '%s'", commandLine);
            continue;
        }

        // Command matched.
        Log_Info("Executing command handler function for '%s'", commandLine);
        if (!matchedCommand->callback(commandLine, NULL))
        {
            Log_Error("Cannot execute a command handler for '%s'.", commandLine);
            continue;
        }
    } while (!g_terminate_thread_request);

done:
    close(fileDescriptor);
    if (!threadCreated)
    {
        Log_Error("Cannot start the command listener thread.");
    }
    return NULL;
}

/**
 * @brief Send specified @p command to the main Device Update agent process.
 *
 * @param command A command to send.
 *
 * @return bool Returns true if success.
 */
bool SendCommand(const char* command)
{
    static char buffer[COMMAND_MAX_LEN];
    bool success = false;

    const size_t cmdLen = strlen(command);

    int fd = -1;
    if (command == NULL || *command == '\0')
    {
        Log_Error("Command is null or empty.");
        goto done;
    }

    if (cmdLen > COMMAND_MAX_LEN - 1)
    {
        Log_Error("Command is too long (63 characters max).");
        goto done;
    }

    // Check if the writer can access the pipe.
    if (!SecurityChecks())
    {
        goto done;
    }

    fd = open(ADUC_COMMANDS_FIFO_NAME, O_WRONLY);
    if (fd < 0)
    {
        Log_Error("Fail to open pipe.");
        goto done;
    }

    // Copy command to buffer and fill the remaining buffer (if any) with additional null bytes.
    ADUC_Safe_StrCopyN(buffer, command, sizeof(buffer), cmdLen);
    ssize_t size = write(fd, buffer, sizeof(buffer));
    if (size != sizeof(buffer))
    {
        Log_Error("Fail to send command.");
        goto done;
    }

    Log_Info("Command sent successfully.");
    success = true;
done:
    if (fd >= 0)
    {
        close(fd);
    }
    return success;
}

/**
 * @brief Initialize command listener thread.
 */
bool InitializeCommandListenerThread()
{
    if (g_commandListenerThreadCreated)
    {
        Log_Warn("Command listener thread already created.");
        return false;
    }

    Log_Info("Initializing command listener thread");

    if (pthread_create(&g_commandListenerThread, NULL, ADUC_CommandListenerThread, NULL) == 0)
    {
        g_commandListenerThreadCreated = true;
        return true;
    }

    return false;
}

/**
 * @brief Uninitialize command listener thread.
 */
void UninitializeCommandListenerThread()
{
    Log_Info("De-initializing command listener thread");
    g_terminate_thread_request = true;
}
