# Smart Weather Station (PIC16F877A)

A hardware-level environmental monitoring system built from scratch using a PIC16F877A microcontroller. This project is written entirely in bare-metal C using the Microchip XC8 compiler, avoiding high-level abstracted libraries to maintain full control over the hardware timing and memory management.

## System Overview:

The station polls multiple environmental sensors, logs the data with precise timestamps to non-volatile memory, and streams the metrics live to both a local I2C display and a serial PC terminal.

### Core Capabilities
* **Sensor Integration:** Processes digital pulse timing for a DHT22 (Humidity/Temp) and uses the internal 10-bit ADC to read an LM35 (Analog Temp) and LDR (Light Level).
* **Hardware Timekeeping:** Communicates with a DS3231 Real-Time Clock over a 50kHz I2C bus.
* **Persistent Data Logging:** Implements a circular buffer inside the DS3231's battery-backed SRAM. Sensor logs survive total power loss and system resets.
* **Interrupt-Driven Timing:** Uses Timer1 interrupts for precision non-blocking interval timing (2.0-second execution loops) while keeping the main loop free.
* **Serial Telemetry:** Dumps historical SRAM logs on boot and streams live telemetry via UART at 9600 baud.

## Hardware Configuration

**Microcontroller:** Microchip PIC16F877A (20MHz External Crystal)
**Compiler:** XC8 v3.10 (C99 Standard)
**Flasher:** PICkit 3

### Pinout Mapping

| Component | PIC16F877A Pin | Port / Function | Protocol / Type |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | Pin 23 | RC4 | I2C Data Line (Requires 10k Pull-up) |
| **I2C SCL** | Pin 18 | RC3 | I2C Clock Line (Requires 10k Pull-up) |
| **DHT22** | Pin 19 | RD0 | Custom Digital Pulse (Requires 10k Pull-up) |
| **LM35 Sensor** | Pin 3 | RA1 (AN1) | Analog Voltage Input |
| **LDR Sensor** | Pin 4 | RA2 (AN2) | Analog Voltage Input |
| **Alert LED** | Pin 39 | RB6 | Digital Output (Threshold: 30.0C) |

## Build and Deployment

1. Clone this repository to your local machine.
2. Open the project directory using MPLAB X IDE.
3. Verify that your hardware matches the pinout table above. 
4. Connect the PICkit 3 and select **Make and Program Device**.
5. Connect a Serial Terminal (9600, 8, N, 1) to RC6/RC7 to monitor the boot sequence and live data stream.

## Hardware Debugging Notes

During development, specific hardware behaviors were accounted for in the firmware:
* **I2C Floating Bus Issue:** If the DS3231 RTC is disconnected or loses power, the I2C bus floats high (0xFF). The BCD-to-Decimal conversion will interpret this as `165`. If the screen displays `165:165:165`, the RTC physical connection is at fault.
* **LCD Display Ghosting:** The `sprintf` formatting for the LCD requires explicit trailing space padding (e.g., `%-4u`) to prevent shorter strings from leaving artifact characters on the screen from previous polling cycles.
