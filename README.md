# stm32_com_port

Библиотека на C для обмена структурами между STM32 и компьютером (Linux Ubuntu или Windows 10/11
x64) по USB COM-порту. Вы описываете две структуры (в одну сторону и в обратную, они могут быть
разными, вложенные допускаются, до 1024 байт каждая), а библиотека сама упаковывает их в кадры,
проверяет CRC32 и отдаёт целую структуру на другой стороне.

## Возможности

- Обмен структурами в обе стороны, структуры туда и обратно разные.
- Проверка CRC32 на каждом кадре; испорченные кадры отбрасываются, поток сам восстанавливается.
- Отправка в момент вызова `Send`; ориентир по частоте — до 100 Гц.
- STM32: обработчик принятой структуры задаётся в конфигурации при инициализации.
- STM32: работа без ОС и без `malloc`, безопасно относительно любых прерываний.
- STM32: несколько независимых экземпляров (по умолчанию два), например USB FS и USB HS сразу.
- Хост: один код и один API для Linux и Windows, безопасные `Send` и `GetRx` из разных потоков.
- Контроль потери связи (тайм-аут) и счётчики ошибок на обеих сторонах.
- Готовые примеры для STM32, Linux и Windows, которые работают друг с другом.

## Требования к настройке в CubeMX

| Параметр | Значение |
|---|---|
| Connectivity -> USB_OTG_FS (или HS) | Mode: Device_Only |
| Middleware -> USB_DEVICE -> Class For FS (HS) IP | Communication Device Class (Virtual Port Com) |
| Тактирование USB | 48 МГц (Clock Configuration) |
| NVIC | прерывание USB включено (приоритет любой) |

Скорость порта в CubeMX не важна: обмен идёт по USB на скорости шины.

## Быстрый старт

Файлы для проекта STM32 (CubeIDE): `common/scom_crc32.c/.h`, `common/scom_frame.c/.h`,
`stm32/scom_stm32.c/.h` и ваш `my_types.h`; `.c` — в `Core/Src`, `.h` — в `Core/Inc`. Эти папки
уже входят в пути сборки, настраивать ничего не нужно. Для Keil, IAR и Makefile добавьте файлы
в проект, а папки с `.h` — в Include paths.

Файлы для программы на компьютере: `common/scom_crc32.c/.h`, `common/scom_frame.c/.h`,
`host/scom_host.c/.h`, `host/scom_host_port.h`, `host/scom_host_posix.c`,
`host/scom_host_win32.c` и ваш `my_types.h`. Все `.c` добавляются в сборку; файл для чужой ОС
внутри пустой.

### Общий файл со структурами

Один и тот же `my_types.h` подключается и в проект STM32, и в программу для компьютера.
Структуры называйте и заполняйте своими; здесь названия просто указывают направление.
Используйте только типы фиксированной ширины (`uint8_t`, `int16_t`, `uint32_t`, `float`); без
указателей, `bool`, `enum` и `long`.

```c
/* my_types.h */
#include <stdint.h>
#include "scom_frame.h"

SCOM_PACK_BEGIN                                   /* структуры без дыр выравнивания */
typedef struct { float x, y, z; } Vec3_t;         /* вложенная структура */
typedef struct { uint32_t counter; Vec3_t vec; } StmToHost_t;    /* STM32 -> компьютер */
typedef struct { uint8_t mode; float value; } HostToStm_t;       /* компьютер -> STM32 */
SCOM_PACK_END
```

### STM32: main.c

Вставляйте код только внутри блоков `USER CODE BEGIN ... / USER CODE END ...`: остальное CubeMX
перезаписывает при генерации. Название блока указано в комментарии.

```c
/* USER CODE BEGIN Includes */
#include "scom_stm32.h"
#include "my_types.h"
/* USER CODE END Includes */
```

```c
/* USER CODE BEGIN PV */
SCOM_Handle_t *g_scom;                 /* экземпляр библиотеки, нужен и в usbd_cdc_if.c */
StmToHost_t tx;                        /* то, что отправляем компьютеру */
volatile uint8_t g_mode;               /* поля принятой структуры; их пишет обработчик */
volatile float   g_value;
static uint32_t next_send;             /* когда отправлять следующий раз */
/* USER CODE END PV */
```

Обработчик принятой структуры — ваша функция; библиотека сама вызывает её, когда от компьютера
пришла целая структура с верным CRC32. Она работает в прерывании USB, поэтому должна быть
короткой, а `data` действителен только внутри неё: забирайте нужные поля в свои переменные.

```c
/* USER CODE BEGIN 0 */
static void on_rx(void *user, const void *data)
{
    const HostToStm_t *rx = (const HostToStm_t *)data;
    (void)user;
    g_mode  = rx->mode;
    g_value = rx->value;
}
/* USER CODE END 0 */
```

В функции `main()`, сразу после `MX_USB_DEVICE_Init();`:

```c
  /* USER CODE BEGIN 2 */
  SCOM_Config_t cfg = {0};
  cfg.port          = SCOM_PORT_USB_FS;      /* или SCOM_PORT_USB_HS */
  cfg.tx_size       = sizeof(StmToHost_t);   /* размер структуры, которую ОТПРАВЛЯЕМ */
  cfg.rx_size       = sizeof(HostToStm_t);   /* размер структуры, которую ПРИНИМАЕМ  */
  cfg.on_rx         = on_rx;                 /* ваш обработчик принятой структуры    */
  cfg.rx_timeout_ms = 200;                   /* нет кадров 200 мс - связь потеряна   */
  g_scom = SCOM_Init(&cfg);                  /* NULL - ошибка конфигурации           */
  /* USER CODE END 2 */
```

В бесконечном цикле `while (1)`:

```c
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* ПЕРЕДАЧА: например, раз в 10 мс. SCOM_BUSY - предыдущий кадр ещё уходит,
       этот не отправлен; в следующий раз передайте свежие данные. */
    if ((int32_t)(HAL_GetTick() - next_send) >= 0)
    {
        next_send = HAL_GetTick() + 10;
        tx.counter++;                      /* заполните свои данные */
        SCOM_Send(g_scom, &tx);
    }

    if (SCOM_IsTimeout(g_scom))
    {
        /* от компьютера нет кадров дольше 200 мс */
    }
  }
  /* USER CODE END 3 */
```

Вместо обработчика структуру можно забирать опросом: `SCOM_GetRx(g_scom, &rx)` возвращает `1`,
если пришла новая (см. [API_REFERENCE.md](API_REFERENCE.md)).

### STM32: usbd_cdc_if.c

Файл лежит в `USB_DEVICE/App`. Тоже только внутри блоков `USER CODE`. В начале файла:

```c
/* USER CODE BEGIN INCLUDE */
#include "scom_stm32.h"
extern SCOM_Handle_t *g_scom;          /* создан в main.c */
/* USER CODE END INCLUDE */
```

В функции `CDC_Receive_FS` добавьте первую строку в блоке `USER CODE BEGIN 6`, остальное
оставьте как есть:

```c
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  SCOM_OnReceive(g_scom, Buf, *Len);                 /* <-- добавить: передать принятые байты */
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}
```

В функции `CDC_TransmitCplt_FS` (блок `USER CODE BEGIN 13`). Вызов обязателен: без него после
первой отправки следующая пройдёт только по тайм-ауту 50 мс. Если функции `CDC_TransmitCplt_FS`
в файле нет (старая версия пакета STM32Cube), обновите пакет в CubeMX (Project Manager ->
Firmware Version) и пересоздайте код.

```c
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  SCOM_OnTxComplete(g_scom);                         /* <-- добавить: USB отправил кадр */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 13 */
  return result;
}
```

### STM32: второй USB-порт

Второй экземпляр создаётся с другим портом; те же два вызова добавьте в `CDC_Receive_HS` /
`CDC_TransmitCplt_HS` со своим хэндлом `g_scom_hs`. Размеры структур и обработчики у экземпляров
независимы.

```c
/* main.c: USER CODE BEGIN PV */
SCOM_Handle_t *g_scom_hs;

/* main.c: USER CODE BEGIN 2, после SCOM_Init для первого порта */
SCOM_Config_t cfg_hs = {0};
cfg_hs.port    = SCOM_PORT_USB_HS;         /* другой порт - другой экземпляр */
cfg_hs.tx_size = sizeof(StmToHost_t);
cfg_hs.rx_size = sizeof(HostToStm_t);
cfg_hs.on_rx   = on_rx;                    /* можно свой обработчик */
g_scom_hs = SCOM_Init(&cfg_hs);
```

### Компьютер: программа

Файл `main.c` рядом с `my_types.h`.

```c
#include <stdio.h>
#include "scom_host.h"
#include "my_types.h"

int main(void)
{
    static SCOM_HostHandle_t com;
    SCOM_HostConfig_t cfg = {0};
    StmToHost_t rx = {0};              /* сюда придёт то, что отправил STM32 */
    HostToStm_t tx = {0};              /* то, что отправляем STM32 */

    cfg.device        = "/dev/ttyACM0";        /* Windows: "COM5" */
    cfg.tx_size       = sizeof(HostToStm_t);   /* ОТПРАВЛЯЕМ то, что STM32 принимает */
    cfg.rx_size       = sizeof(StmToHost_t);   /* ПРИНИМАЕМ то, что STM32 отправляет */
    cfg.rx_timeout_ms = 200;

    if (SCOM_HostOpen(&com, &cfg) != 0)
    {
        printf("Не удалось открыть порт\n");
        return 1;
    }

    for (;;)
    {
        /* Принять всё, что пришло (ждёт до 10 мс). Вызывать не реже раза в 10-20 мс.
           -1 - порт пропал (кабель, перезагрузка STM32): закрыть и открыть заново. */
        if (SCOM_HostPoll(&com, 10) < 0)
        {
            break;
        }

        if (SCOM_HostGetRx(&com, &rx))                 /* 1 - пришла новая структура */
        {
            printf("counter=%u\n", (unsigned)rx.counter);
        }

        tx.mode = 1;
        SCOM_HostSend(&com, &tx);          /* отправить сейчас, можно из любого потока */
    }

    SCOM_HostClose(&com);
    return 0;
}
```

Сборка (в папке с `main.c`, `my_types.h` и папками `common`, `host`).

Linux:

```bash
gcc -std=c99 -Wall -pthread -Icommon -Ihost -I. main.c common/scom_crc32.c common/scom_frame.c host/scom_host.c host/scom_host_posix.c host/scom_host_win32.c -o app
./app
```

Windows (в «x64 Native Tools Command Prompt for VS»):

```bat
cl /std:c11 /W3 /Icommon /Ihost /I. main.c common\scom_crc32.c common\scom_frame.c host\scom_host.c host\scom_host_posix.c host\scom_host_win32.c /Fe:app.exe
app.exe
```

В Visual Studio добавьте те же `.c` файлы в проект x64, а в Additional Include Directories
укажите `common;host;.`. Номер COM-порта смотрите в «Диспетчере устройств», раздел «Порты (COM и
LPT)». На Linux порт доступен группе `dialout` (`sudo usermod -aG dialout $USER`, затем
перелогиньтесь); `ModemManager` иногда занимает `/dev/ttyACM*`.

### Готовые примеры

Три примера работают друг с другом «из коробки». Структуры лежат в `examples/example_types.h`
(заглушки с вложенными структурами и массивом, замените своими). STM32 раз в 10 мс отправляет
заглушку и принимает структуру от компьютера; компьютер раз в 10 мс отправляет свою и выводит
принятую в консоль без мерцания. Строка `echo of what STM32 received from host` подтверждает
приём в обе стороны.

Пример STM32: скопируйте файлы проекта STM32 (см. выше), а также `examples/example_types.h` и
`examples/stm32/example_stm32.c/.h` (`.c` в `Core/Src`, `.h` в `Core/Inc`) и добавьте по одной
строке в блоки `USER CODE`:

| Файл | Блок | Что вставить |
|---|---|---|
| `main.c` | `Includes` | `#include "example_stm32.h"` |
| `main.c` | `2` (после `MX_USB_DEVICE_Init();`) | `Example_Init();` |
| `main.c` | `3` (внутри `while (1)`) | `Example_Process();` |
| `usbd_cdc_if.c` | `INCLUDE` | `#include "example_stm32.h"` |
| `usbd_cdc_if.c` | `6` (в начале `CDC_Receive_FS`) | `Example_UsbOnReceive(Buf, *Len);` |
| `usbd_cdc_if.c` | `13` (в `CDC_TransmitCplt_FS`) | `Example_UsbOnTxComplete();` |

Светодиод по полю `led_on`: определите в настройках проекта `EXAMPLE_LED_PORT` и
`EXAMPLE_LED_PIN` (например, `GPIOD` и `GPIO_PIN_12`); для USB HS —
`EXAMPLE_USB_PORT=SCOM_PORT_USB_HS`.

Пример Linux:

```bash
cd examples/linux
make
./example_linux /dev/ttyACM0
```

Пример Windows (в «x64 Native Tools Command Prompt for VS»):

```bat
examples\windows\build_msvc.bat
examples\windows\build\example_windows.exe COM5
```

Для Linux и Windows работает и CMake: `cmake -S examples -B build`, затем
`cmake --build build --config Release`. Чтобы вывести в монитор свою структуру, замените
`example_types.h` и код вывода в `examples/monitor/example_monitor.c`.

Полный справочник по функциям и типам — [API_REFERENCE.md](API_REFERENCE.md).

## Честные ограничения

- Доставка не гарантируется: подтверждений и повторных передач нет. Испорченный кадр
  отбрасывается, следующий принимается как обычно.
- Очереди отправки и приёма нет: хранится и отправляется самое свежее. Если нельзя пропустить ни
  одной принятой структуры, складывайте их в свою очередь из обработчика `on_rx`.
- Потеря байта в середине кадра стоит до двух кадров, после чего связь восстанавливается сама.
- Совместимость структур двух сторон проверяется только по длине; различие полей одинаковой
  суммарной длины библиотека не обнаружит.
- Порядок байт на обеих сторонах должен быть одинаков (little-endian: STM32 и x86-64); пересчёта
  нет.
- STM32-часть рассчитана на стандартный USB CDC middleware от ST: она вызывает
  `CDC_Transmit_FS` / `CDC_Transmit_HS` из `usbd_cdc_if.c` и требует вызовов из `CDC_Receive_xx`
  и `CDC_TransmitCplt_xx`. Для сборки нужен компилятор со слабыми ссылками (GCC, Clang,
  armclang).
- Хост: поддерживаются Linux (POSIX) и Windows 10/11; `SCOM_HostPoll` вызывается из одного
  потока на порт; повторное открытие порта после обрыва — задача приложения.
- Windows: точность ожидания в приложении по умолчанию около 15 мс; для стабильного периода
  10 мс вызовите `timeBeginPeriod(1)` (как в примере).

## Лицензия

Библиотека распространяется на условиях **PolyForm Noncommercial License 1.0.0** — свободное
использование, копирование, изменение и распространение для любых НЕКОММЕРЧЕСКИХ целей, при
условии сохранения уведомления об авторских правах. Коммерческое использование требует отдельного
разрешения правообладателя. Полный текст — файл [LICENSE](LICENSE).
