/**
 ******************************************************************************
 * @file    stm32_example.c
 * @brief   Пример подключения STM32-части: один USB CDC, отправка заглушки
 *          телеметрии раз в 10 мс, приём команды. Фрагменты переносятся в
 *          main.c и usbd_cdc_if.c вашего CubeMX-проекта.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <string.h>
#include "main.h"
#include "usbd_cdc_if.h"
#include "scom_stm32.h"
#include "example_types.h"

/* ------------------------------------------------------------------------ */
/*  Фрагмент для main.c                                                     */
/* ------------------------------------------------------------------------ */

/** Хэндл библиотеки; используется и в usbd_cdc_if.c (extern). */
SCOM_Handle_t *g_scom = NULL;

static Example_Command_t s_last_command;     /* последняя принятая команда */

/**
 * @brief  Оболочка над CDC_Transmit_FS для библиотеки.
 * @param  user  не используется
 * @param  data  байты кадра
 * @param  len   длина кадра
 * @return 0 - USB принял; не 0 - занят
 */
static int example_usb_tx(void *user, const uint8_t *data, uint16_t len)
{
    (void)user;
    return (CDC_Transmit_FS((uint8_t *)data, len) == USBD_OK) ? 0 : -1;
}

/**
 * @brief  Обработчик принятой команды (вызывается из прерывания USB).
 * @param  user  не используется
 * @param  data  принятая структура Example_Command_t
 */
static void example_on_command(void *user, const void *data)
{
    (void)user;
    s_last_command = *(const Example_Command_t *)data;
}

/**
 * @brief  Регистрирует экземпляр библиотеки (вызвать после MX_USB_DEVICE_Init).
 */
void Example_Init(void)
{
    SCOM_Config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.tx_size       = sizeof(Example_Telemetry_t);
    cfg.rx_size       = sizeof(Example_Command_t);
    cfg.tx_func       = example_usb_tx;
    cfg.on_rx         = example_on_command;
    cfg.rx_timeout_ms = 200U;
    g_scom = SCOM_Init(&cfg);
}

/**
 * @brief  Основной шаг примера: раз в 10 мс отправляет заглушку телеметрии.
 *         Вызывать из главного цикла.
 */
void Example_Process(void)
{
    static uint32_t s_next_ms = 0U;
    static uint32_t s_counter = 0U;
    Example_Telemetry_t tel;
    const uint32_t now = HAL_GetTick();

    if ((int32_t)(now - s_next_ms) < 0)
    {
        return;
    }
    s_next_ms = now + 10U;

    /* Заглушка: пилообразные значения. */
    memset(&tel, 0, sizeof(tel));
    tel.counter           = s_counter++;
    tel.uptime_ms         = now;
    tel.accel.x           = (float)((int)(tel.counter % 200U) - 100) * 0.01f;
    tel.accel.y           = -tel.accel.x;
    tel.accel.z           = 9.81f;
    tel.gyro.x            = (float)(tel.counter % 360U);
    tel.sensors.temperature_x100 = (int16_t)(2500 + (int)(tel.counter % 100U));
    tel.sensors.vbat_mv   = 3300U;
    tel.sensors.status_flags = (uint8_t)((SCOM_IsTimeout(g_scom) != 0U) ? 0x01U : 0x00U);
    tel.adc[0]            = (uint16_t)(tel.counter % 4096U);

    /* SCOM_BUSY здесь не страшен: следующая отправка через 10 мс уйдёт
     * уже с новыми данными. */
    (void)SCOM_Send(g_scom, &tel);
}

/* ------------------------------------------------------------------------ */
/*  Фрагменты для usbd_cdc_if.c (внутри блоков USER CODE)                   */
/* ------------------------------------------------------------------------ */
/*
 * extern SCOM_Handle_t *g_scom;              // USER CODE BEGIN INCLUDE
 *
 * static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
 * {
 *     // USER CODE BEGIN 6
 *     SCOM_OnReceive(g_scom, Buf, *Len);     // <-- добавить
 *     USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
 *     USBD_CDC_ReceivePacket(&hUsbDeviceFS);
 *     return (USBD_OK);
 *     // USER CODE END 6
 * }
 *
 * static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
 * {
 *     // USER CODE BEGIN 13
 *     SCOM_OnTxComplete(g_scom);             // <-- добавить
 *     // USER CODE END 13
 *     return USBD_OK;
 * }
 */
