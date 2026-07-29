#ifndef __CONFIG_H
#define __CONFIG_H

#include <Arduino.h>

// Wi-Fi Settings
const char* ssid = "2735_Tst12";
const char* password = "P@$$w0rd";

// Encoder pins
// replace the pins with ones guaranteed to be free of hardware buses and pull-ups
#define ENCODER_CLK_PIN  25  // Moved from pin 22
#define ENCODER_DT_PIN   26  // Moved from pin 23
#define ENCODER_SW_PIN   27  // Moved from pin 21

// Ultrasonic settings
extern uint32_t freq_nominal;
extern uint32_t freq_start;
extern uint32_t freq_end;
extern uint32_t freq_step;

// Current values
extern uint8_t current_power; 
extern uint8_t target_power;      
extern uint32_t current_freq;   
extern float current_rms;         
extern float current_temperature;
extern String stm32_status;    

// Work timer
extern uint32_t work_timer_limit_sec; 
extern uint32_t work_start_time_ms;
extern bool is_working;

// Allocating memory for variables
uint32_t freq_nominal = 35000;
uint32_t freq_start = 50000;
uint32_t freq_end = 25000;
uint32_t freq_step = 100;
uint8_t current_power = 0;       
uint8_t target_power = 0; 
uint32_t current_freq = 35000;   
float current_rms = 0.0;         
float current_temperature = 20.0;
String stm32_status = "IDLE";    
uint32_t work_timer_limit_sec = 300; 
uint32_t work_start_time_ms = 0;
bool is_working = false;
bool is_web_updating = false;
#endif