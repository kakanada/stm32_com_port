/**
 ******************************************************************************
 * @file    main_windows.c
 * @brief   Пример для Windows 10/11 x64: консольный монитор обмена с STM32
 *          (запуск: example_windows.exe COM5).
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.4
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <stdio.h>
#include <windows.h>
#include <timeapi.h>

#include "example_monitor.h"

#define EXAMPLE_DEFAULT_PORT         "COM3"

/**
 * @brief  Обработчик Ctrl+C / закрытия консоли: просит монитор завершиться.
 * @param  ctrl_type  тип события консоли
 * @return TRUE - событие обработано
 */
static BOOL WINAPI example_on_console_event(DWORD ctrl_type)
{
    (void)ctrl_type;
    ExampleMonitor_Stop();
    return TRUE;
}

/**
 * @brief  Включает обработку ANSI-последовательностей в консоли Windows.
 */
static void example_enable_ansi(void)
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0U;

    if ((out != INVALID_HANDLE_VALUE) && GetConsoleMode(out, &mode))
    {
        (void)SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

/**
 * @brief  Печатает существующие в системе COM-порты.
 */
static void example_print_ports(void)
{
    char name[16];
    char target[256];
    int found = 0;
    int i;

    fprintf(stderr, "Existing COM ports:");
    for (i = 1; i <= 255; i++)
    {
        snprintf(name, sizeof(name), "COM%d", i);
        if (QueryDosDeviceA(name, target, (DWORD)sizeof(target)) != 0U)
        {
            fprintf(stderr, " %s", name);
            found = 1;
        }
    }
    fprintf(stderr, "%s\n", (found != 0) ? "" : " none (is the STM32 connected?)");
    fprintf(stderr, "See Device Manager -> Ports (COM & LPT) for the STM32 port number.\n");
}

/**
 * @brief  Точка входа примера для Windows.
 * @param  argc  число аргументов
 * @param  argv  argv[1] - имя порта, например COM5 (по умолчанию COM3)
 * @return 0 - штатный выход; 1 - ошибка
 */
int main(int argc, char **argv)
{
    const char *device = (argc > 1) ? argv[1] : EXAMPLE_DEFAULT_PORT;
    DWORD last_error;
    int rc;

    SetConsoleCtrlHandler(example_on_console_event, TRUE);
    example_enable_ansi();
    timeBeginPeriod(1U);    /* точность таймеров Windows 1 мс вместо ~15 мс */

    rc = ExampleMonitor_Run(device);
    last_error = GetLastError();
    timeEndPeriod(1U);

    if (rc == 1)
    {
        fprintf(stderr, "Cannot open port %s (Windows error %lu; 2 = no such port, "
                        "5 = port busy or access denied).\n", device, (unsigned long)last_error);
        example_print_ports();
    }
    else if (rc == 2)
    {
        fprintf(stderr, "Port %s lost (device unplugged or reset).\n", device);
    }
    return (rc == 0) ? 0 : 1;
}
