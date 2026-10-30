#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <cstring>

// ===== GANTI SESUAI BOARD ESP32 =====
// ESP32 #1: ROOM_A = "ruang1", ROOM_B = "ruang2"  (Ruang 2 menggunakan sensor fisik AHT10)
// ESP32 #2: ROOM_A = "ruang3", ROOM_B = "ruang4"  (Semua menggunakan potensiometer)
const char *ROOM_A = "ruang1";
const char *ROOM_B = "ruang2";

// Opsional: Jika topik "room/#" bertabrakan dengan orang lain di HiveMQ,
// isi misalnya "kel1/" sehingga menjadi "kel1/room/ruang1/temp"
const char *TOPIC_PREFIX = "pens/klp6/";

const char *WIFI_SSID = "eepisss";
const char *WIFI_PASS = "00000003";
const char *BROKER_HOST = "broker.hivemq.com";
const int BROKER_PORT = 1883;

// ===== PEMETAAN PIN HARDWARE (ADC1, I2C & OUTPUT DIGITAL) =====
// Ruangan A (ruang1 / ruang3)
const int POT_A_PIN = 34; // ADC1_CH6 (Aman saat Wi-Fi aktif)
const int L1_A_PIN = 25;  // LED Lampu 1 Ruangan A (+ resistor 220 Ohm)
const int L2_A_PIN = 26;  // LED Lampu 2 Ruangan A (+ resistor 220 Ohm)

// Ruangan B (ruang2 / ruang4)
const int POT_B_PIN = 35; // ADC1_CH7 (Untuk ruang4 atau fallback ruang2)
const int L1_B_PIN = 27;  // LED Lampu 1 Ruangan B (+ resistor 220 Ohm)
const int L2_B_PIN = 32;  // LED Lampu 2 Ruangan B (+ resistor 220 Ohm)

// Pin I2C Sensor AHT10 (Khusus Ruang 2 pada ESP32 #1)
const int I2C_SDA_PIN = 21; // SDA AHT10
const int I2C_SCL_PIN = 22; // SCL AHT10

// Instance Sensor AHT10
Adafruit_AHTX0 aht;
bool ahtAvailable = false;

// Tombol BOOT bawaan ESP32 untuk simulasi cabut daya Ruangan B pada Uji T7
// (LWT)
const int BOOT_BTN_PIN = 0;

// Mutex FreeRTOS agar pembacaan ADC dan transmisi jaringan tidak tabrakan
SemaphoreHandle_t ioMutex = NULL;

// Struktur Konteks Ruangan (Virtual Device)
struct RoomContext {
  const char *roomName;
  int potPin;
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

  RoomContext(const char *rName, int pPin, int l1, int l2, bool allowCut)
      : roomName(rName), potPin(pPin), l1Pin(l1), l2Pin(l2),
        allowSimulatedPowerCut(allowCut), powerCutActive(false), mqtt(net) {}
};

static RoomContext ctxA(ROOM_A, POT_A_PIN, L1_A_PIN, L2_A_PIN, false);
static RoomContext ctxB(ROOM_B, POT_B_PIN, L1_B_PIN, L2_B_PIN, true);

// ============================================================================
// Callback Pesan Masuk MQTT (E6: hanya menerima payload 1 byte '1' atau '0')
// ============================================================================
void handleIncomingMessage(RoomContext *ctx, char *topic, byte *data,
                           unsigned int len) {
  if (len != 1)
    return;
  if (data[0] != '1' && data[0] != '0')
    return;

  int on = (data[0] == '1') ? HIGH : LOW;

  if (strcmp(topic, ctx->tL1) == 0) {
    digitalWrite(ctx->l1Pin, on);
    Serial.printf("[%s] Lampu 1 -> %s\n", ctx->roomName,
                  on ? "NYALA (1)" : "MATI (0)");
  } else if (strcmp(topic, ctx->tL2) == 0) {
    digitalWrite(ctx->l2Pin, on);
    Serial.printf("[%s] Lampu 2 -> %s\n", ctx->roomName,
                  on ? "NYALA (1)" : "MATI (0)");
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
    // Tunggu koneksi Wi-Fi
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    unsigned long now = millis();

    // Cek tombol BOOT (GPIO0) khusus Ruangan B untuk Uji T7 (LWT)
    if (ctx->allowSimulatedPowerCut) {
      bool btnState = digitalRead(BOOT_BTN_PIN);
      if (lastBtnState == HIGH && btnState == LOW) {
        ctx->powerCutActive = !ctx->powerCutActive;
        if (ctx->powerCutActive) {
          // Putus paksa socket TCP tanpa paket MQTT DISCONNECT agar Broker
          // HiveMQ memicu LWT!
          ctx->net.stop();
          digitalWrite(ctx->l1Pin, LOW);
          digitalWrite(ctx->l2Pin, LOW);
          Serial.printf("\n[%s] SIMULASI CABUT DAYA AKTIF (T7)! Socket TCP "
                        "diputus paksa. Menunggu LWT broker...\n",
                        ctx->roomName);
        } else {
          Serial.printf(
              "\n[%s] DAYA DISAMBUNG KEMBALI (T9)! Mencoba auto-reconnect...\n",
              ctx->roomName);
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

    // E1, E3, E4: Auto-reconnect tiap 5 detik tanpa delay() panjang
    if (!ctx->mqtt.connected()) {
      if (now - lastReconnect >= 5000 || lastReconnect == 0) {
        lastReconnect = now;
        xSemaphoreTake(ioMutex, portMAX_DELAY);
        Serial.printf("[%s] Menghubungkan ke %s sebagai %s...\n", ctx->roomName,
                      BROKER_HOST, ctx->clientId.c_str());

        // E3: Daftarkan LWT (topic: room/ruangN/status, QoS 0, retain: true,
        // payload: "offline")
        bool ok = ctx->mqtt.connect(ctx->clientId.c_str(), ctx->tStatus, 0,
                                    true, "offline");
        if (ok) {
          // E4: Subscribe lamp1 & lamp2 (QoS 1) dan publish status "online"
          // (retain)
          ctx->mqtt.subscribe(ctx->tL1, 1);
          ctx->mqtt.subscribe(ctx->tL2, 1);
          ctx->mqtt.publish(ctx->tStatus, "online", true);
          Serial.printf(
              "[%s] Terhubung ke HiveMQ! Status 'online' (retain) terkirim.\n",
              ctx->roomName);
        } else {
          Serial.printf("[%s] Gagal connect (rc=%d), coba lagi 5 detik...\n",
                        ctx->roomName, ctx->mqtt.state());
        }
        xSemaphoreGive(ioMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Loop MQTT untuk memproses data masuk dan keep-alive
    xSemaphoreTake(ioMutex, portMAX_DELAY);
    ctx->mqtt.loop();
    xSemaphoreGive(ioMutex);

    // E5: Publish suhu dan kelembapan setiap 5 detik (format 1 desimal)
    if (now - lastPublish >= 5000) {
      lastPublish = now;

      xSemaphoreTake(ioMutex, portMAX_DELAY);
      float suhu = 0.0f;
      float humidity = 0.0f;
      int adcVal = -1;

      // Khusus Ruang 2: Gunakan sensor fisik AHT10 (I2C SDA=21, SCL=22)
      if (strcmp(ctx->roomName, "ruang2") == 0 && ahtAvailable) {
        sensors_event_t humid_event, temp_event;
        if (aht.getEvent(&humid_event, &temp_event)) {
          suhu = temp_event.temperature;
          humidity = humid_event.relative_humidity;
        } else {
          // Fallback ke potensiometer jika pembacaan gagal sesaat
          adcVal = analogRead(ctx->potPin);
          suhu = adcVal * 50.0f / 4095.0f;
          humidity = 45.0f + (0.4f * suhu);
        }
      } else {
        // Ruangan 1, 3, 4 (tetap): Potensiometer ADC1 + rumus kelembapan
        adcVal = analogRead(ctx->potPin);
        suhu = adcVal * 50.0f / 4095.0f;
        humidity = 45.0f + (0.4f * suhu);
      }

      char bufTemp[12];
      snprintf(bufTemp, sizeof(bufTemp), "%.1f", suhu);
      ctx->mqtt.publish(ctx->tTemp, bufTemp, false);

      char bufHumid[12];
      snprintf(bufHumid, sizeof(bufHumid), "%.1f", humidity);
      ctx->mqtt.publish(ctx->tHumid, bufHumid, false);

      if (strcmp(ctx->roomName, "ruang2") == 0 && ahtAvailable && adcVal == -1) {
        Serial.printf("[%s] [AHT10] %s -> %s C | %s -> %s %%\n",
                      ctx->roomName, ctx->tTemp, bufTemp, ctx->tHumid, bufHumid);
      } else {
        Serial.printf("[%s] [POT] %s -> %s C | %s -> %s %% (ADC: %d)\n",
                      ctx->roomName, ctx->tTemp, bufTemp, ctx->tHumid, bufHumid,
                      adcVal);
      }
      xSemaphoreGive(ioMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
// Inisialisasi Konteks Ruangan (E2 & E7)
// ============================================================================
void initRoomContext(RoomContext *ctx, MQTT_CALLBACK_SIGNATURE) {
  pinMode(ctx->l1Pin, OUTPUT);
  pinMode(ctx->l2Pin, OUTPUT);
  digitalWrite(ctx->l1Pin, LOW);
  digitalWrite(ctx->l2Pin, LOW);

  // E7: Bentuk seluruh topic dari TOPIC_PREFIX dan nama ruangan
  // Format baru: pens/klpX/ruangN/temperature & pens/klpX/ruangN/humidity
  snprintf(ctx->tTemp, sizeof(ctx->tTemp), "%s%s/temperature", TOPIC_PREFIX,
           ctx->roomName);
  snprintf(ctx->tHumid, sizeof(ctx->tHumid), "%s%s/humidity", TOPIC_PREFIX,
           ctx->roomName);
  snprintf(ctx->tL1, sizeof(ctx->tL1), "%s%s/lamp1", TOPIC_PREFIX,
           ctx->roomName);
  snprintf(ctx->tL2, sizeof(ctx->tL2), "%s%s/lamp2", TOPIC_PREFIX,
           ctx->roomName);
  snprintf(ctx->tStatus, sizeof(ctx->tStatus), "%s%s/status", TOPIC_PREFIX,
           ctx->roomName);

  // E2: Client ID unik berbasis MAC address
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
  analogReadResolution(12);

  ioMutex = xSemaphoreCreateMutex();

  // Koneksi Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Menghubungkan ke Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi Terhubung! IP ESP32: %s\n",
                WiFi.localIP().toString().c_str());

  // Inisialisasi I2C Wire untuk sensor AHT10 (GPIO 21 SDA, GPIO 22 SCL)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // Jika ESP32 ini menangani ruang2, inisialisasi sensor fisik AHT10
  if (strcmp(ROOM_A, "ruang2") == 0 || strcmp(ROOM_B, "ruang2") == 0) {
    if (aht.begin(&Wire)) {
      ahtAvailable = true;
      Serial.println("[AHT10] Sensor AHT10 berhasil diinisialisasi (SDA=21, SCL=22) untuk Ruang 2.");
    } else {
      ahtAvailable = false;
      Serial.println("[AHT10] PERINGATAN: Sensor AHT10 tidak terdeteksi! Menggunakan fallback potensiometer pin 35.");
    }
  }

  // Inisialisasi Ruangan A & Ruangan B
  initRoomContext(&ctxA, callbackA);
  initRoomContext(&ctxB, callbackB);

  // Buat 2 Task FreeRTOS independen di Core 1
  xTaskCreatePinnedToCore(roomTask, "TaskRuangA", 4096, (void *)&ctxA, 1, NULL,
                          1);
  xTaskCreatePinnedToCore(roomTask, "TaskRuangB", 4096, (void *)&ctxB, 1, NULL,
                          1);
}

void loop() {
  // Loop utama tidak digunakan karena semua tugas ditangani Task FreeRTOS
  vTaskDelay(pdMS_TO_TICKS(1000));
}
