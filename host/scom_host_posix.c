/**
 ******************************************************************************
 * @file    scom_host_posix.c
 * @brief   Платформенный слой хостовой части для Linux/POSIX: termios, poll,
 *          pthread. На Windows файл пустой.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.3
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

/* Пустая единица трансляции запрещена стандартом C: этот typedef нужен для
 * сборки на Windows, где весь остальной код ниже исключён. */
typedef int scom_host_posix_unused_t;

#ifndef _WIN32

#define _DEFAULT_SOURCE     /* cfmakeraw */
#define _POSIX_C_SOURCE 200809L

#include "scom_host.h"
#include "scom_host_port.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

uint64_t SCOM_HostNowMs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000U) + ((uint64_t)ts.tv_nsec / 1000000U);
}

/**
 * @brief  Переводит порт в "сырой" режим 8N1 без управления потоком.
 * @param  fd  дескриптор порта
 * @return 0 - успех; -1 - ошибка
 */
static int scom_posix_configure(int fd)
{
    struct termios tio;

    if (tcgetattr(fd, &tio) != 0)
    {
        return -1;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(tcflag_t)CRTSCTS;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    /* Для USB CDC скорость не влияет на реальный обмен, но termios требует. */
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return 0;
}

int scom_port_open(SCOM_HostHandle_t *h)
{
    h->plat.fd = open(h->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (h->plat.fd < 0)
    {
        return -1;
    }
    if (scom_posix_configure(h->plat.fd) != 0)
    {
        const int saved = errno;

        close(h->plat.fd);
        h->plat.fd = -1;
        errno = saved;
        return -1;
    }
    return 0;
}

void scom_port_close(SCOM_HostHandle_t *h)
{
    close(h->plat.fd);
    h->plat.fd = -1;
}

int scom_port_read(SCOM_HostHandle_t *h, uint8_t *buf, size_t size, int timeout_ms)
{
    struct pollfd pfd;
    ssize_t n;
    int rc;

    pfd.fd     = h->plat.fd;
    pfd.events = POLLIN;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0)
    {
        return (errno == EINTR) ? 0 : -1;
    }
    if (rc == 0)
    {
        return 0;
    }
    if (((pfd.revents & POLLIN) == 0) && ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0))
    {
        return -1;      /* устройство отключено */
    }

    n = read(h->plat.fd, buf, size);
    if (n > 0)
    {
        return (int)n;
    }
    if ((n == 0) || (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))
    {
        return 0;
    }
    return -1;
}

int scom_port_write_all(SCOM_HostHandle_t *h, const uint8_t *buf, size_t len)
{
    while (len > 0U)
    {
        const ssize_t n = write(h->plat.fd, buf, len);

        if (n > 0)
        {
            buf += n;
            len -= (size_t)n;
        }
        else if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)))
        {
            struct pollfd pfd;

            pfd.fd     = h->plat.fd;
            pfd.events = POLLOUT;
            if (poll(&pfd, 1, SCOM_HOST_WRITE_TIMEOUT_MS) <= 0)
            {
                return -1;
            }
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

void scom_lock_init(SCOM_HostLock_t *lock)
{
    pthread_mutex_init(lock, NULL);
}

void scom_lock_destroy(SCOM_HostLock_t *lock)
{
    pthread_mutex_destroy(lock);
}

void scom_lock_acquire(SCOM_HostLock_t *lock)
{
    pthread_mutex_lock(lock);
}

void scom_lock_release(SCOM_HostLock_t *lock)
{
    pthread_mutex_unlock(lock);
}

#endif /* !_WIN32 */
