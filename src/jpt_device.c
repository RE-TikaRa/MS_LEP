#include "jpt_device.h"

#include <string.h>

static guint16 read_word(const guint8 *data)
{
    return data[0] | ((guint16)data[1] << 8);
}

static guint32 read_minutes(const guint8 *data)
{
    return read_word(data) | ((guint32)data[2] << 16);
}

gboolean jpt_device_power_from_percent(guint percent, guint16 *raw)
{
    if (percent > 100) {
        return FALSE;
    }
    *raw = (guint16)((percent * 4095 + 50) / 100);
    return TRUE;
}

gdouble jpt_device_power_to_percent(guint16 raw)
{
    return raw * 100.0 / 4095.0;
}

GBytes *jpt_device_read_request(guint16 command, GError **error)
{
    const guint8 zero[] = {0x00, 0x00};
    return jpt_frame_encode(command, zero, sizeof(zero), error);
}
GBytes *jpt_device_u16_request(guint16 command, guint16 value, GError **error)
{
    const guint8 payload[] = {(guint8)(value & 0xff), (guint8)(value >> 8)};
    return jpt_frame_encode(command, payload, sizeof(payload), error);
}

gboolean jpt_device_decode_u16(const JptFrame *frame, guint16 *value, GError **error)
{
    return jpt_frame_read_u16(frame, 0, value, error);
}

gboolean jpt_device_decode_serial(const JptFrame *frame, gchar *serial, gsize serial_size, GError **error)
{
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(frame->payload, &length);
    if (serial_size == 0 || length + 1 > serial_size) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                    "JPT serial number does not fit the destination buffer");
        return FALSE;
    }

    memcpy(serial, payload, length);
    serial[length] = '\0';
    g_strstrip(serial);
    return TRUE;
}

gboolean jpt_device_decode_monitoring(const JptFrame *frame, JptTelemetry *telemetry, GError **error)
{
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(frame->payload, &length);
    if (length < 8) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                    "JPT monitoring response contains %zu bytes; need 8", length);
        return FALSE;
    }

    guint16 sensor_2 = (guint16)payload[0] | ((guint16)payload[1] << 8);
    guint16 sensor_1 = (guint16)payload[2] | ((guint16)payload[3] << 8);
    guint16 seed_tec = (guint16)payload[4] | ((guint16)payload[5] << 8);
    guint16 current = (guint16)payload[6] | ((guint16)payload[7] << 8);

    telemetry->sensor_1_celsius = (gdouble)sensor_1 / 10.0 - 20.0;
    telemetry->sensor_2_celsius = (gdouble)sensor_2 / 10.0 - 20.0;
    telemetry->sensor_1_fault = sensor_1 == 0x04a6;
    telemetry->sensor_2_fault = sensor_2 == 0x04a6;
    telemetry->seed_tec_celsius = (gdouble)seed_tec / 10.0;
    telemetry->output_current_amp = (gdouble)current / 10.0;
    return TRUE;
}

gboolean jpt_device_decode_alarm_counts(const JptFrame *frame, JptTelemetry *telemetry, GError **error)
{
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(frame->payload, &length);
    if (length < 16) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                    "JPT alarm-count response contains %zu bytes; need 16", length);
        return FALSE;
    }

    for (guint i = 0; i < G_N_ELEMENTS(telemetry->alarm_counts); i++) {
        telemetry->alarm_counts[i] = (guint16)payload[i * 2] | ((guint16)payload[i * 2 + 1] << 8);
    }
    return TRUE;
}

void jpt_device_decode_status(guint16 status, JptTelemetry *telemetry)
{
    telemetry->status = status;
    telemetry->pa_enabled = (status & (1u << 0)) != 0;
    telemetry->mo_enabled = (status & (1u << 1)) != 0;
    telemetry->red_light_enabled = (status & (1u << 2)) != 0;
    telemetry->pump_enabled = (status & (1u << 3)) != 0;
    telemetry->db25_power = 0;
    for (guint bit = 0; bit < 8; bit++) {
        if (status & (1u << (bit + 4))) {
            telemetry->db25_power |= (guint8)(1u << (7 - bit));
        }
    }
}

gboolean jpt_device_decode_info(const JptFrame *frame, JptDeviceInfo *info, GError **error)
{
    gsize length = 0;
    const guint8 *data = g_bytes_get_data(frame->payload, &length);
    JptDeviceInfo decoded = {.command = frame->command};
    switch (frame->command) {
    case JPT_CMD_MCU_VERSION:
    case JPT_CMD_FPGA_VERSION:
    case JPT_CMD_SEED_BOARD_VERSION:
    case JPT_CMD_READ_MACHINE_TYPE:
    case JPT_CMD_SERIAL_NUMBER: {
        gsize limit = frame->command == JPT_CMD_SERIAL_NUMBER ? 255 :
                      frame->command == JPT_CMD_READ_MACHINE_TYPE ? 30 : 8;
        if (length > limit) {
            break;
        }
        while (length && (data[length - 1] == 0 || g_ascii_isspace(data[length - 1]))) {
            length--;
        }
        if (!length || !g_utf8_validate((const gchar *)data, length, NULL)) {
            break;
        }
        for (gsize index = 0; index < length; index++) {
            if (data[index] < 0x20 || data[index] == 0x7f) {
                goto invalid;
            }
        }
        memcpy(decoded.text, data, length);
        *info = decoded;
        return TRUE;
    }
    case JPT_CMD_LASER_TIME:
        if (length != 6) {
            break;
        }
        decoded.time.powered_minutes = read_minutes(data);
        decoded.time.emission_minutes = read_minutes(data + 3);
        *info = decoded;
        return TRUE;
    case JPT_CMD_READ_PREFERRED_MODE:
    case JPT_CMD_READ_EXTENDED_ALARM_COUNT:
    case JPT_CMD_KEY_STATE:
    case JPT_CMD_INTERLOCK_STATE:
        if (length != 2) {
            break;
        }
        decoded.value = read_word(data);
        *info = decoded;
        return TRUE;
    default:
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_UNEXPECTED_COMMAND,
                    "Not an information reply: 0x%04X", frame->command);
        return FALSE;
    }
invalid:
    g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                "Invalid information payload for 0x%04X (%zu bytes)",
                frame->command, g_bytes_get_size(frame->payload));
    return FALSE;
}

gboolean jpt_device_decode_m7_cutoffs(const JptFrame *frame, guint16 values[JPT_M7_CUTOFF_COUNT],
                                      GError **error)
{
    gsize length;
    const guint8 *data = g_bytes_get_data(frame->payload, &length);
    if (frame->command != JPT_CMD_READ_CUTOFF_FREQUENCY_M7 ||
        (length != 2 * JPT_M7_CUTOFF_COUNT && length != 2 + 2 * JPT_M7_CUTOFF_COUNT)) {
        g_set_error_literal(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                            "Invalid M7 cutoff table");
        return FALSE;
    }
    data += length - 2 * JPT_M7_CUTOFF_COUNT;
    for (guint index = 0; index < JPT_M7_CUTOFF_COUNT; index++) {
        values[index] = read_word(data + 2 * index);
    }
    return TRUE;
}

GBytes *jpt_device_m7_cutoff_payload(const guint16 values[JPT_M7_CUTOFF_COUNT])
{
    guint8 payload[2 * JPT_M7_CUTOFF_COUNT];
    for (guint index = 0; index < JPT_M7_CUTOFF_COUNT; index++) {
        payload[2 * index] = (guint8)values[index];
        payload[2 * index + 1] = (guint8)(values[index] >> 8);
    }
    return g_bytes_new(payload, sizeof(payload));
}

GArray *jpt_device_decode_alarm_cache(const JptFrame *frame, GError **error)
{
    gsize length;
    const guint8 *data = g_bytes_get_data(frame->payload, &length);
    if (frame->command != JPT_CMD_READ_ALARM_CACHE || length < 6 || length != 6u + data[5] * 108u) {
        g_set_error_literal(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                            "Invalid alarm cache record count or payload length");
        return NULL;
    }
    guint count = data[5];
    GArray *records = g_array_sized_new(FALSE, FALSE, sizeof(JptAlarmRecord), count);
    for (guint index = 0; index < count; index++) {
        const guint8 *record = data + 6 + index * 108;
        JptAlarmRecord item = {.type = record[0], .category = record[1],
                               .powered_minutes = read_minutes(record + 102),
                               .emission_minutes = read_minutes(record + 105)};
        guint16 *series[] = {item.power, item.frequency, item.pulse_width, item.extra[0], item.extra[1]};
        for (guint channel = 0; channel < G_N_ELEMENTS(series); channel++) {
            for (guint sample = 0; sample < JPT_ALARM_SAMPLE_COUNT; sample++) {
                series[channel][sample] = read_word(record + 2 + 2 * (channel * JPT_ALARM_SAMPLE_COUNT + sample));
            }
        }
        g_array_append_val(records, item);
    }
    return records;
}
