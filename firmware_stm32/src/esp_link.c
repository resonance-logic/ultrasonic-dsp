#include "esp_link.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

ESP_Link_TypeDef g_esp_link;

static char rx_buffer[RX_BUFFER_SIZE];
// static uint8_t rx_index = 0;
static uint8_t rx_byte = 0;
/* Global variables shared with main.c */
uint32_t freq_nominal = 32500;
uint32_t freq_start = 30000;
uint32_t freq_end = 35000;
uint32_t freq_step = 10;

// ADD THIS LINE HERE TO SOLVE THE COMPILER ERROR:
extern TIM_HandleTypeDef htim1; 

// Assuming rx_index is declared as an external global in your file
extern volatile uint16_t rx_index; 

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

void ESP_Link_Process(void) {
    // 1. GATEKEEPER: Exit instantly if the UART Ring Buffer hasn't framed a full string yet
    if (!g_esp_link.packet_ready) {
        return; 
    }

    char *packet_ptr = (char*)rx_buffer;
    g_esp_link.last_rx_time = HAL_GetTick(); // Any complete packet serves as a hardware heartbeat

    // 2. PARSE CONFIGURATION COMMANDS
    if (strstr(packet_ptr, "$SET,SWP") != NULL) {
        // Parse profile payload format: "$SET,SWP,freq_start,freq_end,freq_step;"
        uint32_t tmp_start = 0, tmp_end = 0, tmp_step = 0;
        if (sscanf(packet_ptr, "$SET,SWP,%lu,%lu,%lu", &tmp_start, &tmp_end, &tmp_step) >= 3) {
            freq_start = tmp_start;
            freq_end = tmp_end;
            freq_step = tmp_step;
        }
        g_esp_link.scan_active = 1; // Automatically start safe sweep loop
    } 
    else if (strstr(packet_ptr, "$SET,PWR,0;") != NULL || strstr(packet_ptr, "$STOP;") != NULL || strstr(packet_ptr, "$SCAN,0") != NULL) {
        // Immediate system-wide safety stop requests
        g_esp_link.scan_active = 0;
    }
    else if (strstr(packet_ptr, "$SCAN,1") != NULL) {
        // Direct scan toggle commands
        g_esp_link.scan_active = 1;
    }
    else if (strstr(packet_ptr, "$SET,PWR") != NULL) {
        // Parse power level value format: "$SET,PWR,val;"
        uint32_t val = 0;
        if (sscanf(packet_ptr, "$SET,PWR,%lu", &val) == 1) {
            if (val <= 100) {
                g_esp_link.power_level = val;
                // If scan is not running, we change duty cycle directly. 
                // If it is running, our state machine handles the power envelope dynamically.
                if (!g_esp_link.scan_active) {
                    uint32_t current_arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
                    uint32_t new_ccr = (current_arr * val) / 200; // Map 0-100% to 0-50% Duty Cycle limit
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, new_ccr);
                }
            }
        }
    }
    else if (strstr(packet_ptr, "$SET,TMP") != NULL) {
        // Handle temperature setup variables if needed (Heartbeat accounted for above)
    }

    // 3. CLEANUP AND RESET FOR NEXT FRAME TRANSACTION
    g_esp_link.packet_ready = 0;
    rx_index = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer)); // Wipe buffer safely to prevent string remnants
}
