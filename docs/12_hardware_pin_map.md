# Hardware Pin Map

This file lists the ESP32-C3 GPIO pins currently occupied by the display and
the ADXL345 accelerometer.

`board_config.h` remains the firmware source of truth. If the wiring changes,
update `components/board_config/include/board_config.h` first, then update this
document.

## Why This Matters

A GPIO pin is one physical signal line on the ESP32-C3. Once a pin is assigned
to one hardware signal, do not reuse it for another module unless the protocol
is designed to share that line.

I2C is shareable: several devices can use the same `SDA` and `SCL` wires if
each device has a different address.

SPI is not fully shared in the same way: `SCLK` and `MOSI` can sometimes be
shared, but each SPI device normally needs its own `CS` pin. The current
prototype uses only one SPI display, so these pins should be treated as
occupied.

## Current I2C Bus

These pins are used by the shared I2C bus:

| Signal | ESP32-C3 GPIO | Used by |
|---|---:|---|
| `SDA` | `GPIO5` | ADXL345, and I2C OLED when I2C display mode is selected |
| `SCL` | `GPIO6` | ADXL345, and I2C OLED when I2C display mode is selected |

## ADXL345 GY-291 Accelerometer

The ADXL345 is connected over I2C:

| ADXL345 pin | ESP32-C3 connection | Notes |
|---|---|---|
| `VCC` | `3V3` | module power |
| `GND` | `GND` | common ground |
| `SDA` | `GPIO5` | I2C data |
| `SCL` | `GPIO6` | I2C clock |
| `CS` | `3V3` | selects I2C mode on the GY-291 module |
| `SDO` | `GND` | selects I2C address `0x53` |
| `INT1` | not connected | deferred |
| `INT2` | not connected | deferred |

## OLED Display Options

### 4-pin I2C OLED

When `BOARD_OLED_TRANSPORT` is set to `BOARD_OLED_TRANSPORT_I2C`, the OLED
shares the same I2C bus as the ADXL345:

| OLED pin | ESP32-C3 connection | Notes |
|---|---|---|
| `GND` | `GND` | common ground |
| `VCC` | `3V3` | display power |
| `SCL` | `GPIO6` | I2C clock |
| `SDA` | `GPIO5` | I2C data |

The expected OLED I2C address is `0x3C`, but the scanner should confirm this
after wiring.

### 7-pin SPI OLED

When `BOARD_OLED_TRANSPORT` is set to `BOARD_OLED_TRANSPORT_SPI`, the SPI OLED
uses separate GPIO pins:

| OLED pin | ESP32-C3 connection | Notes |
|---|---|---|
| `GND` | `GND` | common ground |
| `VDD` | `3V3` | display power |
| `SCK` | `GPIO4` | SPI clock |
| `SDA` | `GPIO10` | SPI MOSI, not I2C SDA |
| `RES` | `GPIO1` | display reset |
| `DC` | `GPIO3` | command/data select |
| `CS` | `GPIO7` | SPI chip select |

## Pins Occupied by the Current Firmware Definitions

| GPIO | Occupied by |
|---:|---|
| `GPIO1` | SPI OLED `RES` if SPI OLED mode is used |
| `GPIO3` | SPI OLED `DC` if SPI OLED mode is used |
| `GPIO4` | SPI OLED `SCK` if SPI OLED mode is used |
| `GPIO5` | I2C `SDA` for ADXL345 and optional I2C OLED |
| `GPIO6` | I2C `SCL` for ADXL345 and optional I2C OLED |
| `GPIO7` | SPI OLED `CS` if SPI OLED mode is used |
| `GPIO10` | SPI OLED `SDA/MOSI` if SPI OLED mode is used |

Before adding a touch sensor, button, buzzer, or battery-related signal, choose
a GPIO that is not listed here and verify it against the exact ESP32-C3 Super
Mini pinout.

## What You Should Understand Before Moving On

1. The ADXL345 always uses the shared I2C pins `GPIO5` and `GPIO6`.
2. A 4-pin I2C OLED can share those same two I2C pins with the ADXL345.
3. A 7-pin SPI OLED occupies extra GPIO pins because SPI needs clock, data,
   reset, data/command, and chip-select signals.

## Manual Exercise

Draw the ESP32-C3 in the middle of a page. On the left, draw the ADXL345 and
connect `SDA` to `GPIO5` and `SCL` to `GPIO6`. On the right, draw the OLED
variant you are currently using and label every wire from the table above.

## Review Questions

1. Why can the ADXL345 and an I2C OLED share `GPIO5` and `GPIO6`?
2. Why is the SPI OLED pin marked `SDA` connected to MOSI instead of I2C SDA?
3. Which GPIO pins are occupied if the project uses the 7-pin SPI OLED?
4. Where should GPIO numbers be changed first if the wiring changes?
