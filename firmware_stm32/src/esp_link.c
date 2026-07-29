#include "esp_link.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

ESP_Link_TypeDef g_esp_link;

static char rx_buffer[RX_BUFFER_SIZE];
static uint8_t rx_index = 0;
static uint8_t rx_byte = 0;

/* Initialize the communication link and enable interrupts */
void ESP_Link_Init(void) {
    memset(&g_esp_link, 0, sizeof(g_esp_link));
    g_esp_link.power_level = 50; // Default power level
    rx_index = 0;

    /* Start the non-blocking interrupt-driven reception of the first byte */
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}

/* Interrupt Service Routine (ISR) Callback - called automatically on every byte received */
void ESP_Link_RxISR(UART_HandleTypeDef *huart) {
    if (huart->Instance == UART4) {
        /* Check boundary to prevent buffer overflow */
        if (rx_index < (RX_BUFFER_SIZE - 1)) {
            /* Start of packet reached - reset index just in case */
            if (rx_byte == '$') {
                rx_index = 0;
            }

            rx_buffer[rx_index++] = (char)rx_byte;

            /* End of packet reached */
            if (rx_byte == ';') {
                rx_buffer[rx_index] = '\0'; // Null-terminate string
                g_esp_link.packet_ready = 1;
            }
        } else {
            rx_index = 0; // Overflow safety reset
        }

        /* Re-arm interrupt for the next incoming byte */
        HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
    }
}

/* Non-blocking main loop parser */
void ESP_Link_Process(void) {
    if (!g_esp_link.packet_ready) {
        return; // No new packet, exit instantly
    }

    /* Format expected: $SCAN,1; or $POWER,50; */
    if (strncmp(rx_buffer, "$SCAN,", 6) == 0) {
        g_esp_link.scan_active = (uint8_t)atoi(&rx_buffer[6]);
    } 
    else if (strncmp(rx_buffer, "$POWER,", 7) == 0) {
        uint32_t val = (uint32_t)atoi(&rx_buffer[7]);
        if (val <= 100) {
            g_esp_link.power_level = val;
            /* TODO: Update your TIM1 PWM duty cycle registers here based on power level */
        }
    }

    /* Clear flag and buffer for the next transaction */
    g_esp_link.packet_ready = 0;
    rx_index = 0;
}
