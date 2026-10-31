# Kel6-TugasNodeRED-ESP32
**Evaluasi Mata Kuliah Jaringan Sensor dan IoT — Politeknik Elektronika Negeri Surabaya (PENS)**

Sistem telemetri multi-ruangan cerdas berbasis protokol Message Queuing Telemetry Transport (MQTT) dengan mikrokontroler ESP32 (FreeRTOS) dan dashboard Node-RED. Sistem memantau kondisi lingkungan (suhu dan kelembapan) pada 4 ruangan terdistribusi menggunakan sensor digital DHT11 serta mengendalikan aktuator lampu (LED) secara real-time dengan jaminan pesan QoS 1 dan fitur Last Will and Testament (LWT).

---

## 👥 Anggota Kelompok 6

| Ruangan | Nama Anggota | NRP | Perangkat Sensor / Hardware | Username GitHub |
| :--- | :--- | :--- | :--- | :--- | 
| **Ruang 1** | Enzillana Afwa | 2124600092 | **Sensor Digital DHT11 (GPIO 4)** | - |
| **Ruang 2** | Syauqi Fardi Sya'bani | 2124600099 | **Sensor Digital DHT11 (GPIO 21)** | [viefemm](https://github.com/viefemm) |
| **Ruang 3** | Farrel Putra Ardiansyah | 2124600116 | **Sensor Digital DHT11 (GPIO 4)** | [frpaa](https://github.com/frpaa) |
| **Ruang 4** | Rara Kissya Erbianti | 2124600119 | **Sensor Digital DHT11 (GPIO 21)** | [ssyashain](https://github.com/ssyashain) | 

---

## 📡 Rancangan Topik MQTT

* **Broker Publik:** `broker.hivemq.com:1883`
* **Prefix Topik:** `pens/klp6/`

| Topik | Arah Alir Data | Tipe Payload | QoS | Retain | Keterangan |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `pens/klp6/ruangN/temperature` | ESP32 → Client | Numerik (°C, 1 desimal) | 0 | `false` | Publish periodik tiap 5 detik dari DHT11 |
| `pens/klp6/ruangN/humidity` | ESP32 → Client | Numerik (%RH, 1 desimal) | 0 | `false` | Publish periodik tiap 5 detik dari DHT11 |
| `pens/klp6/ruangN/lamp1` | Client → ESP32 | String (`"1"` / `"0"`) | 1 | `false` | Kendali Lampu 1 (LED Merah) |
| `pens/klp6/ruangN/lamp2` | Client → ESP32 | String (`"1"` / `"0"`) | 1 | `false` | Kendali Lampu 2 (LED Hijau) |
| `pens/klp6/ruangN/status` | ESP32 / Broker | String (`"online"` / `"offline"`) | 0 | `true` | LWT & Status Koneksi Perangkat |

---

## 🛠️ Struktur Repositori

```text
Kel6-TugasNodeRED-ESP32/
├── README.md
├── .gitignore
└── esp32/
    └── esp32_ruang/
        ├── platformio.ini            # Konfigurasi PlatformIO multi-env (esp32_ruang12 & esp32_ruang34)
        ├── esp32_ruang12.ino         # Source code Arduino IDE untuk ESP32 #1 (Ruang 1 & 2)
        ├── esp32_ruang34.ino         # Source code Arduino IDE untuk ESP32 #2 (Ruang 3 & 4)
        ├── src/
        │   ├── main12.cpp            # Firmware PlatformIO C++ ESP32 #1 (Ruang 1 & 2)
        │   └── main34.cpp            # Firmware PlatformIO C++ ESP32 #2 (Ruang 3 & 4)
        ├── include/
        │   ├── secrets.h.example     # Template konfigurasi Wi-Fi rahasia
        │   └── README
        └── lib/
```

---

## 🚀 Cara Menjalankan Kode ESP32

### Opsi A: Menggunakan PlatformIO (VS Code)
1. Buka folder `esp32/esp32_ruang/` di Visual Studio Code.
2. Salin `include/secrets.h.example` menjadi `include/secrets.h`, lalu isi SSID dan Password Wi-Fi Anda.
3. Pilih Environment board yang sesuai di status bar bawah PlatformIO:
   * **`env:esp32_ruang12`** untuk mengunggah ke **ESP32 #1** (Ruang 1 & 2).
   * **`env:esp32_ruang34`** untuk mengunggah ke **ESP32 #2** (Ruang 3 & 4).
4. Klik tombol **Build (✓)** lalu **Upload (→)**.

### Opsi B: Menggunakan Arduino IDE
1. Buka berkas yang sesuai di Arduino IDE:
   * **`esp32_ruang12.ino`** untuk board ESP32 #1.
   * **`esp32_ruang34.ino`** untuk board ESP32 #2.
2. Pasang library yang dibutuhkan via **Library Manager**:
   * `PubSubClient` oleh Nick O'Leary
   * `DHT sensor library` oleh Adafruit
   * `Adafruit Unified Sensor` oleh Adafruit
3. Pilih board **DOIT ESP32 DEVKIT V1** dan upload.
