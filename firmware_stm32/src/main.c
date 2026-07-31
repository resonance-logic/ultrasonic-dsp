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
#include <stdlib.h> // For the abs() function to work
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
#define TIM_CHANNEL_1 0x00000000U // Channel 1 PWM
#endif
#ifndef TIM_CHANNEL_2
#define TIM_CHANNEL_2 0x00000004U // Channel 2 PWM
#endif
// Overload circuit breaker states
#define STATE_SCAN   0U
#define STATE_READY  1U
#define STATE_ALARM  2U
// Settings for MOD-ACS712-20A with 1k/2k divider
// Virtual zero of the ADC (~1.66 V on the WeAct pin at 0 Ampere). Adjust after the LBP test!
#define ACS712_ZERO_LEVEL   1987U
#define CURRENT_ALARM_STEP  500U  // Deviation from zero by more than ~500 ADC units (~4.5 Amperes)

#define BUFFER_SIZE 512 // Processing half-buffer size (RMS window size)

#define ESP_LINK_TIMEOUT_MS   1500  // Communication timeout: 1.5 seconds
#define SWEEP_STEP_PERIOD_MS  10    // Frequency change step every 10 ms

typedef enum {
    SWEEP_STATE_IDLE = 0,
    SWEEP_STATE_INIT,
    SWEEP_STATE_RUNNING,
    SWEEP_STATE_STOPPING
} SweepState_t;

// External structures from your modules
extern ESP_Link_TypeDef g_esp_link; 
extern TIM_HandleTypeDef htim1; // Our power timer
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2; // <-- ADD ROW
UART_HandleTypeDef huart4; // At the same time we announce our UART4

// UART_HandleTypeDef huart4;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

// Test declaration of a notch filter structure to test linking
arm_biquad_casd_df1_inst_f32 test_notch_structure;

arm_biquad_casd_df1_inst_f32 notch_filter_inst;
float32_t notch_state[4]; // Filter state (2 stages/delay)
float32_t biquad_coeffs[5]; // Coefficients array: {b0, b1, b2, -a1, -a2}

/* Variables for automatic search for acoustic resonance */
uint32_t best_resonance_freq = 0; // The best cavitation frequency will be written here
uint32_t max_noise_amplitude = 0; // Peak noise level
// Variables for control duty cycle
uint32_t raw_current = 0;
uint32_t raw_noise_voltage = 0;
int32_t noise_delta = 0;
int32_t current_delta = 0;        // Real current deviation from zero ACS

uint8_t device_state = STATE_SCAN; // We always start from the scanning mode


float32_t dsp_input[BUFFER_SIZE];
float32_t dsp_output[BUFFER_SIZE];

char rx_byte;
char rx_buffer[64];
uint8_t rx_index = 0;

// Variable for timing the sending of live telemetry to the ESP32
uint32_t last_live_telemetry_tick = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM1_Init(void);
static void MX_UART4_Init(void);
void Process_Ultrasonic_Sweep(void);
/* USER CODE BEGIN PFP */
void MX_TIM2_Init(void);
void MX_DMA_Init(void);         // Add a prototype
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
	// Checking FPU enablement at the Cortex-M4 core level
	// The CPACR (Copsocessor Access Control Register) register must allow full access to CP10 and CP11
	if ((SCB->CPACR & (0xF << 20)) == 0) {
	    // If we are here, the FPU is turned off at the hardware level!
	    // Usually HAL_Init() enables it itself via SystemInit(),
	    // but if this does not happen, forcefully activate:
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
  MX_UART4_Init();
  /* USER CODE BEGIN 2 */
  // We turn on both ADCs in cyclic mode
  HAL_ADC_Start(&hadc1);
  HAL_ADC_Start(&hadc2);

  // Running direct (PA8) and inverse complementary (PB13) PWM under IR2108
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

  hydrophone_init_filters(50000.0f); // Starting the filter at 50 kHz
  HAL_ADC_Start_DMA(&hadc2, (uint32_t*)adc2_raw_buffer, BUFFER_SIZE * 2);
  HAL_TIM_Base_Start(&htim2);
  // Starting character-by-character packet reception from ESP32 via USART1 interrupts
  ESP_Link_Init();
  // We call an automatic search for resonance at 100% power
  Scan_Magnetostrictive_Resonance();

  // We check: if during scanning the function recorded a current fault
  if (device_state == STATE_ALARM) {

      // Here we will later add sending an emergency message to the ESP32 via UART:
      // Send_UART_Message("STATUS:ALARM_OVERCURRENT");

      while(1) {
          // Software initialization deadlock. The bridge is guaranteed to be off.
          // From here the board will only come out with a physical reset or power restart.
      }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
   /* USER CODE BEGIN WHILE */
  while (1)
  {
      /* 1. Handle Core Generator Sweep, Safety Watchdog, and DSP Calculations */
      Process_Ultrasonic_Sweep();

      /* 2. Process incoming commands from ESP32 via Ring Buffer (Non-blocking) */
      ESP_Link_Process();

      /* ==================================================================
       * 3. EMERGENCY CURRENT PROTECTION (HARDWARE SAFETY CHECK)
       * ==================================================================
       * Evaluated instantly on every single CPU cycle for maximum safety.
       * Uses raw ADC units from your MOD-ACS712-20A (1k/2k divider configuration).
       */
            
      uint32_t current_adc_reading = abs((int32_t)HAL_ADC_GetValue(&hadc1) );
      uint32_t deviation = 0;

      // Calculate absolute deviation from the virtual zero level
      if (current_adc_reading > ACS712_ZERO_LEVEL) {
          deviation = current_adc_reading - ACS712_ZERO_LEVEL;
      } else {
          deviation = ACS712_ZERO_LEVEL - current_adc_reading;
      }

      // Check if current exceeds the maximum safety envelope threshold
      if (deviation > CURRENT_ALARM_STEP) {
          HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);     // Kill power stage hardware instantly
          #ifdef TIM_CHANNELN_ENABLE
          HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);   // Cut complementary channel if used
          #endif
          __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0); // Force zero duty cycle registers

          g_esp_link.scan_active = 0;                  // Revoke scan request globally
          
          // Send an emergency alert sequence packet to notify the ESP32 Web UI
          char estop_str[] = "$ESTOP,OVERCURRENT;\r\n";
          HAL_UART_Transmit(&huart4, (uint8_t*)estop_str, strlen(estop_str), 10);
          
          continue;                                    // Skip further loop execution
      }

      /* USER CODE END WHILE */

      /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}


void Process_Ultrasonic_Sweep(void) {
    static SweepState_t current_state = SWEEP_STATE_IDLE;
    static uint32_t last_sweep_tick = 0;
    static uint32_t last_telemetry_tick = 0;
    static uint32_t current_frequency = 30000; // Track frequency here safely
    
    uint32_t current_tick = HAL_GetTick();
    
    // 1. WATCHDOG TIMEOUT CHECK
    if (current_state == SWEEP_STATE_RUNNING) {
        if ((current_tick - g_esp_link.last_rx_time) > ESP_LINK_TIMEOUT_MS) {
            current_state = SWEEP_STATE_STOPPING;
        }
    }

    uint8_t is_scan_requested = g_esp_link.scan_active; 

    // 2. STATE MACHINE STATE CONTROLS
    switch (current_state) {
        case SWEEP_STATE_IDLE:
            if (is_scan_requested) {
                current_state = SWEEP_STATE_INIT;
            } else {
                /* Send a flatline idle telemetry packet once per second */
                if (current_tick - last_telemetry_tick >= 1000) {
                    char idle_str[32];
                    snprintf(idle_str, sizeof(idle_str), "$LIVE,0,0.0000;\r\n");
                    HAL_UART_Transmit(&huart4, (uint8_t*)idle_str, strlen(idle_str), 10);
                    last_telemetry_tick = current_tick;
                }
            }
            break;

        case SWEEP_STATE_INIT:
            current_frequency = 30000; // Reset to start point
            HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
            
            last_sweep_tick = current_tick;
            last_telemetry_tick = current_tick;
            current_state = SWEEP_STATE_RUNNING;
            break;

        case SWEEP_STATE_RUNNING:
            if (!is_scan_requested) {
                current_state = SWEEP_STATE_STOPPING;
                break;
            }

            /* Step frequency sweep logic (Every 10ms) */
            static float cavitation_noise_rms = 0.0f; // Make it static or keep it local
            
            if ((current_tick - last_sweep_tick) >= 10) {
                last_sweep_tick = current_tick;
                
                // update_tim1_pwm_frequency(current_frequency);
                // start_hydrophone_adc_sampling();
                
                cavitation_noise_rms = 0.0f; // This will update with actual DSP math later
                // arm_biquad_cascade_df1_f32(&S_notch, adc_buffer_f32, filtered_buffer_f32, BLOCK_SIZE);
                // arm_rms_f32(filtered_buffer_f32, BLOCK_SIZE, &cavitation_noise_rms);

                /* Frequency Increment Control */
                current_frequency += 50; 
                if (current_frequency > 35000) {
                    current_frequency = 30000; 
                }
            }

            /* Global Telemetry dispatch (Every 50ms) */
            if ((current_tick - last_telemetry_tick) >= 50) {
                char telemetry_str[64];
                
                // We use cavitation_noise_rms here to fix the compiler warning!
                snprintf(telemetry_str, sizeof(telemetry_str), "$LIVE,%lu,%.4f;\r\n", 
                         current_frequency, cavitation_noise_rms);
                HAL_UART_Transmit(&huart4, (uint8_t*)telemetry_str, strlen(telemetry_str), 10);
                
                last_telemetry_tick = current_tick;
            }
            break;

        case SWEEP_STATE_STOPPING:
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            
            g_esp_link.scan_active = 0; 
            current_state = SWEEP_STATE_IDLE;
            break;

        default:
            current_state = SWEEP_STATE_STOPPING;
            break;
    }
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

// FIXED AND SAFE frequency and power change function
void Set_US_Frequency_And_Power(uint32_t freq_hz, uint32_t power_percent) {
    // Tight frequency limits (25-50 kHz) [11:15]
    if (freq_hz < 25000 || freq_hz > 50000) return;
    if (power_percent > 100) power_percent = 100;

    // In Center-Aligned mode the frequency is: F_pwm = F_tim / (2 * ARR)
    // Therefore, for 168 MHz: ARR = 168,000,000 / (2 * freq_hz) = 84,000,000 / freq_hz
    uint32_t new_period = 84000000 / freq_hz;

    // 2. In center alignment mode, maximum ultrasonic square wave (50% fill)
    // is reached when the comparison register CCR is equal to exactly half the ARR period.
    uint32_t max_safe_pulse = new_period / 2;

    // 3. Linearly scale the pulse from 0% to the maximum safe 50% filling
    uint32_t new_pulse = (max_safe_pulse * power_percent) / 100;

    // 4. Protection "from below": if the pulse is too short (less than hardware Dead-Time),
    // We forcefully turn off the PWM to 0 so that the gates do not heat up with nanosecond needles.
    if (new_pulse < 150) {
        new_pulse = 0;
    }

    // 5. Atomically update the TIM1 timer registers
    __HAL_TIM_SET_AUTORELOAD(&htim1, new_period);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, new_pulse);
}


// The final function of automatic resonance search (returns nothing, changes device_state)
void Scan_Magnetostrictive_Resonance(void) {
    max_noise_amplitude = 0;
    best_resonance_freq = 0;
    device_state = STATE_SCAN; // Set the status: scanning in progress

    // Trigger direct PWM for the upper side (Pin PA8)
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    // Starting inverse PWM for the lower leg (Pin PB13)
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

    for (uint32_t freq = 25000; freq <= 50000; freq += 100) {

        // If the emergency has already been triggered from another location, we immediately interrupt the search
        if (device_state == STATE_ALARM) return;

        Set_US_Frequency_And_Power(freq, 100);
        HAL_Delay(15); // Time to pump up the SME mechanics

        // Quick security check during scanning under ACS712
        int32_t scan_current_delta = abs((int32_t)HAL_ADC_GetValue(&hadc1) - ACS712_ZERO_LEVEL);
        if (scan_current_delta > CURRENT_ALARM_STEP) {
        	// Stop direct PWM for the upper arm (Pin PA8)
        	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
        	// Stop inverse PWM for the lower leg (Pin PB13)
        	HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
            device_state = STATE_ALARM; // Switch the machine to EMERGENCY mode
            return; // Exiting the scanning function early
        }

        // Accumulation of 16 smooth envelope measurements from the TL072 detector
        uint32_t noise_sum = 0;
        for (int i = 0; i < 16; i++) {
            noise_sum += HAL_ADC_GetValue(&hadc2);
            HAL_Delay(1);
        }
        uint32_t average_noise_delta = noise_sum / 16;

        // We record the peak of the acoustic response of cavitation
        if (average_noise_delta > max_noise_amplitude) {
            max_noise_amplitude = average_noise_delta;
            best_resonance_freq = freq;
        }
    }

    // If all scanning was completed successfully and no current fault occurred
    if (device_state != STATE_ALARM) {
        if (best_resonance_freq >= 25000 && best_resonance_freq <= 50000) {
            Set_US_Frequency_And_Power(best_resonance_freq, 100);
            device_state = STATE_READY; // Let's switch to continuous generation operating mode!
        } else {
            // If cavitation is not found at all in the entire range, turn off the bridge out of harm’s way
        	// Stop direct PWM for the upper arm (Pin PA8)
        	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
        	// Stop inverse PWM for the lower leg (Pin PB13)
        	HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
            device_state = STATE_READY;
        }
    }
}

void MX_DMA_Init(void)
{
  /* Enable DMA2 controller clocking */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* Setting DMA2 interrupt priorities for correct operation with ADC2 */
  /* Stream 2 is responsible for collecting data from ADC2 */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0); // High priority for DSP buffer
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 900 - 1; // 180 MHz / 900 = 200 kHz hydrophone sampling
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim2);

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig);

  /* === ЗАМЕНА НА ПРЯМУЮ НАСТРОЙКУ РЕГИСТРОВ === */
  // Set the MMS (Master Mode Selection) bits to 010 (Update event)
  // This will cause TIM2 to issue a TRGO trigger to ADC2 on every counter update
  TIM2->CR2 &= ~TIM_CR2_MMS; // Reset bits
  TIM2->CR2 |= TIM_CR2_MMS_1; // Select Event Update (MMS = 0x20)
}


// Dynamic calculation of notch filter coefficients
void update_notch_coefficients(float32_t f_pwm, float32_t f_s, float32_t bandwidth) {
    float32_t omega = 2.0f * PI * f_pwm / f_s;
    float32_t omega_bw = 2.0f * PI * bandwidth / f_s;
    float32_t alpha = sinf(omega_bw) / 2.0f;
    float32_t cos_w = cosf(omega);

    float32_t a0 = 1.0f + alpha;
    biquad_coeffs[0] = 1.0f / a0;              // b0
    biquad_coeffs[1] = (-2.0f * cos_w) / a0;    // b1
    biquad_coeffs[2] = 1.0f / a0;              // b2
    biquad_coeffs[3] = (2.0f * cos_w) / a0;     // -a1 (CMSIS uses inverted characters for 'a')
    biquad_coeffs[4] = -(1.0f - alpha) / a0;    // -a2

    // Resetting the filter state to avoid transients when changing frequency
    arm_biquad_cascade_df1_init_f32(&notch_filter_inst, 1, biquad_coeffs, notch_state);
}


// Called in the main superloop of the background process
float32_t process_hydrophone_data(void) {
    float32_t rms_value = 0.0f;

    if (dma_ready_flag) {
        // 1. Type conversion and signal centering (remove DC Offset 1.65V ~ 2048 counts)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            dsp_input[i] = ((float32_t)dma_buffer_pointer[i] - 2048.0f) / 2048.0f;
        }

        // 2. Filtering the main PWM frequency
        arm_biquad_cascade_df1_f32(&notch_filter_inst, dsp_input, dsp_output, BUFFER_SIZE);

        // 3. Calculation of RMS white cavitation noise (envelope)
        arm_rms_f32(dsp_output, BUFFER_SIZE, &rms_value);

        dma_ready_flag = 0;
    }
    return rms_value; // Returning the current cavitation level
}

void set_pwm_frequency(uint32_t freq) {
    // In Center-Aligned mode: ARR = F_clk / (2 * F_pwm)
    uint32_t arr_value = 180000000 / (2 * freq);
    TIM1->ARR = arr_value;

    // Recalculation of duty cycle (50% filling taking into account Dead-Time)
    TIM1->CCR1 = arr_value / 2;
    TIM1->CCR2 = arr_value / 2;
}

void run_frequency_sweep(void) {
    uint32_t best_freq = sweep_config.freq_start;
    float32_t max_cavitation_rms = 0.0f;

    for (uint32_t current_f = sweep_config.freq_start; current_f >= sweep_config.freq_end; current_f -= sweep_config.freq_step) {

        // 1. Adaptively rebuild the notch filter to the new PWM frequency
        update_notch_coefficients((float32_t)current_f, 200000.0f, 200.0f);

        // 2. Install generation at SMEs
        set_pwm_frequency(current_f);

        // We give time for transient processes in the circuit
        HAL_Delay(sweep_config.sweep_delay_ms);

        // 3. We measure the level of cavitation (we skip several buffers for stability)
        float32_t current_rms = 0;
        for(int j=0; j<4; j++) {
            while(!dma_ready_flag);
            current_rms += process_hydrophone_data();
        }
        current_rms /= 4.0f;

        // 4. Log the data to be sent to the ESP32 (graph)
        // Fast and easy alternative without using _printf_float
        uint32_t rms_integral = (uint32_t)current_rms;                             // Whole part
        uint32_t rms_fractional = (uint32_t)((current_rms - rms_integral) * 10000); // 4 fraction signs

        // We output using ordinary integers %lu. Padding %04lu will retain leading zeros (e.g. 0.0045)
        printf("FREQ:%lu,RMS:%lu.%04lu\r\n", current_f, rms_integral, rms_fractional);

        // 5. Search for maximum (mechanical resonance of cavitation)
        if (current_rms > max_cavitation_rms) {
            max_cavitation_rms = current_rms;
            best_freq = current_f;
        }

        // Emergency exit if the circuit breaker has tripped (task from the current status)
        if (device_state == STATE_ALARM) break;
    }

    // Fixation of the resonance peak at the operating frequency
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
