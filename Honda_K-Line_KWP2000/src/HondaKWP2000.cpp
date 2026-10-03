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
 * @file HondaKWP2000.cpp
 * @brief Implementation of the HondaKWP2000 library.
 * @version 14.0 (Gear Indicator Feature)
 */

#include "HondaKWP2000.h"
#include <cstdarg>
#include <esp_task_wdt.h>

// K-Line Log Tag
static const char* KLINE_TAG = "[KLINE]";
static const char* SYS_TAG = "[SYS]";

// --- Protocol Constants ---
const uint8_t WAKEUP_MSG[] = {0xFE, 0x04, 0xFF, 0xFF};
const uint8_t INIT_MSG[] = {0x72, 0x05, 0x00, 0xF0, 0x99};
const uint8_t EXPECTED_INIT_RESPONSE_MSG[] = {0x02, 0x04, 0x00, 0xFA};

#define HEADER_LEN 4
#define TABLE_CHOSEN_AS_KEEP_ALIVE_MESSAGE 0x00
#define TABLE_10 0x10
#define TABLE_D1 0xD1
#define REQ_DEST_ADDR 0x72
#define REQ_ALL_TABLE_CONTENTS 0x71
#define T10_OFFSET_RPM_H 0
#define T10_OFFSET_RPM_L 1
#define T10_OFFSET_TPS_VOLTAGE 2
#define T10_OFFSET_TPS_PERCENT 3
#define T10_OFFSET_ECT_VOLTAGE 4
#define T10_OFFSET_ECT_DEGC 5
#define T10_OFFSET_IAT_VOLTAGE 6
#define T10_OFFSET_IAT_DEGC 7
#define T10_OFFSET_MAP_VOLTAGE 8
#define T10_OFFSET_MAP_KPA 9
#define T10_OFFSET_BATT_VOLT 12
#define T10_OFFSET_SPEED_KMH 13
#define TD1_OFFSET_CLUTCH_INGEAR_KICKSTAND 0
#define TD1_OFFSET_IS_ENGINE_RUNNING 4

HondaKWP2000::HondaKWP2000(HardwareSerial& serial, uint8_t rxPin, uint8_t txPin)
    : _serial(serial), _rxPin(rxPin), _txPin(txPin), _activeBufferIndex(0) {}

void HondaKWP2000::begin() {
    _dataMutex = xSemaphoreCreateMutex();
    _serialMutex = xSemaphoreCreateMutex();
}


void HondaKWP2000::setDebug(KWPLogCallback callback, uint8_t level) {
    _logCallback = callback;
    _debugLevel = level;
}


void HondaKWP2000::disableDebug() { 
    _logCallback = nullptr;
    _debugLevel = DEBUG_LEVEL_NONE; 
}


void HondaKWP2000::startBackgroundTask() {
    if (_taskRunning) {
        _log(DEBUG_LEVEL_DEFAULT, "%s Background task already running.", SYS_TAG);
        return;
    }
    _taskRunning = true;
    _log(DEBUG_LEVEL_DEFAULT, "%s Starting KWP background task on Core 0...", SYS_TAG);
    xTaskCreatePinnedToCore(kwpTask, "KWP_Task", 4096, this, 3, &_taskHandle, 0);
}


void HondaKWP2000::stopBackgroundTask() {
    if (!_taskRunning) return;
    _log(DEBUG_LEVEL_DEFAULT, "%s Requesting KWP background task to stop...", SYS_TAG);
    _taskRunning = false;
    
    if (_taskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(250)); 
    }
    _taskHandle = NULL;
}


void HondaKWP2000::setActivityCallbacks(KWPActivityCallback txCallback, KWPActivityCallback rxCallback) {
    _txCallback = txCallback;
    _rxCallback = rxCallback;
}


void HondaKWP2000::setDataReadyCallback(KWPDataReadyCallback dataReadyCallback) {
    _dataReadyCallback = dataReadyCallback;
}


void HondaKWP2000::startSensorPolling() {
    _log(DEBUG_LEVEL_DEFAULT, "%s [KWP Task] Mode changed to LIVE DATA", SYS_TAG);
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    _currentMode = MODE_LIVE_DATA; 
    xSemaphoreGive(_dataMutex);
}


void HondaKWP2000::startTableViewer(uint8_t table) {
    _log(DEBUG_LEVEL_DEFAULT, "%s [KWP Task] Mode changed to TABLE VIEWER for table 0x%02X", SYS_TAG, table);
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    _tableViewTargetTable = table;
    _currentMode = MODE_TABLE_VIEWER;
    _isNewTableDataAvailable = false; 
    xSemaphoreGive(_dataMutex);
}


void HondaKWP2000::stopPolling() {
    _log(DEBUG_LEVEL_DEFAULT, "%s [KWP Task] Mode changed to IDLE", SYS_TAG);
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    _currentMode = MODE_IDLE;
    xSemaphoreGive(_dataMutex);
}


void HondaKWP2000::setSensorRequestInterval(uint32_t intervalMs) { _sensorRequestInterval = (intervalMs < 50) ? 50 : intervalMs; }
void HondaKWP2000::setTableViewerInterval(uint32_t intervalMs) { _tableViewerIntervalMs = (intervalMs < 100) ? 100 : intervalMs; }


void HondaKWP2000::kwpTask(void* objectPtr) {
    HondaKWP2000* self = (HondaKWP2000*)objectPtr;
    TickType_t lastWakeTime = xTaskGetTickCount();

    esp_task_wdt_add(NULL);
    self->_log(DEBUG_LEVEL_TRACE, "%s [KWP_Task] Watchdog starded watching over the kwpTask.", SYS_TAG);

    while (self->_taskRunning) {
        esp_task_wdt_reset();
        bool communication_success = true;
        uint32_t delayMs = 1000;

        if (!self->_isConnected) {
            self->_isReconnecting = true;
            self->_log(DEBUG_LEVEL_DEFAULT, "%s Not connected. Attempting to establish K-Line connection...", KLINE_TAG);

            if (xSemaphoreTake(self->_serialMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                KWPConnectionStatus status = self->performConnect_unsafe();
                self->_lastConnectionStatus = status;
                if (status == CONN_STATUS_OK) {
                    self->_log(DEBUG_LEVEL_DEFAULT, "%s K-Line connection established successfully.", KLINE_TAG);
                    self->_consecutiveErrors = 0;
                } else {
                    self->_log(DEBUG_LEVEL_DEFAULT, "%s K-Line connection failed. Will retry...", KLINE_TAG);
                }
                xSemaphoreGive(self->_serialMutex);
            } else {
                self->_log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Failed to get serial lock for reconnect.", KLINE_TAG);
                self->_lastConnectionStatus = CONN_ERROR_SERIAL_LOCK;
            }
            self->_isReconnecting = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        KWPOperatingMode currentMode;
        
        xSemaphoreTake(self->_dataMutex, portMAX_DELAY);
        currentMode = self->_currentMode;
        xSemaphoreGive(self->_dataMutex);

        switch (currentMode) {
            case MODE_GEAR_INDICATOR:
            case MODE_LIVE_DATA: {
                delayMs = self->_sensorRequestInterval;
                bool t10_success = false;
                bool d1_success = false;
                uint8_t producerBuffer = self->_activeBufferIndex.load() ^ 1;
                self->_liveDataBuffers[producerBuffer] = self->_liveDataBuffers[self->_activeBufferIndex.load()];

                if (xSemaphoreTake(self->_serialMutex, pdMS_TO_TICKS(WAITING_TIME_FOR_SEMAPHORE_TAKE)) == pdTRUE) {
                    if (self->readEntireTable_unsafe(TABLE_10)) {
                        self->parseTable10(self->_liveDataBuffers[producerBuffer], self->_responseBuffer, self->_responseLength);
                        t10_success = true;
                    }
                    xSemaphoreGive(self->_serialMutex);
                } else { self->_log(DEBUG_LEVEL_DEFAULT, "%s FAILED to acquire serial lock for reading and parsing T10.", KLINE_TAG); }

                if (xSemaphoreTake(self->_serialMutex, pdMS_TO_TICKS(WAITING_TIME_FOR_SEMAPHORE_TAKE)) == pdTRUE) {
                    if (self->readEntireTable_unsafe(TABLE_D1)) {
                        self->parseTableD1(self->_liveDataBuffers[producerBuffer], self->_responseBuffer, self->_responseLength);
                        d1_success = true;
                    }
                    xSemaphoreGive(self->_serialMutex);
                } else { self->_log(DEBUG_LEVEL_DEFAULT, "%s FAILED to acquire serial lock for reading and parsing TD1.", KLINE_TAG); }

                if (t10_success || d1_success) {
                    self->_activeBufferIndex.store(producerBuffer);
                    if (self->_dataReadyCallback) self->_dataReadyCallback();
                }
                communication_success = (t10_success || d1_success);
                break;
            }
            case MODE_TABLE_VIEWER: {
                delayMs = self->_tableViewerIntervalMs;
                 if (xSemaphoreTake(self->_serialMutex, pdMS_TO_TICKS(WAITING_TIME_FOR_SEMAPHORE_TAKE)) == pdTRUE) {
                    if (self->readEntireTable_unsafe(self->_tableViewTargetTable)) {
                        xSemaphoreTake(self->_dataMutex, portMAX_DELAY);
                        self->_rawTableViewBufferLength = self->_responseLength;
                        memcpy(self->_rawTableViewBuffer, self->_responseBuffer, self->_responseLength);
                        self->_isNewTableDataAvailable = true;
                        xSemaphoreGive(self->_dataMutex);
                        if (self->_dataReadyCallback) self->_dataReadyCallback();
                    } else { communication_success = false; }
                    xSemaphoreGive(self->_serialMutex);
                } else {
                     self->_log(DEBUG_LEVEL_DEFAULT, "%s FAILED to acquire serial lock for table viewer.", KLINE_TAG);
                     communication_success = false;
                }
                break;
            }
            case MODE_IDLE: {
                delayMs = KWP_KEEP_ALIVE_INTERVAL_MS;
                if (xSemaphoreTake(self->_serialMutex, pdMS_TO_TICKS(WAITING_TIME_FOR_SEMAPHORE_TAKE)) == pdTRUE) {
                    self->_log(DEBUG_LEVEL_VERBOSE, "%s Sending Keep-Alive...", KLINE_TAG);
                    if (!self->readEntireTable_unsafe(TABLE_CHOSEN_AS_KEEP_ALIVE_MESSAGE)) {
                        communication_success = false;
                    }
                     xSemaphoreGive(self->_serialMutex);
                } else {
                    self->_log(DEBUG_LEVEL_DEFAULT, "%s FAILED to acquire serial lock to send the keep-alive message.", KLINE_TAG);
                    communication_success = false;
                }
                break;
            }
        }
        if (communication_success) {
            if (self->_consecutiveErrors > 0) {
                 self->_log(DEBUG_LEVEL_DEFAULT, "%s Communication re-established after error.", KLINE_TAG);
            }
            self->_consecutiveErrors = 0;
        } else {
            self->_consecutiveErrors++;
            self->_log(DEBUG_LEVEL_DEFAULT, "%s WARNING: Communication error. Strike %d of %d.", KLINE_TAG, self->_consecutiveErrors, MAX_CONSECUTIVE_ERRORS);
            if (self->_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                self->_log(DEBUG_LEVEL_DEFAULT, "%s Too many consecutive errors. Marking as disconnected.", KLINE_TAG);
                self->_isConnected = false;
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(delayMs));
    }

    self->_log(DEBUG_LEVEL_DEFAULT, "%s KWP background task has stopped.", SYS_TAG);
    esp_task_wdt_delete(NULL);
    self->_taskHandle = NULL;
    vTaskDelete(NULL);
}


bool HondaKWP2000::isConnected() const {
    return _isConnected;
}


bool HondaKWP2000::isReconnecting() const {
    return _isReconnecting; 
}


KWPConnectionStatus HondaKWP2000::getLastConnectionStatus() { 
    return _lastConnectionStatus; 
}


KWPOperatingMode HondaKWP2000::getCurrentMode() {
    KWPOperatingMode operatingMode; 
    xSemaphoreTake(_dataMutex, portMAX_DELAY); 
    operatingMode = _currentMode; 
    xSemaphoreGive(_dataMutex); 
    return operatingMode; 
}


bool HondaKWP2000::isNewTableDataAvailable() { 
    return _isNewTableDataAvailable; 
}


uint8_t HondaKWP2000::getTableData(uint8_t* b, size_t s) {
    uint8_t c=0; 
    if(xSemaphoreTake(_dataMutex,pdMS_TO_TICKS(50)) == pdTRUE){
        if(_isNewTableDataAvailable){
            c = (_rawTableViewBufferLength>s) ?s : _rawTableViewBufferLength;
            memcpy(b,_rawTableViewBuffer,c);
            _isNewTableDataAvailable=false;
        }
    xSemaphoreGive(_dataMutex);
    }
    return c; 
}


void HondaKWP2000::getLiveDataSnapshot(LiveDataPacket& packet) {
    packet = _liveDataBuffers[_activeBufferIndex.load()];
}


void HondaKWP2000::parseTable10(LiveDataPacket& packet, const uint8_t* responseBytes, uint8_t length) { 
    if (length < HEADER_LEN + T10_OFFSET_SPEED_KMH + 1) 
        return;
    
    packet.rpm = (responseBytes[HEADER_LEN + T10_OFFSET_RPM_H] << 8) | responseBytes[HEADER_LEN + T10_OFFSET_RPM_L]; 
    packet.speed = responseBytes[HEADER_LEN + T10_OFFSET_SPEED_KMH];
    packet.tpsVoltage = responseBytes[HEADER_LEN + T10_OFFSET_TPS_VOLTAGE] * (5.0f / 256.0f);
    packet.ectVoltage = responseBytes[HEADER_LEN + T10_OFFSET_ECT_VOLTAGE] * (5.0f / 256.0f);
    packet.iatVoltage = responseBytes[HEADER_LEN + T10_OFFSET_IAT_VOLTAGE] * (5.0f / 256.0f);
    packet.mapVoltage = responseBytes[HEADER_LEN + T10_OFFSET_MAP_VOLTAGE] * (5.0f / 256.0f);
    packet.tps = responseBytes[HEADER_LEN + T10_OFFSET_TPS_PERCENT] / 16.0f * 10.0f;
    packet.ect = responseBytes[HEADER_LEN + T10_OFFSET_ECT_DEGC] - 40.0f;
    packet.iat = responseBytes[HEADER_LEN + T10_OFFSET_IAT_DEGC] - 40.0f;
    packet.map = responseBytes[HEADER_LEN + T10_OFFSET_MAP_KPA];
    packet.batteryVoltage = responseBytes[HEADER_LEN + T10_OFFSET_BATT_VOLT] / 10.0f; 
}


void HondaKWP2000::parseTableD1(LiveDataPacket& packet, const uint8_t* responseBytes, uint8_t length) { 
    if (length < HEADER_LEN + TD1_OFFSET_IS_ENGINE_RUNNING + 1) 
        return;
        
    uint8_t gearStatusByte = responseBytes[HEADER_LEN + TD1_OFFSET_CLUTCH_INGEAR_KICKSTAND]; 
    switch(gearStatusByte){
        case 0x00: packet.gearState=GEAR_STATE_IN_GEAR; break;
        case 0x01: packet.gearState=GEAR_STATE_NEUTRAL_OR_CLUTCH; break;
        case 0x03: packet.gearState=GEAR_STATE_KICKSTAND_DOWN; break;
        default:   packet.gearState=GEAR_STATE_UNKNOWN; break;
    } 
    packet.isEngineRunning=(responseBytes[HEADER_LEN + TD1_OFFSET_IS_ENGINE_RUNNING] == 0x01); 
}


KWPConnectionStatus HondaKWP2000::performConnect_unsafe() {
    _log(DEBUG_LEVEL_VERBOSE, "%s --- Starting Connection Init Sequence ---", KLINE_TAG);
    _serial.end();
    _log(DEBUG_LEVEL_VERBOSE, "%s Pulling K-Line low for %dms...", KLINE_TAG, K_LINE_INIT_LOW_MS);
    pullKLineLow_unsafe(K_LINE_INIT_LOW_MS);
    delay(K_LINE_INIT_HIGH_MS);
    _serial.begin(HONDA_BAUDRATE, SERIAL_8N1, _rxPin, _txPin);
    _log(DEBUG_LEVEL_DEFAULT, "%s Sending Wakeup Message...", KLINE_TAG);

    unsigned long responseTime;
    if (!sendRequest_unsafe(WAKEUP_MSG, sizeof(WAKEUP_MSG))) { 
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Failed to write Wakeup message to serial.", KLINE_TAG); 
        return CONN_ERROR_WAKEUP_FAIL; 
    }

    delay(K_LINE_WAKEUP_DELAY_MS);
    
    _log(DEBUG_LEVEL_DEFAULT, "%s Sending Init Message...", KLINE_TAG);
    if (!sendRequest_unsafe(INIT_MSG, sizeof(INIT_MSG))) { _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Failed to write Init message to serial.", KLINE_TAG); return CONN_ERROR_INIT_FAIL; }

    int len = receiveResponse_unsafe(INIT_MSG, sizeof(INIT_MSG), responseTime);
    
    if (len > 0) {
        if (len == sizeof(EXPECTED_INIT_RESPONSE_MSG) && memcmp(_responseBuffer, EXPECTED_INIT_RESPONSE_MSG, len) == 0) {
            _isConnected = true;
            return CONN_STATUS_OK;
        } else { 
            _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Init response packet is not valid.", KLINE_TAG); 
            _logHex(DEBUG_LEVEL_DEFAULT, " | Got:      ", _responseBuffer, len); 
            _logHex(DEBUG_LEVEL_DEFAULT, " | Expected: ", EXPECTED_INIT_RESPONSE_MSG, sizeof(EXPECTED_INIT_RESPONSE_MSG)); 
            return CONN_ERROR_RESPONSE_INVALID; }
    } 
    else if (len == -1) { return CONN_ERROR_ECHO_TIMEOUT; } 
    else if (len == -2) { return CONN_ERROR_RESPONSE_TIMEOUT; } 
    else if (len == -3) { return CONN_ERROR_RESPONSE_CHECKSUM; } 
    else if (len == -4) { return CONN_ERROR_INVALID_LENGTH; } 
    else { return CONN_ERROR_RESPONSE_TIMEOUT; }
}


void HondaKWP2000::pullKLineLow_unsafe(unsigned long durationMs) { 
    pinMode(_txPin, OUTPUT); 
    digitalWrite(_txPin, LOW); 
    delay(durationMs); 
    digitalWrite(_txPin, HIGH); 
}


bool HondaKWP2000::sendRequest_unsafe(const uint8_t* message, size_t length) {
    while (_serial.available()) _serial.read(); 
    _logHex(DEBUG_LEVEL_VERBOSE, "TX ->", message, length);
    if (_txCallback) _txCallback();
    size_t bytes_written = _serial.write(message, length);
    _serial.flush(); 
    return bytes_written == length;
}


int HondaKWP2000::receiveResponse_unsafe(const uint8_t* sentMessage, size_t sentLength, unsigned long &responseTime) {
    _responseLength = 0;
    unsigned long startTime = millis();
    _serial.setTimeout(300);
    uint8_t echoBuf[sentLength];
    if (_serial.readBytes(echoBuf, sentLength) != sentLength) {
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Timed out waiting for TX echo. K-Line may be stuck or disconnected.", KLINE_TAG);
        return -1;
    }
    startTime = millis(); 
    _serial.setTimeout(ECU_RESPONSE_HEADER_TIMEOUT_MS);
    if (_serial.readBytes(_responseBuffer, 2) != 2) {
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Timed out waiting for response header.", KLINE_TAG);
        return -2;
    }
    uint8_t expectedLength = _responseBuffer[1];
    _responseLength = 2;
    if (expectedLength < 3 || expectedLength > sizeof(_responseBuffer)) {
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Invalid packet length in header (0x%02X).", KLINE_TAG, expectedLength);
        _logHex(DEBUG_LEVEL_DEFAULT, " | RX Header:", _responseBuffer, 2);
        return -4;
    }
    size_t remainingBytes = expectedLength - 2;
    _serial.setTimeout(ECU_RESPONSE_BODY_TIMEOUT_MS);
    if (_serial.readBytes(_responseBuffer + 2, remainingBytes) != remainingBytes) {
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Timed out waiting for response body (Expected %d, Got less).", KLINE_TAG, remainingBytes);
        _logHex(DEBUG_LEVEL_DEFAULT, " | RX Body:", _responseBuffer, _responseLength + _serial.available());
        return -2;
    }
    _responseLength = expectedLength;
    responseTime = millis() - startTime;
    uint8_t calculated_cs = calculateChecksum(_responseBuffer, _responseLength - 1);
    uint8_t received_cs = _responseBuffer[_responseLength - 1];
    if (received_cs != calculated_cs) {
        _log(DEBUG_LEVEL_DEFAULT, "%s ERROR: Invalid checksum (Expected 0x%02X, Got 0x%02X).", KLINE_TAG, calculated_cs, received_cs);
        _logHex(DEBUG_LEVEL_DEFAULT, " | RX Body:", _responseBuffer, _responseLength);
        return -3;
    }
    _logHex(DEBUG_LEVEL_VERBOSE, "RX <-", _responseBuffer, _responseLength);
    _log(DEBUG_LEVEL_VERBOSE, "%s Response received in %lu ms", KLINE_TAG, responseTime);
    if (_rxCallback) _rxCallback();
    return _responseLength;
}


bool HondaKWP2000::readEntireTable_unsafe(uint8_t table) {
    uint8_t request[5] = {REQ_DEST_ADDR, 5, REQ_ALL_TABLE_CONTENTS, table, 0};
    request[4] = calculateChecksum(request, 4); 
    unsigned long responseTime;
    if (sendRequest_unsafe(request, sizeof(request))) {
        if (receiveResponse_unsafe(request, sizeof(request), responseTime) > 0) {
            return true;
        }
    }
    return false;
}


uint8_t HondaKWP2000::calculateChecksum(const uint8_t* data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum += data[i];
    return (uint8_t)(0x100 - (sum & 0xFF));
}


void HondaKWP2000::_log(uint8_t level, const char* format, ...) {
    if (_logCallback && _debugLevel >= level) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _logCallback(level, buffer);
    }
}


void HondaKWP2000::_logHex(uint8_t level, const char* prefix, const uint8_t* data, size_t length) {
    if (_logCallback && _debugLevel >= level) {
        char buffer[256];
        int offset = snprintf(buffer, sizeof(buffer), "%s [%zu bytes]: ", prefix, length);
        for (size_t i = 0; i < length && offset < sizeof(buffer); ++i) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%02X ", data[i]);
        }
        _logCallback(level, buffer);
    }
}