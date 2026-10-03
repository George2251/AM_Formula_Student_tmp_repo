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


document.addEventListener('DOMContentLoaded', () => {
    
    // --- Configuration & Constants ---
    const Config = {
        WS_URL: `ws://${window.location.hostname}/ws`,
        TIMEOUTS: {
            PING: 30000,
            WARN: 25000,
            RECONNECT_INITIAL: 2000,
            RECONNECT_MAX: 10000
        },
        STATS: {
            HISTORY_LENGTH: 50
        },
        OLED: {
            DEFAULT_PARAM: 'gear'
        }
    };

    /**
     * NetworkManager: Handles WebSocket communication, heartbeat, and resilience.
     */
    class NetworkManager {
        constructor(onMessage, onStatusChange) {
            this.ws = null;
            this.reconnectDelay = Config.TIMEOUTS.RECONNECT_INITIAL;
            this.timers = { ping: null, warn: null };
            this.onMessage = onMessage;
            this.onStatusChange = onStatusChange;
        }

        connect() {
            this.ws = new WebSocket(Config.WS_URL);
            
            this.ws.onopen = () => {
                console.log('[Net] Connected');
                this.reconnectDelay = Config.TIMEOUTS.RECONNECT_INITIAL;
                this.startHeartbeat();
            };

            this.ws.onclose = () => {
                console.log('[Net] Closed');
                this.handleDisconnect();
            };

            this.ws.onerror = (err) => {
                console.error('[Net] Error:', err);
                this.ws.close();
            };

            this.ws.onmessage = (event) => {
                this.startHeartbeat(); // Reset watchdog on any activity
                try {
                    const msg = JSON.parse(event.data);
                    if (msg.type !== 'ping') this.onMessage(msg);
                } catch (e) {
                    console.error('[Net] Parse error', e);
                }
            };
        }

        send(payload) {
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                this.ws.send(JSON.stringify(payload));
            }
        }

        handleDisconnect() {
            const retrySec = Math.round(this.reconnectDelay / 1000);
            this.onStatusChange(false, 'DISCONNECTED', `Reconnecting in ${retrySec}s...`);
            
            this.clearTimers();
            
            setTimeout(() => this.connect(), this.reconnectDelay);
            this.reconnectDelay = Math.min(this.reconnectDelay * 1.5, Config.TIMEOUTS.RECONNECT_MAX);
        }

        startHeartbeat() {
            this.clearTimers();
            // Dispatch event to UI to clear warning styles
            document.dispatchEvent(new CustomEvent('heartbeat-ok'));

            this.timers.warn = setTimeout(() => {
                document.dispatchEvent(new CustomEvent('heartbeat-warn'));
            }, Config.TIMEOUTS.WARN);

            this.timers.ping = setTimeout(() => {
                console.warn('[Net] Ping Timeout');
                this.ws.close();
            }, Config.TIMEOUTS.PING);
        }

        clearTimers() {
            clearTimeout(this.timers.ping);
            clearTimeout(this.timers.warn);
        }
    }

/**
     * UIManager: Handles all DOM interactions, Screen switching, and Data rendering.
     */
    class UIManager {
        constructor(network) {
            this.net = network;
            this.cacheDOM();
            this.bindEvents();
            
            // Internal State
            this.stats = { lastTime: null, history: [] };
            this.recordingState = { isRecording: false, gear: null };
        }

        cacheDOM() {
            this.dom = {
                status: document.getElementById('connection-status'),
                statusText: document.querySelector('#connection-status span'),
                heartbeat: document.getElementById('heartbeat-indicator'),
                screens: document.querySelectorAll('.screen'),
                navButtons: document.querySelectorAll('[data-screen]'),
                gearDisplay: document.getElementById('gear-display'),
                // Groups inputs with their value labels
                sliders: {
                    sensor: { 
                        input: document.getElementById('sensor-interval-slider'), 
                        label: document.getElementById('sensor-interval-value') 
                    },
                    table: { 
                        input: document.getElementById('table-interval-slider'), 
                        label: document.getElementById('table-interval-value') 
                    },
                    oledBright: { 
                        input: document.getElementById('oled-brightness-slider'), 
                        label: document.getElementById('oled-brightness-value') 
                    },
                    oledRate: { 
                        input: document.getElementById('oled-refresh-rate'), 
                        label: document.getElementById('oled-refresh-value') 
                    }
                },
                stats: {
                    last: document.getElementById('stat-last'),
                    avg: document.getElementById('stat-avg'),
                    pps: document.getElementById('stat-pps')
                },
                liveData: {
                    rpm: document.getElementById('data-rpm'),
                    speed: document.getElementById('data-speed'),
                    tps: document.getElementById('data-tps'),
                    tpsV: document.getElementById('data-tpsV'),
                    ect: document.getElementById('data-ect'),
                    ectV: document.getElementById('data-ectV'),
                    iat: document.getElementById('data-iat'),
                    iatV: document.getElementById('data-iatV'),
                    map: document.getElementById('data-map'),
                    mapV: document.getElementById('data-mapV'),
                    batt: document.getElementById('data-batt'),
                    engine: document.getElementById('data-engineState')
                },
                tableGrid: document.getElementById('table-viewer-grid'),
                tableSelect: document.getElementById('table-select'),
                recordButtons: document.querySelectorAll('.button-rec'),
                saveRatiosBtn: document.getElementById('btn-save-ratios')
            };
        }

        bindEvents() {
            // --- Navigation ---
            this.dom.navButtons.forEach(btn => {
                btn.addEventListener('click', () => this.switchScreen(btn.dataset.screen));
            });

            document.querySelectorAll('.back-button').forEach(btn => {
                btn.addEventListener('click', () => {
                    this.stopRecordingIfActive();
                    this.switchScreen('screen-main-menu');
                });
            });

            // --- Sliders (FIXED) ---
            const setupSlider = (sliderObj, action, payloadKey, suffix = ' ms') => {
                sliderObj.input.addEventListener('input', (e) => {
                    sliderObj.label.textContent = `${e.target.value}${suffix}`;
                });                
                sliderObj.input.addEventListener('change', (e) => {
                    this.net.send({ 
                        action: action, 
                        [payloadKey]: parseInt(e.target.value) 
                    });
                });
            };

            // Apply logic to all sliders
            setupSlider(this.dom.sliders.sensor, 'setSensorInterval', 'interval');
            setupSlider(this.dom.sliders.table, 'setTableInterval', 'interval');
            setupSlider(this.dom.sliders.oledRate, 'setOledRate', 'value');
            setupSlider(this.dom.sliders.oledBright, 'setOledBrightness', 'value', ''); // No suffix for brightness

            // --- OLED Params ---
            document.querySelectorAll('input[name="oled_param"]').forEach(radio => {
                radio.addEventListener('change', (e) => this.net.send({ action: 'setOledParam', value: e.target.value }));
            });

            // --- Table Selection ---
            this.dom.tableSelect.addEventListener('change', (e) => {
                this.net.send({ action: 'setMode', mode: 'tableViewer', table: e.target.value });
            });

            // --- Recording Logic ---
            this.dom.recordButtons.forEach(btn => {
                btn.addEventListener('click', () => this.handleRecordClick(btn));
            });

            this.dom.saveRatiosBtn.addEventListener('click', () => this.saveGearRatios());

            // --- Heartbeat Visuals ---
            document.addEventListener('heartbeat-warn', () => this.dom.heartbeat.className = 'heartbeat-warn');
            document.addEventListener('heartbeat-ok', () => {
                this.dom.heartbeat.className = '';
                this.triggerHeartbeatAnim();
            });
        }

        // --- View Logic ---

        switchScreen(screenId) {
            this.dom.screens.forEach(s => s.classList.remove('active'));
            const target = document.getElementById(screenId);
            if(target) target.classList.add('active');
            sessionStorage.setItem('lastScreenId', screenId);

            // Determine Mode
            let payload = { action: 'setMode', mode: 'idle' };
            if (screenId === 'screen-live-data') payload.mode = 'liveData';
            else if (screenId === 'screen-gear-indicator') payload.mode = 'gearIndicator';
            else if (screenId === 'screen-table-viewer') {
                payload.mode = 'tableViewer';
                payload.table = this.dom.tableSelect.value;
            }
            this.net.send(payload);
        }

        updateStatus(connected, reason, customMsg) {
            if (customMsg) {
                this.dom.status.className = 'status-reconnecting';
                this.dom.statusText.textContent = customMsg;
            } else if (connected) {
                this.dom.status.className = 'status-connected';
                this.dom.statusText.textContent = 'ONLINE';
            } else {
                this.dom.status.className = 'status-disconnected';
                this.dom.statusText.textContent = (reason === 'ECU_DISCONNECTED') ? 'ECU OFFLINE' : 'OFFLINE';
                this.resetDataDisplay();
            }
        }

        triggerHeartbeatAnim() {
            this.dom.heartbeat.classList.add('heartbeat-active');
            setTimeout(() => this.dom.heartbeat.classList.remove('heartbeat-active'), 150);
        }

        // --- Data Rendering ---

        processLiveData(data) {
            this.calculateStats();
            
            const map = this.dom.liveData;
            map.rpm.textContent = data.rpm;
            map.speed.textContent = data.speed;
            map.tps.textContent = data.tps.toFixed(1);
            map.tpsV.textContent = data.tpsV.toFixed(2);
            map.ect.textContent = data.ect.toFixed(0);
            map.ectV.textContent = data.ectV.toFixed(2);
            map.iat.textContent = data.iat.toFixed(0);
            map.iatV.textContent = data.iatV.toFixed(2);
            map.map.textContent = data.map.toFixed(0);
            map.mapV.textContent = data.mapV.toFixed(2);
            map.batt.textContent = data.batt.toFixed(1);
            map.engine.textContent = data.engineState ? 'Running' : 'Stopped';

            this.updateGearDisplay(data.gear);

            if (data.gearRatios) this.fillGearInputs(data.gearRatios);
        }

        updateGearDisplay(gear) {
            const map = { '-2': '-', '-1': 'C', '0': 'N' };
            this.dom.gearDisplay.textContent = map[gear] || (gear > 0 ? gear : '?');
        }

        renderTable(data) {
            this.dom.tableGrid.innerHTML = '';
            if (!data.data) {
                this.dom.tableGrid.innerHTML = '<p class="help-text">Polling...</p>';
                return;
            }
            const fragment = document.createDocumentFragment();
            const bytes = data.data.trim().split(' ');
            
            bytes.forEach((byte, i) => {
                if(!byte) return;
                const card = document.createElement('div');
                card.className = 'card data-item';
                card.innerHTML = `<h2>Byte ${i}</h2><p class="value">0x${byte}</p>`;
                fragment.appendChild(card);
            });
            this.dom.tableGrid.appendChild(fragment);
        }

        // --- Settings & Forms ---

        applyInitialSettings(msg) {
            const s = this.dom.sliders;
            if (msg.sensorInterval) { s.sensor.input.value = msg.sensorInterval; s.sensor.label.textContent = `${msg.sensorInterval} ms`; }
            if (msg.tableInterval) { s.table.input.value = msg.tableInterval; s.table.label.textContent = `${msg.tableInterval} ms`; }
            if (msg.oledBrightness) { s.oledBright.input.value = msg.oledBrightness; s.oledBright.label.textContent = msg.oledBrightness; }
            if (msg.oledRate) { s.oledRate.input.value = msg.oledRate; s.oledRate.label.textContent = `${msg.oledRate} ms`; }
            
            if (msg.oledParam) {
                const radio = document.querySelector(`input[name="oled_param"][value="${msg.oledParam}"]`);
                if (radio) radio.checked = true;
            }
            if (msg.gearRatios) this.fillGearInputs(msg.gearRatios);
        }

        fillGearInputs(ratios) {
            ratios.forEach((r, i) => {
                const num = i + 1;
                const minEl = document.getElementById(`g${num}-min`);
                const maxEl = document.getElementById(`g${num}-max`);
                if(minEl) minEl.value = r.min.toFixed(2);
                if(maxEl) maxEl.value = r.max.toFixed(2);
            });
        }

        // --- Gear Recording Logic ---

        handleRecordClick(btn) {
            if (this.recordingState.isRecording) {
                this.stopRecordingIfActive();
            } else {
                const gear = parseInt(btn.dataset.gear);
                this.recordingState = { isRecording: true, gear };
                this.net.send({ action: 'startGearRecording', gear });
                
                btn.textContent = 'Stop';
                btn.classList.add('recording');
                this.dom.recordButtons.forEach(b => {
                    if (parseInt(b.dataset.gear) !== gear) b.disabled = true;
                });
            }
        }

        stopRecordingIfActive() {
            if (!this.recordingState.isRecording) return;
            
            this.net.send({ action: 'stopGearRecording' });
            this.recordingState = { isRecording: false, gear: null };
            
            this.dom.recordButtons.forEach(b => {
                b.disabled = false;
                b.classList.remove('recording');
                b.textContent = 'Record';
            });
        }

        saveGearRatios() {
            const ratios = [];
            for (let i = 1; i <= 6; i++) {
                ratios.push({
                    min: parseFloat(document.getElementById(`g${i}-min`).value),
                    max: parseFloat(document.getElementById(`g${i}-max`).value)
                });
            }
            this.net.send({ action: 'setGearRatios', ratios });
        }

        // --- Statistics ---

        calculateStats() {
            const now = performance.now();
            if (this.stats.lastTime) {
                const delta = now - this.stats.lastTime;
                this.stats.history.push(delta);
                if (this.stats.history.length > 50) this.stats.history.shift();

                const avg = this.stats.history.reduce((a, b) => a + b, 0) / this.stats.history.length;
                
                this.dom.stats.last.textContent = Math.round(delta);
                this.dom.stats.avg.textContent = Math.round(avg);
                this.dom.stats.pps.textContent = (1000 / avg).toFixed(1);
            }
            this.stats.lastTime = now;
        }

        resetDataDisplay() {
            Object.values(this.dom.liveData).forEach(el => el.textContent = '---');
            this.dom.gearDisplay.textContent = '?';
            this.stats.lastTime = null;
            this.stats.history = [];
            this.dom.stats.last.textContent = '---';
            this.dom.stats.avg.textContent = '---';
            this.dom.stats.pps.textContent = '---';
        }
    }

    /**
     * App Controller: Initializes and glues components.
     */
    class App {
        constructor() {
            this.ui = null;
            this.net = new NetworkManager(
                this.handleMessage.bind(this),
                this.handleStatusChange.bind(this)
            );
        }

        init() {
            this.ui = new UIManager(this.net);
            
            // Restore last screen
            const lastScreen = sessionStorage.getItem('lastScreenId') || 'screen-main-menu';
            this.ui.switchScreen(lastScreen);
            
            this.net.connect();
        }

        handleStatusChange(connected, reason, msg) {
            this.ui.updateStatus(connected, reason, msg);
        }

        handleMessage(msg) {
            switch (msg.type) {
                case 'status':
                    this.ui.updateStatus(msg.connected, msg.reason);
                    this.ui.applyInitialSettings(msg);
                    break;
                case 'liveData':
                    this.ui.processLiveData(msg);
                    break;
                case 'tableData':
                    this.ui.renderTable(msg);
                    break;
            }
        }
    }

    // --- Entry Point ---
    const app = new App();
    app.init();

});