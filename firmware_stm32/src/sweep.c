#include "sweep.h"
#include "hydrophone.h"
#include "esp_link.h"

static uint8_t current_power_percent = 0;
extern TIM_HandleTypeDef htim1;
extern volatile uint32_t device_state;
#define STATE_ALARM 1

SweepSettings sweep_config = {50000, 25000, 100, 15};
SweepState current_sweep_state = SWEEP_IDLE;

static uint32_t current_scan_freq = 0;
static uint32_t best_freq = 0;
static float32_t max_cavitation_rms = 0.0f;
static uint32_t last_step_tick = 0;

// Пошаговый неблокирующий автомат сканирования. Вызывается в while(1)
void sweep_step_machine_process(void) {
    if (current_sweep_state != SWEEP_IN_PROGRESS) return;

    // Инициализация при старте сканирования
    if (current_scan_freq == 0) {
        current_scan_freq = sweep_config.freq_start;
        max_cavitation_rms = 0.0f;
        best_freq = sweep_config.freq_start;
        last_step_tick = HAL_GetTick();
        return;
    }

    // Проверка аварии силовой части [11:15]
    if (device_state == STATE_ALARM) {
        current_sweep_state = SWEEP_IDLE;
        current_scan_freq = 0;
        return;
    }

    // Проверяем, прошло ли время шага (замена блокирующего HAL_Delay)
    if (HAL_GetTick() - last_step_tick >= sweep_config.sweep_delay_ms) {

        // Измеряем уровень кавитации из готового DMA-буфера гидрофона [11:34]
        if (dma_ready_flag) {
            float32_t current_rms = hydrophone_process_pipeline();

            // Асинхронно отправляем точку графика в ESP32
            esp_link_send_packet("$DATA,%lu,%.4f;\r\n", current_scan_freq, current_rms);

            // Поиск пика резонанса
            if (current_rms > max_cavitation_rms) {
                max_cavitation_rms = current_rms;
                best_freq = current_scan_freq;
            }

            // Переходим к следующей меньшей частоте (сканирование сверху вниз) [11:34]
            if (current_scan_freq > sweep_config.freq_end) {
                current_scan_freq -= sweep_config.freq_step;

                // Применяем частоту на ШИМ и перестраиваем Notch-фильтр [11:34, 18:02]
                sweep_set_pwm_frequency(current_scan_freq);
                hydrophone_update_notch((float32_t)current_scan_freq);

                last_step_tick = HAL_GetTick();
            } else {
                // Сканирование успешно завершено! Фиксируем резонанс
                sweep_set_pwm_frequency(best_freq);
                hydrophone_update_notch((float32_t)best_freq);

                current_sweep_state = SWEEP_COMPLETED;
                current_scan_freq = 0; // Сброс для следующего запуска

                // Уведомляем ESP32 о завершении
                esp_link_send_packet("$RES,%lu;\r\n", best_freq);
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

    // Расчет CCR с сохранением текущей уставки мощности
    uint32_t ccr_value = (arr_value * current_power_percent) / 200;
    TIM1->CCR1 = ccr_value;
    TIM1->CCR2 = ccr_value;
}

void sweep_set_pwm_duty(uint8_t power_percent) {
    current_power_percent = power_percent;

    // Получаем текущий период таймера из регистра ARR
    uint32_t current_arr = TIM1->ARR;

    // Пересчитываем 0-100% от энкодера в коэффициент заполнения для противофазного ШИМ
    uint32_t ccr_value = (current_arr * power_percent) / 200;

    // Аппаратная защита: заполнение одного плеча не должно превышать 50% периода
    if (ccr_value > (current_arr / 2)) {
        ccr_value = current_arr / 2;
    }

    // Применяем значения в регистры сравнения каналов силовой части
    TIM1->CCR1 = ccr_value;
    TIM1->CCR2 = ccr_value;
}
