#ifndef __ESP_LINK_H
#define __ESP_LINK_H

#include "stm32f4xx_hal.h"

// Делаем хэндл UART доступным для инициализации в main.c
extern UART_HandleTypeDef huart1;

// Прототипы функций
void esp_link_init(void);
void esp_link_parse_command(char* cmd);
void esp_link_send_packet(const char* format, ...);
#endif /* __ESP_LINK_H */
