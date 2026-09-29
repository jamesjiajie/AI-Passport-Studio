#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool available;
    int used_percent;
    char reset_local[12];
} monitor_window_t;

typedef struct {
    monitor_window_t five_hour;
    monitor_window_t weekly;
    char updated_local[12];
} monitor_snapshot_t;

bool monitor_parse_json(const char *json, size_t length, monitor_snapshot_t *out);
