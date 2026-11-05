/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim4;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==========================================================================
 * Bomb UI
 *
 * Everything is drawn into the OLED frame buffer and pushed once per frame,
 * so there is no erase-then-draw flicker. Text is centred by measured width,
 * which is what keeps a long string like "DEFUSED" on one line.
 *
 * Panel is 128 x 64. Content stays inside a 1px border.
 * ======================================================================== */

#define BOMB_SECONDS      10        /* countdown length, real seconds */
#define BOMB_UI_MS        500       /* redraw period while counting down */
#define BOMB_GAP_START    600       /* first beat period, shrinks by 12% per round */

static unsigned char ui_text_w(const char *p, unsigned char size, unsigned char scale)
{
    unsigned int n = 0u;
    unsigned int cw;

    if (p == NULL) {
        return 0u;
    }
    while (p[n] != '\0') {
        n++;
    }
    if (scale == 0u) {
        scale = 1u;
    }
    cw = (unsigned int)(((size == 16u) ? 8u : 6u) * scale);

    n *= cw;
    return (unsigned char)((n > 255u) ? 255u : n);
}

static void ui_center(unsigned char y, const char *p, unsigned char size, unsigned char scale)
{
    unsigned int w = ui_text_w(p, size, scale);
    unsigned char x = (unsigned char)((w < 128u) ? ((128u - w) / 2u) : 0u);

    OLED_ShowStringScale(x, y, p, size, scale);
}

/* Idle screen: title, rule, prompt. No debug text - this is the one people see. */
static void ui_idle(void)
{
    OLED_Clear();
    OLED_Rect(0u, 0u, 127u, 63u, 1u);

    OLED_HLine(0u, 127u, 11u, 1u);
    OLED_FillRect(4u, 4u, 7u, 7u, 1u);
    OLED_ShowString(11u, 2u, "STANDBY", 8u);

    ui_center(20u, "BOMB READY", 16u, 1u);
    OLED_HLine(24u, 103u, 42u, 1u);
    ui_center(48u, "PRESS KEY1", 8u, 1u);

    OLED_Refresh();
}

/* Countdown: two big digits, progress bar, blinking armed lamp. */
static void ui_countdown(unsigned int remain_ms)
{
    unsigned int total = (unsigned int)BOMB_SECONDS * 1000u;
    unsigned int bar;
    unsigned char sec;

    OLED_Clear();
    OLED_Rect(0u, 0u, 127u, 63u, 1u);

    OLED_HLine(0u, 127u, 11u, 1u);
    OLED_ShowString(4u, 2u, "ARMED", 8u);
    if (((remain_ms / 500u) & 1u) == 0u) {
        OLED_FillRect(118u, 4u, 121u, 7u, 1u);
    } else {
        OLED_Rect(118u, 4u, 121u, 7u, 1u);
    }

    /* Always two digits so the number does not shift as it crosses 9. */
    sec = (unsigned char)((remain_ms + 999u) / 1000u);
    OLED_ShowNumScale(48u, 15u, (unsigned long)sec, 2u, 16u, 2u);

    OLED_Rect(8u, 50u, 119u, 58u, 1u);
    bar = (remain_ms * 108u) / total;
    if (bar > 108u) {
        bar = 108u;
    }
    if (bar > 0u) {
        OLED_FillRect(10u, 52u, (unsigned char)(10u + bar - 1u), 56u, 1u);
    }

    OLED_Refresh();
}

/* Success: a face, then the word, well clear of the right edge. */
static void ui_defused(void)
{
    OLED_Clear();
    OLED_Rect(0u, 0u, 127u, 63u, 1u);

    OLED_Circle(64, 14, 9, 1u);
    OLED_FillRect(59u, 9u, 60u, 10u, 1u);
    OLED_FillRect(68u, 9u, 69u, 10u, 1u);
    /* smile: shallow arc, the ends sit one pixel above the middle */
    OLED_HLine(61u, 67u, 19u, 1u);
    OLED_SetPixel(60u, 18u, 1u);
    OLED_SetPixel(68u, 18u, 1u);
    OLED_SetPixel(59u, 17u, 1u);
    OLED_SetPixel(69u, 17u, 1u);

    ui_center(27u, "DEFUSED", 16u, 1u);
    OLED_HLine(32u, 95u, 47u, 1u);
    ui_center(51u, "SAFE", 8u, 1u);

    OLED_Refresh();
}

/* Detonation: whole panel inverts on alternate frames. */
static void ui_boom(unsigned char invert)
{
    OLED_Clear();

    if (invert != 0u) {
        OLED_FillRect(0u, 0u, 127u, 63u, 1u);
        OLED_SetDrawMode(0u);
        OLED_Rect(2u, 2u, 125u, 61u, 1u);
        ui_center(16u, "BOOM", 16u, 2u);
        OLED_SetDrawMode(1u);
    } else {
        OLED_Rect(0u, 0u, 127u, 63u, 1u);
        ui_center(16u, "BOOM", 16u, 2u);
    }

    OLED_Refresh();
}

/* --------------------------------------------------------------------------
 * Defusing is a gamble.
 *
 * The odds get worse the longer you wait, so the interesting decision is not
 * whether to press KEY2 but when. The generator is seeded from the millisecond
 * counter and the SysTick down-counter - both are moving freely by the time a
 * human manages to press a button, so two runs do not repeat.
 * ----------------------------------------------------------------------- */
#define DEFUSE_CHANCE_BASE   80     /* percent, with the full countdown left */
#define DEFUSE_CHANCE_STEP    6     /* lost per second already elapsed */
#define DEFUSE_CHANCE_MIN    20     /* never quite hopeless */

static uint32_t g_rng = 0x9E3779B9u;

static uint32_t rng_next(void)
{
    uint32_t x = g_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    g_rng = x;
    return x;
}

/* 1 = this attempt succeeds */
static unsigned char rng_roll(unsigned char percent)
{
    g_rng ^= (HAL_GetTick() ^ (uint32_t)SysTick->VAL);
    if (g_rng == 0u) {
        g_rng = 0x9E3779B9u;            /* xorshift must never reach zero */
    }
    return (unsigned char)((rng_next() % 100u) < (uint32_t)percent);
}

static unsigned char defuse_chance(int sec_left)
{
    int c = DEFUSE_CHANCE_BASE - (BOMB_SECONDS - sec_left) * DEFUSE_CHANCE_STEP;

    if (c < DEFUSE_CHANCE_MIN) {
        c = DEFUSE_CHANCE_MIN;
    }
    if (c > 99) {
        c = 99;
    }
    return (unsigned char)c;
}

/* 0 = not pressed, 1 = defused, 2 = the gamble failed */
static unsigned char key2_try(uint32_t el, uint32_t total)
{
    int sec_left;
    unsigned char chance;

    if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) != GPIO_PIN_SET) {
        return 0u;
    }
    if (el >= total) {
        return 0u;                      /* already gone */
    }

    sec_left = (int)((total - el + 999u) / 1000u);
    chance   = defuse_chance(sec_left);

    return (rng_roll(chance) != 0u) ? 1u : 2u;
}

/* Generic two-line message, used for "you cut the wrong wire". */
static void ui_message(const char *big, const char *small)
{
    OLED_Clear();
    OLED_Rect(0u, 0u, 127u, 63u, 1u);

    ui_center(14u, big, 16u, 1u);

    if (small != NULL) {
        OLED_HLine(40u, 87u, 44u, 1u);
        ui_center(50u, small, 8u, 1u);
    }

    OLED_Refresh();
}

/*
 * One full play.
 *
 * The beat is the original one: the horn chirps and the servo takes ten steps,
 * then `gap` shrinks by 12% so both the beeps and the sweep speed up together.
 * Only the *total* length is pinned to a wall clock, so the countdown really
 * ends at ten seconds no matter how long a step or a redraw takes.
 */
static void run_bomb(void)
{
    const uint32_t total_ms = (uint32_t)BOMB_SECONDS * 1000u;
    uint32_t t0, t_ui, el;
    int gap = BOMB_GAP_START;
    int dir = 1;
    int pos = 80;
    uint8_t defused = 0;
    uint8_t failed  = 0;

    ui_countdown((unsigned int)total_ms);   /* first frame: 10 s straight away */

    t0   = HAL_GetTick();
    t_ui = t0;

    for (;;)
    {
        el = HAL_GetTick() - t0;
        if (el >= total_ms) {
            break;                      /* out of time */
        }

        /* 1. chirp + all three LEDs, 50 ms, same as before */
        HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_RESET);
        HAL_Delay(50);
        HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_SET);

        /* KEY2 may be hit during the chirp - do not miss it */
        {
            unsigned char hit = key2_try(HAL_GetTick() - t0, total_ms);

            if (hit == 1u) { defused = 1; break; }
            if (hit == 2u) { failed  = 1; break; }
        }

        /* 2. ten servo steps - the sweep keeps pace with the beep */
        {
            int step_delay = gap / 10;
            int step;

            if (step_delay < 15) {
                step_delay = 15;        /* give the servo time to move */
            }

            for (step = 0; step < 10; step++)
            {
                pos += dir * 14;
                if (pos > 220) { pos = 220; }
                if (pos < 80)  { pos = 80;  }
                __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, (uint32_t)pos);
                HAL_Delay((uint32_t)step_delay);

                /* refresh from inside the sweep so the beat is not stretched */
                if ((uint32_t)(HAL_GetTick() - t_ui) >= BOMB_UI_MS)
                {
                    uint32_t now = HAL_GetTick();

                    t_ui = now;
                    el   = now - t0;
                    if (el < total_ms) {
                        ui_countdown((unsigned int)(total_ms - el));
                    }
                }

                {
                    unsigned char hit = key2_try(HAL_GetTick() - t0, total_ms);

                    if (hit == 1u) { defused = 1; break; }
                    if (hit == 2u) { failed  = 1; break; }
                }
            }
        }

        if ((defused != 0) || (failed != 0)) {
            break;
        }

        dir = -dir;
        gap = (gap * 88) / 100;         /* the accelerating beat */
    }

    if (defused != 0)
    {
        /* defused: two short beeps */
        ui_defused();
        for (int i = 0; i < 2; i++)
        {
            HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_RESET);
            HAL_Delay(80);
            HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_SET);
            HAL_Delay(80);
        }
    }
    else
    {
        if (failed != 0)
        {
            /* pressed too late: say so before the bang */
            ui_message("FAILED", "BAD LUCK");
            for (int i = 0; i < 2; i++)
            {
                HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_RESET);
                HAL_Delay(120);
                HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_SET);
                HAL_Delay(140);
            }
            HAL_Delay(350);
        }

        /* detonation: servo freezes mid-stroke, horn holds 1.5 s, LEDs solid */
        HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_RESET);

        for (int i = 0; i < 10; i++)
        {
            ui_boom((unsigned char)(i & 1));
            HAL_Delay(130);
        }

        HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_SET);
    }

    HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_SET);

    HAL_Delay(1200);
    ui_idle();
}

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
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  /* TIM4 was only configured, never started: without this the CCR writes
     never reach PB6 and the servo stays dead. */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 80u);   /* park at the start angle */

  OLED_Init();
  ui_idle();

  if (OLED_ProbeOK() == 0)
  {
      /* Nothing answered on the bus, so the panel stays dark: blink all three
         LEDs (active low) instead of failing silently. */
      for (int k = 0; k < 6; k++)
      {
          HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_RESET);
          HAL_Delay(120);
          HAL_GPIO_WritePin(GPIOA, LED1_Pin | LED2_Pin | LED3_Pin, GPIO_PIN_SET);
          HAL_Delay(120);
      }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET)
      {
          HAL_Delay(20);                    /* debounce */
          if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET)
          {
              run_bomb();
          }
      }
      HAL_Delay(10);
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

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 719;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1999;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 150;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12|GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED1_Pin LED2_Pin LED3_Pin */
  GPIO_InitStruct.Pin = LED1_Pin|LED2_Pin|LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY2_Pin */
  GPIO_InitStruct.Pin = KEY2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY1_Pin */
  GPIO_InitStruct.Pin = KEY1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BEEP_Pin PB12 PB13 */
  GPIO_InitStruct.Pin = BEEP_Pin|GPIO_PIN_12|GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
