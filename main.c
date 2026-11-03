/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MIDI-controlled LEDs using GPIO software PWM and TIM6
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdint.h>

/* Private defines -----------------------------------------------------------*/
#define MIDI_PACKET_SIZE       3U
#define LED_COUNT              7U
#define PWM_STEPS              128U

/* With a 170 MHz TIM6 clock and 128 PWM phases:
 * TIM6 update frequency = 170 MHz / ((170 + 1) * (77 + 1))
 *                       = approximately 12,745.54 Hz
 * PWM frequency         = 12,745.54 Hz / 128
 *                       = approximately 99.57 Hz (about 100 Hz)
 *
 * The prescaler value 170 divides the timer clock by 171.
 * The period value 77 gives 78 counter ticks per update.
 */
#define TIM6_PRESCALER         170U
#define TIM6_PERIOD            77U

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart2;

static uint8_t rx_buffer[MIDI_PACKET_SIZE];

/* Each brightness value is in the range 0..128.
 * 0   = always OFF
 * 128 = always ON
 * This variable is shared between main() and the TIM6 interrupt.
 */
static volatile uint8_t led_brightness[LED_COUNT] = {0U};

/* Private function prototypes ----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART2_UART_Init(void);
static void Process_MIDI_Event(uint8_t channel, uint8_t note, uint8_t velocity);
void Error_Handler(void);

/* Private user code ---------------------------------------------------------*/

/**
  * @brief  Convert a MIDI event into the brightness of one LED.
  * @param  channel  MIDI channel (currently not used).
  * @param  note     LED index: 0..6 corresponds to PC0..PC6.
  * @param  velocity MIDI velocity: 0..127.
  * @retval None
  *
  * NOTE: This preserves the mapping in your original main.c. It assumes that
  * the Python program sends note values 0..6, not standard piano note numbers
  * such as 60, 62, 64, etc.
  */
static void Process_MIDI_Event(uint8_t channel, uint8_t note, uint8_t velocity)
{
    (void)channel; /* Channel filtering is not implemented in this version. */

    if (note >= LED_COUNT)
    {
        return;
    }

    /* MIDI velocity is 0..127; PWM brightness is 0..128. */
    if (velocity > 127U)
    {
        velocity = 127U;
    }

    led_brightness[note] = (uint8_t)(((uint16_t)velocity * PWM_STEPS) / 127U);
}

/**
  * @brief  Main program.
  */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM6_Init();
    MX_USART2_UART_Init();

    /* Start TIM6 with interrupts enabled. The interrupt drives software PWM. */
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        /* The Python program must send fixed three-byte packets:
         * [channel, note, velocity].
         */
        if (HAL_UART_Receive(&huart2, rx_buffer, MIDI_PACKET_SIZE, HAL_MAX_DELAY) == HAL_OK)
        {
            Process_MIDI_Event(rx_buffer[0], rx_buffer[1], rx_buffer[2]);
        }
    }
}

/**
  * @brief  TIM6 callback. Called once for every TIM6 update interrupt.
  *         This function implements the software PWM for PC0..PC6.
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    static uint8_t pwm_phase = 0U;
    uint16_t pins_to_set = 0U;
    uint16_t pins_to_reset = 0U;
    uint8_t i;

    if (htim->Instance != TIM6)
    {
        return;
    }

    /* Compare the current PWM phase with the requested brightness of each LED. */
    for (i = 0U; i < LED_COUNT; i++)
    {
        uint16_t pin = (uint16_t)(1U << i);

        if (pwm_phase < led_brightness[i])
        {
            pins_to_set |= pin;
        }
        else
        {
            pins_to_reset |= pin;
        }
    }

    /* Update all seven GPIO outputs. */
    if (pins_to_set != 0U)
    {
        HAL_GPIO_WritePin(GPIOC, pins_to_set, GPIO_PIN_SET);
    }

    if (pins_to_reset != 0U)
    {
        HAL_GPIO_WritePin(GPIOC, pins_to_reset, GPIO_PIN_RESET);
    }

    /* Move to the next PWM phase: 0, 1, ... 127, then back to 0. */
    pwm_phase++;
    if (pwm_phase >= PWM_STEPS)
    {
        pwm_phase = 0U;
    }
}

/**
  * @brief System Clock Configuration
  *        HSI 16 MHz / 4 * 85 / 2 = 170 MHz SYSCLK.
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief TIM6 Initialization Function
  */
static void MX_TIM6_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim6.Instance = TIM6;
    htim6.Init.Prescaler = TIM6_PRESCALER;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = TIM6_PERIOD;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Enable the shared TIM6/DAC interrupt. */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

/**
  * @brief USART2 Initialization Function
  */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief GPIO Initialization Function
  *        PC0..PC6 are configured as ordinary push-pull GPIO outputs.
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Start with all seven LEDs OFF. */
    HAL_GPIO_WritePin(GPIOC,
                      GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                      GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6,
                      GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif /* USE_FULL_ASSERT */

