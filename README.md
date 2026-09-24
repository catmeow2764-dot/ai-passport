# AI Passport: Chinese Chess

English | [简体中文](README.zh_CN.md)

A fork of [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) that adds a complete Chinese Chess (Xiangqi) game for the ESP32-C3 wearable.

## Features

- Full Chinese Chess: board, piece selection, move animation, check, win/loss
- Two-player or vs AI (beginner / intermediate / advanced)
- Undo, per-side 10-minute clock with orange warning under 60s
- Real-time synthesized sound effects (move / capture / check / win / lose)
- Save/resume on NVS, exit confirmation
- River and palace lines, xuan-paper theme palette

## JEV Commentary (self-compile only)

Each turn after a complete round, the device calls the TypeSafe JEV API to evaluate the board (advantage / tempo / phase) and shows a short comment at the top-left, e.g. `Red slightly better · active attack · opening`.

The community release ships **without** a key, so the commentary area stays empty. To enable it, self-compile with your own key:

1. `git clone` this fork
2. Install ESP-IDF 5.5.3 (see the [official environment setup](docs/development/engineering/environment-setup.md))
3. Create two secret files (gitignored, never committed):
   ```c
   // main/chess_master_key_secret.h
   #define MASTER_AI_KEY "your TypeSafe API key"
   ```
   ```c
   // main/chess_wifi_secret.h
   #define WIFI_SSID "your Wi-Fi name"
   #define WIFI_PASS "your Wi-Fi password"
   ```
4. `idf.py -p <port> build flash`

> The JEV API key is obtained from the TypeSafe AI platform. Wi-Fi must be 2.4 GHz (5 GHz is not supported).

## Official repository

Full AI Passport hardware/software documentation lives in the [official FoloToy/ai-passport README](https://github.com/FoloToy/ai-passport/blob/main/docs/README.md).
