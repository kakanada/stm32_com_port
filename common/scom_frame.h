/**
 ******************************************************************************
 * @file    scom_frame.h
 * @brief   Формат кадра и потоковый разборщик кадров - общий код для STM32
 *          и Linux частей библиотеки (не зависит от платформы).
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef SCOM_FRAME_H
#define SCOM_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/*  Конфигурация (define-ы, меняются до включения заголовка ИЛИ ключом -D,  */
/*  ОДИНАКОВО на STM32 и на Linux)                                          */
/* ------------------------------------------------------------------------ */

/** Максимальный размер полезной нагрузки (структуры) в байтах. Определяет
 *  размер буферов в каждом экземпляре. На двух сторонах значение может
 *  отличаться, но ни одна передаваемая структура не может быть больше. */
#ifndef SCOM_MAX_PAYLOAD_SIZE
#define SCOM_MAX_PAYLOAD_SIZE        1024U
#endif

/** Атрибут "упакованная структура" (без дыр выравнивания). Применять ко
 *  всем структурам, передаваемым через библиотеку: typedef struct SCOM_PACKED
 *  { ... } My_t; Для компилятора без __attribute__ переопределите. */
#ifndef SCOM_PACKED
#define SCOM_PACKED                  __attribute__((packed))
#endif

/** Проверка на этапе компиляции (C99, без static_assert). name - любой
 *  уникальный идентификатор. Пример:
 *  SCOM_STATIC_ASSERT(sizeof(My_t) <= SCOM_MAX_PAYLOAD_SIZE, my_t_size); */
#define SCOM_STATIC_ASSERT(cond, name) \
    typedef char scom_static_assert_##name[(cond) ? 1 : -1]

/* ------------------------------------------------------------------------ */
/*  Формат кадра: | 0xA5 | 0x5A | LEN (2, LE) | payload (LEN) | CRC32 (4, LE) |
 *  CRC32 считается по полям LEN и payload.                                 */
/* ------------------------------------------------------------------------ */

#define SCOM_SOF1                    0xA5U   /**< Первый байт признака начала. */
#define SCOM_SOF2                    0x5AU   /**< Второй байт признака начала. */
#define SCOM_FRAME_OVERHEAD          8U      /**< SOF(2) + LEN(2) + CRC32(4).  */
#define SCOM_MAX_FRAME_SIZE          (SCOM_MAX_PAYLOAD_SIZE + SCOM_FRAME_OVERHEAD)

/** Результат обработки очередного куска потока разборщиком. */
typedef enum
{
    SCOM_PARSE_NEED_MORE = 0,   /**< кадр пока не завершён, нужны ещё байты   */
    SCOM_PARSE_FRAME_OK,        /**< принят целый кадр, CRC верен             */
    SCOM_PARSE_CRC_ERROR,       /**< кадр целиком принят, но CRC не совпал    */
    SCOM_PARSE_SIZE_ERROR       /**< в заголовке длина не равна ожидаемой     */
} SCOM_ParseResult_t;

/** Состояние потокового разборщика. Поля - внутренние, не трогать. */
typedef struct
{
    uint8_t  *payload;          /* буфер под полезную нагрузку (внешний)    */
    uint16_t  payload_size;     /* ожидаемая длина нагрузки, байт           */
    uint16_t  pos;              /* сколько байт нагрузки/CRC уже принято    */
    uint8_t   state;            /* состояние конечного автомата             */
    uint8_t   len_lo;           /* младший байт LEN, пока ждём старший      */
    uint32_t  crc;              /* CRC32, считаемый на лету                 */
    uint32_t  rx_crc;           /* CRC32, принятый из кадра                 */
} SCOM_Parser_t;

/**
 * @brief  Инициализирует разборщик.
 * @param  p             разборщик
 * @param  payload_buf   буфер под принимаемую нагрузку (>= payload_size)
 * @param  payload_size  ожидаемый размер структуры, 1..SCOM_MAX_PAYLOAD_SIZE
 */
void SCOM_ParserInit(SCOM_Parser_t *p, uint8_t *payload_buf, uint16_t payload_size);

/**
 * @brief  Скармливает разборщику байты потока. Обрабатывает данные до первого
 *         "события" (кадр принят / ошибка) либо до конца данных, поэтому
 *         вызывать в цикле, пока не будут израсходованы все байты:
 *         while (len) { r = SCOM_ParserFeed(p, d, len, &n); d += n; len -= n; }
 * @param  p         разборщик
 * @param  data      входные байты
 * @param  len       сколько байт во входном куске
 * @param  consumed  сюда пишется, сколько байт из data израсходовано
 * @return SCOM_PARSE_FRAME_OK - нагрузка лежит в payload_buf разборщика;
 *         SCOM_PARSE_CRC_ERROR / SCOM_PARSE_SIZE_ERROR - кадр отброшен,
 *         разборщик сам ищет следующий признак начала; иначе NEED_MORE
 */
SCOM_ParseResult_t SCOM_ParserFeed(SCOM_Parser_t *p, const uint8_t *data,
                                   size_t len, size_t *consumed);

/**
 * @brief  Собирает кадр (SOF + LEN + нагрузка + CRC32) в выходном буфере.
 * @param  out           буфер, не меньше payload_size + SCOM_FRAME_OVERHEAD
 * @param  payload       структура, которую нужно передать
 * @param  payload_size  размер структуры, 1..SCOM_MAX_PAYLOAD_SIZE
 * @return полная длина кадра в байтах
 */
uint16_t SCOM_FrameBuild(uint8_t *out, const void *payload, uint16_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* SCOM_FRAME_H */
