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

#include "GearCalculator.h"

static const char* PREFS_NAMESPACE = "gear-ratios";     // Namespace for storing gear data in NVS

GearCalculator::GearCalculator() {}

void GearCalculator::begin() {
    _preferences.begin(PREFS_NAMESPACE, false);

    // Load saved ratios for each gear, or use 0.0f if not found
    for (int i = 0; i < NUM_GEARS; ++i) {
        char min_key[8];
        char max_key[8];
        sprintf(min_key, "g%d_min", i + 1);
        sprintf(max_key, "g%d_max", i + 1);

        _ratios[i].min = _preferences.getFloat(min_key, 0.0f);
        _ratios[i].max = _preferences.getFloat(max_key, 0.0f);
    }
}

int GearCalculator::calculate(const LiveDataPacket& data) {
    // Check for special states first, as they override ratio calculation
    if (data.gearState == GEAR_STATE_KICKSTAND_DOWN) {
        return GEAR_KICKSTAND;
    }
    
    if (data.gearState == GEAR_STATE_NEUTRAL_OR_CLUTCH) {
        // If the bike reports neutral/clutch, we assume it's Neutral.
        // The clutch-in while in gear condition is handled below when it's IN_GEAR but not moving.
        return GEAR_NEUTRAL;
    }

    if (data.gearState == GEAR_STATE_IN_GEAR) {
        // Condition for clutch being pulled in while in gear and stationary
        if (data.isEngineRunning && data.speed <= SPEED_THRESHOLD) {
            return GEAR_CLUTCH;
        }

        // Condition for valid ratio calculation
        if (data.isEngineRunning && data.speed > SPEED_THRESHOLD) {            
            float ratio = (float)data.rpm / (float)data.speed;

            // Find which gear's ratio interval the current ratio falls into
            for (int i = 0; i < NUM_GEARS; ++i) {
                // A valid ratio must be greater than 0
                if (_ratios[i].min > 0.0f && ratio >= _ratios[i].min && ratio <= _ratios[i].max) {
                    return i + 1; // Return the gear number (1-6)
                }
            }
        }
    }

    // If no other condition is met, the gear is unknown
    return GEAR_UNKNOWN;
}

void GearCalculator::startRecording(int gear) {
    if (gear < 1 || gear > NUM_GEARS) return;
    
    _isRecording = true;
    _recordingGear = gear;
    
    // Reset min/max trackers for a new recording session
    _currentMin = 99999.0f;
    _currentMax = 0.0f;
}

void GearCalculator::stopRecording() {
    _isRecording = false;
    // The ratios are already updated in real-time by updateRecording,
    // so the code just needs to save them to persistent storage.
    saveRatios();
}

void GearCalculator::updateRecording(const LiveDataPacket& data) {
    if (!_isRecording) return;

    // We can only record a ratio if the bike is in gear and moving at a reasonable speed
    if (data.gearState == GEAR_STATE_IN_GEAR && data.isEngineRunning && data.speed > 3) {
        if (data.speed == 0) return; // Should not happen due to speed > 3 check, but whatever...
        
        float ratio = (float)data.rpm / (float)data.speed;

        // Update the min and max values for this recording session
        _currentMin = min(_currentMin, ratio);
        _currentMax = max(_currentMax, ratio);

        // Update the main ratio array in real-time so the UI can show live updates
        _ratios[_recordingGear - 1].min = _currentMin;
        _ratios[_recordingGear - 1].max = _currentMax;
    }
}

bool GearCalculator::isRecording() const {
    return _isRecording;
}

void GearCalculator::saveRatios() {
    _preferences.begin(PREFS_NAMESPACE, false);
    for (int i = 0; i < NUM_GEARS; ++i) {
        char min_key[8];
        char max_key[8];
        sprintf(min_key, "g%d_min", i + 1);
        sprintf(max_key, "g%d_max", i + 1);

        _preferences.putFloat(min_key, _ratios[i].min);
        _preferences.putFloat(max_key, _ratios[i].max);
    }
    _preferences.end();
}

void GearCalculator::setRatios(const GearRatio newRatios[NUM_GEARS]) {
    for (int i = 0; i < NUM_GEARS; ++i) {
        _ratios[i] = newRatios[i];
    }
    saveRatios();
}

const GearRatio* GearCalculator::getRatios() const {
    return _ratios;
}