#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>

namespace {
constexpr uint8_t dhtPin = 4;
constexpr uint8_t dhtType = DHT22;
constexpr uint8_t ledPin = 48;
constexpr uint8_t gmPulsePin = 6;  // Must be an interrupt-capable GPIO.
constexpr uint8_t hvEnablePin = 7;
constexpr uint8_t hvSensePin = 1;
constexpr uint8_t batterySensePin = 2;
constexpr uint8_t i2cSdaPin = 8;
constexpr uint8_t i2cSclPin = 9;
constexpr uint8_t sdCsPin = 10;
constexpr uint8_t sdMosiPin = 11;
constexpr uint8_t sdMisoPin = 13;
constexpr uint8_t sdSckPin = 12;
constexpr uint8_t wifiResetPin = 5;
constexpr uint32_t wifiResetHoldMs = 5000;

constexpr uint32_t sampleIntervalMs = 10000;

const char *uploadHost = "example.com";
const uint16_t uploadPort = 80;
const char *uploadPath = "/weather";

constexpr float adcReferenceVoltage = 3.3f;
constexpr uint16_t adcMaxValue = 4095;

constexpr float hvDividerRatio = 0.011f;
constexpr float batteryDividerRatio = 0.5f;
constexpr float batteryLowVoltage = 3.3f;
}  // namespace

DHT dht(dhtPin, dhtType);
Adafruit_BMP280 bmp;
WiFiManager wifiManager;
SPIClass sdSpi(FSPI);
bool sdReady = false;

uint32_t lastSampleMs = 0;
volatile uint32_t gmPulseCount = 0;
uint32_t lastGmPulseCount = 0;
uint32_t wifiResetStartMs = 0;

void IRAM_ATTR handleGmPulse() {
  gmPulseCount++;
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("Starting WiFiManager portal...");
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect("ESP32-Weather-Station")) {
    Serial.println("WiFiManager failed; restarting.");
    delay(2000);
    ESP.restart();
  }

  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void handleWifiResetButton() {
  if (digitalRead(wifiResetPin) == LOW) {
    if (wifiResetStartMs == 0) {
      wifiResetStartMs = millis();
    } else if (millis() - wifiResetStartMs >= wifiResetHoldMs) {
      Serial.println("Resetting WiFi settings.");
      wifiManager.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else {
    wifiResetStartMs = 0;
  }
}

void setupSensors() {
  dht.begin();
  Wire.begin(i2cSdaPin, i2cSclPin);
  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 not found. Check wiring or I2C address.");
  } else {
    Serial.println("BMP280 initialized.");
  }
}

void logReadings(float temperatureC, float humidity, float pressurePa) {
  float temperatureF = temperatureC * 9.0f / 5.0f + 32.0f;
  float heatIndexF = dht.computeHeatIndex(temperatureF, humidity);
  float heatIndexC = (heatIndexF - 32.0f) * 5.0f / 9.0f;

  Serial.print("Temp: ");
  Serial.print(temperatureC);
  Serial.print(" C, Humidity: ");
  Serial.print(humidity);
  Serial.print(" %, Pressure: ");
  Serial.print(pressurePa / 100.0f);
  Serial.print(" hPa, Heat Index: ");
  Serial.print(heatIndexC);
  Serial.println(" C");
}

void logToSdCard(uint32_t timestampMs, float temperatureC, float humidity, float pressurePa,
                 uint32_t pulsesThisInterval, float hvSenseVoltage, float batteryVoltage) {
  if (!sdReady) {
    return;
  }

  File logFile = SD.open("/weather.csv", FILE_APPEND);
  if (!logFile) {
    Serial.println("Failed to open weather.csv");
    return;
  }

  logFile.print(timestampMs);
  logFile.print(',');
  logFile.print(temperatureC, 2);
  logFile.print(',');
  logFile.print(humidity, 2);
  logFile.print(',');
  logFile.print(pressurePa / 100.0f, 2);
  logFile.print(',');
  logFile.print(pulsesThisInterval);
  logFile.print(',');
  logFile.print(hvSenseVoltage, 1);
  logFile.print(',');
  logFile.println(batteryVoltage, 2);
  logFile.close();
}

void uploadReadings(float temperatureC, float humidity, float pressurePa) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClient client;
  if (!client.connect(uploadHost, uploadPort)) {
    Serial.println("Upload connection failed.");
    return;
  }

  String url = String(uploadPath) + "?temp_c=" + String(temperatureC, 2) +
               "&humidity=" + String(humidity, 2) +
               "&pressure_pa=" + String(pressurePa, 0);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + uploadHost + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    while (client.available()) {
      client.read();
    }
  }

  client.stop();
  Serial.println("Upload complete.");
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(hvEnablePin, OUTPUT);
  digitalWrite(hvEnablePin, HIGH);
  pinMode(gmPulsePin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(gmPulsePin), handleGmPulse, RISING);
  pinMode(wifiResetPin, INPUT_PULLUP);

  connectWifi();
  setupSensors();

  sdSpi.begin(sdSckPin, sdMisoPin, sdMosiPin, sdCsPin);
  sdReady = SD.begin(sdCsPin, sdSpi);
  if (!sdReady) {
    Serial.println("SD card init failed.");
  } else if (!SD.exists("/weather.csv")) {
    File logFile = SD.open("/weather.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("timestamp_ms,temp_c,humidity_pct,pressure_hpa,gm_pulses,hv_v,battery_v");
      logFile.close();
    }
  }
}

void loop() {
  handleWifiResetButton();
  connectWifi();

  uint32_t now = millis();
  if (now - lastSampleMs < sampleIntervalMs) {
    return;
  }
  lastSampleMs = now;

  float humidity = dht.readHumidity();
  float temperatureC = dht.readTemperature();
  float pressurePa = bmp.readPressure();
  float hvSenseVoltage =
      (analogRead(hvSensePin) * adcReferenceVoltage / adcMaxValue) / hvDividerRatio;
  float batteryVoltage =
      (analogRead(batterySensePin) * adcReferenceVoltage / adcMaxValue) / batteryDividerRatio;
  uint32_t pulsesThisInterval = gmPulseCount - lastGmPulseCount;
  lastGmPulseCount = gmPulseCount;

  if (isnan(humidity) || isnan(temperatureC)) {
    Serial.println("Failed to read DHT sensor.");
    return;
  }

  if (batteryVoltage < batteryLowVoltage) {
    digitalWrite(hvEnablePin, LOW);
    Serial.println("Battery low: disabling Geiger HV.");
  } else {
    digitalWrite(hvEnablePin, HIGH);
  }

  digitalWrite(ledPin, HIGH);
  logReadings(temperatureC, humidity, pressurePa);
  Serial.print("GM pulses (interval): ");
  Serial.print(pulsesThisInterval);
  Serial.print(", HV sense: ");
  Serial.print(hvSenseVoltage);
  Serial.print(" V, Battery: ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
  logToSdCard(now, temperatureC, humidity, pressurePa, pulsesThisInterval, hvSenseVoltage,
              batteryVoltage);
  uploadReadings(temperatureC, humidity, pressurePa);
  digitalWrite(ledPin, LOW);
}
