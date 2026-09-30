/**
 ******************************************************************************
 * @file    example_stm32.c
 * @brief   Пример STM32-стороны: раз в 10 мс отправляет заглушку структуры
 *          компьютеру, принимает структуру от компьютера, светодиод по
 *          принятому полю.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
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

/* Светодиод по принятому полю led_on (необязательно). Чтобы включить, определите
 * в настройках проекта, например: EXAMPLE_LED_PORT=GPIOD, EXAMPLE_LED_PIN=GPIO_PIN_12. */
#if defined(EXAMPLE_LED_PORT) && defined(EXAMPLE_LED_PIN)
#define EXAMPLE_LED_WRITE(on) \
    HAL_GPIO_WritePin(EXAMPLE_LED_PORT, EXAMPLE_LED_PIN, ((on) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#else
#define EXAMPLE_LED_WRITE(on)        ((void)(on))
#endif

static SCOM_Handle_t      *s_scom = NULL;    /* экземпляр библиотеки            */
static Example_HostToStm_t s_rx;             /* последняя принятая структура    */
static uint32_t            s_next_ms = 0U;   /* когда отправлять следующую      */
static uint32_t            s_counter = 0U;   /* номер отправки                  */
static uint8_t             s_ever_rx = 0U;   /* хотя бы одна структура получена */

/**
 * @brief  Оболочка над CDC_Transmit_xx: отдаёт готовый кадр в USB. Библиотека
 *         вызывает её сама из SCOM_Send.
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
    cfg.tx_size       = sizeof(Example_StmToHost_t);   /* размер того, что отправляем */
    cfg.rx_size       = sizeof(Example_HostToStm_t);   /* размер того, что принимаем  */
    cfg.tx_func       = example_usb_tx;
    cfg.on_rx         = NULL;                          /* забираем через SCOM_GetRx   */
    cfg.rx_timeout_ms = EXAMPLE_LINK_TIMEOUT_MS;

    memset(&s_rx, 0, sizeof(s_rx));
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
    Example_StmToHost_t tx;
    Example_HostToStm_t rx;
    uint8_t peer_ok;
    uint32_t now;

    if (s_scom == NULL)
    {
        return;
    }

    /* Приём: забираем свежую структуру (потокобезопасная копия). */
    if (SCOM_GetRx(s_scom, &rx) != 0U)
    {
        s_rx      = rx;
        s_ever_rx = 1U;
    }
    peer_ok = (uint8_t)((s_ever_rx != 0U) && (SCOM_IsTimeout(s_scom) == 0U));

    /* При потере связи светодиод гасим. */
    EXAMPLE_LED_WRITE((peer_ok != 0U) ? s_rx.led_on : 0U);

    /* Передача: раз в EXAMPLE_SEND_PERIOD_MS, независимо от приёма. */
    now = HAL_GetTick();
    if ((int32_t)(now - s_next_ms) < 0)
    {
        return;
    }
    s_next_ms = now + EXAMPLE_SEND_PERIOD_MS;

    memset(&tx, 0, sizeof(tx));
    tx.counter     = s_counter++;
    tx.uptime_ms   = now;
    /* Заглушка данных; амплитуда зависит от принятого value, чтобы на мониторе
     * было видно, что структура от компьютера дошла. */
    tx.vec_a.x     = example_triangle(tx.counter) * s_rx.value;
    tx.vec_a.y     = -tx.vec_a.x;
    tx.vec_a.z     = 9.81f;
    tx.vec_b.x     = (float)(tx.counter % 360U);
    tx.vec_b.y     = example_triangle(tx.counter + 50U) * 90.0f;
    tx.vec_b.z     = 0.0f;
    tx.small.temperature_x100 = (int16_t)(2500 + (int)(tx.counter % 100U));
    tx.small.vbat_mv          = 3300U;
    tx.small.flags            = (uint8_t)(((peer_ok == 0U) ? 0x01U : 0x00U) |
                                          ((s_rx.led_on != 0U) ? 0x02U : 0x00U));
    tx.adc[0]      = (uint16_t)(tx.counter % 4096U);
    tx.adc[1]      = (uint16_t)(4095U - (tx.counter % 4096U));
    tx.echo_counter = s_rx.counter;
    tx.echo_mode    = s_rx.mode;
    tx.peer_ok      = peer_ok;

    /* SCOM_BUSY не страшен: следующая отправка через 10 мс уйдёт со свежими данными. */
    (void)SCOM_Send(s_scom, &tx);
}
