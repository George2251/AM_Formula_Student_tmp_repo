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
 * @file SystemStatus.h
 * @brief Manages system-wide status indicators and logging.
 * 
 * This file contains the SystemStatus class and the global logging helper.
 * It abstracts the hardware specific status feedback (like RGB NeoPixel LEDs)
 * and provides a thread-safe, queue-based logging mechanism to prevent 
 * Serial port contention between FreeRTOS tasks.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Thread-safe, printf-style global logging function.
 * 
 * formatting a string and pushing it to a FreeRTOS queue. A background 
 * task then prints it to Serial. This prevents task blocking and 
 * data corruption on the UART line.
 * 
 * @param level The debug severity level (compared against globalDebugLevel).
 * @param format Standard C printf format string (e.g., "Value: %d").
 * @param ... Variable arguments matching the format string.
 */
void logMessage(uint8_t level, const char* format, ...);

/**
 * @class SystemStatus
 * @brief Controls status LEDs and background logging tasks.
 * 
 * This class manages the visual feedback for K-Line activity (TX/RX blink)
 * and handles the consumption of the log queue.
 */
class SystemStatus {
public:
    /**
     * @brief Initializes hardware resources.
     * 
     * Sets up the NeoPixel/LED hardware and allocates the FreeRTOS queues 
     * required for logging and LED signaling.
     */
    void begin();

    /**
     * @brief Starts the background FreeRTOS tasks.
     * 
     * Launches the 'LogTask' (consumes log queue) and 'LedTask' 
     * (consumes LED signal queue).
     */
    void startTasks();

    /**
     * @brief Signals that a K-Line transmission (TX) occurred.
     * 
     * Enqueues an event to blink the status LED (typically Green).
     * Safe to call from any thread or callback.
     */
    void signalKwpTx();

    /**
     * @brief Signals that a K-Line reception (RX) occurred.
     * 
     * Enqueues an event to blink the status LED (typically Red).
     * Safe to call from any thread or callback.
     */
    void signalKwpRx();
};

/** 
 * @brief Global singleton instance of the SystemStatus manager.
 * Defined in SystemStatus.cpp.
 */
extern SystemStatus SysStatus;