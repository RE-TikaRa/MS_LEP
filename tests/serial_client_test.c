#include "jpt_client.h"

#include <string.h>

typedef struct {
    GMainLoop *loop;
    JptClientResult result;
    GError *error;
    guint16 word;
    guint cached_alarms;
} Probe;

static void completed(const JptClientResult *result, gpointer user_data)
{
    Probe *probe = user_data;
    probe->result.type = result->type;
    probe->result.connected = result->connected;
    probe->result.connection_lost = result->connection_lost;
    probe->result.telemetry = result->telemetry;
    g_clear_error(&probe->error);
    if (result->error) {
        probe->error = g_error_copy(result->error);
    } else if (result->exchange && result->exchange->frame->command == JPT_CMD_READ_ALARM_CACHE) {
        GArray *records = jpt_device_decode_alarm_cache(result->exchange->frame, &probe->error);
        g_assert_no_error(probe->error);
        probe->cached_alarms = records->len;
        if (records->len) {
            const JptAlarmRecord *last = &g_array_index(records, JptAlarmRecord, records->len - 1);
            g_assert_cmpuint(last->type, ==, JPT_CACHE_SUPPLY_VOLTAGE);
            g_assert_cmpuint(last->powered_minutes, ==, 72002);
            g_assert_cmpuint(last->extra[0][9], ==, 2409);
        }
        g_array_unref(records);
    } else if (result->exchange && result->exchange->frame) {
        g_assert_true(jpt_device_decode_u16(result->exchange->frame, &probe->word, NULL));
    }
    g_main_loop_quit(probe->loop);
}

static void wait_for(Probe *probe, JptClientResultType expected)
{
    g_main_loop_run(probe->loop);
    g_assert_cmpint(probe->result.type, ==, expected);
}

int main(int argc, char **argv)
{
    if (argc != 3 || !g_str_has_prefix(argv[1], "/dev/pts/") ||
        !argv[1][9] || strspn(argv[1] + 9, "0123456789") != strlen(argv[1] + 9)) {
        g_printerr("Usage: %s /dev/pts/NUMBER SCENARIO\n", argv[0]);
        return 2;
    }
    Probe probe = {.loop = g_main_loop_new(NULL, FALSE)};
    JptClient *client = jpt_client_new(NULL, NULL);
    JptSerialConfig config = {.baud_rate = 57600, .data_bits = 8, .stop_bits = 1};
    jpt_client_connect_serial_async(client, argv[1], &config, completed, &probe);
    wait_for(&probe, JPT_CLIENT_CONNECTION);
    g_assert_no_error(probe.error);
    g_assert_true(probe.result.connected);

    jpt_client_read_initial_async(client, completed, &probe);
    wait_for(&probe, JPT_CLIENT_TELEMETRY);
    g_assert_no_error(probe.error);
    g_assert_cmpstr(probe.result.telemetry.serial_number, ==, "AIAJP0248999");
    g_assert_cmpuint(probe.result.telemetry.control_mode, ==, 0x1c);
    g_assert_true(probe.result.telemetry.pump_enabled);
    g_assert_cmpuint(probe.result.telemetry.db25_power, ==, 0x80);
    g_assert_true(probe.result.telemetry.sensor_2_fault);
    g_assert_cmpfloat(ABS(probe.result.telemetry.sensor_1_celsius - 25.1), <, 0.001);
    g_assert_cmpuint(probe.result.telemetry.alarm_counts[4], ==, 1);

    jpt_client_read_async(client, JPT_CMD_READ_ALARM_CACHE, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_no_error(probe.error);
    g_assert_cmpuint(probe.cached_alarms, ==, g_str_equal(argv[2], "empty-cache") ? 0 : 3);

    jpt_client_read_async(client, 0xffab, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_error(probe.error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_DEVICE_REJECTED);
    g_assert_false(probe.result.connection_lost);
    jpt_client_write_u16_async(client, JPT_CMD_FREQUENCY, 137, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_no_error(probe.error);
    g_assert_cmpuint(probe.word, ==, 137);
    jpt_client_poll_async(client, completed, &probe);
    wait_for(&probe, JPT_CLIENT_TELEMETRY);
    g_assert_no_error(probe.error);
    g_assert_cmpuint(probe.result.telemetry.real_frequency, ==, 137);
    g_assert_cmpuint(probe.result.telemetry.lock_frequency, ==, 100);
    g_assert_cmpstr(probe.result.telemetry.serial_number, ==, "AIAJP0248999");

    jpt_client_write_u16_async(client, JPT_CMD_LOCK_FREQUENCY, 314, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_no_error(probe.error);
    jpt_client_poll_async(client, completed, &probe);
    wait_for(&probe, JPT_CLIENT_TELEMETRY);
    g_assert_no_error(probe.error);
    g_assert_cmpuint(probe.result.telemetry.lock_frequency, ==, 314);

    jpt_client_set_address_async(client, "::1", 5000, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_error(probe.error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false(probe.result.connection_lost);
    jpt_client_set_address_async(client, "192.0.2.7", 5521, completed, &probe);
    wait_for(&probe, JPT_CLIENT_EXCHANGE);
    g_assert_no_error(probe.error);
    g_assert_cmpuint(probe.word, ==, 5521);

    jpt_client_safe_disconnect_async(client, completed, &probe);
    wait_for(&probe, JPT_CLIENT_DISCONNECTED);
    if (g_str_equal(argv[2], "normal") || g_str_equal(argv[2], "empty-cache")) {
        g_assert_no_error(probe.error);
        g_assert_false(probe.result.connected);
        jpt_client_read_async(client, JPT_CMD_STATUS, completed, &probe);
        wait_for(&probe, JPT_CLIENT_EXCHANGE);
        g_assert_error(probe.error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
    } else {
        g_assert_nonnull(probe.error);
        g_assert_true(probe.result.connected);
        g_assert_false(probe.result.connection_lost);
        jpt_client_read_async(client, JPT_CMD_READ_CONTROL_MODE, completed, &probe);
        wait_for(&probe, JPT_CLIENT_EXCHANGE);
        g_assert_no_error(probe.error);
    }
    g_clear_error(&probe.error);
    jpt_client_free(client);
    g_main_loop_unref(probe.loop);
    return 0;
}
