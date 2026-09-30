/**
 ******************************************************************************
 * @file    host_example.c
 * @brief   Пример хостовой части (Linux и Windows): принимает телеметрию от
 *          STM32, шлёт команды и выводит структуру в консоль без мерцания
 *          (на месте, один вывод за кадр). Для своей структуры правится
 *          только функция example_draw().
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.1
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

#include "scom_host.h"
#include "example_types.h"

#define EXAMPLE_SEND_PERIOD_MS       10      /* как часто слать команду     */
#define EXAMPLE_DRAW_PERIOD_MS       50      /* как часто перерисовывать    */

#ifdef _WIN32
#define EXAMPLE_DEFAULT_PORT         "COM3"
#else
#define EXAMPLE_DEFAULT_PORT         "/dev/ttyACM0"
#endif

static volatile sig_atomic_t s_stop = 0;

/**
 * @brief  Обработчик SIGINT/SIGTERM: просит главный цикл завершиться.
 * @param  sig  номер сигнала
 */
static void example_on_signal(int sig)
{
    (void)sig;
    s_stop = 1;
}

/**
 * @brief  Включает обработку ANSI-последовательностей в консоли Windows
 *         (на Linux ничего не делает).
 */
static void example_console_init(void)
{
#ifdef _WIN32
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0U;

    if ((out != INVALID_HANDLE_VALUE) && GetConsoleMode(out, &mode))
    {
        (void)SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

/**
 * @brief  Формирует текст экрана для телеметрии. Каждая строка имеет
 *         фиксированную ширину полей и заканчивается очисткой хвоста строки
 *         (ESC[K), поэтому картинка не "плавает" при смене значений. Это
 *         единственная функция, которую нужно менять под свою структуру.
 * @param  out       буфер для текста
 * @param  out_size  размер буфера
 * @param  t         последняя принятая телеметрия
 * @param  link_ok   1 - связь в норме
 * @param  stats     счётчики библиотеки
 * @param  cmd       последняя отправленная команда
 * @return длина текста в буфере
 */
static int example_draw(char *out, size_t out_size, const Example_Telemetry_t *t,
                        int link_ok, const SCOM_HostStats_t *stats,
                        const Example_Command_t *cmd)
{
    int n = 0;
    int i;

#define LINE(...) \
    do { n += snprintf(out + n, out_size - (size_t)n, __VA_ARGS__); \
         n += snprintf(out + n, out_size - (size_t)n, "\033[K\n"); } while (0)

    n += snprintf(out + n, out_size - (size_t)n, "\033[H");   /* курсор в начало */
    LINE("STM32 COM PORT monitor          (Ctrl+C - exit)");
    LINE("----------------------------------------------");
    LINE("Link           : %s", link_ok ? "OK      " : "TIMEOUT ");
    LINE("Frames rx     : %10u   CRC err: %6u   size err: %6u",
         (unsigned)stats->rx_frames, (unsigned)stats->rx_crc_errors,
         (unsigned)stats->rx_size_errors);
    LINE("Frames tx     : %10u", (unsigned)stats->tx_frames);
    LINE("%s", "");
    LINE("counter        : %10u", (unsigned)t->counter);
    LINE("uptime_ms      : %10u", (unsigned)t->uptime_ms);
    LINE("accel  x/y/z   : %9.3f %9.3f %9.3f",
         (double)t->accel.x, (double)t->accel.y, (double)t->accel.z);
    LINE("gyro   x/y/z   : %9.3f %9.3f %9.3f",
         (double)t->gyro.x, (double)t->gyro.y, (double)t->gyro.z);
    LINE("temperature    : %9.2f C", t->sensors.temperature_x100 / 100.0);
    LINE("vbat           : %6u mV", (unsigned)t->sensors.vbat_mv);
    LINE("status_flags   :       0x%02X", (unsigned)t->sensors.status_flags);
    n += snprintf(out + n, out_size - (size_t)n, "adc            :");
    for (i = 0; i < 8; i++)
    {
        n += snprintf(out + n, out_size - (size_t)n, " %5u", (unsigned)t->adc[i]);
    }
    n += snprintf(out + n, out_size - (size_t)n, "\033[K\n");
    LINE("%s", "");
    LINE("-> command: counter=%10u mode=%u led=%u setpoint=%8.2f",
         (unsigned)cmd->counter, (unsigned)cmd->mode, (unsigned)cmd->led_on,
         (double)cmd->setpoint);
#undef LINE
    return n;
}

/**
 * @brief  Точка входа: открывает порт и крутит цикл приём/передача/вывод.
 * @param  argc  число аргументов
 * @param  argv  argv[1] - имя порта (Linux: /dev/ttyACM0, Windows: COM3)
 * @return 0 - штатный выход; 1 - ошибка
 */
int main(int argc, char **argv)
{
    static SCOM_HostHandle_t com;
    static char screen[4096];
    SCOM_HostConfig_t cfg;
    Example_Telemetry_t telemetry;
    Example_Command_t   command;
    uint64_t next_send_ms;
    uint64_t next_draw_ms;

    memset(&cfg, 0, sizeof(cfg));
    cfg.device        = (argc > 1) ? argv[1] : EXAMPLE_DEFAULT_PORT;
    cfg.tx_size       = sizeof(Example_Command_t);     /* то, что принимает STM32 */
    cfg.rx_size       = sizeof(Example_Telemetry_t);   /* то, что шлёт STM32 */
    cfg.rx_timeout_ms = 200U;

    if (SCOM_HostOpen(&com, &cfg) != 0)
    {
        fprintf(stderr, "Cannot open port %s\n", cfg.device);
        return 1;
    }

    signal(SIGINT, example_on_signal);
    signal(SIGTERM, example_on_signal);
    example_console_init();
#ifdef _WIN32
    timeBeginPeriod(1U);    /* точность таймеров Windows 1 мс вместо ~15 мс */
#endif

    memset(&telemetry, 0, sizeof(telemetry));
    memset(&command, 0, sizeof(command));

    /* Весь экран уходит одним выводом: буфер stdout равен размеру экрана. */
    setvbuf(stdout, NULL, _IOFBF, sizeof(screen));

    /* Альтернативный экран + скрытый курсор + очистка: вывод не портит
     * историю терминала и восстанавливается при выходе. */
    printf("\033[?1049h\033[?25l\033[2J");
    fflush(stdout);

    next_send_ms = SCOM_HostNowMs();
    next_draw_ms = next_send_ms;

    while (s_stop == 0)
    {
        const uint64_t now = SCOM_HostNowMs();

        if (SCOM_HostPoll(&com, 5) < 0)
        {
            break;      /* порт пропал */
        }
        (void)SCOM_HostGetRx(&com, &telemetry);

        if (now >= next_send_ms)
        {
            next_send_ms = now + EXAMPLE_SEND_PERIOD_MS;
            command.counter++;
            command.mode     = 1U;
            command.led_on   = (uint8_t)(((command.counter / 50U) & 1U) != 0U);
            command.setpoint = 25.0f;
            (void)SCOM_HostSend(&com, &command);
        }

        if (now >= next_draw_ms)
        {
            const int len = example_draw(screen, sizeof(screen), &telemetry,
                                         SCOM_HostIsTimeout(&com) == 0,
                                         &com.stats, &command);

            next_draw_ms = now + EXAMPLE_DRAW_PERIOD_MS;
            fwrite(screen, 1U, (size_t)len, stdout);
            fflush(stdout);
        }
    }

    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    SCOM_HostClose(&com);
#ifdef _WIN32
    timeEndPeriod(1U);
#endif
    return 0;
}
