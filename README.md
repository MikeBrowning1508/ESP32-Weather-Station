# ESP32 Weather Station

This project is a starting point for an ESP32-based weather station using the Arduino
IDE. It reads a DHT22 temperature/humidity sensor and a BMP280 pressure sensor, logs the
data to Serial, and optionally uploads it via HTTP.

## Hardware

- ESP32 DevKit (or compatible)
- ESP32-S3-WROOM-1U-N8 (pins below match this module's defaults)
- DHT22 on GPIO 4 (change `kDhtPin` if needed)
- BMP280 on I2C address `0x76` (SDA=GPIO 8, SCL=GPIO 9 by default)
- Optional on-board LED on GPIO 48
- Geiger module GM pulse on GPIO 6 (interrupt-capable), HV enable on GPIO 7, HV sense on GPIO 1
- Battery sense on GPIO 2 (used to disable HV when battery is low)
- USB programming via the ESP32-S3 native USB port
- UART0 (GPIO 43 TX / GPIO 44 RX) reserved for debugging/recovery (Tag-Connect header)
- SD card via SPI (CS=GPIO 10, MOSI=GPIO 11, MISO=GPIO 13, SCK=GPIO 12)

## Getting Started

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and add the ESP32 board
   support package.
2. Install the following libraries with the Library Manager:
   - DHT sensor library (Adafruit)
   - Adafruit Unified Sensor
   - Adafruit BMP280 Library
   - WiFiManager (tzapu)
3. Open `WeatherStation.ino`.
4. Update the upload host constants if you plan to send data via HTTP.
5. Adjust the voltage divider ratios and low-battery threshold for your hardware.
6. Select your ESP32 board and upload.
7. On first boot, connect to the `ESP32-Weather-Station` access point to configure Wi-Fi.

## Notes

- Sensor readings are sampled every 10 seconds.
- GM pulses are counted per sample interval; update `kGmPulsePin` to an interrupt-capable
  GPIO (most ESP32-S3 GPIOs support interrupts), plus `kHvEnablePin`, `kHvSensePin`,
  `kBatterySensePin`, `kI2cSdaPin`, `kI2cSclPin`, and the SD card SPI pins if your wiring differs.
- Readings are appended to `/weather.csv` on the SD card with a header row if the file
  does not exist.
- Leave UART0 free if you want serial logs, bootloader access, and recovery via Tag-Connect.
- The HTTP upload uses a simple GET request; replace `kUploadHost` and `kUploadPath`
  with your own endpoint or remove the upload if not needed.
