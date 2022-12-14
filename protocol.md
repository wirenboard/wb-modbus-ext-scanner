
# РАСШИРЕНИЕ MODBUS WIRENBOARD

Работа в расширенном режиме происходит через зарезервированный адрес `0xFD` и команду `0x60`.

Расширенный режим добавляет Modbus шине дополнительные функции которых нет в стандартнов режиме: сканирование Modbus-шины, чтобы получить информацию о подключенных устройствах, использование серийного номера устройства вместо Modbus-адреса для отправки команд, и другие.

## Сканирование шины

Режим сканирования шины позволяет обнаруживать устройства Wiren Board, подключенные по Modbus, и получать их серийный номер и Modbus-адрес. 

Сканировать можно только устройства, которые считают себя не сканированными. Устройства считают себя не сканированными после включения и после получения команды `0x01`. 

### Функция начала сканирования — 0x01

Команда отправляется мастером. Приняв эту команду устройства начинают считать себя не сканированными. Далее они участвуют в арбитраже, и устройство с меньшим серийным номером отправляет в ответ команду `0x03` с данными о себе.

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x01`    субкоманда начала сканирования
* (2 байта)             контрольная сумма

`Пример: FD 60 01 09 F0`

### Функция продолжения сканирования — 0x02

Команда отправляется мастером, если был получен ответ на команду `0x01`. Устройства, которые еще не отправили информацию о себе, устраивают арбитраж, и устройство с меньшим серийным номером среди оставшихся отправляет в ответ команду `0x03` с данными о себе.

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x02`    субкоманда сканирования одного устройства
* (2 байта)             контрольная сумма

`Пример: FD 60 02 49 F1`

### Функция ответа на сканирование — 0x03

Отправляется slave-устройством, выигравшим арбитраж, в ответ на команду `0x01` или `0x02`. Содержит пакет с данными о себе:

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x03`    субкоманда — признак ответа на сканирование
* (4 байта)             серийный номер устройства
* (1 байт)              Modbus-адрес устройства
* (2 байта)             контрольная сумма

`Пример: FF FF FF FF FF FF FF FF FF FD 60 03 FE 11 F1 D9 01 09 A8`

Мастер игнорирует байты со значением `0xFF`, пока не получит кадр ответа. Мастеру желательно запомнить все найденные устройства и их Modbus-адреса. В случае повторения Modbus-адреса его можно изменить, идентифицируя устройство по серийному номеру.

Между запросами сканирования мастер может выполнить любую команду, включая стандартные.

### Функция конца сканирования — 0x04

Отправляется slave-устройством, с наименьшим значением серийного номера, если не сканированных устройств не осталось. Таким образом мы избавляем мастера от необходимости ждать таймауты.

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x04`    субкоманда — признак завершения сканирования
* (2 байта)             контрольная сумма

`Пример: FF FF FF FF FF FF FF FF FF FD 60 04 C9 F3`

## Порядок сканирования

Мастер выбирает настройки скорости и четности порта Modbus-шины и отправляет команду начала сканирования `0x01`. После этого он ждет стандартное время 3.5 фрейма. И если на шине есть устройства с такими же настройками Modbus-шины, то одно из них вернет ответ командой `0x03`. Если по истечении времени ответа нет, то мастер меняет настройки порта и снова отправляет команду начала сканирования `0x01`. 

Если ответ был, то мастер направит команду продолжения сканирования `0x02`. Если на шине есть еще одно устройство, то мастер получит от него ответ с командой `0x03`. Так будет продолжаться до тех пор, пока не будут получены ответы от всех устройств. Когда все устройства будут сканированы, в ответ придет команда окончания сканирования `0x04`.

После того как мастер получил команду окончания сканирования `0x04`, он меняет настройки скорости и четности порта и снова начинает сканирование. Так продолжается, пока не будут сканированы все варианты настройки порта.

Запрос сканирования можно периодически повторять, чтобы отслеживать подключение новых устройств или узнать, что какое-то из устройств перезагрузилось.

## Работа с устройствами по серийному номеру

Если два устройства на шине имеют одинаковый Modbus-адрес, с ними не получится обмениваться данными, используя стандартные функции Modbus. Для таких случаев предусмотрена возможность использовать серийный номер устройства вместо его Modbus-адреса. Это позволит, например, сменить Modbus-адрес устройства или получить данные об устройстве до смены Modbus-адреса.

Для отправки стандартной Modbus-команды, используя серийный номер вместо адреса, есть специальные команды: `0x08` — для отправки команды и `0x09` — для получения ответа.

### Функция отправки стандартной команды — 0x08

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x08`    субкоманда эмуляции стандартных запросов, отправляется мастером
* (4 байта)             серийный номер устройства к которому обращаемся
--- обычный PDU ---
* (1 байт)              стандарный код функции работы с регистрами (1-4, 5, 6, 15, 16)
  ...                   тело запроса
--- обычный запрос ---
* (2 байта)             контрольная сумма

`Пример: FD 60 08 FE 11 F1 D9 03 00 68 00 02 8A 44`

### Функция ответа на стандартную команду — 0x09

* (1 байт)    `0xFD`    широковещательный адрес
* (1 байт)    `0x60`    команда работы с расширенными функциями
* (1 байт)    `0x09`    субкоманда эмуляции ответов на стандартные запросы, отправляется slave'ом
* (4 байта)             серийный номер устройства к которому обращаемся
--- обычный PDU ---
* (1 байт)              стандарный код функции работы с регистрами (1-4, 5, 6, 15, 16)
  ...                   тело ответа
--- обычный запрос ---
* (2 байта)             контрольная сумма

`Пример: FD 60 09 FE 11 F1 D9 03 04 00 00 3B 9E BF 03`
