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
    assert(snapshot.five_hour.available && snapshot.five_hour.remaining_percent == 58);
    assert(!snapshot.weekly.available);
    assert(strcmp(snapshot.five_hour.reset_local, "09-29 18:00") == 0);

    const char *bad_percent = "{\"version\":1,\"updated_local\":\"09-29 14:30\","
                              "\"five_hour\":{\"used_percent\":101,\"reset_local\":\"09-29 18:00\"}}";
    assert(!monitor_parse_json(bad_percent, strlen(bad_percent), &snapshot));
    assert(snapshot.five_hour.remaining_percent == 58);

    const char *codex = "{\"version\":1,\"updated_local\":\"09-30 16:30\","
                        "\"five_hour\":{\"used_percent\":69,\"reset_local\":\"09-30 17:06\"},"
                        "\"weekly\":{\"used_percent\":64,\"reset_local\":\"10-04 13:47\"}}";
    assert(monitor_parse_json(codex, strlen(codex), &snapshot));
    assert(snapshot.five_hour.remaining_percent == 31);
    assert(snapshot.weekly.remaining_percent == 36);

    monitor_receiver_t receiver = {0};
    assert(monitor_receive_byte(&receiver, '\n') == 0);
    for (size_t i = 0; i < strlen(valid); ++i) {
        assert(monitor_receive_byte(&receiver, (unsigned char)valid[i]) == 0);
    }
    assert(monitor_receive_byte(&receiver, '\r') == 0);
    assert(monitor_receive_byte(&receiver, '\n') == 1);
    assert(monitor_parse_json(receiver.text, receiver.frame_length, &snapshot));
    for (int i = 0; i < 513; ++i) monitor_receive_byte(&receiver, 'x');
    assert(monitor_receive_byte(&receiver, '\n') == -1);
    assert(monitor_receive_byte(&receiver, '{') == 0);
    assert(monitor_receive_byte(&receiver, '}') == 0);
    assert(monitor_receive_byte(&receiver, '\n') == 1);
    assert(strcmp(receiver.text, "{}") == 0);
    monitor_receive_byte(&receiver, 0);
    assert(monitor_receive_byte(&receiver, '\n') == -1);
    assert(!monitor_data_stale(false, 200000, 0));
    assert(!monitor_data_stale(true, 120999, 1000));
    assert(monitor_data_stale(true, 121000, 1000));
    return 0;
}
