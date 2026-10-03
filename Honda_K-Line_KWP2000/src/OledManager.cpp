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

#include "OledManager.h"
#include "GearCalculator.h" 
#include <esp_task_wdt.h>
#include "GearFont.h"

OledManager::OledManager(HondaKWP2000& ecu) 
    : _ecu(ecu), _display(128, 64, &Wire, -1) {
    _refreshMs = DEFAULT_OLED_REFRESH;
    strncpy((char*)_parameterToShow, DEFAULT_OLED_PARAM, sizeof(_parameterToShow) - 1);
    _gearValue = GEAR_UNKNOWN;
    _brightness = DEFAULT_OLED_BRIGHTNESS;
}

void OledManager::begin() {
    _preferences.begin("honda-ecu", false); // Use the same namespace as main to access settings

    // Load persistent settings
    _brightness = _preferences.getUChar("oledBright", DEFAULT_OLED_BRIGHTNESS);
    _refreshMs = _preferences.getUInt("oledRate", DEFAULT_OLED_REFRESH);
    String param = _preferences.getString("oledParam", DEFAULT_OLED_PARAM);
    strncpy((char*)_parameterToShow, param.c_str(), sizeof(_parameterToShow) - 1);
    _parameterToShow[sizeof(_parameterToShow) - 1] = '\0';

    // Initialize the hardware
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (!_display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    } else {
        _display.ssd1306_command(SSD1306_SEGREMAP | 0x01);
        _display.ssd1306_command(SSD1306_COMSCANINC);
        _display.ssd1306_command(SSD1306_SETCONTRAST);
        _display.ssd1306_command(_brightness);
        _display.setRotation(1);
        _display.clearDisplay();
        _display.display();
    }
    _preferences.end();
}

void OledManager::startTask() {
    xTaskCreatePinnedToCore(
        _oledDisplayTask,
        "OledTask",
        4096,
        this,
        1,       
        &_taskHandle,
        1
    );
    if (_taskHandle) {
        esp_task_wdt_add(_taskHandle);
    }
}

void OledManager::updateGear(int gear) {
    taskENTER_CRITICAL(&_mux);
    _gearValue = gear;
    taskEXIT_CRITICAL(&_mux);
}

void OledManager::updateConnectionStatus(bool isConnected) {
    taskENTER_CRITICAL(&_mux);
    _isConnected = isConnected;
    taskEXIT_CRITICAL(&_mux);
}

void OledManager::setBrightness(uint8_t brightness) {
    taskENTER_CRITICAL(&_mux);
    _brightness = brightness;
    taskEXIT_CRITICAL(&_mux);

    _display.ssd1306_command(SSD1306_SETCONTRAST);
    _display.ssd1306_command(_brightness);
    
    _preferences.begin("honda-ecu", false);
    _preferences.putUChar("oledBright", _brightness);
    _preferences.end();
}

void OledManager::setParameter(const char* param) {
    taskENTER_CRITICAL(&_mux);
    strncpy((char*)_parameterToShow, param, sizeof(_parameterToShow) - 1);
    _parameterToShow[sizeof(_parameterToShow) - 1] = '\0';
    taskEXIT_CRITICAL(&_mux);

    _preferences.begin("honda-ecu", false);
    _preferences.putString("oledParam", param);
    _preferences.end();
}

void OledManager::setRefreshRate(uint32_t rate) {
    taskENTER_CRITICAL(&_mux);
    _refreshMs = rate;
    taskEXIT_CRITICAL(&_mux);

    _preferences.begin("honda-ecu", false);
    _preferences.putUInt("oledRate", rate);
    _preferences.end();
}

void OledManager::getSettingsJson(JsonDocument& doc) {
    taskENTER_CRITICAL(&_mux);
    doc["oledBrightness"] = _brightness;
    doc["oledParam"] = (const char*)_parameterToShow;
    doc["oledRate"] = _refreshMs;
    taskEXIT_CRITICAL(&_mux);
}

void OledManager::_oledDisplayTask(void* pvParameters) {
    OledManager* instance = (OledManager*)pvParameters;
    instance->_taskLoop();
}

// The actual task logic
void OledManager::_taskLoop() {
    while(true) {
        char localParam[sizeof(_parameterToShow)];
        uint32_t localRefreshMs;
        int localGear;
        
        taskENTER_CRITICAL(&_mux);
        strcpy(localParam, (const char*)_parameterToShow);
        localRefreshMs = _refreshMs;
        localGear = _gearValue;
        taskEXIT_CRITICAL(&_mux);

        _display.clearDisplay();
        _display.setTextColor(SSD1306_WHITE);

        if (strcmp(localParam, "none") == 0) {
            _display.setTextSize(1); 
            _display.setCursor(0, 60); 
            _display.println("DISPLAY");
            _display.println("OFF");
        } else if (strcmp(localParam, "gear") == 0) {
            String gearStr;
            switch(localGear) {
                case GEAR_NEUTRAL: gearStr = "N"; break;
                case GEAR_CLUTCH: gearStr = "C"; break;
                case GEAR_KICKSTAND: gearStr = "K"; break;
                case GEAR_UNKNOWN: gearStr = "?"; break;
                default: gearStr = String(localGear); break;
            }
           
            _display.setFont(&GearFont);
            _display.setTextSize(1); 
            _display.setCursor(4, 20); 
            _display.println(gearStr);
        } else {
            String label = "", value = "";
            LiveDataPacket dataPacket;
            _ecu.getLiveDataSnapshot(dataPacket);

            if      (strcmp(localParam, "rpm")   == 0)  { label = "RPM";   value = String(dataPacket.rpm); }
            else if (strcmp(localParam, "speed") == 0)  { label = "SPEED"; value = String(dataPacket.speed); }
            else if (strcmp(localParam, "tps")   == 0)  { label = "TPS";   value = String(dataPacket.tps, 1); }
            else if (strcmp(localParam, "ect")   == 0)  { label = "ECT";   value = String(dataPacket.ect, 0); }
            else if (strcmp(localParam, "iat")   == 0)  { label = "IAT";   value = String(dataPacket.iat, 0); }
            else if (strcmp(localParam, "map")   == 0)  { label = "MAP";   value = String(dataPacket.map, 0); }
            else if (strcmp(localParam, "batt")  == 0)  { label = "BATT";  value = String(dataPacket.batteryVoltage, 1); }

            _display.setFont(NULL);
            _display.setTextSize(1); _display.setCursor(0, 20); _display.println(label);
            _display.setTextSize(2); _display.setCursor(0, 50); _display.println(value);
        }
        _display.display();

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(localRefreshMs));
    }
}