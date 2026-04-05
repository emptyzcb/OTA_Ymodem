#include "HAL_Flash.h"
#include "stm32f1xx_hal.h"
#include <string.h> 
#include <stdio.h>   /* ����ͷ�ļ� */
#include <stdarg.h>

static uint8_t my_flash_PageEraseProgram(uint32_t addr,uint8_t* DataSrc,uint16_t len)
{
        /* 3. ���� Flash ������ */
        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
        /*------����Ŀ��ҳ------*/        
        FLASH_EraseInitTypeDef erase = {
            .TypeErase   = FLASH_TYPEERASE_PAGES,
            .PageAddress = addr,
            .NbPages     = 1
        };
        uint32_t page_error = 0;
        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
        {
            printf("EraseError!");
            HAL_FLASH_Lock();
            return 0;    

          
        }
        /*------ end -------*/

        /*------ дĿ��------ STM32F1 半字编程；奇数 len 最后一字节单独写，高字节填 0xFF */
        uint32_t i = 0;
        while (i + 2u <= (uint32_t)len)
        {
            uint16_t data = (uint16_t)DataSrc[i] | (uint16_t)((uint16_t)DataSrc[i + 1u] << 8);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, data) != HAL_OK)
            {
                printf("ProgramError!");
                HAL_FLASH_Lock();
                return 0;
            }
            i += 2u;
        }
        if (i < (uint32_t)len)
        {
            uint16_t data = (uint16_t)DataSrc[i] | 0xFF00u;
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, data) != HAL_OK)
            {
                printf("ProgramError!");
                HAL_FLASH_Lock();
                return 0;
            }
        }
        /*------ end -------*/    
        HAL_FLASH_Lock();
        return 1;
}


/**
 * @brief   �����ַ�����ⳤ��д Flash�����Զ���������ҳ/������������
 * @param   addr Ŀ����ʼ��ַ
 * @param   src  ����Դָ��
 * @param   len  Ҫд����ֽ���
 * @retval  1 �ɹ���0 ����
 */
uint8_t my_flash_write(uint32_t addr,uint8_t* src,uint16_t len)
{
    const uint32_t page1_base = addr & ~(PAGE_SIZE -1);//��ȡҳ�׵�ַ
    const uint16_t offset = addr & (PAGE_SIZE - 1);  //ҳ��ƫ����
    uint16_t remain = PAGE_SIZE-offset;//�ռ�ʣ����
    
    /* 单线程 OTA 使用；放静态区避免占用默认 0x400 栈导致 HardFault */
    static uint8_t last_data[PAGE_SIZE];
    static uint8_t all_data[PAGE_SIZE];

    memcpy(last_data,(uint8_t*)page1_base,offset);//�Ƚ�ҳ����ȫ��������
     
     
    if(len<=remain)//ҳʣ��ռ��㹻
    {
        //printf("enough!");
        //��ƴ������
        memcpy(all_data,last_data,offset);//�������ݿ�����������
        memcpy(all_data+offset,src,len);  //�������ݿ�����������          
          
        if(my_flash_PageEraseProgram(page1_base,all_data,len+offset)!=1)
        {
                //printf("PageEraseProgramError!"); 
                return 0;
        } 
        return 1;
    }
    else
    {
        //ҳʣ��ռ䲻�㣬����ҳ��д
        uint16_t B_size=len-remain;
       
        //��ƴ������
        memcpy(all_data,last_data,offset);//�������ݿ�����������
        memcpy(all_data+offset,src,remain);  //�������ݿ�����������             
        
        //��д����ҳ
        if(my_flash_PageEraseProgram(page1_base,all_data,PAGE_SIZE)!=1)
        {
                printf("PageEraseProgramError!"); 
                return 0;               
        }
        
        if(my_flash_PageEraseProgram(page1_base+PAGE_SIZE,src+remain,B_size)!=1)
        {
                printf("PageEraseProgramError!");   
                return 0;            
        }
        return 1;
        
    }
       
    
}



