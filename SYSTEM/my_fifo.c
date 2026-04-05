#include "stm32f1xx_hal.h"
#include "usart.h"
#include "my_fifo.h"
#include <string.h> 

/*--- �������� ---*/
void fifo_reset(fifo_t *restrict pfifo)
{

    pfifo->head=0;
    pfifo->tail=0;

}

/*--- �ж��� ---*/
uint8_t fifo_full(fifo_t *pfifo)
{

    if(((pfifo->tail + 1)&(FIFO_SIZE-1))== pfifo->head)
    {
        return 1;    
    }

    return 0;    
    
}


/*--- �жϿ� ---*/
uint8_t fifo_empty(fifo_t *pfifo)
{

    if(pfifo->head==pfifo->tail)
    {
        return 1;    
    }

    return 0;    
    
}

/*���Ӳ���*/
uint8_t fifo_output(fifo_t *pfifo,uint8_t *out)
{

    if(fifo_empty(pfifo)==1)
        return 0;
    
    *out = pfifo->fifo_buf[pfifo->head];
    pfifo->head = (pfifo->head + 1) & (FIFO_SIZE-1);
    return 1;

}

/*��Ӳ���*/
uint8_t fifo_input(fifo_t *pfifo,uint8_t intput)
{
    if(fifo_full(pfifo)==1)
        return 0;
    
    pfifo->fifo_buf[pfifo->tail] = intput;
    pfifo->tail = (pfifo->tail + 1) & (FIFO_SIZE-1); // 3. ����    
    return 1;
}

/*�鿴�Ѿ�ʹ�ÿռ�*/
uint16_t fifo_used(fifo_t *pfifo)
{
   return (pfifo->tail - pfifo->head) & (FIFO_SIZE - 1);   
}


uint16_t fifo_free(fifo_t *pfifo)
{
    if (pfifo == NULL) return 0;
    return (FIFO_SIZE - 1) - fifo_used(pfifo);
}


uint16_t fifo_write(fifo_t *pfifo, const uint8_t *src, uint16_t len)
{
    
    if (pfifo == NULL || src == NULL || len == 0){return 0;}
    
    uint16_t free=fifo_free(pfifo);
    if(free<len)len=free;

    uint16_t first = FIFO_SIZE - pfifo->tail;/* ��һ�������ռ� */
    //д�����ݳ���С�ڵ�һ�οռ�
    if(len<first){first=len;}
    
    memcpy(&pfifo->fifo_buf[pfifo->tail], src, first); /* ��һ�� */           
    
    if(first<len)
    {
        memcpy(&pfifo->fifo_buf[0], src + first, len-first); /* �ڶ��Σ��Ѿ����� */            
    }
    
    pfifo->tail = (pfifo->tail + len) & (FIFO_SIZE - 1); /* һ�θ���βָ�� */    
    return len;// ����ʵ��д����ֽ���������С�����󳤶�
}


uint16_t fifo_read(fifo_t *pfifo, uint8_t *dst, uint16_t len)
{
    if (pfifo == NULL || len == 0|| dst == NULL) return 0;
    
    uint16_t used=fifo_used(pfifo);
    if(used==0){return 0;}
    if(used<len){len=used;}
    
    uint16_t first = FIFO_SIZE - pfifo->head;
    if(len<first){first=len;}
    memcpy(dst, &pfifo->fifo_buf[pfifo->head], first);
    
    /* 3. ���ƶ��ڶ��� */
    if (first < len) 
    {
        memcpy(dst + first, &pfifo->fifo_buf[0], len - first);
    }
    
    /* 4. ���¶�ָ�� */
    pfifo->head = (pfifo->head + len) & (FIFO_SIZE - 1);    
    
    return len;  
}


uint16_t fifo_write_claim(fifo_t *pfifo, uint8_t **pptr)
{
    if (pfifo == NULL || pptr == NULL) return 0;

    uint16_t free = fifo_free(pfifo);          // �ܿ���
    uint16_t first = FIFO_SIZE - pfifo->tail;  // ��������
    uint16_t len = (free < first) ? free : first;

    *pptr = &pfifo->fifo_buf[pfifo->tail];         // ����дָ��
    return len;                                    // ��������д
}
    
uint16_t fifo_read_claim(fifo_t *pfifo, uint8_t **pptr)
{
    if (pfifo == NULL || pptr == NULL) return 0;

    uint16_t used = fifo_used(pfifo);  // ���ÿռ�
    if (used == 0) return 0;                                // �ն���

    uint16_t first = FIFO_SIZE - pfifo->head;                   // �����ɶ�����
    uint16_t len = (used < first) ? used : first;           // ʵ�ʿɶ�

    *pptr = &pfifo->fifo_buf[pfifo->head];                          // �������߶�ָ��
    return len;                                             // ���οɶ�����
}

void fifo_write_commit(fifo_t *pfifo, uint16_t len)
{
    if (pfifo == NULL || len == 0) return;
    uint16_t free = fifo_free(pfifo);
    if (len > free) len = free;
    pfifo->tail = (pfifo->tail + len) & (FIFO_SIZE - 1);
}

void fifo_read_commit(fifo_t *pfifo, uint16_t len)
{
    if (pfifo == NULL || len == 0) return;
    uint16_t used = fifo_used(pfifo);
    if (len > used) len = used;
    pfifo->head = (pfifo->head + len) & (FIFO_SIZE - 1);
}









