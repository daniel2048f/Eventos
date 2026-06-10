# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

```bash
# Build
pio run

# Upload to ESP32
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor

# Upload + monitor in sequence
pio run --target upload && pio device monitor

# Clean build
pio run --target clean
```

## Project Overview

ESP32-based timed event scheduler for a school or institution. It runs in WiFi Access Point mode and exposes a web UI at `192.168.4.1` (SSID: `AlarmaESP32`, password: `123456789`) to switch between two weekly event sequences.

**Hardware:**
- ESP32 (esp32dev)
- RTC DS3231 — keeps real time over I2C
- LCD 16×2 I2C at address `0x27` — shows current date/time
- Relay on pin 33 (active-low)
- 3 LEDs on pins 25, 26, 27
- Piezo buzzer on pin 32

## Architecture

All logic lives in `src/main.cpp`. There are no additional source files.

**Event sequences** — `sec1[]` and `sec2[]` are hardcoded arrays of `Alarma` structs `{dia, hora, minuto, Evento}`. `dia=1` means Monday–Friday; `dia=6` means Saturday. The active sequence is copied into `elegida[28]` by `copiarSecuencia()`.

**Main loop** — polls the RTC every ~200 ms and, when `dia+hora+minuto` matches an entry in `elegida[]`, blocks on that event (relay/LED/buzzer/melody) for up to ~58 s using `delay()`. The loop also auto-swaps sequences every Monday at 00:00 if `Eleccion != EleccionProximaSemana`.

**LCD task** — `tareaLCD()` runs pinned to core 1 via `xTaskCreatePinnedToCore`, updating the display every second. The main loop runs on core 0.

**Web server** — `GET /` renders the sequence selector with the next upcoming event. `GET /set?ahora=N&luego=M` updates `Eleccion` and `EleccionProximaSemana` and redirects back.

**Custom headers:**
- `include/piezo-music.h` — note frequency `#define`s and `playSong(pin, melody[], rythm[], size, tempo)`.
- `include/example-music.h` — melody arrays (Zelda, Tetris, Mario themes). `zelda_main_theme_melody` is the one active in the build.

## Key Constraints

- `elegida[]` is fixed at 28 entries; both `sec1` and `sec2` must also be exactly 28 entries or the copy loop in `copiarSecuencia()` will read out of bounds.
- Event case 4 ("LED4 sequence") uses 3× 18 s delays totalling 54 s; all blocking events must fit within a 60 s window to avoid missing the next minute.
- RTC adjustment logic (`rtc.adjust(...)`) runs when `rtc.lostPower()` or the year is outside 2025. Update the year check when deploying in a new year.
- The WiFi channel is set by the global `canal` variable (default `1`). Change it if there is interference.
- Partition scheme is `min_spiffs.csv` to maximize app space; there is no filesystem use.
