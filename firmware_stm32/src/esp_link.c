#include "esp_link.h"
#include "sweep.h"
#include "hydrophone.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// ИСПРАВЛЕНО: берем хэндл, который CubeMX уже создал в main.c, а не плодим копии
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart4; // ИСПРАВЛЕНО: переключаемся на четвертый порт

static uint8_t rx_byte;
static char rx_buffer[64];
static uint8_t rx_index = 0;

// Ссылка на внешнюю переменную состояния силовой части из main.c
extern volatile uint32_t device_state;

// Инициализация связи (вызывать один раз в main.c)
void esp_link_init(void) {
    memset(rx_buffer, 0, sizeof(rx_buffer));
    rx_index = 0;
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1); // ИСПРАВЛЕНО
}

// Отправка текстового пакета в ESP32
void esp_link_send_packet(const char* format, ...) {
    char tx_buf[64];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(tx_buf, sizeof(tx_buf), format, args);
    va_end(args);

    if (len > 0) {
        HAL_UART_Transmit(&huart4, (uint8_t*)tx_buf, len, 10); // ИСПРАВЛЕНО
    }
}

// Исправленный парсер входящих сообщений от ESP32
void esp_link_parse_command(char* cmd) {
    // ИСПРАВЛЕНО: ESP32 присылает команды с префиксом "$", например "$SET,PWR,45;"
    // Проверяем команду установки параметров
    if (strncmp(cmd, "$SET,PWR,", 9) == 0) {
        uint32_t pwr_val = atoi(cmd + 9);

        // Добавляем ЭХО-ответ для верификации связи в мониторе порта ESP32
        esp_link_send_packet("ECHO:PWR_SET_TO_%lu\r\n", pwr_val);

        // Вызов вашей функции изменения скважности (в sweep.c)
        sweep_set_pwm_duty((uint8_t)pwr_val);
    }
    else if (strncmp(cmd, "$SET,SWP,", 9) == 0) {
        uint32_t val1 = 0, val2 = 0, val3 = 0;
        if (sscanf(cmd + 9, "%lu,%lu,%lu", &val1, &val2, &val3) == 3) {
            sweep_config.freq_start = val1;
            sweep_config.freq_end = val2;
            sweep_config.freq_step = val3;

            esp_link_send_packet("ECHO:SWEEP_CONFIRMED:%lu->%lu,STEP:%lu\r\n", val1, val2, val3);

            // Переводим систему в режим сканирования
            current_sweep_state = SWEEP_IN_PROGRESS;
        }
    }
    else if (strncmp(cmd, "$SET,TMP,", 9) == 0) {
        uint32_t temp_val = atoi(cmd + 9);
        esp_link_send_packet("ECHO:TEMP_LIMIT_%lu_OK\r\n", temp_val);
        // Здесь можно сохранить уставку температуры в вашу структуру контроля
    }
    // ГРУППА 2: ЗАПРОСЫ СОСТОЯНИЯ (PULL ОТ ESP32)
    else if (strcmp(cmd, "$GET,SYS;") == 0 || strcmp(cmd, "$GET,SYS") == 0) {
        esp_link_send_packet("$SYS,%lu,%lu,%d;\r\n",
                             TIM1->ARR,
                             device_state,
                             sweep_config.sweep_delay_ms);
    }
}

// Аппаратный колбэк прерывания UART1
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == UART4) { // ИСПРАВЛЕНО: ловим прерывание от UART4
        if (rx_index >= 63) rx_index = 0;
        rx_buffer[rx_index++] = (char)rx_byte;

        if (rx_byte == ';') {
            rx_buffer[rx_index] = '\0';
            esp_link_parse_command(rx_buffer);
            rx_index = 0;
        }
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}
