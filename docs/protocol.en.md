# Table of contents
- [MODBUS WIRENBOARD EXTENSION](#modbus-wirenboard-extension)
     - [Bus scan](#bus-scan)
       - [Scan start function - 0x01](#Scan-start-function---0x01)
       - [Continue scanning function - 0x02](#continue-scanning-function---0x02)
       - [Scan response function - 0x03](#scan-response-function---0x03)
       - [Scan end function - 0x04](#end-of-scan-function---0x04)
     - [Scan order](#scan-order)
       - [Arbitration in scanning](#arbitration-in-scanning)
     - [Working with devices by serial number](#working-with-devices-by-serial-number)
       - [Standard command sending function - 0x08](#standard-command-sending-function---0x08)
       - [Standard command response fuction - 0x09](#standard-command-response-function---0x09)
     - [How arbitration works](#how-arbitration-works)
       - [Timings](#timings)
     - [Events](#events)
       - [Event request function - 0x10](#event-request-function---0x10)
       - [Event transfer function - 0x11](#event-transfer-function---0x11)
       - [Response function if there are no events - 0x12](#response-function-if-there-are-no-events---0x12)
     - [Event polling order](#event-polling-order)
       - [Arbitration in events](#arbitration-in-events)
       - [Timeout waiting for response in events](#timeout-waiting-for-response-in-events)
     - [Event confirmation](#event-confirmation)
     - [Event sources](#event-sources)
       - [Notifications about case changes](#case-change-notifications)
       - [Device reset detection](#device-reset-detection)
       - [Event sending configuration function - 0x18](#event-sending-configuration-function---0x18)

# MODBUS WIRENBOARD EXTENSION

This Modbus implementation supports extended commands that enhance bus and device operations. Commands are sent to a group of devices, with the devices themselves determining who will respond.

To use extended functions, specific commands are reserved, within which a subcommand defines the message type:

- `0x46` – used for event polling; recommended for all extended commands.
- `0x60` – deprecated, previously used in the scanning protocol. It is reserved in the Modbus specification and therefore not recommended for use.

If the scanning algorithm or its timing characteristics change, a new subcommand will be assigned for scanning.

It was observed that some devices, such as WB-MS, may respond to the broadcast address (`0`) with an unknown command type. To prevent this, extended mode operates through the reserved address `0xFD`.

## Bus scan

After powering on, the device assumes it has not been scanned. Only devices that consider themselves unscanned can be scanned. To reset the scan status, the scan start function `0x01` is used. Once a device responds with `0x03`, it marks itself as scanned and will only respond with `0x04` (end of scan) if it wins arbitration again—indicating that no unscanned devices remain on the bus.

### Scan start function - 0x01

Upon receiving this command, devices reset their scan status and consider themselves unscanned. All devices then participate in arbitration, with the device having the lowest serial number sending a scan response containing its data.

The command format:

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for extended functions  
- (1 byte) `0x01` – subcommand to start scanning  
- (2 bytes) checksum

Example:

```
-> FD 46 01 13 90
```

### Continue scanning function - 0x02

The client can continue scanning the bus by sending this command. Devices that have not yet provided their information will participate in arbitration, and the device with the lowest serial number will send a scan response containing its information.

The command format:

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for extended functions  
- (1 byte) `0x02` – subcommand for scanning one device  
- (2 bytes) checksum  

Example:
```
-> FD 46 02 53 91
```

### Scan response function - 0x03

After 3.5 frames of silence on the bus, all devices receive the command — regardless of the response delay setting — and arbitration occurs. This arbitration appears as periodic bytes with the value `0xFF` being sent on the bus. After arbitration, the devices decide which one will transmit information and determine whether any devices remain unscanned. The device that wins arbitration sends a packet with its information using the following structure:

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for extended functions  
- (1 byte) `0x03` – subcommand for scan response  
- (4 bytes) device serial number (big endian)  
- (1 byte) modbus device address  
- (2 bytes) checksum  

Example:
```
<-FF FF FF FF FF FF FF FF FF FD 46 03 00 01 EB 37 0C CE DC
```

In this case, the client ignores bytes with the value `0xFF` until it receives a response frame. Based on the client's response, it is important to record all detected devices and their Modbus addresses. If a duplicate Modbus address is found, it can be changed by identifying the device via its serial number.

Between scanning requests, the client can execute any command—for example, reading information about the device or changing the address by accessing the device via its serial number.

### End of scan function - 0x04

The client repeats the scan request until all devices are found. If no new devices remain, the device with the lowest serial number will respond, indicating that there are no new devices. This approach prevents the client from having to wait for a timeout.

The command format:
- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for extended functions  
- (1 byte) `0x04` – subcommand indicating scanning completion  
- (2 bytes) checksum

Example:
```
<- FF FF FF FF FF FF FF FF FF FD 46 04 D3 93
```

The scan request can be repeated periodically, allowing the system to detect when a new device is connected or when one of the devices has rebooted.

## Scan order

The client selects the speed and parity settings and sends the start scan command (`0x01`) with them. Next, wait the standard time (3.5 frames); after that, if there are devices on the bus, one of them will return a scan response with the command `0x03`. All preceding `0xFF` bytes must be ignored. If no response is received after the waiting period, then there are no devices on the bus with these connection settings, and you can proceed to scan using other port settings. If a response is received, you can send the command to continue scanning (`0x02`) to receive data about the next device. This process can be repeated until the scan is completed (`0x04`).

### Arbitrage in scanning

You can read more about the arbitration algorithm below.

Initially, there may be devices on the bus with the same addresses. Therefore, during a scan request, arbitration is performed based on each device's unique serial number and spans 32 **arbitration windows**.

For the scan command (`0x60`), legacy arbitration rules apply: the window duration is 20 bits at the current baud rate, and arbitration begins 44 bits (3.5 bytes) after the last received request byte.

Scanning one device takes the following interval:
- (5 bytes × 10 bits per byte) for the request  
- plus 44 bits of wait time for a response  
- plus ((28 + 4) arbitration fields × 20 bits per arbitration window)  
- plus (10 bytes × 10 bits per byte) for the response  

This totals 834 bits, which, at a speed of 115200, lasts approximately 7.5 ms.

> To speed up code execution in the interrupt, we aim to use a 32-bit architecture and restrict ourselves to a 32-bit arbitration field. However, in addition to the serial number, the high bits must convey a flag indicating the presence of new devices—and in the future, under decentralized regimes, possibly additional information. To accomplish this, you need to allocate fewer bits for the serial number (for example, 28 bits) and limit device serial numbers to values up to 2^28. This still allows for a large number of devices (and can be extended to the CAN bus).

> If you implement this protocol in a DIY device, select a serial number in the range **0x0D000000 - 0x0D00FFFF**.

## Working with devices by serial number

If multiple devices share the same Modbus address, you cannot operate with them using regular Modbus functions. For example, after scanning the bus, you may want to request device models or firmware versions before changing their addresses. In such cases, you can communicate with a device using its serial number. The device's address becomes known during the scanning stage or is available on a sticker. It is also possible to encapsulate a standard command within a packet that uses the serial number instead of a Modbus address.

### Standard command sending function - 0x08

- (1 byte) `0xFD` broadcast address  
- (1 byte) `0x46` command for working with extended functions  
- (1 byte) `0x08` subcommand for emulating standard requests  
- (4 bytes) serial number of the device we are accessing (big endian)  
   --- normal PDU ---  
- (1 byte) standard code for the function of working with registers (1-4, 5, 6, 15, 16)  
   ...request body  
   --- normal request ---  
- (2 bytes) checksum

Example:
```
-> FD 46 08 00 01 EB 37 03 00 C8 00 14 5B 07
```

### Standard command response function - 0x09

As a result, the device responds with the following packet:

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for working with extended functions  
- (1 byte) `0x09` – subcommand for emulating standard requests  
- (4 bytes) serial number of the device we are accessing (big endian)  
   --- normal PDU ---  
- (1 byte) standard function code for working with registers (1-4, 5, 6, 15, 16)  
   ... response body  
   --- normal request ---  
- (2 bytes) checksum

Example:
```
<- FD 46 09 00 01 EB 37 03 28 00 57 00 42 00 4D 00 53 00 57 0034 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 30 4F
```

## How arbitration works

Arbitration is a method for devices to decide among themselves who will respond to a request and to determine whether there are more devices willing to respond. Arbitration is based on a unique value for each device (for example, a serial number or `server_id`, depending on the command). The principle is similar to arbitration in the CAN bus: each device transmits one bit of its unique value in sequence, starting with the most significant bit (MSB). When a device transmitting a recessive bit (1) detects a dominant bit (0) on the bus, it loses arbitration. By the end of the transmission of the unique value, only one device remains as the winner of the arbitration.

Arbitration bits are transmitted one at a time into an **arbitration window** (a time interval). The dominant bit is transmitted by sending the value **0xFF** using normal USART transmission and hardware control of the bus driver. A recessive bit means that nothing is transmitted during the window interval—the bus remains silent, and the transceiver does not turn on. This approach bypasses the hardware limitations of RS485, which prevent simultaneous transmission of different states. The receiver circuitry is standard and can be disabled during transmission without affecting the protocol's operation.

Before transmitting a byte, the state of the USART BSY flag is checked. If one device has already transmitted the start bit, all the others remain silent during the current **arbitration window**, indicating that a dominant bit has been transmitted. Thus, a timer mismatch of several bits is acceptable.

### Timings

The duration of the arbitration window and the timeout of the first window depend on the command and bus settings. This is necessary to ensure stable operation at different speeds.

The entire process of sending a response to a request can be divided into three phases:

1. **Waiting for the start of arbitration** – During this time, devices must wait for the previous packet to finish, process the request initially, and determine whether they need to participate in arbitration.
2. **Arbitration**
3. **Sending a data package**

The waiting time for the start of arbitration is limited to below 800 μs. This duration is sufficient to process an interrupt on a controller with an 8 MHz clock (selected experimentally) and to transmit 3 characters (as specified by Modbus, though there are actually 3.5 characters).

The duration of the arbitration window is defined as the reception time of 12 bits (one UART frame with 2 stop bits and a parity bit) plus a time reserve for interrupt processing. The time reserve for interrupts is limited to below 50 μs (selected experimentally) and is a multiple of the transmission time of one bit.

The timeout for receiving a response is calculated as `waiting for the start of arbitration + duration of the arbitration window * number of bits in arbitration`. For scanning, the number of arbitration bits is 32.

_To poll for events, you can use an optimized timeout value—see details below._

Formula for calculating the timeout:

```
max(3.5 symbols, (12 bits + 800us)) + N * max(13 bits, 12 bits + ceil_bits(50us))
```

where `N` is the number of arbitration windows, and `ceil_bits(x)` rounds up to a multiple of the bit transmission length. For example, for a boost rate of 57600 (with a transmission time of one bit equal to 17 µs), it will be calculated as `ceil(50 / 17) = ceil(2.88) = 3`.

## Events

The library allows you to poll events quickly as they occur on devices, without having to poll each device individually. **This is only possible if there are no devices on the network with the same server ID.**

The following functions are provided for event polling: `event query`, `event transmission` and `no events`.

### Event Request Function - 0x10

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for working with extended functions  
- (1 byte) `0x10` – subcommand: request events from devices  
- (1 byte) Minimum server ID from which to start responding  
- (1 byte) Maximum length of the data field with events that the client expects; according to the standard, the entire packet should not exceed 256 bytes.  
- (1 byte) `server_id` of the device from which the previous event packet was received  
- (1 byte) Flag from the previously received packet to confirm reception (see below)  
- (2 bytes) Checksum  

If the specified maximum length of the data field does not fit a single event, the response will be sent without a payload (list of events); in that case, only the number of new events can be determined.

A flag must be included in this request to confirm the receipt of the previous packet for the given `server_id`; otherwise, the events will not be reset.

An example without a `server_id` limit, with an event field length of 100 bytes: the previous event packet was received from device `0x0A` with bit 1.

```
-> FD 46 10 00 64 0A 01 XX XX
```

### Event transfer function - 0x11

- (1 byte) `server_id` device  
- (1 byte) `0x46` command for working with extended functions  
- (1 byte) `0x11` subcommand – transmission of events from the device  
- (1 byte) flag of this packet to confirm receipt (see below)  
- (1 byte) number of unreset events  
- (1 byte) length of the data field of all events in bytes before the controlamounts  
- (4 or more bytes) event 1  
   ...  
- (4 or more bytes) event N  
- (2 bytes) checksum

The event contains 4 or more bytes and has the following format:

- (1 byte) Length of additional event data  
- (1 byte) Event type  
- (2 bytes) Event identifier (big endian)  
- (0 or more bytes) Additional event data (little endian format)

**Example:** A device with address `5` responds with an event indicating a change in input register `0x01D0` with a new value of `4`. One event remains unsent, and the packet confirmation flag is set.
```
<- 05 46 11 01 01 06 02 04 01 D0 04 00 XX XX
```

### Response function if there are no events – 0x12

This packet is sent by the device that wins the arbitration (lowest priority token + `server_id`).

- (1 byte) `0xFD` – broadcast address  
- (1 byte) `0x46` – command for working with extended functions  
- (1 byte) `0x12` – subcommand: no events  
- (2 bytes) checksum

Example:
`<- FD 46 12 52 5D`

The cycle of an event request and device response for one event with 2 bytes of data takes **42** frames, which corresponds to **48.125** ms at 9600 baud and **4.01** ms at 115200 baud. Note that this is the duration of one exchange cycle; the interval between polling requests depends on the port speed and is selected automatically (see below).

## Event polling order

The polling cycle can begin after the completion of any command. The event polling interval is adaptive and depends on the port speed: 
- **50 ms** at **115200 baud** and higher  
- **100 ms** at **38400…115199 baud**  
- **200 ms** at lower speeds
- 
The controller automatically selects the interval based on serial port timing. The request starts with the client sending a packet with the `0x10` event request command. If there is nothing to confirm, set the confirmation field to the 0 address 0 flag.

Devices conduct arbitration over the standard 3.5 frames (see description below). If some devices have events to report, the device that wins arbitration responds with a packet using the `0x11` command, transmitting a list of events. Each event includes an identifier, a type, and optionally additional data. According to the standard, the maximum packet size is 256 bytes. If the events do not fit into a single packet, the device will continue to win arbitration on the next request and transmit the remaining events.

Afterwards, the client can repeat the cycle and request events again.

Confirmation of receipt of events by the client is performed through the parameters in the following request, to save time when polling (see "Confirmation of event receipt").

If a device is transmitting too many events and preventing others from reporting their events, the client can specify a server ID greater than that of the busy device in the request packet. This allows that device (and any others with a `server_id` lower than the specified one) to pass.

### Arbitration in events

For arbitration when polling events, the concatenation of the packet priority marker (*not to be confused with the event priority*) (4 bits) and the device `server_id` (8 bits) is used.

> It is better to use `server_id` instead of the device serial number, because arbitration takes 4 times less time, and at the time of setting up event polling, unique `server_id` values are already expected for all devices.

At the beginning, a priority marker allows devices with higher priority events to win arbitration earlier, thereby reducing latency for those events. Also, the lowest priority token (`0xF`) is used by devices when there are no new events, which allows one of the devices to indicate that there are no events (command `0x12`).

Currently, there are two available event priorities (`HIGH` and `LOW`).

### Timeout waiting for response in events

An event polling implementation can use a standard timeout formula where 12 arbitration bits are expected (4 priority marker bits and 8 `server_id` bits). In practice, you can use a hack that reduces this time.

Recall that in arbitration, bit 0 is dominant (i.e., higher priority). The recessive bit (1) is not actually sent to the bus during arbitration.

In fact, the response timeout is defined as the time from sending the request until the first character is received on the bus, which can be either the dominant arbitration bit or the start of the message. In `server_id` arbitration, the most significant bits (MSB) will appear first.

`server_id` in Modbus is a value from 1 to 247.

- For values from 1 to 127, the binary representation in 8 bits begins with `0` — meaning the first character will be received on the bus in the **fifth** arbitration window (the first 4 bits are reserved for the priority marker).
- For values from 128 to 191, the binary representation begins with `10` — the first character appears in the **sixth** arbitration window.
- For values from 192 to 223, the binary representation begins with `110` — the first character appears in the **seventh** window.
- For values from 224 to 239, the binary representation begins with `1110` — the first character appears in the **eighth** window.
- For values from 240 to 247, the binary representation begins with `11110` — the first character appears in the **ninth** window.
- Values greater than 247 are not allowed.

This means that if no frame is received after waiting through 9 arbitration windows, there are no devices on the bus participating in arbitration.

Thus, it is possible to reduce the waiting time from 12 arbitration windows to 9 (plus the waiting time for arbitration to start).

## Event confirmation

Delivery guarantee is implemented by a procedure that confirms the receipt of event packets for each device.

Each event packet includes a flag field carrying a package number with a value of 0 or 1. Every subsequent packet from the same device toggles the flag to the opposite value relative to the previous one. Thus, no two consecutive packets from the same device will have the same flag value. Flags from different devices are independent.

When the client correctly receives an event packet, it must acknowledge the receipt of the events to the device. The client must remember the last flag value for each device. In the next event request cycle, using the `0x10` request command, the client includes in the confirmation field the server ID of the device and the flag value that was specified in the previously received event packet. All devices receive this packet—even if they did not win the arbitration—and if a device sees its `server_id` in the confirmation field, it discards the previously sent batch of events (with the corresponding confirmation flag).

Possible errors during confirmation:

- **Error when transmitting from a client with one device:**
  - Devices will not respond. The client will experience a timeout and resend the request with the same confirmation field.

- **Error when transmitting from a server with one device:**
  - The client will accept a broken packet and resend the request with the same confirmation field. The server device will notice that the confirmation flag does not match the previously sent packet (which was lost), recognize that the events were not delivered, and resend a packet with the same flag and events as before. It may also add new events to the package.

- **Error when transmitting from a server with several devices:**
  - The client will accept a broken packet and resend the request with the same acknowledgment field. The server device will detect that the acknowledgment flag does not match the previously sent packet and will try to resend it. It may lose arbitration, causing the packet to be sent in a subsequent request.

- **Reboot of the client:**
  - The client begins sending requests with an empty confirmation field. All devices will repeat their packets if they have not been previously acknowledged.

- **Reboot of the server:**
  - During the next event polling, the client will receive a reboot event from the server and synchronize the flag with that packet. Even if the device responds to a request confirming its address, a packet with a reboot event will be sent with any flag option.

- **Error when receiving confirmation from the server:**
  - The server will not reset its events, and the client will receive no confirmation. In the next polling cycle, the server will send the same set of events as last time, possibly adding new ones. If the client remembers which package of events it received from that server previously, it can compare the new package with the old one and consider only the new events. If not, the client may process the same set of events again (_potential duplication of events_).

## Event sources

Similar to registers, each device can generate events. For example, `a double click was recognized`. Events have 16-bit identifiers. The architecture allows events to include a type and additional useful data (e.g., brightness settings or a timestamp). You can organize event queues by including an index in the payload and resetting events to the received index. This approach enables the storage and transmission of multiple events of the same type without missing any individual triggers. Additionally, payload data can be used to transmit the response time and reset events that occurred before the last received tag.

### Case Change Notifications

First of all, it was decided to implement notifications when a device’s register value changes. For this purpose, four types of events `0-4` are reserved, corresponding respectively to `coil` (1), `discrete` (2), `holding` (3), and `input` (4), similar to the standard reading functions. The data field can contain from 1 to 8 bytes of data depending on the type and meaning of the register. For example, 1 byte is transferred for `coil` and `discrete`, whereas the WB-MSW v3 illuminance value, located in registers 9 and 10, is transferred as 4-byte data in little endian format.

Technically, each register can generate events, and a register’s value can be changed in the device not only through Modbus writing. To limit traffic on the bus, this behavior is controlled via a special command. By default, register change event reporting is disabled; once permission is granted to send events when a value changes for a specific register, the device will begin sending an event.

### Device reset detection

As soon as the firmware starts, it sends a power-on event. This is the only event that is allowed once enabled. When the client receives this event, it understands that the device has been rebooted and that event generation must be reconfigured. It makes sense to send the next event request only after successful configuration of event generation on the device. After a reset, the power-on event will not appear until the device reboots again.

The enable event is of type `0x0F`, ID 0, with low priority.

If the client has restarted and plans to configure all devices regardless, it can disable the sending of the enable event so as not to waste time receiving it during the next event request cycle.

To work around the situation where a server may not receive a request packet from the client confirming its enable event, the sending of the enable event should be disabled during configuration—even if the event was just received.

### Event sending configuration function - 0x18

We need to configure everything required for working with events as soon as possible, i.e. in one transaction. In addition to enabling event transmission, we can configure various parameters, such as selecting the priority of event delivery.

Command Description:  
(1 byte) server id device address  
(1 byte) `0x46` control command for allowing event transmission  
(1 byte) `0x18` subcommand – controls the resolution of transmission of register value change events  
(1 byte) length of the settings list  
(5 or more bytes) settings for sending events with register range 1  
...  
(5 or more bytes) settings for sending events with a range of registers N  
(2 bytes) checksum

The event sending settings field has the following format:  
(1 byte) register type  
(2 bytes) register address (big endian)  
(1 byte) number of registers in a row  
(N bytes) setting for each register, where 0 means event sending is not active, 1 enables low priority sending, and 2 enables high priority sending

An example of enabling event sending when changing discrete registers 4 and 6 with low priority, as well as input registers 464, 466, and 473 with high priority on the device with address `0x0A`:

```
                ,-- type (discrete)
                |   ,-- address (4)
                |   |    ,-- number
                |   |    |  ,-- register #4
                |   |    |  |     ,-- register #6
                |   |    |  |     |                                            CRC
                | __|__  |  |     |                                           __|__
-> 0A 46 18 15 02 00 04 03 01 00 01 04 01 D0 0A 02 00 02 00 00 00 00 00 00 02 33 A3
    |  |  |  |                       | -----  |  |     |                    |
    |  |  |  `-- length (20)         |   |    |  |     |                    `-- register 473
    |  |  `-- subcommand             |   |    |  |     `-- register 466
    |  `-- command                   |   |    |  `-- register 464
    `-- server id                     |   |    `-- number of registers (10)
                                     |   `-- address (464)
                                     `-- type (input)
```


Response from device:

- (1 byte) `server id` device address  
- (1 byte) `0x46` control command for allowing event transmission  
- (1 byte) `0x18` subcommand - controls the permission of transmission of register value change events  
- (1 byte) length of the list of setting values  
- (M bytes) event sending enable flags for range 1  
   ...  
- (M bytes) event sending permission flags for range N  
- (2 bytes) checksum

In the response command, the register type, address, and number of registers in a row fields are omitted. Only bit masks containing the status of the event transmission configuration are included. The bits are packed similarly to the `0x01` command, from LSB to MSB with increasing event ID. The number of bytes is `(number of registers in a row / 8 rounded up)`. The bits are grouped into blocks, aligned on byte boundaries, repeating the ranges specified in the request. The bitmask format is little endian.

An example of a response to the command above for a device with address `0x0A`: `discrete` registers 4 and 6, as well as `input` registers 464 and 466, are included. `input` register 473 remains disabled (because it doesn't exist at all, but we're only interested in the fact that it won't transmit events; later, when we poll it with a standard Modbus command, we'll realize that it doesn't exist).


```
                ,-- first block: 101(0 0000)
                |   ,-- second block: 1010 0000 00(00 0000)
                |  _|_
<- 0A 46 18 03 05 05 00 XX XX
    |  |  |  |          -----
    |  |  |  |            |
    |  |  |  |           CRC
    |  |  |  `-- length (3)
    |  |  `-- subcommand
    |  `-- command
    `-- server id
```

This format allows you to enable all the desired events in one `bus exchange` cycle, which greatly saves time and offers flexibility in situations where the firmware does not support all events.

To enable sending events from 32 hit counter registers, it will take **70** frames, or **80.208** ms at 9600 baud / **6.684** ms at 115200 baud. On 30 devices, this will take **2100** frames, or **2406.25** ms at 9600 baud / **200.521** ms at 115200 baud.

If the device does not support sending events, then it will respond to event management commands with error 1 (`ILLEGAL FUNCTION`) _or some other error with bit `0x80` in the function code – an error in the firmware_.
