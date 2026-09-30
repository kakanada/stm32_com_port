/**
 ******************************************************************************
 * @file    example_stm32.c
 * @brief   Пример STM32-стороны: заглушка телеметрии раз в 10 мс, приём
 *          команды от хоста, эхо команды в телеметрии, светодиод по команде.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.2
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
#include "example_stm32.h"

/** Функция отправки USB. Для порта HS замените на CDC_Transmit_HS через -D. */
#ifndef EXAMPLE_CDC_TRANSMIT
#define EXAMPLE_CDC_TRANSMIT         CDC_Transmit_FS
#endif

/* Светодиод по команде хоста (необязательно). Чтобы включить, определите в
 * настройках проекта, например: EXAMPLE_LED_PORT=GPIOD, EXAMPLE_LED_PIN=GPIO_PIN_12. */
#if defined(EXAMPLE_LED_PORT) && defined(EXAMPLE_LED_PIN)
#define EXAMPLE_LED_WRITE(on) \
    HAL_GPIO_WritePin(EXAMPLE_LED_PORT, EXAMPLE_LED_PIN, ((on) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#else
#define EXAMPLE_LED_WRITE(on)        ((void)(on))
#endif

static SCOM_Handle_t    *s_scom = NULL;   /* экземпляр библиотеки             */
static Example_Command_t s_command;       /* последняя принятая команда       */
static uint32_t          s_next_ms = 0U;  /* когда отправлять следующий кадр  */
static uint32_t          s_counter = 0U;  /* номер отправки                   */
static uint8_t           s_ever_rx = 0U;  /* хотя бы одна команда получена    */
static uint8_t           s_last_busy = 0U;/* прошлая отправка вернула BUSY    */

/**
 * @brief  Оболочка над CDC_Transmit_xx для библиотеки.
 * @param  user  не используется
 * @param  data  байты кадра
 * @param  len   длина кадра
 * @return 0 - USB принял данные; не 0 - занят
 */
static int example_usb_tx(void *user, const uint8_t *data, uint16_t len)
{
    (void)user;
    return (EXAMPLE_CDC_TRANSMIT((uint8_t *)data, len) == USBD_OK) ? 0 : -1;
}

/**
 * @brief  Треугольная волна -1..+1 с периодом 200 отсчётов (заглушка данных).
 * @param  n  номер отсчёта
 * @return значение волны
 */
static float example_triangle(uint32_t n)
{
    const uint32_t phase = n % 200U;
    const uint32_t up    = (phase < 100U) ? phase : (200U - phase);

    return ((float)up - 50.0f) / 50.0f;
}

int Example_Init(void)
{
    SCOM_Config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.tx_size       = sizeof(Example_Telemetry_t);   /* то, что шлём хосту   */
    cfg.rx_size       = sizeof(Example_Command_t);     /* то, что шлёт хост    */
    cfg.tx_func       = example_usb_tx;
    cfg.on_rx         = NULL;                          /* забираем через SCOM_GetRx */
    cfg.rx_timeout_ms = EXAMPLE_LINK_TIMEOUT_MS;

    memset(&s_command, 0, sizeof(s_command));
    s_scom = SCOM_Init(&cfg);
    return (s_scom != NULL) ? 0 : -1;
}

void Example_UsbOnReceive(const uint8_t *buf, uint32_t len)
{
    SCOM_OnReceive(s_scom, buf, len);
}

void Example_UsbOnTxComplete(void)
{
    SCOM_OnTxComplete(s_scom);
}

void Example_Process(void)
{
    Example_Telemetry_t tel;
    Example_Command_t   cmd;
    uint8_t link_ok;
    uint32_t now;

    if (s_scom == NULL)
    {
        return;
    }

    /* Приём: забираем свежую команду (потокобезопасная копия). */
    if (SCOM_GetRx(s_scom, &cmd) != 0U)
    {
        s_command = cmd;
        s_ever_rx = 1U;
    }
    link_ok = (uint8_t)((s_ever_rx != 0U) && (SCOM_IsTimeout(s_scom) == 0U));

    /* Безопасное поведение: при потере связи светодиод гасим. */
    EXAMPLE_LED_WRITE((link_ok != 0U) ? s_command.led_on : 0U);

    /* Передача: раз в EXAMPLE_SEND_PERIOD_MS, независимо от приёма. */
    now = HAL_GetTick();
    if ((int32_t)(now - s_next_ms) < 0)
    {
        return;
    }
    s_next_ms = now + EXAMPLE_SEND_PERIOD_MS;

    memset(&tel, 0, sizeof(tel));
    tel.counter      = s_counter++;
    tel.uptime_ms    = now;
    /* Амплитуда ускорения задаётся уставкой хоста - эффект команды виден на мониторе. */
    tel.accel.x      = example_triangle(tel.counter) * s_command.setpoint;
    tel.accel.y      = -tel.accel.x;
    tel.accel.z      = 9.81f;
    tel.gyro.x       = (float)(tel.counter % 360U);
    tel.gyro.y       = example_triangle(tel.counter + 50U) * 90.0f;
    tel.gyro.z       = 0.0f;
    tel.sensors.temperature_x100 = (int16_t)(2500 + (int)(tel.counter % 100U));
    tel.sensors.vbat_mv          = 3300U;
    tel.sensors.status_flags     = (uint8_t)(((link_ok == 0U) ? 0x01U : 0x00U) |
                                             ((s_command.led_on != 0U) ? 0x02U : 0x00U) |
                                             ((s_last_busy != 0U) ? 0x04U : 0x00U));
    tel.adc[0]       = (uint16_t)(tel.counter % 4096U);
    tel.adc[1]       = (uint16_t)(4095U - (tel.counter % 4096U));
    tel.cmd_counter  = s_command.counter;
    tel.cmd_mode     = s_command.mode;
    tel.host_link_ok = link_ok;

    /* SCOM_BUSY не страшен: следующая отправка через 10 мс уйдёт со свежими данными. */
    s_last_busy = (uint8_t)(SCOM_Send(s_scom, &tel) == SCOM_BUSY);
}
