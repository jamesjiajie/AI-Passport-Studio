<p align="right"><a href="codex-monitor.md">English</a> · <strong>简体中文</strong></p>

# Codex 用量监控

AI Passport 显示 Codex 账号的 **5 小时与每周额度剩余百分比及重置时间**。这些是额度窗口，并非原始 token 数或 API 账单用量。已登录 Codex 的电脑通过 USB 数据线发送快照；Codex 凭据保留在电脑端。

## 构建与使用

1. 激活 ESP-IDF 5.5.3，运行 `./tools/validate.sh`。确认目标设备并获得刷写许可后，才将验证通过的 `build/FoloToy-AI-Passport-full.bin` 从 `0x0` 写入。合并固件可能重置已存数据。
2. 使用 USB 数据线连接设备，关闭其他串口监视器。
3. 在已登录 Codex 的 Mac 上，从仓库根目录运行 `/usr/bin/python3 tools/codex_usage_bridge.py`。脚本会查找 macOS 上唯一的 `/dev/cu.usbmodem*` 或 Linux 上唯一的 `/dev/ttyACM*`。连接多个设备时，用 `--serial <port>` 指定目标串口。

保持桥接程序运行。`USB connected` 表示已打开串口；`USB quota delivered` 表示固件已接受快照。桥接程序在 macOS/Linux 上使用 Python 标准库，并在 `PATH` 或 `/Applications/ChatGPT.app` 内查找 `codex`。未登录或额度不可用会显示错误，不按零用量处理。

## 设备行为与协议

百分比及进度条均表示剩余额度，与 Codex 的 Usage remaining 一致。USB 快照包含 `used_percent`，固件校验后按 `100 - used_percent` 显示；69%/64% 已用显示为 31%/36% 剩余。不可用窗口显示 `--`。

电脑每 60 秒发送一次快照。短按确定立即刷新，上键显示概览，下键显示连接详情。响应不可用或无效时，设备在 RAM 中保留最后一次成功读数。连续 120 秒没有成功收到快照时，标记离线。时间按电脑本地时区显示。

每个 USB 帧为以换行结束的紧凑 JSON，不含换行时最多 512 字节。固件用 `MONITOR_USB_OK` 确认已接受的数据，用 `MONITOR_USB_ERROR` 拒绝无效数据；按确定时发送 `MONITOR_USB_REFRESH`。空行用于恢复帧边界，超长帧和控制字符会被拒绝。Codex 账号访问使用本机 CLI app-server 的 `account/rateLimits/read` 协议，已按 Codex CLI 0.158.0-alpha.2.1 测试。

## 验证

2026-09-30 的剩余额度构建已通过完整验证和归档校验：合并镜像 SHA-256 为 `9192808e7b9435a4ab1320cba77ebce0289f55718e23cccaa2445b1706486259`，配套 ELF SHA-256 为 `c6b3e412a9e6cba4bf15eee38f2a9181f7468457bbdbb51183b3198c763f8759`。验证归档位于 `build/firmware/<合并镜像sha256>/`。

镜像已从 0x0 刷入，esptool 数据哈希校验通过。启动日志匹配 ELF，显示/LVGL/电池/按键初始化成功，并输出 `MONITOR_USB_READY`。桥接发送真实额度后收到 `MONITOR_USB_OK`；用户确认剩余百分比与 Codex Usage remaining 一致、中文标签显示正常。主机检查覆盖额度归一化、USB 快照/确认/刷新/断线交互、帧边界、超长帧恢复、过期计时、JSON 校验和中文字形覆盖。物理拔插恢复、120 秒离线切换、长时间运行及内存余量仍未实测。
