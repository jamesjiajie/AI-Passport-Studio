#pragma once

#include "esp_err.h"

// NimBLE 依赖 NVS；只初始化，不在失败时擦除用户数据。
esp_err_t demo_radio_nvs_prepare(void);
