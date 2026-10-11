# HLK-LD2410C
## Human presence sensing module — serial communication protocol

**Manufacturer:** Shenzhen Hi-Link Electronic Co., Ltd
**Version:** V1.00
**Modify date:** 2022-11-7
**Copyright:** © Shenzhen Hi-Link Electronic Co., Ltd

---

## Catalog

1. [Communication interface introduction](#1-communication-interface-introduction)
   - 1.1 [Pin definition](#11-pin-definition)
   - 1.2 [Use and configuration](#12-use-and-configuration)
     - 1.2.1 [Typical application circuits](#121-typical-application-circuits)
     - 1.2.2 [The role of configuration parameters](#122-the-role-of-configuration-parameters)
     - 1.2.3 [Visual configuration tool description](#123-visual-configuration-tool-description)
2. [Communication protocols](#2-communication-protocols)
   - 2.1 [Protocol format](#21-protocol-format)
     - 2.1.1 [Protocol data format](#211-protocol-data-format)
     - 2.1.2 [Command protocol frame format](#212-command-protocol-frame-format)
   - 2.2 [Send command with ACK](#22-send-command-with-ack)
     - 2.2.1 [Enabling configuration commands](#221-enabling-configuration-commands)
     - 2.2.2 [End configuration command](#222-end-configuration-command)
     - 2.2.3 [Maximum distance gate and unoccupied duration parameters configuration command](#223-maximum-distance-gate-and-unoccupied-duration-parameters-configuration-command)
     - 2.2.4 [Read parameter command](#224-read-parameter-command)
     - 2.2.5 [Enabling engineering mode command](#225-enabling-engineering-mode-command)
     - 2.2.6 [Close project mode command](#226-close-project-mode-command)
     - 2.2.7 [Distance gate sensitivity configuration command](#227-distance-gate-sensitivity-configuration-command)
     - 2.2.8 [Read firmware version command](#228-read-firmware-version-command)
     - 2.2.9 [Set serial port baud rate](#229-set-serial-port-baud-rate)
     - 2.2.10 [Restore factory settings](#2210-restore-factory-settings)
     - 2.2.11 [Restart module](#2211-restart-module)
     - 2.2.12 [Bluetooth settings](#2212-bluetooth-settings)
     - 2.2.13 [Get mac address](#2213-get-mac-address)
     - 2.2.14 [Obtaining bluetooth permissions](#2214-obtaining-bluetooth-permissions)
     - 2.2.15 [Setting Bluetooth password](#2215-setting-bluetooth-password)
     - 2.2.16 [Distance resolution setting](#2216-distance-resolution-setting)
     - 2.2.17 [Query distance resolution setting](#2217-query-distance-resolution-setting)
   - 2.3 [Radar data output protocol](#23-radar-data-output-protocol)
     - 2.3.1 [Reported data frame format](#231-reported-data-frame-format)
     - 2.3.2 [Target data composition](#232-target-data-composition)
   - 2.4 [Radar command configuration method](#24-radar-command-configuration-method)
     - 2.4.1 [Radar command configuration steps](#241-radar-command-configuration-steps)
3. [Revision records](#3-revision-records)
4. [Technical support and contact information](#4-technical-support-and-contact-information)

---

## Chart Index

- Table 1 — Pin definition table
- Table 2 — Send command protocol frame format
- Table 3 — Data format in the sending frame
- Table 4 — ACK command protocol frame format
- Table 5 — ACK intra-frame data format
- Table 6 — Serial port baud rate selection
- Table 7 — Factory default configuration values
- Table 8 — Distance resolution selection
- Table 9 — Reported data frame format
- Table 10 — Intra-frame data frame format
- Table 11 — Data type description
- Table 12 — Target basic information data composition
- Table 13 — Target state value description
- Table 14 — Engineering model target data composition

---

# 1 Communication interface introduction

## 1.1 Pin definition

**Table 1 — Pin definition table**

| Pin | Symbol  | Name                | Function                                                                       |
|-----|---------|---------------------|--------------------------------------------------------------------------------|
| 1   | UART_Tx | UART Tx             | UART Tx pin                                                                    |
| 2   | UART_Rx | UART Rx             | UART Rx pin                                                                    |
| 3   | OUT     | Target state output | Human presence detected: output high level / No human presence: output low level |
| 4   | GND     | Power Ground        | Power Ground                                                                   |
| 5   | VCC     | Power input         | Power input 5~12V (Suggest: 5V)                                                |

## 1.2 Use and configuration

### 1.2.1 Typical application circuits

LD2410C module directly through an IO pin output the detected target state (someone high, no one low), but also through the serial port in accordance with the prescribed protocol for the output of the detection results data. The serial output data contains the target state and distance auxiliary information, etc., the user can be used flexibly according to specific application scenarios.

The module power supply voltage is 5V and the power supply capacity of the input power supply is required to be greater than 200 mA.

The module IO output level is 3.3 V. The default baud rate of the serial port is 256000, with 1 stop bit and no parity bit.

### 1.2.2 The role of configuration parameters

Users can modify the configuration parameters to the module through the serial port of LD2410C to adapt to different application requirements.

The configurable radar detection parameters include the following:

**● The farthest detection distance**

Set the maximum detectable distance, only human targets that appear within this maximum distance will be detected and the result will be output.

Set up in units of distance gate, each distance gate is 0.75 m.

Including motion detection of the farthest distance gate and stationary detection of the farthest distance gate, can be set in the range of 1 to 8. For example, set the farthest distance gate to 2: only the presence of the human body within 1.5 m will be effectively detected and output the results.

**● Sensitivity**

The presence of a target is determined when the detected target energy value (range 0 to 100) is greater than the sensitivity value, otherwise it is ignored.

Sensitivity value can be set in the range of 0 to 100. Each distance gate can be set independently of the sensitivity, that is, the detection of different distances within the range of precise adjustment, local precision detection or filtering of specific areas of interference sources.

In addition, if the sensitivity of a distance gate is set to 100, the effect of not identifying the target under this distance gate can be achieved. For example, the sensitivity of distance gate 3 and distance gate 4 is set to 20, and the sensitivity of all other distance gates is set to 100, then only the human body within 2.25 to 3.75 m of the distance module can be achieved to detect.

**● No-one duration**

Radar in the output from occupied to unoccupied results, will continue to report a period of time on the occupied. If the radar test range in this time period continued unoccupied, the radar reported unoccupied; if the radar detects someone in this time period, then refreshed this time, unit seconds. Equivalent to no one delay time: after the person left, keep no one more than this duration before the output status for no one.

### 1.2.3 Visual configuration tool description

In order to facilitate users to quickly and efficiently test and configure the module, the PC terminal configuration tool is provided. Users can use this tool software to connect to the serial port of the module, read and configure the parameters of the module, and also receive the detection result data reported by the module, and make real-time visualization display, which is greatly convenient for users.

**Usage of the Uplink tool:**

1. Properly connect the module serial port with the USB-to-serial port tool.
2. Select the corresponding serial port number in the upper computer tool, set the baud rate 256000, select the engineering mode and click "connect the device".
3. After successful connection, click the "start" button; the right graphical interface will display the detection results and data.
4. After connection, when the start button is not clicked, or click stop after starting, the mode parameter information can be read or set.

**Note:** Parameters cannot be read and configured after clicking Start, and can only be configured after stopping.

In the visualization tool:

- The ball is the target status output indication: **red** means there is a moving target; **purple** means there is a stationary target; **green** means no one.
- **Green line:** the set sensitivity.
- **Blue line:** moving target energy value on each distance gate.
- **Red line:** static target energy value on each range gate.

---

# 2 Communication protocols

The LD2410C communicates with the outside world through a serial port (TTL level). Data output and parameter configuration commands of the radar are carried out under this protocol. The default baud rate of the radar serial port is 256000, 1 stop bit, no parity bit.

## 2.1 Protocol format

### 2.1.1 Protocol data format

The LD2410C uses **little-endian** format for serial data communication, and all data in the following tables are in hexadecimal.

### 2.1.2 Command protocol frame format

The format of the protocol-defined radar configuration commands and ACK commands are shown in Table 2 to Table 5.

**Table 2 — Send command protocol frame format**

| Frame header | Intra-frame data length | Intra-frame data | End of frame |
|--------------|-------------------------|------------------|--------------|
| FD FC FB FA  | 2 bytes                 | See Table 3      | 04 03 02 01  |

**Table 3 — Data format in the sending frame**

| Command word (2 bytes) | Command value (N bytes) |
|------------------------|-------------------------|

**Table 4 — ACK command protocol frame format**

| Frame header | Intra-frame data length | Intra-frame data | End of frame |
|--------------|-------------------------|------------------|--------------|
| FD FC FB FA  | 2 bytes                 | See Table 5      | 04 03 02 01  |

**Table 5 — ACK intra-frame data format**

| Send command word \| 0x0100 (2 bytes) | Return value (N bytes) |
|----------------------------------------|------------------------|

## 2.2 Send command with ACK

### 2.2.1 Enabling configuration commands

Any other commands issued to the radar must be executed after this command is issued, otherwise they are invalid.

- **Command word:** 0x00FF
- **Command value:** 0x0001
- **Return value:** 2 bytes ACK status (0 success, 1 failure) + 2 bytes protocol version (0x0001) + 2 bytes buffer size (0x0040)

**Send data:**

| FD FC FB FA | 04 00 | FF 00 | 01 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 08 00 | FF 01 | 00 00 | 01 00 | 40 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------|-------------|

### 2.2.2 End configuration command

End the configuration command and the radar resumes working mode after execution. If you need to issue other commands again, you need to send the enable configuration command first.

- **Command word:** 0x00FE
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 02 00 | FE 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | FE 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.3 Maximum distance gate and unoccupied duration parameters configuration command

This command sets the radar maximum detection distance gate (motion & stationary) (configuration range 2~8), and the unmanned duration parameter (configuration range 0~65535 seconds). Please refer to the specific parameter word table below. This configuration value is not lost when power is dropped.

- **Command word:** 0x0060
- **Command value:** 2-byte maximum motion distance gate word + 4-byte maximum motion distance gate parameter + 2-byte maximum standstill distance gate word + 4-byte maximum standstill distance gate parameter + 2-byte unoccupied duration word + 4-byte unoccupied duration parameter
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**0x0060 protocol parameter word:**

| Parameter name                 | Parameter word |
|--------------------------------|----------------|
| Maximum movement distance door | 0x0000         |
| Maximum resting distance door  | 0x0001         |
| No one duration                | 0x0002         |

**Send data:** maximum distance door 8 (motion & stationary), no one duration 5 seconds

| FD FC FB FA | 14 00 | 60 00 | 00 00 | 08 00 00 00 | 01 00 | 08 00 00 00 | 02 00 | 05 00 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|-------|-------------|-------|-------------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | 60 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.4 Read parameter command

This command allows you to read the current configuration parameters of the radar.

- **Command word:** 0x0061
- **Command value:** None
- **Return value:** 2 bytes ACK status (0 success, 1 failure) + header (0xAA) + max distance gate N (0x08) + configure max motion distance gate + configure max rest distance gate + distance gate 0 motion sensitivity (1 byte) + ... + distance gate N motion sensitivity (1 byte) + distance gate 0 rest sensitivity (1 byte) + ... + distance gate N stationary sensitivity (1 byte) + unoccupied duration (2 bytes)

**Send data:**

| FD FC FB FA | 02 00 | 61 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK** (success, maximum distance gate 8, configured motion distance gate 8, stationary distance gate 8, 0~8 motion sensitivity 20, 0~8 stationary sensitivity 25, unoccupied duration 5 seconds):

| Bytes      | Value           |
|------------|-----------------|
| Byte 1–4   | FD FC FB FA     |
| Byte 5, 6  | 1C 00           |
| Byte 7, 8  | 61 01           |
| Byte 9, 10 | 00 00           |
| Byte 11    | AA              |
| Byte 12    | 08              |
| Byte 13    | 08              |
| Byte 14    | 08              |
| Byte 15    | 14              |
| Byte 16    | 14              |
| Byte 17    | 14              |
| Byte 18    | 14              |
| Byte 19    | 14              |
| Byte 20    | 14              |
| Byte 21    | 14              |
| Byte 22    | 14              |
| Byte 23    | 14              |
| Byte 24    | 19              |
| Byte 25    | 19              |
| Byte 26    | 19              |
| Byte 27    | 19              |
| Byte 28    | 19              |
| Byte 29    | 19              |
| Byte 30    | 19              |
| Byte 31    | 19              |
| Byte 32    | 19              |
| Byte 33, 34| 05 00           |
| Byte 35–38 | 04 03 02 01     |

### 2.2.5 Enabling engineering mode command

This command opens the radar engineering mode. When the engineering mode is turned on, each distance gate energy value will be added to the radar report data, please refer to [2.3.2 Target Data Composition](#232-target-data-composition) for detailed format. Engineering mode is off by default after the module is powered on, this configuration value is lost when power is lost.

- **Command word:** 0x0062
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 02 00 | 62 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | 62 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.6 Close project mode command

This command turns off the radar engineering mode. After it is turned off, please refer to [2.3.2 Target Data Composition](#232-target-data-composition) for the format of radar report data.

- **Command word:** 0x0063
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 02 00 | 63 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | 63 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.7 Distance gate sensitivity configuration command

This command configures the sensitivity of the distance gate, and the configured value is not lost when power is dropped. It supports both configuring each distance gate individually and configuring all distance gates to a uniform value at the same time. If setting all distance gates sensitivity to the same value at the same time, the distance gate value needs to be set to 0xFFFF.

- **Command word:** 0x0064
- **Command value:** 2-byte distance gate word + 4-byte distance gate value + 2-byte motion sensitivity word + 4-byte motion sensitivity value + 2-byte standstill sensitivity word + 4-byte standstill sensitivity value
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**0x0064 protocol parameter word:**

| Parameter name           | Parameter word |
|--------------------------|----------------|
| Distance door            | 0x0000         |
| Movement sensitivity word| 0x0001         |
| Static Sensitivity Word  | 0x0002         |

**Send data:** configured distance gate 3, motion sensitivity 40, stationary sensitivity 40

| FD FC FB FA | 14 00 | 64 00 | 00 00 | 03 00 00 00 | 01 00 | 28 00 00 00 | 02 00 | 28 00 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|-------|-------------|-------|-------------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | 64 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Send data:** Configure motion sensitivity 40 for all distance doors, rest sensitivity 40

| FD FC FB FA | 14 00 | 64 00 | 00 00 | FF FF 00 00 | 01 00 | 28 00 00 00 | 02 00 | 28 00 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|-------|-------------|-------|-------------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | 64 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.8 Read firmware version command

This command reads the radar firmware version information.

- **Command word:** 0x00A0
- **Command value:** None
- **Return value:** 2 bytes ACK status (0 success, 1 failure) + 2 bytes firmware type (0x0001) + 2 bytes major version number + 4 bytes minor version number

> Note: the ACK example below has known transcription errors versus the protocol structure (Tables 4-5). Per the protocol, intra-frame data must start with the *command word ACK* (`sent_command | 0x0100`, i.e. bytes `A0 01` LE for command 0x00A0), which the example omits and the description above also fails to mention. The structurally correct positional layout (1-indexed bytes from frame start) is:
>
> | Bytes  | Field                              | Notes                                                  |
> |--------|------------------------------------|--------------------------------------------------------|
> | 1-4    | Frame header                       | `FD FC FB FA`                                          |
> | 5-6    | Intra-frame data length            | `0C 00` = 12                                           |
> | 7-8    | Command word ACK                   | `A0 01` LE — **missing in the example below**          |
> | 9-10   | ACK status                         | `00 00` = success                                      |
> | 11-12  | Firmware type                      | `01 00` LE = 0x0001 — example shows `00 01` (typo)     |
> | 13     | Minor version                      | e.g. `07` = 0x07                                       |
> | 14     | Major version                      | e.g. `01` = 0x01                                       |
> | 15-18  | Bugfix version (LE uint32)         | e.g. `16 15 09 22` → 0x22091516                        |
> | 19-22  | End of frame                       | `04 03 02 01`                                          |
>
> Implementations should anchor on the command word ACK at bytes 7-8 (offset 6-7 in 0-indexed buffers) — *not* on the example's leading `00 00`. With the example bytes, the version parses as `V1.07` plus bugfix `0x22091516`; the original Hi-Link version string `V1.07.22091615` has the last four hex digits transposed.

**Send data:**

| FD FC FB FA | 02 00 | A0 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 0C 00 | 00 00 | 00 01 | 07 01 | 16 15 09 22 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------------|-------------|

The corresponding version number is V1.07.22091615.

### 2.2.9 Set serial port baud rate

This command is used to set the baud rate of the serial port of the module. The configured value is not lost when power is lost, and the configured value takes effect after restarting the module.

- **Command word:** 0x00A1
- **Command value:** 2-byte baud rate selection index
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Table 6 — Serial port baud rate selection**

| Baud rate selection index value | Baud rate |
|---------------------------------|-----------|
| 0x0001                          | 9600      |
| 0x0002                          | 19200     |
| 0x0003                          | 38400     |
| 0x0004                          | 57600     |
| 0x0005                          | 115200    |
| 0x0006                          | 230400    |
| 0x0007                          | 256000    |
| 0x0008                          | 460800    |

The factory default value is 0x0007, which is 256000.

**Send data:**

| FD FC FB FA | 04 00 | A1 00 | 07 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A1 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.10 Restore factory settings

This command is used to restore all the configuration values to their non-factory values, which take effect after rebooting the module.

- **Command word:** 0x00A2
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 02 00 | A2 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A2 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

The factory default configuration values are as follows:

**Table 7 — Factory default configuration values**

| Configuration items            | Default value |
|--------------------------------|---------------|
| Maximum movement distance door | 8             |
| Maximum resting distance door  | 8             |
| No one duration                | 5             |
| Serial port baud rate          | 256000        |

| Configuration items                   | Default value     | Configuration items                   | Default value      |
|---------------------------------------|-------------------|---------------------------------------|--------------------|
| Motion sensitivity of distance gate 0 | 50                | Static sensitivity of distance gate 0 | – (not settable)   |
| Motion sensitivity of distance gate 1 | 50                | Static sensitivity of distance gate 1 | – (not settable)   |
| Motion sensitivity of distance gate 2 | 40                | Static sensitivity of distance gate 2 | 40                 |
| Motion sensitivity of distance gate 3 | 30                | Static sensitivity of distance gate 3 | 40                 |
| Motion sensitivity of distance gate 4 | 20                | Static sensitivity of distance gate 4 | 30                 |
| Motion sensitivity of distance gate 5 | 15                | Static sensitivity of distance gate 5 | 30                 |
| Motion sensitivity of distance gate 6 | 15                | Static sensitivity of distance gate 6 | 20                 |
| Motion sensitivity of distance gate 7 | 15                | Static sensitivity of distance gate 7 | 20                 |
| Motion sensitivity of distance gate 8 | 15                | Static sensitivity of distance gate 8 | 20                 |

### 2.2.11 Restart module

The module receives this command and will automatically restart after the answer is sent.

- **Command word:** 0x00A3
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 02 00 | A3 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A3 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.12 Bluetooth settings

This command is used to control the Bluetooth on or off. The Bluetooth function of the module is on by default.

After receiving this command, a reboot is required for the function to take effect.

- **Command word:** 0x00A4
- **Command value:** 0x0100 Turn on bluetooth / 0x0000 Turn off bluetooth
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:** (Turn on bluetooth)

| FD FC FB FA | 04 00 | A4 00 | 01 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A4 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.13 Get mac address

This command is used to query the MAC address.

- **Command word:** 0x00A5
- **Command value:** 0x0001
- **Return value:** 2-byte ACK status (0 success, 1 failure) + 1 byte fixed type (0x00) + 3 bytes MAC address (address is in big terminal order)

> Note: the documented payload returns 6 bytes of MAC address (the example below shows a 6-byte address).

**Send data:**

| FD FC FB FA | 04 00 | A5 00 | 01 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 0A 00 | A5 01 | 00 00 | 8F 27 | 2E B8 | 0F 65 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------|-------|-------------|

The MAC address queried is: `8F 27 2E B8 0F 65`.

### 2.2.14 Obtaining bluetooth permissions

This command is used to get the Bluetooth permission, and you can use the APP to get the device information and debugging parameters through Bluetooth after successful acquisition.

- **Command word:** 0x00A8
- **Command value:** 6 bytes of password value (every 2 bytes in little-endian order)
- **Return value:** 2-byte ACK status (0 success, 1 failure)

The default password is `"HiLink"`, then the corresponding value is 0x4869 (Hi) 0x4c69 (Li) 0x6e6b (nk).

**Send data:**

| FD FC FB FA | 08 00 | A8 00 | 48 69 | 4c 69 | 6e 6b | 48 69 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A8 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Note:** This response only answers to Bluetooth, not to the serial port.

### 2.2.15 Setting Bluetooth password

This command is used to set the password for Bluetooth control.

- **Command word:** 0x00A9
- **Command value:** 6 bytes of password value (each byte is in little-endian order)
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Send data:**

| FD FC FB FA | 08 00 | A9 00 | 48 69 | 4c 69 | 6e 6b | 48 69 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | A9 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.16 Distance resolution setting

Set the distance resolution of the module, that is how far away each distance gate represents. The configuration value is not lost when power is lost, and the configuration value takes effect after restarting the module.

Can be configured to 0.75 m or 0.2 m per distance gate; the maximum number of distance gates supported is 8.

- **Command word:** 0x00AA
- **Command value:** 2-byte distance resolution selection index
- **Return value:** 2-byte ACK status (0 success, 1 failure)

**Table 8 — Distance resolution selection**

| Distance resolution selection index value | Distance resolution (distance represented by each distance gate) |
|-------------------------------------------|------------------------------------------------------------------|
| 0x0000                                    | 0.75 m                                                           |
| 0x0001                                    | 0.2 m                                                            |

Factory default value is 0x0000, which is 0.75 m.

> Note: the original document text states "Factory default value is 0x0001, which is 0.75m", which is inconsistent with Table 8. Per the table mapping, 0x0000 corresponds to 0.75 m.

**Send data:**

| FD FC FB FA | 04 00 | AA 00 | 01 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 04 00 | AA 01 | 00 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------------|

### 2.2.17 Query distance resolution setting

Query the module's current distance resolution setting, i.e. how far away each distance gate represents.

- **Command word:** 0x00AB
- **Command value:** None
- **Return value:** 2-byte ACK status (0 success, 1 failure) + 2-byte distance resolution selection index

Return value definition is the same as Table 8 — Distance resolution selection.

**Send data:**

| FD FC FB FA | 02 00 | AB 00 | 04 03 02 01 |
|-------------|-------|-------|-------------|

**Radar ACK (success):**

| FD FC FB FA | 06 00 | AB 01 | 00 00 | 01 00 | 04 03 02 01 |
|-------------|-------|-------|-------|-------|-------------|

Represents the currently set distance resolution of 0.2 m.

## 2.3 Radar data output protocol

LD2410C outputs the radar detection result through serial port. The default output is basic target information, including target status, motion energy value, stationary energy value, motion distance, stationary distance and other information. If the radar is configured as engineering mode, the radar will additionally output each distance gate energy value (motion & stationary). Radar data is output in the prescribed frame format.

### 2.3.1 Reported data frame format

The format of the radar uplink message frames defined by the protocol is shown in Table 9 and Table 10. The definition of the report data type values in normal operation mode and engineering mode are shown in Table 11.

**Table 9 — Reported data frame format**

| Frame header | Length of data in the frame | Intra-frame data | End of frame |
|--------------|-----------------------------|------------------|--------------|
| F4 F3 F2 F1  | 2 bytes                     | See Table 10     | F8 F7 F6 F5  |

**Table 10 — Intra-frame data frame format**

| Data type             | Head | Target data            | Tail | Calibration |
|-----------------------|------|------------------------|------|-------------|
| 1 byte (See Table 11) | 0xAA | See Table 12, Table 14 | 0x55 | 0x00        |

**Table 11 — Data type description**

| Data type value | Description                   |
|-----------------|-------------------------------|
| 0x01            | Engineering mode data         |
| 0x02            | Target basic information data |

### 2.3.2 Target data composition

The content of the target data reported by the radar will change depending on the operating mode of the radar. In normal operation mode, the radar outputs the basic information data of the target by default; when configured to engineering mode, the radar adds each distance gate energy value information after the basic information data of the target. Therefore, the basic information of the target will always be output in the radar report data, while the distance gate energy value information needs to be enabled by command to be output.

The composition of the target data reported by the radar in normal operation mode is shown in Table 12, and the definition of the target state values is shown in Table 13. The composition of the target data frame in engineering mode is shown in Table 14, with additional data added to the data reported in normal operation mode.

**Table 12 — Target basic information data composition**

| Target Status         | Movement target distance (cm) | Exercise target energy value | Distance to stationary target (cm) | Stationary target energy value | Detection distance (cm) |
|-----------------------|-------------------------------|------------------------------|-------------------------------------|--------------------------------|-------------------------|
| 1 byte (See Table 13) | 2 bytes                       | 1 byte                       | 2 bytes                             | 1 byte                         | 2 bytes                 |

**Table 13 — Target state value description**

| Target state value | Description                  |
|--------------------|------------------------------|
| 0x00               | No target                    |
| 0x01               | Movement target              |
| 0x02               | Stationary target            |
| 0x03               | Movement & Stationary target |

**Table 14 — Engineering model target data composition**

Add the following data after the target basic information data in Table 12:

| ... | Maximum movement distance gate N | Maximum resting distance gate N | Movement distance gate 0 energy value | ... | Movement distance gate N energy value | Stationary distance gate 0 energy value | ... | Stationary distance gate N energy value | Retain data, store additional information |
|-----|----------------------------------|---------------------------------|---------------------------------------|-----|---------------------------------------|------------------------------------------|-----|------------------------------------------|-------------------------------------------|
| ... | 1 byte                           | 1 byte                          | 1 byte                                | ... | 1 byte                                | 1 byte                                   | ... | 1 byte                                   | M bytes                                   |

**Example of reported data:**

Data reported in normal operating mode:

| Frame header | Length of data in frame | Intra-frame data                          | End of frame |
|--------------|-------------------------|-------------------------------------------|--------------|
| F4 F3 F2 F1  | 0D 00                   | 02 AA 02 51 00 00 00 00 3B 00 00 55 00    | F8 F7 F6 F5  |

Data reported in engineering mode:

| Frame header | Length of data in frame | Intra-frame data                                                                                                | End of frame |
|--------------|-------------------------|-----------------------------------------------------------------------------------------------------------------|--------------|
| F4 F3 F2 F1  | 23 00                   | 01 AA 03 1E 00 3C 00 00 39 00 00 08 08 3C 22 05 03 03 04 03 06 05 00 00 39 10 13 06 06 08 04 03 05 55 00         | F8 F7 F6 F5  |

## 2.4 Radar command configuration method

### 2.4.1 Radar command configuration steps

The process of executing a configuration command by LD2410C radar consists of two parts: the upper computer "sends the command" and the radar "replies to the command ACK". If the radar does not reply with ACK or fails to reply with ACK, it means the radar fails to execute the configuration command.

As mentioned earlier, before sending any other commands to the radar, the developer needs to send the "enable configuration" command and then send the configuration command within the specified time. After the commands are configured, the "end configuration" command is sent to inform the radar that the configuration is finished.

For example, if you want to read the radar configuration parameters, first the host computer sends the "enable configuration" command; after receiving a successful radar ACK, then sends the "read parameters" command; after receiving a successful radar ACK, then sends the "end configuration" command; after receiving successful radar ACK, it indicates that the complete action of reading parameters is finished.

The radar command configuration flow is as follows:

```
            ┌───────┐
            │ Start │
            └───┬───┘
                │
                ▼
   Send the "enable command configuration" command
                │
                ▼
        Received radar ACK success ── NO ──┐
                │ YES                       │
                ▼                           │
   Send command (Multiple bars available)   │
                │                           │
                ▼                           │
        Received radar ACK success ── NO ──┤
                │ YES                       │
                ▼                           │
       Send "End command configuration"     │
                │                           │
                ▼                           │
        Received radar ACK success ── NO ──┤
                │ YES                       │
                ▼                           ▼
            ┌───────┐                    ┌───────┐
            │  End  │ ◄──────────────────┤  End  │
            └───────┘                    └───────┘
```

*Figure — Radar command configuration process*

---

# 3 Revision records

| Date       | Version | Modify the content |
|------------|---------|--------------------|
| 2022-11-7  | 1.00    | Initial version    |

---

# 4 Technical support and contact information

**Shenzhen Hi-Link Electronic Co., Ltd**

- **Tel:** 0755-23152658 / 83575155
- **Website:** [www.hlktech.com](http://www.hlktech.com)
