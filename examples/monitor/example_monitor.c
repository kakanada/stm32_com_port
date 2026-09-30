/**
 ******************************************************************************
 * @file    example_monitor.c
 * @brief   Общая часть хостовых примеров: цикл обмена с STM32 и консольный
 *          монитор. Для своей структуры правится только example_draw().
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.4
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "scom_host.h"
#include "example_types.h"
#include "example_monitor.h"

#define EXAMPLE_DRAW_PERIOD_MS       50U     /* как часто перерисовывать (20 Гц) */

static volatile sig_atomic_t s_stop = 0;

void ExampleMonitor_Stop(void)
{
    s_stop = 1;
}

/**
 * @brief  Формирует текст экрана. Каждая строка имеет фиксированную ширину
 *         полей и заканчивается очисткой хвоста строки (ESC[K), поэтому
 *         картинка не "плавает" при смене значений. Это единственная
 *         функция, которую нужно менять под свою структуру.
 * @param  out       буфер для текста
 * @param  out_size  размер буфера
 * @param  rx        последняя принятая от STM32 структура
 * @param  rx_ok     1 - структуры от STM32 приходят
 * @param  stats     счётчики библиотеки
 * @param  tx        последняя отправленная STM32 структура
 * @return длина текста в буфере
 */
static int example_draw(char *out, size_t out_size, const Example_StmToHost_t *rx,
                        int rx_ok, const SCOM_HostStats_t *stats,
                        const Example_HostToStm_t *tx)
{
    int n = 0;
    int i;

#define LINE(...) \
    do { n += snprintf(out + n, out_size - (size_t)n, __VA_ARGS__); \
         n += snprintf(out + n, out_size - (size_t)n, "\033[K\n"); } while (0)

    n += snprintf(out + n, out_size - (size_t)n, "\033[H");   /* курсор в начало */
    LINE("STM32 COM PORT monitor          (Ctrl+C - exit)");
    LINE("------------------------------------------------------------");
    LINE("STM32 -> host  : %s", rx_ok ? "OK      " : "TIMEOUT ");
    LINE("host -> STM32  : %s", rx->peer_ok ? "OK      " : "NO DATA ");
    LINE("Frames rx      : %10u   CRC err: %6u   size err: %6u",
         (unsigned)stats->rx_frames, (unsigned)stats->rx_crc_errors,
         (unsigned)stats->rx_size_errors);
    LINE("Frames tx      : %10u", (unsigned)stats->tx_frames);
    LINE("%s", "");
    LINE("received from STM32:");
    LINE("  counter      : %10u", (unsigned)rx->counter);
    LINE("  uptime_ms    : %10u", (unsigned)rx->uptime_ms);
    LINE("  vec_a x/y/z  : %9.3f %9.3f %9.3f",
         (double)rx->vec_a.x, (double)rx->vec_a.y, (double)rx->vec_a.z);
    LINE("  vec_b x/y/z  : %9.3f %9.3f %9.3f",
         (double)rx->vec_b.x, (double)rx->vec_b.y, (double)rx->vec_b.z);
    LINE("  temperature  : %9.2f C", rx->small.temperature_x100 / 100.0);
    LINE("  vbat_mv      : %10u", (unsigned)rx->small.vbat_mv);
    LINE("  flags        :       0x%02X  (1=no data from host, 2=led on)",
         (unsigned)rx->small.flags);
    n += snprintf(out + n, out_size - (size_t)n, "  adc          :");
    for (i = 0; i < 8; i++)
    {
        n += snprintf(out + n, out_size - (size_t)n, " %5u", (unsigned)rx->adc[i]);
    }
    n += snprintf(out + n, out_size - (size_t)n, "\033[K\n");
    LINE("  echo of what STM32 received from host: counter=%10u mode=%u",
         (unsigned)rx->echo_counter, (unsigned)rx->echo_mode);
    LINE("%s", "");
    LINE("sent to STM32:");
    LINE("  counter=%10u mode=%u led_on=%u value=%6.2f",
         (unsigned)tx->counter, (unsigned)tx->mode, (unsigned)tx->led_on,
         (double)tx->value);
#undef LINE
    return n;
}

int ExampleMonitor_Run(const char *device)
{
    static SCOM_HostHandle_t com;
    static char screen[4096];
    SCOM_HostConfig_t cfg;
    Example_StmToHost_t rx_data;
    Example_HostToStm_t tx_data;
    uint64_t next_send_ms;
    uint64_t next_draw_ms;
    int result = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.device        = device;
    cfg.tx_size       = sizeof(Example_HostToStm_t);   /* что отправляем STM32 */
    cfg.rx_size       = sizeof(Example_StmToHost_t);   /* что принимаем от STM32 */
    cfg.rx_timeout_ms = EXAMPLE_LINK_TIMEOUT_MS;

    if (SCOM_HostOpen(&com, &cfg) != 0)
    {
        return 1;
    }

    memset(&rx_data, 0, sizeof(rx_data));
    memset(&tx_data, 0, sizeof(tx_data));

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
        uint64_t now;

        if (SCOM_HostPoll(&com, 5) < 0)
        {
            result = 2;     /* порт пропал */
            break;
        }
        now = SCOM_HostNowMs();     /* после ожидания, чтобы период не плыл */
        (void)SCOM_HostGetRx(&com, &rx_data);

        if (now >= next_send_ms)
        {
            next_send_ms = now + EXAMPLE_SEND_PERIOD_MS;
            tx_data.counter++;
            tx_data.mode   = 1U;
            tx_data.led_on = (uint8_t)(((tx_data.counter / 50U) & 1U) != 0U);   /* 0,5 с */
            tx_data.value  = 1.0f + (float)((tx_data.counter / 100U) % 10U);    /* 1..10 */
            (void)SCOM_HostSend(&com, &tx_data);
        }

        if (now >= next_draw_ms)
        {
            const int len = example_draw(screen, sizeof(screen), &rx_data,
                                         SCOM_HostIsTimeout(&com) == 0,
                                         &com.stats, &tx_data);

            next_draw_ms = now + EXAMPLE_DRAW_PERIOD_MS;
            fwrite(screen, 1U, (size_t)len, stdout);
            fflush(stdout);
        }
    }

    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    SCOM_HostClose(&com);
    return result;
}
