#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AiEsp32RotaryEncoder.h>

void push_immediate_web_update();

#include "config.h"
#include "web_page.h"
#include "stm32_uart.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AiEsp32RotaryEncoder encoder = AiEsp32RotaryEncoder(ENCODER_CLK_PIN, ENCODER_DT_PIN, ENCODER_SW_PIN, -1, 4);

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        data[len] = '\0';
        String msg = String((char*)data);
        
        // 1. Changing the setting from the Web without applying (dragging the slider)
        if (msg.startsWith("PREVIEW_PWR:")) {
            is_web_updating = true; // block the encoder polling in the loop
            target_power = msg.substring(12).toInt();
            encoder.setEncoderValue(target_power); // Synchronizing the physical pen
            push_immediate_web_update(); // <-- ADD HERE: Sync changes from the Web          
        }
        // 2. Web power accept (mouse click/release)
        else if (msg == "APPLY_PWR") {
            send_command_to_stm32("$SET,PWR," + String(target_power) + ";");
        }
        else if (msg.startsWith("START_SWEEP")) {
            if (freq_start < freq_end) { uint32_t t = freq_start; freq_start = freq_end; freq_end = t; }
            send_command_to_stm32("$SET,SWP," + String(freq_start) + "," + String(freq_end) + "," + String(freq_step) + ";");
            is_working = true;
            work_start_time_ms = millis();
            stm32_status = "SCANNING";
        }
        else if (msg.startsWith("STOP_GEN")) {
            target_power = 0;
            current_power = 0;
            encoder.setEncoderValue(0);
            send_command_to_stm32("$SET,PWR,0;");
            is_working = false;
            stm32_status = "STOPPED";
        }
        else if (msg.startsWith("SET_CONFIG:")) {
            float target_temp = 8.0;
            sscanf(msg.c_str(), "SET_CONFIG:%lu,%lu,%lu,%lu,%lu,%f", &freq_nominal, &freq_start, &freq_end, &freq_step, &work_timer_limit_sec, &target_temp);
            send_command_to_stm32("$SET,TMP," + String((int)target_temp) + ";");
        }
    }
}

void setup() {
    Serial.begin(115200);   
    Serial2.begin(115200);  

    encoder.begin();
    // FIXED: Added interrupt for button reading
    encoder.setup(
        [] { encoder.readEncoder_ISR(); }, 
        [] { encoder.readButton_ISR(); }
    );
    encoder.setBoundaries(0, 100, false); 
    encoder.setEncoderValue(0);
    encoder.setAcceleration(2);

    WiFi.softAP(ssid, password);
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(204); // We send "No content", the browser will calm down
    });    
    server.begin();

    delay(500);
    send_command_to_stm32("$GET,SYS;"); 
}

void push_immediate_web_update() {
    String json = "{\"status\":\"" + stm32_status + "\",\"freq\":" + String(current_freq) + 
                  ",\"pwr\":" + String(current_power) + ",\"target_pwr\":" + String(target_power) + 
                  ",\"temp\":" + String(current_temperature) + ",\"rms\":" + String(current_rms) + 
                  ",\"timer\":\"00:00 / 00:00\",\"scan_freq\":0,\"scan_rms\":0}";
    // === ЛИНЕЙКА ОТЛАДКИ ===
    Serial.print("WS SEND: ");
    Serial.println(json); 
    // =======================
    ws.textAll(json);
}


void loop() {
    // 1. Encoder handle rotation query
    if (encoder.encoderChanged()) {
        if (is_web_updating) {
            // Это было эхо от вызова setEncoderValue! We just reset the flag and ignore it.
            is_web_updating = false;
        } else {
            // This is the actual physical rotation of the handle by the user
            target_power = encoder.readEncoder();
            Serial.print("Encoder Changed! Target power: ");
            Serial.println(target_power);
            
            push_immediate_web_update(); 
        }
    }

    // 2. Polling a physical PRESS on a ZEC11S encoder button (Applies power)
    if (encoder.isEncoderButtonClicked()) {
        Serial.print("Encoder Button Clicked! Applying power: ");
        Serial.println(target_power);
        send_command_to_stm32("$SET,PWR," + String(target_power) + ";");
    }

    // 3. Background reading and parsing of asynchronous UART packets from STM32
    process_stm32_uart();

    // 4. Non-blocking time limit timer
    String timer_str = "00:00 / 00:00";
  /*   if (is_working) {
        uint32_t elapsed_sec = (millis() - work_start_time_ms) / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu", elapsed_sec / 60, elapsed_sec % 60, work_timer_limit_sec / 60, work_timer_limit_sec % 60);
        timer_str = String(buf);

        if (elapsed_sec >= work_timer_limit_sec) {
            is_working = false;
            target_power = 0;
            current_power = 0;
            encoder.setEncoderValue(0);
            send_command_to_stm32("$SET,PWR,0;");
            stm32_status = "TIMEOUT_STOP";
            push_immediate_web_update();
        }
    }*/

    // 5. PERIODIC DATA SENDING (Corrected background telemetry)
    static uint32_t last_web_update = 0;
    if (millis() - last_web_update >= 200) { 
        // We send background data ONLY if the system is not busy with a flood of graph scans
        if (stm32_status != "SCANNING") {
            //FIXED: Background package now MUST contain an up-to-date target_power,
            // Otherwise it would erase changes from the encoder every 200 ms!
            String json = "{\"status\":\"" + stm32_status + "\",\"freq\":" + String(current_freq) + 
                          ",\"pwr\":" + String(current_power) + ",\"target_pwr\":" + String(target_power) + 
                          ",\"temp\":" + String(current_temperature) + ",\"rms\":" + String(current_rms) + 
                          ",\"timer\":\"" + timer_str + "\",\"scan_freq\":0,\"scan_rms\":0}";
            ws.textAll(json);
        }
        last_web_update = millis();
    }
}