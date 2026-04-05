#ifndef __MY_YMODEM_H__
#define __MY_YMODEM_H__

#include "stm32f1xx_hal.h"

/** 应用程序之后的首个 Flash 扇区起始（需与链接脚本 APP 起始一致；STM32F103 常为大页 2KB） */
#ifndef OTA_FLASH_BASE_ADDR
#define OTA_FLASH_BASE_ADDR 0x08004000U
#endif

/** MY_ymodem_ReceiveBinToFlash 返回值 */
#define OTA_OK 0
#define OTA_ERR_START_TIMEOUT (-1)
#define OTA_ERR_USER_CANCEL (-2)
#define OTA_ERR_BAD_HEADER (-3)
#define OTA_ERR_PACKET_TIMEOUT (-4)
#define OTA_ERR_FLASH_WRITE (-5)
#define OTA_ERR_EOT_HANDSHAKE (-6)
#define OTA_ERR_SIZE_MISMATCH (-7)

/**
 * Ymodem-1K 接收并写入 Flash。
 * @param flash_base_addr 目标起始地址；传 0 则使用 OTA_FLASH_BASE_ADDR
 * @return OTA_OK 或 OTA_ERR_* 宏
 *
 * 前置：Init_USART() 已启动 DMA 环形接收 + IDLE 中断，数据进入 FIFO。
 * 本函数从 FIFO 读包、CRC 校验、my_flash_write 编程。
 */
int MY_ymodem_ReceiveBinToFlash(uint32_t flash_base_addr);

#endif
