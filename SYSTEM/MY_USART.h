#ifndef __MY_USART_H__
#define __MY_USART_H__

#include "usart.h"
#include "stm32f1xx_hal.h"

#define MY_USART1_DMA_BUF_SIZE 1024

/**
 * 接收数据路径（与 Ymodem OTA 配合）：
 *  USART1 RX -> DMA 循环缓冲 usart1_dma_rx_buf
 *  DMA 半传输 HT / 传输完成 TC（环形缓冲“半满/绕回”）-> HAL_UART_RxHalfCpltCallback / RxCpltCallback
 *      -> USART1_PumpRxFromDmaISR()：按 NDTR 计算已收长度，拷贝新段到 FIFO
 *  USART 空闲 IDLE -> USART1_IRQHandler -> UART_IdleCallback() -> 同上 Pump，处理不足半包的数据
 *  应用层（Ymodem）：USART1_PumpRxFromDma() + USART1_Read() 从 FIFO 取字节（主循环中可关中断后 Pump 一次）
 */

void Init_USART(void);

void UART_IdleCallback(void);
void USART1_PumpRxFromDma(void);

uint16_t USART1_Available(void);

uint16_t USART1_Read(uint8_t *dst, uint16_t len);

uint16_t USART1_ReadClaim(uint8_t **pptr);

void USART1_ReadCommit(uint16_t len);

uint32_t USART1_RxDropped(void);

/** 清空 FIFO，并把 DMA 读指针同步到当前位置，丢弃环内未取走的字节（OTA 开始前建议调用） */
void USART1_ALLData_flush(void);

#endif
