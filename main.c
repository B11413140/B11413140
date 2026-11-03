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
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/

/* USER CODE BEGIN 0 */

/* =========================================
   F1 REACTION TEST - NUCLEO-G474RE

   RED1   PC8   GPIO
   RED2   PC6   GPIO
   RED3   PC5   GPIO
   GREEN  PC9   TIM3_CH4 PWM
   YELLOW PA9   TIM1_CH2 PWM
   START  PC13  EXTI Rising
   REACT  PA8   EXTI Falling
   ========================================= */

/* ===== Time Settings ===== */

#define RED_INTERVAL_MS   500U
#define REACTION_LIMIT    2000U
#define DEBOUNCE_MS       30U
#define TARGET_COUNT      5U
#define PWM_DUTY          2500U

/* ===== State Definitions ===== */

typedef enum
{
    F1_IDLE,
    F1_RED1,
    F1_RED2,
    F1_WAIT_GO,
    F1_REACTION,
    F1_RESULT,
    F1_FINISHED

} F1_State;

static F1_State f1State = F1_IDLE;

/* ===== Time Variables ===== */

static uint32_t stateTime = 0;
static uint32_t goTime = 0;
static uint32_t randomDelay = 0;
static uint32_t randomSeed = 1234567U;

/* ===== Interrupt Variables ===== */

static volatile uint8_t startPending = 0;
static volatile uint8_t reactPending = 0;

static volatile uint32_t startPressTime = 0;
static volatile uint32_t reactPressTime = 0;

static volatile uint32_t lastStartIRQ = 0;
static volatile uint32_t lastReactIRQ = 0;

/* ===== Result Variables ===== */

volatile uint32_t reactionResults[5] = {0};

volatile uint32_t reactionTime = 0;
volatile uint32_t averageTime_x10 = 0;

volatile uint8_t validCount = 0;
volatile uint8_t testFinished = 0;

volatile uint8_t falseStart = 0;
volatile uint8_t timeoutFlag = 0;

/* ===== Turn Off Red LEDs ===== */

static void RedOff(void)
{
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_8 | GPIO_PIN_6 | GPIO_PIN_5,
        GPIO_PIN_RESET
    );
}

/* ===== Turn Off PWM LEDs ===== */

static void StatusOff(void)
{
    // Green LED OFF
    __HAL_TIM_SET_COMPARE(
        &htim3, TIM_CHANNEL_4, 0
    );

    // Yellow LED OFF
    __HAL_TIM_SET_COMPARE(
        &htim1, TIM_CHANNEL_2, 0
    );
}

/* ===== PWM Result Display ===== */

static void StatusPWM(uint8_t success)
{
    StatusOff();

    // Reset timer counters
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    __HAL_TIM_SET_COUNTER(&htim1, 0);

    if (success == 1)
    {
        // Green LED blinking
        __HAL_TIM_SET_COMPARE(
            &htim3,
            TIM_CHANNEL_4,
            PWM_DUTY
        );
    }
    else
    {
        // Yellow LED blinking
        __HAL_TIM_SET_COMPARE(
            &htim1,
            TIM_CHANNEL_2,
            PWM_DUTY
        );
    }
}

/* ===== Start Race ===== */
static void UART_SendText(const char *text);
static void StartRace(void)
{
    uint32_t now = HAL_GetTick();

    RedOff();
    StatusOff();

    // Random delay 1000~3000 ms
    randomSeed ^= now;

    randomSeed =
        randomSeed * 1664525U + 1013904223U;

    randomDelay =
        1000U + (randomSeed % 2001U);

    // Reset current attempt
    reactionTime = 0;
    falseStart = 0;
    timeoutFlag = 0;

    // First red LED ON
    HAL_GPIO_WritePin(
        GPIOC, GPIO_PIN_8, GPIO_PIN_SET
    );

    stateTime = now;
    f1State = F1_RED1;

    UART_SendText("\r\nRace started...\r\n");
    // 告訴電腦新的一回合已開始
}

/* ===== Show Result ===== */

static void ShowResult(uint8_t success)
{
    RedOff();

    StatusPWM(success);

    if (testFinished == 1)
    {
        f1State = F1_FINISHED;
    }
    else
    {
        f1State = F1_RESULT;
    }
}


/* ========== UART 傳送文字 ========== */

static void UART_SendText(const char *text)
{
    uint16_t length = 0;  // 記錄字串長度

    while (text[length] != '\0')
    {
        length++;  // 計算字串長度
    }

    HAL_UART_Transmit(
        &hlpuart1,       // LPUART1控制結構
        (uint8_t *)text, // 要傳送的字串
        length,          // 字串長度
        HAL_MAX_DELAY    // 等待傳送完成
    );
}

/* ========== UART 傳送整數 ========== */

static void UART_SendNumber(uint32_t number)
{
    char buffer[11];  // 最多10位數加結束字元
    uint8_t i = 0;
    uint8_t j = 0;

    if (number == 0)
    {
        UART_SendText("0");
        return;
    }

    while (number > 0)
    {
        buffer[i++] = (number % 10U) + '0';
        number /= 10U;
        // 將數字轉成字元，順序先反向儲存
    }

    // 將字元順序反轉
    for (j = 0; j < i / 2; j++)
    {
        char temp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = temp;
    }

    buffer[i] = '\0';  // 加入字串結束字元

    UART_SendText(buffer);  // 傳送整數字串
}

/* ========== UART 傳送反應秒數 ========== */

static void UART_SendSeconds(uint32_t ms)
{
    uint32_t sec = ms / 1000U; // 整數秒
    uint32_t frac = ms % 1000U; // 小數毫秒

    UART_SendNumber(sec);
    UART_SendText(".");

    // 補足三位小數
    if (frac < 100U)
        UART_SendText("0");

    if (frac < 10U)
        UART_SendText("0");

    UART_SendNumber(frac);
    UART_SendText(" s");
}

/* ========== 顯示五次有效成績與平均 ========== */

static void UART_SendFinalResult(void)
{
    UART_SendText(
        "\r\n===== FINAL RESULT =====\r\n"
    );

    for (uint8_t i = 0; i < TARGET_COUNT; i++)
    {
        UART_SendText("Test ");
        UART_SendNumber(i + 1U);
        UART_SendText(": ");

        UART_SendNumber(reactionResults[i]);
        UART_SendText(" ms\r\n");
        // 依序列印五次有效成績
    }

    UART_SendText("Average: ");

    // 平均值以0.1ms為單位
    UART_SendNumber(averageTime_x10 / 10U);
    UART_SendText(".");
    UART_SendNumber(averageTime_x10 % 10U);

    UART_SendText(" ms\r\n");

    UART_SendText(
        "========================\r\n"
    );
}

/* ===== False Start ===== */

static void FalseStart(void)
{
    falseStart = 1;    // 設定搶跑旗標
    reactionTime = 0;  // 搶跑不計入有效成績

    UART_SendText(
        "FALSE START! Not recorded.\r\n"
    ); // 傳送搶跑訊息到電腦

    ShowResult(0);  // 黃燈PWM閃爍
}

/* ===== Reaction Timeout ===== */

static void ReactionTimeout(void)
{
    timeoutFlag = 1; // 設定超時旗標

    UART_SendText(
        "TIMEOUT! Over 2000 ms.\r\n"
    ); // 傳送超時訊息到電腦

    ShowResult(0); // 黃燈PWM閃爍
}

/* ===== Save Reaction Time ===== */


static void SaveReaction(uint32_t elapsed)
{
    reactionTime = elapsed;
    // 記錄本次反應時間，單位為ms

    if (elapsed > REACTION_LIMIT)
    {
        ReactionTimeout();
        // 超過2000ms，判定失敗

        return;
    }

    if (validCount >= TARGET_COUNT)
    {
        return;
        // 已記錄5次有效成績，不再儲存
    }

    reactionResults[validCount] = elapsed;
    // 將本次反應時間寫入陣列

    validCount++;
    // 有效次數加1

    /* ===== 透過UART傳送本次成績 ===== */

    UART_SendText("Test ");

    UART_SendNumber(validCount);
    // 傳送目前第幾次測試

    UART_SendText("/5: ");

    UART_SendSeconds(elapsed);
    // 將毫秒轉換成秒數傳送

    UART_SendText(" (");

    UART_SendNumber(elapsed);
    // 傳送原始毫秒數

    UART_SendText(" ms)\r\n");

    /* ===== 計算五次平均成績 ===== */

    if (validCount == TARGET_COUNT)
    {
        uint32_t total = 0;

        for (uint8_t i = 0;
             i < TARGET_COUNT;
             i++)
        {
            total += reactionResults[i];
            // 將五次成績加總
        }

        averageTime_x10 =
            (total * 10U) / TARGET_COUNT;
        // 計算平均值，保留一位小數

        testFinished = 1;
        // 設定測試完成旗標

        UART_SendFinalResult();
        // 將五次成績及平均值傳送到電腦
    }

    ShowResult(1);
    // 有效反應，綠燈PWM閃爍
}


/* =========================================
   GPIO EXTI Interrupt Callback
   ========================================= */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    // START BUTTON PC13
    if (GPIO_Pin == GPIO_PIN_13)
    {
        if ((uint32_t)(now - lastStartIRQ)
            >= DEBOUNCE_MS)
        {
            lastStartIRQ = now;

            if (startPending == 0)
            {
                startPressTime = now;
                startPending = 1;
            }
        }
    }

    // REACTION BUTTON PA8
    else if (GPIO_Pin == GPIO_PIN_8)
    {
        if ((uint32_t)(now - lastReactIRQ)
            >= DEBOUNCE_MS)
        {
            lastReactIRQ = now;

            if (reactPending == 0)
            {
                reactPressTime = now;
                reactPending = 1;
            }
        }
    }
}

/* =========================================
   F1 System Initialization
   ========================================= */

void F1_Init(void)
{
    // Start Green PWM
    HAL_TIM_PWM_Start(
        &htim3, TIM_CHANNEL_4
    );

    // Start Yellow PWM
    HAL_TIM_PWM_Start(
        &htim1, TIM_CHANNEL_2
    );

    RedOff();
    StatusOff();

    uint32_t now = HAL_GetTick();

    lastStartIRQ = now - DEBOUNCE_MS;
    lastReactIRQ = now - DEBOUNCE_MS;

    startPending = 0;
    reactPending = 0;

    validCount = 0;
    testFinished = 0;

    f1State = F1_IDLE;

    UART_SendText(
        "\r\n===== F1 REACTION TEST =====\r\n"
    );

    UART_SendText("System Ready!\r\n");
    UART_SendText("Press START to begin.\r\n");
}

/* =========================================
   F1 Main Program
   ========================================= */

void F1_Update(void)
{
    uint32_t now = HAL_GetTick();

    uint8_t startEvent;
    uint8_t reactEvent;

    uint32_t startTime;
    uint32_t pressTime;

    // Atomically read interrupt events
    uint32_t irqState = __get_PRIMASK();

    __disable_irq();

    startEvent = startPending;
    reactEvent = reactPending;

    startTime = startPressTime;
    pressTime = reactPressTime;

    startPending = 0;
    reactPending = 0;

    __set_PRIMASK(irqState);

    /* ===== START BUTTON ===== */

    if (startEvent)
    {
        if (f1State == F1_IDLE ||
            f1State == F1_RESULT)
        {
            StartRace();
        }
        else if (f1State == F1_FINISHED)
        {
            // Clear five results
            validCount = 0;
            testFinished = 0;
            averageTime_x10 = 0;

            for (uint8_t i = 0;
                 i < TARGET_COUNT;
                 i++)
            {
                reactionResults[i] = 0;
            }

            StartRace();
        }
    }

    /* ===== REACTION BUTTON ===== */

    if (reactEvent)
    {
        if (f1State == F1_RED1 ||
            f1State == F1_RED2 ||
            f1State == F1_WAIT_GO)
        {
            // Ignore an event before this round
            if ((int32_t)(pressTime - stateTime)
                >= 0)
            {
                FalseStart();
            }
        }
        else if (f1State == F1_REACTION)
        {
            // Check if button was pressed early
            if ((int32_t)(pressTime - goTime) < 0)
            {
                FalseStart();
            }
            else
            {
                uint32_t elapsed =
                    pressTime - goTime;

                SaveReaction(elapsed);
            }
        }
    }

    /* ===== F1 STATE MACHINE ===== */

    switch (f1State)
    {
        case F1_IDLE:
            // Waiting for start button
            break;

        /* First Red LED */
        case F1_RED1:

            if ((uint32_t)(now - stateTime)
                >= RED_INTERVAL_MS)
            {
                // Turn on second red LED
                HAL_GPIO_WritePin(
                    GPIOC,
                    GPIO_PIN_6,
                    GPIO_PIN_SET
                );

                stateTime = now;
                f1State = F1_RED2;
            }
            break;

        /* Second Red LED */
        case F1_RED2:

            if ((uint32_t)(now - stateTime)
                >= RED_INTERVAL_MS)
            {
                // Turn on third red LED
                HAL_GPIO_WritePin(
                    GPIOC,
                    GPIO_PIN_5,
                    GPIO_PIN_SET
                );

                stateTime = now;
                f1State = F1_WAIT_GO;
            }
            break;

        /* All Red LEDs ON */
        case F1_WAIT_GO:

            if ((uint32_t)(now - stateTime)
                >= randomDelay)
            {
                RedOff();
                // 三顆紅燈同時熄滅

                goTime = HAL_GetTick();
                // 記錄熄燈時間，開始計時

                f1State = F1_REACTION;
                // 進入反應計時狀態

                UART_SendText("LIGHTS OUT!\r\n");
                // 傳送起跑訊息到電腦
            }
            break;

        /* Measure Reaction Time */
        case F1_REACTION:

            /*
             * Allow time for a pending EXTI
             * event to be handled.
             */
            if ((uint32_t)(now - goTime)
                > REACTION_LIMIT
                  + DEBOUNCE_MS + 5U)
            {
                reactionTime = now - goTime;

                ReactionTimeout();
            }
            break;

        /* Display Result */
        case F1_RESULT:

            // PWM handles LED blinking
            break;

        /* Five Valid Scores Completed */
        case F1_FINISHED:

            // Wait for START to reset scores
            break;

        default:
            break;
    }

    (void)startTime;
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
  MX_LPUART1_UART_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  F1_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
	    F1_Update();
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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
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
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 16999;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 4999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 16999;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 4999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, RED3_Pin|RED2_Pin|RED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : START_Pin */
  GPIO_InitStruct.Pin = START_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(START_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RED3_Pin RED2_Pin RED1_Pin */
  GPIO_InitStruct.Pin = RED3_Pin|RED2_Pin|RED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : REACT_Pin */
  GPIO_InitStruct.Pin = REACT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(REACT_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

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
