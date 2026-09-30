#include "demo_radio.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "demo_radio";

static bool s_nvs_ready;

esp_err_t demo_radio_nvs_prepare(void)
{
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 示例不能为了启动无线功能而擦除未来应用可能已经保存的数据。
        ESP_LOGE(TAG, "NVS 初始化失败: %s;未自动擦除分区", esp_err_to_name(err));
        return err;
    }
    s_nvs_ready = true;
    return ESP_OK;
}
