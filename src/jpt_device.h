#pragma once

#include "jpt_protocol.h"

G_BEGIN_DECLS

enum {
    JPT_CMD_POWER = 0x01,
    JPT_CMD_FREQUENCY = 0x02,
    JPT_CMD_PULSE_WIDTH = 0x03,
    JPT_CMD_SIMMER = 0x04,
    JPT_CMD_DEFAULT_SIMMER = 0x04,
    JPT_CMD_DEFAULT_PULSE_WIDTH = 0x05,
    JPT_CMD_LOCK_FREQUENCY = 0x06,
    JPT_CMD_PARAMETER_SELECT = 0x07,
    JPT_CMD_CONTROL_MODE = 0x08,
    JPT_CMD_GUI_PA = 0x09,
    JPT_CMD_GUI_MO = 0x0A,
    JPT_CMD_RED_LIGHT = 0x0B,
    JPT_CMD_LOCK_POWER = 0x17,
    JPT_CMD_RED_LIGHT_POWER = 0x18,
    JPT_CMD_LOCK_PARAMETER = 0x19,
    JPT_CMD_READ_CONTROL_MODE = 0x36,
    JPT_CMD_STATUS = 0x38,
    JPT_CMD_REAL_POWER = 0x39,
    JPT_CMD_REAL_PULSE_WIDTH = 0x3A,
    JPT_CMD_REAL_FREQUENCY = 0x3B,
    JPT_CMD_MONITORING = 0x3C,
    JPT_CMD_SERIAL_NUMBER = 0x3F,
    JPT_CMD_ALARM_STATE = 0x40,
    JPT_CMD_ALARM_COUNTS = 0x42,
    JPT_CMD_READ_DEFAULT_SIMMER = 0x32,
    JPT_CMD_READ_DEFAULT_PULSE_WIDTH = 0x33,
    JPT_CMD_READ_LOCK_FREQUENCY = 0x34,
    JPT_CMD_READ_PARAMETER_SELECT = 0x35,
    JPT_CMD_READ_LOCK_POWER = 0x47,
    JPT_CMD_READ_RED_LIGHT_POWER = 0x48,
    JPT_CMD_READ_LOCK_PARAMETER = 0x49,
};

typedef struct {
    guint16 control_mode;
    guint16 status;
    guint16 parameter_select;
    guint16 lock_options;
    guint16 default_simmer;
    guint16 default_pulse_width;
    guint16 lock_frequency;
    guint16 lock_power;
    guint16 red_light_power;
    guint16 real_power_percent;
    guint16 real_pulse_width;
    guint16 real_frequency;
    gdouble sensor_1_celsius;
    gdouble sensor_2_celsius;
    gboolean sensor_1_fault;
    gboolean sensor_2_fault;
    gdouble seed_tec_celsius;
    gdouble output_current_amp;
    guint16 alarm_state;
    guint16 alarm_counts[8];
    gchar serial_number[32];
} JptTelemetry;

GBytes *jpt_device_read_request(guint16 command, GError **error);
GBytes *jpt_device_u16_request(guint16 command, guint16 value, GError **error);
gboolean jpt_device_decode_u16(const JptFrame *frame, guint16 *value, GError **error);
gboolean jpt_device_decode_serial(const JptFrame *frame, gchar *serial, gsize serial_size, GError **error);
gboolean jpt_device_decode_monitoring(const JptFrame *frame, JptTelemetry *telemetry, GError **error);
gboolean jpt_device_decode_alarm_counts(const JptFrame *frame, JptTelemetry *telemetry, GError **error);

G_END_DECLS
