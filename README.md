# stm32_com_port

Небольшая библиотека на языке C для обмена бинарными структурами между микроконтроллером STM32
и компьютером с Linux по USB COM-порту (CDC). Структура в одну сторону и структура в обратную
могут быть разными, каждая — до 1024 байт, вложенные подструктуры допускаются. Каждый кадр
защищён CRC32, приём устойчив к потере синхронизации. Библиотека состоит из двух частей с общим
ядром: STM32 (HAL, без ОС) и Linux (termios).

## Возможности

- Передача и приём произвольных `packed`-структур (в том числе вложенных) в обе стороны;
  типы структур на разных сторонах и направлениях независимы.
- Проверка целостности CRC32 (IEEE 802.3, совместим с `zlib.crc32`), автоматическая
  ресинхронизация по признаку начала кадра.
- Отправка в момент вызова `Send`: библиотека не навязывает период, ориентир — до 100 Гц.
- STM32: потокобезопасность относительно любых прерываний (короткие критические секции через
  PRIMASK, макросы переопределяются), работа без ОС и без `malloc`.
- STM32: несколько независимых экземпляров (например, USB FS и USB HS одновременно, каждый со
  своим Linux-компьютером).
- STM32: обработчик принятой структуры задаётся в конфигурации, либо данные забираются опросом.
- Контроль тайм-аута связи на обеих сторонах.
- Linux: неблокирующий приём через `poll`, потокобезопасный `Send` и `GetRx`.
- Диагностические счётчики: принятые кадры, ошибки CRC и длины, отправленные и отброшенные.
- Пример консольного монитора Linux с выводом без мерцания и пример заглушки телеметрии на STM32.

## Требования к настройке в CubeMX

| Параметр | Значение |
|---|---|
| Connectivity → USB_OTG_FS (или HS) | Mode: Device_Only |
| Middleware → USB_DEVICE → Class For FS (HS) IP | Communication Device Class (Virtual Port Com) |
| Тактирование USB | 48 МГц (Clock Configuration) |
| NVIC | прерывание USB включено (приоритет любой) |
| USB HS в режиме FS PHY | поддерживается, настраивается как обычный CDC |

Скорость и параметры порта в CubeMX не имеют значения: обмен идёт по USB на скорости шины.
Библиотека не трогает HAL напрямую: вы вызываете две её функции из сгенерированных CubeMX
файлов `usbd_cdc_if.c` и передаёте одну функцию отправки.

Файлы для копирования в проект STM32: `common/*` и `stm32/*`. Для Linux: `common/*` и `linux/*`.

## Быстрый старт

Общий заголовок со структурами (одинаковый на обеих сторонах):

```c
#include "scom_frame.h"

typedef struct SCOM_PACKED { float x, y, z; } Vec3_t;
typedef struct SCOM_PACKED { uint32_t counter; Vec3_t accel; } Telemetry_t;  /* STM32 -> Linux */
typedef struct SCOM_PACKED { uint8_t mode; float setpoint; } Command_t;      /* Linux -> STM32 */
```

STM32 (`main.c`):

```c
#include "scom_stm32.h"

SCOM_Handle_t *g_scom;

static int usb_tx(void *user, const uint8_t *data, uint16_t len)
{
    (void)user;
    return (CDC_Transmit_FS((uint8_t *)data, len) == USBD_OK) ? 0 : -1;
}

static void on_command(void *user, const void *data)   /* из прерывания USB - коротко */
{
    (void)user;
    s_command = *(const Command_t *)data;
}

/* после MX_USB_DEVICE_Init(): */
SCOM_Config_t cfg = {0};
cfg.tx_size       = sizeof(Telemetry_t);
cfg.rx_size       = sizeof(Command_t);
cfg.tx_func       = usb_tx;
cfg.on_rx         = on_command;
cfg.rx_timeout_ms = 200U;
g_scom = SCOM_Init(&cfg);

/* когда нужно отправить (главный цикл, таймер, любой контекст): */
SCOM_Send(g_scom, &telemetry);

/* проверка связи: */
if (SCOM_IsTimeout(g_scom)) { /* нет валидных кадров дольше 200 мс */ }
```

STM32 (`usbd_cdc_if.c`, внутри блоков `USER CODE`):

```c
extern SCOM_Handle_t *g_scom;

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    SCOM_OnReceive(g_scom, Buf, *Len);                 /* добавить */
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return (USBD_OK);
}

static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    SCOM_OnTxComplete(g_scom);                         /* добавить */
    return USBD_OK;
}
```

Два USB-порта: заведите второй хэндл с другой функцией отправки (оболочка над
`CDC_Transmit_HS`) и вызывайте `SCOM_OnReceive` / `SCOM_OnTxComplete` со своим хэндлом в
`usbd_cdc_if.c` второго порта. Экземпляры полностью независимы, размеры структур у них могут
различаться.

Linux:

```c
#include "scom_linux.h"

static SCOM_LinuxHandle_t com;
SCOM_LinuxConfig_t cfg = {0};
Telemetry_t telemetry;
Command_t   command = {0};

cfg.device        = "/dev/ttyACM0";
cfg.tx_size       = sizeof(Command_t);      /* то, что принимает STM32 */
cfg.rx_size       = sizeof(Telemetry_t);    /* то, что отправляет STM32 */
cfg.rx_timeout_ms = 200U;
if (SCOM_LinuxOpen(&com, &cfg) != 0) { perror("open"); return 1; }

for (;;)
{
    SCOM_LinuxPoll(&com, 10);                       /* принять и разобрать кадры */
    if (SCOM_LinuxGetRx(&com, &telemetry)) { /* пришла новая структура */ }
    SCOM_LinuxSend(&com, &command);                 /* отправить, когда нужно */
}
```

Сборка: `gcc -std=c99 -pthread` с файлами `common/*.c` и `linux/scom_linux.c`. Готовый пример
с консольным монитором — в папке `examples/` (`make` в этой папке, затем
`./linux_example /dev/ttyACM0`). Для своей структуры достаточно заменить `example_types.h` и
функцию `example_draw()`. Пользователь должен иметь доступ к порту (группа `dialout`).
Менеджер `ModemManager` может занимать `/dev/ttyACM*` — при необходимости отключите его для
этого устройства правилом udev.

Полный справочник по функциям и типам — [API_REFERENCE.md](API_REFERENCE.md).

## Формат кадра и требования к структурам

```
| 0xA5 | 0x5A | LEN (2 байта, LE) | структура (LEN байт) | CRC32 (4 байта, LE) |
```

CRC32 считается по полям `LEN` и структуре. Кадр с неверным CRC или с длиной, не равной ожидаемой
`rx_size`, отбрасывается, приёмник ищет следующий признак начала.

Структуры передаются как есть, побайтно. Поэтому:

- объявляйте их с `SCOM_PACKED` и типами фиксированной ширины (`uint8_t`, `int16_t`, `float`);
- не используйте указатели, `bool`, `enum` неопределённой ширины и `long`;
- порядок байт одинаков на обеих сторонах (little-endian: STM32 и все распространённые Linux-
  платформы), пересчёта не выполняется;
- размер структуры проверяйте на этапе компиляции: `SCOM_STATIC_ASSERT`.

Максимальный размер структуры задаётся `SCOM_MAX_PAYLOAD_SIZE` (по умолчанию 1024). Уменьшение
до 512 сокращает расход ОЗУ: экземпляр STM32 занимает около трёх буферов такого размера.

## Честные ограничения

- Доставка не гарантируется: подтверждений и повторных передач нет. Испорченный кадр
  отбрасывается, следующий принимается как обычно.
- Очереди отправки нет: пока предыдущий кадр уходит в USB, `SCOM_Send` возвращает `SCOM_BUSY`.
  Для потоков телеметрии это правильное поведение (следующий кадр несёт свежие данные).
- Потеря байта в середине кадра стоит до двух кадров (испорченный и следующий, чьё начало мог
  «съесть» разборщик), после чего связь восстанавливается сама.
- Совместимость структур двух сторон проверяется только по длине; различие полей одинаковой
  суммарной длины библиотека не обнаружит.
- STM32-часть рассчитана на стандартный USB CDC middleware от ST (`CDC_Transmit_xx`,
  `CDC_Receive_xx`) с обработчиком завершения передачи `CDC_TransmitCplt_xx`.
- Функция `on_rx` вызывается из контекста, где вызвана `SCOM_OnReceive` (обычно прерывание USB),
  и должна быть короткой.
- Linux-часть: `SCOM_LinuxPoll` вызывается из одного потока; порт открывается только в Linux
  (termios/poll), других ОС нет.

## Лицензия

Библиотека распространяется на условиях **PolyForm Noncommercial License 1.0.0** — свободное
использование, копирование, изменение и распространение для любых НЕКОММЕРЧЕСКИХ целей, при
условии сохранения уведомления об авторских правах. Коммерческое использование требует отдельного
разрешения правообладателя. Полный текст — файл [LICENSE](LICENSE).
