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

#include "AppController.h"
#include "Config.h"
#include "SystemStatus.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>

uint8_t globalDebugLevel = DEBUG_LEVEL_TRACE;
static AppController* _instance = nullptr; 


AppController::AppController() 
    : _ecu(Serial2, K_LINE_RX_PIN, K_LINE_TX_PIN), 
      _gearCalc(),
      _network(nullptr)
      #ifdef USE_OLED_DISPLAY
      , _oled(_ecu)
      #endif
{
    _instance = this;
    _broadcastTaskHandle = NULL;
}


// Called from setup(). Safe to touch hardware/OS here.
void AppController::begin() {
    SysStatus.begin();
    SysStatus.startTasks();
    logMessage(1, "[SYS] Booting...");

    if (!LittleFS.begin()) logMessage(1, "[SYS] CRITICAL: LittleFS Mount Failed");
    else logMessage(1, "[SYS] LittleFS Mounted");

    _dataReadySemaphore = xSemaphoreCreateBinary();

    _loadPreferences();
    _gearCalc.begin();

    #ifdef USE_OLED_DISPLAY
    _oled.begin();
    #endif

    _ecu.begin();
    _ecu.setDebug(_kwpLogWrapper, globalDebugLevel);
    _ecu.setActivityCallbacks(
        [](){ SysStatus.signalKwpTx(); },
        [](){ SysStatus.signalKwpRx(); }
    );
    _ecu.setDataReadyCallback(_onDataReadyWrapper);
    _ecu.startBackgroundTask();

    if (_preferredOledParam != "none") {
        logMessage(1, "[SYS] Starting Polling (OLED Requirement)");
        _ecu.startSensorPolling();
    }

    if (_network == nullptr) {
        _network = new NetworkService(*this);
        _network->begin();
    }

    // Start Main Task
    xTaskCreatePinnedToCore(_broadcastTask, "Broadcast", 4096, this, 2, &_broadcastTaskHandle, 1);
    
    #ifdef USE_OLED_DISPLAY
    _oled.startTask();
    #endif
    
    logMessage(1, "[SYS] Ready.");
}

void AppController::_loadPreferences() {
    _preferences.begin("honda-ecu", false);
    _ecu.setSensorRequestInterval(_preferences.getUInt("sensorInterval", DEFAULT_SENSOR_INTERVAL));
    _ecu.setTableViewerInterval(_preferences.getUInt("tableInterval", DEFAULT_TABLE_INTERVAL));
    _preferredOledParam = _preferences.getString("oledParam", "gear");
    _preferences.end();
}

void AppController::_kwpLogWrapper(uint8_t level, const char* msg) {
    logMessage(level, msg);
}

void AppController::_onDataReadyWrapper() {
    if (_instance && _instance->_dataReadySemaphore) {
        xSemaphoreGive(_instance->_dataReadySemaphore);
    }
}

void AppController::handleClientConnect(AsyncWebSocketClient* client) {
    logMessage(1, "[WS] Client #%u connected", client->id());
    StaticJsonDocument<1024> doc;
    doc["type"] = "status";
    doc["connected"] = _ecu.isConnected();
    
    _preferences.begin("honda-ecu", true);
    doc["sensorInterval"] = _preferences.getUInt("sensorInterval", DEFAULT_SENSOR_INTERVAL);
    doc["tableInterval"] = _preferences.getUInt("tableInterval", DEFAULT_TABLE_INTERVAL);
    _preferences.end();

    #ifdef USE_OLED_DISPLAY
    _oled.getSettingsJson(doc);
    #endif

    JsonArray ratios = doc.createNestedArray("gearRatios");
    const GearRatio* currentRatios = _gearCalc.getRatios();
    for (int i = 0; i < NUM_GEARS; ++i) {
        JsonObject ratio = ratios.createNestedObject();
        ratio["min"] = currentRatios[i].min;
        ratio["max"] = currentRatios[i].max;
    }
    
    String out; serializeJson(doc, out);
    client->text(out);
}

void AppController::handleClientMessage(uint8_t* data, size_t len) {
    StaticJsonDocument<512> request;
    DeserializationError error = deserializeJson(request, data, len);
    if (error) return;
    const char* action = request["action"];
    if (action) _processJsonCommand(action, request);
}

void AppController::_processJsonCommand(const char* action, JsonDocument& request) {
    logMessage(2, "[WS] Action: %s", action);

    if (strcmp(action, "setMode") == 0) {
        const char* mode = request["mode"];
        if (strcmp(mode, "liveData") == 0 || strcmp(mode, "gearIndicator") == 0) {
            _ecu.startSensorPolling();
        } else if (strcmp(mode, "tableViewer") == 0) {
            _ecu.startTableViewer((uint8_t)strtol(request["table"], NULL, 16));
        } else {
            if (_preferredOledParam != "none") _ecu.startSensorPolling();
            else _ecu.stopPolling();
        }
    } 
    else if (strcmp(action, "setSensorInterval") == 0) {
        uint32_t val = request["interval"];
        _ecu.setSensorRequestInterval(val);
        _preferences.begin("honda-ecu", false); _preferences.putUInt("sensorInterval", val); _preferences.end();
    }
    else if (strcmp(action, "setTableInterval") == 0) {
        uint32_t val = request["interval"];
        _ecu.setTableViewerInterval(val);
        _preferences.begin("honda-ecu", false); _preferences.putUInt("tableInterval", val); _preferences.end();
    }
    else if (strcmp(action, "startGearRecording") == 0) {
        _gearCalc.startRecording(request["gear"]);
    }
    else if (strcmp(action, "stopGearRecording") == 0) {
        _gearCalc.stopRecording();
    }
    else if (strcmp(action, "setGearRatios") == 0) {
        GearRatio newRatios[NUM_GEARS];
        JsonArray arr = request["ratios"];
        for(int i=0; i<NUM_GEARS; ++i) { newRatios[i].min = arr[i]["min"]; newRatios[i].max = arr[i]["max"]; }
        _gearCalc.setRatios(newRatios);
    }
    #ifdef USE_OLED_DISPLAY
    else if (strcmp(action, "setOledParam") == 0) {
        const char* val = request["value"];
        _oled.setParameter(val);
        _preferredOledParam = String(val);
        if (_preferredOledParam != "none" && _ecu.getCurrentMode() == MODE_IDLE) _ecu.startSensorPolling();
        else if (_preferredOledParam == "none" && _ecu.getCurrentMode() == MODE_LIVE_DATA) _ecu.stopPolling();
    }
    else if (strcmp(action, "setOledRate") == 0) _oled.setRefreshRate(request["value"]);
    else if (strcmp(action, "setOledBrightness") == 0) _oled.setBrightness(request["value"]);
    #endif
}

void AppController::_broadcastTask(void* pvParameters) {
    AppController* self = (AppController*)pvParameters;
    bool lastConnState = false;
    LiveDataPacket pkt;
    esp_task_wdt_add(NULL);

    while(true) {
        if (xSemaphoreTake(self->_dataReadySemaphore, pdMS_TO_TICKS(WS_PING_INTERVAL_MS)) == pdTRUE) {
            bool currConnState = self->_ecu.isConnected();
            if (currConnState != lastConnState) {
                lastConnState = currConnState;
                #ifdef USE_OLED_DISPLAY
                self->_oled.updateConnectionStatus(currConnState);
                #endif
                StaticJsonDocument<128> doc;
                doc["type"] = "status";
                doc["connected"] = currConnState;
                if (!currConnState) doc["reason"] = "ECU_DISCONNECTED";
                String s; serializeJson(doc, s); 
                self->_network->getWebSocket().textAll(s);
            }

            if (currConnState) {
                self->_ecu.getLiveDataSnapshot(pkt);
                int gear = self->_gearCalc.calculate(pkt);
                #ifdef USE_OLED_DISPLAY
                self->_oled.updateGear(gear);
                #endif

                if (self->_gearCalc.isRecording()) self->_gearCalc.updateRecording(pkt);

                if (self->_network->getWebSocket().count() > 0) {
                    if (self->_ecu.getCurrentMode() == MODE_LIVE_DATA || self->_ecu.getCurrentMode() == MODE_GEAR_INDICATOR) {
                        StaticJsonDocument<1024> doc;
                        doc["type"] = "liveData";
                        doc["rpm"] = pkt.rpm; 
                        doc["speed"] = pkt.speed;
                        doc["tps"] = pkt.tps; 
                        doc["ect"] = pkt.ect; 
                        doc["iat"] = pkt.iat; 
                        doc["map"] = pkt.map;
                        doc["batt"] = pkt.batteryVoltage; 
                        doc["tpsV"] = pkt.tpsVoltage; 
                        doc["ectV"] = pkt.ectVoltage; 
                        doc["iatV"] = pkt.iatVoltage; 
                        doc["mapV"] = pkt.mapVoltage; 
                        doc["engineState"] = pkt.isEngineRunning;
                        doc["gearState"] = (int)pkt.gearState; 
                        doc["gear"] = gear;
                        
                        if (self->_gearCalc.isRecording()) {
                            JsonArray r = doc.createNestedArray("gearRatios");
                            const GearRatio* cr = self->_gearCalc.getRatios();
                            for(int i=0; i<NUM_GEARS; ++i) { 
                                JsonObject o = r.createNestedObject(); 
                                o["min"] = cr[i].min; 
                                o["max"] = cr[i].max; 
                            }
                        }
                        String s; serializeJson(doc, s); self->_network->getWebSocket().textAll(s);
                    } 
                    else if (self->_ecu.getCurrentMode() == MODE_TABLE_VIEWER && self->_ecu.isNewTableDataAvailable()) {
                        uint8_t len = self->_ecu.getTableData(self->_tableDataBuffer, 256);
                        if (len > 0) {
                             String hexS = "";
                             for (int i=0; i<len; i++) { char h[4]; sprintf(h, "%02X ", self->_tableDataBuffer[i]); hexS += h; }
                             hexS.trim();
                             StaticJsonDocument<1024> doc; doc["type"] = "tableData"; doc["data"] = hexS;
                             String s; serializeJson(doc, s); self->_network->getWebSocket().textAll(s);
                        }
                    }
                }
            }
        } else {
             if (self->_network->getWebSocket().count() > 0) {
                self->_network->getWebSocket().textAll("{\"type\":\"ping\"}");
             }
        }
        esp_task_wdt_reset();
    }
}