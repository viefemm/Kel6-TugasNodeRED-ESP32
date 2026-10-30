# Kel6-TugasNodeRED-ESP32
**Evaluasi Mata Kuliah Jaringan Sensor dan IoT — Politeknik Elektronika Negeri Surabaya (PENS)**

Sistem telemetri multi-ruangan cerdas berbasis protokol Message Queuing Telemetry Transport (MQTT) dengan mikrokontroler ESP32 (FreeRTOS) dan dashboard Node-RED. Sistem memantau kondisi lingkungan (suhu dan kelembapan) pada 4 ruangan terdistribusi serta mengendalikan aktuator lampu (LED) secara real-time dengan jaminan pesan QoS 1 dan fitur Last Will and Testament (LWT).

---

## 👥 Anggota Kelompok 6

| Ruangan | Nama Anggota | NRP | Perangkat Sensor / Simulator | Username GitHub |
| :--- | :--- | :--- | :--- | :--- | 
| **Ruang 1** | Enzillana Afwa | 2124600092 | Potensiometer 10 kΩ | - |
| **Ruang 2** | Syauqi Fardi Sya'bani | 2124600099 | **Sensor Digital I2C AHT10** | [viefemm](github.com/viefemm) |
| **Ruang 3** | Farrel Putra Ardiansyah | 2124600116 | Potensiometer 10 kΩ | [frpaa](github.com/frpaa) |
| **Ruang 4** | Rara Kissya Erbianti | 2124600119 | Potensiometer 10 kΩ | [ssyashain](github.com/ssyashain) | 

---

## 📡 Rancangan Topik MQTT

* **Broker Publik:** `broker.hivemq.com:1883`
* **Prefix Topik:** `pens/klp6/`

| Topik | Arah Alir Data | Tipe Payload | QoS | Retain | Keterangan |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `pens/klp6/ruangN/temperature` | ESP32 → Client | Numerik (°C, 1 desimal) | 0 | `false` | Publish periodik tiap 5 detik |
| `pens/klp6/ruangN/humidity` | ESP32 → Client | Numerik (%RH, 1 desimal) | 0 | `false` | AHT10 nyata (R2) / Formula (R1, R3, R4) |
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
        ├── platformio.ini       # Konfigurasi PlatformIO & Dependencies (AHTX0, PubSubClient)
        ├── esp32_ruang.ino      # Source code Arduino IDE
        ├── src/
        │   └── main.cpp         # Source code PlatformIO C++
        ├── include/
        └── lib/
```

---

## 🚀 Cara Menjalankan Kode ESP32

### Opsi A: Menggunakan PlatformIO (VS Code)
1. Buka folder `esp32/esp32_ruang/` di Visual Studio Code dengan ekstensi PlatformIO.
2. Buka `src/main.cpp`, sesuaikan konstanta `ROOM_A` atau `ROOM_B` serta SSID/Password Wi-Fi.
3. Klik tombol **Build (✓)** lalu **Upload (→)** ke board ESP32.

### Opsi B: Menggunakan Arduino IDE
1. Buka file `esp32/esp32_ruang/esp32_ruang.ino` di Arduino IDE.
2. Pasang library yang dibutuhkan via Library Manager:
   * `PubSubClient` oleh Nick O'Leary
   * `Adafruit AHTX0` oleh Adafruit
3. Pilih board **DOIT ESP32 DEVKIT V1** dan upload.
