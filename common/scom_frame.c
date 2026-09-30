/**
 ******************************************************************************
 * @file    scom_frame.c
 * @brief   Сборка кадров и потоковый конечный автомат разбора (платформо-
 *          независимый код).
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "scom_frame.h"
#include "scom_crc32.h"
#include <string.h>

/** Состояния конечного автомата разбора. */
enum
{
    SCOM_ST_SOF1 = 0,   /* ищем первый байт признака начала                 */
    SCOM_ST_SOF2,       /* ждём второй байт признака начала                 */
    SCOM_ST_LEN0,       /* ждём младший байт длины                          */
    SCOM_ST_LEN1,       /* ждём старший байт длины                          */
    SCOM_ST_PAYLOAD,    /* копим нагрузку                                   */
    SCOM_ST_CRC         /* копим 4 байта CRC32                              */
};

void SCOM_ParserInit(SCOM_Parser_t *p, uint8_t *payload_buf, uint16_t payload_size)
{
    memset(p, 0, sizeof(*p));
    p->payload      = payload_buf;
    p->payload_size = payload_size;
    p->state        = SCOM_ST_SOF1;
}

SCOM_ParseResult_t SCOM_ParserFeed(SCOM_Parser_t *p, const uint8_t *data,
                                   size_t len, size_t *consumed)
{
    size_t i = 0U;
    SCOM_ParseResult_t result = SCOM_PARSE_NEED_MORE;

    while ((i < len) && (result == SCOM_PARSE_NEED_MORE))
    {
        const uint8_t b = data[i];

        switch (p->state)
        {
            case SCOM_ST_SOF1:
                i++;
                if (b == SCOM_SOF1)
                {
                    p->state = SCOM_ST_SOF2;
                }
                break;

            case SCOM_ST_SOF2:
                /* Повторный SOF1 не сбрасывает поиск: A5 A5 5A - валидное
                 * начало со второго байта. */
                i++;
                if (b == SCOM_SOF2)
                {
                    p->state = SCOM_ST_LEN0;
                }
                else if (b != SCOM_SOF1)
                {
                    p->state = SCOM_ST_SOF1;
                }
                break;

            case SCOM_ST_LEN0:
                i++;
                p->len_lo = b;
                p->state  = SCOM_ST_LEN1;
                break;

            case SCOM_ST_LEN1:
            {
                const uint16_t len_field = (uint16_t)(p->len_lo | ((uint16_t)b << 8));

                i++;
                if (len_field != p->payload_size)
                {
                    p->state = SCOM_ST_SOF1;
                    result   = SCOM_PARSE_SIZE_ERROR;
                }
                else
                {
                    /* CRC покрывает поле длины и нагрузку. */
                    const uint8_t hdr[2] = { p->len_lo, b };

                    p->crc   = SCOM_Crc32Update(SCOM_CRC32_INIT, hdr, 2U);
                    p->pos   = 0U;
                    p->state = SCOM_ST_PAYLOAD;
                }
                break;
            }

            case SCOM_ST_PAYLOAD:
            {
                /* Нагрузку копируем целыми кусками, не побайтно. */
                size_t chunk = (size_t)(p->payload_size - p->pos);

                if (chunk > (len - i))
                {
                    chunk = len - i;
                }
                memcpy(&p->payload[p->pos], &data[i], chunk);
                p->crc = SCOM_Crc32Update(p->crc, &data[i], chunk);
                p->pos = (uint16_t)(p->pos + chunk);
                i     += chunk;
                if (p->pos == p->payload_size)
                {
                    p->pos    = 0U;
                    p->rx_crc = 0U;
                    p->state  = SCOM_ST_CRC;
                }
                break;
            }

            default: /* SCOM_ST_CRC */
                p->rx_crc |= (uint32_t)b << (8U * p->pos);
                i++;
                p->pos++;
                if (p->pos == 4U)
                {
                    p->state = SCOM_ST_SOF1;
                    result   = (SCOM_Crc32Final(p->crc) == p->rx_crc) ?
                               SCOM_PARSE_FRAME_OK : SCOM_PARSE_CRC_ERROR;
                }
                break;
        }
    }

    *consumed = i;
    return result;
}

uint16_t SCOM_FrameBuild(uint8_t *out, const void *payload, uint16_t payload_size)
{
    uint32_t crc;
    uint16_t n = 0U;

    out[n++] = SCOM_SOF1;
    out[n++] = SCOM_SOF2;
    out[n++] = (uint8_t)(payload_size & 0xFFU);
    out[n++] = (uint8_t)(payload_size >> 8);
    memcpy(&out[n], payload, payload_size);
    n = (uint16_t)(n + payload_size);

    /* CRC по LEN (2 байта) + нагрузке, т.е. по всему, что после SOF. */
    crc = SCOM_Crc32(&out[2], (size_t)payload_size + 2U);
    out[n++] = (uint8_t)(crc & 0xFFU);
    out[n++] = (uint8_t)((crc >> 8) & 0xFFU);
    out[n++] = (uint8_t)((crc >> 16) & 0xFFU);
    out[n++] = (uint8_t)((crc >> 24) & 0xFFU);
    return n;
}
