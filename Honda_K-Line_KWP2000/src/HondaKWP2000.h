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
 * @file HondaKWP2000.h
 * @brief Core library for Honda K-Line (KWP2000) ECU communication.
 * 
 * This file defines the HondaKWP2000 class, which implements the physical layer
 * bit-banging initialization, the KWP2000 protocol framing, and the high-level
 * logic to poll specific memory tables from the ECU. It runs a dedicated 
 * FreeRTOS task to manage the strict timing requirements of the protocol.
 * 
 * @version 14.0 (Gear Indicator Feature)
 */

#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <atomic>

// --- Debug Configuration ---
/** @brief No debug output. */
#define DEBUG_LEVEL_NONE    0
/** @brief Standard operational logs (Connection state, major errors). */
#define DEBUG_LEVEL_DEFAULT 1
/** @brief Detailed logs (Hex dumps of packets). */
#define DEBUG_LEVEL_VERBOSE 2
/** @brief Low-level flow tracing. */
#define DEBUG_LEVEL_TRACE   3

// --- Resilience Constants ---
/** @brief Number of failed packets before declaring connection lost. */
#define MAX_CONSECUTIVE_ERRORS 5
/** @brief Interval to send Keep-Alive messages when idle. */
#define KWP_KEEP_ALIVE_INTERVAL_MS 2000
/** @brief Watchdog timeout for the background task. */
#define KWP_TASK_WDT_TIMEOUT_S 10
/** @brief Max ticks to wait for mutex acquisition. */
#define WAITING_TIME_FOR_SEMAPHORE_TAKE 100

// --- Protocol Timing & Constants ---
/** @brief Standard baud rate for Honda K-Line. */
#define HONDA_BAUDRATE 10400
/** @brief Duration to hold K-Line LOW to wake ECU (ms). */
#define K_LINE_INIT_LOW_MS 70
/** @brief Duration to hold K-Line HIGH before handshake (ms). */
#define K_LINE_INIT_HIGH_MS 120
/** @brief Wait time after Wakeup message before sending Init (ms). */
#define K_LINE_WAKEUP_DELAY_MS 200
/** @brief Timeout waiting for response header bytes (ms). */
#define ECU_RESPONSE_HEADER_TIMEOUT_MS 50
/** @brief Timeout waiting for response body bytes (ms). */
#define ECU_RESPONSE_BODY_TIMEOUT_MS 100

/**
 * @enum KWPOperatingMode
 * @brief Defines the active behavior of the background task.
 */
enum KWPOperatingMode { 
    MODE_IDLE,          ///< Connection maintained via Keep-Alive, no data polling.
    MODE_LIVE_DATA,     ///< Actively polling Table 0x10 and 0xD1 for sensor data.
    MODE_TABLE_VIEWER,  ///< Polling a specific arbitrary table for inspection.
    MODE_GEAR_INDICATOR ///< Logically same as LIVE_DATA, implies Gear UI usage.
};

/**
 * @enum KWPConnectionStatus
 * @brief Detailed status codes for the connection state machine.
 */
enum KWPConnectionStatus { 
    CONN_STATUS_OK,                 ///< Connected and communicating.
    CONN_ERROR_SERIAL_LOCK,         ///< Could not acquire Serial mutex.
    CONN_ERROR_WAKEUP_FAIL,         ///< Wakeup pulse/message failed.
    CONN_ERROR_INIT_FAIL,           ///< Initialization handshake failed.
    CONN_ERROR_ECHO_TIMEOUT,        ///< TX Echo not received (Line broken?).
    CONN_ERROR_RESPONSE_TIMEOUT,    ///< ECU did not reply in time.
    CONN_ERROR_RESPONSE_INVALID,    ///< Reply received but header/format wrong.
    CONN_ERROR_RESPONSE_CHECKSUM,   ///< Checksum verification failed.
    CONN_ERROR_INVALID_LENGTH       ///< Packet length byte invalid.
};

/**
 * @enum GearState
 * @brief Discrete gear states derived from ECU digital flags (Table 0xD1).
 */
enum GearState { 
    GEAR_STATE_IN_GEAR,             ///< Bike is likely moving or ready to move.
    GEAR_STATE_NEUTRAL_OR_CLUTCH,   ///< Neutral light is on OR Clutch is pulled.
    GEAR_STATE_KICKSTAND_DOWN,      ///< Safety cutout active.
    GEAR_STATE_UNKNOWN              ///< Undefined state.
};

/**
 * @struct LiveDataPacket
 * @brief normalized snapshot of ECU sensor data.
 * 
 * Contains converted values (Integers, Floats) ready for UI display.
 * Used to transfer data from the KWP task to the App/Network tasks safely.
 */
struct LiveDataPacket {
    int rpm = 0;                    ///< Engine Speed (Revolutions Per Minute).
    int speed = 0;                  ///< Vehicle Speed (km/h).
    float tps = 0.0f;               ///< Throttle Position (%).
    float tpsVoltage = 0.0f;        ///< Throttle Position (Volts).
    float ect = 0.0f;               ///< Engine Coolant Temp (Celsius).
    float ectVoltage = 0.0f;        ///< ECT Sensor Voltage.
    float iat = 0.0f;               ///< Intake Air Temp (Celsius).
    float iatVoltage = 0.0f;        ///< IAT Sensor Voltage.
    float batteryVoltage = 0.0f;    ///< Battery Voltage (Volts).
    float map = 0.0f;               ///< Manifold Absolute Pressure (kPa).
    float mapVoltage = 0.0f;        ///< MAP Sensor Voltage.
    bool isEngineRunning = false;   ///< True if RPM > 0 / Status Flag set.
    GearState gearState = GEAR_STATE_UNKNOWN; ///< Derived mechanical state.
};

// --- Callback Type Definitions ---
using KWPLogCallback = void (*)(uint8_t level, const char* message);
using KWPActivityCallback = void (*)();
using KWPDataReadyCallback = void (*)();

/**
 * @class HondaKWP2000
 * @brief Manages K-Line communication with the ECU.
 * 
 * This class handles the complex initialization timing required to "wake up"
 * Honda ECUs. It implements a double-buffered data acquisition system to allow
 * thread-safe reading of live data while the background task continues polling.
 */
class HondaKWP2000 {
public:
    /**
     * @brief Constructor.
     * 
     * @param serial Reference to the HardwareSerial port (e.g., Serial2).
     * @param rxPin GPIO pin for RX.
     * @param txPin GPIO pin for TX.
     */
    HondaKWP2000(HardwareSerial& serial, uint8_t rxPin, uint8_t txPin);

    /**
     * @brief Allocates synchronization primitives (Mutexes).
     * Must be called in setup().
     */
    void begin();
    
    // --- Debugging ---
    /** @brief Registers a callback function for log output. */
    void setDebug(KWPLogCallback callback, uint8_t level = DEBUG_LEVEL_DEFAULT);
    /** @brief Disables logging. */
    void disableDebug();
    
    // --- Task Control ---
    /** 
     * @brief Creates and starts the pinned FreeRTOS task.
     * Usually pinned to Core 0 to leave Core 1 for WiFi/App logic.
     */
    void startBackgroundTask();
    /** @brief Signals the task to stop and waits for it to delete itself. */
    void stopBackgroundTask();

    // --- Callbacks ---
    /** 
     * @brief Sets callbacks for LED activity indicators (TX/RX). 
     * @param txCallback Called when a packet is sent.
     * @param rxCallback Called when a valid packet is received.
     */
    void setActivityCallbacks(KWPActivityCallback txCallback, KWPActivityCallback rxCallback);
    
    /** 
     * @brief Sets callback fired when new Live Data is parsed. 
     * Used to notify the main thread that fresh data is available.
     */
    void setDataReadyCallback(KWPDataReadyCallback dataReadyCallback);

    // --- Operation Modes ---
    /** @brief Switches to polling standard sensors (Table 0x10 & 0xD1). */
    void startSensorPolling();
    /** 
     * @brief Switches to polling a raw memory table.
     * @param table The hex ID of the table (e.g., 0x10, 0x20). 
     */
    void startTableViewer(uint8_t table);
    /** @brief Stops polling but maintains connection via Keep-Alive. */
    void stopPolling();

    // --- Configuration ---
    void setSensorRequestInterval(uint32_t intervalMs);
    void setTableViewerInterval(uint32_t intervalMs);
    
    // --- State Accessors ---
    bool isConnected() const;
    bool isReconnecting() const;
    KWPConnectionStatus getLastConnectionStatus();
    KWPOperatingMode getCurrentMode();

    // --- Data Accessors ---
    
    /**
     * @brief Checks if new raw data is available in Table Viewer mode.
     */
    bool isNewTableDataAvailable();

    /**
     * @brief Copies raw table data to a user buffer.
     * @return Number of bytes copied.
     */
    uint8_t getTableData(uint8_t* userBuffer, size_t bufferSize);

    /**
     * @brief atomically retrieves the latest parsed sensor data.
     * Uses double-buffering logic to ensure data consistency.
     * @param packet Reference to destination struct.
     */
    void getLiveDataSnapshot(LiveDataPacket& packet);

private:
    HardwareSerial& _serial;
    uint8_t _rxPin, _txPin;
    uint8_t _debugLevel = DEBUG_LEVEL_NONE;
    KWPLogCallback _logCallback = nullptr;
    
    // --- Internal State ---
    volatile bool _isConnected = false;
    volatile bool _isReconnecting = false;
    volatile KWPConnectionStatus _lastConnectionStatus = CONN_STATUS_OK;
    uint8_t _consecutiveErrors = 0;

    // --- RTOS Objects ---
    TaskHandle_t _taskHandle = NULL;
    SemaphoreHandle_t _dataMutex = NULL;   ///< Protects shared data access.
    SemaphoreHandle_t _serialMutex = NULL; ///< Protects Serial port access.

    volatile bool _taskRunning = false;
    volatile KWPOperatingMode _currentMode = MODE_LIVE_DATA;
    uint32_t _sensorRequestInterval = 200;
    uint32_t _tableViewerIntervalMs = 1000;
    uint8_t  _tableViewTargetTable = 0x00;

    // --- Callbacks ---
    KWPActivityCallback _txCallback = nullptr;
    KWPActivityCallback _rxCallback = nullptr;
    KWPDataReadyCallback _dataReadyCallback = nullptr;

    // --- Buffers ---
    volatile bool _isNewTableDataAvailable = false;
    uint8_t _rawTableViewBuffer[256];
    uint8_t _rawTableViewBufferLength = 0;
    uint8_t _responseBuffer[256];
    uint8_t _responseLength = 0;
    
    /** 
     * @brief Double-buffer for live data. 
     * One is being written by the background task, the other read by the app.
     */
    LiveDataPacket _liveDataBuffers[2];
    std::atomic<uint8_t> _activeBufferIndex;
    
    // --- Internal Helpers ---
    void _log(uint8_t level, const char* format, ...);
    void _logHex(uint8_t level, const char* prefix, const uint8_t* data, size_t length);
    
    /** @brief Executes the Wakeup -> Init -> Echo sequence. */
    KWPConnectionStatus performConnect_unsafe();
    
    /** @brief Bits-bangs the K-Line low for initialization. */
    void pullKLineLow_unsafe(unsigned long durationMs);
    
    /** @brief Sends a packet and flushes serial. */
    bool sendRequest_unsafe(const uint8_t* message, size_t length);
    
    /** @brief Waits for and validates the ECU response. */
    int receiveResponse_unsafe(const uint8_t* sentMessage, size_t sentLength, unsigned long &responseTime);
    
    /** @brief High-level helper to request a full table dump. */
    bool readEntireTable_unsafe(uint8_t table);
    
    uint8_t calculateChecksum(const uint8_t* data, size_t length);
    
    /** @brief Parses raw bytes from Table 0x10 into physical units. */
    void parseTable10(LiveDataPacket& packet, const uint8_t* response, uint8_t length);
    
    /** @brief Parses raw bytes from Table 0xD1 into gear/engine states. */
    void parseTableD1(LiveDataPacket& packet, const uint8_t* response, uint8_t length);
    
    /** @brief The FreeRTOS task loop function. */
    static void kwpTask(void *pvParameters);
};