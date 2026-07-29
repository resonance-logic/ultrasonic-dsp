#ifndef __SWEEP_H
#define __SWEEP_H

#include "stm32f4xx_hal.h"

// Структура конфигурации сканирования
typedef struct {
    uint32_t freq_start;      // Начальная (верхняя) частота, Гц (например, 50000)
    uint32_t freq_end;        // Конечная (нижняя) частота, Гц (например, 25000)
    uint32_t freq_step;       // Шаг уменьшения частоты, Гц (например, 100)
    uint32_t sweep_delay_ms;  // Задержка на каждом шаге для стабилизации контура, мс
} SweepSettings;

// Перечисление состояний процесса сканирования
typedef enum {
    SWEEP_IDLE = 0,
    SWEEP_IN_PROGRESS,
    SWEEP_COMPLETED
} SweepState;

// Делаем структуру доступной для изменения из других модулей (например, из парсера UART)
extern SweepSettings sweep_config;
extern SweepState current_sweep_state;

// Прототипы функций
void sweep_init(void);
void sweep_set_pwm_frequency(uint32_t freq);
void sweep_set_pwm_duty(uint8_t power_percent);  //прототипы для управления заполнением ШИМ (Duty)
void sweep_step_machine_process(void);          //прототипы для шагового автомата

#endif /* __SWEEP_H */
