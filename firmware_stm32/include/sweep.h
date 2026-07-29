#ifndef __SWEEP_H
#define __SWEEP_H

#include "stm32f4xx_hal.h"

// Scan Configuration Structure
typedef struct {
    uint32_t freq_start;      // Initial (upper) frequency, Hz (for example, 50000)
    uint32_t freq_end;        // Final (lower) frequency, Hz (for example, 25000)
    uint32_t freq_step;       // Frequency reduction step, Hz (for example, 100)
    uint32_t sweep_delay_ms;  // Delay at each step to stabilize the circuit, ms
} SweepSettings;

// Enumerating scanning process states
typedef enum {
    SWEEP_IDLE = 0,
    SWEEP_IN_PROGRESS,
    SWEEP_COMPLETED
} SweepState;

// We make the structure accessible for modification from other modules (for example, from the UART parser)
extern SweepSettings sweep_config;
extern SweepState current_sweep_state;

// Function prototypes
void sweep_init(void);
void sweep_set_pwm_frequency(uint32_t freq);
void sweep_set_pwm_duty(uint8_t power_percent);  // prototypes for PWM fill control (Duty)
void sweep_step_machine_process(void);          // prototypes for a stepper machine

#endif /* __SWEEP_H */
