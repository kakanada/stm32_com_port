/**
 ******************************************************************************
 * @file    scom_linux.h
 * @brief   Linux-часть библиотеки обмена структурами с STM32 по COM-порту
 *          (/dev/ttyACMx) с проверкой CRC32. Потокобезопасна, без malloc.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef SCOM_LINUX_H
#define SCOM_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include "scom_frame.h"

/** Максимальная длина пути к устройству (с завершающим нулём). */
#ifndef SCOM_LINUX_DEVICE_MAX
#define SCOM_LINUX_DEVICE_MAX        128U
#endif

/** Сколько мс ждать освобождения порта при записи, прежде чем вернуть ошибку. */
#ifndef SCOM_LINUX_WRITE_TIMEOUT_MS
#define SCOM_LINUX_WRITE_TIMEOUT_MS  100
#endif

/** Конфигурация экземпляра - заполняется пользователем. */
typedef struct
{
    /** Путь к устройству, например "/dev/ttyACM0". */
    const char *device;

    /** Размер отправляемой структуры, байт (sizeof), 0 - только приём.
     *  Это структура, которую STM32 принимает. */
    uint16_t tx_size;

    /** Размер принимаемой структуры, байт (sizeof), 0 - только передача.
     *  Это структура, которую STM32 отправляет. */
    uint16_t rx_size;

    /** Через сколько мс без валидного кадра связь считается потерянной
     *  (см. SCOM_LinuxIsTimeout). 0 - контроль таймаута отключён. */
    uint32_t rx_timeout_ms;
} SCOM_LinuxConfig_t;

/** Счётчики для диагностики (только чтение). */
typedef struct
{
    uint32_t rx_frames;         /**< принято валидных кадров                */
    uint32_t rx_crc_errors;     /**< кадров с неверным CRC32                */
    uint32_t rx_size_errors;    /**< кадров с неожиданной длиной            */
    uint32_t tx_frames;         /**< отправлено кадров                      */
} SCOM_LinuxStats_t;

/*
 * Хэндл экземпляра. Память выделяет пользователь (на стеке или статически),
 * несколько экземпляров (несколько STM32) работают независимо.
 */
typedef struct
{
    /* ---- Публичные поля (можно читать снаружи) ---- */
    SCOM_LinuxConfig_t config;                  /* копия конфигурации       */
    SCOM_LinuxStats_t  stats;                   /* диагностика (под rx_lock/tx_lock) */
    char               device[SCOM_LINUX_DEVICE_MAX]; /* копия пути к порту */

    /* ---- Внутреннее состояние - не трогать напрямую, только через API ---- */
    int             fd;                         /* дескриптор порта, -1 = закрыт */
    pthread_mutex_t rx_lock;                    /* защита rx_latest/rx_new/stats */
    pthread_mutex_t tx_lock;                    /* защита tx_frame и записи в порт */
    uint8_t         rx_new;                     /* есть непрочитанная структура */
    uint64_t        last_rx_ms;                 /* монотонное время последнего кадра */
    SCOM_Parser_t   parser;                     /* разборщик входного потока */
    uint8_t         rx_payload[SCOM_MAX_PAYLOAD_SIZE]; /* сборка приёма     */
    uint8_t         rx_latest[SCOM_MAX_PAYLOAD_SIZE];  /* последняя принятая */
    uint8_t         tx_frame[SCOM_MAX_FRAME_SIZE];     /* кадр для записи   */
} SCOM_LinuxHandle_t;

/**
 * @brief  Открывает порт (8N1, raw-режим) и готовит экземпляр.
 * @param  h       хэндл (память пользователя)
 * @param  config  конфигурация
 * @return 0 - успех; -1 - ошибка (при ошибке порта установлен errno)
 */
int SCOM_LinuxOpen(SCOM_LinuxHandle_t *h, const SCOM_LinuxConfig_t *config);

/**
 * @brief  Закрывает порт и освобождает ресурсы экземпляра.
 * @param  h  хэндл
 */
void SCOM_LinuxClose(SCOM_LinuxHandle_t *h);

/**
 * @brief  Ждёт данные до timeout_ms, вычитывает всё доступное и разбирает
 *         кадры. Вызывайте из ОДНОГО потока (обычно главный цикл).
 * @param  h           хэндл
 * @param  timeout_ms  сколько максимум ждать данные, мс (0 - не ждать)
 * @return число принятых валидных кадров (0 и более); -1 - ошибка порта
 *         (устройство отключено и т.п.)
 */
int SCOM_LinuxPoll(SCOM_LinuxHandle_t *h, int timeout_ms);

/**
 * @brief  Отправляет структуру в момент вызова. Можно вызывать из любого
 *         потока.
 * @param  h     хэндл
 * @param  data  структура размером config.tx_size
 * @return 0 - отправлено; -1 - ошибка (неверные аргументы, порт занят дольше
 *         SCOM_LINUX_WRITE_TIMEOUT_MS или отключён)
 */
int SCOM_LinuxSend(SCOM_LinuxHandle_t *h, const void *data);

/**
 * @brief  Забирает последнюю принятую структуру (копия). Можно вызывать из
 *         любого потока.
 * @param  h    хэндл
 * @param  out  куда скопировать config.rx_size байт
 * @return 1 - скопирована НОВАЯ структура; 0 - новых данных нет (out не изменён)
 */
int SCOM_LinuxGetRx(SCOM_LinuxHandle_t *h, void *out);

/**
 * @brief  Проверяет тайм-аут связи: с момента последнего валидного кадра
 *         (или открытия порта) прошло больше config.rx_timeout_ms.
 * @param  h  хэндл
 * @return 1 - связь потеряна; 0 - в норме либо контроль отключён
 */
int SCOM_LinuxIsTimeout(SCOM_LinuxHandle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* SCOM_LINUX_H */
