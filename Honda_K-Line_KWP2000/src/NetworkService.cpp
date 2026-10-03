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


#include "NetworkService.h"
#include "AppController.h"
#include "SystemStatus.h"
#include "Config.h"
#include <LittleFS.h>
#include <ESPmDNS.h>

NetworkService::NetworkService(AppController& app) : _app(app), _server(80), _ws("/ws") {}

void NetworkService::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(_wifiEventCallback);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    xTaskCreatePinnedToCore(_reconnectTask, "WiFiRecon", 4096, NULL, 1, NULL, 0);

    if (MDNS.begin(MDNS_NAME)) {
        logMessage(1, "[NET] MDNS started: http://%s.local", MDNS_NAME);
    }

    _ws.onEvent(std::bind(
                        &NetworkService::_onWebSocketEvent,
                        this, 
                        std::placeholders::_1, 
                        std::placeholders::_2, 
                        std::placeholders::_3, 
                        std::placeholders::_4, 
                        std::placeholders::_5, 
                        std::placeholders::_6
                        )
    );
    _server.addHandler(&_ws);
    _server.serveStatic("/", LittleFS, "/");
    _server.onNotFound([](AsyncWebServerRequest *req){ req->send(LittleFS, "/index.html", "text/html"); });
    _server.begin();
    logMessage(1, "[NET] Web Server started");
}

AsyncWebSocket& NetworkService::getWebSocket() { return _ws; }

void NetworkService::_wifiEventCallback(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: 
            logMessage(1, "[NET] Connected. IP: %s", WiFi.localIP().toString().c_str()); 
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: 
            logMessage(1, "[NET] Disconnected"); 
            break;
        default: break;
    }
}

void NetworkService::_reconnectTask(void* param) {
    while(true) {
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
            vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void NetworkService::_onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) _app.handleClientConnect(client);
    else if (type == WS_EVT_DATA) _app.handleClientMessage(data, len);
}