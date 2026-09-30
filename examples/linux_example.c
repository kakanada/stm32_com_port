/**
 ******************************************************************************
 * @file    linux_example.c
 * @brief   Пример Linux-части: принимает телеметрию от STM32, шлёт команды и
 *          выводит структуру в консоль без мерцания (на месте, в одну строку
 *          вывода за кадр). Для своей структуры правится только функция
 *          example_draw().
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "scom_linux.h"
#include "example_types.h"

#define EXAMPLE_SEND_PERIOD_MS       10      /* как часто слать команду     */
#define EXAMPLE_DRAW_PERIOD_MS       50      /* как часто перерисовывать    */

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
 * @brief  Возвращает монотонное время в миллисекундах.
 * @return время, мс
 */
static long example_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

/**
 * @brief  Формирует текст экрана для телеметрии. Каждая строка имеет
 *         фиксированную ширину полей и заканчивается очисткой хвоста строки
 *         (\033[K), поэтому картинка не "плавает" при смене значений. Это
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
                        int link_ok, const SCOM_LinuxStats_t *stats,
                        const Example_Command_t *cmd)
{
    int n = 0;
    int i;

#define LINE(...) \
    do { n += snprintf(out + n, out_size - (size_t)n, __VA_ARGS__); \
         n += snprintf(out + n, out_size - (size_t)n, "\033[K\n"); } while (0)

    n += snprintf(out + n, out_size - (size_t)n, "\033[H");   /* курсор в начало */
    LINE("STM32 COM PORT monitor          (Ctrl+C - выход)");
    LINE("----------------------------------------------");
    LINE("Связь          : %s", link_ok ? "OK      " : "TIMEOUT ");
    LINE("Кадров принято : %10u   CRC-ошибок: %6u   размер: %6u",
         (unsigned)stats->rx_frames, (unsigned)stats->rx_crc_errors,
         (unsigned)stats->rx_size_errors);
    LINE("Кадров отправл.: %10u", (unsigned)stats->tx_frames);
    LINE("%s", "");
    LINE("counter        : %10u", (unsigned)t->counter);
    LINE("uptime_ms      : %10u", (unsigned)t->uptime_ms);
    LINE("accel  x/y/z   : %9.3f %9.3f %9.3f", (double)t->accel.x, (double)t->accel.y, (double)t->accel.z);
    LINE("gyro   x/y/z   : %9.3f %9.3f %9.3f", (double)t->gyro.x, (double)t->gyro.y, (double)t->gyro.z);
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
    LINE("-> команда: counter=%10u mode=%u led=%u setpoint=%8.2f",
         (unsigned)cmd->counter, (unsigned)cmd->mode, (unsigned)cmd->led_on,
         (double)cmd->setpoint);
#undef LINE
    return n;
}

/**
 * @brief  Точка входа: открывает порт и крутит цикл приём/передача/вывод.
 * @param  argc  число аргументов
 * @param  argv  argv[1] - путь к порту (по умолчанию /dev/ttyACM0)
 * @return 0 - штатный выход; 1 - ошибка
 */
int main(int argc, char **argv)
{
    static SCOM_LinuxHandle_t com;
    SCOM_LinuxConfig_t cfg;
    Example_Telemetry_t telemetry;
    Example_Command_t   command;
    char screen[4096];
    long next_send_ms;
    long next_draw_ms;

    memset(&cfg, 0, sizeof(cfg));
    cfg.device        = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    cfg.tx_size       = sizeof(Example_Command_t);     /* то, что принимает STM32 */
    cfg.rx_size       = sizeof(Example_Telemetry_t);   /* то, что шлёт STM32 */
    cfg.rx_timeout_ms = 200U;

    if (SCOM_LinuxOpen(&com, &cfg) != 0)
    {
        perror("SCOM_LinuxOpen");
        return 1;
    }

    signal(SIGINT, example_on_signal);
    signal(SIGTERM, example_on_signal);

    memset(&telemetry, 0, sizeof(telemetry));
    memset(&command, 0, sizeof(command));

    /* Альтернативный экран + скрытый курсор + очистка: вывод не портит
     * историю терминала и восстанавливается при выходе. */
    printf("\033[?1049h\033[?25l\033[2J");
    fflush(stdout);

    next_send_ms = example_now_ms();
    next_draw_ms = next_send_ms;

    while (s_stop == 0)
    {
        const long now = example_now_ms();

        if (SCOM_LinuxPoll(&com, 5) < 0)
        {
            break;      /* порт пропал */
        }
        (void)SCOM_LinuxGetRx(&com, &telemetry);

        if ((now - next_send_ms) >= 0)
        {
            next_send_ms = now + EXAMPLE_SEND_PERIOD_MS;
            command.counter++;
            command.mode     = 1U;
            command.led_on   = (uint8_t)(((command.counter / 50U) & 1U) != 0U);
            command.setpoint = 25.0f;
            (void)SCOM_LinuxSend(&com, &command);
        }

        if ((now - next_draw_ms) >= 0)
        {
            const int len = example_draw(screen, sizeof(screen), &telemetry,
                                         SCOM_LinuxIsTimeout(&com) == 0,
                                         &com.stats, &command);

            next_draw_ms = now + EXAMPLE_DRAW_PERIOD_MS;
            /* Один write на весь экран - терминал показывает кадр целиком. */
            if (write(STDOUT_FILENO, screen, (size_t)len) < 0)
            {
                break;
            }
        }
    }

    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    SCOM_LinuxClose(&com);
    return 0;
}
