/**
 * ============================================================================
 * Evaluasi Mata Kuliah Jaringan Sensor dan IoT — Politeknik Elektronika Negeri Surabaya (PENS)
 * 
 * Hardware Target : ESP32 #2 (DOIT ESP32 DevKit V1)
 * Alokasi Ruang   : Ruang 3 (Farrel Putra Ardiansyah) & Ruang 4 (Rara Kissya Erbianti)
 * Sensor Fisik    : 2x Sensor DHT11 (Suhu & Kelembapan Relatif)
 * Protokol        : MQTT (broker.hivemq.com:1883) via FreeRTOS Dual-Task
 * Fitur Tambahan  : Tombol BOOT (GPIO 0) untuk Simulasi Cabut Daya Ruang 4 (Uji T7 LWT)
 * ============================================================================
 */

#include <Arduino.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstring>

// ===== IDENTITAS RUANGAN ESP32 #2 =====
const char *ROOM_A = "ruang3";
const char *ROOM_B = "ruang4";

// ===== TOPIK MQTT =====
const char *TOPIC_PREFIX = "pens/klp6/";

// ===== KREDENSIAL WI-FI (SECRETS) =====
#if __has_include("secrets.h")
  #include "secrets.h"
#elif __has_include("include/secrets.h")
  #include "include/secrets.h"
#else
  #define SECRET_WIFI_SSID "SSID_WIFI_LAB"
  #define SECRET_WIFI_PASS "PASSWORD_WIFI_LAB"
#endif

const char *WIFI_SSID = SECRET_WIFI_SSID;
const char *WIFI_PASS = SECRET_WIFI_PASS;
const char *BROKER_HOST = "broker.hivemq.com";
const int BROKER_PORT = 1883;

// ===== PEMETAAN PIN HARDWARE ESP32 #2 =====
// Ruang 3 (Ruangan A)
const int DHT_A_PIN = 4;   // Pin DATA Sensor DHT11 Ruang 3 (GPIO 4)
const int L1_A_PIN  = 25;  // LED Lampu 1 Ruang 3 (Merah)
const int L2_A_PIN  = 26;  // LED Lampu 2 Ruang 3 (Hijau)

// Ruang 4 (Ruangan B)
const int DHT_B_PIN = 21;  // Pin DATA Sensor DHT11 Ruang 4 (GPIO 21)
const int L1_B_PIN  = 27;  // LED Lampu 1 Ruang 4 (Merah)
const int L2_B_PIN  = 32;  // LED Lampu 2 Ruang 4 (Hijau)

// Tombol BOOT bawaan ESP32 untuk Simulasi Cabut Daya Ruang 4 (Uji T7 LWT)
const int BOOT_BTN_PIN = 0;

// Sensor DHT11 Instances
#define DHTTYPE DHT11
DHT dhtA(DHT_A_PIN, DHTTYPE);
DHT dhtB(DHT_B_PIN, DHTTYPE);

// Mutex FreeRTOS
SemaphoreHandle_t ioMutex = NULL;

// Struktur Konteks Ruangan
struct RoomContext {
  const char *roomName;
  DHT *dht;
  int l1Pin;
  int l2Pin;
  bool allowSimulatedPowerCut;
  bool powerCutActive;

  WiFiClient net;
  PubSubClient mqtt;
  String clientId;

  char tTemp[64];
  char tHumid[64];
  char tL1[64];
  char tL2[64];
  char tStatus[64];

  float lastTemp;
  float lastHumid;

  RoomContext(const char *rName, DHT *dhtInst, int l1, int l2, bool allowCut)
      : roomName(rName), dht(dhtInst), l1Pin(l1), l2Pin(l2),
        allowSimulatedPowerCut(allowCut), powerCutActive(false), mqtt(net),
        lastTemp(27.0f), lastHumid(65.0f) {}
};

static RoomContext ctxA(ROOM_A, &dhtA, L1_A_PIN, L2_A_PIN, false);
static RoomContext ctxB(ROOM_B, &dhtB, L1_B_PIN, L2_B_PIN, true); // True: Ruang 4 dapat disimulasikan putus daya via tombol BOOT

// ============================================================================
// Callback Pesan Masuk MQTT (Hanya 1 byte: '1' atau '0')
// ============================================================================
void handleIncomingMessage(RoomContext *ctx, char *topic, byte *data, unsigned int len) {
  if (len != 1) return;
  if (data[0] != '1' && data[0] != '0') return;

  int on = (data[0] == '1') ? HIGH : LOW;

  if (strcmp(topic, ctx->tL1) == 0) {
    digitalWrite(ctx->l1Pin, on);
    Serial.printf("[%s] Lampu 1 -> %s\n", ctx->roomName, on ? "NYALA (1)" : "MATI (0)");
  } else if (strcmp(topic, ctx->tL2) == 0) {
    digitalWrite(ctx->l2Pin, on);
    Serial.printf("[%s] Lampu 2 -> %s\n", ctx->roomName, on ? "NYALA (1)" : "MATI (0)");
  }
}

void callbackA(char *topic, byte *data, unsigned int len) {
  handleIncomingMessage(&ctxA, topic, data, len);
}

void callbackB(char *topic, byte *data, unsigned int len) {
  handleIncomingMessage(&ctxB, topic, data, len);
}

// ============================================================================
// Task FreeRTOS Mandiri untuk Setiap Ruangan
// ============================================================================
void roomTask(void *pvParameters) {
  RoomContext *ctx = (RoomContext *)pvParameters;

  unsigned long lastReconnect = 0;
  unsigned long lastPublish = 0;
  bool lastBtnState = HIGH;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    unsigned long now = millis();

    // Cek tombol BOOT (GPIO 0) khusus Ruang 4 untuk Uji T7 (LWT)
    if (ctx->allowSimulatedPowerCut) {
      bool btnState = digitalRead(BOOT_BTN_PIN);
      if (lastBtnState == HIGH && btnState == LOW) {
        ctx->powerCutActive = !ctx->powerCutActive;
        if (ctx->powerCutActive) {
          // Putus paksa koneksi TCP tanpa kirim MQTT DISCONNECT
          ctx->net.stop();
          digitalWrite(ctx->l1Pin, LOW);
          digitalWrite(ctx->l2Pin, LOW);
          Serial.printf("\n[%s] SIMULASI CABUT DAYA AKTIF (T7)! Socket TCP diputus paksa. Menunggu LWT broker...\n", ctx->roomName);
        } else {
          Serial.printf("\n[%s] DAYA DISAMBUNG KEMBALI (T9)! Mencoba auto-reconnecting...\n", ctx->roomName);
          lastReconnect = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(300)); // Debounce
      }
      lastBtnState = btnState;

      if (ctx->powerCutActive) {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
    }

    // Auto-reconnect MQTT tiap 5 detik
    if (!ctx->mqtt.connected()) {
      if (now - lastReconnect >= 5000 || lastReconnect == 0) {
        lastReconnect = now;
        xSemaphoreTake(ioMutex, portMAX_DELAY);
        Serial.printf("[%s] Menghubungkan ke %s sebagai %s...\n", ctx->roomName, BROKER_HOST, ctx->clientId.c_str());

        // LWT: retain true, QoS 0, payload "offline"
        bool ok = ctx->mqtt.connect(ctx->clientId.c_str(), ctx->tStatus, 0, true, "offline");
        if (ok) {
          ctx->mqtt.subscribe(ctx->tL1, 1);
          ctx->mqtt.subscribe(ctx->tL2, 1);
          ctx->mqtt.publish(ctx->tStatus, "online", true);
          Serial.printf("[%s] Terhubung ke HiveMQ! Status 'online' (retain) terkirim.\n", ctx->roomName);
        } else {
          Serial.printf("[%s] Gagal connect (rc=%d), coba lagi 5 detik...\n", ctx->roomName, ctx->mqtt.state());
        }
        xSemaphoreGive(ioMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Loop MQTT
    xSemaphoreTake(ioMutex, portMAX_DELAY);
    ctx->mqtt.loop();
    xSemaphoreGive(ioMutex);

    // Publish telemetri suhu & kelembapan DHT11 setiap 5 detik
    if (now - lastPublish >= 5000) {
      lastPublish = now;

      float suhu = ctx->dht->readTemperature();
      float humidity = ctx->dht->readHumidity();

      xSemaphoreTake(ioMutex, portMAX_DELAY);
      if (isnan(suhu) || isnan(humidity)) {
        Serial.printf("[%s] [DHT11 WARNING] Gagal membaca sensor DHT11! Menggunakan nilai cache.\n", ctx->roomName);
        suhu = ctx->lastTemp;
        humidity = ctx->lastHumid;
      } else {
        ctx->lastTemp = suhu;
        ctx->lastHumid = humidity;
      }

      char bufTemp[12];
      snprintf(bufTemp, sizeof(bufTemp), "%.1f", suhu);
      ctx->mqtt.publish(ctx->tTemp, bufTemp, false);

      char bufHumid[12];
      snprintf(bufHumid, sizeof(bufHumid), "%.1f", humidity);
      ctx->mqtt.publish(ctx->tHumid, bufHumid, false);

      Serial.printf("[%s] [DHT11] %s -> %s C | %s -> %s %%\n",
                    ctx->roomName, ctx->tTemp, bufTemp, ctx->tHumid, bufHumid);
      xSemaphoreGive(ioMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
// Inisialisasi Konteks Ruangan
// ============================================================================
void initRoomContext(RoomContext *ctx, MQTT_CALLBACK_SIGNATURE) {
  pinMode(ctx->l1Pin, OUTPUT);
  pinMode(ctx->l2Pin, OUTPUT);
  digitalWrite(ctx->l1Pin, LOW);
  digitalWrite(ctx->l2Pin, LOW);

  ctx->dht->begin();

  snprintf(ctx->tTemp, sizeof(ctx->tTemp), "%s%s/temperature", TOPIC_PREFIX, ctx->roomName);
  snprintf(ctx->tHumid, sizeof(ctx->tHumid), "%s%s/humidity", TOPIC_PREFIX, ctx->roomName);
  snprintf(ctx->tL1, sizeof(ctx->tL1), "%s%s/lamp1", TOPIC_PREFIX, ctx->roomName);
  snprintf(ctx->tL2, sizeof(ctx->tL2), "%s%s/lamp2", TOPIC_PREFIX, ctx->roomName);
  snprintf(ctx->tStatus, sizeof(ctx->tStatus), "%s%s/status", TOPIC_PREFIX, ctx->roomName);

  String macClean = WiFi.macAddress();
  macClean.replace(":", "");
  ctx->clientId = "esp32-" + macClean + "-" + String(ctx->roomName);

  ctx->mqtt.setServer(BROKER_HOST, BROKER_PORT);
  ctx->mqtt.setCallback(callback);
  ctx->mqtt.setKeepAlive(15);
}

// ============================================================================
// Setup Arduino Framework
// ============================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  ioMutex = xSemaphoreCreateMutex();

  // Koneksi Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println("\n========================================================");
  Serial.println("  SISTEM IOT MULTI-RUANG (ESP32 #2: RUANG 3 & RUANG 4)  ");
  Serial.println("  Sensor: 2x DHT11 (Ruang 3: GPIO 4, Ruang 4: GPIO 21)   ");
  Serial.println("========================================================");
  Serial.print("Menghubungkan ke Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi Terhubung! IP ESP32 #2: %s\n", WiFi.localIP().toString().c_str());

  // Inisialisasi Ruang 3 & Ruang 4
  initRoomContext(&ctxA, callbackA);
  initRoomContext(&ctxB, callbackB);

  // Buat 2 Task FreeRTOS independen di Core 1
  xTaskCreatePinnedToCore(roomTask, "TaskRuang3", 4096, (void *)&ctxA, 1, NULL, 1);
  xTaskCreatePinnedToCore(roomTask, "TaskRuang4", 4096, (void *)&ctxB, 1, NULL, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
