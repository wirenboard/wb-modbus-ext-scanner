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

This implementation of Modbus supports extended commands that improve operation of the bus and devices. Commands are sent to a group of devices; the devices themselves decide who will respond.

To work with extended functions, commands are reserved, within which a subcommand will be transmitted that determines the type of message:

- `0x46` - used in event polling, recommended to use in all extended commands;
- `0x60` - deprecated, used in the scanning protocol. Reserved in the modbus specification and therefore not recommended for use.

If the scanning algorithm or its timing characteristics change, a new subcommand will be allocated for scanning.

As it turned out, devices can respond to the broadcast address (`0`) about an unknown command type, for example, WB-MS. Therefore, extended mode works through the reserved address `0xFD`

## Bus scan

After turning on, the device considers that it has not been scanned. It is possible to scan only devices that think they are not scanned. In order for the device to begin to consider itself unscanned, the scan start function `0x01` is used. Once a device has sent a `0x03` response, it begins to consider itself scanned and can only respond with `0x04` (end of scan) if it wins arbitration again (meaning there are no more unscanned devices on the bus).

### Scan start function - 0x01

Having accepted this command, devices begin to consider themselves not scanned. Then all the devices participate in arbitration, and the device with the lower serial number sends a scan response with data about itself.

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x01` subcommand to start scanning
- (2 bytes) checksum

Example:

```
-> FD 46 01 13 90
```

### Continue scanning function - 0x02

The client can then continue scanning the bus by sending this command. Devices, that have not yet sent information about themselves, arrange arbitration, and the device with the lowest serial number sends a scan response with information about itself.

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x02` subcommand for scanning one device
- (2 bytes) checksum

Example:

```
-> FD 46 02 53 91
```

### Scan response function - 0x03

All devices on the bus receive the command after 3.5 frames of silence on the bus, regardless of the response delay setting,  and arbitration occurs. It looks like bytes, which are periodically sent to the bus with the value 0xFF. After that the devices decide who will transmit the information, and they understand whether there are any devices that are not scanned at all. The device, that wins the arbitration, transmits a packet with information about itself, having the following structure:

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x03` subcommand - scan response sign
- (4 bytes) device serial number (big endian)
- (1 byte) modbus device address
- (2 bytes) checksum

Example:

```
<-FF FF FF FF FF FF FF FF FF FD 46 03 00 01 EB 37 0C CE DC
```

In this case, the client ignores bytes with the value 0xFF until it receives a response frame. Based on the response from the client, it is important to remember all the found devices and their Modbus addresses. If the Modbus address is repeated, it can be changed by identifying the device by serial number.

Between scanning requests, the client can execute any command, for example, read information about the device or change the address, accessing to the device by serial number.

### End of scan function - 0x04

The client repeats the scan request until all the devices are found. If there are no new devices left, the device with the lowest serial number will respond, and in the response it will report that there are no new devices left. This way we save the client from having to wait until the timeout occurs.

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x04` subcommand - sign of scanning completion
- (2 bytes) checksum

Example:

```
<- FF FF FF FF FF FF FF FF FF FD 46 04 D3 93
```

The scan request can be repeated periodically and thus, for example, understand that a new device has been connected, or one of the devices has rebooted.

## Scan order

The client selects the speed and parity settings and sends the start scan command (`0x01`) with them. Next, you need to wait the standard time (3.5 frames) and after that, if there are devices on the bus,  one of them will return a scan response with the command `0x03`. All preceding `0xFF` bytes must be ignored. If after the waiting time there is no response, then there are no devices on the bus with these connection settings and you can proceed to scan on other port settings. If there was a response, then after it you can send the command to continue scanning (`0x02`) and receive data about the next device. This can be repeated until the scan is completed (`0x04`).

### Arbitrage in scanning

You can read more about the arbitration algorithm below.

Initially, there may be devices on the bus with the same addresses. Therefore, in a scan request, arbitration is carried out based on the unique serial number of the device and takes 32 **arbitration windows** of time.

For the scan command (`0x60`), legacy arbitration rules apply, where the window duration is 20 bits at the current baud rate, and the start of arbitration is 44 bits after the last received request byte - 3.5 bytes.

Scanning one device will take \*_(5 bytes _ 10 bits) request + (44 bits) wait for response + ((28 + 4) arbitration fields _ 20 bits per arbitration window) + (10 bytes _ 10 bits) response = 834 bit interval, which at a speed of 115200 will last about 7.5 ms.

> To speed up code execution in the interrupt, we would like to use a 32-bit architecture and limit myself to an arbitration field of 32 bits. However, in addition to the serial number, somewhere in the high bits it is necessary to convey a sign of the presence of new devices. And in the future, under decentralized regimes, some more information. To do this, you need to use a smaller number of bits of the serial number, for example 28, and limit yourself to releasing devices with serial numbers up to 2 ^ 28 - this is also a lot (you can also stretch it to the CAN bus)

> If you implement this protocol in a DIY device, select a serial number in the range **0x0D000000 - 0x0D00FFFF**

## Working with devices by serial number

If some devices have the same Modbus address, you cannot work with them through regular Modbus functions. For example, we scanned the bus and want to request device models or firmware versions before changing the address. It is possible to work with the device through its serial number. The address becomes known at the scanning stage or is available on a sticker. It is possible to wrap a standard command in a packet, that uses a serial number instead of a Modbus address.

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

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x09` subcommand for emulating standard requests
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

Arbitration is a way for devices to decide themselves, who will respond to a request, and also to understand whether there are more devices willing to respond. Arbitration occurs based on a unique value of each device (for example, serial number or `server_id`, depending on the command). The principle is similar to arbitration in the CAN bus. Each device transmits one bit of its unique value in order, starting with MSB. When a device, transmitting a recessive bit (1), detects a dominant bit (0) on the bus, it loses arbitration. By the end of the transmission of the unique value, there is only one device left that has won the arbitration.

Arbitration bits are transmitted one at a time into the **arbitration window** (time interval). The dominant bit is transmitted as sending the value **0xFF** using normal USART transmission and hardware control of the bus driver. Recessive - nothing is transmitted during the window interval, there is silence on the bus, the transceiver does not turn on. In this way, we bypass the hardware limitations of RS485 in which it is impossible to simultaneously transmit different states. The receiver circuitry is standard. It can be disabled during transmission; this does not affect the operation of the protocol in any way.

Before transmitting a byte, the state of the USART BSY flag is checked. If one device has already transmitted the start bit, then all the rest are silent during the current **arbitration window**, implying that the dominant bit has been transmitted. Thus, a timer mismatch of several bits is acceptable.

### Timings

The duration of the arbitration window and the timeout of the first window depend on the command and bus settings; this is necessary in order to ensure stable operation at different speeds.

The entire process of sending a response to a request can be divided into three phases:

1. Waiting for the start of arbitration; during this time, devices must wait for the end of the previous packet, have time to initially process the request and understand whether they need to participate in arbitration.
2. Arbitration
3. Sending a data package

The waiting time for the start of arbitration is limited below 800 μs — this time is enough to process an interrupt by a controller with a clock frequency of 8 MHz (selected experimentally) and the transmission time of 3 characters (taken from the Modbus specification, however, there are 3.5 characters).

The duration of the arbitration window is the reception time of 12 bits (one UART frame with 2 stop bits and a parity bit) + time reserve for interrupt processing. The time reserve for interruption is limited below 50 μs (selected experimentally) and is a multiple of the transmission time of 1 bit.

The timeout for receiving a response is calculated as `waiting for the start of arbitration + duration of the arbitration window * number of bits in arbitration`. For scanning, the number of arbitration bits is 32.

_To poll for events, you can use an optimized timeout value, see below._

Formula for calculating timeout:

```
max(3.5 symbols, (12 bits + 800us)) + N * max(13 bits, 12 bits + ceil_bits(50us))
```

where `N` is the number of arbitration windows, `ceil_bits(x)` is rounded up to a multiple of the bit transmission length. For example, for a boost rate of 57600 (transmission time of one bit is 17 µs) it will be `ceil(50 / 17) = ceil(2.88) = 3`.

## Events

The library allows you to poll events quickly, that occur on devices without polling each of them in turn. **This is only possible if there are no devices on the network with the same server ID**.
The following functions are provided for event polling: event query, event transmission and no events.

### Event Request Function - 0x10

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x10` subcommand - request events from devices
- (1 byte) minimum server id of the device from which to start responding
- (1 byte) the maximum length of the data field with events in the packet that the client expects; according to the standard, the length of the entire packet should not exceed 256 bytes.
- (1 byte) `server_id` of the device from which the previous event packet was received
- (1 byte) flag of the previous received packet to confirm reception (see below).
- (2 bytes) checksum

If the specified maximum length of the data field does not fit a single event, the response will come without a payload (list of events), from it it will be possible to find out only the number of new events.

A flag must be inserted into this request to confirm the receipt from the previous received packet for this `server_id`, otherwise the events will not be reset.

An example without a `server_id` limit with an event field length of 100 bytes. The previous event packet was received from device 0x0A with bit 1:

```
-> FD 46 10 00 64 0A 01 XX XX
```

### Event transfer function - 0x11

- (1 byte) `server_id` device
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x11` subcommand - transmission of events from the device
- (1 byte) flag of this packet to confirm receipt (see below)
- (1 byte) number of unreset events
- (1 byte) length of the data field of all events in bytes before the controlamounts
- (4 or more bytes) event 1
   ...
- (4 or more bytes) event N
- (2 bytes) checksum

The event contains 4 or more bytes and has the following format:

- (1 byte) length of additional event data
- (1 byte) event type
- (2 bytes) event identifier (big endian)
- (0 or more bytes) additional event data (little endian format)

Example: a device with address 5 responds with an event about a change in input register 0x01D0, new value 4, one event not sent, the packet confirmation flag is set:

```
<- 05 46 11 01 01 06 02 04 01 D0 04 00 XX XX
```

### Response function if there are no events - 0x12

This packet is sent by the device that wins the arbitration (lowest priority token + `server_id`).

- (1 byte) `0xFD` broadcast address
- (1 byte) `0x46` command for working with extended functions
- (1 byte) `0x12` sub command - no events
- (2 bytes) checksum

Example:

`<- FD 46 12 52 5D`

The cycle of an event request and device response for one event with 2 bytes of data will take **42** frames, or **48.125** ms (9600) / **4.01** ms (115200)

## Event polling order

The polling cycle can begin after the completion of any command, including the server's response. The request begins with the client sending a packet with the `0x10` event request command. If there is nothing to confirm, set the confirmation field to 0 address 0 flag.

Devices conduct arbitration through standard 3.5 frames (see description below). If there are some devices that want to report events that have been occurred, then the one, that wins the arbitration, responds by sending a packet with the command `0x11`, in which it transmits a list of events. Events have an identifier, a type, and optionally additional data. The maximum packet size is 256 bytes according to the standard. If the events do not fit into one packet, then at the next request the device will continue to win arbitration and transmit events.

Then the client can repeat the cycle and request events again.

Confirmation of receipt of events by the client is done through the parameters in the following request, in order to save time when polling, see "Confirmation of event receipt"

If a device is transmitting too many events and preventing others from transmitting events, the client can specify a server id greater than the device's in the request packet to allow that device to pass (along with all others whose `server_id` is less than the specified one).

### Arbitration in events

For arbitration when polling events, the concatenation of the packet priority marker (_not to be confused with the event priority_) (4 bits) and the device `server_id` (8 bits) is used.

> It is better to use `server_id` instead of the device serial number, because arbitration takes 4 times less time, and at the time of setting up event polling, unique `server_id` values are already expected for all devices.

At the beginning a priority marker allows devices with higher priority events to win arbitration earlier, thereby reducing latency for those events. Also, the lowest priority token (`0xF`) is used by devices when there are no new events, which allows one of the devices to answer for all that there are no events (command `0x12`).

Сurrently there are two available event priorities (`HIGH` and `LOW`).

### Timeout waiting for response in events

An event polling implementation can use a standard timeout formula where 12 arbitration bits are expected (4 priority marker bits and 8 `server_id` bits). In practice, you can use a hack that allows you to reduce this time.

Let us recall that in arbitration bit 0 is dominant (read: higher priority). The recessive bit (1) is not actually sent to the bus during arbitration.

In fact, the response timeout is the time from sending the request until the first character is received on the bus, which can be either the dominant arbitration bit or the start of the message. In `server_id` arbitration, the most significant bits (MSB) will appear first.

`server_id` in Modbus is a value from 1 to 247.

- for values from 1 to 127, the binary record in 8 bits begins with `0` => we will receive the first character on the bus in the fifth arbitration window (the first 4 bits are behind the priority marker);
- for values from 128 to 191, the binary entry begins with `10` => the first character - into the sixth arbitration window;
- for values from 192 to 223, the binary notation begins with `110` => seventh window;
- for values from 224 to 239, binary notation begins with `1110` => eighth window;
- for values from 240 to 247, the binary notation begins with `11110` => ninth window;
- there cannot be values greater than 247.

So that means if we do not receive a frame after waiting time 9 arbitration windows, then there are no devices on the bus participating in arbitration.

Thus, it is possible to reduce the waiting time from 12 arbitration windows to 9 (+ waiting for the arbitration to start).

## Event confirmation

Delivery guarantee is implemented by procedure to confirm the receipt of a packet with events for each device.

Each event packet has a flag field, which has a package number with a value of 0 or 1. Every next device in the packet changes the flag on the opposite one relative to the previous one. Thus, there are no two adjacent packets from the same device with the same flag value. Flags from different devices are not connected in any way.

When the client has correctly received the event packet, it must acknowledge the receipt of the events to the device. The client must remember the last flag value in the packet for each device. In the next event request cycle, in the request command `0x10` in the confirmation field, the client specifies the server id of the device and the flag, that was specified in the previous received event packet. All devices receive this packet, even if they did not win the arbitration, and if they see their `server_id` in the confirmation field in the packet, they forget the previously sent batch of events (with the corresponding confirmation flag).

Possible errors during confirmation:

- error when transmitting from a client with one device:

   - devices will not respond. The client will get time out and send the request again with the same confirmation field.

- error when transmitting from a server with one device:

   - the client will accept the broken package will request again with the same confirmation field, the server device will see that the confirmation flag does not correspond to the previously sent packet (which was lost), understand, that the events were not delivered, and will send a packet with the same flag again as in the lost packet with the same events. Perhaps it will add new events to the package.

- error when transmitting from a server with several devices:

   - the client will accept the broken package and request again with the same acknowledgment field, the server device will see that the acknowledgment flag does not match with the previously sent packet and will try to send it again. It may lose arbitration, then this packet will be sent to one of the next requests.

- reboot the client:

   - the client starts sending requests with an empty confirmation field. All devices will repeat packets if they have not been previously acknowledged.

- reboot the server:

   - the client, during the next event polling, will receive a reboot event from the server and synchronize the flag with the packet. Even if the device responds to a request confirming its address, a packet with a reboot event will be sent with any flag option.

- error when receiving confirmation from the server:
   - the server will not reset the events for himself, but the client will get no information, so in the next polling cycle, the server will send the same set of events as last time, and may add new ones. If the client remembers which package of events he received from this server last time, he can compare the new package with the old one and take into account only those events that are not in the old one, but are in the new one. If it doesn’t remember, it will publish the same set of events again (_potential duplication of events_).

## Event sources

Similar to registers, each device can generate events. For example, `a double click was recognized`. Events have 16-bit identifiers. The architecture allows events to have a type and useful data: for example, brightness settings or a timestamp. You can organize event queues: for example, have an index in the payload and reset events to the received index, this way you can organize the storage and transmission of several events of the same type without missing a separate trigger. Also, through payload data, you can transmit the response time and reset events that occurred before the last received tag.

### Case Change Notifications

First of all, it was decided to make notifications that the device’s register value has changed. For this, 4 types of events are reserved: `1 - 4` respectively for `coil`, `discrete`, `holding`, `input` (similar to standard reading functions). The data field can contain from 1 to 8 bytes of data depending on the type and meaning of the register. For example, 1 byte is transferred for `coil` and `discrete`, and the WB-MSW v3 illuminance value located in registers 9 and 10 is transferred as 4-byte data in little endian format.

Technically, each register can transmit events, which can be changed in the device not only through Modbus writing. To limit traffic on the bus, this behavior is configured through a special command. By default, register change event reporting is disabled. After setting the permission to send events when a value changes to a specific register, the device will begin sending an event.

### Device reset detection

As soon as the firmware starts, it informs about this with a power-on event. This is the only event that is allowed once enabled.
Having read this event, the client understands that the device has been rebooted and it is necessary to reconfigure event generation. It makes sense to send the next event request only after successful configuration of event generation on the device. After a reset, the event will not appear until the device reboots.

The enable event is of type `0x0F`, ID 0, priority low.

If the client has restarted and plans to configure all the devices appear in any case, then it can turn off sending the enable event so as not to waste time receiving it during the next event request cycle.

To work around the situation where a server may not receive a request packet from the client confirming its enable event, the sending of an enable event should be disabled during configuration. Even if we just received this event.

### Event sending configuration function - 0x18

We need to configure everything we need to work with events as soon as possible, i.e. in one transaction. In addition to enabling, we can configure various parameters, for example, select the priority of event delivery.

Command Description:

- (1 byte) server id device address
- (1 byte) `0x46` control command for allowing event transmission
- (1 byte) `0x18` subcommand - controls the resolution of transmission of register value change events
- (1 byte) length of the settings list
- (5 or more bytes) settings for sending events with register range 1
   ...
- (5 or more bytes) settings for sending events with a range of registers N
- (2 bytes) checksum

The event sending settings field has the following format:

- (1 byte) register type
- (2 bytes) register address (big endian)
- (1 byte) number of registers in a row
- (N bytes) setting for each register
   0 - event sending is not active
   1 - enable low priority sending
   2 - enable high priority sending

An example of enabling sending events when changing discrete registers 4 and 6 with low priority, as well as input registers 464, 466 and 473 with high priority on the device with address 0x0A:

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

- (1 byte) server id device address
- (1 byte) `0x46` control command for allowing event transmission
- (1 byte) `0x18` subcommand - controls the permission of transmission of register value change events
- (1 byte) length of the list of setting values
- (M bytes) event sending enable flags for range 1
   ...
- (M bytes) event sending permission flags for range N
- (2 bytes) checksum

In the response command, the register type, address, and number of registers in a row fields are omitted. Only bit masks containing the status of the event transmission configuration are contained. The bits are packed similarly to the `0x01` command, from LSB to MSB with increasing event ID. The number of bytes is `(number of registers in a row / 8 rounded up)`. The bits are grouped into blocks, aligned on byte boundaries, repeating the ranges in the request. The bitmask format is little endian.

An example of a response to the command above, a device with address 0x0A. `discrete` registers 4 and 6, as well as `input` registers 464, 466 are included. `input` register 473 remains disabled (because it doesn't exist at all, but we're only interested in the fact that it won't transmit events; later, when we poll it with a standard modbus command, we'll realize that it doesn't exist).


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

This format allows you to enable all the desired events in 1 bus exchange cycle, which greatly saves time and leaves flexibility for situations where the firmware does not support all events.

To enable sending events from 32 hit counter registers, it will take **70** frames, or **80.208** ms (9600) / **6.684** ms (115200)
On 30 devices this will take **2100** frames, or **2406.25** ms (9600) / **200.521** ms (115200)

If the device does not support sending events, then the device will respond to event management commands with error 1 (ILLEGAL FUNCTION) _or some other error with bit 0x80 in the function code - an error in the firmware_.
