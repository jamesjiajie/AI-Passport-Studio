#include <assert.h>
#include <string.h>

#include "monitor_data.h"

int main(void)
{
    const char *valid = "{\"version\":1,\"updated_local\":\"09-29 14:30\","
                        "\"five_hour\":{\"used_percent\":42,\"reset_local\":\"09-29 18:00\"},"
                        "\"weekly\":null}";
    monitor_snapshot_t snapshot = {0};
    assert(monitor_parse_json(valid, strlen(valid), &snapshot));
    assert(snapshot.five_hour.available && snapshot.five_hour.used_percent == 42);
    assert(!snapshot.weekly.available);
    assert(strcmp(snapshot.five_hour.reset_local, "09-29 18:00") == 0);

    const char *bad_percent = "{\"version\":1,\"updated_local\":\"09-29 14:30\","
                              "\"five_hour\":{\"used_percent\":101,\"reset_local\":\"09-29 18:00\"}}";
    assert(!monitor_parse_json(bad_percent, strlen(bad_percent), &snapshot));
    assert(snapshot.five_hour.used_percent == 42);
    return 0;
}
