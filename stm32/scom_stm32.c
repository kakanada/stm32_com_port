/**
 ******************************************************************************
 * @file    scom_stm32.c
 * @brief   Реализация STM32-части библиотеки обмена структурами по USB
 *          COM-порту (CDC) с проверкой CRC32.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.1
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "scom_stm32.h"
#include <string.h>

/** Статический пул экземпляров (без malloc). */
static SCOM_Handle_t s_pool[SCOM_MAX_INSTANCES];

/**
 * @brief  Проверяет, что хэндл принадлежит пулу и занят.
 * @param  h  проверяемый указатель
 * @return 1 - хэндл валиден; 0 - нет
 */
static uint8_t scom_handle_valid(const SCOM_Handle_t *h)
{
    return (uint8_t)((h != NULL) && (h->used != 0U));
}

/**
 * @brief  Проверяет конфигурацию экземпляра.
 * @param  c  конфигурация
 * @return 1 - корректна; 0 - нет
 */
static uint8_t scom_config_valid(const SCOM_Config_t *c)
{
    if ((c == NULL) || ((c->tx_size == 0U) && (c->rx_size == 0U)))
    {
        return 0U;
    }
    if ((c->tx_size > SCOM_MAX_PAYLOAD_SIZE) || (c->rx_size > SCOM_MAX_PAYLOAD_SIZE))
    {
        return 0U;
    }
    if ((c->tx_size != 0U) && (c->tx_func == NULL))
    {
        return 0U;
    }
    return 1U;
}

SCOM_Handle_t *SCOM_Init(const SCOM_Config_t *config)
{
    SCOM_Handle_t *result = NULL;
    uint32_t primask;
    uint8_t i;

    if (scom_config_valid(config) == 0U)
    {
        return NULL;
    }

    /* Вся регистрация под запретом прерываний: это разовая операция при
     * старте (несколько микросекунд), зато пул не разорвёт параллельный Init. */
    SCOM_ENTER_CRITICAL(primask);

    for (i = 0U; i < SCOM_MAX_INSTANCES; i++)
    {
        if ((s_pool[i].used != 0U) &&
            (s_pool[i].config.tx_func == config->tx_func) &&
            (s_pool[i].config.user == config->user))
        {
            result = &s_pool[i];    /* идемпотентность: уже зарегистрирован */
            break;
        }
    }

    if (result == NULL)
    {
        for (i = 0U; i < SCOM_MAX_INSTANCES; i++)
        {
            if (s_pool[i].used == 0U)
            {
                result = &s_pool[i];
                memset(result, 0, sizeof(*result));
                result->config = *config;
                if (result->config.tx_timeout_ms == 0U)
                {
                    result->config.tx_timeout_ms = SCOM_DEFAULT_TX_TIMEOUT_MS;
                }
                if (config->rx_size != 0U)
                {
                    SCOM_ParserInit(&result->parser, result->rx_payload, config->rx_size);
                }
                result->last_rx_ms = SCOM_GET_TICK_MS();
                result->index      = i;
                result->used       = 1U;
                break;
            }
        }
    }

    SCOM_EXIT_CRITICAL(primask);
    return result;
}

/**
 * @brief  Фиксирует принятую структуру: копирует в "последнюю" и взводит флаг.
 * @param  h  хэндл экземпляра
 */
static void scom_commit_rx(SCOM_Handle_t *h)
{
    uint32_t primask;

    /* Копия под защитой: читатель из другого прерывания не должен увидеть
     * наполовину обновлённую структуру. */
    SCOM_ENTER_CRITICAL(primask);
    memcpy(h->rx_latest, h->rx_payload, h->config.rx_size);
    h->rx_new     = 1U;
    h->last_rx_ms = SCOM_GET_TICK_MS();
    SCOM_EXIT_CRITICAL(primask);

    h->stats.rx_frames++;
}

void SCOM_OnReceive(SCOM_Handle_t *h, const uint8_t *buf, uint32_t len)
{
    if ((scom_handle_valid(h) == 0U) || (h->config.rx_size == 0U) || (buf == NULL))
    {
        return;
    }

    while (len > 0U)
    {
        size_t consumed = 0U;
        const SCOM_ParseResult_t res = SCOM_ParserFeed(&h->parser, buf, len, &consumed);

        buf += consumed;
        len -= (uint32_t)consumed;

        if (res == SCOM_PARSE_FRAME_OK)
        {
            scom_commit_rx(h);
            if (h->config.on_rx != NULL)
            {
                h->config.on_rx(h->config.user, h->rx_payload);
            }
        }
        else if (res == SCOM_PARSE_CRC_ERROR)
        {
            h->stats.rx_crc_errors++;
        }
        else if (res == SCOM_PARSE_SIZE_ERROR)
        {
            h->stats.rx_size_errors++;
        }
        else
        {
            /* SCOM_PARSE_NEED_MORE: данные кончились, ждём следующую порцию. */
        }
    }
}

void SCOM_OnTxComplete(SCOM_Handle_t *h)
{
    if (scom_handle_valid(h) == 0U)
    {
        return;
    }
    h->stats.tx_frames++;
    h->tx_busy = 0U;
}

SCOM_Status_t SCOM_Send(SCOM_Handle_t *h, const void *data)
{
    uint32_t primask;
    uint32_t now;
    uint16_t frame_len;

    if ((scom_handle_valid(h) == 0U) || (data == NULL) || (h->config.tx_size == 0U))
    {
        return SCOM_ERROR;
    }

    now = SCOM_GET_TICK_MS();

    /* Захват передатчика: только один контекст получает право собрать кадр
     * в tx_frame. Остальные сразу получают SCOM_BUSY, без ожидания. */
    SCOM_ENTER_CRITICAL(primask);
    if (h->tx_busy != 0U)
    {
        if ((uint32_t)(now - h->tx_start_ms) > h->config.tx_timeout_ms)
        {
            /* Завершение передачи так и не пришло (отключили кабель и т.п.) -
             * не блокируем передатчик навсегда. */
            h->stats.tx_timeouts++;
        }
        else
        {
            h->stats.tx_dropped++;
            SCOM_EXIT_CRITICAL(primask);
            return SCOM_BUSY;
        }
    }
    h->tx_busy     = 1U;
    h->tx_start_ms = now;
    SCOM_EXIT_CRITICAL(primask);

    /* Дальше tx_frame принадлежит только нам - сборка вне критической секции. */
    frame_len = SCOM_FrameBuild(h->tx_frame, data, h->config.tx_size);

    if (h->config.tx_func(h->config.user, h->tx_frame, frame_len) != 0)
    {
        SCOM_ENTER_CRITICAL(primask);
        h->tx_busy = 0U;
        h->stats.tx_dropped++;
        SCOM_EXIT_CRITICAL(primask);
        return SCOM_BUSY;
    }
    return SCOM_OK;
}

uint8_t SCOM_GetRx(SCOM_Handle_t *h, void *out)
{
    uint32_t primask;
    uint8_t  fresh = 0U;

    if ((scom_handle_valid(h) == 0U) || (out == NULL) || (h->config.rx_size == 0U))
    {
        return 0U;
    }

    SCOM_ENTER_CRITICAL(primask);
    if (h->rx_new != 0U)
    {
        memcpy(out, h->rx_latest, h->config.rx_size);
        h->rx_new = 0U;
        fresh     = 1U;
    }
    SCOM_EXIT_CRITICAL(primask);
    return fresh;
}

uint8_t SCOM_IsTimeout(const SCOM_Handle_t *h)
{
    if ((scom_handle_valid(h) == 0U) || (h->config.rx_timeout_ms == 0U))
    {
        return 0U;
    }
    return (uint8_t)(((uint32_t)(SCOM_GET_TICK_MS() - h->last_rx_ms)) > h->config.rx_timeout_ms);
}
