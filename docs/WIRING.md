# GestureLock Wiring Guide

## Pin map

| Signal | ESP32 pin |
|---|---|
| MPU6050 SDA | GPIO 21 |
| MPU6050 SCL | GPIO 22 |
| LCD SDA | GPIO 21 |
| LCD SCL | GPIO 22 |
| Buzzer signal | GPIO 25 |
| Common ground | GND |

### Power
- MPU6050 VCC → ESP32 3V3.
- LCD supply must be compatible with the backpack and ESP32 I²C logic. Do not connect a 5 V pull-up to ESP32 SDA/SCL.
- Power the ESP32 through its USB connector using a suitable supply.

### Bus safety
Both I²C devices share SDA/SCL. Confirm pull-up voltage on the LCD backpack. If needed, use a bidirectional I²C level shifter. An I²C scan only verifies communication, not safe logic levels.

### Buzzer
GPIO 25 is the sketch's buzzer signal. Check the exact buzzer module polarity and input requirements. Use a transistor driver for loads that exceed safe GPIO current.
