/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MMC5983_ADDR (0x30 << 1)
#define ASM330_ADDR  (0x6A << 1)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
void MX_I2C1_Init(void);
void MX_I2C2_Init(void);
void MX_I2C3_Init(void);
void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
HAL_StatusTypeDef MMC5983_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef MMC5983_Trigger(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef MMC5983_Read(I2C_HandleTypeDef *hi2c, int32_t *mx, int32_t *my, int32_t *mz);
HAL_StatusTypeDef ASM330_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef ASM330_Read(I2C_HandleTypeDef *hi2c, int16_t *ax, int16_t *ay, int16_t *az);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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

  /* Latch power supply immediately as the absolute first action to avoid shutdown when switch is released */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStructPower = {0};
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
  GPIO_InitStructPower.Pin = GPIO_PIN_3;
  GPIO_InitStructPower.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStructPower.Pull = GPIO_NOPULL;
  GPIO_InitStructPower.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStructPower);

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
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_I2C3_Init();
  MX_USART3_UART_Init();
  
  /* USER CODE BEGIN 2 */
  // Giữ chân V5_ENABLE mức HIGH để cấp nguồn cho siêu tụ
  HAL_GPIO_WritePin(GPIOC, V5_enable_Pin, GPIO_PIN_SET);
  
  // Tắt xả siêu tụ điện (DISCHARGE = LOW)
  HAL_GPIO_WritePin(GPIOA, Discharge_Pin, GPIO_PIN_RESET);
  
  // Mặc định tắt các tải (Relay/LED) ban đầu
  HAL_GPIO_WritePin(GPIOA, Load1_enable_Pin | Load2_enable_Pin, GPIO_PIN_RESET);

  // Khởi tạo các cảm biến
  MMC5983_Init(&hi2c1); // Mag 2
  MMC5983_Init(&hi2c2); // Mag 1
  ASM330_Init(&hi2c3);  // IMU
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 1. Kích hoạt đo từ trường cho cả 2 cảm biến Mag
    MMC5983_Trigger(&hi2c1);
    MMC5983_Trigger(&hi2c2);
    
    // 2. Chờ 12ms để cảm biến hoàn thành đo đạc (chu kỳ đo MMC5983 là khoảng 8-10ms)
    HAL_Delay(12);
    
    // 3. Đọc dữ liệu từ trường thô
    int32_t mx1 = 0, my1 = 0, mz1 = 0;
    int32_t mx2 = 0, my2 = 0, mz2 = 0;
    MMC5983_Read(&hi2c2, &mx1, &my1, &mz1); // Mag 1 (I2C2)
    MMC5983_Read(&hi2c1, &mx2, &my2, &mz2); // Mag 2 (I2C1)
    
    // 4. Đọc dữ liệu gia tốc góc IMU
    int16_t ax = 0, ay = 0, az = 0;
    ASM330_Read(&hi2c3, &ax, &ay, &az);     // IMU (I2C3)
    
    // Bộ lọc thông thấp số (Exponential Moving Average) tối ưu cho STM32
    static float mx1_f = 0.0f, my1_f = 0.0f, mz1_f = 0.0f;
    static float mx2_f = 0.0f, my2_f = 0.0f, mz2_f = 0.0f;
    static float ax_f = 0.0f, ay_f = 0.0f, az_f = 0.0f;
    static uint8_t filter_init = 0;
    
    if (!filter_init) {
        mx1_f = (float)mx1; my1_f = (float)my1; mz1_f = (float)mz1;
        mx2_f = (float)mx2; my2_f = (float)my2; mz2_f = (float)mz2;
        ax_f = (float)ax; ay_f = (float)ay; az_f = (float)az;
        filter_init = 1;
    } else {
        const float alpha_mag = 0.20f; // Hệ số lọc từ trường (giảm nhiễu tần số cao)
        const float alpha_acc = 0.15f; // Hệ số lọc gia tốc (chống rung lắc cơ học)
        
        mx1_f = alpha_mag * mx1 + (1.0f - alpha_mag) * mx1_f;
        my1_f = alpha_mag * my1 + (1.0f - alpha_mag) * my1_f;
        mz1_f = alpha_mag * mz1 + (1.0f - alpha_mag) * mz1_f;
        
        mx2_f = alpha_mag * mx2 + (1.0f - alpha_mag) * mx2_f;
        my2_f = alpha_mag * my2 + (1.0f - alpha_mag) * my2_f;
        mz2_f = alpha_mag * mz2 + (1.0f - alpha_mag) * mz2_f;
        
        ax_f = alpha_acc * ax + (1.0f - alpha_acc) * ax_f;
        ay_f = alpha_acc * ay + (1.0f - alpha_acc) * ay_f;
        az_f = alpha_acc * az + (1.0f - alpha_acc) * az_f;
    }
    
    // 5. Định dạng chuỗi gửi telemetry đã lọc sang ESP32 qua USART3
    char tx_buf[128];
    int len = sprintf(tx_buf, "MMC1: %ld %ld %ld | MMC2: %ld %ld %ld | ACC: %d %d %d\r\n",
                      (int32_t)mx1_f, (int32_t)my1_f, (int32_t)mz1_f,
                      (int32_t)mx2_f, (int32_t)my2_f, (int32_t)mz2_f,
                      (int)ax_f, (int)ay_f, (int)az_f);
    HAL_UART_Transmit(&huart3, (uint8_t*)tx_buf, len, 100);
    
    // 6. Nhận lệnh điều khiển tải (Relay/LED) từ ESP32 qua UART3 trực tiếp bằng thanh ghi (chống treo ORE)
    if (USART3->ISR & USART_ISR_ORE) {
        USART3->ICR = USART_ICR_ORECF; // Xóa lỗi tràn bộ đệm (Overrun)
    }
    if (USART3->ISR & USART_ISR_FE) {
        USART3->ICR = USART_ICR_FECF;  // Xóa lỗi khung truyền (Framing error)
    }
    if (USART3->ISR & USART_ISR_NE) {
        USART3->ICR = USART_ICR_NECF;  // Xóa lỗi nhiễu (Noise error)
    }
    // TEST PHẦN CỨNG: Nếu chập chân RX (PC5 / J4 Pin 4) xuống GND, tự động bật LED/Relay để test tiếp xúc
    if ((GPIOC->IDR & GPIO_PIN_5) == 0) {
        HAL_GPIO_WritePin(GPIOA, Load1_enable_Pin | Load2_enable_Pin, GPIO_PIN_SET);
    }
    
    if (USART3->ISR & USART_ISR_RXNE) {
        uint8_t rx_data = (uint8_t)(USART3->RDR & 0xFF);
        if (rx_data == '1') {
            // Bật cả 2 tải để kích relay đóng đèn sáng
            HAL_GPIO_WritePin(GPIOA, Load1_enable_Pin | Load2_enable_Pin, GPIO_PIN_SET);
            HAL_UART_Transmit(&huart3, (uint8_t*)"[DEBUG] STM32 RX 1\r\n", 20, 50);
        } else if (rx_data == '0') {
            // Tắt cả 2 tải
            HAL_GPIO_WritePin(GPIOA, Load1_enable_Pin | Load2_enable_Pin, GPIO_PIN_RESET);
            HAL_UART_Transmit(&huart3, (uint8_t*)"[DEBUG] STM32 RX 0\r\n", 20, 50);
        }
    }
    
    // 7. Chờ 100ms trước khi bắt đầu chu kỳ đo tiếp theo (chu kỳ cập nhật ~8-9Hz, rất ổn định và mượt)
    HAL_Delay(100);
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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6; // 4 MHz clock
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @retval None
  */
void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00D00E26; // Cấu hình timing 100kHz I2C tại tần số MSI 4MHz
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @retval None
  */
void MX_I2C2_Init(void)
{
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00D00E26; // Cấu hình timing 100kHz I2C tại tần số MSI 4MHz
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C3 Initialization Function
  * @retval None
  */
void MX_I2C3_Init(void)
{
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x00D00E26; // Cấu hình timing 100kHz I2C tại tần số MSI 4MHz
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function
  * @retval None
  */
void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level for PC13 (V5_enable) */
  HAL_GPIO_WritePin(V5_enable_GPIO_Port, V5_enable_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level for Discharge and Load Switch control */
  HAL_GPIO_WritePin(Discharge_GPIO_Port, Discharge_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, Load1_enable_Pin|Load2_enable_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : V5_enable_Pin */
  GPIO_InitStruct.Pin = V5_enable_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(V5_enable_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Discharge_Pin Load1_enable_Pin Load2_enable_Pin */
  GPIO_InitStruct.Pin = Discharge_Pin|Load1_enable_Pin|Load2_enable_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ========================================================================= */
/* ===================== CÁC HÀM MSP INIT CHO NGOẠI VI ===================== */
/* ========================================================================= */

void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(hi2c->Instance==I2C1)
  {
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // SCL = PB6, SDA = PB7
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
  else if(hi2c->Instance==I2C2)
  {
    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // SCL = PB10, SDA = PB11
    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
  else if(hi2c->Instance==I2C3)
  {
    __HAL_RCC_I2C3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // SCL = PC0, SDA = PC1
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(huart->Instance==USART3)
  {
    /* Bật bộ dao động nội HSI16 và đợi sẵn sàng */
    __HAL_RCC_HSI_ENABLE();
    while(__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET) {}

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART3;
    PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_HSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // TX = PC4, RX = PC5
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  }
}

/* ========================================================================= */
/* ======================== DRIVERS CHO CẢM BIẾN ======================== */
/* ========================================================================= */

HAL_StatusTypeDef MMC5983_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t data = 0x08; // Auto_SR_en = 1
    return HAL_I2C_Mem_Write(hi2c, MMC5983_ADDR, 0x09, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

HAL_StatusTypeDef MMC5983_Trigger(I2C_HandleTypeDef *hi2c) {
    uint8_t data = 0x09; // TM_M = 1, Auto_SR_en = 1
    return HAL_I2C_Mem_Write(hi2c, MMC5983_ADDR, 0x09, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

HAL_StatusTypeDef MMC5983_Read(I2C_HandleTypeDef *hi2c, int32_t *mx, int32_t *my, int32_t *mz) {
    uint8_t reg[7];
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, MMC5983_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, reg, 7, 100);
    if (status == HAL_OK) {
        uint32_t raw_x = ((uint32_t)reg[0] << 10) | ((uint32_t)reg[1] << 2) | (reg[6] >> 6);
        uint32_t raw_y = ((uint32_t)reg[2] << 10) | ((uint32_t)reg[3] << 2) | ((reg[6] >> 4) & 0x03);
        uint32_t raw_z = ((uint32_t)reg[4] << 10) | ((uint32_t)reg[5] << 2) | ((reg[6] >> 2) & 0x03);
        
        *mx = (int32_t)raw_x - 131072;
        *my = (int32_t)raw_y - 131072;
        *mz = (int32_t)raw_z - 131072;
    }
    return status;
}

HAL_StatusTypeDef ASM330_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t who_am_i = 0;
    HAL_I2C_Mem_Read(hi2c, ASM330_ADDR, 0x0F, I2C_MEMADD_SIZE_8BIT, &who_am_i, 1, 100);
    
    // Cấu hình gia tốc kế: CTRL1_XL (0x10) = 0x40 (104Hz, ±2g)
    uint8_t data = 0x40;
    return HAL_I2C_Mem_Write(hi2c, ASM330_ADDR, 0x10, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

HAL_StatusTypeDef ASM330_Read(I2C_HandleTypeDef *hi2c, int16_t *ax, int16_t *ay, int16_t *az) {
    uint8_t reg[6];
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, ASM330_ADDR, 0x28, I2C_MEMADD_SIZE_8BIT, reg, 6, 100);
    if (status == HAL_OK) {
        *ax = (int16_t)((uint16_t)reg[1] << 8 | reg[0]);
        *ay = (int16_t)((uint16_t)reg[3] << 8 | reg[2]);
        *az = (int16_t)((uint16_t)reg[5] << 8 | reg[4]);
    }
    return status;
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
