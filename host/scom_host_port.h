/**
 ******************************************************************************
 * @file    scom_host_port.h
 * @brief   Внутренний интерфейс платформенного слоя хостовой части (порт,
 *          мьютексы). Реализуется в scom_host_posix.c и scom_host_win32.c.
 *          Пользователю не нужен.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.4
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef SCOM_HOST_PORT_H
#define SCOM_HOST_PORT_H

#include <stddef.h>
#include <stdint.h>
#include "scom_host.h"

/**
 * @brief  Открывает и настраивает порт по имени h->device.
 * @param  h  хэндл
 * @return 0 - успех; -1 - ошибка
 */
int scom_port_open(SCOM_HostHandle_t *h);

/**
 * @brief  Закрывает порт.
 * @param  h  хэндл
 */
void scom_port_close(SCOM_HostHandle_t *h);

/**
 * @brief  Ждёт до timeout_ms появления данных и читает их.
 * @param  h           хэндл
 * @param  buf         куда читать
 * @param  size        размер буфера
 * @param  timeout_ms  сколько ждать, мс (0 - не ждать)
 * @return число прочитанных байт (> 0); 0 - данных нет; -1 - ошибка порта
 */
int scom_port_read(SCOM_HostHandle_t *h, uint8_t *buf, size_t size, int timeout_ms);

/**
 * @brief  Записывает весь буфер в порт, ожидая готовность не дольше
 *         SCOM_HOST_WRITE_TIMEOUT_MS на каждый шаг.
 * @param  h    хэндл
 * @param  buf  данные
 * @param  len  длина данных
 * @return 0 - записано полностью; -1 - ошибка или таймаут
 */
int scom_port_write_all(SCOM_HostHandle_t *h, const uint8_t *buf, size_t len);

/**
 * @brief  Создаёт мьютекс.
 * @param  lock  мьютекс
 */
void scom_lock_init(SCOM_HostLock_t *lock);

/**
 * @brief  Уничтожает мьютекс.
 * @param  lock  мьютекс
 */
void scom_lock_destroy(SCOM_HostLock_t *lock);

/**
 * @brief  Захватывает мьютекс.
 * @param  lock  мьютекс
 */
void scom_lock_acquire(SCOM_HostLock_t *lock);

/**
 * @brief  Освобождает мьютекс.
 * @param  lock  мьютекс
 */
void scom_lock_release(SCOM_HostLock_t *lock);

#endif /* SCOM_HOST_PORT_H */
