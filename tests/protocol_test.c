#include "jpt_device.h"

#include <glib.h>

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

static void test_decode_status_db25_power(void)
{
    JptTelemetry telemetry = {0};
    jpt_device_decode_status(0x18, &telemetry);
    g_assert_true(telemetry.pump_enabled);
    g_assert_cmpuint(telemetry.db25_power, ==, 0x80);
    g_assert_cmpfloat_with_epsilon((gdouble)telemetry.db25_power * 100.0 / 255.0, 50.196, 0.001);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/protocol/encode/sn-request", test_encode_sn_request);
    g_test_add_func("/protocol/parser/fragmented-sn", test_parser_fragmented_sn_reply);
    g_test_add_func("/protocol/parser/coalesced-frames", test_parser_coalesced_frames_and_noise);
    g_test_add_func("/protocol/encode/oversized-payload", test_encode_rejects_oversized_payload);
    g_test_add_func("/device/status/db25-power", test_decode_status_db25_power);
    return g_test_run();
}
