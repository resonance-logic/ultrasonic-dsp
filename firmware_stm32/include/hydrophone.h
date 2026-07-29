#ifndef __HYDROPHONE_H
#define __HYDROPHONE_H

#include "stm32f4xx_hal.h"
#include "arm_math.h"

#define BUFFER_SIZE 512 // Processing half-buffer length

// External variables so that main.c or the scan module knows the DMA status
extern uint16_t adc2_raw_buffer[BUFFER_SIZE * 2];
extern volatile uint8_t dma_ready_flag;
extern uint16_t *dma_buffer_pointer;

// Prototypes of module functions
void hydrophone_init_filters(float32_t f_pwm);
void hydrophone_update_notch(float32_t f_pwm);
float32_t hydrophone_process_pipeline(void);

#endif /* __HYDROPHONE_H */
