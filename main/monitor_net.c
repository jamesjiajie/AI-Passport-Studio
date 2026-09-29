#include "monitor_net.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONNECTED_BIT BIT0
#define RESPONSE_MAX 512

static const char *TAG = "monitor_net";

typedef struct {
    char ssid[33];
    char password[65];
    char url[160];
    char token[64];
} monitor_config_t;

typedef struct {
    char text[RESPONSE_MAX + 1];
    size_t length;
    bool overflow;
} response_t;

static monitor_config_t s_config;
static monitor_update_cb_t s_callback;
static EventGroupHandle_t s_wifi_events;
static TaskHandle_t s_network_task;

static bool read_string(nvs_handle_t nvs, const char *key, char *out, size_t capacity)
{
    size_t length = capacity;
    return nvs_get_str(nvs, key, out, &length) == ESP_OK;
}

static bool load_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open("monitor", NVS_READONLY, &nvs) != ESP_OK) return false;
    bool valid = read_string(nvs, "ssid", s_config.ssid, sizeof(s_config.ssid)) &&
                 read_string(nvs, "password", s_config.password, sizeof(s_config.password)) &&
                 read_string(nvs, "url", s_config.url, sizeof(s_config.url)) &&
                 read_string(nvs, "token", s_config.token, sizeof(s_config.token));
    nvs_close(nvs);
    return valid;
}

static bool copy_json_string(const cJSON *root, const char *key, char *out, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    size_t length = strlen(item->valuestring);
    if (length >= capacity) return false;
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char)item->valuestring[i] < 0x20) return false;
    }
    memcpy(out, item->valuestring, length + 1);
    return true;
}

static bool save_config(const char *line, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(line, length);
    if (!root) return false;
    monitor_config_t next = {0};
    bool valid = cJSON_IsObject(root) &&
                 copy_json_string(root, "ssid", next.ssid, sizeof(next.ssid)) &&
                 copy_json_string(root, "password", next.password, sizeof(next.password)) &&
                 copy_json_string(root, "url", next.url, sizeof(next.url)) &&
                 copy_json_string(root, "token", next.token, sizeof(next.token)) &&
                 next.ssid[0] && next.token[0] &&
                 strncmp(next.url, "http://", 7) == 0;
    cJSON_Delete(root);
    if (!valid) return false;

    nvs_handle_t nvs;
    if (nvs_open("monitor", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t error = nvs_set_str(nvs, "ssid", next.ssid);
    if (error == ESP_OK) error = nvs_set_str(nvs, "password", next.password);
    if (error == ESP_OK) error = nvs_set_str(nvs, "url", next.url);
    if (error == ESP_OK) error = nvs_set_str(nvs, "token", next.token);
    if (error == ESP_OK) error = nvs_commit(nvs);
    nvs_close(nvs);
    return error == ESP_OK;
}

static void config_task(void *arg)
{
    (void)arg;
    setvbuf(stdin, NULL, _IONBF, 0);
    char line[512];
    size_t length = 0;
    bool discard = false;
    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\r') continue;
        if (ch == '\n') {
            bool ok = !discard && length && save_config(line, length);
            puts(ok ? "MONITOR_CONFIG_OK" : "MONITOR_CONFIG_ERROR");
            length = 0;
            discard = false;
            if (ok) {
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
            continue;
        }
        if (!discard && length < sizeof(line)) line[length++] = (char)ch;
        else discard = true;
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);
    }
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (event->data_len < 0 || response->length + (size_t)event->data_len > RESPONSE_MAX) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->text + response->length, event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->text[response->length] = '\0';
    return ESP_OK;
}

static bool fetch_usage(monitor_snapshot_t *out)
{
    response_t response = {0};
    esp_http_client_config_t config = {
        .url = s_config.url,
        .timeout_ms = 15000,
        .event_handler = http_event,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "X-Passport-Token", s_config.token);
    esp_err_t error = esp_http_client_perform(client);
    bool valid = error == ESP_OK && !response.overflow &&
                 esp_http_client_get_status_code(client) == 200 &&
                 monitor_parse_json(response.text, response.length, out);
    esp_http_client_cleanup(client);
    return valid;
}

static void network_task(void *arg)
{
    (void)arg;
    bool ever_live = false;
    for (;;) {
        if (!(xEventGroupGetBits(s_wifi_events) & CONNECTED_BIT)) {
            s_callback(MONITOR_CONNECTING, NULL);
            esp_wifi_connect();
            EventBits_t bits = xEventGroupWaitBits(s_wifi_events, CONNECTED_BIT,
                                                   pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
            if (!(bits & CONNECTED_BIT)) {
                s_callback(MONITOR_OFFLINE, NULL);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        s_callback(MONITOR_FETCHING, NULL);
        monitor_snapshot_t snapshot;
        if (fetch_usage(&snapshot)) {
            ever_live = true;
            s_callback(MONITOR_LIVE, &snapshot);
        } else {
            s_callback(ever_live ? MONITOR_OFFLINE : MONITOR_ERROR, NULL);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
    }
}

esp_err_t monitor_net_start(monitor_update_cb_t callback)
{
    s_callback = callback;
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return error;
    if (xTaskCreate(config_task, "monitor_config", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (!load_config()) {
        callback(MONITOR_NEEDS_SETUP, NULL);
        return ESP_OK;
    }
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;
    error = esp_netif_init();
    if (error == ESP_OK) error = esp_event_loop_create_default();
    if (error != ESP_OK) return error;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&init);
    if (error == ESP_OK) error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error == ESP_OK) error = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                            wifi_event, NULL);
    if (error == ESP_OK) error = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            wifi_event, NULL);
    if (error == ESP_OK) error = esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, s_config.ssid, strlen(s_config.ssid));
    memcpy(wifi.sta.password, s_config.password, strlen(s_config.password));
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (error == ESP_OK) error = esp_wifi_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(error));
        return error;
    }
    if (xTaskCreate(network_task, "monitor_network", 8192, NULL, 5,
                    &s_network_task) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void monitor_net_refresh(void)
{
    if (s_network_task) xTaskNotifyGive(s_network_task);
}
