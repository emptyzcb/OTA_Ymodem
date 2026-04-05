/**
 * USART1 + DMA 环形接收 -> 软件 FIFO，供 Ymodem OTA 等协议使用。
 *
 * 中断路径：DMA HT/TC -> Pump；USART IDLE -> Pump。
 * 主循环中读 FIFO 前可调用 USART1_PumpRxFromDma()，与 ISR 内 Pump 二选一防重入需关中断。
 */
#include "usart.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include "MY_USART.h"
#include "dma.h"
#include "my_fifo.h"

static fifo_t usart1_rx_fifo;
static uint8_t usart1_dma_rx_buf[MY_USART1_DMA_BUF_SIZE];
static volatile uint16_t usart1_dma_last_pos;
static volatile uint32_t usart1_rx_dropped;

static void USART1_PumpRxFromDmaISR(void)
{
    if (huart1.hdmarx == NULL)
    {
        return;
    }

    uint16_t pos = (uint16_t)(MY_USART1_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx));
    uint16_t last = usart1_dma_last_pos;

    if (pos == last)
    {
        return;
    }

    if (pos > last)
    {
        uint16_t len = (uint16_t)(pos - last);
        uint16_t written = fifo_write(&usart1_rx_fifo, &usart1_dma_rx_buf[last], len);
        usart1_rx_dropped += (uint32_t)(len - written);
    }
    else
    {
        uint16_t first = (uint16_t)(MY_USART1_DMA_BUF_SIZE - last);
        uint16_t written1 = fifo_write(&usart1_rx_fifo, &usart1_dma_rx_buf[last], first);
        usart1_rx_dropped += (uint32_t)(first - written1);

        uint16_t second = pos;
        uint16_t written2 = fifo_write(&usart1_rx_fifo, &usart1_dma_rx_buf[0], second);
        usart1_rx_dropped += (uint32_t)(second - written2);
    }

    usart1_dma_last_pos = pos;
}

void USART1_PumpRxFromDma(void)
{
    if (huart1.hdmarx == NULL)
    {
        return;
    }
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    USART1_PumpRxFromDmaISR();
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}

uint16_t USART1_Available(void)
{
    return fifo_used(&usart1_rx_fifo);
}

uint16_t USART1_Read(uint8_t *dst, uint16_t len)
{
    return fifo_read(&usart1_rx_fifo, dst, len);
}

uint16_t USART1_ReadClaim(uint8_t **pptr)
{
    return fifo_read_claim(&usart1_rx_fifo, pptr);
}

void USART1_ReadCommit(uint16_t len)
{
    fifo_read_commit(&usart1_rx_fifo, len);
}

uint32_t USART1_RxDropped(void)
{
    return usart1_rx_dropped;
}

void USART1_ALLData_flush(void)
{
    if (huart1.hdmarx == NULL)
    {
        fifo_reset(&usart1_rx_fifo);
        usart1_dma_last_pos = 0;
        return;
    }

    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);

    uint16_t pos = (uint16_t)(MY_USART1_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx));
    fifo_reset(&usart1_rx_fifo);
    usart1_dma_last_pos = pos;

    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}

void Init_USART(void)
{
    fifo_reset(&usart1_rx_fifo);
    usart1_dma_last_pos = 0;
    usart1_rx_dropped = 0;

    if (HAL_UART_Receive_DMA(&huart1, usart1_dma_rx_buf, MY_USART1_DMA_BUF_SIZE) != HAL_OK)
    {
        return;
    }
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
    printf("USART1 DMA+FIFO OK, IDLE on.\r\n");
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        USART1_PumpRxFromDmaISR();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        USART1_PumpRxFromDmaISR();
    }
}

void UART_IdleCallback(void)
{
    USART1_PumpRxFromDmaISR();
}

int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}
