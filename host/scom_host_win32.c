/**
 ******************************************************************************
 * @file    scom_host_win32.c
 * @brief   Платформенный слой хостовой части для Windows 10/11 (x64): COM-порт
 *          через Win32 API с overlapped-вводом-выводом, CRITICAL_SECTION.
 *          На Linux файл пустой.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.2
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

/* Пустая единица трансляции запрещена стандартом C: этот typedef нужен для
 * сборки на Linux, где весь остальной код ниже исключён. */
typedef int scom_host_win32_unused_t;

#ifdef _WIN32

#include "scom_host.h"
#include "scom_host_port.h"
#include <stdio.h>
#include <string.h>

/** Префикс, нужный для портов COM10 и выше (работает и для COM1..COM9). */
#define SCOM_WIN_PORT_PREFIX         "\\\\.\\"

uint64_t SCOM_HostNowMs(void)
{
    return (uint64_t)GetTickCount64();
}

/**
 * @brief  Настраивает порт: 8N1, без управления потоком, DTR/RTS включены.
 * @param  port  дескриптор порта
 * @return 0 - успех; -1 - ошибка
 */
static int scom_win_configure(HANDLE port)
{
    DCB dcb;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(port, &dcb))
    {
        return -1;
    }
    /* Скорость для USB CDC не влияет на реальный обмен, но поле обязательно. */
    dcb.BaudRate        = CBR_115200;
    dcb.ByteSize        = 8;
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = ONESTOPBIT;
    dcb.fBinary         = TRUE;
    dcb.fParity         = FALSE;
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX           = FALSE;
    dcb.fInX            = FALSE;
    dcb.fNull           = FALSE;
    dcb.fAbortOnError   = FALSE;
    /* DTR включаем: некоторые CDC-устройства передают данные только при
     * поднятом DTR (Linux делает это сам при открытии порта). */
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fRtsControl     = RTS_CONTROL_ENABLE;
    if (!SetCommState(port, &dcb))
    {
        return -1;
    }
    (void)SetupComm(port, 4096U, 4096U);
    (void)PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    return 0;
}

int scom_port_open(SCOM_HostHandle_t *h)
{
    char path[SCOM_HOST_DEVICE_MAX + 8U];
    DWORD err;

    if (strncmp(h->device, SCOM_WIN_PORT_PREFIX, 4U) == 0)
    {
        snprintf(path, sizeof(path), "%s", h->device);
    }
    else
    {
        snprintf(path, sizeof(path), SCOM_WIN_PORT_PREFIX "%s", h->device);
    }

    h->plat.port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, NULL);
    if (h->plat.port == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    h->plat.ev_rd         = CreateEventA(NULL, TRUE, FALSE, NULL);
    h->plat.ev_wr         = CreateEventA(NULL, TRUE, FALSE, NULL);
    h->plat.rd_timeout_ms = 0xFFFFFFFFU;    /* "ещё не применён" */

    if ((h->plat.ev_rd == NULL) || (h->plat.ev_wr == NULL) ||
        (scom_win_configure(h->plat.port) != 0))
    {
        err = GetLastError();
        scom_port_close(h);
        SetLastError(err);
        return -1;
    }
    return 0;
}

void scom_port_close(SCOM_HostHandle_t *h)
{
    if (h->plat.ev_rd != NULL)
    {
        CloseHandle(h->plat.ev_rd);
        h->plat.ev_rd = NULL;
    }
    if (h->plat.ev_wr != NULL)
    {
        CloseHandle(h->plat.ev_wr);
        h->plat.ev_wr = NULL;
    }
    if (h->plat.port != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h->plat.port);
        h->plat.port = INVALID_HANDLE_VALUE;
    }
}

/**
 * @brief  Задаёт таймаут чтения драйвера COM-порта (только при изменении).
 *         Режим "вернуть сразу, как только есть хотя бы один байт, но не
 *         ждать дольше timeout_ms" (см. описание COMMTIMEOUTS в Win32 API).
 * @param  h           хэндл
 * @param  timeout_ms  таймаут, мс (0 - вернуть немедленно)
 * @return 0 - успех; -1 - ошибка
 */
static int scom_win_set_read_timeout(SCOM_HostHandle_t *h, DWORD timeout_ms)
{
    COMMTIMEOUTS ct;

    if (h->plat.rd_timeout_ms == timeout_ms)
    {
        return 0;
    }
    memset(&ct, 0, sizeof(ct));
    ct.ReadIntervalTimeout = MAXDWORD;
    if (timeout_ms > 0U)
    {
        ct.ReadTotalTimeoutMultiplier = MAXDWORD;
        ct.ReadTotalTimeoutConstant   = timeout_ms;
    }
    if (!SetCommTimeouts(h->plat.port, &ct))
    {
        return -1;
    }
    h->plat.rd_timeout_ms = timeout_ms;
    return 0;
}

int scom_port_read(SCOM_HostHandle_t *h, uint8_t *buf, size_t size, int timeout_ms)
{
    OVERLAPPED ov;
    DWORD got = 0U;
    const DWORD tmo = (timeout_ms > 0) ? (DWORD)timeout_ms : 0U;

    if (scom_win_set_read_timeout(h, tmo) != 0)
    {
        return -1;
    }

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = h->plat.ev_rd;
    ResetEvent(ov.hEvent);

    if (!ReadFile(h->plat.port, buf, (DWORD)size, &got, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            return -1;      /* устройство отключено и т.п. */
        }
        /* Драйвер сам завершит чтение по таймауту; запас на случай сбоя. */
        if (WaitForSingleObject(ov.hEvent, tmo + 1000U) != WAIT_OBJECT_0)
        {
            CancelIoEx(h->plat.port, &ov);
            (void)GetOverlappedResult(h->plat.port, &ov, &got, TRUE);
            return 0;
        }
        if (!GetOverlappedResult(h->plat.port, &ov, &got, FALSE))
        {
            return -1;
        }
    }
    return (int)got;
}

int scom_port_write_all(SCOM_HostHandle_t *h, const uint8_t *buf, size_t len)
{
    while (len > 0U)
    {
        OVERLAPPED ov;
        DWORD written = 0U;

        memset(&ov, 0, sizeof(ov));
        ov.hEvent = h->plat.ev_wr;
        ResetEvent(ov.hEvent);

        if (!WriteFile(h->plat.port, buf, (DWORD)len, &written, &ov))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                return -1;
            }
            if (WaitForSingleObject(ov.hEvent, SCOM_HOST_WRITE_TIMEOUT_MS) != WAIT_OBJECT_0)
            {
                CancelIoEx(h->plat.port, &ov);
                (void)GetOverlappedResult(h->plat.port, &ov, &written, TRUE);
                return -1;
            }
            if (!GetOverlappedResult(h->plat.port, &ov, &written, FALSE))
            {
                return -1;
            }
        }
        if (written == 0U)
        {
            return -1;
        }
        buf += written;
        len -= written;
    }
    return 0;
}

void scom_lock_init(SCOM_HostLock_t *lock)
{
    InitializeCriticalSection(lock);
}

void scom_lock_destroy(SCOM_HostLock_t *lock)
{
    DeleteCriticalSection(lock);
}

void scom_lock_acquire(SCOM_HostLock_t *lock)
{
    EnterCriticalSection(lock);
}

void scom_lock_release(SCOM_HostLock_t *lock)
{
    LeaveCriticalSection(lock);
}

#endif /* _WIN32 */
