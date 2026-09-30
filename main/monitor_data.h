#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool available;
    int remaining_percent;
    char reset_local[12];
} monitor_window_t;

typedef struct {
    monitor_window_t five_hour;
    monitor_window_t weekly;
    char updated_local[12];
} monitor_snapshot_t;

bool monitor_parse_json(const char *json, size_t length, monitor_snapshot_t *out);

typedef struct {
    char text[513];
    size_t length;
    size_t frame_length;
    bool discard;
} monitor_receiver_t;

/* Newline framing: 0 = incomplete/empty, 1 = complete, -1 = rejected. */
int monitor_receive_byte(monitor_receiver_t *receiver, unsigned char byte);
bool monitor_data_stale(bool has_snapshot, uint64_t now_ms, uint64_t last_update_ms);
