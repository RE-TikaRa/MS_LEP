#include "jpt_device.h"

#include <string.h>

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
        decoded.time.powered_minutes = data[0] | ((guint32)data[1] << 8) | ((guint32)data[2] << 16);
        decoded.time.emission_minutes = data[3] | ((guint32)data[4] << 8) | ((guint32)data[5] << 16);
        *info = decoded;
        return TRUE;
    case JPT_CMD_READ_PREFERRED_MODE:
    case JPT_CMD_READ_EXTENDED_ALARM_COUNT:
    case JPT_CMD_KEY_STATE:
    case JPT_CMD_INTERLOCK_STATE:
        if (length != 2) {
            break;
        }
        decoded.value = data[0] | ((guint16)data[1] << 8);
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
