#ifndef __HAL_FLASH_H__
#define __HAL_FLASH_H__
#include "stm32f1xx_hal.h"

#define PAGE_SIZE 2048

uint8_t my_flash_write(uint32_t addr,uint8_t* src,uint16_t len);

#endif 
