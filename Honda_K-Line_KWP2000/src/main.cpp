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

/**
 * @file main.cpp
 * @brief Main Entry point.
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "Config.h"
#include "AppController.h"

AppController app;

void setup() {
    Serial.begin(115200);
    
    delay(1000); 

    Serial.println("[MAIN] Starting Boot Sequence...");

    esp_task_wdt_init(APP_WDT_TIMEOUT_S, true);
    
    Serial.println("[MAIN] Calling app.begin()...");
    app.begin();

    Serial.println("[MAIN] Setup Done");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}