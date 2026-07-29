#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "web_page.h"

// Access point credentials
const char* ssid = "Ultrasonic_Cavitation_AP";
const char* password = "Password1234";

// Server and WebSocket instances on standard port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Global parsed values from STM32 telemetry
String received_packet = "";
String last_freq = "0";
String last_rms = "0.0000";
int current_power = 50;

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char*)data);
        if (error) return;

        // If user adjusted power slider on the web UI
        if (doc.containsKey("power")) {
            current_power = doc["power"];
            // Send command forward to STM32 via Hardware Serial 2
            Serial2.printf("$POWER,%d;\r\n", current_power);
        }
        // If user pressed "START SCAN" button
        if (doc.containsKey("cmd")) {
            String cmd = doc["cmd"];
            if (cmd == "scan") {
                Serial2.print("$SCAN,1;\r\n");
            }
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            break;
        case WS_EVT_DISCONNECT:
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        default:
            break;
    }
}

void parse_stm32_telemetry(String packet) {
    // Format expected: $LIVE,frequency,RMS;
    if (packet.startsWith("$LIVE,") && packet.endsWith(";")) {
        packet.remove(0, 6); // Strip $LIVE,
        packet.remove(packet.length() - 1); // Strip ;

        int comma_idx = packet.indexOf(',');
        if (comma_idx != -1) {
            last_freq = packet.substring(0, comma_idx);
            last_rms = packet.substring(comma_idx + 1);

            // Broadcast telemetry packet to all connected web clients
            JsonDocument doc;
            doc["freq"] = last_freq;
            doc["rms"] = last_rms;
            doc["power"] = current_power;
            
            String json_output;
            serializeJson(doc, json_output);
            ws.textAll(json_output);
        }
    }
}

void setup() {
    // Local debugging output to PC via built-in USB
    Serial.begin(115200);

    // Communication link to STM32 UART4 (Default pins: RX2=GPIO16, TX2=GPIO17)
    Serial2.begin(115200, SERIAL_8N1, 16, 17);

    // Setup Wi-Fi Soft Access Point
    WiFi.softAP(ssid, password);
    Serial.println("SoftAP network initialized");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Connect WebSocket events
    ws.onEvent(onEvent);
    server.addHandler(&ws);

    // Define HTTP routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    // Start network server
    server.begin();
}

void loop() {
    ws.cleanupClients();

    // Read byte stream coming cross-wired from STM32 UART4
    while (Serial2.available()) {
        char in_char = (char)Serial2.read();
        received_packet += in_char;

        // Check if packet terminator reached
        if (in_char == ';') {
            parse_stm32_telemetry(received_packet);
            received_packet = ""; // Reset buffer
        }
    }
}
