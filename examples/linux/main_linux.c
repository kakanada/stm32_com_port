/**
 ******************************************************************************
 * @file    main_linux.c
 * @brief   Пример для Linux (Ubuntu): консольный монитор обмена с STM32
 *          (запуск: ./example_linux /dev/ttyACM0).
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.4
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "example_monitor.h"

#define EXAMPLE_DEFAULT_PORT         "/dev/ttyACM0"

/**
 * @brief  Обработчик SIGINT/SIGTERM: просит монитор завершиться.
 * @param  sig  номер сигнала
 */
static void example_on_signal(int sig)
{
    (void)sig;
    ExampleMonitor_Stop();
}

/**
 * @brief  Печатает найденные порты /dev/ttyACM* и подсказки по доступу.
 */
static void example_print_hints(void)
{
    glob_t g;
    size_t i;

    memset(&g, 0, sizeof(g));
    if ((glob("/dev/ttyACM*", 0, NULL, &g) == 0) && (g.gl_pathc > 0U))
    {
        fprintf(stderr, "Found ports:");
        for (i = 0U; i < g.gl_pathc; i++)
        {
            fprintf(stderr, " %s", g.gl_pathv[i]);
        }
        fprintf(stderr, "\n");
    }
    else
    {
        fprintf(stderr, "No /dev/ttyACM* ports found. Is the STM32 connected?\n");
    }
    globfree(&g);
    fprintf(stderr, "If access is denied: sudo usermod -aG dialout $USER (then re-login).\n");
    fprintf(stderr, "If the port is busy: ModemManager may be probing it.\n");
}

/**
 * @brief  Точка входа примера для Linux.
 * @param  argc  число аргументов
 * @param  argv  argv[1] - путь к порту (по умолчанию /dev/ttyACM0)
 * @return 0 - штатный выход; 1 - ошибка
 */
int main(int argc, char **argv)
{
    const char *device = (argc > 1) ? argv[1] : EXAMPLE_DEFAULT_PORT;
    int saved_errno;
    int rc;

    signal(SIGINT, example_on_signal);
    signal(SIGTERM, example_on_signal);

    rc = ExampleMonitor_Run(device);
    saved_errno = errno;

    if (rc == 1)
    {
        fprintf(stderr, "Cannot open port %s: %s\n", device, strerror(saved_errno));
        example_print_hints();
    }
    else if (rc == 2)
    {
        fprintf(stderr, "Port %s lost (device unplugged or reset).\n", device);
    }
    return (rc == 0) ? 0 : 1;
}
