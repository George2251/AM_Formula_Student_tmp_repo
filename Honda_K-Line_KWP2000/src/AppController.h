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
 * @file AppController.h
 * @brief Defines the main application controller class.
 * 
 * This file contains the definition of the AppController class, which acts as the 
 * central orchestrator for the system. It manages the lifecycle of sub-systems 
 * (ECU, Network, OLED, Gear Calculation), handles data synchronization between 
 * threads, and processes WebSocket commands.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "HondaKWP2000.h"
#include "GearCalculator.h"
#include "OledManager.h" 
#include "NetworkService.h"

/**
 * @brief Global debug level controlling verbosity of serial logs.
 * Defined in AppController.cpp.
 */
extern uint8_t globalDebugLevel;

/**
 * @class AppController
 * @brief The central "Brain" of the firmware.
 * 
 * This class implements the Singleton-ish pattern (via a global instance in main)
 * using Two-Stage Initialization. It owns all major subsystem objects and coordinates 
 * the flow of data from the ECU K-Line to the WebSocket clients and OLED display.
 */
class AppController {
public:
    /**
     * @brief Constructor.
     * 
     * Initializes member objects that do not require hardware access.
     * @note This constructor is designed to be safe for static allocation 
     * before the FreeRTOS scheduler or Arduino core is fully running.
     */
    AppController();

    /**
     * @brief Initializes the application logic and hardware.
     * 
     * This method must be called inside setup(). It initializes the heap-allocated
     * objects, starts the FreeRTOS tasks, mounts the filesystem, and begins
     * hardware communication (Serial, I2C, K-Line).
     */
    void begin();
    
    /**
     * @brief Handles a new WebSocket client connection.
     * 
     * Sends the initial state (connection status, settings, current data) 
     * to the newly connected client.
     * 
     * @param client Pointer to the AsyncWebSocketClient instance.
     */
    void handleClientConnect(AsyncWebSocketClient* client);

    /**
     * @brief Processes an incoming text/JSON message from a WebSocket client.
     * 
     * @param data Pointer to the raw data buffer.
     * @param len Length of the received data.
     */
    void handleClientMessage(uint8_t* data, size_t len);

    /**
     * @brief Grant NetworkService access to private members.
     * 
     * Allows NetworkService to trigger callbacks or access state without 
     * exposing public getters/setters for everything.
     */
    friend class NetworkService; 

private:
    // --- Subsystems ---
    HondaKWP2000 _ecu;          ///< Handles low-level K-Line protocol communication.
    GearCalculator _gearCalc;   ///< Logic for determining current gear based on RPM/Speed.
    Preferences _preferences;   ///< Handle for Non-Volatile Storage (NVS).
    NetworkService* _network;   ///< Pointer to Network manager (heap allocated in begin()).
    
    #ifdef USE_OLED_DISPLAY
    OledManager _oled;          ///< Manages the physical OLED display (if enabled).
    #endif

    // --- State & Configuration ---
    String _preferredOledParam;         ///< The parameter currently selected for OLED display (e.g., "gear", "rpm").
    uint8_t _tableDataBuffer[256];      ///< Buffer for raw ECU table dumps.

    // --- RTOS Handles ---
    TaskHandle_t _broadcastTaskHandle;      ///< Handle for the WebSocket broadcast task.
    SemaphoreHandle_t _dataReadySemaphore;  ///< Binary semaphore to signal when new ECU data is ready.

    // --- Internal Helpers ---

    /**
     * @brief Loads configuration settings from NVS (Preferences).
     */
    void _loadPreferences();

    /**
     * @brief Parses and executes a JSON command received via WebSocket.
     * 
     * @param action The command string (e.g., "setMode").
     * @param doc The full JSON document containing parameters.
     */
    void _processJsonCommand(const char* action, JsonDocument& doc);
    
    // --- Static Callbacks (FreeRTOS/ISR Context) ---

    /**
     * @brief Main loop for broadcasting data to WebSockets.
     * 
     * This static function runs as a FreeRTOS task. It waits for the 
     * _dataReadySemaphore and pushes updates to connected clients.
     * 
     * @param pvParameters Pointer to the AppController instance (this).
     */
    static void _broadcastTask(void* pvParameters);

    /**
     * @brief Callback fired by HondaKWP2000 when a packet is fully parsed.
     * 
     * Gives the semaphore to unblock the broadcast task.
     */
    static void _onDataReadyWrapper();

    /**
     * @brief Wrapper to route ECU library logs to the system logging queue.
     * 
     * @param level Log severity level.
     * @param msg The message string.
     */
    static void _kwpLogWrapper(uint8_t level, const char* msg);
};