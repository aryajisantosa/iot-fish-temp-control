#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================================
// KONFIGURASI JARINGAN & LINK SCRIPT
// ==========================================================
const char* ssid = "Ruang Rapat FT";         
const char* password = "informatika"; 

// Link Deploy Google Script yang baru
const String url_google_script = "https://script.google.com/macros/s/AKfycbxAL8QsvaUhnfmRff5ZCSgt5z8joigxRoS1PdoRv81h5VyGYYrNIQECEjW_Opt4Q0vu/exec";

// --- Definisi Pin ---
const int pinSuhu = 5;       
const int pinKipas = 34;     
const int pinPendingin = 32; // Ke Buck Converter (Peltier)
const int pinAmonia = 25;    // Ke Sensor MQ-135

OneWire oneWire(pinSuhu);
DallasTemperature sensorSuhu(&oneWire);

int nilaiPWM = 0; 

void setup() {
  Serial.begin(115200);

  pinMode(pinSuhu, INPUT_PULLUP);
  pinMode(pinKipas, OUTPUT); 
  pinMode(pinPendingin, OUTPUT);
  pinMode(pinAmonia, INPUT);

  sensorSuhu.begin();
  
  // Pastikan aktuator mati saat pertama kali menyala
  digitalWrite(pinKipas, LOW);
  analogWrite(pinPendingin, 0); 

  Serial.println("\nMenghubungkan ke Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Terhubung!");
  Serial.println("Sistem Real-Time Smart Cold Storage Mulai!");
}

void loop() {
  // ==========================================================
  // 1. BACA SENSOR REAL (Suhu & Amonia)
  // ==========================================================
  sensorSuhu.requestTemperatures();
  float suhuCelsius = sensorSuhu.getTempCByIndex(0);
  
  int nilaiAmoniaAnalog = analogRead(pinAmonia);
  float amoniaPPM = map(nilaiAmoniaAnalog, 0, 4095, 0, 120);

  if (suhuCelsius == DEVICE_DISCONNECTED_C) {
    Serial.println("ERROR: Sensor Suhu terputus!");
    delay(2000);
    return;
  }

  // ==========================================================
  // 2. LOGIKA CERDAS STATUS KESEGARAN IKAN
  // ==========================================================
  String statusKesegaran = "";
  
  if (amoniaPPM >= 65.0) {
    statusKesegaran = "busuk"; // Fix: Amonia >= 65 langsung busuk, berapapun suhunya
  } 
  else if (amoniaPPM > 20.0 || suhuCelsius > 25.0) {
    statusKesegaran = "waspada"; // Fix: Amonia > 20 ATAU Suhu bocor di atas 25 derajat
  } 
  else {
    statusKesegaran = "segar"; // Fix: Amonia 0-20 ppm DAN Suhu di bawah 25 derajat
  }

  // ==========================================================
  // 3. FUZZY LOGIC UNTUK KENDALI PELTIER (BUCK CONVERTER)
  // ==========================================================
  float u_dingin = 0.0, u_normal = 0.0, u_panas = 0.0;

  if (suhuCelsius <= 15.0) u_dingin = 1.0;
  else if (suhuCelsius > 15.0 && suhuCelsius <= 17.5) u_dingin = (17.5 - suhuCelsius) / 2.5;
  else u_dingin = 0.0;

  if (suhuCelsius > 15.0 && suhuCelsius <= 17.5) u_normal = (suhuCelsius - 15.0) / 2.5;
  else if (suhuCelsius > 17.5 && suhuCelsius < 20.0) u_normal = (20.0 - suhuCelsius) / 2.5;
  else u_normal = 0.0;

  if (suhuCelsius >= 20.0) u_panas = 1.0;
  else if (suhuCelsius > 17.5 && suhuCelsius < 20.0) u_panas = (suhuCelsius - 17.5) / 2.5;
  else u_panas = 0.0;

  float pembilang = (u_dingin * 0) + (u_normal * 127) + (u_panas * 255);
  float penyebut = u_dingin + u_normal + u_panas;
  if (penyebut == 0) penyebut = 0.0001; 
  
  // Hitung nilai PWM untuk Buck Converter
  nilaiPWM = (int)(pembilang / penyebut);
  
  // Eksekusi daya ke Peltier
  analogWrite(pinPendingin, nilaiPWM); 
  
  // Logika Kipas
  if (nilaiPWM > 0 || amoniaPPM >= 65.0) {
    digitalWrite(pinKipas, HIGH);
  } else {
    digitalWrite(pinKipas, LOW);
  }

  // ==========================================================
  // 4. KIRIM DATA KE GOOGLE SCRIPT (POST)
  // ==========================================================
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 
    
    HTTPClient http;
    http.begin(client, url_google_script);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Gabungkan data Suhu, Amonia, dan Status menjadi satu paket
    String httpRequestData = "suhu=" + String(suhuCelsius, 2) + 
                             "&amonia=" + String(amoniaPPM, 2) + 
                             "&status=" + statusKesegaran;
                             
    Serial.print("Suhu: " + String(suhuCelsius, 2) + "C | Amonia: " + String(amoniaPPM, 2) + "ppm | Status: " + statusKesegaran);
    
    int kodeResponHTTP = http.POST(httpRequestData);
    if (kodeResponHTTP > 0) {
      Serial.println(" => [SUKSES TERKIRIM]");
    } else {
      Serial.println(" => [GAGAL]");
    }
    http.end();
  }

  // Jeda 5 detik sebelum mengambil data baru
  delay(5000); 
}