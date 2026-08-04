#include "hydrophone.h"
#include <string.h>
#include <math.h>

#define NUM_STAGES  3  // Stage 0: f0 Notch, Stage 1: 2*f0 Notch, Stage 2: 45kHz High-Pass

// CMSIS-DSP structures (Fixed typo in instance type name)
static arm_biquad_casd_df1_inst_f32 notch_filter_inst;

// State buffer requires 4 * NUM_STAGES elements
static float32_t notch_state[4 * NUM_STAGES];

// Coeffs buffer requires 5 * NUM_STAGES elements Layout: [b0, b1, b2, a1, a2,  b0, b1, b2, a1, a2]
static float32_t biquad_coeffs[5 * NUM_STAGES];

// Allocate memory for double buffers (Size = BUFFER_SIZE * 2)
uint16_t adc2_raw_buffer[BUFFER_SIZE * 2];
volatile uint8_t dma_ready_flag = 0;
uint16_t *volatile dma_buffer_pointer = NULL;

static float32_t dsp_input[BUFFER_SIZE];
static float32_t dsp_output[BUFFER_SIZE];

// Dynamic recalculation of notch filter coefficients for active PWM frequency
void hydrophone_update_notch(float32_t f_pwm) {
    float32_t f_s = 200000.0f;     // Sampling frequency 200 kHz
    float32_t bandwidth = 250.0f;  // 250 Hz cut band width

    // --- STAGE 0: Fundamental Frequency (f0) ---
    float32_t omega0 = 2.0f * (float32_t)M_PI * f_pwm / f_s;
    float32_t alpha0 = sinf(2.0f * (float32_t)M_PI * bandwidth / f_s) / 2.0f;
    float32_t cos_w0 = cosf(omega0);
    float32_t a0_0 = 1.0f + alpha0;

    biquad_coeffs[0] = 1.0f / a0_0;               // Stage 0: b0
    biquad_coeffs[1] = (-2.0f * cos_w0) / a0_0;     // Stage 0: b1
    biquad_coeffs[2] = 1.0f / a0_0;               // Stage 0: b2
    biquad_coeffs[3] = (-2.0f * cos_w0) / a0_0;    // Stage 0: a1
    biquad_coeffs[4] = (1.0f - alpha0) / a0_0;     // Stage 0: a2

    // --- STAGE 1: Second Harmonic (2 * f0) ---
    float32_t f_harmonic2 = f_pwm * 2.0f;
    
    // Safety check: if 2*f0 approaches the Nyquist frequency (100 kHz), bypass or cap it
    if (f_harmonic2 >= 95000.0f) {
        f_harmonic2 = 95000.0f; 
    }

    float32_t omega1 = 2.0f * (float32_t)M_PI * f_harmonic2 / f_s;
    float32_t alpha1 = sinf(2.0f * (float32_t)M_PI * (bandwidth * 1.5f) / f_s) / 2.0f; // Slightly wider for harmonic
    float32_t cos_w1 = cosf(omega1);
    float32_t a0_1 = 1.0f + alpha1;

    biquad_coeffs[5] = 1.0f / a0_1;               // Stage 1: b0
    biquad_coeffs[6] = (-2.0f * cos_w1) / a0_1;     // Stage 1: b1
    biquad_coeffs[7] = 1.0f / a0_1;               // Stage 1: b2
    biquad_coeffs[8] = (-2.0f * cos_w1) / a0_1;    // Stage 1: a1
    biquad_coeffs[9] = (1.0f - alpha1) / a0_1;     // Stage 1: a2

    // --- STAGE 2: 45 kHz Second-Order High-Pass Filter (Standard Audio EQ Layout) ---
    float32_t f_cut = 45000.0f; // 45 kHz Cutoff
    float32_t h_omega = 2.0f * (float32_t)M_PI * f_cut / f_s;
    float32_t h_cos = cosf(h_omega);
    
    // Q factor = 0.7071f (Butterworth response for maximally flat passband)
    float32_t h_alpha = sinf(h_omega) / (2.0f * 0.7071f); 
    float32_t h_a0 = 1.0f + h_alpha;

    // Map High-Pass Coefficients to biquad slots 10 to 14
    biquad_coeffs[10] = ((1.0f + h_cos) / 2.0f) / h_a0;       // b0
    biquad_coeffs[11] = (-(1.0f + h_cos)) / h_a0;             // b1
    biquad_coeffs[12] = ((1.0f + h_cos) / 2.0f) / h_a0;       // b2
    biquad_coeffs[13] = (-2.0f * h_cos) / h_a0;               // a1 (Subtracted inside CMSIS)
    biquad_coeffs[14] = (1.0f - h_alpha) / h_a0;              // a2 (Subtracted inside CMSIS)

    // Clear history memory state to eliminate transition pops
    memset(notch_state, 0, sizeof(notch_state));
    
    // Re-initialize the expanded 3-stage hardware pipeline descriptor
    arm_biquad_cascade_df1_init_f32(&notch_filter_inst, NUM_STAGES, biquad_coeffs, notch_state);
}

void hydrophone_init_filters(float32_t f_pwm) {
    hydrophone_update_notch(f_pwm);
}

// Processing pipeline: DC extraction -> Notch filter -> RMS evaluation
float32_t hydrophone_process_pipeline(void) {
    float32_t rms_value = 0.0f;

    // Use atomic capture to prevent immediate DMA updates mid-pass
    if (dma_ready_flag && dma_buffer_pointer != NULL) {
        uint16_t *current_processing_ptr = dma_buffer_pointer;
        dma_ready_flag = 0; // Clear flag immediately

        // 1. Fast signal centering (DC Offset 1.65V centering)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            // Normalize raw 12-bit ADC data (0..4095) to float scope (-1.0f to +1.0f)
            dsp_input[i] = ((float32_t)current_processing_ptr[i] - 2048.0f) / 2048.0f;
        }

        // 2. Vector filtering of the primary fundamental driving tone
        arm_biquad_cascade_df1_f32(&notch_filter_inst, dsp_input, dsp_output, BUFFER_SIZE);

        // 3. Calculation of white cavitation noise envelope
        arm_rms_f32(dsp_output, BUFFER_SIZE, &rms_value);
    }
    return rms_value;
}

/* 
 * FIXED DMA HANDLING CORES:
 * When Half-Complete fires, the hardware is filling the first half ([0]),
 * so our main loop thread must safely process the second half ([BUFFER_SIZE]).
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
    if(hadc->Instance == ADC2) {
        dma_buffer_pointer = &adc2_raw_buffer[BUFFER_SIZE]; // Read back-buffer safe zone
        dma_ready_flag = 1;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if(hadc->Instance == ADC2) {
        dma_buffer_pointer = &adc2_raw_buffer[0];           // Read front-buffer safe zone
        dma_ready_flag = 1;
    }
}
