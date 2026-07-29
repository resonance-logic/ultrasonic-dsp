#include "hydrophone.h"

// Выделяем память под буферы и переменные DSP строго здесь
uint16_t adc2_raw_buffer[BUFFER_SIZE * 2];
volatile uint8_t dma_ready_flag = 0;
uint16_t *dma_buffer_pointer = NULL;

static float32_t dsp_input[BUFFER_SIZE];
static float32_t dsp_output[BUFFER_SIZE];

// Структуры CMSIS-DSP
static arm_biquad_casd_df1_inst_f32 notch_filter_inst;
static float32_t notch_state[4];
static float32_t biquad_coeffs[5];

// Динамический пересчет коэффициентов режекторного фильтра под частоту ШИМ
void hydrophone_update_notch(float32_t f_pwm) {
    float32_t f_s = 200000.0f;     // Частота дискретизации 200 кГц
    float32_t bandwidth = 200.0f;  // Полоса вырезания 200 Гц

    float32_t omega = 2.0f * PI * f_pwm / f_s;
    float32_t omega_bw = 2.0f * PI * bandwidth / f_s;
    float32_t alpha = sinf(omega_bw) / 2.0f;
    float32_t cos_w = cosf(omega);

    float32_t a0 = 1.0f + alpha;
    biquad_coeffs[0] = 1.0f / a0;               // b0
    biquad_coeffs[1] = (-2.0f * cos_w) / a0;     // b1
    biquad_coeffs[2] = 1.0f / a0;               // b2
    biquad_coeffs[3] = (2.0f * cos_w) / a0;      // -a1 (в CMSIS-DSP знак инвертирован)
    biquad_coeffs[4] = -(1.0f - alpha) / a0;     // -a2

    // Обнуляем состояние во избежание щелчков и выбросов при смене частоты
    memset(notch_state, 0, sizeof(notch_state));
    arm_biquad_cascade_df1_init_f32(&notch_filter_inst, 1, biquad_coeffs, notch_state);
}

void hydrophone_init_filters(float32_t f_pwm) {
    hydrophone_update_notch(f_pwm);
}

// Конвейер обработки: убираем постоянку -> Notch-фильтр -> Расчет RMS огибающей
float32_t hydrophone_process_pipeline(void) {
    float32_t rms_value = 0.0f;

    if (dma_ready_flag && dma_buffer_pointer != NULL) {
        // 1. Быстрое центрирование сигнала (DC Offset 1.65V от аппаратного делителя)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            dsp_input[i] = ((float32_t)dma_buffer_pointer[i] - 2048.0f) / 2048.0f;
        }

        // 2. Векторная фильтрация частоты ШИМ (вырезаем наводку силовой части)
        arm_biquad_cascade_df1_f32(&notch_filter_inst, dsp_input, dsp_output, BUFFER_SIZE);

        // 3. Вычисление RMS (огибающая белого кавитационного шума трубчатого МСП)
        arm_rms_f32(dsp_output, BUFFER_SIZE, &rms_value);

        dma_ready_flag = 0; // Сбрасываем флаг готовности для следующей половины буфера
    }
    return rms_value;
}

// Перенаправляем коллбеки прерываний DMA из HAL в наш модуль
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
