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

#pragma once

#include <Arduino.h>

// You should just create a hotspot on your phone for
// the ESP to connect to, and write the credentials here.

// --- WiFi Credentials ---
#define WIFI_SSID       "SSID"
#define WIFI_PASS       "PASS"

// --- mDNS name ---
#define MDNS_NAME       "ecu"

// --- Hardware Pins ---
#define K_LINE_RX_PIN   8
#define K_LINE_TX_PIN   9
#define I2C_SDA_PIN     1
#define I2C_SCL_PIN     2
#define NEOPIXEL_PIN    21

// --- System Constants ---
#define APP_WDT_TIMEOUT_S       10
#define WIFI_RECONNECT_MS       10000
#define WS_PING_INTERVAL_MS     5000
#define LOG_QUEUE_LENGTH        20
#define LOG_MSG_MAX_LENGTH      192

// --- Application Defaults ---
#define DEFAULT_SENSOR_INTERVAL 150
#define DEFAULT_TABLE_INTERVAL  1000