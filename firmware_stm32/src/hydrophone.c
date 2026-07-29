#include "hydrophone.h"

// We allocate memory for buffers and DSP variables strictly here
uint16_t adc2_raw_buffer[BUFFER_SIZE * 2];
volatile uint8_t dma_ready_flag = 0;
uint16_t *dma_buffer_pointer = NULL;

static float32_t dsp_input[BUFFER_SIZE];
static float32_t dsp_output[BUFFER_SIZE];

// CMSIS-DSP structures
static arm_biquad_casd_df1_inst_f32 notch_filter_inst;
static float32_t notch_state[4];
static float32_t biquad_coeffs[5];

// Dynamic recalculation of notch filter coefficients for PWM frequency
void hydrophone_update_notch(float32_t f_pwm) {
    float32_t f_s = 200000.0f;     // Sampling frequency 200 kHz
    float32_t bandwidth = 200.0f;  // 200 Hz cut band

    float32_t omega = 2.0f * PI * f_pwm / f_s;
    float32_t omega_bw = 2.0f * PI * bandwidth / f_s;
    float32_t alpha = sinf(omega_bw) / 2.0f;
    float32_t cos_w = cosf(omega);

    float32_t a0 = 1.0f + alpha;
    biquad_coeffs[0] = 1.0f / a0;               // b0
    biquad_coeffs[1] = (-2.0f * cos_w) / a0;     // b1
    biquad_coeffs[2] = 1.0f / a0;               // b2
    biquad_coeffs[3] = (2.0f * cos_w) / a0;      // -a1 (in CMSIS-DSP the sign is inverted)
    biquad_coeffs[4] = -(1.0f - alpha) / a0;     // -a2

    // We reset the state to avoid clicks and emissions when changing frequencies
    memset(notch_state, 0, sizeof(notch_state));
    arm_biquad_cascade_df1_init_f32(&notch_filter_inst, 1, biquad_coeffs, notch_state);
}

void hydrophone_init_filters(float32_t f_pwm) {
    hydrophone_update_notch(f_pwm);
}

// Processing pipeline: remove constant -> Notch filter -> RMS envelope calculation
float32_t hydrophone_process_pipeline(void) {
    float32_t rms_value = 0.0f;

    if (dma_ready_flag && dma_buffer_pointer != NULL) {
        // 1. Fast signal centering (DC Offset 1.65V from hardware divider)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            dsp_input[i] = ((float32_t)dma_buffer_pointer[i] - 2048.0f) / 2048.0f;
        }

        // 2. Vector filtering of the PWM frequency (we cut out the pickup of the power section)
        arm_biquad_cascade_df1_f32(&notch_filter_inst, dsp_input, dsp_output, BUFFER_SIZE);

        // 3. Calculation of RMS (envelope of white cavitation noise of a tubular SME)
        arm_rms_f32(dsp_output, BUFFER_SIZE, &rms_value);

        dma_ready_flag = 0; // Reset the ready flag for the next half of the buffer
    }
    return rms_value;
}

// Redirecting DMA interrupt callbacks from HAL to our module
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
    if(hadc->Instance == ADC2) {
        dma_buffer_pointer = &adc2_raw_buffer[0];
        dma_ready_flag = 1;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if(hadc->Instance == ADC2) {
        dma_buffer_pointer = &adc2_raw_buffer[BUFFER_SIZE];
        dma_ready_flag = 1;
    }
}
