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
