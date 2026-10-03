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
 * @file GearCalculator.h
 * @brief Defines the logic for calculating gear position from ECU data.
 * 
 * This file contains the GearCalculator class, which determines the current 
 * gear by analyzing the relationship between Engine RPM and Vehicle Speed.
 * It also handles the "Calibration/Learning" mode where new gear ratios 
 * can be recorded and saved to persistent storage.
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "HondaKWP2000.h" // For LiveDataPacket struct

// --- Constants & Return Codes ---

/** @brief Total number of forward gears supported (Standard 6-speed). */
#define NUM_GEARS 6

/** @brief Return code: Transmission is in Neutral. */
#define GEAR_NEUTRAL 0
/** @brief Return code: Clutch is pulled in (RPM/Speed ratio invalid). */
#define GEAR_CLUTCH -1
/** @brief Return code: Kickstand is down (Safety cutout). */
#define GEAR_KICKSTAND -2
/** @brief Return code: Ratio does not match any known gear. */
#define GEAR_UNKNOWN -3
/** @brief Speed threshold in km/h for detecting if the bike is in gear while the clutch lever is pulled in.
 *         I needed this since my short clutch lever does not have a switch. */
#define SPEED_THRESHOLD 3

/**
 * @struct GearRatio
 * @brief Represents the valid ratio range for a specific gear.
 * 
 * A gear is considered "active" if the current (RPM / Speed) ratio
 * falls inclusively between min and max.
 */
struct GearRatio {
    float min = 0.0f; ///< Minimum calculated ratio threshold.
    float max = 0.0f; ///< Maximum calculated ratio threshold.
};

/**
 * @class GearCalculator
 * @brief Manages gear determination, calibration, and storage.
 * 
 * This class encapsulates the logic to convert raw ECU sensor data (RPM, Speed, 
 * Neutral Switch) into a user-friendly gear number. It uses Non-Volatile Storage (NVS)
 * via the Preferences library to persist gear ratio calibration data across reboots.
 */
class GearCalculator {
public:
    /**
     * @brief Constructor.
     */
    GearCalculator();

    /**
     * @brief Initializes the calculator and loads saved ratios.
     * 
     * Opens the NVS namespace and populates the internal ratio array 
     * with previously saved values. Should be called during setup().
     */
    void begin();

    /**
     * @brief Determines the current gear position.
     * 
     * Calculates the current RPM/Speed ratio and checks 
     * it against the stored values. Also handles special states like Neutral, 
     * Clutch-in, or Kickstand based on flags in the data packet.
     * 
     * @param data The latest data packet received from the ECU.
     * @return int The gear number (1-6) or a special GEAR_* error code.
     */
    int calculate(const LiveDataPacket& data);

    // --- Calibration Methods ---

    /**
     * @brief Enters calibration mode for a specific gear.
     * 
     * Resets internal min/max trackers. Subsequent calls to updateRecording()
     * will expand the ratio band for this specific gear.
     * 
     * @param gear The gear number to record (1-6).
     */
    void startRecording(int gear);

    /**
     * @brief Exits calibration mode and saves data.
     * 
     * Stops the recording process and immediately commits the newly calculated
     * min/max values to NVS memory.
     */
    void stopRecording();

    /**
     * @brief Updates the ratio band while in calibration mode.
     * 
     * If recording is active, this calculates the instantaneous ratio from the
     * packet and expands the min/max window for the target gear to include this new value.
     * 
     * @param data The latest data packet containing RPM and Speed.
     */
    void updateRecording(const LiveDataPacket& data);

    /**
     * @brief Checks if the calculator is currently in calibration mode.
     * @return true if recording, false otherwise.
     */
    bool isRecording() const;

    // --- Persistence & Accessors ---

    /**
     * @brief Manually forces a save of current ratios to NVS.
     */
    void saveRatios();

    /**
     * @brief Bulk updates all gear ratios.
     * 
     * Used when receiving a full configuration update from the Web App.
     * Automatically saves to NVS after updating.
     * 
     * @param newRatios Array of 6 GearRatio objects.
     */
    void setRatios(const GearRatio newRatios[NUM_GEARS]);

    /**
     * @brief Retrieves the current ratio configuration.
     * @return Pointer to the internal array of GearRatios.
     */
    const GearRatio* getRatios() const;

private:
    Preferences _preferences;       ///< Handle for NVS storage.
    GearRatio _ratios[NUM_GEARS];   ///< Internal storage for gear bands.
    
    // --- Volatile State for Calibration ---
    volatile bool _isRecording = false; ///< Flag indicating if learning mode is active.
    volatile int _recordingGear = 0;    ///< The specific gear currently being learned (1-6).

    float _currentMin = 9999.0f;        ///< Temporary tracker for min ratio during recording.
    float _currentMax = 0.0f;           ///< Temporary tracker for max ratio during recording.
};