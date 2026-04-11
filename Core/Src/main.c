/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Ymodem OTA：USART1 DMA -> FIFO；协议层从 FIFO 读并写 Flash。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "MY_USART.h"
#include "MY_Ymodem.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// 定义函数指针类型，对应代码中的__func，用于指向APP的复位中断函数
typedef void (*__func)(void);

typedef struct {
    uint8_t active_partition;
    uint8_t upgrade_status;
} upgrade_flag_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define IDLE_FLAG              0x00// 空闲状态
#define START_UPGRADE_FLAG     0x01// 刚开始升级
#define FIRMWARE_BURNED_FLAG   0x02// 完成固件烧录

volatile uint8_t upgrade_flag_Ymodem = 0;

#define APP_ADDR_A      0x08004000// A 区起始地址
#define APP_ADDR_B      0x08008000// B 区起始地址

#define PARTITION_A     0// A 区
#define PARTITION_B     1// B 区
//标志位区宏定义
#define  FLAG_ADDRESS    0x0800F000// 标志位存储地址，位于最后一个扇区

#define KEY1_GPIO_PORT    GPIOE
#define KEY1_GPIO_PIN     GPIO_PIN_3

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */



/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
uint8_t KEY1_Scan(void);
void JumpToApp(uint32_t appBase);
void read_upgrade_flag(upgrade_flag_t *upgrade_flag);


void Update_flag_states(uint8_t status);
void Update_flag_partition(uint8_t partition);
uint8_t Flash_CheckData(uint32_t addr);
//static void OTA_PrintResult(int code);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
upgrade_flag_t upgrade_flag={0xA5,0xA5};
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  Init_USART();
    
//------------------------------------查看是否是新设备-------------------------
  if (0x00 == Flash_CheckData(FLAG_ADDRESS + offsetof(upgrade_flag_t, active_partition))
      && 0x00 == Flash_CheckData(FLAG_ADDRESS + offsetof(upgrade_flag_t, upgrade_status)))
  {

      Update_flag_partition(PARTITION_B);//AB区都没有，先认为B区有，让更新到A区去
      Update_flag_states(START_UPGRADE_FLAG);
      
  }//NO APP
//---------------------------------------------------------------------------

  //查看当前APP运行位置
  read_upgrade_flag(&upgrade_flag);
  printf("Current active partition: %s\r\n", (upgrade_flag.active_partition == PARTITION_A) ? "A" : "B");

  //------------------------------------------------------------------------------
  //实现断点续传功能：如果上次升级完成但未跳转，直接跳转到APP区；
  //如果上次只是完成固件下载升级未完成，则进入升级检查流程，校验固件是否正确
  switch(upgrade_flag.upgrade_status)
  {
      case IDLE_FLAG:
//----------------------------------按键模拟版本检查--------------------------------
    printf("Please press the KEY1 button within 2 seconds to enter OTA mode.\r\n");
    uint32_t previous_time = HAL_GetTick();//获取当前时钟 
    while(1)
    {
        if (HAL_GetTick() - previous_time <= 2000 && KEY1_Scan()) 
        {
            upgrade_flag_Ymodem=1;
            //跳转到OTA模式
            //将信息直接写道标志位区，APP区的代码会根据这个标志位来决定是否进入升级流程
            Update_flag_states(START_UPGRADE_FLAG);
            break;
        }
        if(HAL_GetTick() - previous_time > 2000)
        {
          uint32_t app_addr = (upgrade_flag.active_partition == PARTITION_A) ? APP_ADDR_A : APP_ADDR_B;
          JumpToApp(app_addr);
        }
    }
//----------------------------------按键模拟版本检查 end --------------------------------
          break;
      case START_UPGRADE_FLAG:
            printf("Start fetching the new version again");
            upgrade_flag_Ymodem=1;
            //如果上次刚开始升级，说明还未完成固件烧录，继续进入升级流程
          break;
      case FIRMWARE_BURNED_FLAG:
          //回滚:返回上一版本，并尝试再此获取新版本
            printf("Roll back to the previous version");
            Update_flag_partition((upgrade_flag.active_partition == PARTITION_A) ? PARTITION_B : PARTITION_A);
            Update_flag_states(START_UPGRADE_FLAG);
            upgrade_flag_Ymodem=1;
          //如果上次只是完成固件下载升级未完成，则进入升级检查流程，校验固件是否正确
          break;
  }
//------------------------------------------------------------------------------

if(upgrade_flag_Ymodem==1)
{
  upgrade_flag_Ymodem=0;
//-------------------------------接收新固件----------------------------------
    printf("\r\n\r\n*** STM32 Ymodem OTA ***\r\n");
    printf("UART1 %lu baud | DMA circular RX | IDLE + HT/TC -> FIFO\r\n", (unsigned long)huart1.Init.BaudRate);
    printf("Target Flash: %08lX\r\n", (upgrade_flag.active_partition == PARTITION_A) ? APP_ADDR_A : APP_ADDR_B);
    printf("Send firmware (Ymodem-1K CRC)...\r\n");

    int ret = MY_ymodem_ReceiveBinToFlash((upgrade_flag.active_partition == PARTITION_A) ? APP_ADDR_B : APP_ADDR_A);
    if (ret==OTA_OK)
    {
      //烧录完成，更新分区
      Update_flag_partition((upgrade_flag.active_partition == PARTITION_A) ? PARTITION_B : PARTITION_A);
      Update_flag_states(FIRMWARE_BURNED_FLAG);//完成固件烧录
      printf("Upgrade complete.\r\n");

      if(upgrade_flag.upgrade_status == FIRMWARE_BURNED_FLAG)
      {
          //跳转到APP区
          // 根据分区号选择地址
          uint32_t app_addr = (upgrade_flag.active_partition == PARTITION_A) ? APP_ADDR_A : APP_ADDR_B;
          JumpToApp(app_addr);//跳转到APP运行  如果APP在运行的中期能将标志位改为空闲（IDLE_FLAG），则认为代码能正常运行
      }
    }
//-------------------------------接收新固件end----------------------------------
}


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
      HAL_Delay(500);
      printf("while(1)！！！！！！\r\n");
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// KEY1 按键扫描函数（返回1表示按下，0表示未按下）
uint8_t KEY1_Scan(void)
{
    if(HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == GPIO_PIN_RESET)
    {
        HAL_Delay(20); // 软件消抖
        if(HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == GPIO_PIN_RESET)
        {
            while(HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == GPIO_PIN_RESET); // 等待按键松开
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
            return 1;
        }
    }
    return 0;
}


void Update_flag_states(uint8_t status)
{
  upgrade_flag.upgrade_status = status;
  my_flash_write(FLAG_ADDRESS + offsetof(upgrade_flag_t, upgrade_status), (uint8_t *)&upgrade_flag.upgrade_status, sizeof(upgrade_flag.upgrade_status));
}

void Update_flag_partition(uint8_t partition)
{
  upgrade_flag.active_partition = partition;
  my_flash_write(FLAG_ADDRESS + offsetof(upgrade_flag_t, active_partition), (uint8_t *)&upgrade_flag.active_partition, sizeof(upgrade_flag.active_partition));
}


/**
 * @brief  恢复所有GPIO为默认浮空输入状态，避免跳转后引脚电平异常
 * @note   适配正点原子STM32F1战舰板（STM32F103ZET6）全GPIO端口
 */
void all_gpio_init_default(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能所有GPIO端口时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    // 配置所有引脚为复位默认状态：浮空输入
    GPIO_InitStruct.Pin = GPIO_PIN_All;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    // 可选：关闭GPIO时钟，APP会重新初始化
    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_GPIOC_CLK_DISABLE();
    __HAL_RCC_GPIOD_CLK_DISABLE();
    __HAL_RCC_GPIOE_CLK_DISABLE();
    __HAL_RCC_GPIOF_CLK_DISABLE();
    __HAL_RCC_GPIOG_CLK_DISABLE();
}


/**
  * @brief  检查Flash指定地址是否存在有效数据（空=0xFFFFFFFF）
  * @param  addr: 要检查的Flash地址
  * @retval 0: 无数据（空）  1: 有数据
  */
uint8_t Flash_CheckData(uint32_t addr)
{
    // 读取32位数据（STM32 Flash 按字读取最稳定）
    uint32_t data = *(uint32_t *)addr;

    // 空Flash = 0xFFFFFFFF → 无数据
    if(data == 0xFFFFFFFF)
    {
        return 0;  // 无数据
    }
    else
    {
        return 1;  // 有数据
    }
}



typedef void (*pFunc)(void);
void JumpToApp(uint32_t appBase)
{
    all_gpio_init_default();
    uint32_t sp = *(uint32_t *)appBase;
    uint32_t pc = *(uint32_t *)(appBase + 4);
    
    if ((sp & 0x2FFE0000) != 0x20000000)
        return;
    
    __disable_irq();
    
    SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk;   /* 清 PendSV 挂起 */
    
#if defined(USE_FREERTOS)  /*如果使用FreeRTOS*/
    vTaskEndScheduler();    /*关闭任务调度器*/
#endif

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    
    SCB->VTOR=0x8002000U;
    
    __set_MSP(sp);
    ((pFunc)pc)();          // 跳过去永不返回
}



void read_upgrade_flag(upgrade_flag_t *upgrade_flag)
{
    // 1. 读取 active_partition：从 FLAG_ADDRESS 取 1 字节
    upgrade_flag->active_partition = *(volatile uint8_t *)FLAG_ADDRESS;
    // 2. 读取 upgrade_status：从 FLAG_ADDRESS + 1 取 1 字节
    upgrade_flag->upgrade_status = *(volatile uint8_t *)(FLAG_ADDRESS + sizeof(uint8_t));
}



/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
