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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h> // Для работы функции abs()
#include "arm_math.h"
#include "hydrophone.h"
#include "sweep.h"
#include "esp_link.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifndef TIM_CHANNEL_1
#define TIM_CHANNEL_1 0x00000000U // Канал 1 ШИМ
#endif
#ifndef TIM_CHANNEL_2
#define TIM_CHANNEL_2 0x00000004U // Канал 2 ШИМ
#endif
// Состояния автомата перегрузки
#define STATE_SCAN   0U
#define STATE_READY  1U
#define STATE_ALARM  2U
// Настройки для MOD-ACS712-20A с делителем 1к/2к
// Виртуальный ноль АЦП (~1.66 В на пине WeAct при 0 Ампер). Подстроите после теста с ЛБП!
#define ACS712_ZERO_LEVEL   1987U
#define CURRENT_ALARM_STEP  500U  // Отклонение от нуля более чем на ~500 единиц АЦП (~4.5 Ампера)

#define BUFFER_SIZE 512 // Размер полубуфера для обработки (размер окна RMS)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2; // <-- ДОБАВИТЬ СТРОКУ
UART_HandleTypeDef huart4; // Заодно объявляем наш UART4

//UART_HandleTypeDef huart4;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

// Пробное объявление структуры режекторного фильтра для проверки линковки
arm_biquad_casd_df1_inst_f32 test_notch_structure;

arm_biquad_casd_df1_inst_f32 notch_filter_inst;
float32_t notch_state[4]; // Состояние фильтра (2 каскада/задержки)
float32_t biquad_coeffs[5]; // Массив коэффициентов: {b0, b1, b2, -a1, -a2}

/* Переменные для автоматического поиска акустического резонанса */
uint32_t best_resonance_freq = 0; // Сюда запишется лучшая частота кавитации
uint32_t max_noise_amplitude = 0; // Пиковый уровень шума
// Переменные для рабочего цикла контроля
uint32_t raw_current = 0;
uint32_t raw_noise_voltage = 0;
int32_t noise_delta = 0;
int32_t current_delta = 0;        // Реальное отклонение тока от нуля ACS

uint8_t device_state = STATE_SCAN; // Стартуем всегда с режима сканирования


float32_t dsp_input[BUFFER_SIZE];
float32_t dsp_output[BUFFER_SIZE];

char rx_byte;
char rx_buffer[64];
uint8_t rx_index = 0;

// Переменная для тайминга отправки живой телеметрии в ESP32
uint32_t last_live_telemetry_tick = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_UART4_Init(void);
/* USER CODE BEGIN PFP */
void MX_TIM2_Init(void);
void MX_DMA_Init(void);         // Добавить прототип
void MX_USART1_UART_Init(void); // Добавить прототип
void Set_US_Frequency_And_Power(uint32_t freq_hz, uint32_t power_percent);
void Scan_Magnetostrictive_Resonance(void);
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
	// Проверка включения FPU на уровне ядра Cortex-M4
	// Регистр CPACR (Copsocessor Access Control Register) должен разрешать полный доступ к CP10 и CP11
	if ((SCB->CPACR & (0xF << 20)) == 0) {
	    // Если мы здесь, FPU выключен на уровне железа!
	    // Обычно HAL_Init() включает его сам через SystemInit(),
	    // но если этого не произошло, принудительно активируем:
	    SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));
	}
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
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_UART4_Init();
  /* USER CODE BEGIN 2 */
  // Включаем оба АЦП в циклическом режиме
  HAL_ADC_Start(&hadc1);
  HAL_ADC_Start(&hadc2);

  // Запуск прямого (PA8) и инверсного комплементарного (PB13) ШИМ под IR2108
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

  hydrophone_init_filters(50000.0f); // Стартуем фильтр на 50 кГц
  HAL_ADC_Start_DMA(&hadc2, (uint32_t*)adc2_raw_buffer, BUFFER_SIZE * 2);
  HAL_TIM_Base_Start(&htim2);
  // Запуск посимвольного приема пакетов от ESP32 через прерывания USART1
//  esp_link_init();
  // Вызываем автоматический поиск резонанса на 100% мощности
  Scan_Magnetostrictive_Resonance();

  // Проверяем: если во время сканирования функция зафиксировала аварию по току
  if (device_state == STATE_ALARM) {

      // Сюда мы позже допишем отправку аварийного сообщения в ESP32 по UART:
      // Send_UART_Message("STATUS:ALARM_OVERCURRENT");

      while(1) {
          // Программный тупик инициализации. Мост гарантированно выключен.
          // Отсюда плата выйдет только при физическом сбросе или перезапуске питания.
      }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	    // === ТЕСТ «ПРЯМОЙ ТОК»: ШЛЕМ ДАННЫЕ В ОБХОД ВСЕХ ПРЕРЫВАНИЙ И ФЛАГОВ ===
	    // === МАТЕМАТИЧЕСКИ ИСПРАВЛЕННЫЙ ТЕСТ «ПРЯМОЙ ТОК» (БЕЗ ПРЕРЫВАНИЙ) ===
	    // === 100% НАДЕЖНЫЙ ЦЕЛОЧИСЛЕННЫЙ СИМУЛЯТОР СПЕКТРА (БЕЗ FLOAT И EXPF) ===
	    // === ТЕСТ «ЖЕЛЕЗНАЯ ПЕТЛЯ»: ОТПРАВЛЯЕМ И ТУТ ЖЕ ПРИНИМАЕМ ИЗ ПРОВОДА ===
	    // === ТЕСТ «ЧИСТАЯ МЕДЬ» НА ПИНАХ PC10/PC11 ===
	    static uint32_t test_f1 = 50000;
	    static uint32_t last_send_tick = 0;
	    static uint8_t received_byte = 0;
	    static uint8_t echo_success_flag __attribute__((unused)) = 0; 

	    if (HAL_GetTick() - last_send_tick >= 50)
	    {
	        uint32_t simulated_rms_x10000 = 100;
	        if (test_f1 >= 31000 && test_f1 <= 34000) {
	            uint32_t distance = (test_f1 > 32500) ? (test_f1 - 32500) : (32500 - test_f1);
	            if (distance < 1500) simulated_rms_x10000 = 9500 - (distance * 6);
	        }

	        uint32_t rms_int = simulated_rms_x10000 / 10000;
	        uint32_t rms_frac = simulated_rms_x10000 % 10000;

	        static char test_packet_str[64];
	        snprintf(test_packet_str, sizeof(test_packet_str), "$LIVE,%lu,%lu.%04lu;\r\n",
	                 test_f1, rms_int, rms_frac);

	        // Очищаем RDR буфер приема
	        HAL_UART_Receive(&huart4, &received_byte, 1, 0);

	        // Отправляем в ножку PC10
	        HAL_UART_Transmit(&huart4, (uint8_t*)test_packet_str, strlen(test_packet_str), 10);

	        // Читаем из ножки PC11
	        if (HAL_UART_Receive(&huart4, &received_byte, 1, 5) == HAL_OK) {
	            if (received_byte == '$' || received_byte == 36) {
	                echo_success_flag = 1; // ЖЕЛЕЗНЫЙ УСПЕХ НА БОКОВОЙ ГРЕБЕНКЕ!
	            }
	        }

	        if (test_f1 > 25000) test_f1 -= 200; else test_f1 = 50000;
	        last_send_tick = HAL_GetTick();
	    }



	    // end test

    // ====================================================================
    // 1. АППАРАТНАЯ МГНОВЕННАЯ ЗАЩИТА ПО ТОКУ (АВТОНОМНЫЙ АВТОМАТ СОСТОЯНИЙ)
    // ====================================================================
    // Этот блок работает всегда на максимальной скорости и защищает IGBT-мост [11:15]
    if (device_state != STATE_ALARM)
    {
        // Считываем текущую дельту тока с аналогового входа PA0 (ADC1) [11:15]
        // (Используйте вашу исходную переменную вместо 'current_delta')
        if (current_delta > CURRENT_ALARM_STEP)
        {
            // Мгновенно рубим аппаратный противофазный ШИМ на пинах PA8/PA9 [18:02]
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);

            // Фиксируем статус аварии (автомат состояний STATE_ALARM) [11:15]
            device_state = STATE_ALARM;

            // Асинхронно оповещаем ESP32, чтобы Web-интерфейс сразу окрасился в красный
            esp_link_send_packet("$ALARM,CURRENT_OVERLOAD;\r\n");
        }
    }

    // ====================================================================
    // 2. НЕБЛОКИРУЮЩИЙ АВТОМАТ СКАНИРОВАНИЯ ЧАСТОТЫ «СВЕРХУ ВНИЗ»
    // ====================================================================
    // Если от ESP32 пришла команда на сканирование, этот автомат начинает
    // шагать по частотам, не зависая внутри циклов и не используя HAL_Delay
    if (device_state != STATE_ALARM)
    {
        sweep_step_machine_process();
    }
/// begin for test STM32-ESP32
    // === ПОЛНОСТЬЮ АВТОНОМНЫЙ ТЕСТ СВЯЗИ И ГРАФИКА (СИМУЛЯТОР РЕЗОНАНСА МСП) ===
    // Объявляем статические переменные прямо внутри while(1), чтобы они сохраняли значения
    // === БЕЗОПАСНЫЙ НЕБЛОКИРУЮЩИЙ СИМУЛЯТОР РЕЗОНАНСА МСП ===
    static uint32_t test_f = 50000;
    static uint8_t test_running = 0;
    static uint32_t last_test_step_tick = 0; // Неблокирующий таймер шага

    // Перехватываем команду старта сканирования из буфера esp_link
    if (current_sweep_state == SWEEP_IN_PROGRESS && test_running == 0) {
        test_f = sweep_config.freq_start;
        test_running = 1;
        last_test_step_tick = HAL_GetTick();
    }

    // Запускаем шаг симуляции строго раз в 20 мс, не останавливая процессор!
    if (test_running && (HAL_GetTick() - last_test_step_tick >= 20))
    {
        // Математическая модель колокола кавитационного резонанса
        float center_freq = 32500.0f;
        float bandwidth = 1500.0f;
        float deviation = ((float)test_f - center_freq) / bandwidth;

        float fake_noise = (float)(rand() % 100) / 2000.0f;
        float test_rms = expf(-0.5f * deviation * deviation) + fake_noise;

        // 1. Быстрый целочисленный вывод в printf (без зависаний)
        uint32_t rms_integral = (uint32_t)test_rms;
        uint32_t rms_fractional = (uint32_t)((test_rms - rms_integral) * 10000);
        printf("FREQ:%lu,RMS:%lu.%04lu\r\n", test_f, rms_integral, rms_fractional);

        // 2. Отправка пакета в ESP32 по нашему новому USART1 (PA9/PA10)
        esp_link_send_packet("$LIVE,%lu,%.4f;\r\n", test_f, test_rms);

        // Переходим к следующей частоте "сверху вниз" [11:34]
        if (test_f > sweep_config.freq_end && test_f > sweep_config.freq_step) {
            test_f -= sweep_config.freq_step;
        } else {
            test_running = 0;
            current_sweep_state = 0; // Возвращаем автомат в IDLE

            // Сигнализируем ESP32 об успешном окончании сканирования
            esp_link_send_packet("DONE:RES_FREQ:32500.0,MAX_RMS:0.9500\r\n");
        }

        // Обновляем метку времени для следующего шага через 20 мс
        last_test_step_tick = HAL_GetTick();
    }

/// end for test STM32-ESP32
    // ====================================================================
    // 3. ФОНОВАЯ ОБРАБОТКА ГИДРОФОНА И ЖИВАЯ ТЕЛЕМЕТРИЯ ДЛЯ WEB-ИНТЕРФЕЙСА
    // ====================================================================
    // Если сканирование завершено или еще не запущено, STM32 продолжает
    // непрерывно очищать сигнал от силовой частоты и считать RMS кавитации [11:34]
    if (current_sweep_state == SWEEP_IDLE && device_state != STATE_ALARM)
    {
        // Проверяем аппаратный флаг готовности Ping-Pong буфера DMA2 [11:34]
        if (dma_ready_flag)
        {
            // Прогоняем данные через CMSIS-DSP Notch-фильтр и RMS-детектор [11:34]
            float32_t live_cavitation_noise = hydrophone_process_pipeline();

            // Чтобы не спамить в UART и не грузить ESP32 Web-сервер,
            // отправляем текущий уровень шума кавитации ровно раз в 250 мс
            if (HAL_GetTick() - last_live_telemetry_tick >= 250)
            {
                esp_link_send_packet("$LIVE,%.4f;\r\n", live_cavitation_noise);
                last_live_telemetry_tick = HAL_GetTick();
            }
        }
    }

    /* USER CODE END WHILE */

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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.ScanConvMode = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

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
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
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
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
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
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// ИСПРАВЛЕННАЯ И БЕЗОПАСНАЯ функция изменения частоты и мощности
void Set_US_Frequency_And_Power(uint32_t freq_hz, uint32_t power_percent) {
    // Жесткие рамки по частоте (25-50 кГц) [11:15]
    if (freq_hz < 25000 || freq_hz > 50000) return;
    if (power_percent > 100) power_percent = 100;

    // В режиме Center-Aligned частота равна: F_pwm = F_tim / (2 * ARR)
    // Следовательно, для 168 МГц: ARR = 168 000 000 / (2 * freq_hz) = 84 000 000 / freq_hz
    uint32_t new_period = 84000000 / freq_hz;

    // 2. В режиме выравнивания по центру максимальный ультразвуковой меандр (50% заполнения)
    // достигается, когда регистр сравнения CCR равен ровно половине периода ARR.
    uint32_t max_safe_pulse = new_period / 2;

    // 3. Линейно масштабируем импульс от 0% до максимальных безопасных 50% заполнения
    uint32_t new_pulse = (max_safe_pulse * power_percent) / 100;

    // 4. Защита "снизу": если импульс слишком короткий (меньше аппаратного Dead-Time),
    // принудительно гасим ШИМ в 0, чтобы затворы не грелись наносекундными иголками.
    if (new_pulse < 150) {
        new_pulse = 0;
    }

    // 5. Атомарно обновляем регистры таймера TIM1
    __HAL_TIM_SET_AUTORELOAD(&htim1, new_period);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, new_pulse);
}


// Финальная функция автоматического поиска резонанса (ничего не возвращает, меняет device_state)
void Scan_Magnetostrictive_Resonance(void) {
    max_noise_amplitude = 0;
    best_resonance_freq = 0;
    device_state = STATE_SCAN; // Выставляем статус: идет сканирование

    // Запуск прямого ШИМ для верхнего плеча (Пин PA8)
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    // Запуск инверсного ШИМ для нижнего плеча (Пин PB13)
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

    for (uint32_t freq = 25000; freq <= 50000; freq += 100) {

        // Если авария уже взведена из другого места — мгновенно прерываем поиск
        if (device_state == STATE_ALARM) return;

        Set_US_Frequency_And_Power(freq, 100);
        HAL_Delay(15); // Время на раскачку механики МСП

        // Быстрая проверка защиты во время сканирования под ACS712
        int32_t scan_current_delta = abs((int32_t)HAL_ADC_GetValue(&hadc1) - ACS712_ZERO_LEVEL);
        if (scan_current_delta > CURRENT_ALARM_STEP) {
        	// Стоп прямого ШИМ для верхнего плеча (Пин PA8)
        	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
        	// Стоп инверсного ШИМ для нижнего плеча (Пин PB13)
        	HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
            device_state = STATE_ALARM; // Переключаем автомат в режим АВАРИИ
            return; // Досрочно выходим из функции сканирования
        }

        // Накопление 16 замеров гладкой огибающей с детектора TL072
        uint32_t noise_sum = 0;
        for (int i = 0; i < 16; i++) {
            noise_sum += HAL_ADC_GetValue(&hadc2);
            HAL_Delay(1);
        }
        uint32_t average_noise_delta = noise_sum / 16;

        // Фиксируем пик акустического отклика кавитации
        if (average_noise_delta > max_noise_amplitude) {
            max_noise_amplitude = average_noise_delta;
            best_resonance_freq = freq;
        }
    }

    // Если всё сканирование успешно завершилось и аварии по току не произошло
    if (device_state != STATE_ALARM) {
        if (best_resonance_freq >= 25000 && best_resonance_freq <= 50000) {
            Set_US_Frequency_And_Power(best_resonance_freq, 100);
            device_state = STATE_READY; // Переходим в рабочий режим постоянной генерации!
        } else {
            // Если кавитация вообще не найдена во всем диапазоне — выключаем мост от греха подальше
        	// Стоп прямого ШИМ для верхнего плеча (Пин PA8)
        	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
        	// Стоп инверсного ШИМ для нижнего плеча (Пин PB13)
        	HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
            device_state = STATE_READY;
        }
    }
}

void MX_DMA_Init(void)
{
  /* Включаем тактирование контроллера DMA2 */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* Настройка приоритетов прерываний DMA2 для корректной работы с ADC2 */
  /* Поток 2 (Stream 2) отвечает за сбор данных с ADC2 */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0); // Высокий приоритет для DSP-буфера
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 900 - 1; // 180 МГц / 900 = 200 кГц дискретизации гидрофона
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim2);

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig);

  /* === ЗАМЕНА НА ПРЯМУЮ НАСТРОЙКУ РЕГИСТРОВ === */
  // Устанавливаем биты MMS (Master Mode Selection) в значение 010 (Update event)
  // Это заставит TIM2 выдавать триггер TRGO для ADC2 на каждом обновлении счетчика
  TIM2->CR2 &= ~TIM_CR2_MMS; // Сброс бит
  TIM2->CR2 |= TIM_CR2_MMS_1; // Выбор события Update (MMS = 0x20)
}


// Динамический расчет коэффициентов режекторного фильтра
void update_notch_coefficients(float32_t f_pwm, float32_t f_s, float32_t bandwidth) {
    float32_t omega = 2.0f * PI * f_pwm / f_s;
    float32_t omega_bw = 2.0f * PI * bandwidth / f_s;
    float32_t alpha = sinf(omega_bw) / 2.0f;
    float32_t cos_w = cosf(omega);

    float32_t a0 = 1.0f + alpha;
    biquad_coeffs[0] = 1.0f / a0;              // b0
    biquad_coeffs[1] = (-2.0f * cos_w) / a0;    // b1
    biquad_coeffs[2] = 1.0f / a0;              // b2
    biquad_coeffs[3] = (2.0f * cos_w) / a0;     // -a1 (CMSIS использует инвертированные знаки для 'a')
    biquad_coeffs[4] = -(1.0f - alpha) / a0;    // -a2

    // Сброс состояния фильтра во избежание переходных процессов при смене частоты
    arm_biquad_cascade_df1_init_f32(&notch_filter_inst, 1, biquad_coeffs, notch_state);
}


// Вызывается в основном суперцикле background-процесса
float32_t process_hydrophone_data(void) {
    float32_t rms_value = 0.0f;

    if (dma_ready_flag) {
        // 1. Преобразование типов и центрирование сигнала (убираем DC Offset 1.65В ~ 2048 отсчетов)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            dsp_input[i] = ((float32_t)dma_buffer_pointer[i] - 2048.0f) / 2048.0f;
        }

        // 2. Фильтрация основной частоты ШИМ
        arm_biquad_cascade_df1_f32(&notch_filter_inst, dsp_input, dsp_output, BUFFER_SIZE);

        // 3. Вычисление RMS белого кавитационного шума (огибающая)
        arm_rms_f32(dsp_output, BUFFER_SIZE, &rms_value);

        dma_ready_flag = 0;
    }
    return rms_value; // Возвращаем текущий уровень кавитации
}

void set_pwm_frequency(uint32_t freq) {
    // В Center-Aligned режиме: ARR = F_clk / (2 * F_pwm)
    uint32_t arr_value = 180000000 / (2 * freq);
    TIM1->ARR = arr_value;

    // Пересчет скважности (50% заполнение с учетом Dead-Time)
    TIM1->CCR1 = arr_value / 2;
    TIM1->CCR2 = arr_value / 2;
}

void run_frequency_sweep(void) {
    uint32_t best_freq = sweep_config.freq_start;
    float32_t max_cavitation_rms = 0.0f;

    for (uint32_t current_f = sweep_config.freq_start; current_f >= sweep_config.freq_end; current_f -= sweep_config.freq_step) {

        // 1. Адаптивно перестраиваем режекторный фильтр под новую частоту ШИМ
        update_notch_coefficients((float32_t)current_f, 200000.0f, 200.0f);

        // 2. Устанавливаем генерацию на МСП
        set_pwm_frequency(current_f);

        // Даем время на переходные процессы в контуре
        HAL_Delay(sweep_config.sweep_delay_ms);

        // 3. Измеряем уровень кавитации (пропускаем несколько буферов для стабильности)
        float32_t current_rms = 0;
        for(int j=0; j<4; j++) {
            while(!dma_ready_flag);
            current_rms += process_hydrophone_data();
        }
        current_rms /= 4.0f;

        // 4. Логируем данные для отправки на ESP32 (график)
        // Быстрая и легкая альтернатива без использования _printf_float
        uint32_t rms_integral = (uint32_t)current_rms;                             // Целая часть
        uint32_t rms_fractional = (uint32_t)((current_rms - rms_integral) * 10000); // 4 знака дроби

        // Выводим через обычные целые числа %lu. Паддинг %04lu сохранит ведущие нули (например, 0.0045)
        printf("FREQ:%lu,RMS:%lu.%04lu\r\n", current_f, rms_integral, rms_fractional);

        // 5. Поиск максимума (механический резонанс кавитации)
        if (current_rms > max_cavitation_rms) {
            max_cavitation_rms = current_rms;
            best_freq = current_f;
        }

        // Аварийный выход, если сработал автомат токовой защиты (задача из текущего статуса)
        if (device_state == STATE_ALARM) break;
    }

    // Фиксация на рабочей частоте пика резонанса
    set_pwm_frequency(best_freq);
    update_notch_coefficients((float32_t)best_freq, 200000.0f, 200.0f);
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

#ifdef  USE_FULL_ASSERT
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
