# AI Passport:中国象棋

[English](README.md) | 简体中文

基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的 fork,为 ESP32-C3 可穿戴设备新增完整的象棋游戏。

## 功能

- 完整中国象棋:棋盘、选子落子、走子动画、将军提示、胜负判定
- 双人对弈或人机(初级 / 中级 / 高级 三档 AI)
- 悔棋、双方 10 分钟计时(不足 60 秒变橙警告)
- 实时合成音效(走子 / 吃子 / 将军 / 胜 / 负)
- NVS 存档续局、退出确认
- 楚河汉界九宫线、宣纸文雅配色

## JEV 棋评(需自编译启用)

每走满一个回合,设备调用 TypeSafe JEV API 评估局面(占优 / 攻势 / 阶段),在左上角显示简短棋评,例如 `红方略优·攻势积极·开局`。

社区版**不含** key,棋评区域为空。需自编译启用:

1. `git clone` 本仓库
2. 安装 ESP-IDF 5.5.3(见 [官方环境配置](docs/development/engineering/environment-setup.md))
3. 创建两个 secret 文件(gitignore 忽略,不入 git):
   ```c
   // main/chess_master_key_secret.h
   #define MASTER_AI_KEY "你的 TypeSafe API key"
   ```
   ```c
   // main/chess_wifi_secret.h
   #define WIFI_SSID "你的 Wi-Fi 名"
   #define WIFI_PASS "你的 Wi-Fi 密码"
   ```
4. `idf.py -p <端口> build flash`

> JEV API key 在 TypeSafe AI 平台注册获取。Wi-Fi 需为 2.4 GHz(不支持 5 GHz)。

## 官方仓库

完整 AI Passport 硬件 / 软件文档见 [官方 FoloToy/ai-passport README](https://github.com/FoloToy/ai-passport/blob/main/docs/README.md)。
