#ifndef __STM32_UART_H
#define __STM32_UART_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

extern AsyncWebSocket ws;
String uart_rx_buffer = "";

void send_command_to_stm32(String cmd) {
    Serial2.print(cmd);
}

// Parsing low-level asynchronous responses (STM32)
void process_stm32_uart() {
    while (Serial2.available()) { // Reminder: This is the line to the STM32 (USART1)
        char c = Serial2.read();
        
        // === ADD A LINE FOR HARDWARE TEST ===
        Serial.print(c); // We simply duplicate every byte received in the computer's USB!
        // =======================================    
        static String inputBuffer = "";
        
        if (c == ';') { // End of package
            inputBuffer.trim();
            
            // 1. Processing the scan simulator packet: "$LIVE,32500,0.8540"
            if (inputBuffer.startsWith("$LIVE,")) {
                int firstComma = inputBuffer.indexOf(',');
                int secondComma = inputBuffer.indexOf(',', firstComma + 1);
                
                if (firstComma != -1 && secondComma != -1) {
                    // Extracting "live" frequency and RMS from the STM32 simulator
                    current_freq = inputBuffer.substring(firstComma + 1, secondComma).toFloat();
                    current_rms = inputBuffer.substring(secondComma + 1).toFloat();
                    
                    // INSTANTLY broadcast the point to the Web to bring the chart to life!
                    String json = "{\"status\":\"SCANNING\",\"freq\":" + String(current_freq) + 
                                  ",\"pwr\":" + String(current_power) + ",\"target_pwr\":" + String(target_power) + 
                                  ",\"temp\":" + String(current_temperature) + ",\"rms\":" + String(current_rms) + 
                                  ",\"timer\":\"SCANNING\",\"scan_freq\":" + String(current_freq) + 
                                  ",\"scan_rms\":" + String(current_rms) + "}";
                    ws.textAll(json);
                }
            }
            
            // 2. Processing the echo acknowledgement packet (ECHO) for output to the PC console
            else if (inputBuffer.startsWith("ECHO:")) {
                Serial.print("[UART RX от STM32]: ");
                Serial.println(inputBuffer);
                
                // If power confirmation has arrived, pull Istwert to Solllwert
                if (inputBuffer.indexOf("PWR_SET_TO_") != -1) {
                    current_power = target_power; // We simulate that the power section has worked out the settings
                }
            }
            
            // 3. Processing the system state packet: "$SYS,ARR,STATE,DELAY"
            else if (inputBuffer.startsWith("$SYS,")) {
                // There will be an initialization analysis here in the future.
                stm32_status = "READY";
            }
            
            inputBuffer = ""; // Clearing the packet buffer
        } 
        else if (c != '\r' && c != '\n') {
            inputBuffer += c; // Copy a byte into a string
        }
    }
}


#endif