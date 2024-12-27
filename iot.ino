#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>

// Konfigurasi WiFi
#define WIFI_SSID "Pioo"         // Ganti dengan SSID WiFi Anda
#define WIFI_PASSWORD "12345678" // Ganti dengan password WiFi Anda

// Konfigurasi Firebase
#define FIREBASE_HOST "smart-farming-4019d-default-rtdb.firebaseio.com" // URL database Firebase (tanpa "https://")
#define FIREBASE_AUTH "AIzaSyBSjcJqFceRBGnO1eitZ4RCP7j0RSFDuCA"         // API key Firebase Anda

FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

// Definisikan pin untuk sensor dan relay
#define DHTPIN 21               // Pin sensor DHT21 (AM2301)
#define DHTTYPE DHT21           // Tipe sensor DHT
#define SOIL_MOISTURE_PIN 34    // Pin analog sensor kelembapan tanah
#define RELAY_SOIL_PIN 25       // Pin relay untuk kontrol kelembapan tanah

// Ambang batas kelembapan tanah
#define DRY_THRESHOLD 70        // Tanah kering
#define WET_THRESHOLD 45        // Tanah basah

// Objek sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Variabel untuk non-blocking timer
unsigned long previousMillis = 0;
const long interval = 2000; // Interval 2 detik

// Variabel untuk menyimpan nilai sebelumnya
float lastHumidity = -1, lastTemperature = -1;
String lastSoilCondition = "";
String lastRelayStatus = "";

// String untuk menyimpan log Serial Monitor
String logBuffer = "";

// Fungsi untuk mencetak ke Serial Monitor dan menambahkan ke log
void logToSerialAndBuffer(const String &message) {
  Serial.println(message);
  logBuffer += message + "\n"; // Tambahkan ke buffer log
  if (logBuffer.length() > 1000) { // Batasi ukuran log (misalnya 1000 karakter)
    logBuffer.remove(0, logBuffer.indexOf('\n') + 1);
  }
}

void setup() {
  // Inisialisasi Serial
  Serial.begin(115200);

  // Inisialisasi sensor dan relay
  dht.begin();
  pinMode(RELAY_SOIL_PIN, OUTPUT);
  digitalWrite(RELAY_SOIL_PIN, HIGH); // Pastikan relay mati saat awal

  // Koneksi ke WiFi
  logToSerialAndBuffer("Menghubungkan ke WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    logToSerialAndBuffer(".");
  }
  logToSerialAndBuffer("\nWiFi Terhubung!");

  // Konfigurasi Firebase
  firebaseConfig.host = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);
  logToSerialAndBuffer("Terhubung ke Firebase.");
}

void loop() {
  // Menggunakan millis() untuk non-blocking timer
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Membaca sensor dan mengirim data ke Firebase
    bacaSensorDanKirimFirebase();
  }
}

void bacaSensorDanKirimFirebase() {
  // Membaca data sensor DHT
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Validasi data sensor DHT
  if (isnan(humidity) || isnan(temperature)) {
    logToSerialAndBuffer("Gagal membaca sensor DHT!");
  } else {
    logToSerialAndBuffer("Kelembapan Udara: " + String(humidity) + "%  Suhu: " + String(temperature) + "°C");
  }

  // Membaca kelembapan tanah
  int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);
  float soilMoisturePercentage = map(soilMoistureValue, 0, 4095, 0, 100);

  String soilCondition;
  if (soilMoisturePercentage > DRY_THRESHOLD) {
    soilCondition = "Kering";
    digitalWrite(RELAY_SOIL_PIN, LOW); // Hidupkan relay
    logToSerialAndBuffer("Relay ON - Tanah kering");
  } else if (soilMoisturePercentage < WET_THRESHOLD) {
    soilCondition = "Basah";
    digitalWrite(RELAY_SOIL_PIN, HIGH); // Matikan relay
    logToSerialAndBuffer("Relay OFF - Tanah basah");
  } else {
    soilCondition = "Normal";
    digitalWrite(RELAY_SOIL_PIN, HIGH); // Matikan relay
    logToSerialAndBuffer("Relay OFF - Tanah normal");
  }

  logToSerialAndBuffer("Kelembapan Tanah: " + String(soilMoisturePercentage) + "% (" + soilCondition + ")");

  // Mengirim data ke Firebase hanya jika ada perubahan
  if (Firebase.ready()) {
    // Kirim data kelembapan udara
    if (humidity != lastHumidity) {
      if (Firebase.setFloat(firebaseData, "/DHT21/Humidity", humidity)) {
        logToSerialAndBuffer("Kelembapan Udara berhasil dikirim!");
        lastHumidity = humidity;
      } else {
        logToSerialAndBuffer("Gagal mengirim kelembapan udara: " + firebaseData.errorReason());
      }
    }

    // Kirim data suhu
    if (temperature != lastTemperature) {
      if (Firebase.setFloat(firebaseData, "/DHT21/Temperature", temperature)) {
        logToSerialAndBuffer("Suhu berhasil dikirim!");
        lastTemperature = temperature;
      } else {
        logToSerialAndBuffer("Gagal mengirim suhu: " + firebaseData.errorReason());
      }
    }

    // Kirim data kondisi tanah
    if (soilCondition != lastSoilCondition) {
      if (Firebase.setString(firebaseData, "/SoilMoisture/Condition", soilCondition)) {
        logToSerialAndBuffer("Kondisi Tanah berhasil dikirim!");
        lastSoilCondition = soilCondition;
      } else {
        logToSerialAndBuffer("Gagal mengirim kondisi tanah: " + firebaseData.errorReason());
      }
    }

    // Kirim persentase kelembapan tanah
    if (Firebase.setFloat(firebaseData, "/SoilMoisture/Percentage", soilMoisturePercentage)) {
      logToSerialAndBuffer("Persentase Kelembapan Tanah berhasil dikirim: " + String(soilMoisturePercentage) + "%");
    } else {
      logToSerialAndBuffer("Gagal mengirim persentase kelembapan tanah: " + firebaseData.errorReason());
    }

    // Kirim status relay
    String relayStatus = (digitalRead(RELAY_SOIL_PIN) == LOW) ? "ON" : "OFF";
    if (relayStatus != lastRelayStatus) {
      if (Firebase.setString(firebaseData, "/SoilMoisture/RelayStatus", relayStatus)) {
        logToSerialAndBuffer("Status Relay berhasil dikirim!");
        lastRelayStatus = relayStatus;
      } else {
        logToSerialAndBuffer("Gagal mengirim status relay: " + firebaseData.errorReason());
      }
    }

    // Kirim log ke Firebase
    if (Firebase.setString(firebaseData, "/System/Log", logBuffer)) {
      logToSerialAndBuffer("Log berhasil dikirim ke Firebase.");
    } else {
      logToSerialAndBuffer("Gagal mengirim log: " + firebaseData.errorReason());
    }
  } else {
    logToSerialAndBuffer("Firebase tidak siap.");
    Firebase.begin(&firebaseConfig, &firebaseAuth); // Coba hubungkan kembali
  }
}
