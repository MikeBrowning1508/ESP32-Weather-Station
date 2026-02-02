#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>

namespace {
constexpr uint8_t kDhtPin = 4;
constexpr uint8_t kDhtType = DHT22;
constexpr uint8_t kLedPin = 48;
constexpr uint8_t kGmPulsePin = 6;  // Must be an interrupt-capable GPIO.
constexpr uint8_t kHvEnablePin = 7;
constexpr uint8_t kHvSensePin = 1;
constexpr uint8_t kBatterySensePin = 2;
constexpr uint8_t kI2cSdaPin = 8;
constexpr uint8_t kI2cSclPin = 9;
constexpr uint8_t kSdCsPin = 10;
constexpr uint8_t kSdMosiPin = 11;
constexpr uint8_t kSdMisoPin = 13;
constexpr uint8_t kSdSckPin = 12;

constexpr uint32_t kSampleIntervalMs = 10000;

const char *kUploadHost = "example.com";
const uint16_t kUploadPort = 80;
const char *kUploadPath = "/weather";

constexpr float kAdcReferenceVoltage = 3.3f;
constexpr uint16_t kAdcMaxValue = 4095;

constexpr float kHvDividerRatio = 0.011f;
constexpr float kBatteryDividerRatio = 0.5f;
constexpr float kBatteryLowVoltage = 3.3f;
}  // namespace

DHT dht(kDhtPin, kDhtType);
Adafruit_BMP280 bmp;
WiFiManager wifiManager;
SPIClass sdSpi(FSPI);
bool sdReady = false;

uint32_t lastSampleMs = 0;
volatile uint32_t gmPulseCount = 0;
uint32_t lastGmPulseCount = 0;

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

void setupSensors() {
  dht.begin();
  Wire.begin(kI2cSdaPin, kI2cSclPin);
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
  if (!client.connect(kUploadHost, kUploadPort)) {
    Serial.println("Upload connection failed.");
    return;
  }

  String url = String(kUploadPath) + "?temp_c=" + String(temperatureC, 2) +
               "&humidity=" + String(humidity, 2) +
               "&pressure_pa=" + String(pressurePa, 0);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + kUploadHost + "\r\n" +
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
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  pinMode(kHvEnablePin, OUTPUT);
  digitalWrite(kHvEnablePin, HIGH);
  pinMode(kGmPulsePin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kGmPulsePin), handleGmPulse, RISING);

  connectWifi();
  setupSensors();

  sdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  sdReady = SD.begin(kSdCsPin, sdSpi);
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
  connectWifi();

  uint32_t now = millis();
  if (now - lastSampleMs < kSampleIntervalMs) {
    return;
  }
  lastSampleMs = now;

  float humidity = dht.readHumidity();
  float temperatureC = dht.readTemperature();
  float pressurePa = bmp.readPressure();
  float hvSenseVoltage =
      (analogRead(kHvSensePin) * kAdcReferenceVoltage / kAdcMaxValue) / kHvDividerRatio;
  float batteryVoltage =
      (analogRead(kBatterySensePin) * kAdcReferenceVoltage / kAdcMaxValue) / kBatteryDividerRatio;
  uint32_t pulsesThisInterval = gmPulseCount - lastGmPulseCount;
  lastGmPulseCount = gmPulseCount;

  if (isnan(humidity) || isnan(temperatureC)) {
    Serial.println("Failed to read DHT sensor.");
    return;
  }

  if (batteryVoltage < kBatteryLowVoltage) {
    digitalWrite(kHvEnablePin, LOW);
    Serial.println("Battery low: disabling Geiger HV.");
  } else {
    digitalWrite(kHvEnablePin, HIGH);
  }

  digitalWrite(kLedPin, HIGH);
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
  digitalWrite(kLedPin, LOW);
}
