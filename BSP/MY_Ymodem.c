/**
 * Ymodem-1K ??????? USART1 FIFO ??????CRC ????????????? Flash??
 * ??????MY_USART??DMA+FIFO????HAL_Flash??my_flash_write????
 */
#include "MY_Ymodem.h"
#include "HAL_Flash.h"
#include "MY_USART.h"
#include "usart.h"
#include <stdint.h>
#include <stdio.h>

#define OTA_YMODEM_SOH 0x01u
#define OTA_YMODEM_STX 0x02u
#define OTA_YMODEM_EOT 0x04u
#define OTA_YMODEM_ACK 0x06u
#define OTA_YMODEM_NAK 0x15u
#define OTA_YMODEM_CAN 0x18u
#define OTA_YMODEM_C 0x43u

#define OTA_YMODEM_PACKET_128 128u
#define OTA_YMODEM_PACKET_1K 1024u

#define OTA_YMODEM_START_RETRY 64u
#define OTA_YMODEM_PACKET_RETRY 32u
#define OTA_YMODEM_PACKET_TIMEOUT_MS 5000u

static uint8_t ota_ymodem_packet[3u + OTA_YMODEM_PACKET_1K + 2u];

static uint16_t ota_crc16_xmodem(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0u;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8u; b++)
        {
            if (crc & 0x8000u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void MY_ymodem_uart1_tx(uint8_t ch)
{
    (void)HAL_UART_Transmit(&huart1, &ch, 1, 100);
}

static uint16_t MY_ymodem_strnlen_s(const char *s, uint16_t max_len)
{
    uint16_t n = 0;
    while (n < max_len && s[n] != '\0')
    {
        n++;
    }
    return n;
}

static uint32_t MY_ymodem_parse_u32_dec_bounded(const char *s, uint16_t max_chars)
{
    uint32_t v = 0u;
    uint16_t i = 0;
    while (i < max_chars && *s == ' ')
    {
        s++;
        i++;
    }
    while (i < max_chars && *s >= '0' && *s <= '9')
    {
        v = (uint32_t)(v * 10u + (uint32_t)(*s - '0'));
        s++;
        i++;
    }
    return v;
}

static int ota_uart1_read_exact(uint8_t *dst, uint16_t len, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t got = 0;
    while (got < len)
    {
        USART1_PumpRxFromDma();
        uint16_t n = USART1_Read(dst + got, (uint16_t)(len - got));
        if (n)
        {
            got = (uint16_t)(got + n);
            start = HAL_GetTick();
            continue;
        }
        if ((HAL_GetTick() - start) >= timeout_ms)
        {
            return -1;
        }
    }
    return 0;
}

static int ota_ymodem_receive_packet(uint16_t *out_data_len, uint32_t timeout_ms)
{
    uint8_t c = 0;
    if (ota_uart1_read_exact(&c, 1, timeout_ms) < 0)
    {
        return -1;
    }

    ota_ymodem_packet[0] = c;

    if (c == OTA_YMODEM_EOT)
    {
        return 1;
    }
    if (c == OTA_YMODEM_CAN)
    {
        uint8_t c2 = 0;
        if (ota_uart1_read_exact(&c2, 1, timeout_ms) < 0)
        {
            return 2;
        }
        ota_ymodem_packet[1] = c2;
        return 2;
    }

    uint16_t data_len = 0;
    if (c == OTA_YMODEM_SOH)
    {
        data_len = OTA_YMODEM_PACKET_128;
    }
    else if (c == OTA_YMODEM_STX)
    {
        data_len = OTA_YMODEM_PACKET_1K;
    }
    else
    {
        return -2;
    }

    uint16_t total = (uint16_t)(3u + data_len + 2u);
    if (ota_uart1_read_exact(&ota_ymodem_packet[1], (uint16_t)(total - 1u), timeout_ms) < 0)
    {
        return -1;
    }

    uint8_t blk = ota_ymodem_packet[1];
    uint8_t blk_inv = ota_ymodem_packet[2];
    if ((uint8_t)(blk + blk_inv) != 0xFFu)
    {
        return -2;
    }

    const uint8_t *data = &ota_ymodem_packet[3];
    uint16_t crc_rx = (uint16_t)(((uint16_t)ota_ymodem_packet[3u + data_len] << 8) |
                                 (uint16_t)ota_ymodem_packet[3u + data_len + 1u]);
    uint16_t crc = ota_crc16_xmodem(data, data_len);
    if (crc != crc_rx)
    {
        return -2;
    }

    *out_data_len = data_len;
    return 0;
}

int MY_ymodem_ReceiveBinToFlash(uint32_t flash_base_addr)
{
    if (flash_base_addr == 0u)
    {
        flash_base_addr = OTA_FLASH_BASE_ADDR;
    }

    USART1_ALLData_flush();

    uint16_t data_len = 0;
    uint8_t started = 0;

    for (uint32_t i = 0; i < OTA_YMODEM_START_RETRY; i++)
    {
        MY_ymodem_uart1_tx(OTA_YMODEM_C);
        int r = ota_ymodem_receive_packet(&data_len, OTA_YMODEM_PACKET_TIMEOUT_MS);
        if (r == 0 && (ota_ymodem_packet[0] == OTA_YMODEM_SOH || ota_ymodem_packet[0] == OTA_YMODEM_STX) &&
            ota_ymodem_packet[1] == 0u)
        {
            started = 1;
            break;
        }
        if (r == 2)
        {
            return OTA_ERR_USER_CANCEL;
        }
    }
    if (!started)
    {
        return OTA_ERR_START_TIMEOUT;
    }

    const uint8_t *hdr = &ota_ymodem_packet[3];

    if (hdr[0] == 0u)
    {
        MY_ymodem_uart1_tx(OTA_YMODEM_ACK);
        return OTA_OK;
    }

    uint16_t fn_len = MY_ymodem_strnlen_s((const char *)hdr, data_len);
    if (fn_len >= data_len)
    {
        return OTA_ERR_BAD_HEADER;
    }

    uint16_t size_field_max = (uint16_t)(data_len - (fn_len + 1u));
    const char *file_size_str = (const char *)&hdr[fn_len + 1u];
    uint32_t file_size = MY_ymodem_parse_u32_dec_bounded(file_size_str, size_field_max);

    USART1_ALLData_flush();
    MY_ymodem_uart1_tx(OTA_YMODEM_ACK);
    MY_ymodem_uart1_tx(OTA_YMODEM_C);

    uint8_t expected_blk = 1u;
    uint32_t received = 0;
    uint32_t retry = 0;

    const uint32_t base_packet_timeout = OTA_YMODEM_PACKET_TIMEOUT_MS;
    const uint32_t max_packet_timeout = 15000u;
    uint32_t current_packet_timeout = base_packet_timeout;

    while (1)
    {
        int r = ota_ymodem_receive_packet(&data_len, current_packet_timeout);

        if (r == -1)
        {
            if (++retry > OTA_YMODEM_PACKET_RETRY)
            {
                return OTA_ERR_PACKET_TIMEOUT;
            }
            MY_ymodem_uart1_tx(OTA_YMODEM_NAK);
            current_packet_timeout =
                (current_packet_timeout * 2u > max_packet_timeout) ? max_packet_timeout : current_packet_timeout * 2u;
            continue;
        }

        retry = 0;
        current_packet_timeout = base_packet_timeout;

        if (r == 1)
        {
            MY_ymodem_uart1_tx(OTA_YMODEM_NAK);
            uint16_t dummy_len = 0;
            int r2 = ota_ymodem_receive_packet(&dummy_len, current_packet_timeout);
            if (r2 != 1)
            {
                return OTA_ERR_EOT_HANDSHAKE;
            }
            MY_ymodem_uart1_tx(OTA_YMODEM_ACK);
            MY_ymodem_uart1_tx(OTA_YMODEM_C);
            break;
        }
        if (r == 2)
        {
            return OTA_ERR_USER_CANCEL;
        }
        if (r != 0)
        {
            MY_ymodem_uart1_tx(OTA_YMODEM_NAK);
            continue;
        }

        uint8_t blk = ota_ymodem_packet[1];
        uint8_t *data = &ota_ymodem_packet[3];

        if (blk == expected_blk)
        {
            uint16_t consume = data_len;
            if (file_size && (received + consume > file_size))
            {
                consume = (uint16_t)(file_size - received);
            }

            if (consume)
            {
                uint32_t target = flash_base_addr + received;
                if (my_flash_write(target, data, consume) != 1u)
                {
                    return OTA_ERR_FLASH_WRITE;
                }
            }

            received += consume;
            expected_blk = (uint8_t)(expected_blk + 1u);

            MY_ymodem_uart1_tx(OTA_YMODEM_ACK);
            continue;
        }

        if (blk == (uint8_t)(expected_blk - 1u))
        {
            MY_ymodem_uart1_tx(OTA_YMODEM_ACK);
            continue;
        }

        MY_ymodem_uart1_tx(OTA_YMODEM_NAK);
    }

    if (file_size && (received != file_size))
    {
        return OTA_ERR_SIZE_MISMATCH;
    }
    return OTA_OK;
}
