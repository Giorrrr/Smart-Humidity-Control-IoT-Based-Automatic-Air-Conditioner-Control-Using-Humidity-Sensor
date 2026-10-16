#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>
#include <DHT.h>

// ---------- WiFi & MQTT ----------
const char* WIFI_SSID = "Your_WIFI_Name";
const char* WIFI_PASS = "Your_WIFI_Password";
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

#define T_STATUS    "gio-humidity-ac/status"
#define T_POWER     "gio-humidity-ac/cmd/power"
#define T_MODE      "gio-humidity-ac/cmd/mode"
#define T_THRESHOLD "gio-humidity-ac/cmd/threshold"
#define T_AUTO      "gio-humidity-ac/cmd/auto"

// ---------- Pin ----------
const uint16_t kIrLedPin = 4;   // GPIO4  -> IR transmitter
#define DHT_PIN  14             // GPIO14 -> DHT22 OUT
#define DHT_TYPE DHT22

// ---------- Pengaturan ----------
const float HYSTERESIS = 25.0; // buat test dibikin 2 saja
const unsigned long READ_INTERVAL    = 2000;   // ms
const unsigned long PUBLISH_INTERVAL = 5000;   // ms
const unsigned long MQTT_RETRY       = 5000;   // ms

// ---------- Global variable ----------
bool  acOn      = false;   // status AC: true = ON, false = OFF
bool  modeDry   = true;    // true = Dry, false = Cool
bool  autoMode  = true;    // true = otomatis berdasarkan humidity
float threshold = 70.0;    // % RH
float humidity  = 0, temperature = 0;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
IRDaikin152  ac(kIrLedPin);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastRead = 0, lastPublish = 0, lastMqttTry = 0;

// ---------- Publish status ke web ----------
void publishStatus() {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"temp\":%.1f,\"hum\":%.1f,\"threshold\":%.1f,"
    "\"ac\":\"%s\",\"mode\":\"%s\",\"auto\":%s}",
    temperature, humidity, threshold,
    acOn ? "on" : "off", modeDry ? "dry" : "cool",
    autoMode ? "true" : "false");
  mqtt.publish(T_STATUS, buf, true);  // retained
}

// ---------- Kirim perintah IR ke AC ----------
void applyAC(bool on, bool dry) {
  if (on) {
    ac.on();
    ac.setMode(dry ? kDaikinDry : kDaikinCool);
    ac.setTemp(24);
    ac.setFan(kDaikinFanAuto);
    ac.setSwingV(false);
  } else {
    ac.off();
  }
  ac.send();
  acOn = on;
  modeDry = dry;
  Serial.printf(">> AC %s | mode %s\n", acOn ? "ON" : "OFF", modeDry ? "Dry" : "Cool");
  publishStatus();
}

// ---------- Terima perintah dari web ----------
void onMessage(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  msg.toLowerCase();
  String t = topic;
  Serial.printf("MQTT [%s] %s\n", topic, msg.c_str());

  if (t == T_POWER) {
    autoMode = false;                       // kontrol manual menonaktifkan auto
    applyAC(msg == "on", modeDry);
  } else if (t == T_MODE) {
    bool dry = (msg == "dry");
    if (msg != "dry" && msg != "cool") return;
    if (acOn) applyAC(true, dry);           // AC sedang ON: kirim ulang dengan mode baru
    else { modeDry = dry; publishStatus(); }
  } else if (t == T_THRESHOLD) {
    float v = msg.toFloat();
    if (v >= 30 && v <= 95) { threshold = v; publishStatus(); }
  } else if (t == T_AUTO) {
    autoMode = (msg == "on");
    publishStatus();
  }
}

// ---------- Koneksi MQTT (non-blocking) ----------
void ensureMqtt() {
  if (mqtt.connected()) return;
  if (millis() - lastMqttTry < MQTT_RETRY) return;
  lastMqttTry = millis();

  String clientId = "esp32-ac-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting MQTT... ");
  if (mqtt.connect(clientId.c_str())) {
    Serial.println("OK");
    mqtt.subscribe(T_POWER);
    mqtt.subscribe(T_MODE);
    mqtt.subscribe(T_THRESHOLD);
    mqtt.subscribe(T_AUTO);
    publishStatus();
  } else {
    Serial.printf("gagal (rc=%d)\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  ac.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK: " + WiFi.localIP().toString());

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
}

void loop() {
  ensureMqtt();
  mqtt.loop();

  // Baca sensor
  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("Gagal membaca DHT22!");
    } else {
      humidity = h;
      temperature = t;

      // Otomasi (hanya jika mode auto aktif)
      if (autoMode) {
        if (humidity > threshold && !acOn) {
          applyAC(true, true);                       // ON, mode Dry
        } else if (humidity < threshold - HYSTERESIS && acOn) {
          applyAC(false, modeDry);                   // OFF
        }
      }
      // Serial.printf("Suhu: %.1f C | Hum: %.1f %% | AC: %s | Auto: %s\n",
      //               temperature, humidity, acOn ? "ON" : "OFF", autoMode ? "ON" : "OFF");
      Serial.printf("Suhu: %.1f C | Hum: %.1f %% | Threshold: %.1f %% | AC: %s | Mode: %s | Auto: %s\n",
              temperature, humidity, threshold,
              acOn ? "ON" : "OFF",
              modeDry ? "Dry" : "Cool",
              autoMode ? "ON" : "OFF");

      // Serial.println("------------------------------");
      // Serial.printf("Suhu      : %.1f C\n", temperature);
      // Serial.printf("Humidity  : %.1f %%\n", humidity);
      // Serial.printf("Threshold : %.1f %%\n", threshold);
      // Serial.printf("AC        : %s\n", acOn ? "ON" : "OFF");
      // Serial.printf("Mode      : %s\n", modeDry ? "Dry" : "Cool");
      // Serial.printf("Auto      : %s\n", autoMode ? "ON" : "OFF");
    }
  }

  // Kirim status berkala ke web
  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();
    publishStatus();
  }
}
