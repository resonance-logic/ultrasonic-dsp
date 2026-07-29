#include "main.h"
#include <string.h>
#include <stdio.h>
#include "sweep.h"
#include "hydrophone.h"
#include "esp_link.h"

static uint8_t current_power_percent = 0;
extern TIM_HandleTypeDef htim1;
extern volatile uint32_t device_state;
extern UART_HandleTypeDef huart4;
#define STATE_ALARM 1

SweepSettings sweep_config = {50000, 25000, 100, 15};
SweepState current_sweep_state = SWEEP_IDLE;

static uint32_t current_scan_freq = 0;
static uint32_t best_freq = 0;
static float32_t max_cavitation_rms = 0.0f;
static uint32_t last_step_tick = 0;

// Step-by-step non-blocking automatic scanning. Called in while(1)
void sweep_step_machine_process(void) {
    if (current_sweep_state != SWEEP_IN_PROGRESS) return;

    // Initialization when scanning starts
    if (current_scan_freq == 0) {
        current_scan_freq = sweep_config.freq_start;
        max_cavitation_rms = 0.0f;
        best_freq = sweep_config.freq_start;
        last_step_tick = HAL_GetTick();
        return;
    }

    // Checking the power unit failure [11:15]
    if (device_state == STATE_ALARM) {
        current_sweep_state = SWEEP_IDLE;
        current_scan_freq = 0;
        return;
    }

    // Checking whether the step time has passed (replacing the blocking HAL_Delay)
    if (HAL_GetTick() - last_step_tick >= sweep_config.sweep_delay_ms) {

        // Measuring the level of cavitation from the finished DMA hydrophone buffer [11:34]
        if (dma_ready_flag) {
            float32_t current_rms = hydrophone_process_pipeline();

            /* 1. Format the telemetry string buffer */
            char telemetry_str[64];
            snprintf(telemetry_str, sizeof(telemetry_str), "$LIVE,%lu,%.4f;\r\n", 
                     current_scan_freq, current_rms);
            
            /* 2. Direct clean transmit via UART4 hardware peripheral */
            HAL_UART_Transmit(&huart4, (uint8_t*)telemetry_str, strlen(telemetry_str), 10);


            // Finding the Resonance Peak
            if (current_rms > max_cavitation_rms) {
                max_cavitation_rms = current_rms;
                best_freq = current_scan_freq;
            }

            // Moving on to the next lower frequency (scanning from top to bottom) [11:34]
            if (current_scan_freq > sweep_config.freq_end) {
                current_scan_freq -= sweep_config.freq_step;

                // We apply the frequency to PWM and rebuild the Notch filter [11:34, 18:02]
                sweep_set_pwm_frequency(current_scan_freq);
                hydrophone_update_notch((float32_t)current_scan_freq);

                last_step_tick = HAL_GetTick();
            } else {
                // Scanning completed successfully! We fix the resonance
                sweep_set_pwm_frequency(best_freq);
                hydrophone_update_notch((float32_t)best_freq);

                current_sweep_state = SWEEP_COMPLETED;
                current_scan_freq = 0; // Reset for next run

                /* --------------------------------------------------------------
                 * REPLACEMENT: Notify ESP32 about completion ($RES,frequency;)
                 * --------------------------------------------------------------
                 */
                char res_packet_str[32];
                snprintf(res_packet_str, sizeof(res_packet_str), "$RES,%lu;\r\n", best_freq);
                HAL_UART_Transmit(&huart4, (uint8_t*)res_packet_str, strlen(res_packet_str), 10);
            }
        }
    }
}

void sweep_set_pwm_frequency(uint32_t freq) {
    if (freq == 0) return;

    uint32_t arr_value = 180000000 / (2 * freq);
    if (arr_value > 0xFFFF) arr_value = 0xFFFF;
    if (arr_value < 10) arr_value = 10;

    TIM1->ARR = arr_value;

    // Calculate CCR while maintaining current power setpoint
    uint32_t ccr_value = (arr_value * current_power_percent) / 200;
    TIM1->CCR1 = ccr_value;
    TIM1->CCR2 = ccr_value;
}

void sweep_set_pwm_duty(uint8_t power_percent) {
    current_power_percent = power_percent;

    // Getting the current timer period from the ARR register
    uint32_t current_arr = TIM1->ARR;

    // We recalculate 0-100% of the encoder into the duty cycle for anti-phase PWM
    uint32_t ccr_value = (current_arr * power_percent) / 200;

    // Hardware protection: filling one arm should not exceed 50% of the period
    if (ccr_value > (current_arr / 2)) {
        ccr_value = current_arr / 2;
    }

    // We apply the values ​​to the comparison registers of the power section channels
    TIM1->CCR1 = ccr_value;
    TIM1->CCR2 = ccr_value;
}
