# stm32_com_port — справочник по API

Дисклеймер: при расхождениях с заголовочными файлами (`scom_frame.h`, `scom_crc32.h`,
`scom_stm32.h`, `scom_host.h`) ориентируйтесь на `.h` — они первичны, этот справочник — их
сжатый пересказ.

## Оглавление

- [Общие define-ы и типы](#общие-define-ы-и-типы)
- [STM32: типы](#stm32-типы)
- [STM32: регистрация экземпляра](#stm32-регистрация-экземпляра)
- [STM32: обработчики USB](#stm32-обработчики-usb)
- [STM32: операционные функции](#stm32-операционные-функции)
- [Хост: типы](#хост-типы)
- [Хост: открытие и закрытие](#хост-открытие-и-закрытие)
- [Хост: операционные функции](#хост-операционные-функции)
- [Ядро: CRC32 и разбор кадров](#ядро-crc32-и-разбор-кадров)

---

## Общие define-ы и типы

Задаются до включения заголовков либо ключом `-D`, одинаково для всех файлов проекта.

| Define | По умолчанию | Описание |
|---|---|---|
| `SCOM_MAX_PAYLOAD_SIZE` | `1024U` | максимальный размер передаваемой структуры, байт |
| `SCOM_PACK_BEGIN` / `SCOM_PACK_END` | pragma pack(1) | упаковка структур на любом компиляторе (GCC, Clang, MSVC, armclang, IAR) |
| `SCOM_PACKED` | `__attribute__((packed))`, для MSVC пусто | атрибут упаковки только для GCC/Clang |
| `SCOM_ALIGN4` | `aligned(4)`, для MSVC пусто | выравнивание внутренних буферов |
| `SCOM_STATIC_ASSERT(cond, name)` | — | проверка условия на этапе компиляции (C99) |
| `SCOM_MAX_INSTANCES` | `2U` | STM32: число экземпляров (по одному на USB-порт) |
| `SCOM_DEFAULT_TX_TIMEOUT_MS` | `50U` | STM32: таймаут зависшей передачи по умолчанию, мс |
| `SCOM_GET_TICK_MS()` | `HAL_GetTick()` | STM32: источник миллисекундного времени |
| `SCOM_ENTER_CRITICAL(state)` | PRIMASK | STM32: вход в критическую секцию |
| `SCOM_EXIT_CRITICAL(state)` | PRIMASK | STM32: выход из критической секции |
| `SCOM_HOST_DEVICE_MAX` | `128U` | хост: максимальная длина имени порта |
| `SCOM_HOST_WRITE_TIMEOUT_MS` | `100` | хост: максимум ожидания готовности порта на запись, мс |

Формат кадра: `0xA5 0x5A | LEN (2, LE) | структура (LEN) | CRC32 (4, LE)`; накладные расходы —
`SCOM_FRAME_OVERHEAD` = 8 байт.

---

## STM32: типы

Заголовок: `scom_stm32.h`.

### `SCOM_Status_t`

| Значение | Описание |
|---|---|
| `SCOM_OK` | успех |
| `SCOM_ERROR` | неверные аргументы либо экземпляр не инициализирован |
| `SCOM_BUSY` | передача занята, кадр не отправлен, повторите позже |

### `SCOM_TxFunc_t` — `int (*)(void *user, const uint8_t *data, uint16_t len)`

Оболочка над `CDC_Transmit_xx`. Возвращает `0`, если USB принял данные, иначе не `0`. Буфер
`data` остаётся валидным до вызова `SCOM_OnTxComplete()`.

### `SCOM_RxCallback_t` — `void (*)(void *user, const void *data)`

Обработчик принятой структуры. Вызывается из контекста `SCOM_OnReceive` (обычно прерывание
USB), `data` валиден только во время вызова.

### `SCOM_Config_t`

| Поле | Тип | Описание |
|---|---|---|
| `tx_size` | `uint16_t` | размер отправляемой структуры (`sizeof`), `0` — только приём |
| `rx_size` | `uint16_t` | размер принимаемой структуры (`sizeof`), `0` — только передача |
| `tx_func` | `SCOM_TxFunc_t` | функция отправки в USB, обязательна при `tx_size != 0` |
| `on_rx` | `SCOM_RxCallback_t` | обработчик принятой структуры, может быть `NULL` |
| `user` | `void*` | указатель пользователя, передаётся в `tx_func` и `on_rx` |
| `rx_timeout_ms` | `uint32_t` | тайм-аут связи, мс; `0` — контроль отключён |
| `tx_timeout_ms` | `uint32_t` | тайм-аут зависшей передачи, мс; `0` — значение по умолчанию |

### `SCOM_Stats_t`

Поля `rx_frames`, `rx_crc_errors`, `rx_size_errors`, `tx_frames`, `tx_dropped`, `tx_timeouts`
(`uint32_t`) — счётчики событий с момента инициализации.

### `SCOM_Handle_t`

Возвращается `SCOM_Init()` по указателю. Память статическая (пул на `SCOM_MAX_INSTANCES`
элементов), освобождать не нужно. Публичные поля (можно читать): `config`, `stats`, `index`.
Остальные — внутренние.

---

## STM32: регистрация экземпляра

### `SCOM_Handle_t *SCOM_Init(const SCOM_Config_t *config)`

Регистрирует экземпляр; вызывать при старте, не из прерывания. Повторный вызов с теми же
`(tx_func, user)` идемпотентен — вернёт указатель на тот же хэндл. Возвращает `NULL`, если
конфигурация некорректна (оба размера нулевые, размер больше `SCOM_MAX_PAYLOAD_SIZE`, нет
`tx_func` при `tx_size != 0`) либо исчерпан пул.

---

## STM32: обработчики USB

### `void SCOM_OnReceive(SCOM_Handle_t *h, const uint8_t *buf, uint32_t len)`

Вызывать из `CDC_Receive_xx()`. Порции любой длины, кадр может быть разорван между вызовами. Для
одного хэндла вызывать из одного контекста. Проверяет CRC32, при успехе обновляет «последнюю
принятую» структуру и вызывает `on_rx`.

### `void SCOM_OnTxComplete(SCOM_Handle_t *h)`

Вызывать из `CDC_TransmitCplt_xx()`: освобождает передатчик для следующей отправки.

---

## STM32: операционные функции

Все функции быстрые, без циклов ожидания, потокобезопасны относительно любых прерываний.

### `SCOM_Status_t SCOM_Send(SCOM_Handle_t *h, const void *data)`

Копирует структуру размера `tx_size` в кадр и передаёт в USB. Если предыдущий кадр ещё уходит —
возвращает `SCOM_BUSY` (в очередь не ставится). Если завершение передачи не приходит дольше
`tx_timeout_ms`, передатчик освобождается принудительно.

| Параметр | Описание |
|---|---|
| `h` | хэндл экземпляра |
| `data` | структура размером `config.tx_size` |

Возврат: `SCOM_OK`, `SCOM_BUSY` или `SCOM_ERROR`.

### `uint8_t SCOM_GetRx(SCOM_Handle_t *h, void *out)`

Атомарно копирует в `out` последнюю принятую структуру (`rx_size` байт). Возвращает `1`, если
скопирована новая структура (пришедшая после прошлого вызова), `0` — если новых данных нет.

### `uint8_t SCOM_IsTimeout(const SCOM_Handle_t *h)`

Возвращает `1`, если с момента последнего валидного кадра (или инициализации) прошло больше
`rx_timeout_ms`; `0` — связь в норме либо контроль отключён.

---

## Хост: типы

Заголовок: `scom_host.h`. Один API для Linux и Windows.

### `SCOM_HostConfig_t`

| Поле | Тип | Описание |
|---|---|---|
| `device` | `const char*` | имя порта: Linux `"/dev/ttyACM0"`; Windows `"COM5"` (для COM10 и выше префикс `\\.\` добавляется автоматически) |
| `tx_size` | `uint16_t` | размер отправляемой структуры (её принимает STM32), `0` — только приём |
| `rx_size` | `uint16_t` | размер принимаемой структуры (её отправляет STM32), `0` — только передача |
| `rx_timeout_ms` | `uint32_t` | тайм-аут связи, мс; `0` — контроль отключён |

### `SCOM_HostStats_t`

Поля `rx_frames`, `rx_crc_errors`, `rx_size_errors`, `tx_frames` (`uint32_t`).

### `SCOM_HostHandle_t`

Память выделяет пользователь. Публичные поля: `config`, `stats`, `device`. Несколько
экземпляров (несколько STM32) независимы.

---

## Хост: открытие и закрытие

### `int SCOM_HostOpen(SCOM_HostHandle_t *h, const SCOM_HostConfig_t *config)`

Открывает порт (8N1, без управления потоком; на Windows включается DTR) и готовит экземпляр.
Возвращает `0` или `-1` (при ошибке порта: Linux — `errno`, Windows — `GetLastError()`).

### `void SCOM_HostClose(SCOM_HostHandle_t *h)`

Закрывает порт и освобождает ресурсы экземпляра.

---

## Хост: операционные функции

### `int SCOM_HostPoll(SCOM_HostHandle_t *h, int timeout_ms)`

Ждёт данные до `timeout_ms` (`0` — не ждать), вычитывает всё доступное и разбирает кадры.
Вызывать из одного потока. Возвращает число принятых валидных кадров (`0` и более) либо `-1` при
ошибке порта (устройство отключено).

### `int SCOM_HostSend(SCOM_HostHandle_t *h, const void *data)`

Отправляет структуру размера `tx_size` в момент вызова; допускается из любого потока. Возвращает
`0` или `-1` (неверные аргументы, порт не готов дольше `SCOM_HOST_WRITE_TIMEOUT_MS`, порт
отключён).

### `int SCOM_HostGetRx(SCOM_HostHandle_t *h, void *out)`

Копирует последнюю принятую структуру (`rx_size` байт); допускается из любого потока. Возвращает
`1`, если копия новая, `0` — новых данных нет.

### `int SCOM_HostIsTimeout(SCOM_HostHandle_t *h)`

Возвращает `1`, если с момента последнего валидного кадра (или открытия порта) прошло больше
`rx_timeout_ms`; иначе `0`.

### `uint64_t SCOM_HostNowMs(void)`

Монотонное время в миллисекундах, одинаково на Linux и Windows (для таймеров в вашем коде).

---

## Ядро: CRC32 и разбор кадров

Заголовки `scom_crc32.h`, `scom_frame.h`. Обычно напрямую не используются; нужны, если вы пишете
собственный транспорт.

| Функция | Назначение |
|---|---|
| `uint32_t SCOM_Crc32(const void *data, size_t len)` | CRC32 одним вызовом (`"123456789"` → `0xCBF43926`) |
| `uint32_t SCOM_Crc32Update(uint32_t crc, const void *data, size_t len)` | пошаговый расчёт, начальное значение `SCOM_CRC32_INIT` |
| `uint32_t SCOM_Crc32Final(uint32_t crc)` | завершение пошагового расчёта |
| `uint16_t SCOM_FrameBuild(uint8_t *out, const void *payload, uint16_t payload_size)` | сборка кадра, возвращает его длину |
| `void SCOM_ParserInit(SCOM_Parser_t *p, uint8_t *payload_buf, uint16_t payload_size)` | инициализация разборщика потока |
| `SCOM_ParseResult_t SCOM_ParserFeed(SCOM_Parser_t *p, const uint8_t *data, size_t len, size_t *consumed)` | разбор порции байт до первого события |

`SCOM_ParseResult_t`: `SCOM_PARSE_NEED_MORE`, `SCOM_PARSE_FRAME_OK`, `SCOM_PARSE_CRC_ERROR`,
`SCOM_PARSE_SIZE_ERROR`. Вызывать `SCOM_ParserFeed` нужно в цикле, пока не израсходованы все
байты порции:

```c
while (len > 0U)
{
    size_t n;
    SCOM_ParseResult_t r = SCOM_ParserFeed(&parser, data, len, &n);
    data += n;
    len  -= n;
    if (r == SCOM_PARSE_FRAME_OK) { /* нагрузка лежит в payload_buf */ }
}
```
