# Wirenboard Modbus scanner tool

The repository contains a description of the Wiren Board Modbus protocol extension and an example implementation of a utility for working with it.

## Installation

`apt update && apt install wb-modbus-ext-scanner`

**!!! Before use, make sure that the serial port is not being used by another application. Stop the wb-mqtt-serial** service

## Utility parameters

To display help, call the utility without arguments

```sh
# wb-modbus-scanner
Wirenboard modbus extension tool. version: 1.2.0
Usage: ./wb-modbus-scanner -d device [-b baud] [-s sn] [-i id] [-D]

Options:
     -d device TTY serial device
     -b baud Baudrate, default 9600
     -L use 0x60 (deprecated) cmd instead of 0x46 in scan
     -s device sn
     -i id slave id
     -D debug mode
     -l len max len of event data field
     -e id event request with confirm 0 for slave id
     -E id event request with confirm 1 for slave id
     -r reg event control reg
     -t type event control type
     -c ctrl event control value

For scan use: ./wb-modbus-scanner -d device [-b baud] [-D]
For scan some old fw use: ./wb-modbus-scanner -d device [-b baud] -L [-D]
For set slave id use: ./wb-modbus-scanner -d device [-b baud] -s sn -i id [-D]
For setup event use: ./wb-modbus-scanner -d device [-b baud] -i id -r reg -t type -c ctrl
Event request examples:
          ./wb-modbus-scanner -d device [-b baud] -e 0 (request + nothing to confirm)
          ./wb-modbus-scanner -d device [-b baud] -e 4 (request + confirm events from slave 4 flag 0)
          ./wb-modbus-scanner -d device [-b baud] -E 6 (request + confirm events from slave 6 flag 1)
```

## Scanning a device on the bus

Example call:

```sh
# wb-modbus-scanner -d /dev/ttyRS485-1 -b 115200
Serial port: /dev/ttyRS485-1
Use baud 115200
Send SCAN INIT cmd
Found device ( 1) with serial 4262588889 [FE11F1D9] modbus id: 1 model: MRPS6
Found device ( 2) with serial 4267937719 [FE638FB7] modbus id: 1 model: WBMR6C [MODBUS ID REPEAT]
End SCAN
```

The utility has detected 2 devices, and their addresses on the modbus bus are repeated, as evidenced by the inscription MODBUS ID REPEAT

If not all devices are found, try running the utility with the -L flag

## Bus address changes

Example call:

```sh
# wb-modbus-scanner -d /dev/ttyRS485-1 -b 115200 -s 4267937719 -i 3
Serial port: /dev/ttyRS485-1

Using baud 115200
Change ID for device with serial   4267937719 [FE638FB7] New ID: 3

```

## Enable sending modbus register events

Example call:

```sh
# wb-modbus-scanner -d /dev/ttyRS485-2 -D -i 62 -r0 -t 1 -c 1
Serial port: /dev/ttyRS485-2
Use baud 9600

    -> :  3E 46 18 05 01 00 00 01 01 F3 4F
    <- :  3E 46 18 01 01 58 DA

```

Here we enabled the device with address 62 to transmit an event when coil (type 1) of register 0 changes with priority 1

## Query events

Example call:

```sh
# wb-modbus-scanner -d /dev/ttyRS485-2 -e 0
Serial port: /dev/ttyRS485-2

Using baud 9600
    send EVENT GET    -> :  FD 46 10 00 FF 00 00 C8 9A
    <- :  FF FF FF FF FF 3E 46 11 00 03 10 01 02 00 03 00 02 04 00 23 01 00 01 01 00 03 00 4F CF
    device:  62 - events:   3   flag: 0   event data len: 016   frame len: 024
Event type:   2   id:     3 [0003]   payload:          0   device 62
Event type:   4   id:    35 [0023]   payload:          1   device 62
Event type:   1   id:     3 [0003]   payload:          0   device 62
```

A request was sent here without confirmation of previous events

in response, the device with address 62 notified three events

discrete (type 2) register 3 changed value, new value 0
input (type 4) register 35 changed value, new value 1
coil (type 1) register 3 changed value, new value 0

We also see that the confirmation flag has the value 0

To confirm events from this device and request the next ones, you need to use the -e switch with address 62

```sh
# wb-modbus-scanner -d /dev/ttyRS485-2 -e 62 -D
Serial port: /dev/ttyRS485-2
Use baud 9600
     send EVENT GET -> : FD 46 10 00 FF 3E 00 D8 FA
     <- : FF FF FF FD 46 12 52 5D
NO EVENTS
```
