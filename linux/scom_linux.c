/**
 ******************************************************************************
 * @file    scom_linux.c
 * @brief   Реализация Linux-части библиотеки обмена структурами по
 *          COM-порту (termios, poll) с проверкой CRC32.
 * @author  Mechanic
 * @date    30.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#define _DEFAULT_SOURCE     /* cfmakeraw */
#define _POSIX_C_SOURCE 200809L

#include "scom_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief  Возвращает монотонное время в миллисекундах.
 * @return время, мс
 */
static uint64_t scom_now_ms(void)
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
static int scom_configure_port(int fd)
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
    /* Для USB CDC скорость не влияет на реальный обмен, но термиос требует. */
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return 0;
}

int SCOM_LinuxOpen(SCOM_LinuxHandle_t *h, const SCOM_LinuxConfig_t *config)
{
    if ((h == NULL) || (config == NULL) || (config->device == NULL) ||
        ((config->tx_size == 0U) && (config->rx_size == 0U)) ||
        (config->tx_size > SCOM_MAX_PAYLOAD_SIZE) ||
        (config->rx_size > SCOM_MAX_PAYLOAD_SIZE) ||
        (strlen(config->device) >= SCOM_LINUX_DEVICE_MAX))
    {
        errno = EINVAL;
        return -1;
    }

    memset(h, 0, sizeof(*h));
    h->config = *config;
    strcpy(h->device, config->device);
    h->config.device = h->device;

    h->fd = open(h->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (h->fd < 0)
    {
        return -1;
    }
    if (scom_configure_port(h->fd) != 0)
    {
        const int saved = errno;

        close(h->fd);
        h->fd = -1;
        errno = saved;
        return -1;
    }

    if (config->rx_size != 0U)
    {
        SCOM_ParserInit(&h->parser, h->rx_payload, config->rx_size);
    }
    pthread_mutex_init(&h->rx_lock, NULL);
    pthread_mutex_init(&h->tx_lock, NULL);
    h->last_rx_ms = scom_now_ms();
    return 0;
}

void SCOM_LinuxClose(SCOM_LinuxHandle_t *h)
{
    if ((h == NULL) || (h->fd < 0))
    {
        return;
    }
    close(h->fd);
    h->fd = -1;
    pthread_mutex_destroy(&h->rx_lock);
    pthread_mutex_destroy(&h->tx_lock);
}

/**
 * @brief  Разбирает порцию байт из порта и фиксирует принятые кадры.
 * @param  h    хэндл
 * @param  buf  байты из порта
 * @param  len  сколько байт
 * @return число валидных кадров в этой порции
 */
static int scom_process_bytes(SCOM_LinuxHandle_t *h, const uint8_t *buf, size_t len)
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

        pthread_mutex_lock(&h->rx_lock);
        if (res == SCOM_PARSE_FRAME_OK)
        {
            memcpy(h->rx_latest, h->rx_payload, h->config.rx_size);
            h->rx_new     = 1U;
            h->last_rx_ms = scom_now_ms();
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
        pthread_mutex_unlock(&h->rx_lock);
    }
    return frames;
}

int SCOM_LinuxPoll(SCOM_LinuxHandle_t *h, int timeout_ms)
{
    struct pollfd pfd;
    uint8_t buf[512];
    int frames = 0;
    int rc;

    if ((h == NULL) || (h->fd < 0) || (h->config.rx_size == 0U))
    {
        return -1;
    }

    pfd.fd     = h->fd;
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
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
        return -1;
    }

    for (;;)
    {
        const ssize_t n = read(h->fd, buf, sizeof(buf));

        if (n > 0)
        {
            frames += scom_process_bytes(h, buf, (size_t)n);
            continue;
        }
        if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)))
        {
            break;      /* всё вычитали */
        }
        if (n == 0)
        {
            break;      /* нет данных (VMIN=0) */
        }
        return -1;      /* реальная ошибка: устройство пропало */
    }
    return frames;
}

/**
 * @brief  Записывает весь буфер в порт, ожидая готовность не дольше таймаута.
 * @param  fd   дескриптор порта
 * @param  buf  данные
 * @param  len  длина данных
 * @return 0 - записано полностью; -1 - ошибка или таймаут
 */
static int scom_write_all(int fd, const uint8_t *buf, size_t len)
{
    while (len > 0U)
    {
        const ssize_t n = write(fd, buf, len);

        if (n > 0)
        {
            buf += n;
            len -= (size_t)n;
        }
        else if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)))
        {
            struct pollfd pfd;

            pfd.fd     = fd;
            pfd.events = POLLOUT;
            if (poll(&pfd, 1, SCOM_LINUX_WRITE_TIMEOUT_MS) <= 0)
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

int SCOM_LinuxSend(SCOM_LinuxHandle_t *h, const void *data)
{
    uint16_t frame_len;
    int rc;

    if ((h == NULL) || (h->fd < 0) || (data == NULL) || (h->config.tx_size == 0U))
    {
        return -1;
    }

    /* tx_lock держим на сборку и запись: кадры из разных потоков не должны
     * перемешаться в порту. */
    pthread_mutex_lock(&h->tx_lock);
    frame_len = SCOM_FrameBuild(h->tx_frame, data, h->config.tx_size);
    rc = scom_write_all(h->fd, h->tx_frame, frame_len);
    if (rc == 0)
    {
        h->stats.tx_frames++;
    }
    pthread_mutex_unlock(&h->tx_lock);
    return rc;
}

int SCOM_LinuxGetRx(SCOM_LinuxHandle_t *h, void *out)
{
    int fresh = 0;

    if ((h == NULL) || (h->fd < 0) || (out == NULL) || (h->config.rx_size == 0U))
    {
        return 0;
    }

    pthread_mutex_lock(&h->rx_lock);
    if (h->rx_new != 0U)
    {
        memcpy(out, h->rx_latest, h->config.rx_size);
        h->rx_new = 0U;
        fresh     = 1;
    }
    pthread_mutex_unlock(&h->rx_lock);
    return fresh;
}

int SCOM_LinuxIsTimeout(SCOM_LinuxHandle_t *h)
{
    uint64_t last;

    if ((h == NULL) || (h->fd < 0) || (h->config.rx_timeout_ms == 0U))
    {
        return 0;
    }
    pthread_mutex_lock(&h->rx_lock);
    last = h->last_rx_ms;
    pthread_mutex_unlock(&h->rx_lock);
    return (scom_now_ms() - last) > h->config.rx_timeout_ms;
}
