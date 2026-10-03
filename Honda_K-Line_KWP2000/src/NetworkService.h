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
 * @file NetworkService.h
 * @brief Manages WiFi connectivity and the Web Server interface.
 * 
 * This file contains the NetworkService class, which handles the ESP32's 
 * WiFi Station mode, manages automatic reconnection via a background task, 
 * and sets up the Asynchronous Web Server and WebSocket endpoints for 
 * frontend communication.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

/** 
 * @brief Forward declaration to resolve circular dependency.
 * 
 * NetworkService needs a reference to AppController to dispatch incoming
 * WebSocket messages, while AppController owns NetworkService.
 */
class AppController; 

/**
 * @class NetworkService
 * @brief Handles Network I/O, WiFi lifecycle, and Web Server routing.
 * 
 * This class abstracts the networking layer. It ensures the device remains 
 * connected to the configured WiFi access point and routes incoming 
 * WebSocket JSON packets to the AppController for processing.
 */
class NetworkService {
public:
    /**
     * @brief Constructor.
     * 
     * @param app Reference to the main application controller. Used to callback
     *            when data is received from clients.
     */
    NetworkService(AppController& app);

    /**
     * @brief Starts the WiFi connection and Web Server.
     * 
     * Initiates the connection to the AP defined in Config.h, starts the 
     * mDNS responder, and launches the FreeRTOS background task responsible 
     * for monitoring connection health and reconnecting if necessary.
     */
    void begin();

    /**
     * @brief Accessor for the WebSocket instance.
     * 
     * Used by AppController to broadcast data to all connected clients.
     * 
     * @return Reference to the active AsyncWebSocket object.
     */
    AsyncWebSocket& getWebSocket();

private:
    AppController& _app;        ///< Reference to the main controller for callbacks.
    AsyncWebServer _server;     ///< The asynchronous HTTP web server instance.
    AsyncWebSocket _ws;         ///< The WebSocket handler instance (path: /ws).
    
    /**
     * @brief System event callback for WiFi status changes.
     * 
     * Logs connection success or disconnection events to the system log.
     * 
     * @param event The WiFi event ID (e.g., GOT_IP, DISCONNECTED).
     */
    static void _wifiEventCallback(WiFiEvent_t event);

    /**
     * @brief FreeRTOS task for connection resilience.
     * 
     * Periodically checks WiFi status and attempts to reconnect if the 
     * connection is lost, blocking only its own thread.
     * 
     * @param param Unused task parameter.
     */
    static void _reconnectTask(void* param);

    /**
     * @brief Internal handler for WebSocket events.
     * 
     * Routes connection and data events to the AppController.
     * 
     * @param server Pointer to the server instance.
     * @param client Pointer to the specific client triggering the event.
     * @param type Event type (Connect, Disconnect, Data).
     * @param arg Additional argument (unused).
     * @param data Pointer to the payload buffer.
     * @param len Length of the payload.
     */
    void _onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
};