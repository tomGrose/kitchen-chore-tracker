# Hardware Reference — Elecrow CrowPanel ESP32 HMI 7.0" (DIS08070H) — **V3.0**

Source: Elecrow official wiki, retrieved 2026-10-22
https://elecrow.com/wiki/esp32-display-702727-intelligent-touch-screen-wi-fi26ble-800480-hmi-display.html

Board revision confirmed from the PCB silkscreen: **"ESP32 Display-7 inch, SKU: DIS08070H, V3.0"**.

## Core specs
- MCU: ESP32-S3-WROOM-1-N4R8 (4MB flash, 8MB PSRAM), Wi-Fi 2.4GHz b/g/n, BT 5.0
- Display: 7.0" TFT-LCD (TN panel), 800x480, RGB parallel, driver ICs EK9716BD3 + EK73002ACGB
- Touch: capacitive, GT911 controller (I2C)
- Power: DC 5V / 2A via USB-C
- Interfaces: 2x UART0, 2x GPIO, 2x I2C, 1x battery (unused in this project), TF card, speaker

## Connector pinout
| Port | Pins | Connector |
|---|---|---|
| GPIO_D | IO38 | HY2.0-4P (IO38 / NC / 3V3 / GND) |
| UART0 | RX = IO44, TX = IO43 | HY2.0-4P + USB-C |
| I2C | SDA = IO19, SCL = IO20 | HY2.0-4P (two ports, same bus) |
| SPK (I2S) | LRCLK = IO18, BCLK = IO42, SDIN = IO17, plus I2S-CTRL | PH2.0-2P (onboard amplifier) |
| TF card (SPI) | MOSI = IO11, MISO = IO13, CLK = IO12, CS = IO10 | microSD slot |
| LCD backlight | IO2 | — |
| BAT | LiPo with onboard charging circuit | PH2.0-2P (**not used — wall powered**) |

## RGB display bus (LovyanGFX config from Elecrow)
```cpp
// Data pins
cfg.pin_d0  = GPIO_NUM_15; // B0
cfg.pin_d1  = GPIO_NUM_7;  // B1
cfg.pin_d2  = GPIO_NUM_6;  // B2
cfg.pin_d3  = GPIO_NUM_5;  // B3
cfg.pin_d4  = GPIO_NUM_4;  // B4
cfg.pin_d5  = GPIO_NUM_9;  // G0
cfg.pin_d6  = GPIO_NUM_46; // G1
cfg.pin_d7  = GPIO_NUM_3;  // G2
cfg.pin_d8  = GPIO_NUM_8;  // G3
cfg.pin_d9  = GPIO_NUM_16; // G4
cfg.pin_d10 = GPIO_NUM_1;  // G5
cfg.pin_d11 = GPIO_NUM_14; // R0
cfg.pin_d12 = GPIO_NUM_21; // R1
cfg.pin_d13 = GPIO_NUM_47; // R2
cfg.pin_d14 = GPIO_NUM_48; // R3
cfg.pin_d15 = GPIO_NUM_45; // R4

// Control / timing
cfg.pin_henable = GPIO_NUM_41;
cfg.pin_vsync   = GPIO_NUM_40;
cfg.pin_hsync   = GPIO_NUM_39;
cfg.pin_pclk    = GPIO_NUM_0;
cfg.freq_write  = 15000000;

cfg.hsync_polarity    = 0;
cfg.hsync_front_porch = 40;
cfg.hsync_pulse_width = 48;
cfg.hsync_back_porch  = 40;

cfg.vsync_polarity    = 0;
cfg.vsync_front_porch = 1;
cfg.vsync_pulse_width = 31;
cfg.vsync_back_porch  = 13;

cfg.pclk_active_neg = 1;
cfg.de_idle_high    = 0;
cfg.pclk_idle_high  = 0;

// Panel
memory_width = panel_width = 800;
memory_height = panel_height = 480;
offset_x = offset_y = 0;
```

## Touch (GT911)
```cpp
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_SDA 19
```
Touch shares the I2C bus (IO19/IO20) with the external I2C connectors. Any added I2C device (e.g., DS3231 RTC at 0x68) must not conflict with the GT911 address.

## V3.0-specific: PCA9557 touch timing control
V3.0 adds touch timing control (via an onboard **PCA9557 I2C I/O expander**) on top of V2.0 to prevent touch failures. Programs must be built on the V2.0 code **plus** this init sequence before touch init:

```cpp
PCA9557 Out;
Out.reset();
Out.setMode(IO_OUTPUT);
Out.setState(IO0, IO_LOW);
Out.setState(IO1, IO_LOW);
delay(20);
Out.setState(IO0, IO_HIGH);
delay(100);
Out.setMode(IO1, IO_INPUT);
```
If touch doesn't respond on V3.0, this sequence is the first thing to check.

## What the board has / doesn't have (for spec decisions)
- Onboard RTC: **No** (none listed) -> use NTP; add DS3231 on I2C if offline timekeeping is needed
- Speaker output: **Yes** — I2S with onboard amplifier (PH2.0-2P SPK connector)
- SD card slot: **Yes** (SPI)
- Spare GPIO: IO38 on GPIO_D -> candidate for PIR sensor input
- I2C header: Yes (shared with touch + PCA9557)

## Software versions — use the V3.0 PlatformIO example as the base
The repo's **`example/V3.0/PlatformIO70`** project is newer than the wiki text and uses (from its `platformio.ini`):
- Platform: pioarduino `platform-espressif32` 55.03.38
- Arduino-ESP32 core: **3.3.8**
- LVGL: **9.1.0**
- LovyanGFX: **1.2.25** (patched at build time by `scripts/patch_lovyangfx.py`)
- Custom board file: `esp32-s3-devkitc-1-myboard.json` (`boards_dir = .`)
- Partitions: `huge_app.csv` (revisit in Phase 1 — we need room for LittleFS)
- Build flags: `LV_CONF_SKIP`, `LV_KCONFIG_IGNORE`, `LV_COLOR_DEPTH=16`, `LV_FONT_MONTSERRAT_40=1`, `-I./include`
- Its `upload_port`/`monitor_port` are hard-coded to COM16 — change to your own port

The older wiki text (core 2.0.14/2.0.15, LVGL 8.3.3, LovyanGFX 1.1.8, SquareLine <=1.5.1) applies to the V1.0/V2.0 Arduino examples. **Decision: build on the V3.0 PlatformIO example -> LVGL 9.1.**

## Official resources
- Wiki: https://elecrow.com/wiki/esp32-display-702727-intelligent-touch-screen-wi-fi26ble-800480-hmi-display.html
- GitHub (examples, factory firmware, schematics, 3D files): https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480
- PlatformIO tutorial: https://elecrow.com/wiki/CrowPanel_ESP32_7.0-inch_with_PlatformIO.html
- Arduino + SquareLine tutorial: https://www.elecrow.com/wiki/ESP32_Display_7.0-inch_HMI_Arduino_Tutorial.html
- Schematic & PCB (zip): https://www.elecrow.com/download/product/CrowPanel/ESP32-HMI/7.0-DIS08070H/7.0-DIS08070H-SCH&PCB.zip
- Revision history: https://forum.elecrow.com/discussion/672/esp32-display-update-history
- Video course code: https://github.com/Elecrow-RD/CrowPanel-ESP32-Display-Course-File
