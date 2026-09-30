/**
 ******************************************************************************
 * @file    example_stm32.h
 * @brief   Пример STM32-стороны: заглушка телеметрии раз в 10 мс и приём
 *          команды от хоста (Linux или Windows). Три вызова в проекте
 *          CubeMX - см. README.md, раздел "Быстрый старт".
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.2
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef EXAMPLE_STM32_H
#define EXAMPLE_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  Регистрирует экземпляр библиотеки. Вызвать один раз в main.c
 *         после MX_USB_DEVICE_Init().
 * @return 0 - успех; -1 - ошибка (пул или конфигурация)
 */
int Example_Init(void);

/**
 * @brief  Рабочий шаг примера: применяет принятую команду и раз в 10 мс
 *         отправляет телеметрию. Вызывать в главном цикле while (1).
 */
void Example_Process(void);

/**
 * @brief  Передаёт библиотеке принятые от USB байты. Вызвать из
 *         CDC_Receive_FS() в usbd_cdc_if.c.
 * @param  buf  принятые байты
 * @param  len  сколько байт принято
 */
void Example_UsbOnReceive(const uint8_t *buf, uint32_t len);

/**
 * @brief  Сообщает библиотеке, что USB закончил отправку кадра. Вызвать из
 *         CDC_TransmitCplt_FS() в usbd_cdc_if.c.
 */
void Example_UsbOnTxComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_STM32_H */
