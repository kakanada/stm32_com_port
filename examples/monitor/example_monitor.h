/**
 ******************************************************************************
 * @file    example_monitor.h
 * @brief   Общая часть хостовых примеров (Linux и Windows): цикл обмена с
 *          STM32 и консольный монитор без мерцания.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef EXAMPLE_MONITOR_H
#define EXAMPLE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Открывает порт и крутит цикл: приём структуры от STM32, отправка
 *         своей структуры раз в EXAMPLE_SEND_PERIOD_MS, перерисовка монитора.
 *         Возвращает управление после ExampleMonitor_Stop() или при потере
 *         порта.
 * @param  device  имя порта ("/dev/ttyACM0" или "COM5")
 * @return 0 - штатный выход; 1 - порт не открылся; 2 - порт потерян
 */
int ExampleMonitor_Run(const char *device);

/**
 * @brief  Просит ExampleMonitor_Run завершиться. Безопасно вызывать из
 *         обработчика сигнала или консольного события.
 */
void ExampleMonitor_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MONITOR_H */
