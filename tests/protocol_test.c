#include "jpt_device.h"

#include <glib.h>
#include <string.h>

static void test_encode_sn_request(void)
{
    const guint8 payload[] = {0x00, 0x00};
    const guint8 expected[] = {0x24, 0x3f, 0x00, 0x02, 0x00, 0x00, 0x2a};
    GError *error = NULL;
    GBytes *frame = jpt_frame_encode(0x3f, payload, sizeof(payload), &error);
    g_assert_no_error(error);

    gsize length = 0;
    const guint8 *actual = g_bytes_get_data(frame, &length);
    g_assert_cmpuint(length, ==, sizeof(expected));
    g_assert_cmpmem(actual, length, expected, sizeof(expected));
    g_bytes_unref(frame);
}

static void test_parser_fragmented_sn_reply(void)
{
    const guint8 first[] = {0x24, 0x3f, 0x00, 0x0c, 'A', 'I', 'A'};
    const guint8 second[] = {'J', 'P', '0', '2', '4', '8', '9', '9', '9', 0x2a};
    JptParser parser;
    jpt_parser_init(&parser);

    GError *error = NULL;
    GPtrArray *frames = jpt_parser_push(&parser, first, sizeof(first), &error);
    g_assert_no_error(error);
    g_assert_cmpuint(frames->len, ==, 0);
    g_ptr_array_unref(frames);

    frames = jpt_parser_push(&parser, second, sizeof(second), &error);
    g_assert_no_error(error);
    g_assert_cmpuint(frames->len, ==, 1);
    JptFrame *frame = g_ptr_array_index(frames, 0);
    g_assert_cmpuint(frame->command, ==, 0x3f);
    gsize length = 0;
    const gchar *serial = g_bytes_get_data(frame->payload, &length);
    g_assert_cmpuint(length, ==, 12);
    g_assert_cmpmem(serial, length, "AIAJP0248999", 12);

    g_ptr_array_unref(frames);
    jpt_parser_clear(&parser);
}

static void test_parser_coalesced_frames_and_noise(void)
{
    const guint8 input[] = {
        0x00, 0x45,
        0x24, 0x38, 0x00, 0x02, 0x18, 0x00, 0x2a,
        0x24, 0x40, 0x00, 0x02, 0x10, 0x00, 0x2a,
    };
    JptParser parser;
    jpt_parser_init(&parser);

    GError *error = NULL;
    GPtrArray *frames = jpt_parser_push(&parser, input, sizeof(input), &error);
    g_assert_no_error(error);
    g_assert_cmpuint(frames->len, ==, 2);
    g_assert_cmpuint(((JptFrame *)g_ptr_array_index(frames, 0))->command, ==, 0x38);
    g_assert_cmpuint(((JptFrame *)g_ptr_array_index(frames, 1))->command, ==, 0x40);

    g_ptr_array_unref(frames);
    jpt_parser_clear(&parser);
}

static void test_encode_rejects_oversized_payload(void)
{
    guint8 payload[256] = {0};
    GError *error = NULL;
    GBytes *frame = jpt_frame_encode(0x3f, payload, sizeof(payload), &error);
    g_assert_null(frame);
    g_assert_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_PAYLOAD_TOO_LARGE);
    g_clear_error(&error);
}

static void test_encode_tcp_address_update(void)
{
    const guint8 ip[] = {192, 168, 0, 10};
    const guint8 expected_ip[] = {0x24, 0x6e, 0x00, 0x04, 192, 168, 0, 10, 0x2a};
    const guint8 expected_port[] = {0x24, 0x6f, 0x00, 0x02, 0x88, 0x13, 0x2a};
    GError *error = NULL;
    GBytes *frame = jpt_frame_encode(JPT_CMD_IP_ADDRESS, ip, sizeof(ip), &error);
    g_assert_no_error(error);
    gsize length = 0;
    const guint8 *actual = g_bytes_get_data(frame, &length);
    g_assert_cmpuint(length, ==, sizeof(expected_ip));
    g_assert_cmpmem(actual, length, expected_ip, sizeof(expected_ip));
    g_bytes_unref(frame);

    frame = jpt_device_u16_request(JPT_CMD_TCP_PORT, 5000, &error);
    g_assert_no_error(error);
    actual = g_bytes_get_data(frame, &length);
    g_assert_cmpuint(length, ==, sizeof(expected_port));
    g_assert_cmpmem(actual, length, expected_port, sizeof(expected_port));
    g_bytes_unref(frame);
}

static void test_decode_status_db25_power(void)
{
    JptTelemetry telemetry = {0};
    jpt_device_decode_status(0x18, &telemetry);
    g_assert_true(telemetry.pump_enabled);
    g_assert_cmpuint(telemetry.db25_power, ==, 0x80);
    gdouble percent = (gdouble)telemetry.db25_power * 100.0 / 255.0;
    g_assert_cmpfloat(ABS(percent - 50.196), <=, 0.001);
}

static void test_device_info(void)
{
    JptDeviceInfo info;
    GError *error = NULL;
    const guint8 version[] = {'M', '3', ' ', 0, 0, 0, 0, 0};
    JptFrame *frame = jpt_frame_new(JPT_CMD_MCU_VERSION, version, sizeof(version));
    g_assert_true(jpt_device_decode_info(frame, &info, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(info.text, ==, "M3");
    jpt_frame_free(frame);

    const guint8 time[] = {1, 2, 3, 4, 5, 6};
    frame = jpt_frame_new(JPT_CMD_LASER_TIME, time, sizeof(time));
    g_assert_true(jpt_device_decode_info(frame, &info, &error));
    g_assert_cmpuint(info.time.powered_minutes, ==, 0x030201);
    g_assert_cmpuint(info.time.emission_minutes, ==, 0x060504);
    jpt_frame_free(frame);

    frame = jpt_frame_new(JPT_CMD_READ_PREFERRED_MODE, (const guint8[]){0x24, 0x2a}, 2);
    g_assert_true(jpt_device_decode_info(frame, &info, &error));
    g_assert_cmpuint(info.value, ==, 0x2a24);
    jpt_frame_free(frame);
    frame = jpt_frame_new(JPT_CMD_LASER_TIME, time, sizeof(time) - 1);
    g_assert_false(jpt_device_decode_info(frame, &info, &error));
    g_assert_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD);
    g_clear_error(&error);
    jpt_frame_free(frame);
}

static void test_m7_table(void)
{
    guint16 input[JPT_M7_CUTOFF_COUNT] = {0x1234, 0x2a24}, output[JPT_M7_CUTOFF_COUNT] = {0};
    input[JPT_M7_CUTOFF_COUNT - 1] = 65535;
    GBytes *payload = jpt_device_m7_cutoff_payload(input);
    gsize length;
    const guint8 *wire = g_bytes_get_data(payload, &length);
    g_assert_cmpuint(length, ==, 22);
    g_assert_cmpmem(wire, 4, ((guint8[]){0x34, 0x12, 0x24, 0x2a}), 4);
    guint8 prefixed[24] = {0xff, 0xee};
    memcpy(prefixed + 2, wire, length);
    for (guint offset = 0; offset <= 2; offset += 2) {
        JptFrame *frame = jpt_frame_new(JPT_CMD_READ_CUTOFF_FREQUENCY_M7, prefixed + offset, 24 - offset);
        g_assert_true(jpt_device_decode_m7_cutoffs(frame, output, NULL));
        g_assert_cmpmem(input, sizeof(input), output, sizeof(output));
        jpt_frame_free(frame);
    }
    JptFrame *short_frame = jpt_frame_new(JPT_CMD_READ_CUTOFF_FREQUENCY_M7, wire, length - 1);
    GError *error = NULL;
    g_assert_false(jpt_device_decode_m7_cutoffs(short_frame, output, &error));
    g_assert_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD);
    g_clear_error(&error);
    jpt_frame_free(short_frame);
    g_bytes_unref(payload);
}

static void test_alarm_cache(void)
{
    guint8 payload[109] = {0};
    for (gsize size = 0; size <= 1; size++) {
        JptFrame *empty = jpt_frame_new(JPT_CMD_READ_ALARM_CACHE, payload, size);
        GArray *records = jpt_device_decode_alarm_cache(empty, NULL);
        g_assert_nonnull(records);
        g_assert_cmpuint(records->len, ==, 0);
        g_array_unref(records);
        jpt_frame_free(empty);
    }
    payload[0] = 1;
    payload[1] = 3;
    payload[2] = 2;
    for (guint index = 0; index < 50; index++) {
        payload[3 + 2 * index] = (guint8)index;
        payload[4 + 2 * index] = 1;
    }
    payload[103] = 22;
    payload[108] = 1;
    JptFrame *frame = jpt_frame_new(JPT_CMD_READ_ALARM_CACHE, payload, sizeof(payload));
    GArray *records = jpt_device_decode_alarm_cache(frame, NULL);
    g_assert_nonnull(records);
    g_assert_cmpuint(records->len, ==, 1);
    const JptAlarmRecord *record = &g_array_index(records, JptAlarmRecord, 0);
    g_assert_cmpuint(record->type, ==, 3);
    g_assert_cmpuint(record->category, ==, 2);
    g_assert_cmpuint(record->power[9], ==, 265);
    g_assert_cmpuint(record->frequency[0], ==, 266);
    g_assert_cmpuint(record->pulse_width[9], ==, 285);
    g_assert_cmpuint(record->extra[0][0], ==, 286);
    g_assert_cmpuint(record->extra[1][9], ==, 305);
    g_assert_cmpuint(record->powered_minutes, ==, 22);
    g_assert_cmpuint(record->emission_minutes, ==, 65536);
    g_array_unref(records);
    jpt_frame_free(frame);
    frame = jpt_frame_new(JPT_CMD_READ_ALARM_CACHE, payload, sizeof(payload) - 1);
    GError *error = NULL;
    g_assert_null(jpt_device_decode_alarm_cache(frame, &error));
    g_assert_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD);
    g_clear_error(&error);
    jpt_frame_free(frame);
}

static void test_alarm_reply_stream(void)
{
    guint8 wire[5 + 325 + 1 + 7] = {0x24, 0x93, 0, 0x45, 0x01, 3};
    wire[14] = 0x2a;
    wire[330] = 0x2a;
    memcpy(wire + 331, (guint8[]){0x24, 0x38, 0, 2, 0, 0, 0x2a}, 7);
    const gsize boundaries[] = {1, 4, 5, 76, 300, 330, sizeof(wire)};
    JptParser parser;
    jpt_parser_init(&parser);
    gsize offset = 0;
    for (guint index = 0; index < G_N_ELEMENTS(boundaries); index++) {
        GError *error = NULL;
        GPtrArray *frames = jpt_parser_push(&parser, wire + offset, boundaries[index] - offset, &error);
        g_assert_no_error(error);
        offset = boundaries[index];
        g_assert_cmpuint(frames->len, ==, offset == sizeof(wire) ? 2 : 0);
        if (frames->len) {
            JptFrame *cache = g_ptr_array_index(frames, 0);
            g_assert_cmpuint(cache->command, ==, JPT_CMD_READ_ALARM_CACHE);
            g_assert_cmpmem(g_bytes_get_data(cache->payload, NULL), g_bytes_get_size(cache->payload),
                            wire + 5, 325);
            GArray *records = jpt_device_decode_alarm_cache(cache, &error);
            g_assert_no_error(error);
            g_assert_cmpuint(records->len, ==, 3);
            g_assert_cmpuint(((JptFrame *)g_ptr_array_index(frames, 1))->command, ==, JPT_CMD_STATUS);
            g_array_unref(records);
        }
        g_ptr_array_unref(frames);
    }
    jpt_parser_clear(&parser);
}

static void test_alarm_extra(void)
{
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_SENSOR_1, 0), ==, JPT_EXTRA_TEMPERATURE);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_SENSOR_2, 1), ==, JPT_EXTRA_ABSENT);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_TEC, 0), ==, JPT_EXTRA_TEC);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_TEC, 1), ==, JPT_EXTRA_TEMPERATURE);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_SUPPLY_VOLTAGE, 0), ==, JPT_EXTRA_VOLTAGE);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_LOW_CURRENT, 0), ==, JPT_EXTRA_VOLTAGE);
    g_assert_cmpint(jpt_alarm_extra_kind(JPT_CACHE_NO_SEED, 1), ==, JPT_EXTRA_ABSENT);
    g_assert_cmpint(jpt_alarm_extra_kind(0xfe, 1), ==, JPT_EXTRA_RAW);
    g_assert_cmpfloat(jpt_alarm_extra_value(JPT_EXTRA_TEMPERATURE, 185), ==, -1.5);
    g_assert_cmpfloat(jpt_alarm_extra_value(JPT_EXTRA_TEC, 285), ==, 28.5);
    g_assert_cmpfloat(jpt_alarm_extra_value(JPT_EXTRA_VOLTAGE, 2425), ==, 24.25);
    g_assert_cmpfloat(jpt_alarm_extra_value(JPT_EXTRA_RAW, 123), ==, 123);
}

static void test_power_units(void)
{
    guint16 raw = 0;
    g_assert_true(jpt_device_power_from_percent(0, &raw));
    g_assert_cmpuint(raw, ==, 0);
    g_assert_true(jpt_device_power_from_percent(50, &raw));
    g_assert_cmpuint(raw, ==, 2048);
    g_assert_cmpfloat(ABS(jpt_device_power_to_percent(raw) - 50.0), <, 0.02);
    g_assert_true(jpt_device_power_from_percent(100, &raw));
    g_assert_cmpuint(raw, ==, 4095);
    g_assert_false(jpt_device_power_from_percent(101, &raw));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/protocol/encode/sn-request", test_encode_sn_request);
    g_test_add_func("/protocol/parser/fragmented-sn", test_parser_fragmented_sn_reply);
    g_test_add_func("/protocol/parser/coalesced-frames", test_parser_coalesced_frames_and_noise);
    g_test_add_func("/protocol/encode/oversized-payload", test_encode_rejects_oversized_payload);
    g_test_add_func("/protocol/encode/tcp-address-update", test_encode_tcp_address_update);
    g_test_add_func("/device/status/db25-power", test_decode_status_db25_power);
    g_test_add_func("/device/info/decode", test_device_info);
    g_test_add_func("/device/power/units", test_power_units);
    g_test_add_func("/device/m7/cutoffs", test_m7_table);
    g_test_add_func("/device/alarm/cache", test_alarm_cache);
    g_test_add_func("/device/alarm/extra", test_alarm_extra);
    g_test_add_func("/protocol/parser/extended-alarm-reply", test_alarm_reply_stream);
    return g_test_run();
}
