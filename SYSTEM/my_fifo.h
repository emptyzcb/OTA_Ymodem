#ifndef __MY_FIFO_H__
#define __MY_FIFO_H__
#include "stm32f1xx_hal.h"

#define FIFO_SIZE 2048
typedef struct 
{
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t fifo_buf[FIFO_SIZE];
   
}fifo_t; 

/*--- �������� ---*/
void fifo_reset(fifo_t *restrict pfifo);
/*--- �ж��� ---*/
uint8_t fifo_full(fifo_t *pfifo);

uint8_t fifo_empty(fifo_t *pfifo);

uint8_t fifo_output(fifo_t *pfifo,uint8_t *out);

uint8_t fifo_input(fifo_t *pfifo,uint8_t intput);

uint16_t fifo_used(fifo_t *pfifo);

uint16_t fifo_free(fifo_t *pfifo);

uint16_t fifo_write(fifo_t *pfifo, const uint8_t *src, uint16_t len);

uint16_t fifo_read(fifo_t *pfifo, uint8_t *dst, uint16_t len);

uint16_t fifo_write_claim(fifo_t *pfifo, uint8_t **pptr);

uint16_t fifo_read_claim(fifo_t *pfifo, uint8_t **pptr);

void fifo_write_commit(fifo_t *pfifo, uint16_t len);

void fifo_read_commit(fifo_t *pfifo, uint16_t len);


#endif


