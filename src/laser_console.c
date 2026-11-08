#include "laser_console.h"

#include "jpt_client.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef struct _LaserConsole LaserConsole;

static gboolean poll_tick(gpointer user_data);

struct _LaserConsole {
    GtkWidget *window;
    GtkDropDown *transport_dropdown;
    GtkEntry *endpoint_entry;
    GtkButton *connect_button;
    GtkButton *laser_button;
    GtkButton *red_button;
    GtkButton *mo_button;
    GtkCheckButton *safety_ack;
    GtkCheckButton *free_mode;
    GtkCheckButton *gui_mode;
    GtkCheckButton *control_options[5];
    GtkCheckButton *parameter_options[6];
    GtkCheckButton *lock_options[2];
    GtkLabel *connection_label;
    GtkLabel *serial_label;
    GtkLabel *serial_status_label;
    GtkLabel *last_command_label;
    GtkLabel *last_error_label;
    GtkLabel *real_power_label;
    GtkLabel *real_frequency_label;
    GtkLabel *real_pulse_label;
    GtkLabel *sensor_1_label;
    GtkLabel *sensor_2_label;
    GtkLabel *tec_label;
    GtkLabel *current_label;
    GtkLabel *control_mode_label;
    GtkLabel *status_label;
    GtkLabel *alarm_state_label;
    GtkLabel *status_indicators[5];
    GtkLabel *power_indicators[8];
    GtkLabel *alarm_indicators[8];
    GtkLabel *alarm_counts[8];
    GtkTextBuffer *log_buffer;
    GtkSpinButton *power_spin;
    GtkSpinButton *frequency_spin;
    GtkSpinButton *pulse_spin;
    GtkSpinButton *simmer_spin;
    GtkSpinButton *default_frequency_spin;
    GtkSpinButton *default_pulse_spin;
    GtkSpinButton *default_simmer_spin;
    GtkSpinButton *lock_power_spin;
    GtkSpinButton *red_power_spin;
    GtkProgressBar *power_bar;
    GtkWidget *chart;
    GPtrArray *write_buttons;
    JptClient *client;
    JptTelemetry telemetry;
    gdouble power_samples[120];
    guint sample_count;
    guint sample_index;
    guint poll_source;
    gboolean connected;
    gboolean poll_pending;
    gboolean updating_controls;
    gboolean initialized;
    gboolean closing;
    gboolean laser_on;
    gboolean red_on;
    gboolean mo_on;
};

static const gchar *alarm_names[] = {
    "传感器 1 过温",
    "传感器 2 过温",
    "一级电流低",
    "种子源 TEC",
    "种子源无脉冲",
    "供电电压",
    "传感器 1 低温",
    "传感器 2 低温",
};

static void append_log(LaserConsole *console, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    gchar *message = g_strdup_vprintf(format, args);
    va_end(args);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(console->log_buffer, &end);
    gtk_text_buffer_insert(console->log_buffer, &end, message, -1);
    gtk_text_buffer_insert(console->log_buffer, &end, "\n", 1);
    g_free(message);
}

static gchar *format_bytes(GBytes *bytes)
{
    if (bytes == NULL) {
        return g_strdup("-");
    }

    gsize length = 0;
    const guint8 *data = g_bytes_get_data(bytes, &length);
    GString *text = g_string_new(NULL);
    for (gsize i = 0; i < length; i++) {
        if (i > 0) {
            g_string_append_c(text, ' ');
        }
        g_string_append_printf(text, "%02X", data[i]);
    }
    return g_string_free(text, FALSE);
}

static void trace_callback(guint16 command, GBytes *transmitted, GBytes *received,
                           const GError *error, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (console->closing) {
        return;
    }
    gchar *tx = format_bytes(transmitted);
    gchar *rx = format_bytes(received);
    append_log(console, "TX 0x%04X  %s", command, tx);
    if (error != NULL) {
        append_log(console, "RX 0x%04X  %s  [%s]", command, rx, error->message);
    } else {
        append_log(console, "RX 0x%04X  %s", command, rx);
    }
    g_free(tx);
    g_free(rx);
}

static void set_indicator(GtkLabel *label, gboolean active, gboolean alarm)
{
    gtk_widget_remove_css_class(GTK_WIDGET(label), "indicator-ok");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "indicator-alarm");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "indicator-muted");
    gtk_widget_add_css_class(GTK_WIDGET(label), alarm ? "indicator-alarm" :
                             active ? "indicator-ok" : "indicator-muted");
    gtk_label_set_text(label, alarm ? "报警" : active ? "正常" : "关闭");
}

static void set_connection_state(LaserConsole *console, const gchar *text, gboolean connected, gboolean error)
{
    gtk_label_set_text(console->connection_label, text);
    gtk_widget_remove_css_class(GTK_WIDGET(console->connection_label), "state-ok");
    gtk_widget_remove_css_class(GTK_WIDGET(console->connection_label), "state-error");
    gtk_widget_remove_css_class(GTK_WIDGET(console->connection_label), "state-muted");
    gtk_widget_add_css_class(GTK_WIDGET(console->connection_label), error ? "state-error" :
                             connected ? "state-ok" : "state-muted");
}

static void update_output_buttons(LaserConsole *console)
{
    gboolean enabled = console->connected && gtk_check_button_get_active(console->safety_ack);
    gboolean gui_mo = (console->telemetry.control_mode & (1u << 0)) != 0;
    gboolean gui_pa = (console->telemetry.control_mode & (1u << 1)) != 0;
    gtk_widget_set_sensitive(GTK_WIDGET(console->laser_button), enabled && gui_mo && gui_pa);
    gtk_widget_set_sensitive(GTK_WIDGET(console->red_button), enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(console->mo_button), enabled && gui_mo);
    if (console->free_mode != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->free_mode), console->connected);
        gtk_widget_set_sensitive(GTK_WIDGET(console->gui_mode), console->connected);
        for (guint i = 0; i < G_N_ELEMENTS(console->control_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->control_options[i]), console->connected);
        }
        for (guint i = 0; i < G_N_ELEMENTS(console->parameter_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->parameter_options[i]), console->connected);
        }
        for (guint i = 0; i < G_N_ELEMENTS(console->lock_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->lock_options[i]), console->connected);
        }
    }
    if (console->write_buttons != NULL) {
        for (guint i = 0; i < console->write_buttons->len; i++) {
            GtkWidget *button = g_ptr_array_index(console->write_buttons, i);
            guint bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "control-bit"));
            gtk_widget_set_sensitive(button, console->connected &&
                                     (bit == 0 || (console->telemetry.control_mode & (1u << bit))));
        }
    }
    gtk_button_set_label(console->laser_button, console->laser_on ? "关闭激光" : "开启激光");
    gtk_button_set_label(console->red_button, console->red_on ? "关闭红光" : "开启红光");
    gtk_button_set_label(console->mo_button, console->mo_on ? "关闭 MO" : "开启 MO");
    if (console->laser_on) {
        gtk_widget_add_css_class(GTK_WIDGET(console->laser_button), "danger-active");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(console->laser_button), "danger-active");
    }
}

static void update_telemetry(LaserConsole *console, const JptTelemetry *telemetry)
{
    console->telemetry.status = telemetry->status;
    console->telemetry.control_mode = telemetry->control_mode;
    console->telemetry.parameter_select = telemetry->parameter_select;
    console->telemetry.lock_options = telemetry->lock_options;
    console->telemetry.default_simmer = telemetry->default_simmer;
    console->telemetry.default_pulse_width = telemetry->default_pulse_width;
    console->telemetry.lock_frequency = telemetry->lock_frequency;
    console->telemetry.lock_power = telemetry->lock_power;
    console->telemetry.red_light_power = telemetry->red_light_power;
    console->telemetry.real_power_percent = telemetry->real_power_percent;
    console->telemetry.real_pulse_width = telemetry->real_pulse_width;
    console->telemetry.real_frequency = telemetry->real_frequency;
    console->telemetry.sensor_1_celsius = telemetry->sensor_1_celsius;
    console->telemetry.sensor_2_celsius = telemetry->sensor_2_celsius;
    console->telemetry.sensor_1_fault = telemetry->sensor_1_fault;
    console->telemetry.sensor_2_fault = telemetry->sensor_2_fault;
    console->telemetry.seed_tec_celsius = telemetry->seed_tec_celsius;
    console->telemetry.output_current_amp = telemetry->output_current_amp;
    console->telemetry.alarm_state = telemetry->alarm_state;
    memcpy(console->telemetry.alarm_counts, telemetry->alarm_counts, sizeof(telemetry->alarm_counts));
    if (telemetry->serial_number[0] != '\0') {
        g_strlcpy(console->telemetry.serial_number, telemetry->serial_number,
                  sizeof(console->telemetry.serial_number));
    }
    const JptTelemetry *current = &console->telemetry;
    gtk_label_set_text(console->serial_label, current->serial_number[0] ? current->serial_number : "未读取");
    gtk_label_set_text(console->serial_status_label, current->serial_number[0] ? current->serial_number : "SN: 未读取");
    gchar *text = g_strdup_printf("%u %%", current->real_power_percent);
    gtk_label_set_text(console->real_power_label, text);
    gtk_progress_bar_set_fraction(console->power_bar,
                                  CLAMP((gdouble)current->real_power_percent / 100.0, 0.0, 1.0));
    g_free(text);
    text = g_strdup_printf("%u kHz", current->real_frequency);
    gtk_label_set_text(console->real_frequency_label, text);
    g_free(text);
    text = g_strdup_printf("%u ns", current->real_pulse_width);
    gtk_label_set_text(console->real_pulse_label, text);
    g_free(text);
    text = current->sensor_1_fault ? g_strdup("传感器故障") :
        g_strdup_printf("%.1f °C", current->sensor_1_celsius);
    gtk_label_set_text(console->sensor_1_label, text);
    g_free(text);
    text = current->sensor_2_fault ? g_strdup("传感器故障") :
        g_strdup_printf("%.1f °C", current->sensor_2_celsius);
    gtk_label_set_text(console->sensor_2_label, text);
    g_free(text);
    text = g_strdup_printf("%.1f °C", current->seed_tec_celsius);
    gtk_label_set_text(console->tec_label, text);
    g_free(text);
    text = g_strdup_printf("%.1f A", current->output_current_amp);
    gtk_label_set_text(console->current_label, text);
    g_free(text);

    text = g_strdup_printf("0x%04X", current->control_mode);
    gtk_label_set_text(console->control_mode_label, text);
    g_free(text);
    text = g_strdup_printf("0x%04X", current->status);
    gtk_label_set_text(console->status_label, text);
    g_free(text);
    text = g_strdup_printf("0x%04X", current->alarm_state);
    gtk_label_set_text(console->alarm_state_label, text);
    g_free(text);

    set_indicator(console->status_indicators[0], current->status & (1u << 0), FALSE);
    set_indicator(console->status_indicators[1], current->status & (1u << 1), FALSE);
    set_indicator(console->status_indicators[2], current->status & (1u << 2), FALSE);
    set_indicator(console->status_indicators[3], current->status & (1u << 3), FALSE);
    for (guint i = 0; i < G_N_ELEMENTS(console->power_indicators); i++) {
        gboolean active = current->status & (1u << (4 + i));
        gchar *name = g_strdup_printf("D%u  %u", 7 - i, active ? 1 : 0);
        gtk_label_set_text(console->power_indicators[i], name);
        gtk_widget_remove_css_class(GTK_WIDGET(console->power_indicators[i]), "indicator-ok");
        gtk_widget_remove_css_class(GTK_WIDGET(console->power_indicators[i]), "indicator-muted");
        gtk_widget_add_css_class(GTK_WIDGET(console->power_indicators[i]), active ? "indicator-ok" : "indicator-muted");
        g_free(name);
    }

    for (guint i = 0; i < G_N_ELEMENTS(console->alarm_indicators); i++) {
        gboolean alarm = current->alarm_state & (1u << i);
        set_indicator(console->alarm_indicators[i], !alarm, alarm);
        gchar *count = g_strdup_printf("%u", current->alarm_counts[i]);
        gtk_label_set_text(console->alarm_counts[i], count);
        g_free(count);
    }

    console->laser_on = (current->status & (1u << 0)) != 0;
    console->mo_on = (current->status & (1u << 1)) != 0;
    console->red_on = (current->status & (1u << 2)) != 0;
    update_output_buttons(console);
    console->power_samples[console->sample_index] = CLAMP(current->real_power_percent, 0, 100);
    console->sample_index = (console->sample_index + 1) % G_N_ELEMENTS(console->power_samples);
    console->sample_count = MIN(console->sample_count + 1, G_N_ELEMENTS(console->power_samples));
    if (console->chart != NULL) {
        gtk_widget_queue_draw(console->chart);
    }
}

static void result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (console->closing) {
        return;
    }
    if (result->type == JPT_CLIENT_CONNECTION) {
        if (result->connected) {
            console->connected = TRUE;
            gtk_button_set_label(console->connect_button, "断开");
            set_connection_state(console, "已连接", TRUE, FALSE);
            append_log(console, "连接已建立，开始读取设备信息");
            console->initialized = FALSE;
            jpt_client_read_initial_async(console->client, result_callback, console);
        } else {
            console->connected = FALSE;
            set_connection_state(console, result->error ? result->error->message : "连接失败", FALSE, TRUE);
            gtk_button_set_label(console->connect_button, "连接");
            if (result->error != NULL) {
                gtk_label_set_text(console->last_error_label, result->error->message);
            }
        }
        update_output_buttons(console);
        return;
    }

    if (result->type == JPT_CLIENT_DISCONNECTED) {
        console->connected = FALSE;
        console->poll_pending = FALSE;
        console->initialized = FALSE;
        set_connection_state(console, "未连接", FALSE, FALSE);
        gtk_button_set_label(console->connect_button, "连接");
        update_output_buttons(console);
        return;
    }

    if (result->type == JPT_CLIENT_TELEMETRY) {
        console->poll_pending = FALSE;
        if (result->error != NULL) {
            gtk_label_set_text(console->last_error_label, result->error->message);
            append_log(console, "轮询失败：%s", result->error->message);
            return;
        }
        update_telemetry(console, &result->telemetry);
        if (!console->initialized) {
            gtk_spin_button_set_value(console->default_simmer_spin, result->telemetry.default_simmer);
            gtk_spin_button_set_value(console->default_pulse_spin, result->telemetry.default_pulse_width);
            gtk_spin_button_set_value(console->default_frequency_spin, result->telemetry.lock_frequency);
            gtk_spin_button_set_value(console->lock_power_spin,
                                      (gdouble)result->telemetry.lock_power * 100.0 / 4095.0);
            gtk_spin_button_set_value(console->red_power_spin, result->telemetry.red_light_power);
            gtk_spin_button_set_value(console->frequency_spin, result->telemetry.real_frequency);
            gtk_spin_button_set_value(console->pulse_spin, result->telemetry.real_pulse_width);
            console->updating_controls = TRUE;
            gtk_check_button_set_active(console->gui_mode, result->telemetry.control_mode == 0x1f);
            gtk_check_button_set_active(console->free_mode, result->telemetry.control_mode != 0x1f);
            for (guint i = 0; i < G_N_ELEMENTS(console->control_options); i++) {
                gtk_check_button_set_active(console->control_options[i],
                                            (result->telemetry.control_mode & (1u << i)) != 0);
            }
            const guint parameter_bits[] = {0, 1, 3, 4, 5, 6};
            for (guint i = 0; i < G_N_ELEMENTS(console->parameter_options); i++) {
                gtk_check_button_set_active(console->parameter_options[i],
                                            (result->telemetry.parameter_select & (1u << parameter_bits[i])) != 0);
            }
            for (guint i = 0; i < G_N_ELEMENTS(console->lock_options); i++) {
                gtk_check_button_set_active(console->lock_options[i],
                                            (result->telemetry.lock_options & (1u << i)) != 0);
            }
            console->updating_controls = FALSE;
            console->initialized = TRUE;
        }
        if (console->poll_source == 0) {
            console->poll_source = g_timeout_add_seconds(1, poll_tick, console);
        }
        return;
    }

    if (result->type == JPT_CLIENT_EXCHANGE) {
        if (result->error != NULL) {
            gtk_label_set_text(console->last_error_label, result->error->message);
            append_log(console, "命令失败：%s", result->error->message);
        } else if (result->exchange != NULL) {
            gchar *message = g_strdup_printf("命令 0x%04X 已确认", result->exchange->frame->command);
            gtk_label_set_text(console->last_command_label, message);
            g_free(message);
        }
    }
}

static gboolean poll_tick(gpointer user_data)
{
    LaserConsole *console = user_data;
    if (!console->connected) {
        console->poll_source = 0;
        return G_SOURCE_REMOVE;
    }
    if (!console->poll_pending) {
        console->poll_pending = TRUE;
        jpt_client_poll_async(console->client, result_callback, console);
    }
    return G_SOURCE_CONTINUE;
}

static void connect_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    if (console->connected) {
        set_connection_state(console, "正在断开", FALSE, FALSE);
        jpt_client_disconnect_async(console->client, result_callback, console);
        return;
    }

    const gchar *endpoint = gtk_editable_get_text(GTK_EDITABLE(console->endpoint_entry));
    if (endpoint[0] == '\0') {
        set_connection_state(console, "请输入设备地址", FALSE, TRUE);
        return;
    }
    set_connection_state(console, "正在连接", FALSE, FALSE);
    guint selected = gtk_drop_down_get_selected(console->transport_dropdown);
    if (selected == 0) {
        jpt_client_connect_serial_async(console->client, endpoint, result_callback, console);
    } else {
        gchar **parts = g_strsplit(endpoint, ":", 2);
        guint port = 5000;
        if (parts[1] != NULL && parts[1][0] != '\0') {
            port = (guint)g_ascii_strtoull(parts[1], NULL, 10);
        }
        jpt_client_connect_tcp_async(console->client, parts[0], (guint16)port, result_callback, console);
        g_strfreev(parts);
    }
}

static void safety_changed(GtkCheckButton *button, gpointer user_data)
{
    (void)button;
    update_output_buttons(user_data);
}

static void clear_log_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(user_data), "", 0);
}

static void write_result_callback(const JptClientResult *result, gpointer user_data)
{
    result_callback(result, user_data);
}

static void send_u16(LaserConsole *console, guint16 command, guint16 value)
{
    if (!console->connected) {
        gtk_label_set_text(console->last_error_label, "设备未连接");
        return;
    }
    jpt_client_write_u16_async(console->client, command, value, write_result_callback, console);
}

static void spin_write_clicked(GtkButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    guint control_bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "control-bit"));
    if (!console->connected || (control_bit != 0 &&
        !(console->telemetry.control_mode & (1u << control_bit)))) {
        return;
    }
    GtkSpinButton *spin = g_object_get_data(G_OBJECT(button), "spin");
    guint command = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "command"));
    guint value = (guint)gtk_spin_button_get_value_as_int(spin);
    if (command == JPT_CMD_POWER || command == JPT_CMD_LOCK_POWER) {
        value = (guint)((gdouble)value * 4095.0 / 100.0 + 0.5);
    }
    send_u16(console, (guint16)command, (guint16)value);
}

static void mode_toggled(GtkCheckButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (!gtk_check_button_get_active(button) || console->updating_controls || !console->connected) {
        return;
    }
    guint16 value = button == console->gui_mode ? 0x1F : 0x00;
    console->telemetry.control_mode = value;
    console->updating_controls = TRUE;
    for (guint i = 0; i < G_N_ELEMENTS(console->control_options); i++) {
        gtk_check_button_set_active(console->control_options[i], (value & (1u << i)) != 0);
    }
    console->updating_controls = FALSE;
    send_u16(console, JPT_CMD_CONTROL_MODE, value);
    update_output_buttons(console);
}

static void control_option_toggled(GtkCheckButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (console->updating_controls || !console->connected) {
        return;
    }
    guint bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "control-bit"));
    if (gtk_check_button_get_active(button)) {
        console->telemetry.control_mode |= (guint16)(1u << bit);
    } else {
        console->telemetry.control_mode &= (guint16)~(1u << bit);
    }
    console->updating_controls = TRUE;
    gtk_check_button_set_active(console->gui_mode, FALSE);
    gtk_check_button_set_active(console->free_mode, TRUE);
    console->updating_controls = FALSE;
    send_u16(console, JPT_CMD_CONTROL_MODE, console->telemetry.control_mode);
    update_output_buttons(console);
}

static void mask_option_toggled(GtkCheckButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (console->updating_controls || !console->connected) {
        return;
    }

    guint source = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "mask-source"));
    guint16 *mask = source == 0 ? &console->telemetry.parameter_select : &console->telemetry.lock_options;
    guint bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "mask-bit"));
    if (gtk_check_button_get_active(button)) {
        *mask |= (guint16)(1u << bit);
    } else {
        *mask &= (guint16)~(1u << bit);
    }
    guint command = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "mask-command"));
    send_u16(console, (guint16)command, *mask);
}

static void output_button_clicked(GtkButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (!gtk_check_button_get_active(console->safety_ack)) {
        return;
    }
    if (button == console->laser_button) {
        gboolean target = !console->laser_on;
        send_u16(console, JPT_CMD_GUI_MO, target ? 1 : 0);
        send_u16(console, JPT_CMD_GUI_PA, target ? 1 : 0);
        console->laser_on = target;
        console->mo_on = target;
    } else if (button == console->red_button) {
        console->red_on = !console->red_on;
        send_u16(console, JPT_CMD_RED_LIGHT, console->red_on ? 1 : 0);
    } else {
        console->mo_on = !console->mo_on;
        send_u16(console, JPT_CMD_GUI_MO, console->mo_on ? 1 : 0);
    }
    update_output_buttons(console);
}

static void emergency_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    if (console->connected) {
        send_u16(console, JPT_CMD_GUI_PA, 0);
        send_u16(console, JPT_CMD_GUI_MO, 0);
        send_u16(console, JPT_CMD_RED_LIGHT, 0);
    }
    console->laser_on = FALSE;
    console->mo_on = FALSE;
    console->red_on = FALSE;
    update_output_buttons(console);
    append_log(console, "已发送 PA=0、MO=0、红光=0");
}

static GtkWidget *make_value_row(const gchar *title, GtkWidget *value, GtkWidget *button)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "value-row");
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), value);
    if (button != NULL) {
        gtk_box_append(GTK_BOX(row), button);
    }
    return row;
}

static GtkSpinButton *make_spin(gdouble min, gdouble max, gdouble step, gdouble value)
{
    GtkSpinButton *spin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(min, max, step));
    gtk_spin_button_set_value(spin, value);
    gtk_widget_set_size_request(GTK_WIDGET(spin), 120, -1);
    return spin;
}

static GtkWidget *make_card(const gchar *title, GtkWidget **content)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "card");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    GtkWidget *heading = gtk_label_new(title);
    gtk_widget_add_css_class(heading, "card-title");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);
    gtk_frame_set_child(GTK_FRAME(frame), box);
    *content = box;
    return frame;
}

static GtkWidget *make_metric(const gchar *title, GtkLabel **value)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_add_css_class(box, "metric");
    GtkWidget *title_label = gtk_label_new(title);
    gtk_widget_add_css_class(title_label, "metric-title");
    gtk_widget_add_css_class(title_label, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    *value = GTK_LABEL(gtk_label_new("--"));
    gtk_widget_add_css_class(GTK_WIDGET(*value), "metric-value");
    gtk_label_set_xalign(*value, 0.0f);
    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(*value));
    return box;
}

static void add_spin_setting(GtkWidget *box, const gchar *title, GtkSpinButton *spin,
                             guint command, guint control_bit, LaserConsole *console)
{
    GtkWidget *button = gtk_button_new_with_label("写入");
    gtk_widget_add_css_class(button, "compact-button");
    g_object_set_data(G_OBJECT(button), "spin", spin);
    g_object_set_data(G_OBJECT(button), "command", GUINT_TO_POINTER(command));
    g_object_set_data(G_OBJECT(button), "control-bit", GUINT_TO_POINTER(control_bit));
    g_signal_connect(button, "clicked", G_CALLBACK(spin_write_clicked), console);
    if (console->write_buttons != NULL) {
        g_ptr_array_add(console->write_buttons, button);
    }
    gtk_box_append(GTK_BOX(box), make_value_row(title, GTK_WIDGET(spin), button));
}

static GtkCheckButton *add_control_option(GtkWidget *grid, const gchar *title, guint column, guint row,
                                          guint bit, LaserConsole *console)
{
    GtkCheckButton *button = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(title));
    g_object_set_data(G_OBJECT(button), "control-bit", GUINT_TO_POINTER(bit));
    g_signal_connect(button, "toggled", G_CALLBACK(control_option_toggled), console);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(button), column, row, 1, 1);
    return button;
}

static GtkCheckButton *add_mask_option(GtkWidget *grid, const gchar *title, guint column, guint row,
                                       guint command, guint bit, guint source, LaserConsole *console)
{
    GtkCheckButton *button = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(title));
    g_object_set_data(G_OBJECT(button), "mask-command", GUINT_TO_POINTER(command));
    g_object_set_data(G_OBJECT(button), "mask-bit", GUINT_TO_POINTER(bit));
    g_object_set_data(G_OBJECT(button), "mask-source", GUINT_TO_POINTER(source));
    g_signal_connect(button, "toggled", G_CALLBACK(mask_option_toggled), console);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(button), column, row, 1, 1);
    return button;
}

static GtkWidget *build_control_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_widget_set_margin_top(page, 20);
    gtk_widget_set_margin_bottom(page, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);

    GtkWidget *safety_box;
    GtkWidget *safety_card = make_card("输出许可", &safety_box);
    GtkWidget *warning = gtk_label_new("1064 nm 为不可见 IV 级激光。确认防护区域、联锁和护目镜状态后，才启用输出按钮。即使设为 0%，输出端仍可能有约 100 mW 激光。");
    gtk_label_set_wrap(GTK_LABEL(warning), TRUE);
    gtk_label_set_xalign(GTK_LABEL(warning), 0.0f);
    gtk_widget_add_css_class(warning, "warning-text");
    gtk_box_append(GTK_BOX(safety_box), warning);
    console->safety_ack = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("已确认防护区域和联锁状态"));
    gtk_box_append(GTK_BOX(safety_box), GTK_WIDGET(console->safety_ack));
    g_signal_connect(console->safety_ack, "toggled", G_CALLBACK(safety_changed), console);
    gtk_box_append(GTK_BOX(page), safety_card);

    GtkWidget *parameter_box;
    GtkWidget *parameter_card = make_card("实时参数写入", &parameter_box);
    console->power_spin = make_spin(0, 100, 1, 0);
    console->frequency_spin = make_spin(1, 4000, 1, 100);
    console->pulse_spin = make_spin(1, 500, 1, 20);
    console->simmer_spin = make_spin(0, 1000, 1, 0);
    add_spin_setting(parameter_box, "功率 (%)", console->power_spin, JPT_CMD_POWER, 5, console);
    add_spin_setting(parameter_box, "频率 (kHz)", console->frequency_spin, JPT_CMD_FREQUENCY, 4, console);
    add_spin_setting(parameter_box, "脉宽 (ns)", console->pulse_spin, JPT_CMD_PULSE_WIDTH, 3, console);
    add_spin_setting(parameter_box, "Simmer", console->simmer_spin, JPT_CMD_SIMMER, 0, console);
    gtk_box_append(GTK_BOX(page), parameter_card);

    GtkWidget *output_box;
    GtkWidget *output_card = make_card("控制信号", &output_box);
    GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    console->free_mode = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("自由控制"));
    console->gui_mode = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("GUI 全控"));
    gtk_check_button_set_group(console->gui_mode, console->free_mode);
    gtk_check_button_set_active(console->free_mode, TRUE);
    gtk_box_append(GTK_BOX(mode_row), GTK_WIDGET(console->free_mode));
    gtk_box_append(GTK_BOX(mode_row), GTK_WIDGET(console->gui_mode));
    g_signal_connect(console->free_mode, "toggled", G_CALLBACK(mode_toggled), console);
    g_signal_connect(console->gui_mode, "toggled", G_CALLBACK(mode_toggled), console);
    gtk_box_append(GTK_BOX(output_box), mode_row);
    GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    console->laser_button = GTK_BUTTON(gtk_button_new_with_label("开启激光"));
    console->red_button = GTK_BUTTON(gtk_button_new_with_label("开启红光"));
    console->mo_button = GTK_BUTTON(gtk_button_new_with_label("开启 MO"));
    gtk_widget_add_css_class(GTK_WIDGET(console->laser_button), "primary-button");
    gtk_widget_add_css_class(GTK_WIDGET(console->red_button), "secondary-button");
    gtk_widget_add_css_class(GTK_WIDGET(console->mo_button), "secondary-button");
    g_signal_connect(console->laser_button, "clicked", G_CALLBACK(output_button_clicked), console);
    g_signal_connect(console->red_button, "clicked", G_CALLBACK(output_button_clicked), console);
    g_signal_connect(console->mo_button, "clicked", G_CALLBACK(output_button_clicked), console);
    gtk_box_append(GTK_BOX(button_row), GTK_WIDGET(console->laser_button));
    gtk_box_append(GTK_BOX(button_row), GTK_WIDGET(console->red_button));
    gtk_box_append(GTK_BOX(button_row), GTK_WIDGET(console->mo_button));
    gtk_box_append(GTK_BOX(output_box), button_row);
    gtk_box_append(GTK_BOX(page), output_card);

    GtkWidget *control_box;
    GtkWidget *control_card = make_card("控制来源", &control_box);
    GtkWidget *control_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(control_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(control_grid), 18);
    const gchar *control_names[] = {"GUI MO", "GUI PA", "GUI 脉宽", "GUI 频率", "GUI 功率"};
    for (guint i = 0; i < G_N_ELEMENTS(control_names); i++) {
        console->control_options[i] = add_control_option(control_grid, control_names[i], i, 0, i, console);
    }
    gtk_box_append(GTK_BOX(control_box), control_grid);
    gtk_box_append(GTK_BOX(page), control_card);

    GtkWidget *options_box;
    GtkWidget *options_card = make_card("控制与锁定选项", &options_box);
    GtkWidget *options_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(options_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(options_grid), 18);
    const gchar *parameter_names[] = {
        "Pin9 功率锁存", "Pin23 急停", "外部频率", "风扇调速", "红光模式", "增强模式"
    };
    const guint parameter_bits[] = {0, 1, 3, 4, 5, 6};
    for (guint i = 0; i < G_N_ELEMENTS(parameter_names); i++) {
        console->parameter_options[i] = add_mask_option(options_grid, parameter_names[i],
                                                         i % 3, i / 3, JPT_CMD_PARAMETER_SELECT,
                                                         parameter_bits[i], 0, console);
    }
    const gchar *lock_names[] = {"锁定频率", "锁定功率"};
    for (guint i = 0; i < G_N_ELEMENTS(lock_names); i++) {
        console->lock_options[i] = add_mask_option(options_grid, lock_names[i], i, 2,
                                                   JPT_CMD_LOCK_PARAMETER, i, 1, console);
    }
    gtk_box_append(GTK_BOX(options_box), options_grid);
    gtk_box_append(GTK_BOX(page), options_card);

    GtkWidget *default_box;
    GtkWidget *default_card = make_card("默认参数", &default_box);
    console->default_frequency_spin = make_spin(1, 4000, 1, 100);
    console->default_pulse_spin = make_spin(1, 500, 1, 20);
    console->default_simmer_spin = make_spin(0, 1000, 1, 0);
    console->lock_power_spin = make_spin(0, 100, 1, 0);
    console->red_power_spin = make_spin(0, 100, 1, 0);
    add_spin_setting(default_box, "锁定频率 (kHz)", console->default_frequency_spin, JPT_CMD_LOCK_FREQUENCY, 0, console);
    add_spin_setting(default_box, "默认脉宽 (ns)", console->default_pulse_spin, JPT_CMD_DEFAULT_PULSE_WIDTH, 0, console);
    add_spin_setting(default_box, "默认 Simmer", console->default_simmer_spin, JPT_CMD_DEFAULT_SIMMER, 0, console);
    add_spin_setting(default_box, "锁定功率 (%)", console->lock_power_spin, JPT_CMD_LOCK_POWER, 0, console);
    add_spin_setting(default_box, "红光功率 (%)", console->red_power_spin, JPT_CMD_RED_LIGHT_POWER, 0, console);
    gtk_box_append(GTK_BOX(page), default_card);

    update_output_buttons(console);
    return scroll;
}

static GtkWidget *build_monitor_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_widget_set_margin_top(page, 20);
    gtk_widget_set_margin_bottom(page, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);

    GtkWidget *metrics_box;
    GtkWidget *metrics_card = make_card("实时监控", &metrics_box);
    GtkWidget *metrics_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(metrics_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(metrics_grid), 20);
    GtkWidget *metrics[] = {
        make_metric("实时功率", &console->real_power_label),
        make_metric("实时频率", &console->real_frequency_label),
        make_metric("实时脉宽", &console->real_pulse_label),
        make_metric("传感器 1", &console->sensor_1_label),
        make_metric("传感器 2", &console->sensor_2_label),
        make_metric("种子源 TEC", &console->tec_label),
        make_metric("输出电流", &console->current_label),
        make_metric("SN", &console->serial_label),
    };
    for (guint i = 0; i < G_N_ELEMENTS(metrics); i++) {
        gtk_grid_attach(GTK_GRID(metrics_grid), metrics[i], i % 4, i / 4, 1, 1);
    }
    gtk_box_append(GTK_BOX(metrics_box), metrics_grid);
    console->power_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(console->power_bar, TRUE);
    gtk_progress_bar_set_text(console->power_bar, "实时功率");
    gtk_box_append(GTK_BOX(metrics_box), GTK_WIDGET(console->power_bar));
    gtk_box_append(GTK_BOX(page), metrics_card);

    GtkWidget *signal_box;
    GtkWidget *signal_card = make_card("信号状态", &signal_box);
    GtkWidget *signal_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(signal_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(signal_grid), 24);
    const gchar *signal_names[] = {"PA", "MO", "红光", "泵浦", "DB25 功率"};
    for (guint i = 0; i < G_N_ELEMENTS(signal_names); i++) {
        GtkWidget *label = gtk_label_new(signal_names[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        console->status_indicators[i] = GTK_LABEL(gtk_label_new("关闭"));
        gtk_widget_add_css_class(GTK_WIDGET(console->status_indicators[i]), "indicator-muted");
        gtk_grid_attach(GTK_GRID(signal_grid), label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(signal_grid), GTK_WIDGET(console->status_indicators[i]), 1, i, 1, 1);
    }
    GtkWidget *protocol_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(protocol_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(protocol_grid), 24);
    const gchar *protocol_names[] = {"控制模式", "状态字", "报警字"};
    GtkLabel **protocol_values[] = {&console->control_mode_label, &console->status_label, &console->alarm_state_label};
    for (guint i = 0; i < G_N_ELEMENTS(protocol_names); i++) {
        GtkWidget *label = gtk_label_new(protocol_names[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        *protocol_values[i] = GTK_LABEL(gtk_label_new("--"));
        gtk_grid_attach(GTK_GRID(protocol_grid), label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(protocol_grid), GTK_WIDGET(*protocol_values[i]), 1, i, 1, 1);
    }
    GtkWidget *signal_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
    gtk_box_append(GTK_BOX(signal_row), signal_grid);
    gtk_box_append(GTK_BOX(signal_row), protocol_grid);
    gtk_box_append(GTK_BOX(signal_box), signal_row);
    GtkWidget *power_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(power_grid), 16);
    for (guint i = 0; i < G_N_ELEMENTS(console->power_indicators); i++) {
        console->power_indicators[i] = GTK_LABEL(gtk_label_new("D"));
        gtk_widget_add_css_class(GTK_WIDGET(console->power_indicators[i]), "indicator-muted");
        gtk_grid_attach(GTK_GRID(power_grid), GTK_WIDGET(console->power_indicators[i]), i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(signal_box), power_grid);
    gtk_box_append(GTK_BOX(page), signal_card);

    GtkWidget *alarm_box;
    GtkWidget *alarm_card = make_card("报警状态与计数", &alarm_box);
    GtkWidget *alarm_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(alarm_grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(alarm_grid), 20);
    gtk_grid_attach(GTK_GRID(alarm_grid), gtk_label_new("项目"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(alarm_grid), gtk_label_new("状态"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(alarm_grid), gtk_label_new("累计"), 2, 0, 1, 1);
    for (guint i = 0; i < G_N_ELEMENTS(alarm_names); i++) {
        gtk_grid_attach(GTK_GRID(alarm_grid), gtk_label_new(alarm_names[i]), 0, i + 1, 1, 1);
        console->alarm_indicators[i] = GTK_LABEL(gtk_label_new("关闭"));
        console->alarm_counts[i] = GTK_LABEL(gtk_label_new("0"));
        gtk_widget_add_css_class(GTK_WIDGET(console->alarm_indicators[i]), "indicator-muted");
        gtk_grid_attach(GTK_GRID(alarm_grid), GTK_WIDGET(console->alarm_indicators[i]), 1, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(alarm_grid), GTK_WIDGET(console->alarm_counts[i]), 2, i + 1, 1, 1);
    }
    gtk_box_append(GTK_BOX(alarm_box), alarm_grid);
    gtk_box_append(GTK_BOX(page), alarm_card);
    return scroll;
}

static GtkWidget *build_log_page(LaserConsole *console)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *clear = gtk_button_new_with_label("清空日志");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_log_clicked), console->log_buffer);
    gtk_box_append(GTK_BOX(toolbar), clear);
    gtk_box_append(GTK_BOX(box), toolbar);
    GtkWidget *view = gtk_text_view_new_with_buffer(console->log_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);
    return box;
}

static gboolean window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    LaserConsole *console = user_data;
    if (console->poll_source != 0) {
        g_source_remove(console->poll_source);
        console->poll_source = 0;
    }
    console->closing = TRUE;
    jpt_client_free(console->client);
    g_ptr_array_unref(console->write_buttons);
    g_free(console);
    return FALSE;
}

static void apply_css(void)
{
    AdwStyleManager *style_manager = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_manager, ADW_COLOR_SCHEME_DEFAULT);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".page-title { font-size: 18px; font-weight: 700; }"
        ".card { background: var(--card-bg-color); border: 1px solid var(--border-color); border-radius: 10px; }"
        ".card-title { font-size: 15px; font-weight: 700; }"
        ".metric { background: var(--view-bg-color); border: 1px solid var(--border-color); border-radius: 8px; padding: 10px; min-width: 132px; }"
        ".metric-title { font-size: 12px; }"
        ".metric-value { color: var(--window-fg-color); font-size: 16px; font-weight: 700; }"
        ".value-row { min-height: 34px; }"
        ".compact-button { min-width: 64px; }"
        ".primary-button { background: var(--accent-bg-color); color: var(--accent-fg-color); }"
        ".secondary-button { background: var(--view-bg-color); color: var(--window-fg-color); }"
        ".danger-active { background: var(--error-bg-color); color: var(--error-fg-color); }"
        ".warning-text { color: var(--warning-fg-color); background: var(--warning-bg-color); padding: 10px; border-radius: 7px; }"
        ".state-ok, .indicator-ok { color: var(--success-color); font-weight: 700; }"
        ".state-error, .indicator-alarm { color: var(--error-color); font-weight: 700; }"
        ".state-muted, .indicator-muted { opacity: var(--dim-opacity); }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget *laser_console_create(AdwApplication *application)
{
    apply_css();
    LaserConsole *console = g_new0(LaserConsole, 1);
    console->client = jpt_client_new(trace_callback, console);
    console->write_buttons = g_ptr_array_new();

    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(application));
    console->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "MS LEP | JPT Laser");
    gtk_window_set_default_size(GTK_WINDOW(window), 1240, 840);
    gtk_widget_set_size_request(window, 980, 620);
    g_signal_connect(window, "close-request", G_CALLBACK(window_close_request), console);

    GtkWidget *header = adw_header_bar_new();
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new("MS LEP");
    gtk_widget_add_css_class(title, "page-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *subtitle = gtk_label_new("JPT YDFLP-E3-100-M7 调试控制台");
    gtk_widget_add_css_class(subtitle, "metric-title");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_box);

    GtkWidget *connection_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkStringList *transport_model = gtk_string_list_new((const gchar *[]){"串口", "TCP", NULL});
    console->transport_dropdown = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(transport_model), NULL));
    g_object_unref(transport_model);
    gtk_drop_down_set_selected(console->transport_dropdown, 0);
    gtk_widget_set_size_request(GTK_WIDGET(console->transport_dropdown), 90, -1);
    console->endpoint_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(console->endpoint_entry), "/dev/ttyUSB0");
    gtk_entry_set_placeholder_text(console->endpoint_entry, "/dev/ttyUSB0 或 192.168.0.10:5000");
    gtk_widget_set_size_request(GTK_WIDGET(console->endpoint_entry), 240, -1);
    console->connect_button = GTK_BUTTON(gtk_button_new_with_label("连接"));
    g_signal_connect(console->connect_button, "clicked", G_CALLBACK(connect_clicked), console);
    gtk_box_append(GTK_BOX(connection_box), GTK_WIDGET(console->transport_dropdown));
    gtk_box_append(GTK_BOX(connection_box), GTK_WIDGET(console->endpoint_entry));
    gtk_box_append(GTK_BOX(connection_box), GTK_WIDGET(console->connect_button));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), connection_box);
    GtkWidget *emergency = gtk_button_new_with_label("紧急关闭");
    gtk_widget_add_css_class(emergency, "danger-active");
    g_signal_connect(emergency, "clicked", G_CALLBACK(emergency_clicked), console);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), emergency);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(status_bar, "card");
    gtk_widget_set_margin_start(status_bar, 16);
    gtk_widget_set_margin_end(status_bar, 16);
    gtk_widget_set_margin_top(status_bar, 12);
    gtk_widget_set_margin_bottom(status_bar, 4);
    console->connection_label = GTK_LABEL(gtk_label_new("未连接"));
    console->serial_status_label = GTK_LABEL(gtk_label_new("SN: 未读取"));
    console->last_command_label = GTK_LABEL(gtk_label_new("尚无写入命令"));
    console->last_error_label = GTK_LABEL(gtk_label_new(""));
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->connection_label));
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->serial_status_label));
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->last_command_label));
    gtk_widget_set_hexpand(GTK_WIDGET(console->last_error_label), TRUE);
    gtk_label_set_xalign(console->last_error_label, 1.0f);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->last_error_label));
    gtk_box_append(GTK_BOX(root), status_bar);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    GtkWidget *control_page = build_control_page(console);
    GtkWidget *monitor_page = build_monitor_page(console);
    console->log_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *log_page = build_log_page(console);
    gtk_stack_add_titled(GTK_STACK(stack), control_page, "control", "控制台");
    gtk_stack_add_titled(GTK_STACK(stack), monitor_page, "monitor", "监控");
    gtk_stack_add_titled(GTK_STACK(stack), log_page, "log", "HEX 日志");

    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
    gtk_widget_set_size_request(sidebar, 150, -1);
    GtkWidget *content = adw_overlay_split_view_new();
    adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(content), sidebar);
    adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(content), stack);
    adw_overlay_split_view_set_sidebar_width_fraction(ADW_OVERLAY_SPLIT_VIEW(content), 0.18);
    adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(content), 150);
    adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(content), 220);
    adw_overlay_split_view_set_enable_show_gesture(ADW_OVERLAY_SPLIT_VIEW(content), TRUE);
    adw_overlay_split_view_set_enable_hide_gesture(ADW_OVERLAY_SPLIT_VIEW(content), TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(root), content);
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbar_view, header);
    adw_toolbar_view_set_content(toolbar_view, root);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), GTK_WIDGET(toolbar_view));
    set_connection_state(console, "未连接", FALSE, FALSE);
    update_output_buttons(console);
    return window;
}
