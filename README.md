# 🌤️ Smart Weather Station v3 (PIC16F877A)

![C](https://img.shields.io/badge/Language-C99-blue.svg)
![Microcontroller](https://img.shields.io/badge/MCU-PIC16F877A-red.svg)
![Compiler](https://img.shields.io/badge/Compiler-XC8_v3.10-orange.svg)
![Status](https://img.shields.io/badge/Status-Fully_Operational-success.svg)

> A highly precise, real-time environmental monitoring system built from the ground up on bare-metal silicon. Features I2C display routing, hardware RTC timekeeping, analog-to-digital sensor processing, and non-volatile SRAM data logging.

---

## ⚡ Core Features

* **Real-Time Environmental Tracking:** High-accuracy humidity and temperature reading via a custom digital pulse protocol (DHT22).
* **Analog Light & Heat Sensing:** 10-bit ADC processing for ambient light levels (LDR) and secondary analog temperature cross-checking (LM35).
* **Hardware Timekeeping:** Precision I2C communication with a DS3231 RTC module to timestamp all recorded events.
* **Non-Volatile Data Logging:** Implements a cooperative circular buffer utilizing the DS3231's internal SRAM to save historical telemetry data across power cycles.
* **Live Telemetry Stream:** Streams formatted CSV history and live metrics out via UART (9600 Baud) to any serial terminal.
* **Thermal Alert System:** Automated hardware interrupt/threshold system triggers a warning LED if temperatures exceed safe limits (30.0°C).

---

## 🛠️ Hardware Architecture

### Components List
* **Microcontroller:** Microchip PIC16F877A (@ 20MHz External Crystal)
* **Display:** 16x2 Character LCD (with PCF8574 I2C Expander)
* **Sensors:** DHT22 (Digital), LM35 (Analog), LDR (Analog)
* **Clock:** DS3231 I2C Real-Time Clock

### Pinout Mapping
| Component | PIC16 Pin | Port / Function | Protocol / Type |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | Pin 23 | RC4 | I2C Data Line |
| **I2C SCL** | Pin 18 | RC3 | I2C Clock Line |
| **DHT22** | Pin 19 | RD0 | Custom Digital Pulse |
| **LM35** | Pin 3 | RA1 (AN1) | Analog Voltage Input |
| **LDR** | Pin 4 | RA2 (AN2) | Analog Voltage Input |
| **Alert LED** | Pin 39 | RB6 | Digital Output |

*(Note: I2C lines and DHT22 require 10kΩ pull-up resistors to the 5V rail).*

---

## 💻 Software & Toolchain

* **IDE:** MPLAB X IDE (v6.20+)
* **Compiler:** XC8 (v3.10) - Strict C99 Standard
* **Flasher:** Microchip PICkit 3

### Boot Sequence & Serial Output
Upon booting, the station initializes all protocols, sweeps the memory banks, and streams the historical logs to the serial monitor before entering its live 1-second sample loop:

```text
# Smart Weather Station v3 (I2C LCD + DHT22 + DS3231)
Last 8 saved samples:
time,date,temp,hum,light,alert
14:30:00,28/05/26,24.5,45,71,0
14:30:02,28/05/26,24.5,45,72,0
--- End of saved data ---
