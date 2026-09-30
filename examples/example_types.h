/**
 ******************************************************************************
 * @file    example_types.h
 * @brief   Примерные структуры обмена (общие для STM32 и хостовых примеров).
 *          Замените своими - файл должен быть одинаковым на обеих сторонах.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.1
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

SCOM_PACK_BEGIN

/** Вложенная структура: вектор из трёх float. */
typedef struct
{
    float x;
    float y;
    float z;
} Example_Vec3_t;

/** Вложенная структура: показания датчиков. */
typedef struct
{
    int16_t  temperature_x100;      /**< температура, сотые доли градуса   */
    uint16_t vbat_mv;               /**< напряжение батареи, мВ            */
    uint8_t  status_flags;          /**< битовые флаги состояния           */
} Example_Sensors_t;

/** STM32 -> хост: телеметрия. */
typedef struct
{
    uint32_t          counter;      /**< номер отправки                    */
    uint32_t          uptime_ms;    /**< время работы STM32, мс            */
    Example_Vec3_t    accel;        /**< ускорение                         */
    Example_Vec3_t    gyro;         /**< угловая скорость                  */
    Example_Sensors_t sensors;      /**< датчики                           */
    uint16_t          adc[8];       /**< сырые значения АЦП                */
} Example_Telemetry_t;

/** Хост -> STM32: команда. */
typedef struct
{
    uint32_t counter;               /**< номер отправки                    */
    uint8_t  mode;                  /**< режим работы                      */
    uint8_t  led_on;                /**< 1 - включить светодиод            */
    float    setpoint;              /**< уставка                           */
} Example_Command_t;

SCOM_PACK_END

SCOM_STATIC_ASSERT(sizeof(Example_Telemetry_t) <= SCOM_MAX_PAYLOAD_SIZE, telemetry_size);
SCOM_STATIC_ASSERT(sizeof(Example_Command_t) <= SCOM_MAX_PAYLOAD_SIZE, command_size);
/* Контроль отсутствия дыр выравнивания: 8 + 24 + 5 + 16 = 53 и 4 + 1 + 1 + 4 = 10. */
SCOM_STATIC_ASSERT(sizeof(Example_Telemetry_t) == 53U, telemetry_packed);
SCOM_STATIC_ASSERT(sizeof(Example_Command_t) == 10U, command_packed);

#endif /* EXAMPLE_TYPES_H */
