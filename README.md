# Wirenboard Modbus scanner tool

Репозиторий содержит описание расширения протокола Modbus Wiren Board и пример реализции утилиты для работы с ним.

## Установка

`apt update && apt install wb-modbus-ext-scanner`

**!!! Перед использованием убедитесь что последовательный порт не используется другим приложением. Остановите сервис wb-mqtt-serial**

## Параметры утилиты

Для вывода помощи вызовите утилиту без аргументов

```
# wb-modbus-scanner
Wirenboard modbus extension tool. vsrsion: 1.0.0
Usage: wb-modbus-scanner -d device [-b baud] [-s sn] [-i id] [-D]

Options:
    -d device      TTY serial device
    -b baud        Baudrate, default 9600
    -s sn          device sn
    -i id          slave id
    -D             debug mode

For scan use:              wb-modbus-scanner -d device [-b baud] [-D]
For set slave id use:      wb-modbus-scanner -d device [-b baud] -s sn -i id [-D]
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
