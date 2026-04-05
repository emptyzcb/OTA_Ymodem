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


// CRC-16 Xmodem 预生成查表数组
const uint16_t CRC16_Xmodem_Table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

static uint16_t ota_crc16_xmodem(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;  // 固定初始值
    for(uint32_t i = 0; i < len; i++)
    {
        uint8_t index = (crc >> 8) ^ data[i];  // 计算查表索引
        crc = (crc << 8) ^ CRC16_Xmodem_Table[index];  // 查表更新CRC
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
