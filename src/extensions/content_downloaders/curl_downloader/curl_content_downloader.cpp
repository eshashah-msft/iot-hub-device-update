/**
 * @file curl_content_downloader.cpp
 * @brief Content Downloader Extension using curl command.
 *
 * @copyright Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License.
 */

#include "aduc/content_downloader_extension.hpp"
#include "aduc/contract_utils.h"
#include "aduc/hash_utils.h"
#include "aduc/logging.h"
#include "aduc/process_utils.hpp" // for ADUC_LaunchChildProcess

#include <sstream>
#include <sys/stat.h> // for stat
#include <vector>
// keep this last to minimize chance to interfere with system header includes.
#include "aduc/aduc_banned.h"

#ifdef ADUC_BUILD_SNAP
#    define CURL_PATH "/usr/bin/curl-downloader"
#else
#    define CURL_PATH "/usr/bin/curl"
#endif

ADUC_Result Download_curl(
    const ADUC_FileEntity* entity,
    const char* workflowId,
    const char* workFolder,
    unsigned int timeoutInSeconds,
    ADUC_DownloadProgressCallback downloadProgressCallback)
{
    UNREFERENCED_PARAMETER(timeoutInSeconds);
    ADUC_Result result = { ADUC_Result_Failure };
    SHAversion algVersion;
    std::vector<std::string> args;
    std::string output;
    int exitCode = 1;
    std::stringstream fullFilePath;
    bool isValidHash;
    bool reportProgress = false;

    if (entity == nullptr)
    {
        result.ExtendedResultCode = ADUC_ERC_CONTENT_DOWNLOADER_INVALID_FILE_ENTITY;
        goto done;
    }

    if (entity->DownloadUri == nullptr || *entity->DownloadUri == 0)
    {
        result.ExtendedResultCode = ADUC_ERC_CONTENT_DOWNLOADER_INVALID_DOWNLOAD_URI;
        goto done;
    }

    if (entity->HashCount == 0)
    {
        Log_Error("File entity does not contain a file hash! Cannot validate cancelling download.");
        result.ExtendedResultCode = ADUC_ERC_VALIDATION_FILE_HASH_IS_EMPTY;
        if (downloadProgressCallback != nullptr)
        {
            downloadProgressCallback(
                workflowId,
                entity->FileId,
                ADUC_DownloadProgressState_Error,
                result.ResultCode,
                result.ExtendedResultCode);
        }
        goto done;
    }

    fullFilePath << workFolder << "/" << entity->TargetFilename;

    if (!ADUC_HashUtils_GetShaVersionForTypeString(
            ADUC_HashUtils_GetHashType(entity->Hash, entity->HashCount, 0), &algVersion))
    {
        Log_Error(
            "FileEntity for %s has unsupported hash type %s",
            fullFilePath.str().c_str(),
            ADUC_HashUtils_GetHashType(entity->Hash, entity->HashCount, 0));
        result.ExtendedResultCode = ADUC_ERC_VALIDATION_FILE_HASH_TYPE_NOT_SUPPORTED;

        if (downloadProgressCallback != nullptr)
        {
            downloadProgressCallback(
                workflowId,
                entity->FileId,
                ADUC_DownloadProgressState_Error,
                result.ResultCode,
                result.ExtendedResultCode);
        }
        goto done;
    }

    // If target file exists, validate file hash.
    // If file is valid, then skip the download.
    isValidHash = ADUC_HashUtils_IsValidFileHash(
        fullFilePath.str().c_str(),
        ADUC_HashUtils_GetHashValue(entity->Hash, entity->HashCount, 0),
        algVersion,
        false /* suppressErrorLog */);

    if (isValidHash)
    {
        result = { ADUC_Result_Download_Skipped_FileExists };
        reportProgress = true;
        goto done;
    }

    Log_Info(
        "Downloading File '%s' from '%s' to '%s'",
        entity->TargetFilename,
        entity->DownloadUri,
        fullFilePath.str().c_str());

    args.emplace_back("-o");
    args.emplace_back(fullFilePath.str().c_str());
    args.emplace_back("-O");
    args.emplace_back(entity->DownloadUri);

    exitCode = ADUC_LaunchChildProcess(CURL_PATH, args, output);

    if (exitCode == 0)
    {
        result = { ADUC_Result_Download_Success };
    }
    else
    {
        result.ResultCode = ADUC_Result_Failure;
        result.ExtendedResultCode = ADUC_ERROR_CURL_DOWNLOADER_EXTERNAL_FAILURE(exitCode);
        reportProgress = true;
        goto done;
    }

    if (IsAducResultCodeFailure(result.ResultCode))
    {
        Log_Error(
            "Download job end. exit code: %d, resultCode: %d, extendedCode: %d (0x%X)",
            exitCode,
            result.ResultCode,
            result.ExtendedResultCode,
            result.ExtendedResultCode);
    }

    Log_Info("Download output:: \n%s", output.c_str());

    // If we downloaded successfully, validate the file hash.
    if (IsAducResultCodeSuccess(result.ResultCode))
    {
        // Note: Currently we expect there to be only one hash, but
        // support for multiple hashes is already built in.
        Log_Info("Validating file hash");

        const bool isValid = ADUC_HashUtils_IsValidFileHash(
            fullFilePath.str().c_str(),
            ADUC_HashUtils_GetHashValue(entity->Hash, entity->HashCount, 0),
            algVersion,
            true /* suppressErrorLog */);

        if (!isValid)
        {
            Log_Error("Hash for %s is not valid", entity->TargetFilename);

            result.ResultCode = ADUC_Result_Failure;
            result.ExtendedResultCode = ADUC_ERC_VALIDATION_FILE_HASH_INVALID_HASH;
            reportProgress = true;
            goto done;
        }
    }

done:

    if (reportProgress && (downloadProgressCallback != nullptr))
    {
        if (IsAducResultCodeSuccess(result.ResultCode))
        {
            struct stat st;
            const off_t fileSize{ (stat(fullFilePath.str().c_str(), &st) == 0) ? st.st_size : 0 };
            downloadProgressCallback(
                workflowId, entity->FileId, ADUC_DownloadProgressState_Completed, fileSize, entity->SizeInBytes);
        }
        else
        {
            downloadProgressCallback(
                workflowId,
                entity->FileId,
                (result.ResultCode == ADUC_Result_Failure_Cancelled) ? ADUC_DownloadProgressState_Cancelled
                                                                     : ADUC_DownloadProgressState_Error,
                0,
                entity->SizeInBytes);
        }
    }

    if (IsAducResultCodeSuccess(result.ResultCode))
    {
        Log_Info(
            "Download job end. resultCode: %d, extendedCode: %d (0x%X)\n",
            result.ResultCode,
            result.ExtendedResultCode,
            result.ExtendedResultCode);
    }
    return result;
}
