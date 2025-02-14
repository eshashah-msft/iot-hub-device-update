# How To Build the Device Update Agent Snap (for Ubuntu 20.04)


## Prerequisites
- Learn about Snapcraft here https://snapcraft.io/docs/getting-started#heading--learning
- Learn about `snapcraft.yaml` schema here https://snapcraft.io/docs/snapcraft-yaml-reference
- 

## Build Environment
- Ubuntu 20.04
    - The Ubuntu 20.04 image can be found at https://releases.ubuntu.com/20.04/
- snapd, snapcraft, and [multipass](https://snapcraft.io/docs/build-options) packages
    - `snapd` is pre-installed on Ubuntu 20.04 LTS by default. For more information about `snapd`, see https://snapcraft.io/docs/installing-snapd
    - To install `snapcraft` use following command:
        ```shell
        sudo snap install snapcraft --classic
        ```
    - To install `multipass` use following command:
        ```shell
        sudo snap install multipass --classic
        ```
        For more infomation about `Multipass`, see https://multipass.run/
- KVM enabled
    - To enable KVM on a Linux Hyper-V VM, run following Powershell (as an Administrator) command on host PC
        ```powershell
        Set-VMProcessor -VMName <VMName> -ExposeVirtualizationExtensions $true
        ```
        For more detail, see [Windows 10 Nested Virtualization](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/nested-virtualization)

## Build Instruction

```shell
# change directory to iot-hub-device-update project root directory
pushd <project root>

# run 'snapcraft' to build the snap package using <project root>/snap/snapcraft.yaml
#
# Note: if a build error occurs, you can run snapcraft --debug to troubleshoot the error.
snapcraft

```

## Incremental Build

To rebuild on specific part(s), you can use `snapcraft clean <PART_NAME>` command follow by `snapcraft build` command


### Build Output

If build success, you can find `deviceupdate-agent_#.#_amd64.snap` at the project root directory.

### How To View Snap Package Content

You can use `unsquashfs` command to extract `.snap` file.

```shell
# Run unsquashfs <snap file name>
# e.g., unsquashfs deviceupdate-agent.0.1_amd64.snap
unsquashfs deviceupdate-agent.0.1_adm64.snap

# The content will be extracted to 'squashfs-root' directory
# You can use 'tree' command to view the directory content
tree squashfs-root
```

## Install The Snap

To install the snap for testing (developer mode)

```shell
sudo snap install --devmode ./deviceupdate-agent_0.1_amd64.snap 
```

> NOTE | This will installs the snap content to /snap/<'snap name'>/ directory on the host device

Although installing and running a snap in dev_mode is useful, it is advisable to test the snap's functionality under strict confinement. This will help verify that all necessary permissions and connections have been correctly configured.

In order to achieve this, the confinement setting should be modified to strict. After rebuilding the snap, it can be installed using the --dangerous option.

For example:

```shell
sudo snap install --dangerous ./deviceupdate-agent_0.1_amd64.snap 
```

> NOTE | You can find more information about the --dangerous option and other snap installation options in the official Snapcraft documentation:<br/>
https://snapcraft.io/docs/install-modes#heading--dangerous


## Configure The Device Update Agent Snap

Specify an agent settings, such as, manufacturer, model, agent.name, agent.connectionSource, etc., in `du-config.json`, which localted in `$SNAP_DATA/config` folder.

```json
{
  "schemaVersion": "1.1",
  "aduShellTrustedUsers": [
    "snap_aziotdu",
    "snap_aziotdo"
  ],
  "iotHubProtocol": "mqtt",
  "manufacturer":"<Place your device info manufacturer here>",
  "model": "<Place your device info model here>",
  "agents": [
    {
      "name": "<Place your agent name here>",
      "runas": "snap_aziotdu",
      "connectionSource": {
        "connectionType": "string",
        "connectionData": "HostName=...HIDDEN..."
      },
      "manufacturer": "<Place your device property manufacturer here>",
      "model": "<Place your device property model here>"
    }
  ]
}
```

For example:
- "manufacturer":"contoso"
- "model":"vacuum-v1"
- "agents[0].name":"main"
- "agents[0].connectionSource.connectionType":"string"
- "agents[0].connectionSource.connectionData":"**enter your device connection string here**"
- "agents[0].manufacturer":"contoso"
- "agents[0].model":"vacuum-v1"

>TIP: To share file between host machine and the snap, you can use a ['home' interface](https://snapcraft.io/docs/home-interface).

- To share 'home' directory with the snap, run:
    ```shell
    $ snap connect deviceupdate-agent:home :home
    ```
- Create du-config.json file in your home directory like so:
    ```json
    {
        "schemaVersion": "1.1",
        "aduShellTrustedUsers": [
            "snap_aziotdu",
            "snap_aziotdo"
        ],
        "iotHubProtocol": "mqtt",
        "manufacturer": "contoso",
        "model": "vacuum-v1",
        "agents": [
            {
            "name": "main",
            "runas": "snap_aziotdu",
            "connectionSource": {
                "connectionType": "string",
                "connectionData": "HostName=...HIDDEN..."
            },
            "manufacturer": "contoso",
            "model": "vacuum-v1"
            }
        ]
    }
    ```

> NOTE | Please refer to [Provisioning Device Update Agent Snap](../../snap/local/README.md#provisioning-device-update-agent-snap) for an alternative approach, as the method described earlier may not be effective when using snap in 'strict' confinement.

## Run The DU Agent Snap

To run the "deviceupdate-agent" snap package with elevated privileges (using sudo command) in a Linux-based operating system that supports snap package management, use following command:

```shell
$ sudo snap run deviceupdate-agent
```
To run the "deviceupdate-agent' snap and open a shell environment:

```shell
$ sudo snap run --shell deviceupdate-agent
```

When you run the snap run --shell command followed by the name of a snap package, it launches a shell with an environment set up for that specific snap. This allows you to execute commands and access files within the snap package's environment, as if you were running them directly on the host system.

Optionally (for --devmode), copy the prepared `du-config.json` from [host machine] home directory to `$SNAP_DATA/config` directory

```shell
$ cp /path/to/your/du-config.json $SNAP_DATA/config
```

Inspect some snap variables:

```shell
root@myhost:~# printenv SNAP
/snap/deviceupdate-agent/x1

root@myhost:~# printenv SNAP_COMMON
/var/snap/deviceupdate-agent/common

root@myhost:~# printenv SNAP_DATA  
/var/snap/deviceupdate-agent/x1
```

For testing purposes, you can manually run `$SNAP/usr/bin/AducIotAgent -l 0 -e` to verify that the Device Update agent is successfully connected to, and communicated with, the Azure Iot Hub.

```shell
$SNAP/usr/bin/AducIotAgent -l 0 -e

2022-11-30T15:19:17.5333Z 33853[33853] [D] Log file created: /var/log/adu/du-agent.20221130-151917.log [zlog_init]
2022-11-30T15:19:17.5338Z 33853[33853] [I] Agent (linux; 1.0.0) starting. [main]

...

2022-11-30T15:19:17.5339Z 33853[33853] [I] Supported Update Manifest version: min: 4, max: 5 [main]
2022-11-30T15:19:17.5340Z 33853[33853] [I] Attempting to create connection to IotHub using type: ADUC_ConnType_Device  [ADUC_DeviceClient_Create]
2022-11-30T15:19:17.5341Z 33853[33853] [I] IotHub Protocol: MQTT [GetIotHubProtocolFromConfig]
2022-11-30T15:19:17.5341Z 33853[33853] [I] IoTHub Device Twin callback registered. [ADUC_DeviceClient_Create]
2022-11-30T15:19:17.5341Z 33853[33853] [I] Initializing PnP components. [ADUC_PnP_Components_Create]
2022-11-30T15:19:17.5341Z 33853[33853] [I] ADUC agent started. Using IoT Hub Client SDK 1.7.0 [AzureDeviceUpdateCoreInterface_Create]
2022-11-30T15:19:17.5341Z 33853[33853] [I] Calling ADUC_RegisterPlatformLayer [ADUC_MethodCall_Register]

...

2022-11-30T15:19:17.5702Z 33853[33853] [I] Agent running. [main]

...

2022-11-30T15:19:18.1976Z 33853[33853] [D] IotHub connection status: 0, reason: 6 [ADUC_ConnectionStatus_Callback]
-> 15:19:18 SUBSCRIBE | PACKET_ID: 2 | TOPIC_NAME: $iothub/twin/res/# | QOS: 0

...

2022-11-30T15:19:18.5998Z 33853[33853] [I] Processing existing Device Twin data after agent started. [ADUC_PnPDeviceTwin_Callback]
2022-11-30T15:19:18.5999Z 33853[33853] [D] Notifies components that all callback are subscribed. [ADUC_PnPDeviceTwin_Callback]

...

2022-11-30T15:19:18.6001Z 33853[33853] [D] Queueing message (t:0, c:0xbb236af0 [ADUC_D2C_Message_SendAsync]
2022-11-30T15:19:18.6002Z 33853[33853] [I] DeviceInformation component is ready - reporting properties [DeviceInfoInterface_Connected]
2022-11-30T15:19:18.6003Z 33853[33853] [I] Property manufacturer changed to contoso [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6004Z 33853[33853] [I] Property model changed to vacuum-v1 [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6006Z 33853[33853] [I] Property osName changed to Ubuntu Core [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6007Z 33853[33853] [I] Property swVersion changed to 20 [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6007Z 33853[33853] [I] Property processorArchitecture changed to x86_64 [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6242Z 33853[33853] [I] Property processorManufacturer changed to GenuineIntel [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6243Z 33853[33853] [I] Property totalMemory changed to 5776704 [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6244Z 33853[33853] [I] Property totalStorage changed to 64768 [RefreshDeviceInfoInterfaceData]
2022-11-30T15:19:18.6245Z 33853[33853] [D] Queueing message (t:2, c:0xbb2306a0 [ADUC_D2C_Message_SendAsync]
2022-11-30T15:19:18.6245Z 33853[33853] [I] DiagnosticsInterface is connected [DiagnosticsInterface_Connected]
2022-11-30T15:19:18.7248Z 33853[33853] [D] Sending D2C message (t:0, retries:0). [ProcessMessage]
2022-11-30T15:19:18.7250Z 33853[33853] [D] Sending D2C message:
{"deviceUpdate":{"__t":"c","agent":{"deviceProperties":{"manufacturer":"contoso","model":"vacuum-v1","interfaceId":null,"contractModelId":"dtmi:azure:iot:deviceUpdateContractModel;2","aduVer":"DU;agent\/1.0.0"},"compatPropertyNames":"manufacturer,model"}}} [ADUC_D2C_Default_Message_Transport_Function]
2022-11-30T15:19:18.7250Z 33853[33853] [D] Sending D2C message (t:2, retries:0). [ProcessMessage]
2022-11-30T15:19:18.7250Z 33853[33853] [D] Sending D2C message:
{"deviceInformation":{"__t":"c","manufacturer":"contoso","model":"vacuum-v1","osName":"Ubuntu Core","swVersion":"20","processorArchitecture":"x86_64","processorManufacturer":"GenuineIntel","totalMemory":5776704,"totalStorage":64768}} [ADUC_D2C_Default_Message_Transport_Function]

...

2022-11-30T15:19:20.5271Z 33853[33853] [D] Send message completed (status:3) [OnDeviceInfoD2CMessageCompleted]

...

```

Verify that the Device Twin contains properties reported by the agent:

```json
{
    "deviceId": "...",
    
    ...

    "modelId": "dtmi:azure:iot:deviceUpdateModel;2",
    "version": 10,
    "tags": {
        "ADUGroup": "ucore-test-1"
    },
    "properties": {
        "desired": {
            ...
        },
        "reported": {
            "deviceUpdate": {
                "__t": "c",
                "agent": {
                    "deviceProperties": {
                        "manufacturer": "contoso",
                        "model": "vacuum-v1",
                        "contractModelId": "dtmi:azure:iot:deviceUpdateContractModel;2",
                        "aduVer": "DU;agent/1.0.0"
                    },
                    "compatPropertyNames": "manufacturer,model"
                }
            },
            "deviceInformation": {
                "__t": "c",
                "manufacturer": "contoso",
                "model": "vacuum-v1",
                "osName": "Ubuntu Core",
                "swVersion": "20",
                "processorArchitecture": "x86_64",
                "processorManufacturer": "GenuineIntel",
                "totalMemory": 5776704,
                "totalStorage": 64768
            },
            
            ...

            "$version": 8
        }
    },
    "capabilities": {
        "iotEdge": true
    },
    "deviceScope": "ms-azure-iot-edge://ucore-test-1-638054160455707418"
}
```

## View Snap Logs using Journalctl

The name of the Device Update agent in the confined environment is `snap.deviceupdate-agent.deviceupdate-agent`.

To view the journal log, use following command:

```shell
$ journalctl -u snap.deviceupdate-agent.deviceupdate-agent -f --no-tail
```

To start the daemon:

```shell
$ sudo systemctl start  snap.deviceupdate-agent.deviceupdate-agent
```

To stop the daemon:

```shell
$ sudo systemctl stop  snap.deviceupdate-agent.deviceupdate-agent
```

To restart the daemon:

```shell
$ sudo systemctl restart  snap.deviceupdate-agent.deviceupdate-agent
```
