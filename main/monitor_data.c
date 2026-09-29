#include "monitor_data.h"

#include <string.h>
#include "cJSON.h"

static bool local_time_label(const cJSON *value, char out[12])
{
    if (!cJSON_IsString(value) || !value->valuestring) {
        return false;
    }
    const char *s = value->valuestring;
    if (strcmp(s, "--") == 0) {
        memcpy(out, s, 3);
        return true;
    }
    if (strlen(s) != 11) return false;
    for (int i = 0; i < 11; ++i) {
        const char expected = i == 2 ? '-' : i == 5 ? ' ' : i == 8 ? ':' : 0;
        if (expected ? s[i] != expected : s[i] < '0' || s[i] > '9') return false;
    }
    memcpy(out, s, 12);
    return true;
}

static bool parse_window(const cJSON *root, const char *key, monitor_window_t *out)
{
    const cJSON *window = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNull(window)) return true;
    if (!cJSON_IsObject(window)) return false;
    const cJSON *used = cJSON_GetObjectItemCaseSensitive(window, "used_percent");
    const cJSON *reset = cJSON_GetObjectItemCaseSensitive(window, "reset_local");
    if (!cJSON_IsNumber(used) || used->valuedouble != used->valueint ||
        used->valueint < 0 || used->valueint > 100 ||
        !local_time_label(reset, out->reset_local)) return false;
    out->used_percent = used->valueint;
    out->available = true;
    return true;
}

bool monitor_parse_json(const char *json, size_t length, monitor_snapshot_t *out)
{
    if (!json || !out || !length || length > 512) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!root) return false;
    monitor_snapshot_t next = {0};
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    bool valid = cJSON_IsObject(root) && cJSON_IsNumber(version) &&
                 version->valuedouble == 1 &&
                 local_time_label(cJSON_GetObjectItemCaseSensitive(root, "updated_local"),
                                  next.updated_local) &&
                 parse_window(root, "five_hour", &next.five_hour) &&
                 parse_window(root, "weekly", &next.weekly) &&
                 (next.five_hour.available || next.weekly.available);
    cJSON_Delete(root);
    if (valid) *out = next;
    return valid;
}
