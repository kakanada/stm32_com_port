/**
 ******************************************************************************
 * @file    scom_host.c
 * @brief   Платформонезависимая часть хостовой библиотеки: разбор кадров,
 *          отправка, тайм-аут. Работа с портом - в scom_host_posix.c и
 *          scom_host_win32.c.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.2
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "scom_host.h"
#include "scom_host_port.h"
#include <string.h>

int SCOM_HostOpen(SCOM_HostHandle_t *h, const SCOM_HostConfig_t *config)
{
    size_t name_len;

    if ((h == NULL) || (config == NULL) || (config->device == NULL) ||
        ((config->tx_size == 0U) && (config->rx_size == 0U)) ||
        (config->tx_size > SCOM_MAX_PAYLOAD_SIZE) ||
        (config->rx_size > SCOM_MAX_PAYLOAD_SIZE))
    {
        return -1;
    }
    name_len = strlen(config->device);
    if ((name_len == 0U) || (name_len >= SCOM_HOST_DEVICE_MAX))
    {
        return -1;
    }

    memset(h, 0, sizeof(*h));
    h->config = *config;
    memcpy(h->device, config->device, name_len + 1U);
    h->config.device = h->device;

    if (scom_port_open(h) != 0)
    {
        return -1;
    }

    if (config->rx_size != 0U)
    {
        SCOM_ParserInit(&h->parser, h->rx_payload, config->rx_size);
    }
    scom_lock_init(&h->rx_lock);
    scom_lock_init(&h->tx_lock);
    h->last_rx_ms = SCOM_HostNowMs();
    h->is_open    = 1U;
    return 0;
}

void SCOM_HostClose(SCOM_HostHandle_t *h)
{
    if ((h == NULL) || (h->is_open == 0U))
    {
        return;
    }
    h->is_open = 0U;
    scom_port_close(h);
    scom_lock_destroy(&h->rx_lock);
    scom_lock_destroy(&h->tx_lock);
}

/**
 * @brief  Разбирает порцию байт из порта и фиксирует принятые кадры.
 * @param  h    хэндл
 * @param  buf  байты из порта
 * @param  len  сколько байт
 * @return число валидных кадров в этой порции
 */
static int scom_host_process_bytes(SCOM_HostHandle_t *h, const uint8_t *buf, size_t len)
{
    int frames = 0;

    while (len > 0U)
    {
        size_t consumed = 0U;
        const SCOM_ParseResult_t res = SCOM_ParserFeed(&h->parser, buf, len, &consumed);

        buf += consumed;
        len -= consumed;

        if (res == SCOM_PARSE_NEED_MORE)
        {
            continue;
        }

        scom_lock_acquire(&h->rx_lock);
        if (res == SCOM_PARSE_FRAME_OK)
        {
            memcpy(h->rx_latest, h->rx_payload, h->config.rx_size);
            h->rx_new     = 1U;
            h->last_rx_ms = SCOM_HostNowMs();
            h->stats.rx_frames++;
            frames++;
        }
        else if (res == SCOM_PARSE_CRC_ERROR)
        {
            h->stats.rx_crc_errors++;
        }
        else
        {
            h->stats.rx_size_errors++;
        }
        scom_lock_release(&h->rx_lock);
    }
    return frames;
}

int SCOM_HostPoll(SCOM_HostHandle_t *h, int timeout_ms)
{
    uint8_t buf[512];
    int frames = 0;
    int n;

    if ((h == NULL) || (h->is_open == 0U) || (h->config.rx_size == 0U))
    {
        return -1;
    }

    /* Первое чтение ждёт данные, остальные только добирают уже пришедшее. */
    n = scom_port_read(h, buf, sizeof(buf), timeout_ms);
    while (n > 0)
    {
        frames += scom_host_process_bytes(h, buf, (size_t)n);
        n = scom_port_read(h, buf, sizeof(buf), 0);
    }
    return (n < 0) ? -1 : frames;
}

int SCOM_HostSend(SCOM_HostHandle_t *h, const void *data)
{
    uint16_t frame_len;
    int rc;

    if ((h == NULL) || (h->is_open == 0U) || (data == NULL) || (h->config.tx_size == 0U))
    {
        return -1;
    }

    /* tx_lock держим на сборку и запись: кадры из разных потоков не должны
     * перемешаться в порту. */
    scom_lock_acquire(&h->tx_lock);
    frame_len = SCOM_FrameBuild(h->tx_frame, data, h->config.tx_size);
    rc = scom_port_write_all(h, h->tx_frame, frame_len);
    if (rc == 0)
    {
        h->stats.tx_frames++;
    }
    scom_lock_release(&h->tx_lock);
    return rc;
}

int SCOM_HostGetRx(SCOM_HostHandle_t *h, void *out)
{
    int fresh = 0;

    if ((h == NULL) || (h->is_open == 0U) || (out == NULL) || (h->config.rx_size == 0U))
    {
        return 0;
    }

    scom_lock_acquire(&h->rx_lock);
    if (h->rx_new != 0U)
    {
        memcpy(out, h->rx_latest, h->config.rx_size);
        h->rx_new = 0U;
        fresh     = 1;
    }
    scom_lock_release(&h->rx_lock);
    return fresh;
}

int SCOM_HostIsTimeout(SCOM_HostHandle_t *h)
{
    uint64_t last;

    if ((h == NULL) || (h->is_open == 0U) || (h->config.rx_timeout_ms == 0U))
    {
        return 0;
    }
    scom_lock_acquire(&h->rx_lock);
    last = h->last_rx_ms;
    scom_lock_release(&h->rx_lock);
    return (SCOM_HostNowMs() - last) > h->config.rx_timeout_ms;
}
