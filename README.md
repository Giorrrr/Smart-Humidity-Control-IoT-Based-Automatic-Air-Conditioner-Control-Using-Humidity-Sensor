# Smart-Humidity-Control-IoT-Based-Automatic-Air-Conditioner-Control-Using-Humidity-Sensor
An IoT system that periodically reads room humidity and controls a Daikin air conditioner via infrared (IR). When humidity exceeds the threshold, the AC is turned on in Dry mode. When humidity drops far enough below the threshold, the AC is turned off. Users can also monitor and control the system from a web/app interface over MQTT.

## 1. Features

- Temperature and humidity readings (DHT22) every 2 seconds.
- Publish to MQTT will updates every 5 seconds. 
- **Automatic mode**: AC ON (Dry mode) when `humidity > threshold`, AC OFF when `humidity < threshold - HYSTERESIS`.
- **Manual mode** from the web: turn the AC ON/OFF and choose **Cool** or **Dry** mode.
- Humidity threshold adjustable from the web (range 30-95 %RH).
- System status published to MQTT as JSON (retained).
- Non-blocking automatic MQTT reconnect, so sensor reading is never interrupted.

---

## 2. Hardware

| Device | Function |
|---|---|
| ESP32 (DevKit) | Main microcontroller, WiFi, MQTT client |
| DHT22 (3-pin module: `+`, `OUT`, `-`) | Temperature and humidity sensor |
| IR transmitter (HX53 module, or 940 nm IR LED + NPN transistor) | Sends commands to the Daikin AC |
| Daikin AC (protocol `DAIKIN152`) | Device being controlled |

### Libraries (Arduino IDE / Library Manager)

| Library | Purpose |
|---|---|
| `PubSubClient` (Nick O'Leary) | MQTT client |
| `IRremoteESP8266` | Sends Daikin152 IR signals (`ir_Daikin.h`) |
| `DHT sensor library` (Adafruit) | Reads the DHT22 |
| `Adafruit Unified Sensor` | Dependency of the DHT library |

---

## 3. Pins and Wiring

| Component | Component pin | ESP32 pin | Notes |
|---|---|---|---|
| DHT22 | `+` | VIN (5V) | Power |
| DHT22 | `-` | GND | Ground |
| DHT22 | `OUT` | **GPIO14** | Data (`DHT_PIN`) |
| IR transmitter | VCC | VIN (5V) | 5V gives longer range if the module has a transistor |
| IR transmitter | GND | GND | Ground |
| IR transmitter | DAT | **GPIO4** | IR output (`kIrLedPin`) |

**IR note:** a GPIO pin can only supply a small current, so IR range driven directly from the GPIO is about 10-50 cm. For roughly 2 m, use an NPN transistor driver (e.g. 2N2222/S8050) with a 1 kΩ base resistor and a 33-47 Ω LED resistor from 5V.

---

## 4. How the System Works

```
 DHT22 ──► ESP32 ──► (IR) ──► Daikin AC
             ▲  │
   commands  │  │ status (JSON)
             │  ▼
        MQTT Broker ◄──► Web / App
```

1. **Setup**: the ESP32 connects to WiFi, then configures the MQTT broker and the incoming-message callback.
2. **Loop** (runs continuously):
   1. Keeps the MQTT connection alive and processes incoming messages (`ensureMqtt()` and `mqtt.loop()`).
   2. Every `READ_INTERVAL` (2 seconds): reads temperature and humidity. If the read fails (`NaN`), the cycle is skipped.
   3. If `autoMode == true`, runs the automation logic (below).
   4. Every `PUBLISH_INTERVAL` (5 seconds): publishes the status to the `status` topic.

### Automation logic

```
if (autoMode):
    if humidity > threshold               and AC OFF  →  AC ON  (Dry mode)
    if humidity < threshold - HYSTERESIS  and AC ON   →  AC OFF
```

The IR signal is sent **only when the state changes** (not on every reading), so the AC is not flooded with repeated commands.

**Example** with `threshold = 70` and `HYSTERESIS = 25`:
- Humidity rises above 70 % → AC ON (Dry).
- Humidity between 45 % and 70 % → AC keeps its last state (hysteresis zone).
- Humidity falls below 45 % → AC OFF.

### Manual control rules (from the web)

- A `power` command **automatically disables auto mode**. Send `on` to the `auto` topic to re-enable it.
- A `mode` command while the AC is **ON** immediately resends IR with the new mode. While the AC is **OFF**, the mode is only stored and used the next time the AC is turned on.
- When auto mode turns the AC on, the mode is always **Dry**, regardless of the previously selected mode.

---

## 5. Parameters and Variables

### Constants (`const`)

| Name | Value | Description |
|---|---|---|
| `HYSTERESIS` | `25.0` (%RH) | How far below the threshold humidity must fall before the AC turns off. Prevents the AC from rapidly toggling ON/OFF around the threshold. A small value (e.g. 2-5) turns the AC off sooner; 2 is handy for testing |
| `READ_INTERVAL` | `2000` ms | Delay between DHT22 readings (the DHT22 needs at least 2 seconds) |
| `PUBLISH_INTERVAL` | `5000` ms | Delay between periodic status publications to MQTT |
| `MQTT_RETRY` | `5000` ms | Delay between MQTT reconnect attempts |

### Global variables (system state)

| Name | Type | Default | Description |
|---|---|---|---|
| `acOn` | `bool` | `false` | AC state: `true` = ON, `false` = OFF |
| `modeDry` | `bool` | `true` | AC mode: `true` = Dry, `false` = Cool |
| `autoMode` | `bool` | `true` | Whether humidity-based automation is active |
| `threshold` | `float` | `70.0` | Humidity limit (%RH); valid range 30-95 |
| `humidity`, `temperature` | `float` | `0` | Last valid sensor readings |

### AC settings when turned on (in `applyAC()`)

| Parameter | Value |
|---|---|
| Target temperature | 24 °C |
| Fan speed | Auto |
| Vertical swing | Off |
| Mode | Dry or Cool (according to `modeDry`) |

---

## 6. WiFi Connection

1. Fill in your credentials at the top of the code:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_NAME";
   const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
   ```
2. The ESP32 runs as a **station** (`WiFi.mode(WIFI_STA)`) and waits until connected (dots `.` are printed to the Serial Monitor).
3. Once connected, the IP address is printed: `WiFi OK: 192.168.x.x`.

Note: the ESP32 only supports **2.4 GHz** WiFi. Phone hotspots and dual-band routers must broadcast on 2.4 GHz.

---

## 7. MQTT Communication

### Architecture

MQTT uses a **publish/subscribe** pattern through a **broker**. The ESP32 and the web never talk to each other directly; both connect to the same broker and exchange messages through **topics**.

| Party | Broker connection | Role |
|---|---|---|
| ESP32 | TCP, port **1883** | Publishes to `status`; subscribes to all `cmd/*` topics |
| Web | WebSocket: `ws://broker.hivemq.com:8000/mqtt` or `wss://broker.hivemq.com:8884/mqtt` | Subscribes to `status`; publishes to `cmd/*` topics |

- **Broker**: `broker.hivemq.com` (free public broker). `test.mosquitto.org` can also be used by changing `MQTT_HOST`.
- **Client ID**: `esp32-ac-<MAC>` (unique per device).
- **Retained**: the `status` message is stored by the broker, so a newly connected web client immediately receives the latest status.

### Topics

| Topic | Direction | Valid payload | Effect |
|---|---|---|---|
| `gio-humidity-ac/status` | ESP32 → Web | JSON (see below) | System status |
| `gio-humidity-ac/cmd/power` | Web → ESP32 | `on` / `off` | Turn the AC on/off; auto becomes `false` |
| `gio-humidity-ac/cmd/mode` | Web → ESP32 | `dry` / `cool` | Change the AC mode |
| `gio-humidity-ac/cmd/threshold` | Web → ESP32 | number `30` - `95` | Change the threshold (values outside the range are ignored) |
| `gio-humidity-ac/cmd/auto` | Web → ESP32 | `on` / `off` | Enable/disable automation |

Payloads are case-insensitive. For `power` and `auto`, any value other than `on` is treated as `off`.

### `status` message format

```json
{"temp":29.5,"hum":72.3,"threshold":70.0,"ac":"on","mode":"dry","auto":true}
```

| Field | Type | Meaning |
|---|---|---|
| `temp` | number | Temperature (°C) |
| `hum` | number | Humidity (%RH) |
| `threshold` | number | Active threshold (%RH) |
| `ac` | string | `"on"` / `"off"` |
| `mode` | string | `"dry"` / `"cool"` |
| `auto` | boolean | Automation state |

`status` is published every 5 seconds, when MQTT connects, and whenever a state or command changes.

---

## 8. Running and Testing

1. Install the four libraries listed in section 2.
2. Fill in `WIFI_SSID` and `WIFI_PASS`, and replace the topic prefix (`gio-humidity-ac`) with a unique name (public brokers are shared by everyone).
3. Upload to the ESP32 and open the Serial Monitor (**115200** baud). Expected output:
   ```
   WiFi OK: 192.168.x.x
   Connecting MQTT... OK
   Suhu: 29.5 C | Hum: 65.2 % | Threshold: 70.0 % | AC: OFF | Mode: Dry | Auto: ON
   ```
4. Open the [HiveMQ Web Client](https://www.hivemq.com/demos/websocket-client/), connect to `broker.hivemq.com` (port 8884), and subscribe to `gio-humidity-ac/status`. The JSON should appear within 5 seconds.
5. Test manual control (recommended order):

   | Step | Topic | Message |
   |---|---|---|
   | 1 | `gio-humidity-ac/cmd/auto` | `off` |
   | 2 | `gio-humidity-ac/cmd/mode` | `cool` |
   | 3 | `gio-humidity-ac/cmd/power` | `on` |
   | 4 | `gio-humidity-ac/cmd/power` | `off` |

6. Test automation: enable `auto`, then send a `threshold` below the current humidity. The AC should turn on in Dry mode within about 2 seconds.

---

## 9. Troubleshooting

| Problem | Likely cause |
|---|---|
| `Gagal membaca DHT22!` (DHT22 read failed) | Wrong wiring, loose data pin, sensor is actually a DHT11 (change `DHT_TYPE`), or missing library |
| `Connecting MQTT... gagal (rc=-2)` | WiFi has no internet access, or the broker is unreachable |
| No `status` data on the web | Topic name does not match exactly; the web is using port 1883 (must use WebSocket 8000/8884) |
| MQTT works but the AC does not react | IR problem: distance too far, angle, wiring, or wrong protocol (try another Daikin variant or capture the original remote) |
| `https://` web page cannot connect | Use `wss://` (port 8884), not `ws://` |

---

## 10. Limitations and Future Work

- **One-way IR communication**: the ESP32 cannot know the AC's real state. If the AC is changed with the original remote, the ESP32's state goes out of sync.
- **Public broker without authentication**: anyone who knows the topic names can control the AC. For a final version, use a broker with username/password and TLS (e.g. HiveMQ Cloud or a self-hosted Mosquitto, port 8883).
- WiFi credentials are stored in the code. Do not push code containing real credentials to a public repository.
- Possible extensions: a web page (HTML + MQTT.js), periodic IR resend, target temperature control from the web, historical data logging.

---

## 11. Project Structure

```
.
├── README.md
└── smart_humidity_monitoring.ino   # main sketch (ESP32)
```
