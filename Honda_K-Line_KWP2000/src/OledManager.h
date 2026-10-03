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
 * @file OledManager.h
 * @brief Manages the physical SSD1306 OLED Display.
 * 
 * This file contains the OledManager class, which handles the rendering of 
 * real-time ECU data (Gear, RPM, Speed, etc.) onto an I2C OLED screen.
 * It runs in its own FreeRTOS task to decouple display refreshing from 
 * the time-critical K-Line communication logic.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HondaKWP2000.h" // Required for LiveDataPacket

// --- Hardware & Configuration Constants ---

/** @brief I2C Serial Data Pin (ESP32-S3 specific). */
#define I2C_SDA_PIN 1 
/** @brief I2C Serial Clock Pin (ESP32-S3 specific). */
#define I2C_SCL_PIN 2

/** @brief Default brightness (0-255) if not found in NVS. */
#define DEFAULT_OLED_BRIGHTNESS 20
/** @brief Default refresh interval in milliseconds. */
#define DEFAULT_OLED_REFRESH    1000
/** @brief Default parameter to display ("gear", "rpm", "speed", etc.). */
#define DEFAULT_OLED_PARAM      "gear"

/**
 * @class OledManager
 * @brief Thread-safe manager for the OLED display.
 * 
 * This class wraps the Adafruit_SSD1306 driver. It handles persistent settings 
 * via Preferences, manages a dedicated update thread, and provides thread-safe 
 * setters for changing display parameters from other system tasks.
 */
class OledManager {
public:
    /**
     * @brief Constructor.
     * 
     * @param ecu Reference to the main HondaKWP2000 object. 
     *            Required to poll live sensor data (e.g., Temp, RPM) when
     *            the display is not in "Gear Indicator" mode.
     */
    OledManager(HondaKWP2000& ecu);

    /**
     * @brief Initializes hardware and loads settings.
     * 
     * Configures the I2C bus, initializes the SSD1306 display driver, 
     * and retrieves saved configuration (Brightness, Param, Rate) from NVS.
     */
    void begin();

    /**
     * @brief Launches the display refresh task.
     * 
     * Creates a pinned FreeRTOS task that continuously updates the screen
     * based on the configured refresh rate.
     */
    void startTask();

    // --- Thread-Safe State Updaters ---

    /**
     * @brief Updates the cached gear value.
     * 
     * Called by the main loop/broadcast task whenever a new gear is calculated.
     * @param gear The current gear (-1=Clutch, 0=Neutral, 1-6=Gear).
     */
    void updateGear(int gear);

    /**
     * @brief Updates the ECU connection status flag.
     * 
     * Used to display a disconnected icon or status text on the screen.
     * @param isConnected True if K-Line communication is active.
     */
    void updateConnectionStatus(bool isConnected);
    
    // --- WebSocket Command Handlers ---

    /**
     * @brief Sets the display contrast/brightness.
     * 
     * Updates state and saves to NVS.
     * @param brightness Value from 0 (dim) to 255 (bright).
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Sets which data parameter to display.
     * 
     * Supports: "gear", "rpm", "speed", "tps", "ect", "iat", "batt", "map", "none".
     * Updates state and saves to NVS.
     * @param param C-string identifier of the parameter.
     */
    void setParameter(const char* param);

    /**
     * @brief Sets the screen refresh interval.
     * 
     * Updates state and saves to NVS.
     * @param rate Refresh delay in milliseconds.
     */
    void setRefreshRate(uint32_t rate);

    /**
     * @brief Serializes current settings into a JSON document.
     * 
     * Used during the WebSocket handshake to sync the frontend UI 
     * with the current device state.
     * 
     * @param doc Reference to the ArduinoJson document to populate.
     */
    void getSettingsJson(JsonDocument& doc);

private:
    HondaKWP2000& _ecu;         ///< Reference to ECU object for pulling live data.
    Adafruit_SSD1306 _display;  ///< Underlying display driver instance.
    Preferences _preferences;   ///< NVS storage handle.
    TaskHandle_t _taskHandle = NULL; ///< Handle for the background FreeRTOS task.
    
    /** 
     * @brief Mutex for thread safety.
     * Protects access to volatile variables shared between the display task
     * and the main application thread.
     */
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    // --- Volatile State (Protected by _mux) ---
    volatile uint32_t _refreshMs;       ///< Current refresh interval in ms.
    volatile char _parameterToShow[16]; ///< Current parameter string ID.
    volatile int _gearValue;            ///< Last calculated gear.
    volatile uint8_t _brightness;       ///< Current brightness level.
    volatile bool _isConnected = false; ///< Current ECU connection state.

    /**
     * @brief The main execution loop for the display task.
     * 
     * Contains the logic for clearing the buffer, drawing the specific
     * parameter/gear, and sending the buffer to the OLED.
     */
    void _taskLoop();

    /**
     * @brief Static trampoline for FreeRTOS task creation.
     * 
     * @param pvParameters Pointer to the OledManager instance.
     */
    static void _oledDisplayTask(void* pvParameters);
};