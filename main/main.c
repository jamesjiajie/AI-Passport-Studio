#include <stdio.h>
#include <string.h>
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "lvgl.h"
#include "monitor_usb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

LV_FONT_DECLARE(monitor_font_16);

static const char *TAG = "codex_monitor";
static const uint32_t BG = 0x0C1220;
static const uint32_t CARD = 0x17243A;
static const uint32_t INK = 0xEDF6FA;
static const uint32_t MUTED = 0x9AAFC0;
static const uint32_t TEAL = 0x28D7B4;

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_t;

static QueueHandle_t s_inputs;
static lv_obj_t *s_screen;
static lv_obj_t *s_overview;
static lv_obj_t *s_details;
static lv_obj_t *s_status_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_updated_label;
static lv_obj_t *s_five_percent;
static lv_obj_t *s_week_percent;
static lv_obj_t *s_five_reset;
static lv_obj_t *s_week_reset;
static lv_obj_t *s_five_bar;
static lv_obj_t *s_week_bar;
static lv_obj_t *s_detail_status;
static lv_obj_t *s_detail_update;
static monitor_status_t s_status = MONITOR_CONNECTING;
static monitor_snapshot_t s_snapshot;
static bool s_has_snapshot;
static bool s_show_details;
static int s_battery = -1;

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

static lv_obj_t *panel(lv_obj_t *parent, int y)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_pos(object, 12, y);
    lv_obj_set_size(object, 216, 89);
    lv_obj_set_style_bg_color(object, lv_color_hex(CARD), 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 14, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *quota_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, 13, 60);
    lv_obj_set_size(bar, 190, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x324359), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    return bar;
}

static const char *status_text(monitor_status_t status)
{
    switch (status) {
    case MONITOR_CONNECTING: return "连接中";
    case MONITOR_FETCHING: return "更新中";
    case MONITOR_LIVE: return "在线";
    case MONITOR_OFFLINE: return "离线";
    default: return "读取失败";
    }
}

static void refresh_ui(void)
{
    lv_label_set_text(s_status_label, status_text(s_status));
    lv_label_set_text(s_detail_status, status_text(s_status));
    lv_obj_set_style_text_color(s_status_label,
        lv_color_hex(s_status == MONITOR_LIVE ? TEAL : MUTED), 0);
    if (s_battery < 0) lv_label_set_text(s_battery_label, "BAT --");
    else lv_label_set_text_fmt(s_battery_label, "BAT %d%%", s_battery);
    if (s_has_snapshot) {
        const monitor_window_t *five = &s_snapshot.five_hour;
        const monitor_window_t *week = &s_snapshot.weekly;
        lv_label_set_text_fmt(s_five_percent, five->available ? "%d%%" : "--",
                              five->remaining_percent);
        lv_label_set_text_fmt(s_week_percent, week->available ? "%d%%" : "--",
                              week->remaining_percent);
        lv_label_set_text_fmt(s_five_reset, "重置 %s",
                              five->available ? five->reset_local : "--");
        lv_label_set_text_fmt(s_week_reset, "重置 %s",
                              week->available ? week->reset_local : "--");
        lv_bar_set_value(s_five_bar, five->available ? five->remaining_percent : 0, LV_ANIM_OFF);
        lv_bar_set_value(s_week_bar, week->available ? week->remaining_percent : 0, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_updated_label, "更新 %s", s_snapshot.updated_local);
        lv_label_set_text_fmt(s_detail_update, "更新 %s", s_snapshot.updated_local);
    } else {
        lv_label_set_text(s_five_percent, "--");
        lv_label_set_text(s_week_percent, "--");
        lv_label_set_text(s_five_reset, "重置 --");
        lv_label_set_text(s_week_reset, "重置 --");
        lv_label_set_text(s_updated_label, "更新 --");
        lv_label_set_text(s_detail_update, "更新 --");
    }
}

static void show_page(void)
{
    if (s_show_details) {
        lv_obj_add_flag(s_overview, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_details, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_details, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_overview, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_ui(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    label(s_screen, "CODEX", 15, 12, &lv_font_montserrat_20, INK);
    label(s_screen, "剩余额度", 16, 41, &monitor_font_16, MUTED);
    s_battery_label = label(s_screen, "BAT --", 161, 17, &lv_font_montserrat_14, MUTED);
    s_status_label = label(s_screen, "连接中", 164, 43,
                           &monitor_font_16, MUTED);

    s_overview = lv_obj_create(s_screen);
    lv_obj_set_pos(s_overview, 0, 70);
    lv_obj_set_size(s_overview, 240, 250);
    lv_obj_set_style_bg_opa(s_overview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overview, 0, 0);
    lv_obj_set_style_pad_all(s_overview, 0, 0);
    lv_obj_remove_flag(s_overview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *five = panel(s_overview, 0);
    label(five, "五小时剩余", 13, 9, &monitor_font_16, MUTED);
    s_five_percent = label(five, "--", 130, 5, &lv_font_montserrat_20, INK);
    s_five_bar = quota_bar(five);
    s_five_reset = label(five, "重置 --", 13, 71,
                         &monitor_font_16, MUTED);

    lv_obj_t *week = panel(s_overview, 98);
    label(week, "每周剩余", 13, 9, &monitor_font_16, MUTED);
    s_week_percent = label(week, "--", 130, 5, &lv_font_montserrat_20, INK);
    s_week_bar = quota_bar(week);
    s_week_reset = label(week, "重置 --", 13, 71,
                         &monitor_font_16, MUTED);
    s_updated_label = label(s_overview, "更新 --", 17, 196,
                            &monitor_font_16, MUTED);
    label(s_overview, "确定刷新", 137, 196,
          &monitor_font_16, MUTED);

    s_details = lv_obj_create(s_screen);
    lv_obj_set_pos(s_details, 0, 70);
    lv_obj_set_size(s_details, 240, 250);
    lv_obj_set_style_bg_opa(s_details, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_details, 0, 0);
    lv_obj_set_style_pad_all(s_details, 0, 0);
    lv_obj_remove_flag(s_details, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *info = panel(s_details, 0);
    lv_obj_set_height(info, 174);
    label(info, "连接状态", 14, 14, &monitor_font_16, MUTED);
    s_detail_status = label(info, "连接中", 14, 42,
                            &monitor_font_16, TEAL);
    s_detail_update = label(info, "更新 --", 14, 76,
                            &monitor_font_16, INK);
    label(info, "电脑与设备 USB 连接", 14, 107,
          &monitor_font_16, MUTED);
    label(info, "电脑读取用量 USB 更新", 14, 137,
          &monitor_font_16, MUTED);
    label(s_details, "上/下切换  确定刷新", 16, 196,
          &monitor_font_16, MUTED);
    show_page();
    refresh_ui();
    lv_screen_load(s_screen);
}

static void on_monitor_update(monitor_status_t status, const monitor_snapshot_t *snapshot)
{
    if (!bsp_lvgl_lock(1000)) return;
    s_status = status;
    if (snapshot) {
        s_snapshot = *snapshot;
        s_has_snapshot = true;
    }
    refresh_ui();
    bsp_lvgl_unlock();
}

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    input_t input = {.button = button, .event = event};
    if (s_inputs) (void)xQueueSend(s_inputs, &input, 0);
}

static void input_task(void *user)
{
    (void)user;
    input_t input;
    for (;;) {
        if (xQueueReceive(s_inputs, &input, pdMS_TO_TICKS(60000)) == pdTRUE) {
            if (input.event != BSP_BTN_CLICK) continue;
            if (input.button == BSP_BTN_OK) {
                monitor_usb_refresh();
            } else if (bsp_lvgl_lock(500)) {
                s_show_details = input.button == BSP_BTN_DOWN;
                show_page();
                bsp_lvgl_unlock();
            }
        } else if (s_battery >= 0) {
            int battery = bsp_battery_soc();
            if (battery >= 0 && bsp_lvgl_lock(500)) {
                s_battery = battery;
                refresh_ui();
                bsp_lvgl_unlock();
            }
        }
    }
}

void app_main(void)
{
    if (bsp_i2c_init() != ESP_OK || bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    if (bsp_battery_init() == ESP_OK) s_battery = bsp_battery_soc();
    if (!bsp_lvgl_lock(1000)) return;
    build_ui();
    bsp_lvgl_unlock();
    bsp_display_backlight(100);

    s_inputs = xQueueCreate(8, sizeof(input_t));
    if (s_inputs && xTaskCreate(input_task, "monitor_input", 4096, NULL, 5, NULL) == pdPASS) {
        esp_err_t error = bsp_button_init(on_button, NULL);
        if (error != ESP_OK) ESP_LOGE(TAG, "Button initialization failed: %s", esp_err_to_name(error));
    }
    esp_err_t error = monitor_usb_start(on_monitor_update);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "USB initialization failed: %s", esp_err_to_name(error));
        on_monitor_update(MONITOR_ERROR, NULL);
    }
}
