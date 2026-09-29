<p align="right"><a href="codex-monitor.md">English</a> · <strong>简体中文</strong></p>

# Codex 用量监控

本应用显示 Codex 账号的 **5 小时与每周额度已用百分比及重置时间**，并非原始 token 数，也不是 OpenAI API 账单用量。已登录 Codex 的电脑运行桥接程序；设备只通过可信局域网 Wi-Fi 读取桥接程序提供的只读快照。

## 构建与使用

1. 激活 ESP-IDF 5.5.3，运行 `./tools/validate.sh`。确认目标设备并获得刷写许可后，才将验证通过的 `build/FoloToy-AI-Passport-full.bin` 从 `0x0` 写入。合并固件可能重置已存数据。
2. 在已登录 Codex 的电脑上运行 `python3 tools/codex_usage_bridge.py`。程序在 `build/monitor-token` 生成私有只读令牌，并监听 8787 端口。需要在可信局域网中允许该端口通过电脑防火墙。
3. 用 USB 连接已刷写的设备，在已激活 ESP-IDF 的 Python 环境中运行 `python3 tools/configure_monitor.py <串口>`。输入 2.4 GHz Wi-Fi 名称、密码及 `http://<电脑局域网IP>:8787/usage`。一次配置会把 Wi-Fi 信息、地址和只读令牌保存到设备 NVS，然后重启；需要修改时重新运行该命令。

实时更新要求电脑持续运行桥接程序。设备每 60 秒刷新一次，短按确定可立即刷新；上键显示额度概览，下键显示连接信息。后续请求失败时，设备在 RAM 中保留最后一次成功读数，并标记离线。桥接响应不包含 Codex 登录凭据或账号标识。局域网 HTTP 连接和 NVS 没有加密；请使用可信 Wi-Fi，不要把 8787 端口暴露到互联网。

桥接程序使用本机 Codex CLI app-server 的 `account/rateLimits/read` 协议，已按 Codex CLI 0.158.0-alpha.2.1 测试；Codex 更新后协议可能变化。无法取得的额度显示为不可用，不按零处理。重置时间按电脑本地时区显示。

尚需设备实测：Wi-Fi 连接和恢复、USB 配置、按键操作、百分比及重置时间显示（包括中文字形）、离线状态、电池显示，以及 Wi-Fi 和 LVGL 同时运行时的内存余量。构建成功不能替代这些结果。
