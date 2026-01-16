// Inkludieren der benötigten Bibliotheken
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h> // Notwendig für die manuelle I2C-Initialisierung

// --- Konfiguration ---

// WLAN Zugangsdaten
const char* ssid = "DEIN_WIFI_SSID";
const char* password = "DEIN_WIFI_PASSWORT";

// IFTTT Konfiguration
const char* iftttApiKey = "DEIN_IFTTT_API_KEY"; // Deinen IFTTT Key hier einfügen
const char* iftttEventName = "sturz_alarm";     // Den Event-Namen, den du bei IFTTT vergeben hast

// Pinbelegung für Seeed Studio XIAO ESP32-C3
const int BUZZER_PIN = 1;  // D1
const int LED_PIN = 10;    // D9 (eingebaute User-LED)
const int BUTTON_PIN = 3;  // D3

// I2C Pins für Seeed Studio XIAO ESP32-C3
const int I2C_SDA_PIN = 6; // D4
const int I2C_SCL_PIN = 7; // D5

// MPU6050 Objekt
Adafruit_MPU6050 mpu;

// Sturzerkennungs-Parameter
const float ACCELERATION_THRESHOLD_G = 2.5; // Schwellenwert in G. Muss eventuell experimentell angepasst werden!

// Zustandsvariablen
bool fallDetected = false;
unsigned long fallDetectionTime = 0;
const unsigned long ALARM_DELAY_MS = 30000; // 30 Sekunden Vorwarnzeit bis zum Senden des Webhooks

// Debouncing für den Taster
const long DEBOUNCE_DELAY_MS = 50;
unsigned long lastButtonPressTime = 0;


// --- Funktionen ---

/**
 * @brief Stellt eine Verbindung zum konfigurierten WLAN her.
 */
void connectToWiFi() {
  Serial.print("Verbinde mit WLAN '");
  Serial.print(ssid);
  Serial.println("'...");
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWLAN verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
}

/**
 * @brief Sendet den Webhook an IFTTT, um den Alarm auszulösen.
 */
void sendIFTTTWebhook() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Fehler: Keine WLAN-Verbindung für IFTTT Webhook.");
    return;
  }

  HTTPClient http;
  String url = "http://maker.ifttt.com/trigger/" + String(iftttEventName) + "/with/key/" + String(iftttApiKey);
  
  Serial.print("Sende IFTTT Webhook an: ");
  Serial.println(url);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // JSON-Payload für den Webhook
  String postData = "{\"value1\":\"Sturz erkannt!\",\"value2\":\"Bitte umgehend prüfen.\",\"value3\":\"XIAO ESP32-C3 Sturzmelder\"}";
  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) {
    Serial.printf("IFTTT Response Code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.printf("Fehler beim Senden des IFTTT Webhooks. Error Code: %d\n", httpResponseCode);
  }
  
  http.end();
}

/**
 * @brief Setzt den Alarmzustand zurück (lokal und visuell).
 */
void resetAlarm() {
  Serial.println("Alarm zurückgesetzt.");
  fallDetected = false;
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
}

/**
 * @brief Löst den lokalen Voralarm aus (Buzzer und LED).
 */
void triggerPreAlarm() {
  Serial.println("!!! POTENZIELLER STURZ ERKANNT - Voralarm gestartet !!!");
  fallDetected = true;
  fallDetectionTime = millis();
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 1000, 500); // 1kHz Ton für 500ms, wird im Loop wiederholt
}


void setup() {
  Serial.begin(115200);
  while (!Serial); // Warten auf serielle Verbindung (wichtig für C3-Chips)
  
  Serial.println("\n--- ESP32-C3 Sturzmelder Initialisierung ---");

  // Pins initialisieren
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // I2C-Schnittstelle für XIAO ESP32-C3 initialisieren
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // MPU6050 initialisieren
  if (!mpu.begin()) {
    Serial.println("Fehler: MPU6050 Sensor nicht gefunden!");
    // Fehler signalisieren durch schnelles Blinken
    while (1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
  }
  Serial.println("MPU6050 initialisiert.");

  // Sensor-Parameter konfigurieren
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Mit WLAN verbinden
  connectToWiFi();

  Serial.println("System bereit.");
}


void loop() {
  // --- Sensordaten auslesen ---
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Gesamtbeschleunigung berechnen und in G umrechnen (1G = 9.80665 m/s^2)
  float totalAcceleration = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2)) / 9.80665;

  // --- Logik für Sturzerkennung ---
  if (!fallDetected && totalAcceleration > ACCELERATION_THRESHOLD_G) {
    triggerPreAlarm();
  }

  // --- Taster zur Alarm-Quittierung ---
  // Taster ist gedrückt (LOW, da PULLUP) und Debounce-Zeit ist abgelaufen
  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPressTime > DEBOUNCE_DELAY_MS)) {
    lastButtonPressTime = millis(); // Zeit des Tastendrucks aktualisieren
    if (fallDetected) {
      Serial.println("!!! ALARM DURCH TASTER ABGEBROCHEN !!!");
      resetAlarm();
    }
  }

  // --- Alarm-Management ---
  if (fallDetected) {
    // Prüfen, ob die Vorwarnzeit abgelaufen ist
    if (millis() - fallDetectionTime >= ALARM_DELAY_MS) {
      Serial.println("!!! ALARM BESTÄTIGT - IFTTT WEBHOOK WIRD GESENDET !!!");
      sendIFTTTWebhook();
      resetAlarm(); // Alarm nach dem Senden zurücksetzen
    } else {
      // Voralarm aufrechterhalten (pulsierender Ton/Licht)
      if (millis() % 1000 < 500) {
        tone(BUZZER_PIN, 1000);
        digitalWrite(LED_PIN, HIGH);
      } else {
        noTone(BUZZER_PIN);
        digitalWrite(LED_PIN, LOW);
      }
    }
  }

  // Kurze Pause zur Stabilisierung und um den Prozessor nicht zu überlasten
  delay(50);
}
