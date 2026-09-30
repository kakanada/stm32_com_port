/**
 ******************************************************************************
 * @file    scom_crc32.h
 * @brief   CRC32 (IEEE 802.3, как в zlib/Ethernet) - общий код для STM32 и
 *          хостовой частей библиотеки.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef SCOM_CRC32_H
#define SCOM_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** Начальное значение для пошагового расчёта (SCOM_Crc32Update). */
#define SCOM_CRC32_INIT              0xFFFFFFFFU

/**
 * @brief  Продолжает расчёт CRC32 на очередном куске данных.
 * @param  crc   текущее значение (SCOM_CRC32_INIT для первого куска)
 * @param  data  указатель на данные
 * @param  len   длина данных в байтах
 * @return промежуточное значение; для итога применить SCOM_Crc32Final
 */
uint32_t SCOM_Crc32Update(uint32_t crc, const void *data, size_t len);

/**
 * @brief  Завершает пошаговый расчёт CRC32.
 * @param  crc  промежуточное значение из SCOM_Crc32Update
 * @return итоговый CRC32
 */
uint32_t SCOM_Crc32Final(uint32_t crc);

/**
 * @brief  Считает CRC32 одним вызовом.
 * @param  data  указатель на данные
 * @param  len   длина данных в байтах
 * @return CRC32 (для строки "123456789" равен 0xCBF43926)
 */
uint32_t SCOM_Crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SCOM_CRC32_H */
