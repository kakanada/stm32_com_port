/**
 ******************************************************************************
 * @file    example_types.h
 * @brief   Заглушки структур для примеров (общие для STM32, Linux и Windows).
 *          Замените своими - файл должен быть одинаковым на всех сторонах.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef EXAMPLE_TYPES_H
#define EXAMPLE_TYPES_H

#include <stdint.h>
#include "scom_frame.h"

/** Как часто каждая сторона примера отправляет свою структуру, мс (100 Гц). */
#define EXAMPLE_SEND_PERIOD_MS       10U

/** Через сколько мс без кадров каждая сторона считает связь потерянной. */
#define EXAMPLE_LINK_TIMEOUT_MS      200U

SCOM_PACK_BEGIN

/** Вложенная структура: вектор из трёх float. */
typedef struct
{
    float x;
    float y;
    float z;
} Example_Vec3_t;

/** Вложенная структура: набор мелких значений. */
typedef struct
{
    int16_t  temperature_x100;      /**< сотые доли градуса                */
    uint16_t vbat_mv;               /**< милливольты                       */
    uint8_t  flags;                 /**< битовые флаги                     */
} Example_Small_t;

/** Структура STM32 -> компьютер (заглушка данных). */
typedef struct
{
    uint32_t        counter;        /**< номер отправки                    */
    uint32_t        uptime_ms;      /**< время работы STM32, мс            */
    Example_Vec3_t  vec_a;          /**< вложенная структура               */
    Example_Vec3_t  vec_b;          /**< вложенная структура               */
    Example_Small_t small;          /**< вложенная структура               */
    uint16_t        adc[8];         /**< массив                            */
    uint32_t        echo_counter;   /**< копия counter из последней принятой структуры */
    uint8_t         echo_mode;      /**< копия mode из последней принятой структуры    */
    uint8_t         peer_ok;        /**< 1 - STM32 принимает структуры от компьютера   */
} Example_StmToHost_t;

/** Структура компьютер -> STM32 (заглушка данных, отличается от обратной). */
typedef struct
{
    uint32_t counter;               /**< номер отправки                    */
    uint8_t  mode;                  /**< произвольное число                */
    uint8_t  led_on;                /**< 1 - включить светодиод            */
    float    value;                 /**< произвольное число                */
} Example_HostToStm_t;

SCOM_PACK_END

SCOM_STATIC_ASSERT(sizeof(Example_StmToHost_t) <= SCOM_MAX_PAYLOAD_SIZE, stm_to_host_size);
SCOM_STATIC_ASSERT(sizeof(Example_HostToStm_t) <= SCOM_MAX_PAYLOAD_SIZE, host_to_stm_size);
/* Контроль отсутствия дыр выравнивания: 8 + 24 + 5 + 16 + 6 = 59 и 4 + 1 + 1 + 4 = 10. */
SCOM_STATIC_ASSERT(sizeof(Example_StmToHost_t) == 59U, stm_to_host_packed);
SCOM_STATIC_ASSERT(sizeof(Example_HostToStm_t) == 10U, host_to_stm_packed);

#endif /* EXAMPLE_TYPES_H */
