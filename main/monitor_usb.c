#include "monitor_usb.h"

#include <stdio.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t s_task;
static monitor_update_cb_t s_callback;

static void usb_task(void *arg)
{
    (void)arg;
    monitor_receiver_t receiver = {0};
    monitor_status_t status = MONITOR_CONNECTING;
    bool has_snapshot = false;
    uint64_t last_update_ms = 0;
    puts("MONITOR_USB_READY");
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            status = MONITOR_FETCHING;
            s_callback(status, NULL);
            puts("MONITOR_USB_REFRESH");
        }
        unsigned char bytes[64];
        int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            int frame = monitor_receive_byte(&receiver, bytes[i]);
            if (!frame) continue;
            monitor_snapshot_t snapshot;
            if (frame > 0 && monitor_parse_json(receiver.text, receiver.frame_length, &snapshot)) {
                has_snapshot = true;
                last_update_ms = esp_timer_get_time() / 1000;
                status = MONITOR_LIVE;
                s_callback(status, &snapshot);
                puts("MONITOR_USB_OK");
            } else {
                status = MONITOR_ERROR;
                s_callback(status, NULL);
                puts("MONITOR_USB_ERROR");
            }
        }
        uint64_t now_ms = esp_timer_get_time() / 1000;
        if (monitor_data_stale(has_snapshot, now_ms, last_update_ms) && status != MONITOR_OFFLINE) {
            status = MONITOR_OFFLINE;
            s_callback(status, NULL);
        }
    }
}

esp_err_t monitor_usb_start(monitor_update_cb_t callback)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 1024, .rx_buffer_size = 1024};
    esp_err_t error = usb_serial_jtag_driver_install(&config);
    if (error != ESP_OK) return error;
    usb_serial_jtag_vfs_use_driver();
    s_callback = callback;
    callback(MONITOR_CONNECTING, NULL);
    if (xTaskCreate(usb_task, "monitor_usb", 4096, NULL, 4, &s_task) != pdPASS) {
        usb_serial_jtag_driver_uninstall();
        usb_serial_jtag_vfs_use_nonblocking();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void monitor_usb_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
