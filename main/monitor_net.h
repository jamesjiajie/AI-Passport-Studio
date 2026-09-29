#pragma once

#include "esp_err.h"
#include "monitor_data.h"

typedef enum {
    MONITOR_NEEDS_SETUP,
    MONITOR_CONNECTING,
    MONITOR_FETCHING,
    MONITOR_LIVE,
    MONITOR_OFFLINE,
    MONITOR_ERROR,
} monitor_status_t;

typedef void (*monitor_update_cb_t)(monitor_status_t status, const monitor_snapshot_t *snapshot);

esp_err_t monitor_net_start(monitor_update_cb_t callback);
void monitor_net_refresh(void);
