/*
    Honda ECU Manager
    Copyright (C) 2025 andreibaw

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include "SystemStatus.h"
#include "Config.h"
#include <Adafruit_NeoPixel.h>
#include <esp_task_wdt.h>

static QueueHandle_t logQueue = NULL;
static QueueHandle_t ledQueue = NULL;
static Adafruit_NeoPixel statusPixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

struct LogMessage { char msg[LOG_MSG_MAX_LENGTH]; };
enum LedColor { TX_LED_GREEN, TX_LED_RED };

SystemStatus SysStatus;
extern uint8_t globalDebugLevel; 

void logTask(void* pvParameters) {
    LogMessage logMsg;
    while(true) {
        if (xQueueReceive(logQueue, &logMsg, portMAX_DELAY) == pdPASS) {
            Serial.println(logMsg.msg);
        }
    }
}

void ledTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while(true) {
        LedColor color;
        if (xQueueReceive(ledQueue, &color, pdMS_TO_TICKS(5000)) == pdPASS) {
            if (color == TX_LED_GREEN) statusPixel.setPixelColor(0, statusPixel.Color(0, 80, 0));
            else statusPixel.setPixelColor(0, statusPixel.Color(80, 0, 0));
            statusPixel.show();
            vTaskDelay(pdMS_TO_TICKS(10));
            statusPixel.clear();
            statusPixel.show();
        }
        esp_task_wdt_reset();
    }
}

void SystemStatus::begin() {
    logQueue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage));
    ledQueue = xQueueCreate(10, sizeof(LedColor));
    statusPixel.begin();
    statusPixel.setBrightness(10);
    statusPixel.clear();
    statusPixel.show();
}

void SystemStatus::startTasks() {
    xTaskCreatePinnedToCore(logTask, "LogTask", 3000, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(ledTask, "LedTask", 2048, NULL, 1, NULL, 1);
}

void SystemStatus::signalKwpTx() {
    if(!ledQueue) return;
    LedColor c = TX_LED_GREEN;
    xQueueSend(ledQueue, &c, 0);
}

void SystemStatus::signalKwpRx() {
    if(!ledQueue) return;
    LedColor c = TX_LED_RED;
    xQueueSend(ledQueue, &c, 0);
}

void logMessage(uint8_t level, const char* format, ...) {
    // Failsafe: If queue not ready, print directly so boot errors can be read
    if (logQueue == NULL) {
        if(level <= globalDebugLevel) {
            va_list args; va_start(args, format);
            char buf[LOG_MSG_MAX_LENGTH];
            vsnprintf(buf, sizeof(buf), format, args);
            va_end(args);
            Serial.println(buf);
        }
        return;
    }
    
    if (level > globalDebugLevel) return;
    
    LogMessage logMsg;
    va_list args;
    va_start(args, format);
    vsnprintf(logMsg.msg, LOG_MSG_MAX_LENGTH, format, args);
    va_end(args);
    xQueueSend(logQueue, &logMsg, 0);
}