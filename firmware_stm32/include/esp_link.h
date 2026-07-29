#ifndef __ESP_LINK_H
#define __ESP_LINK_H

#include "main.h"

#define RX_BUFFER_SIZE 128

/* Structure to hold current system states received from ESP32 */
typedef struct {
    uint8_t  scan_active;    /* 1 = Scan in progress, 0 = Idle */
    uint32_t power_level;    /* Current target power (0 to 100%) */
    uint8_t  packet_ready;   /* Flag indicating a complete packet is ready to parse */
} ESP_Link_TypeDef;

/* Exported global variables */
extern ESP_Link_TypeDef g_esp_link;
extern UART_HandleTypeDef huart4;

/* Exported functions */
void ESP_Link_Init(void);
void ESP_Link_Process(void);
void ESP_Link_RxISR(UART_HandleTypeDef *huart);

#endif /* __ESP_LINK_H */
