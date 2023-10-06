# Wirenboard Modbus scanner tool

Репозиторий содержит описание расширения протокола Modbus Wiren Board и пример реализции утилиты для работы с ним.

## Установка

`apt update && apt install wb-modbus-ext-scanner`

**!!! Перед использованием убедитесь что последовательный порт не используется другим приложением. Остановите сервис wb-mqtt-serial**

## Параметры утилиты

Для вывода помощи вызовите утилиту без аргументов

```
# wb-modbus-scanner
Wirenboard modbus extension tool. version: 1.2.0
Usage: ./wb-modbus-scanner -d device [-b baud] [-s sn] [-i id] [-D]

Options:
    -d device      TTY serial device
    -b baud        Baudrate, default 9600
    -s sn          device sn
    -i id          slave id
    -D             debug mode
    -l len         max len of event data field
    -e id          event request with confirm 0 for slave id
    -E id          event request with confirm 1 for slave id
    -r reg         event control reg
    -t type        event control type
    -c ctrl        event control value

For scan use:              ./wb-modbus-scanner -d device [-b baud] [-D]
For set slave id use:      ./wb-modbus-scanner -d device [-b baud] -s sn -i id [-D]
For setup event use:       ./wb-modbus-scanner -d device [-b baud] -i id -r reg -t type -c ctrl
Event request examples:
         ./wb-modbus-scanner -d device [-b baud] -e 0               (request + nothing to confirm)
         ./wb-modbus-scanner -d device [-b baud] -e 4               (request + confirm events from slave 4 flag 0)
         ./wb-modbus-scanner -d device [-b baud] -E 6               (request + confirm events from slave 6 flag 1)
```

## Сканирование устройства на шине

Пример вызова:

```
# wb-modbus-scanner -d /dev/ttyRS485-1 -b 115200
Serial port: /dev/ttyRS485-1
Use baud 115200
Send SCAN INIT cmd
Found device ( 1) with serial   4262588889 [FE11F1D9]  modbus id:   1  model: MRPS6
Found device ( 2) with serial   4267937719 [FE638FB7]  modbus id:   1  model: WBMR6C                  [MODBUS ID REPEAT]
End SCAN
```

Утилита обнаружила 2 устройства, приэтом у них повторяются адреса на шине modbus, очем свидетельствует надпись MODBUS ID REPEAT

## Изменения адреса на шине

Пример вызова:

```
# wb-modbus-scanner -d /dev/ttyRS485-1 -b 115200 -s 4267937719 -i 3
Serial port: /dev/ttyRS485-1
Use baud 115200
Chande ID for device with serial   4267937719 [FE638FB7] New ID: 3
```

## Включение отправки событий modbus регистра

Пример вызова:

```
# wb-modbus-scanner -d /dev/ttyRS485-2 -D -i 62 -r0 -t 1 -c 1
Serial port: /dev/ttyRS485-2
Use baud 9600
    -> :  3E 46 18 05 01 00 00 01 01 F3 4F
    <- :  3E 46 18 01 01 58 DA

```

Здесь мы устройству с адресом 62 включили передачу события при изменении coil (type 1) регистра 0 с приоритетом 1

## Запрос событий

Пример вызова:

```
# wb-modbus-scanner -d /dev/ttyRS485-2 -e 0
Serial port: /dev/ttyRS485-2
Use baud 9600
    send EVENT GET    -> :  FD 46 10 00 FF 00 00 C8 9A
    <- :  FF FF FF FF FF 3E 46 11 00 03 10 01 02 00 03 00 02 04 00 23 01 00 01 01 00 03 00 4F CF
    device:  62 - events:   3   flag: 0   event data len: 016   frame len: 024
Event type:   2   id:     3 [0003]   payload:          0   device 62
Event type:   4   id:    35 [0023]   payload:          1   device 62
Event type:   1   id:     3 [0003]   payload:          0   device 62
```

Здесь был отправлен запрос без подтверждения предыдущих событий

в ответ устройство с адресом 62 уведомило о трех событиях

discrete (тип 2) регистр 3 изменил значение, новое значение 0
input (тип 4) регистр 35 изменил значение, новое значение 1
coil (тип 1) регистр 3 изменил значение, новое значение 0

Также видим что флаг подтверждения имеет значение 0

Чтобы подтвердить события от данного устройства, и запросить следующие, нужно использовать ключ -e c адресом 62

```
# wb-modbus-scanner -d /dev/ttyRS485-2 -e 62 -D
Serial port: /dev/ttyRS485-2
Use baud 9600
    send EVENT GET    -> :  FD 46 10 00 FF 3E 00 D8 FA
    <- :  FF FF FF FD 46 12 52 5D
NO EVENTS
```
