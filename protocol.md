# Оглавление
- [РАСШИРЕНИЕ MODBUS WIRENBOARD](#расширение-modbus-wirenboard)
    - [Сканирование шины](#сканирование-шины)
      - [Функция начала сканирования - 0x01](#функция-начала-сканирования---0x01)
      - [Функция продолжения сканирования - 0x02](#функция-продолжения-сканирования---0x02)
      - [Функция ответа на сканирование - 0x03](#функция-ответа-на-сканирование---0x03)
      - [Функция конца сканирования - 0x04](#функция-конца-сканирования---0x04)
    - [Порядок сканирования](#порядок-сканирования)
      - [Арбитраж в сканировании](#арбитраж-в-сканировании)
    - [Работа с устройствами по серийному номеру](#работа-с-устройствами-по-серийному-номеру)
      - [Функция отправки стандартной команды - 0x08](#функция-отправки-стандартной-команды---0x08)
      - [Функция ответа на стандартную команду - 0x09](#функция-ответа-на-стандартную-команду---0x09)
    - [Как работает арбитраж](#как-работает-арбитраж)
      - [Тайминги](#тайминги)
    - [События](#события)
      - [Функция запроса событий - 0x10](#функция-запроса-событий---0x10)
      - [Функция передачи событий - 0x11](#функция-передачи-событий---0x11)
      - [Функция ответа если события отсутствуют - 0x12](#функция-ответа-если-события-отсутствуют---0x12)
    - [Порядок опроса событий](#порядок-опроса-событий)
      - [Арбитраж в событиях](#арбитраж-в-событиях)
      - [Таймаут ожидания ответа в событиях](#таймаут-ожидания-ответа-в-событиях)
    - [Подтверждение приема событий](#подтверждение-приема-событий)
    - [Источники событий](#источники-событий)
      - [Уведомления об изменении регистра](#уведомления-об-изменении-регистра)
      - [Детектирование сброса устройства](#детектирование-сброса-устройства)
      - [Функция настройки отправки событий - 0x18](#функция-настройки-отправки-событий---0x18)

# РАСШИРЕНИЕ MODBUS WIRENBOARD

Данная реализация modbus поддерживает расширенные команды, улучшающие работу с шиной и устройствами. Команды отправляются группе устройств; устройства сами решают, кто будет отвечать.

Для работы с раcширенными функциями зарезервированы команды, внутри которых будет передаваться субкоманда, определяющая тип сообщения:

- `0x46` - используется в опросе событий, рекомендуется к использованию во всех расширенных командах;
- `0x60` - deprecated, используется в протоколе сканирования. Зарезервирована в спецификации modbus, потому не рекомендуется к использованию.

Если будет меняться алгоритм работы сканирования или его временные характеристики, то будет выделена новая субкоманда для сканирования.

Как выяснилось, устройства могут отвечать по широковещательному адресу (`0`) о неизвестном типе команды, например, WB-MS. Поэтому расширенный режим работает через зарезервированный адрес `0xFD`

## Сканирование шины

После включения устройство считает, что оно не сканировано. Возможно сканировать только устройства которые считают, что они не сканированы. Для того, чтобы устройство начало считать себя неотсканированным, используется функция начала сканирования `0x01`. После того, как устройство отправило ответ `0x03`, оно начинает считать себя отсканированным и может ответить только `0x04` (конец сканирования), если выиграет арбитраж повторно (что означает, что на шине больше нет неотсканированных устройств).

### Функция начала сканирования - 0x01

Приняв эту команду, устройства начинают считать себя не сканированными. Далее все участвуют в арбитраже, и устройство с меньшим серийным номером отправляет ответ на сканирование с данными о себе.

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x01` субкоманда начала сканирования
- (2 байта) контрольная сумма

Пример:

```
-> FD 46 01 13 90
```

### Функция продолжения сканирования - 0x02

Далее мастер может продолжить сканировать шину, отправляя эту команду. Устройства, которые еще не отправили информацию о себе, устраивают арбитраж, и устройство с наименьшим серийным номером отправляет ответ на сканирование с данными о себе.

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x02` субкоманда сканирования одного устройства
- (2 байта) контрольная сумма

Пример:

```
-> FD 46 02 53 91
```

### Функция ответа на сканирование - 0x03

Все устройства на шине получают команду после 3.5 фреймов молчания на шине, независимо от настройки задержки ответа, происходит арбитраж. Выглядит это как периодически отправляющиеся на шину байты со значением 0xFF. В недалеком будущем устройства решают кто будет передавать, при этом они понимают, есть ли вообще не сканированные устройства. Устройство, выигравшее арбитраж, передает пакет с информацией о себе, имеющий следующую структуру:

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x03` субкоманда - признак ответа на сканирование
- (4 байта) серийный номер устройства (big endian)
- (1 байт) modbus адрес устройства
- (2 байта) контрольная сумма

Пример:

```
<- FF FF FF FF FF FF FF FF FF FD 46 03 00 01 EB 37 0C CE DC
```

При этом мастер игнорирует байты со значением 0xFF, пока не получит кадр ответа. По ответу мастеру желательно запомнить все найденные устройства и их modbus адреса. В случае повторения modbus адреса его можно изменить, идентифицируя устройство по серийному номеру.

Между запросами сканирования мастер может выполнить любую команду, например, считать информацию об устройстве или поменять адрес, обращаясь к устройству по серийному номеру.

### Функция конца сканирования - 0x04

Мастер повторяет запрос сканирования, пока все устройства не будут найдены. Если новых устройств не осталось, то ответит устройство с наименьшим значением серийного номера, и в ответе сообщит о том, что новых устройств не осталось. Таким образом, мы избавляем мастер от необходимости ждать до таймаута.

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x04` субкоманда - признак завершения сканирования
- (2 байта) контрольная сумма

Пример:

```
<- FF FF FF FF FF FF FF FF FF FD 46 04 D3 93
```

Запрос сканирования можно периодически повторять и таким образом, например, понять, что было подключено новое устройство, или какое-то из устройств перезагрузилось.

## Порядок сканирования

Мастер выбирает настройки скорости и четности, отправляет с ними команду начала сканирования (`0x01`). Далее необходимо подождать стандартное время (3,5 фрейма) и после этого, если на шине есть устройства, то одно из них вернет ответ на сканирование командой `0x03`. Необходимо игнорировать все предшествующие байты `0xFF`. Если после времени ожидания ответа нет, то на шине нет устройств с данными настройками соединения и можно переходить к сканированию на других настройках порта. Если был ответ, то после него можно отправить команду команду продолжения сканирования (`0x02`) и получить данные о следующем устройстве. Это можно повторять, пока не придет ответ об окончании сканирования (`0x04`).

### Арбитраж в сканировании

Подробней об алгоритме арбитража можно почитать ниже.

Изначально на шине могут быть устройства с одинаковыми адресами. Поэтому в запросе на сканирование арбитраж проводится по уникальному серийному номеру устройства и занимает 32 **арбитражных окна**.

Для команды сканирования (`0x60`) действуют устаревшие правила арбитража, в которых длительность окна составляет 20 бит при текущем baud rate, а начало арбитража - через 44 бита после последнего принятого байта запроса - 3.5 байта.

Сканирование одного устройства будет занимать \*_(5 байт _ 10 бит) запрос + (44 бита) ожидание ответа + ((28 + 4) полей арбитража _ 20 бит на арбитражное окно) + (10 байт _ 10 бит) ответ = 834 битовых интервала, что на скорости 115200 будет длиться около 7.5 мс.

> Для ускорения выполнения кода в прерывании хочется использовать 32 бита архитектуры и ограничиться полем арбитража в 32 бита. Однако помимо серийника где-то в старших битах нужно передать признак наличия новых устройств. А в будущем, при децентрализованых режимах, еще какую-то информацию. Для этого нужно использовать меньшее количество бит серийного номера, например 28, и ограничить себя выпуском устройств с серийными номерами до 2 ^ 28 - это тоже очень много (можно натянуть еще и на CAN шину)

> Если вы реализуете данный протокол в DIY устройстве выбирайте серийный номер в диапазоне **0х0D000000 - 0х0D00FFFF**

## Работа с устройствами по серийному номеру

Если какие-то устройства имеют одинаковый modbus адрес, работать с ними через обычные modbus функции нельзя. Например, мы просканировали шину и хотим запросить модели устройств или версию прошивки до смены адреса. Предусмотрена возможность работать с устройством через его серийный номер. Адрес становится известен на этапе сканирования или доступен на наклейке. Есть возможность завернуть стандартную команду в пакет, использующий серийный номер вместо modbus адреса.

### Функция отправки стандартной команды - 0x08

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x08` субкоманда эмуляции стандартных запросов
- (4 байта) серийный номер устройства к которому обращаемся (big endian)
  --- обычный PDU ---
- (1 байт) стандарный код функции работы с регистрами (1-4, 5, 6, 15, 16)
  ... тело запроса
  --- обычный запрос ---
- (2 байта) контрольная сумма

Пример:

```
-> FD 46 08 00 01 EB 37 03 00 C8 00 14 5B 07
```

### Функция ответа на стандартную команду - 0x09

В результате устройство отвечает пакетом:

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x09` субкоманда эмуляции стандартных запросов
- (4 байта) серийный номер устройства к которому обращаемся (big endian)
  --- обычный PDU ---
- (1 байт) стандартный код функции работы с регистрами (1-4, 5, 6, 15, 16)
  ... тело ответа
  --- обычный запрос ---
- (2 байта) контрольная сумма

Пример:

```
<- FD 46 09 00 01 EB 37 03 28 00 57 00 42 00 4D 00 53 00 57 00 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 30 4F
```

## Как работает арбитраж

Арбитраж - это способ устройствам самим решить, кто будет отвечать на запрос, а также понять, остались ли еще желающие ответить. Арбитраж происходит по уникальному для каждого устройства значению (например, по серийному номеру или `slave_id`, в зависимости от команды). Принцип аналогичен арбитражу в CAN шине. Каждое устройство передает один бит своего уникального значения по порядку, начиная с MSB. Когда устройство, передающее рецессивный бит (1), обнаруживает на шине доминантный бит (0), оно проигрывает арбитраж. К завершению передачи уникального значения остается только одно устройство, которое выиграло арбитраж.

Передача арбитражных битов ведется по одному в **арбитражное окно** (интервал времени). Доминантный бит передается как отправка значения **0xFF** с использованием обычной передачи USART и аппаратным управлением драйвера шины. Рецессивный - ничего не передается в течении интервала окна, на шине молчание, трансивер не включается. Таким образом мы обходим аппаратные ограничения RS485 в которой невозможно одновременно передавать разные состояния. Схемотехника приемника стандартная. Его можно отключать на время передачи, это никак не влияет на работу протокола.

Перед передачей байта проверяется состояние флага BSY USART. Если другое устройство уже передало стартбит, то мы молчим в течении текущего **арбитражного окна**, подразумевая, что доминантный бит передан. Таким образом допустимо рассогласование таймера на несколько бит.

### Тайминги

Длительность арбитражного окна и время ожидания первого окна зависят от команды и настроек шины; это нужно для того, чтобы обеспечить стабильную работу на разных скоростях.

Весь процесс отправки ответа на запрос можно разделить на три фазы:

1.  Ожидание начала арбитража, за это время устройства должны дождаться окончания предыдущего пакета, успеть первично обработать запрос и понять, надо ли им участвовать в арбитраже.
2.  Арбитраж
3.  Отправка пакета с данными

Время ожидания начала арбитража ограничено снизу 800 мкс, это время на обработку прерывания контроллером с тактовой частотой 8 МГц (выбрано экспериментально), и временем передачи 3 символов (взято из спецификации Modbus, правда, там 3.5 символа).

Длительность арбитражного окна - время приёма 12 битов (одного UART-фрейма с 2 стоп-битами и битом чётности) + запас времени на обработку прерывания. Запас времени на прерывание ограничен снизу 50 мкс (выбрано экспериментально) и кратно времени передачи 1 бита.

Таймаут на приём ответа считается как `ожидание начала арбитража + длительность арбитражного окна * количество бит в арбитраже`. Для сканирования количество бит арбитража - 32.

_Для опроса событий можно использовать оптимизированное значение таймаута, см. ниже._

Формула для подсчёта таймаута:

```
max(3.5 symbols, (12 bits + 800us)) + N * max(13 bits, 12 bits + ceil_bits(50us))
```

где `N` - количество арбитражных окон, `ceil_bits(x)` - округление вверх до числа, кратного длине передачи бита. Например, для бодрейта 57600 (время передачи одного бита 17 мкс) это будет `ceil(50 / 17) = ceil(2.88) = 3`.

## События

Библиотека позволяет быстро опрашивать события, возникшие на устройствах без поочередного опроса каждого из них. **Это возможно только если в сети нет устройств с одинаковым slave ID**.
Для опроса событий предусмотрены следующие функции: запрос событий, передача событий и событий нет.

### Функция запроса событий - 0x10

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x10` субкоманда - запрос событий от устройств
- (1 байт) минимальный slave id устройства с которого начать отвечать
- (1 байт) максимальная длина поля данных с событиями в пакете, которую ожидает мастер, по стандарту длина всего пакета не должна превышать 256 байт.
- (1 байт) `slave_id` устройства, от которого был получен предыдущий пакет с событиями
- (1 байт) флаг предыдущего полученного пакета для подтверждения приёма (см. ниже).
- (2 байта) контрольная сумма

Если в заданную максимальную длину поля данных не вмещается ни одно событие, ответ придёт без payload (списка событий), из него можно будет узнать только количество новых событий.

В этот запрос обязательно нужно вставлять флаг для подтверждения получения из предыдущего принятого пакета для этого `slave_id`, иначе события не будут сбрасываться.

Пример без ограничения по `slave_id` с длиной поля событий 100 байт. Предыдущий пакет с событием был принят от устройства 0x0A с битом 1:

```
-> FD 46 10 00 64 0A 01 XX XX
```

### Функция передачи событий - 0x11

- (1 байт) `slave_id` устройства
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x11` субкоманда - передача событий от устройства
- (1 байт) флаг этого пакета для подтверждения получения (см. ниже)
- (1 байт) количество не сброшенных событий
- (1 байт) длина поля данных всех событий в байтах до контрольной суммы
- (4 и более байт) событие 1
  ...
- (4 и более байт) событие N
- (2 байта) контрольная сумма

Событие содержит 4 и более байт и имеет следующий формат:

- (1 байт) длина дополнительных данных события
- (1 байт) тип события
- (2 байта) идентификатор события (big endian)
- (0 и более байт) дополнительные данные события (формат little endian)

Пример: отвечает устройство с адресом 5 событие об изменении input регистра 0x01D0, новое значение 4, одно не отправленное событие, флаг подтверждения пакета установлен:

```
<- 05 46 11 01 01 06 02 04 01 D0 04 00 XX XX
```

### Функция ответа если события отсутствуют - 0x12

Этот пакет отправляется устройством, выигравшим арбитраж (маркер наименьшего приоритета + `slave_id`).

- (1 байт) `0xFD` широковещательный адрес
- (1 байт) `0x46` команда работы с расширенными функциями
- (1 байт) `0x12` суб команда - событий нет
- (2 байта) контрольная сумма

Пример:

`<- FD 46 12 52 5D`

Цикл запроса события и ответом устройства об одном событии с 2 байтами данных будет занимать **42** фрейма, или **48.125** мс (9600) / **4.01** мс (115200)

## Порядок опроса событий

Цикл опроса можно начинать после завершения любой команды, включая ответ подчиненного устройства. Запрос начинается с отправки мастером пакета с командой запроса событий `0x10`. Если подтверждать нечего, в поле подтверждения установить 0 адрес 0 флаг.

Устройства через стандартные 3,5 фрейма проводят арбитраж (см. описание ниже), и если есть устройства желающие сообщить о наступивших событиях, то устройство, выигравшее арбитраж, отвечает пакетом с командой `0x11`, в котором передает список событий. События имеют идентификатор, тип, и опционально дополнительные данные. Максимальный размер пакета составляет по стандарту 256 байт. Если события не влезают в один пакет, то при следующем запросе устройство будет продолжать выигрывать арбитраж и передавать события.

Далее мастер может повторить цикл и запросить события снова.

Подтверждение получения событий мастером делается через параметры в следующем запросе, чтобы экономить время при опросе, см. "Подтверждение приёма событий"

Если какое-то устройство передает слишком много событий и мешает передавать события остальным, мастер может указать в пакете запроса slave id больше, чем у устройства, чтобы пропустить это устройство (вместе со всеми остальными, у которых `slave_id` меньше заданного).

### Арбитраж в событиях

Для арбитража при опросе событий используется конкатенация маркера приоритета пакета (_не путать с приоритетом события_) (4 бита) и `slave_id` устройства (8 бит).

> Использовать `slave_id` вместо серийного номера устройства лучше, потому что арбитраж занимает в 4 раза меньше времени, при этом на момент настройки опроса событий у всех устройств уже ожидаются уникальные значения `slave_id`.

Маркер приоритета в начале позволяет устройствам с более приоритетными событиями выигрывать арбитраж раньше, тем самым уменьшая задержку для этих событий. Также маркер с самым низким приоритетом (`0xF`) используется устройствами при отсутствии новых событий, что позволяет одному из устройств ответить за всех, что событий нет (команда `0x12`).

На сегодняшний день доступны два приоритета событий (`HIGH` и `LOW`).

### Таймаут ожидания ответа в событиях

В реализации опроса событий можно использовать стандартную формулу времени ожидания, в которой ожидается 12 бит арбитража (4 бита маркера приоритета и 8 бит `slave_id`). На практике можно использовать хак, который позволяет сократить это время.

Напомним, что в арбитраже бит 0 - доминантный (читай - более приоритетный). Рецессивный бит (1) фактически не отправляется в шину во время арбитража.

Фактически, таймаут ответа - это время от отправки запроса до получения первого символа на шине, которым может быть либо доминантный бит арбитража, либо начало сообщения. В арбитраже по `slave_id` раньше появятся старшие биты (MSB).

`slave_id` в Modbus - это значения от 1 до 247.

- для значений от 1 до 127 двоичная запись в 8 битах начинается с `0` => первый символ в шине мы получим в пятое окно арбитража (первые 4 бита - за маркером приоритета);
- для значений от 128 до 191 двоичная запись начинается с `10` => первый символ - в шестое окно арбитража;
- для значений от 192 до 223 двоичная запись начинается с `110` => седьмое окно;
- для значений от 224 до 239 двоичная запись начинается с `1110` => восьмое окно;
- для значений от 240 до 247 двоичная запись начинается с `11110` => девятое окно;
- значений больше 247 быть не может.

Из этого рассуждения следует, что если мы не получим фрейм после времени ожидания 9 арбитражных окон, то на шине нет устройств, участвующих в арбитраже.

Таким образом, можно сократить время ожидания с 12 арбитражных окон до 9 (+ ожидание начала арбитража).

## Подтверждение приема событий

Гарантия доставки реализована с помощью механизма подтверждения приема пакета с событиями для каждого устройства.

Каждый пакет с событиями имеет поле флага. Это своеобразный номер пакета, имеющий значение 0 или 1. Устройства в каждом пакете инвертирует флаг относительно предыдущего отправленного им же. Таким образом не существует двух соседних пакетов от одного устройства с одинаковым значением флага. Флаги от разных устройств никак не связаны.

Когда мастер корректно принял пакет с событиями, он должен подтвердить устройству получение событий. Мастер должен запомнить последнее значение флага в пакете для каждого устройства. В следующем цикле запроса событий в команде запроса `0x10` в поле подтверждения мастер указывает slave id устройства и флаг, который был указан в предыдущем принятом пакете с событиями. Этот пакет получают все устройства, даже если они не выиграли арбитраж, и если они видят в поле подтверждения в пакете свой `slave_id`, забывают ранее отправленную пачку событий (с соответствующим флагом подтверждения).

Возможные варианты ошибок при подтверждении:

- ошибка при передаче от мастера с одним устройством:

  - устройства не отреагируют. Мастер получит таймаут и снова отправит запрос с тем же полем подтверждения.

- ошибка при передаче от слэйва с одним устройством:

  - мастер примет битый пакет. Запросит снова с тем же полем подтверждения, слэйв устройство увидит, что флаг подтверждения не соответствует ранее отправленному пакету (который потерялся), поймет что события не доставлены и снова вышлет пакет с тем же флагом, что и в потерянном пакете с теми же событиями. Возможно, дополнит пакет свежими событиями.

- ошибка при передаче от слэйва с несколькими устройствами:

  - мастер примет битый пакет. Запросит снова с тем же полем подтверждения, слэйв устройство увидит, что флаг подтверждения не соответствует ранее отправленному пакету, и попробует отправить его снова. Он может проиграть арбитраж, тогда этот пакет будет отправлен в один из следующих запросов.

- перезагрузка мастера:

  - мастер начинает слать запросы с пустым полем подтверждения. Все устройства повторят пакеты если они не были подтверждены ранее.

- перезагрузка слэйва:

  - мастер при очередном опросе событий получит событие перезагрузки от слейва и синхронизирует флаг с пакетом. Даже если устройство будет отвечать на запрос, в котором подтверждается его адрес, при любом варианте флага будет отправлен пакет с событием перезагрузки.

- ошибка при приёме подтверждения слэйвом:
  - слэйв не сбросит события у себя, но мастер будет уверен, что события сбросились. В следующем цикле опроса слэйв отправит тот же комплект событий, что и в прошлый раз, и может дополнить новыми. Если мастер запомнит, какой пакет событий в последний раз получал от этого слэйва, он может сравнить новый пакет со старым и учесть только те события, которых нет в старом, но есть в новом. Если не запомнит - опубликует тот же комплект событий заново (_потенциальное дублирование событий_).

## Источники событий

Аналогично регистрам, каждое устройство может генерировать события. Например, `было распознано двойное нажатие`. События имеют 16-битные идентификаторы. Архитектура позволяет событиям иметь тип и полезные данные: например, установленную яркость или метку времени. Можно организовать очереди событий: например в полезных данных иметь индекс и сбрасывать события до принятого индекса, таким образом можно организовать хранение и передачу нескольких однотипных событий без пропуска отдельного срабатывания. Также через полезные данные можно передавать время срабатывания и сбрасывать события, возникшие до последней принятой метки.

### Уведомления об изменении регистра

В первую очередь решено сделать уведомления о том, что у устройства изменилось значение регистра. Для этого резервируются 4 типа событий: `1 - 4` соответственно для `coil`, `discrete`, `holding`, `input` (аналогично стандартным функциям чтения). Поле данных может содержать от 1 до 8 байт данных в зависимости от типа и смысла регистра. Например, для `coil` и `discrete` передается 1 байт, а значение освещенности WB-MSW v3, расположенное в 9 и 10 регистрах, передается как 4-байтные данные в формате little endian.

Технически, передавать события может каждый регистр, который может изменяться в устройстве не только через запись по modbus. Для ограничения трафика на шине это поведение настраивается через специальную команду. По умолчанию передача событий изменения регистров выключена. После установки разрешения передачи событий по изменению значения на конкретный регистр устройство начнет отправлять событие.

### Детектирование сброса устройства

Как только прошивка запускается, она информирует об этом событием о включении. Это единственное событие, которое разрешено после включения.
Считав данное событие, мастер понимает, что устройство было перезагружено и необходимо заново сконфигурировать генерацию событий. Отправлять следующий запрос событий имеет смысл только после успешной конфигурации генерации событий на устройстве. После сброса событие не возникнет, пока устройство не перезагрузится.

Событие включения имеет тип `0x0F`, идентификатор 0, приоритет низкий.

Если мастер перезапустился и планирует конфигурировать все устройства в любом случае, то он может выключить отправку события включения, чтобы не тратить на его получение время во время следующего цикла запроса событий.

Для обхода ситуации, когда слэйв может не получить от мастера пакет запроса с подтверждением его события включения, во время конфигурирования следует отключать отправку события включения. Даже если мы только что получили это событие.

### Функция настройки отправки событий - 0x18

Нам нужно максимально быстро, т.е. за одну транзакцию, сконфигурировать всё, что нам нужно для работы с событиями. Помимо включения, мы можем настраивать разные параметры, например, выбрать приоритет доставки события.

Описание команды:

- (1 байт) slave id адрес устройства
- (1 байт) `0x46` команда управления разрешением передачи событий
- (1 байт) `0x18` субкоманда - управления разрешением передачи событий изменения значения регистра
- (1 байт) длина списка настроек
- (5 и более байт) настройки отправки событий диапазоном регистров 1
  ...
- (5 и более байт) настройки отправки событий диапазоном регистров N
- (2 байта) контрольная сумма

Поле настройки отправки события имеет следующий формат:

- (1 байт) тип регистра
- (2 байта) адрес регистра (big endian)
- (1 байт) количество регистров подряд
- (N байт) настройка для каждого регистра
  0 - отправка события не активна
  1 - включить отправку с низким приоритетом
  2 - включить отправку с высоким приоритетом

Пример включения отправки событий при изменении discrete регистров 4 и 6 с низким приоритетом, а также input регистров 464, 466 и 473 с высоким приоритетом на устройстве с адресом 0x0A:

```
                ,-- тип (discrete)
                |   ,-- адрес (4)
                |   |    ,-- количество
                |   |    |  ,-- регистр 4
                |   |    |  |     ,-- регистр 6
                |   |    |  |     |                                            CRC
                | __|__  |  |     |                                           __|__
-> 0A 46 18 15 02 00 04 03 01 00 01 04 01 D0 0A 02 00 02 00 00 00 00 00 00 02 33 A3
    |  |  |  |                       | -----  |  |     |                    |
    |  |  |  `-- длина (20)          |   |    |  |     |                    `-- регистр 473
    |  |  `-- суб команда            |   |    |  |     `-- регистр 466
    |  `-- команда                   |   |    |  `-- регистр 464
    `-- slave id                     |   |    `-- количество регистров (10)
                                     |   `-- адрес (464)
                                     `-- тип (input)
```

Ответ от устройства:

- (1 байт) slave id адрес устройства
- (1 байт) `0x46` команда управления разрешением передачи событий
- (1 байт) `0x18` субкоманда - управления разрешением передачи событий изменения значения регистра
- (1 байт) длина списка значений настройки
- (M байт) флаги разрешения отправки событий для диапазона 1
  ...
- (M байт) флаги разрешения отправки событий для диапазона N
- (2 байта) контрольная сумма

В ответной команде опущены поля тип регистра, адрес, и количество регистров подряд. Содержатся только битовые маски с состоянием настройки передачи событий. Биты упакованы аналогично команде `0x01`, от LSB к MSB с увеличением ID события. Количество байт - это `(количество регистров подряд / 8 с округлением вверх)`. Биты сгруппированы в блоки, с выравниванием по границе байта, повторяя диапазоны в запросе. Формат битовых масок - little endian.

Пример ответа на команду выше, устройство с адресом 0x0A. `discrete` регистры 4 и 6, а также `input` регистры 464, 466 включены. `input` регистр 473 остался отключенным (потому что он вообще не существует, но нас интересует лишь то, что он не будет передавать события, позже, опросив его стандартной командой modbus, мы поймем что он не существует).

```
                ,-- первый блок: 101(0 0000)
                |   ,-- второй блок: 1010 0000 00(00 0000)
                |  _|_
<- 0A 46 18 03 05 05 00 XX XX
    |  |  |  |          -----
    |  |  |  |            |
    |  |  |  |           CRC
    |  |  |  `-- длина (3)
    |  |  `-- суб команда
    |  `-- команда
    `-- slave id
```

Такой формат позволяет включить все желаемые события за 1 цикл обмена на шине, что сильно экономит время и оставляет гибкость для ситуаций, когда прошивка поддерживает не все события.

Для включения отправки событий от 32 регистров счетчиков нажатий потребуется время **70** фреймов, или **80.208** мс (9600) / **6.684** мс (115200)
На 30 устройствах это будет занимать **2100** фреймов, или **2406.25** мс (9600) / **200.521** мс (115200)

Если устройство не поддерживает отправку событий, то на команды управления событиями устройство ответит ошибкой 1 (ILLEGAL FUNCTION) _или какой то другой ошибкой с битом 0x80 в коде функции - ошибка в firmware_.
