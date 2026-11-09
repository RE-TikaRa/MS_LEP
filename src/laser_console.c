#include "laser_console.h"

#include "jpt_client.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

typedef struct _LaserConsole LaserConsole;

typedef struct {
    gchar *display_name;
    gchar *path;
} SerialPort;

typedef struct {
    const gchar *label;
    guint16 command;
    gboolean readable;
} ProtocolCommand;

typedef struct {
    LaserConsole *console;
    guint16 port;
} NetworkAddressWrite;

static gboolean poll_tick(gpointer user_data);
static AdwPreferencesGroup *make_group(const gchar *title);
static void send_u16(LaserConsole *console, guint16 command, guint16 value);
static void navigation_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data);
static void sidebar_visibility_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
static void show_sidebar_toggled(GtkToggleButton *button, gpointer user_data);

struct _LaserConsole {
    GtkWidget *window;
    GtkStack *main_stack;
    AdwViewStack *view_stack;
    AdwOverlaySplitView *workbench_split_view;
    GtkListBox *navigation_list;
    GtkToggleButton *show_sidebar_button;
    GtkWidget *connection_page;
    GtkBox *connection_split_box;
    GtkPicture *device_picture;
    GtkImage *connection_icon;
    GtkLabel *connection_title;
    GtkLabel *connection_description;
    GtkWidget *control_page;
    GtkWidget *settings_page;
    GtkWidget *protocol_page;
    GtkWidget *alarm_history_page;
    GtkWidget *extension_page;
    GtkWidget *connection_indicator;
    GtkWidget *device_indicator;
    GtkWidget *command_indicator;
    GtkLabel *control_source_label;
    GtkDropDown *transport_dropdown;
    GtkDropDown *serial_port_dropdown;
    GtkStringList *serial_port_model;
    GtkEntry *manual_serial_entry;
    GtkEntry *endpoint_entry;
    GtkStack *endpoint_stack;
    GtkButton *connect_button;
    GtkButton *disconnect_button;
    GtkButton *refresh_serial_button;
    GtkButton *select_serial_button;
    GtkWidget *serial_controls;
    GtkWidget *endpoint_row;
    AdwExpanderRow *serial_settings_row;
    AdwComboRow *baud_dropdown;
    AdwComboRow *data_bits_dropdown;
    AdwComboRow *parity_dropdown;
    AdwComboRow *stop_bits_dropdown;
    GtkSwitch *hardware_flow_switch;
    GtkButton *emergency_button;
    GtkButton *apply_parameters_button;
    GtkButton *laser_button;
    GtkButton *red_button;
    GtkButton *mo_button;
    GtkCheckButton *safety_ack;
    GtkCheckButton *protocol_ack;
    GtkCheckButton *free_mode;
    GtkCheckButton *gui_mode;
    GtkCheckButton *control_options[5];
    GtkCheckButton *parameter_options[6];
    GtkCheckButton *lock_options[2];
    GtkLabel *connection_label;
    GtkLabel *device_status_label;
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
    GtkLabel *status_indicators[4];
    GtkLabel *power_indicators[8];
    GtkLabel *external_power_label;
    GtkLabel *alarm_indicators[8];
    GtkLabel *alarm_counts[8];
    GtkTextBuffer *log_buffer;
    GtkTextBuffer *alarm_history_buffer;
    GtkTextBuffer *alarm_cache_buffer;
    GtkLabel *extension_status_label;
    GtkButton *m7_cutoff_read_button;
    GtkButton *m7_cutoff_write_button;
    GtkButton *alarm_cache_refresh_button;
    GtkEntry *raw_command_entry;
    GtkEntry *raw_payload_entry;
    GtkLabel *raw_result_label;
    GtkButton *raw_read_button;
    GtkButton *raw_send_button;
    GtkDropDown *raw_command_dropdown;
    GtkEntry *device_ip_entry;
    GtkEntry *device_port_entry;
    GtkButton *apply_network_address_button;
    AdwPreferencesGroup *alarm_actions_group;
    AdwSpinRow *power_spin;
    AdwSpinRow *frequency_spin;
    AdwSpinRow *pulse_spin;
    AdwSpinRow *simmer_spin;
    AdwSpinRow *default_frequency_spin;
    AdwSpinRow *default_pulse_spin;
    AdwSpinRow *default_simmer_spin;
    AdwSpinRow *lock_power_spin;
    AdwSpinRow *red_power_spin;
    AdwSpinRow *m7_cutoff_spins[11];
    GtkProgressBar *power_bar;
    GPtrArray *write_buttons;
    GPtrArray *serial_ports;
    JptClient *client;
    JptTelemetry telemetry;
    guint poll_source;
    gboolean connected;
    gboolean connection_pending;
    gboolean poll_pending;
    gboolean updating_controls;
    gboolean initialized;
    gboolean closing;
    gboolean laser_on;
    gboolean red_on;
    gboolean mo_on;
    gboolean alarm_history_initialized;
    guint16 last_alarm_state;
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

static const ProtocolCommand protocol_commands[] = {
    {"读 · Simmer", JPT_CMD_READ_DEFAULT_SIMMER, TRUE},
    {"读 · 默认脉宽", JPT_CMD_READ_DEFAULT_PULSE_WIDTH, TRUE},
    {"读 · 锁定频率", JPT_CMD_READ_LOCK_FREQUENCY, TRUE},
    {"读 · 参数选择", JPT_CMD_READ_PARAMETER_SELECT, TRUE},
    {"读 · 控制来源", JPT_CMD_READ_CONTROL_MODE, TRUE},
    {"读 · 设备状态", JPT_CMD_STATUS, TRUE},
    {"读 · 实时功率", JPT_CMD_REAL_POWER, TRUE},
    {"读 · 实时脉宽", JPT_CMD_REAL_PULSE_WIDTH, TRUE},
    {"读 · 实时频率", JPT_CMD_REAL_FREQUENCY, TRUE},
    {"读 · 监控信息", JPT_CMD_MONITORING, TRUE},
    {"读 · SN", JPT_CMD_SERIAL_NUMBER, TRUE},
    {"读 · 报警状态", JPT_CMD_ALARM_STATE, TRUE},
    {"读 · 报警次数", JPT_CMD_ALARM_COUNTS, TRUE},
    {"读 · 锁定功率", JPT_CMD_READ_LOCK_POWER, TRUE},
    {"读 · 红光功率", JPT_CMD_READ_RED_LIGHT_POWER, TRUE},
    {"读 · 锁定选项", JPT_CMD_READ_LOCK_PARAMETER, TRUE},
    {"写 · 功率", JPT_CMD_POWER, FALSE},
    {"写 · 频率", JPT_CMD_FREQUENCY, FALSE},
    {"写 · 脉宽", JPT_CMD_PULSE_WIDTH, FALSE},
    {"写 · Simmer", JPT_CMD_SIMMER, FALSE},
    {"写 · 默认脉宽", JPT_CMD_DEFAULT_PULSE_WIDTH, FALSE},
    {"写 · 锁定频率", JPT_CMD_LOCK_FREQUENCY, FALSE},
    {"写 · 参数选择", JPT_CMD_PARAMETER_SELECT, FALSE},
    {"写 · 控制来源", JPT_CMD_CONTROL_MODE, FALSE},
    {"写 · GUI PA", JPT_CMD_GUI_PA, FALSE},
    {"写 · GUI MO", JPT_CMD_GUI_MO, FALSE},
    {"写 · 红光", JPT_CMD_RED_LIGHT, FALSE},
    {"写 · 截止频率", JPT_CMD_CUTOFF_FREQUENCY, FALSE},
    {"写 · 报警屏蔽", JPT_CMD_ALARM_SHIELD, FALSE},
    {"写 · 报警复位", JPT_CMD_ALARM_RESET, FALSE},
    {"写 · 清除报警次数", JPT_CMD_ALARM_CLEAR_COUNT, FALSE},
    {"写 · 清除出光时间", JPT_CMD_LASER_TIME_CLEAR, FALSE},
    {"写 · M7 截止频率", JPT_CMD_CUTOFF_FREQUENCY_M7, FALSE},
    {"写 · ControlSel", JPT_CMD_CONTROL_SELECT, FALSE},
    {"写 · 锁定功率", JPT_CMD_LOCK_POWER, FALSE},
    {"写 · 红光功率", JPT_CMD_RED_LIGHT_POWER, FALSE},
    {"写 · 锁定选项", JPT_CMD_LOCK_PARAMETER, FALSE},
    {"写 · 优先模式", JPT_CMD_PREFERRED_MODE, FALSE},
    {"写 · 机型", JPT_CMD_WRITE_MACHINE_TYPE, FALSE},
    {"写 · 调 Q 参数", JPT_CMD_WRITE_MODULA_Q, FALSE},
    {"写 · 调 Q 1000W 参数", JPT_CMD_WRITE_MODULA_Q_1000W, FALSE},
    {"写 · 调 Q 1000W V2 参数", JPT_CMD_WRITE_MODULA_Q_1000W_V2, FALSE},
    {"读 · 报警屏蔽", JPT_CMD_READ_ALARM_SHIELD, TRUE},
    {"读 · 截止频率", JPT_CMD_READ_CUTOFF_FREQUENCY, TRUE},
    {"读 · M7 截止频率", JPT_CMD_READ_CUTOFF_FREQUENCY_M7, TRUE},
    {"读 · 出光时间", JPT_CMD_LASER_TIME, TRUE},
    {"读 · 优先模式", JPT_CMD_READ_PREFERRED_MODE, TRUE},
    {"读 · 报警缓存", JPT_CMD_READ_ALARM_CACHE, TRUE},
    {"读 · 系统参数状态", JPT_CMD_READ_SYSTEM_PARAMETER_STATE, TRUE},
    {"读 · 报警缓存开关", JPT_CMD_READ_ALARM_CACHE_ENABLE, TRUE},
    {"读 · 机型", JPT_CMD_READ_MACHINE_TYPE, TRUE},
    {"读 · 扩展报警次数", JPT_CMD_READ_EXTENDED_ALARM_COUNT, TRUE},
    {"读 · E100 型号能力", JPT_CMD_READ_E100_TYPE, TRUE},
    {"读 · 调 Q 参数", JPT_CMD_READ_MODULA_Q, TRUE},
    {"读 · 调 Q 1000W 参数", JPT_CMD_READ_MODULA_Q_1000W, TRUE},
    {"读 · 调 Q 1000W V2 参数", JPT_CMD_READ_MODULA_Q_1000W_V2, TRUE},
    {"写 · TCP IP 地址", JPT_CMD_IP_ADDRESS, FALSE},
    {"写 · TCP 端口", JPT_CMD_TCP_PORT, FALSE},
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

static void append_alarm_history(LaserConsole *console, guint16 alarm_state)
{
    if (console->alarm_history_buffer == NULL) {
        return;
    }
    GDateTime *now = g_date_time_new_now_local();
    gchar *timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(console->alarm_history_buffer, &end);
    gchar *message = g_strdup_printf("%s  报警字变更为 0x%04X\n", timestamp, alarm_state);
    gtk_text_buffer_insert(console->alarm_history_buffer, &end, message, -1);
    g_free(message);
    g_free(timestamp);
    g_date_time_unref(now);
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

static gboolean parse_hex_bytes(const gchar *text, GBytes **bytes, GError **error)
{
    GByteArray *array = g_byte_array_new();
    gchar **parts = g_strsplit_set(text, " ,\t\r\n", -1);
    for (guint i = 0; parts[i] != NULL; i++) {
        const gchar *part = parts[i];
        if (*part == '\0') {
            continue;
        }
        if (g_str_has_prefix(part, "0x") || g_str_has_prefix(part, "0X")) {
            part += 2;
        }
        if (part[0] == '\0' || part[1] == '\0' || part[2] != '\0') {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "HEX 数据应由两位字节组成");
            g_strfreev(parts);
            g_byte_array_unref(array);
            return FALSE;
        }
        gint high = g_ascii_xdigit_value(part[0]);
        gint low = g_ascii_xdigit_value(part[1]);
        if (high < 0 || low < 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "HEX 数据包含无效字符");
            g_strfreev(parts);
            g_byte_array_unref(array);
            return FALSE;
        }
        guint8 value = (guint8)((high << 4) | low);
        g_byte_array_append(array, &value, 1);
    }
    g_strfreev(parts);
    *bytes = g_byte_array_free_to_bytes(array);
    return TRUE;
}

static gboolean parse_command_value(const gchar *text, guint16 *command, GError **error)
{
    gchar *end = NULL;
    guint64 value = g_ascii_strtoull(text, &end, 0);
    while (end != NULL && g_ascii_isspace(*end)) {
        end++;
    }
    if (end == text || (end != NULL && *end != '\0') || value > G_MAXUINT16) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "命令地址应为 0x0000 至 0xFFFF");
        return FALSE;
    }
    *command = (guint16)value;
    return TRUE;
}

static void serial_port_free(gpointer data)
{
    SerialPort *port = data;
    g_free(port->display_name);
    g_free(port->path);
    g_free(port);
}

static gint serial_port_compare(gconstpointer a, gconstpointer b)
{
    const SerialPort *left = *(SerialPort *const *)a;
    const SerialPort *right = *(SerialPort *const *)b;
    return g_utf8_collate(left->display_name, right->display_name);
}

static void append_serial_port(LaserConsole *console, GHashTable *seen,
                               const gchar *path, const gchar *display_name)
{
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISCHR(info.st_mode)) {
        return;
    }

    gchar *canonical_path = g_canonicalize_filename(path, NULL);
    if (g_hash_table_contains(seen, canonical_path)) {
        g_free(canonical_path);
        return;
    }
    g_hash_table_add(seen, g_strdup(canonical_path));

    SerialPort *port = g_new0(SerialPort, 1);
    port->display_name = g_strdup(display_name);
    port->path = canonical_path;
    g_ptr_array_add(console->serial_ports, port);
}

static void append_serial_directory(LaserConsole *console, GHashTable *seen,
                                    const gchar *directory)
{
    GError *error = NULL;
    GDir *entries = g_dir_open(directory, 0, &error);
    if (entries == NULL) {
        g_clear_error(&error);
        return;
    }

    const gchar *name;
    while ((name = g_dir_read_name(entries)) != NULL) {
        gchar *link_path = g_build_filename(directory, name, NULL);
        gchar *target = g_file_read_link(link_path, NULL);
        if (target != NULL) {
            gchar *path = g_canonicalize_filename(target, directory);
            gchar *display_name = g_strdup_printf("%s · %s", name, path);
            append_serial_port(console, seen, path, display_name);
            g_free(display_name);
            g_free(path);
            g_free(target);
        } else {
            append_serial_port(console, seen, link_path, name);
        }
        g_free(link_path);
    }
    g_dir_close(entries);
}

static void refresh_serial_ports(LaserConsole *console)
{
    gchar *selected_path = NULL;
    guint selected = gtk_drop_down_get_selected(console->serial_port_dropdown);
    gboolean manual_selected = selected == console->serial_ports->len + 1;
    if (selected > 0 && selected <= console->serial_ports->len) {
        SerialPort *port = g_ptr_array_index(console->serial_ports, selected - 1);
        selected_path = g_strdup(port->path);
    }

    g_ptr_array_set_size(console->serial_ports, 0);
    gtk_string_list_splice(console->serial_port_model, 0,
                           g_list_model_get_n_items(G_LIST_MODEL(console->serial_port_model)), NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    append_serial_directory(console, seen, "/dev/serial/by-id");
    append_serial_directory(console, seen, "/dev/serial/by-path");

    GError *error = NULL;
    GDir *devices = g_dir_open("/dev", 0, &error);
    if (devices != NULL) {
        const gchar *name;
        while ((name = g_dir_read_name(devices)) != NULL) {
            const gchar *type = NULL;
            if (g_str_has_prefix(name, "ttyUSB")) {
                type = "USB 串口";
            } else if (g_str_has_prefix(name, "ttyACM")) {
                type = "USB ACM 串口";
            } else if (g_str_has_prefix(name, "ttyS")) {
                type = "串口";
            } else if (g_str_has_prefix(name, "rfcomm")) {
                type = "蓝牙串口";
            }
            if (type != NULL) {
                gchar *path = g_build_filename("/dev", name, NULL);
                gchar *display_name = g_strdup_printf("%s · %s", type, path);
                append_serial_port(console, seen, path, display_name);
                g_free(display_name);
                g_free(path);
            }
        }
        g_dir_close(devices);
    } else {
        g_clear_error(&error);
    }
    g_hash_table_unref(seen);

    g_ptr_array_sort(console->serial_ports, serial_port_compare);
    gtk_string_list_append(console->serial_port_model,
                           console->serial_ports->len == 0 ? "未发现串口设备" : "选择串口设备…");
    guint selected_index = 0;
    for (guint i = 0; i < console->serial_ports->len; i++) {
        SerialPort *port = g_ptr_array_index(console->serial_ports, i);
        gtk_string_list_append(console->serial_port_model, port->display_name);
        if (selected_path != NULL && g_str_equal(selected_path, port->path)) {
            selected_index = i + 1;
        }
    }
    gtk_string_list_append(console->serial_port_model, "手动输入设备路径…");
    if (selected_path == NULL && manual_selected) {
        selected_index = console->serial_ports->len + 1;
    }
    gtk_drop_down_set_selected(console->serial_port_dropdown, selected_index);
    g_free(selected_path);
}

static void update_transport_controls(LaserConsole *console)
{
    gboolean serial = gtk_drop_down_get_selected(console->transport_dropdown) == 0;
    guint selected = gtk_drop_down_get_selected(console->serial_port_dropdown);
    gboolean manual = selected == console->serial_ports->len + 1;
    gtk_stack_set_visible_child_name(console->endpoint_stack, serial ? "serial" : "tcp");
    gtk_widget_set_visible(GTK_WIDGET(console->serial_port_dropdown), serial && !manual);
    gtk_widget_set_visible(GTK_WIDGET(console->refresh_serial_button), serial && !manual);
    gtk_widget_set_visible(GTK_WIDGET(console->manual_serial_entry), serial && manual);
    gtk_widget_set_visible(GTK_WIDGET(console->select_serial_button), serial && manual);
    gtk_widget_set_visible(GTK_WIDGET(console->serial_settings_row), serial);
}

static void serial_port_selection_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    update_transport_controls(user_data);
}

static void transport_mode_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    update_transport_controls(user_data);
}

static void refresh_serial_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    refresh_serial_ports(console);
}

static void select_serial_list_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    gtk_drop_down_set_selected(console->serial_port_dropdown, 0);
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
    gtk_widget_remove_css_class(GTK_WIDGET(label), "success");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "error");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "dim-label");
    gtk_widget_add_css_class(GTK_WIDGET(label), alarm ? "error" :
                             active ? "success" : "dim-label");
    gtk_label_set_text(label, alarm ? "报警" : active ? "正常" : "关闭");
}

static void set_status_item(GtkWidget *indicator, GtkLabel *label, const gchar *text,
                            const gchar *style_class)
{
    gtk_widget_remove_css_class(indicator, "success");
    gtk_widget_remove_css_class(indicator, "warning");
    gtk_widget_remove_css_class(indicator, "error");
    gtk_widget_remove_css_class(indicator, "dim-label");
    gtk_widget_add_css_class(indicator, style_class);
    gtk_widget_remove_css_class(GTK_WIDGET(label), "success");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "warning");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "error");
    gtk_widget_remove_css_class(GTK_WIDGET(label), "dim-label");
    gtk_widget_add_css_class(GTK_WIDGET(label), style_class);
    gtk_label_set_text(label, text);
}

static GtkWidget *make_status_item(const gchar *title, const gchar *text,
                                       GtkWidget **indicator, GtkLabel **label)
{
    GtkWidget *item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *title_label = gtk_label_new(title);
    gtk_widget_add_css_class(title_label, "dim-label");
    *indicator = gtk_image_new_from_icon_name("media-record-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(*indicator), 10);
    gtk_widget_add_css_class(*indicator, "dim-label");
    *label = GTK_LABEL(gtk_label_new(text));
    gtk_box_append(GTK_BOX(item), *indicator);
    gtk_box_append(GTK_BOX(item), title_label);
    gtk_box_append(GTK_BOX(item), GTK_WIDGET(*label));
    return item;
}

static AdwActionRow *make_widget_row(const gchar *title, GtkWidget *widget)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_add_suffix(row, widget);
    return row;
}

static AdwComboRow *make_combo_row(const gchar *title, const gchar *const *options, guint selected)
{
    AdwComboRow *row = ADW_COMBO_ROW(adw_combo_row_new());
    GtkStringList *model = gtk_string_list_new(options);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_combo_row_set_model(row, G_LIST_MODEL(model));
    adw_combo_row_set_selected(row, selected);
    g_object_unref(model);
    return row;
}

static JptSerialConfig serial_config_from_ui(LaserConsole *console)
{
    const guint baud_rates[] = {9600, 19200, 38400, 57600, 115200};
    JptSerialConfig config = {
        .baud_rate = baud_rates[adw_combo_row_get_selected(console->baud_dropdown)],
        .data_bits = 5 + adw_combo_row_get_selected(console->data_bits_dropdown),
        .stop_bits = 1 + adw_combo_row_get_selected(console->stop_bits_dropdown),
        .parity = (JptSerialParity)adw_combo_row_get_selected(console->parity_dropdown),
        .hardware_flow_control = gtk_switch_get_active(console->hardware_flow_switch),
    };
    return config;
}

static void serial_settings_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    LaserConsole *console = user_data;
    const guint baud_rates[] = {9600, 19200, 38400, 57600, 115200};
    const gchar parity[] = {'N', 'E', 'O'};
    gchar *summary = g_strdup_printf("%u / %u%c%u / %s流控",
                                     baud_rates[adw_combo_row_get_selected(console->baud_dropdown)],
                                     5 + adw_combo_row_get_selected(console->data_bits_dropdown),
                                     parity[adw_combo_row_get_selected(console->parity_dropdown)],
                                     1 + adw_combo_row_get_selected(console->stop_bits_dropdown),
                                     gtk_switch_get_active(console->hardware_flow_switch) ? "硬件" : "无");
    adw_expander_row_set_subtitle(console->serial_settings_row, summary);
    g_free(summary);
}

static void build_connection_page(LaserConsole *console)
{
    GtkWidget *visual_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_vexpand(visual_panel, FALSE);
    gtk_widget_set_valign(visual_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(visual_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(visual_panel, 32);
    gtk_widget_set_margin_end(visual_panel, 32);
    console->device_picture = GTK_PICTURE(gtk_picture_new_for_resource(
        "/com/tikara/mslep/laser-device.png"));
    gtk_picture_set_content_fit(console->device_picture, GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(console->device_picture, TRUE);
    gtk_picture_set_alternative_text(console->device_picture, "YDFLP-E3-100-M7 脉冲光纤激光器");
    gtk_widget_set_size_request(GTK_WIDGET(console->device_picture), 280, 180);
    gtk_widget_set_halign(GTK_WIDGET(console->device_picture), GTK_ALIGN_CENTER);
    console->connection_icon = GTK_IMAGE(gtk_image_new_from_icon_name("network-offline-symbolic"));
    gtk_image_set_pixel_size(console->connection_icon, 24);
    gtk_widget_add_css_class(GTK_WIDGET(console->connection_icon), "dim-label");
    gtk_widget_set_halign(GTK_WIDGET(console->connection_icon), GTK_ALIGN_CENTER);
    console->connection_title = GTK_LABEL(gtk_label_new("YDFLP-E3-100-M7"));
    gtk_widget_add_css_class(GTK_WIDGET(console->connection_title), "title-1");
    gtk_label_set_xalign(console->connection_title, 0.5f);
    gtk_widget_set_halign(GTK_WIDGET(console->connection_title), GTK_ALIGN_CENTER);
    console->connection_description = GTK_LABEL(gtk_label_new("MOPA 脉冲光纤激光器"));
    gtk_widget_add_css_class(GTK_WIDGET(console->connection_description), "dim-label");
    gtk_widget_add_css_class(GTK_WIDGET(console->connection_description), "connection-side-caption");
    gtk_label_set_xalign(console->connection_description, 0.5f);
    gtk_label_set_wrap(console->connection_description, TRUE);
    gtk_label_set_justify(console->connection_description, GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(console->connection_description, 26);
    gtk_widget_set_halign(GTK_WIDGET(console->connection_description), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(GTK_WIDGET(console->connection_icon), FALSE);
    gtk_box_append(GTK_BOX(visual_panel), GTK_WIDGET(console->device_picture));
    gtk_box_append(GTK_BOX(visual_panel), GTK_WIDGET(console->connection_icon));
    gtk_box_append(GTK_BOX(visual_panel), GTK_WIDGET(console->connection_title));
    gtk_box_append(GTK_BOX(visual_panel), GTK_WIDGET(console->connection_description));

    AdwPreferencesGroup *connection_group = make_group("设备连接");
    gtk_widget_set_hexpand(GTK_WIDGET(connection_group), TRUE);
    AdwActionRow *transport_row = make_widget_row("连接方式", GTK_WIDGET(console->transport_dropdown));
    gtk_widget_add_css_class(GTK_WIDGET(transport_row), "connection-form-row");
    adw_preferences_group_add(connection_group, GTK_WIDGET(transport_row));
    console->endpoint_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(console->endpoint_stack, GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(console->endpoint_stack, console->serial_controls, "serial");
    gtk_stack_add_named(console->endpoint_stack, GTK_WIDGET(console->endpoint_entry), "tcp");
    console->endpoint_row = GTK_WIDGET(make_widget_row("设备地址", GTK_WIDGET(console->endpoint_stack)));
    gtk_widget_add_css_class(console->endpoint_row, "connection-form-row");
    adw_preferences_group_add(connection_group, console->endpoint_row);
    gtk_widget_add_css_class(GTK_WIDGET(console->connect_button), "suggested-action");
    adw_preferences_group_set_header_suffix(connection_group, GTK_WIDGET(console->connect_button));
    console->serial_settings_row = ADW_EXPANDER_ROW(adw_expander_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(console->serial_settings_row), "串口参数");
    gtk_widget_add_css_class(GTK_WIDGET(console->serial_settings_row), "connection-form-row");
    adw_expander_row_set_expanded(console->serial_settings_row, FALSE);

    const gchar *baud_options[] = {"9600", "19200", "38400", "57600（手册默认）", "115200", NULL};
    const gchar *data_bits_options[] = {"5", "6", "7", "8", NULL};
    const gchar *parity_options[] = {"无校验", "偶校验", "奇校验", NULL};
    const gchar *stop_bits_options[] = {"1", "2", NULL};
    console->baud_dropdown = ADW_COMBO_ROW(make_combo_row("波特率", baud_options, 3));
    console->data_bits_dropdown = ADW_COMBO_ROW(make_combo_row("数据位", data_bits_options, 3));
    console->parity_dropdown = ADW_COMBO_ROW(make_combo_row("校验", parity_options, 0));
    console->stop_bits_dropdown = ADW_COMBO_ROW(make_combo_row("停止位", stop_bits_options, 0));
    console->hardware_flow_switch = GTK_SWITCH(gtk_switch_new());
    AdwActionRow *flow_row = make_widget_row("硬件流控", GTK_WIDGET(console->hardware_flow_switch));
    adw_expander_row_add_row(console->serial_settings_row, GTK_WIDGET(console->baud_dropdown));
    adw_expander_row_add_row(console->serial_settings_row, GTK_WIDGET(console->data_bits_dropdown));
    adw_expander_row_add_row(console->serial_settings_row, GTK_WIDGET(console->parity_dropdown));
    adw_expander_row_add_row(console->serial_settings_row, GTK_WIDGET(console->stop_bits_dropdown));
    adw_expander_row_add_row(console->serial_settings_row, GTK_WIDGET(flow_row));
    adw_preferences_group_add(connection_group, GTK_WIDGET(console->serial_settings_row));

    g_signal_connect(console->baud_dropdown, "notify::selected",
                     G_CALLBACK(serial_settings_changed), console);
    g_signal_connect(console->data_bits_dropdown, "notify::selected",
                     G_CALLBACK(serial_settings_changed), console);
    g_signal_connect(console->parity_dropdown, "notify::selected",
                     G_CALLBACK(serial_settings_changed), console);
    g_signal_connect(console->stop_bits_dropdown, "notify::selected",
                     G_CALLBACK(serial_settings_changed), console);
    g_signal_connect(console->hardware_flow_switch, "notify::active",
                     G_CALLBACK(serial_settings_changed), console);
    serial_settings_changed(NULL, NULL, console);

    refresh_serial_ports(console);
    update_transport_controls(console);

    GtkWidget *form_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(form_panel, "connection-form");
    gtk_widget_set_hexpand(form_panel, TRUE);
    gtk_widget_set_vexpand(form_panel, FALSE);
    gtk_widget_set_valign(form_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(form_panel, 28);
    gtk_widget_set_margin_end(form_panel, 28);
    gtk_widget_set_margin_top(form_panel, 24);
    gtk_widget_set_margin_bottom(form_panel, 24);
    gtk_box_append(GTK_BOX(form_panel), GTK_WIDGET(connection_group));

    gtk_widget_add_css_class(visual_panel, "card");
    gtk_widget_set_size_request(visual_panel, 300, -1);
    gtk_widget_set_size_request(form_panel, 560, -1);
    console->connection_split_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32));
    gtk_widget_set_hexpand(GTK_WIDGET(console->connection_split_box), TRUE);
    gtk_box_append(console->connection_split_box, visual_panel);
    gtk_box_append(console->connection_split_box, form_panel);

    AdwClamp *clamp = ADW_CLAMP(adw_clamp_new());
    adw_clamp_set_maximum_size(clamp, 1120);
    adw_clamp_set_child(clamp, GTK_WIDGET(console->connection_split_box));
    gtk_widget_set_hexpand(GTK_WIDGET(clamp), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(clamp), FALSE);
    gtk_widget_set_halign(GTK_WIDGET(clamp), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(clamp), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(GTK_WIDGET(clamp), 24);
    gtk_widget_set_margin_end(GTK_WIDGET(clamp), 24);
    gtk_widget_set_margin_top(GTK_WIDGET(clamp), 24);
    gtk_widget_set_margin_bottom(GTK_WIDGET(clamp), 24);
    GtkCenterBox *center_box = GTK_CENTER_BOX(gtk_center_box_new());
    gtk_widget_set_hexpand(GTK_WIDGET(center_box), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(center_box), TRUE);
    gtk_center_box_set_center_widget(center_box, GTK_WIDGET(clamp));
    gtk_box_append(GTK_BOX(console->connection_page), GTK_WIDGET(center_box));
}

static void update_device_status(LaserConsole *console)
{
    if (!console->connected) {
        set_status_item(console->device_indicator, console->device_status_label,
                        "报警状态未知", "dim-label");
    } else if (!console->initialized) {
        set_status_item(console->device_indicator, console->device_status_label,
                        "正在读取设备", "warning");
    } else if (console->telemetry.alarm_state != 0) {
        set_status_item(console->device_indicator, console->device_status_label,
                        "存在设备报警", "error");
    } else {
        set_status_item(console->device_indicator, console->device_status_label,
                        "无设备报警", "success");
    }
}

static void set_connection_state(LaserConsole *console, const gchar *text, gboolean connected, gboolean error)
{
    const gchar *style_class = error ? "error" : connected ? "success" :
        g_str_has_prefix(text, "正在") ? "warning" : "dim-label";
    set_status_item(console->connection_indicator, console->connection_label, text, style_class);
}

static void navigation_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    (void)list;
    LaserConsole *console = user_data;
    const gchar *page_name = g_object_get_data(G_OBJECT(row), "page-name");
    adw_view_stack_set_visible_child_name(console->view_stack, page_name);
    if (adw_overlay_split_view_get_collapsed(console->workbench_split_view)) {
        adw_overlay_split_view_set_show_sidebar(console->workbench_split_view, FALSE);
    }
}

static void sidebar_visibility_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    LaserConsole *console = user_data;
    gtk_toggle_button_set_active(
        console->show_sidebar_button,
        adw_overlay_split_view_get_show_sidebar(console->workbench_split_view));
}

static void show_sidebar_toggled(GtkToggleButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    adw_overlay_split_view_set_show_sidebar(
        console->workbench_split_view, gtk_toggle_button_get_active(button));
}

static GtkWidget *make_navigation_row(const gchar *page_name, const gchar *title, const gchar *icon_name)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    GtkImage *icon = GTK_IMAGE(gtk_image_new_from_icon_name(icon_name));
    gtk_image_set_pixel_size(icon, 20);
    adw_action_row_add_prefix(row, GTK_WIDGET(icon));
    g_object_set_data_full(G_OBJECT(row), "page-name", g_strdup(page_name), g_free);
    return GTK_WIDGET(row);
}

static void show_connection_page(LaserConsole *console, const gchar *title,
                                 const gchar *description, const gchar *icon_name)
{
    gtk_label_set_text(console->connection_title, title);
    gtk_label_set_text(console->connection_description, description);
    gtk_image_set_from_icon_name(console->connection_icon, icon_name);
    gtk_widget_set_visible(GTK_WIDGET(console->connection_icon), TRUE);
    gtk_stack_set_visible_child_name(console->main_stack, "connection");
}

static void update_output_buttons(LaserConsole *console)
{
    gboolean ready = console->connected && console->initialized;
    gboolean enabled = ready && gtk_check_button_get_active(console->safety_ack);
    gboolean gui_mo = (console->telemetry.control_mode & (1u << 0)) != 0;
    gboolean gui_pa = (console->telemetry.control_mode & (1u << 1)) != 0;
    gtk_widget_set_sensitive(console->control_page, ready);
    if (console->settings_page != NULL) {
        gtk_widget_set_sensitive(console->settings_page, ready);
    }
    if (console->protocol_page != NULL) {
        gtk_widget_set_sensitive(console->protocol_page, ready);
    }
    if (console->extension_page != NULL) {
        gtk_widget_set_sensitive(console->extension_page, ready);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(console->laser_button), enabled && gui_mo && gui_pa);
    gtk_widget_set_sensitive(GTK_WIDGET(console->red_button), enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(console->mo_button), enabled && gui_mo);
    gtk_widget_set_sensitive(GTK_WIDGET(console->emergency_button), ready);
    gtk_widget_set_visible(GTK_WIDGET(console->emergency_button), ready);
    gtk_widget_set_sensitive(GTK_WIDGET(console->apply_parameters_button), ready);
    if (console->raw_read_button != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->raw_read_button), ready);
        gtk_widget_set_sensitive(GTK_WIDGET(console->raw_send_button),
                                 ready && console->protocol_ack != NULL &&
                                 gtk_check_button_get_active(console->protocol_ack));
    }
    if (console->apply_network_address_button != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->apply_network_address_button),
                                 ready && console->protocol_ack != NULL &&
                                 gtk_check_button_get_active(console->protocol_ack));
    }
    if (console->alarm_actions_group != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->alarm_actions_group), ready);
    }
    if (console->m7_cutoff_read_button != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->m7_cutoff_read_button), ready);
        gtk_widget_set_sensitive(GTK_WIDGET(console->m7_cutoff_write_button),
                                 ready && console->protocol_ack != NULL &&
                                 gtk_check_button_get_active(console->protocol_ack));
        gtk_widget_set_sensitive(GTK_WIDGET(console->alarm_cache_refresh_button), ready);
    }
    if (console->free_mode != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(console->free_mode), ready);
        gtk_widget_set_sensitive(GTK_WIDGET(console->gui_mode), ready);
        for (guint i = 0; i < G_N_ELEMENTS(console->control_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->control_options[i]), ready);
        }
        for (guint i = 0; i < G_N_ELEMENTS(console->parameter_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->parameter_options[i]), ready);
        }
        for (guint i = 0; i < G_N_ELEMENTS(console->lock_options); i++) {
            gtk_widget_set_sensitive(GTK_WIDGET(console->lock_options[i]), ready);
        }
    }
    if (console->write_buttons != NULL) {
        for (guint i = 0; i < console->write_buttons->len; i++) {
            GtkWidget *button = g_ptr_array_index(console->write_buttons, i);
            guint bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "control-bit"));
            gboolean setting_enabled = ready &&
                (bit == G_MAXUINT || (console->telemetry.control_mode & (1u << bit)) != 0);
            gtk_widget_set_sensitive(button, setting_enabled);
            gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(button), "spin"), setting_enabled);
        }
    }
    gtk_widget_set_sensitive(GTK_WIDGET(console->connect_button), !console->connection_pending);
    gtk_widget_set_sensitive(GTK_WIDGET(console->disconnect_button), ready);
    gtk_widget_set_visible(GTK_WIDGET(console->disconnect_button), ready);
    gboolean selecting_endpoint = !console->connected && !console->connection_pending;
    gtk_widget_set_sensitive(GTK_WIDGET(console->transport_dropdown), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->serial_port_dropdown), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->refresh_serial_button), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->select_serial_button), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->manual_serial_entry), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->endpoint_entry), selecting_endpoint);
    gtk_widget_set_sensitive(GTK_WIDGET(console->serial_settings_row), selecting_endpoint);
    gtk_button_set_label(console->laser_button, console->laser_on ? "关闭激光" : "开启激光");
    gtk_button_set_label(console->red_button, console->red_on ? "关闭红光" : "开启红光");
    gtk_button_set_label(console->mo_button, console->mo_on ? "关闭 MO" : "开启 MO");
    if (console->laser_on) {
        gtk_widget_add_css_class(GTK_WIDGET(console->laser_button), "destructive-action");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(console->laser_button), "destructive-action");
    }
}

static void update_telemetry(LaserConsole *console, const JptTelemetry *telemetry)
{
    if (console->alarm_history_initialized && console->last_alarm_state != telemetry->alarm_state) {
        append_alarm_history(console, telemetry->alarm_state);
    }
    console->last_alarm_state = telemetry->alarm_state;
    console->alarm_history_initialized = TRUE;
    console->telemetry.status = telemetry->status;
    jpt_device_decode_status(telemetry->status, &console->telemetry);
    console->telemetry.control_mode = telemetry->control_mode;
    if (console->control_source_label != NULL) {
        const gchar *mode = telemetry->control_mode == 0x1F ? "MS_LEP 内控" :
            telemetry->control_mode == 0 ? "JCZ/DB25 外控" : "混合控制";
        gtk_label_set_text(console->control_source_label, mode);
    }
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
        gtk_label_set_text(console->serial_status_label, telemetry->serial_number);
    }
    const JptTelemetry *current = &console->telemetry;
    gtk_label_set_text(console->serial_label, current->serial_number[0] ? current->serial_number : "未读取");
    gchar *text = g_strdup_printf("%u %%", current->real_power_percent);
    gtk_label_set_text(console->real_power_label, text);
    gtk_progress_bar_set_text(console->power_bar, text);
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
    text = g_strdup_printf("%u / 255  (%.0f%%)", current->db25_power,
                           (gdouble)current->db25_power * 100.0 / 255.0);
    gtk_label_set_text(console->external_power_label, text);
    g_free(text);
    for (guint i = 0; i < G_N_ELEMENTS(console->power_indicators); i++) {
        gboolean active = current->status & (1u << (4 + i));
        gtk_label_set_text(console->power_indicators[i], active ? "1" : "0");
        gtk_widget_remove_css_class(GTK_WIDGET(console->power_indicators[i]), "success");
        gtk_widget_remove_css_class(GTK_WIDGET(console->power_indicators[i]), "dim-label");
        gtk_widget_add_css_class(GTK_WIDGET(console->power_indicators[i]), active ? "success" : "dim-label");
    }

    for (guint i = 0; i < G_N_ELEMENTS(console->alarm_indicators); i++) {
        gboolean alarm = current->alarm_state & (1u << i);
        set_indicator(console->alarm_indicators[i], !alarm, alarm);
        gchar *count = g_strdup_printf("%u 次", current->alarm_counts[i]);
        gtk_label_set_text(console->alarm_counts[i], count);
        g_free(count);
    }

    console->laser_on = (current->status & (1u << 0)) != 0;
    console->mo_on = (current->status & (1u << 1)) != 0;
    console->red_on = (current->status & (1u << 2)) != 0;
    update_device_status(console);
    update_output_buttons(console);
}

static void result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    if (console->closing) {
        return;
    }
    if (result->type == JPT_CLIENT_CONNECTION) {
        console->connection_pending = FALSE;
        if (result->connected) {
            console->connected = TRUE;
            gtk_button_set_label(console->connect_button, "断开");
            set_connection_state(console, "正在读取设备", FALSE, FALSE);
            show_connection_page(console, "正在读取设备", "连接已建立，正在读取激光器信息。",
                                "network-wired-symbolic");
            append_log(console, "连接已建立，开始读取设备信息");
            console->initialized = FALSE;
            jpt_client_read_initial_async(console->client, result_callback, console);
        } else {
            console->connected = FALSE;
            gtk_check_button_set_active(console->safety_ack, FALSE);
            if (console->protocol_ack != NULL) {
                gtk_check_button_set_active(console->protocol_ack, FALSE);
            }
            set_connection_state(console, result->error ? result->error->message : "连接失败", FALSE, TRUE);
            gtk_button_set_label(console->connect_button, "连接");
            show_connection_page(console, "连接失败",
                                 result->error ? result->error->message : "无法建立设备连接。",
                                 "dialog-warning-symbolic");
            if (result->error != NULL) {
                gtk_label_set_text(console->last_error_label, result->error->message);
            }
        }
        update_device_status(console);
        update_output_buttons(console);
        return;
    }

    if (result->type == JPT_CLIENT_DISCONNECTED) {
        console->connected = FALSE;
        gtk_check_button_set_active(console->safety_ack, FALSE);
        if (console->protocol_ack != NULL) {
            gtk_check_button_set_active(console->protocol_ack, FALSE);
        }
        console->connection_pending = FALSE;
        console->poll_pending = FALSE;
        console->initialized = FALSE;
        set_connection_state(console, "未连接", FALSE, FALSE);
        gtk_button_set_label(console->connect_button, "连接");
        show_connection_page(console, "设备未连接",
                             "选择串口或 TCP 地址并连接激光器。",
                             "network-offline-symbolic");
        update_device_status(console);
        update_output_buttons(console);
        return;
    }

    if (result->type == JPT_CLIENT_TELEMETRY) {
        console->poll_pending = FALSE;
        if (!console->connected) {
            return;
        }
        if (result->error != NULL) {
            gtk_label_set_text(console->last_error_label, result->error->message);
            append_log(console, "轮询失败：%s", result->error->message);
            if (!console->initialized) {
                set_connection_state(console, "设备读取失败", FALSE, TRUE);
                set_status_item(console->device_indicator, console->device_status_label,
                                "设备读取失败", "error");
                show_connection_page(console, "设备读取失败", result->error->message,
                                     "dialog-warning-symbolic");
            }
            return;
        }
        update_telemetry(console, &result->telemetry);
        if (!console->initialized) {
            adw_spin_row_set_value(console->power_spin, result->telemetry.real_power_percent);
            adw_spin_row_set_value(console->default_simmer_spin, result->telemetry.default_simmer);
            adw_spin_row_set_value(console->default_pulse_spin, result->telemetry.default_pulse_width);
            adw_spin_row_set_value(console->default_frequency_spin, result->telemetry.lock_frequency);
            adw_spin_row_set_value(console->lock_power_spin,
                                   (gdouble)result->telemetry.lock_power * 100.0 / 4095.0);
            adw_spin_row_set_value(console->red_power_spin, result->telemetry.red_light_power);
            adw_spin_row_set_value(console->frequency_spin, result->telemetry.real_frequency);
            adw_spin_row_set_value(console->pulse_spin, result->telemetry.real_pulse_width);
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
            set_connection_state(console, "已连接", TRUE, FALSE);
            gtk_stack_set_visible_child_name(console->main_stack, "workbench");
            adw_overlay_split_view_set_show_sidebar(console->workbench_split_view, TRUE);
        }
        update_device_status(console);
        update_output_buttons(console);
        if (console->poll_source == 0) {
            console->poll_source = g_timeout_add_seconds(1, poll_tick, console);
        }
        return;
    }

    if (result->type == JPT_CLIENT_EXCHANGE) {
        if (result->error != NULL) {
            gtk_label_set_text(console->last_error_label, result->error->message);
            append_log(console, "命令失败：%s", result->error->message);
            set_status_item(console->command_indicator, console->last_command_label,
                            "命令失败", "error");
        } else if (result->exchange != NULL) {
            gchar *message = g_strdup_printf("命令 0x%04X 已确认", result->exchange->frame->command);
            set_status_item(console->command_indicator, console->last_command_label,
                            message, "success");
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
        console->connected = FALSE;
        console->connection_pending = TRUE;
        set_connection_state(console, "正在断开", FALSE, FALSE);
        show_connection_page(console, "正在断开连接", "正在关闭设备连接。",
                             "network-offline-symbolic");
        update_device_status(console);
        update_output_buttons(console);
        jpt_client_disconnect_async(console->client, result_callback, console);
        return;
    }

    gboolean serial = gtk_drop_down_get_selected(console->transport_dropdown) == 0;
    const gchar *endpoint = gtk_editable_get_text(GTK_EDITABLE(console->endpoint_entry));
    if (serial) {
        guint selected = gtk_drop_down_get_selected(console->serial_port_dropdown);
        if (selected > 0 && selected <= console->serial_ports->len) {
            SerialPort *port = g_ptr_array_index(console->serial_ports, selected - 1);
            endpoint = port->path;
        } else if (selected == console->serial_ports->len + 1) {
            endpoint = gtk_editable_get_text(GTK_EDITABLE(console->manual_serial_entry));
            if (endpoint[0] == '\0') {
                set_connection_state(console, "未填写串口路径", FALSE, TRUE);
                show_connection_page(console, "请填写串口路径",
                                     "填写设备路径，或返回串口列表选择一个设备。",
                                     "dialog-warning-symbolic");
                return;
            }
        } else {
            set_connection_state(console, "未选择串口", FALSE, TRUE);
            show_connection_page(console, "请选择串口设备",
                                 console->serial_ports->len == 0
                                     ? "当前没有检测到串口设备。刷新设备列表或手动填写路径。"
                                     : "从串口列表中选择要连接的设备。",
                                 "dialog-warning-symbolic");
            return;
        }
    } else if (endpoint[0] == '\0') {
        set_connection_state(console, "请输入设备地址", FALSE, TRUE);
        show_connection_page(console, "设备地址为空",
                             "填写主机名或 IP 地址后再连接。",
                             "dialog-warning-symbolic");
        return;
    }

    console->connection_pending = TRUE;
    set_connection_state(console, "正在连接", FALSE, FALSE);
    show_connection_page(console, "正在连接设备", "正在建立设备连接。",
                         "network-wired-symbolic");
    update_device_status(console);
    update_output_buttons(console);
    if (serial) {
        JptSerialConfig config = serial_config_from_ui(console);
        jpt_client_connect_serial_async(console->client, endpoint, &config, result_callback, console);
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

static gboolean raw_request_from_ui(LaserConsole *console, gboolean write, GBytes **payload,
                                    guint16 *command)
{
    GError *error = NULL;
    if (!parse_command_value(gtk_editable_get_text(GTK_EDITABLE(console->raw_command_entry)),
                             command, &error)) {
        gtk_label_set_text(console->raw_result_label, error->message);
        g_clear_error(&error);
        return FALSE;
    }
    const gchar *payload_text = gtk_editable_get_text(GTK_EDITABLE(console->raw_payload_entry));
    if (!write && payload_text[0] == '\0') {
        *payload = jpt_device_read_request(*command, &error);
    } else {
        parse_hex_bytes(payload_text, payload, &error);
    }
    if (error != NULL) {
        gtk_label_set_text(console->raw_result_label, error->message);
        g_clear_error(&error);
        return FALSE;
    }
    return TRUE;
}

static void raw_result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    result_callback(result, console);
    if (result->error != NULL) {
        gtk_label_set_text(console->raw_result_label, result->error->message);
    } else if (result->exchange != NULL) {
        gchar *message = g_strdup_printf("已收到 0x%04X 的响应", result->exchange->frame->command);
        gtk_label_set_text(console->raw_result_label, message);
        g_free(message);
    }
}

static void protocol_command_selected(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    LaserConsole *console = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected >= G_N_ELEMENTS(protocol_commands)) {
        return;
    }
    const ProtocolCommand *option = &protocol_commands[selected];
    gchar *command = g_strdup_printf("0x%02X", option->command);
    gtk_editable_set_text(GTK_EDITABLE(console->raw_command_entry), command);
    gtk_editable_set_text(GTK_EDITABLE(console->raw_payload_entry),
                          option->command == JPT_CMD_IP_ADDRESS ? "00 00 00 00" : "00 00");
    gtk_label_set_text(console->raw_result_label, "");
    gtk_widget_set_sensitive(GTK_WIDGET(console->raw_read_button), option->readable);
    g_free(command);
}

static void raw_read_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    GBytes *payload = NULL;
    guint16 command = 0;
    if (!raw_request_from_ui(console, FALSE, &payload, &command)) {
        return;
    }
    gtk_label_set_text(console->raw_result_label, "读取中…");
    jpt_client_request_async(console->client, command, payload, raw_result_callback, console);
    g_bytes_unref(payload);
}

static void raw_send_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    if (!gtk_check_button_get_active(console->protocol_ack)) {
        gtk_label_set_text(console->raw_result_label, "发送命令前请确认防护区域和联锁状态");
        return;
    }
    GBytes *payload = NULL;
    guint16 command = 0;
    if (!raw_request_from_ui(console, TRUE, &payload, &command)) {
        return;
    }
    gtk_label_set_text(console->raw_result_label, "发送中…");
    jpt_client_request_async(console->client, command, payload, raw_result_callback, console);
    g_bytes_unref(payload);
}

static void network_port_written(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    result_callback(result, console);
    gtk_label_set_text(console->raw_result_label,
                       result->error == NULL
                           ? "TCP 地址已写入，重启设备后生效"
                           : result->error->message);
}

static void network_ip_written(const JptClientResult *result, gpointer user_data)
{
    NetworkAddressWrite *write = user_data;
    LaserConsole *console = write->console;
    result_callback(result, console);
    if (result->error == NULL) {
        jpt_client_write_u16_async(console->client, JPT_CMD_TCP_PORT, write->port,
                                   network_port_written, console);
    } else {
        gtk_label_set_text(console->raw_result_label, result->error->message);
    }
    g_free(write);
}

static void extension_set_status(LaserConsole *console, const gchar *text)
{
    if (console->extension_status_label != NULL) {
        gtk_label_set_text(console->extension_status_label, text);
    }
}

static guint16 read_payload_u16(const guint8 *data, gsize offset)
{
    return (guint16)data[offset] | ((guint16)data[offset + 1] << 8);
}

static guint32 read_payload_u24(const guint8 *data, gsize offset)
{
    return (guint32)data[offset] |
        ((guint32)data[offset + 1] << 8) |
        ((guint32)data[offset + 2] << 16);
}

static void m7_cutoff_result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    result_callback(result, console);
    if (result->error != NULL || result->exchange == NULL) {
        extension_set_status(console, result->error ? result->error->message : "设备未返回截止频率");
        return;
    }
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(result->exchange->frame->payload, &length);
    gsize offset = length >= 24 ? 2 : 0;
    if (length < offset + 22) {
        extension_set_status(console, "M7 截止频率响应长度不足");
        return;
    }
    for (guint i = 0; i < G_N_ELEMENTS(console->m7_cutoff_spins); i++) {
        adw_spin_row_set_value(console->m7_cutoff_spins[i], read_payload_u16(payload, offset + i * 2));
    }
    extension_set_status(console, "M7 截止频率已读取");
}

static void m7_cutoff_read_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    extension_set_status(console, "正在读取 M7 截止频率…");
    GBytes *request = jpt_device_read_request(JPT_CMD_READ_CUTOFF_FREQUENCY_M7, NULL);
    jpt_client_request_async(console->client, JPT_CMD_READ_CUTOFF_FREQUENCY_M7,
                             request, m7_cutoff_result_callback, console);
    g_bytes_unref(request);
}

static void m7_cutoff_write_result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    result_callback(result, console);
    extension_set_status(console, result->error == NULL ? "M7 截止频率已写入" : result->error->message);
}

static void m7_cutoff_write_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    if (!gtk_check_button_get_active(console->protocol_ack)) {
        extension_set_status(console, "写入前请确认设备处于安全维护状态");
        return;
    }
    GByteArray *payload = g_byte_array_new();
    for (guint i = 0; i < G_N_ELEMENTS(console->m7_cutoff_spins); i++) {
        guint16 value = (guint16)adw_spin_row_get_value(console->m7_cutoff_spins[i]);
        guint8 bytes[] = {(guint8)(value & 0xff), (guint8)(value >> 8)};
        g_byte_array_append(payload, bytes, sizeof(bytes));
    }
    GBytes *request = g_byte_array_free_to_bytes(payload);
    extension_set_status(console, "正在写入 M7 截止频率…");
    jpt_client_request_async(console->client, JPT_CMD_CUTOFF_FREQUENCY_M7,
                             request, m7_cutoff_write_result_callback, console);
    g_bytes_unref(request);
}

static void alarm_cache_result_callback(const JptClientResult *result, gpointer user_data)
{
    LaserConsole *console = user_data;
    result_callback(result, console);
    if (result->error != NULL || result->exchange == NULL) {
        extension_set_status(console, result->error ? result->error->message : "设备未返回报警缓存");
        return;
    }
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(result->exchange->frame->payload, &length);
    if (length < 6) {
        extension_set_status(console, "报警缓存响应长度不足");
        return;
    }
    guint count = payload[5];
    if (count == 0) {
        gtk_text_buffer_set_text(console->alarm_cache_buffer, "设备没有报警缓存记录。", -1);
        extension_set_status(console, "报警缓存为空");
        return;
    }
    if (length < 6 + (gsize)count * 108) {
        extension_set_status(console, "报警缓存响应不完整");
        return;
    }
    GString *text = g_string_new(NULL);
    for (guint i = 0; i < count; i++) {
        const guint8 *record = payload + 6 + i * 108;
        guint32 total_time = read_payload_u24(record, 102);
        guint32 open_time = read_payload_u24(record, 105);
        g_string_append_printf(text,
                               "记录 %u · 类型 %u · 分类 %u · 累计功率时间 %u min · 出光时间 %u min\n",
                               i + 1, record[0], record[1], total_time, open_time);
        g_string_append_printf(text, "  功率: %u %u %u %u %u %u %u %u %u %u\n",
                               read_payload_u16(record, 2), read_payload_u16(record, 4),
                               read_payload_u16(record, 6), read_payload_u16(record, 8),
                               read_payload_u16(record, 10), read_payload_u16(record, 12),
                               read_payload_u16(record, 14), read_payload_u16(record, 16),
                               read_payload_u16(record, 18), read_payload_u16(record, 20));
    }
    gtk_text_buffer_set_text(console->alarm_cache_buffer, text->str, -1);
    g_string_free(text, TRUE);
    extension_set_status(console, "报警缓存已读取");
}

static void alarm_cache_refresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    extension_set_status(console, "正在读取报警缓存…");
    GBytes *request = jpt_device_read_request(JPT_CMD_READ_ALARM_CACHE, NULL);
    jpt_client_request_async(console->client, JPT_CMD_READ_ALARM_CACHE,
                             request, alarm_cache_result_callback, console);
    g_bytes_unref(request);
}

static void network_address_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    if (!gtk_check_button_get_active(console->protocol_ack)) {
        gtk_label_set_text(console->raw_result_label, "写入地址前请确认防护区域和联锁状态");
        return;
    }
    const gchar *ip_text = gtk_editable_get_text(GTK_EDITABLE(console->device_ip_entry));
    const gchar *port_text = gtk_editable_get_text(GTK_EDITABLE(console->device_port_entry));
    GInetAddress *address = g_inet_address_new_from_string(ip_text);
    guint64 port = g_ascii_strtoull(port_text, NULL, 10);
    if (address == NULL || port == 0 || port > G_MAXUINT16 || g_inet_address_get_family(address) != G_SOCKET_FAMILY_IPV4) {
        g_clear_object(&address);
        gtk_label_set_text(console->raw_result_label, "请输入有效的 IPv4 地址和 TCP 端口");
        return;
    }
    const guint8 *ip = g_inet_address_to_bytes(address);
    GBytes *ip_payload = g_bytes_new(ip, 4);
    NetworkAddressWrite *write = g_new0(NetworkAddressWrite, 1);
    write->console = console;
    write->port = (guint16)port;
    gtk_label_set_text(console->raw_result_label, "正在写入 TCP IP 地址");
    jpt_client_request_async(console->client, JPT_CMD_IP_ADDRESS, ip_payload,
                             network_ip_written, write);
    g_bytes_unref(ip_payload);
    g_object_unref(address);
}

static void alarm_action_clicked(GtkButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    guint command = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "command"));
    send_u16(console, (guint16)command, 0);
}

static void send_u16(LaserConsole *console, guint16 command, guint16 value)
{
    if (!console->connected || !console->initialized) {
        gtk_label_set_text(console->last_error_label, "设备尚未就绪");
        return;
    }
    set_status_item(console->command_indicator, console->last_command_label,
                    "命令发送中", "warning");
    jpt_client_write_u16_async(console->client, command, value, write_result_callback, console);
}

static gboolean write_spin_value(LaserConsole *console, AdwSpinRow *spin,
                                 guint command, guint control_bit)
{
    if (!console->connected || !console->initialized ||
        (control_bit != G_MAXUINT &&
        !(console->telemetry.control_mode & (1u << control_bit)))) {
        return FALSE;
    }
    guint value = (guint)adw_spin_row_get_value(spin);
    if (command == JPT_CMD_POWER || command == JPT_CMD_LOCK_POWER) {
        value = (guint)((gdouble)value * 4095.0 / 100.0 + 0.5);
    }
    send_u16(console, (guint16)command, (guint16)value);
    return TRUE;
}

static void spin_write_clicked(GtkButton *button, gpointer user_data)
{
    LaserConsole *console = user_data;
    guint control_bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "control-bit"));
    AdwSpinRow *spin = g_object_get_data(G_OBJECT(button), "spin");
    guint command = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "command"));
    write_spin_value(console, spin, command, control_bit);
}

static gboolean spin_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                                 GdkModifierType state, gpointer user_data)
{
    (void)controller;
    (void)keycode;
    (void)state;
    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter) {
        return FALSE;
    }
    GtkWidget *button = user_data;
    AdwSpinRow *spin = g_object_get_data(G_OBJECT(button), "spin");
    adw_spin_row_update(spin);
    gtk_widget_activate(button);
    return TRUE;
}

static void apply_parameters_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LaserConsole *console = user_data;
    const struct {
        AdwSpinRow *spin;
        guint command;
        guint control_bit;
    } settings[] = {
        {console->power_spin, JPT_CMD_POWER, 4},
        {console->frequency_spin, JPT_CMD_FREQUENCY, 3},
        {console->pulse_spin, JPT_CMD_PULSE_WIDTH, 2},
        {console->simmer_spin, JPT_CMD_SIMMER, G_MAXUINT},
    };
    guint sent = 0;
    for (guint i = 0; i < G_N_ELEMENTS(settings); i++) {
        sent += write_spin_value(console, settings[i].spin, settings[i].command,
                                 settings[i].control_bit);
    }
    if (sent > 0) {
        set_status_item(console->command_indicator, console->last_command_label,
                        "正在应用实时参数", "warning");
    }
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
    if (!console->connected || !console->initialized) {
        return;
    }
    send_u16(console, JPT_CMD_GUI_PA, 0);
    send_u16(console, JPT_CMD_GUI_MO, 0);
    send_u16(console, JPT_CMD_RED_LIGHT, 0);
    console->laser_on = FALSE;
    console->mo_on = FALSE;
    console->red_on = FALSE;
    update_output_buttons(console);
    append_log(console, "已发送 PA=0、MO=0、红光=0");
}

static AdwPreferencesGroup *make_group(const gchar *title)
{
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, title);
    return group;
}

static void add_preference_child(GtkWidget *container, GtkWidget *child)
{
    if (ADW_IS_EXPANDER_ROW(container)) {
        adw_expander_row_add_row(ADW_EXPANDER_ROW(container), child);
    } else {
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(container), child);
    }
}

static AdwActionRow *make_property_row(const gchar *title, GtkLabel **value)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    gtk_widget_add_css_class(GTK_WIDGET(row), "property");
    *value = GTK_LABEL(gtk_label_new("--"));
    gtk_label_set_selectable(*value, TRUE);
    adw_action_row_add_suffix(row, GTK_WIDGET(*value));
    return row;
}

static AdwSpinRow *make_spin(gdouble min, gdouble max, gdouble step, gdouble value)
{
    AdwSpinRow *spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(min, max, step));
    adw_spin_row_set_digits(spin, 0);
    adw_spin_row_set_numeric(spin, TRUE);
    adw_spin_row_set_snap_to_ticks(spin, TRUE);
    adw_spin_row_set_value(spin, value);
    return spin;
}

static void add_spin_setting(GtkWidget *container, const gchar *title, AdwSpinRow *spin,
                             guint command, guint control_bit, LaserConsole *console)
{
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(spin), title);
    GtkButton *button = GTK_BUTTON(gtk_button_new_with_label("写入"));
    g_object_set_data(G_OBJECT(button), "spin", spin);
    g_object_set_data(G_OBJECT(button), "command", GUINT_TO_POINTER(command));
    g_object_set_data(G_OBJECT(button), "control-bit", GUINT_TO_POINTER(control_bit));
    g_signal_connect(button, "clicked", G_CALLBACK(spin_write_clicked), console);
    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(spin_key_pressed), button);
    gtk_widget_add_controller(GTK_WIDGET(spin), key_controller);
    if (console->write_buttons != NULL) {
        g_ptr_array_add(console->write_buttons, button);
    }
    adw_action_row_add_suffix(ADW_ACTION_ROW(spin), GTK_WIDGET(button));
    add_preference_child(container, GTK_WIDGET(spin));
}

static GtkCheckButton *add_control_option(GtkWidget *container, const gchar *title,
                                           guint bit, LaserConsole *console)
{
    GtkCheckButton *button = GTK_CHECK_BUTTON(gtk_check_button_new());
    g_object_set_data(G_OBJECT(button), "control-bit", GUINT_TO_POINTER(bit));
    g_signal_connect(button, "toggled", G_CALLBACK(control_option_toggled), console);
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_add_suffix(row, GTK_WIDGET(button));
    adw_action_row_set_activatable_widget(row, GTK_WIDGET(button));
    add_preference_child(container, GTK_WIDGET(row));
    return button;
}

static GtkCheckButton *add_mask_option(GtkWidget *container, const gchar *title,
                                       guint command, guint bit, guint source, LaserConsole *console)
{
    GtkCheckButton *button = GTK_CHECK_BUTTON(gtk_check_button_new());
    g_object_set_data(G_OBJECT(button), "mask-command", GUINT_TO_POINTER(command));
    g_object_set_data(G_OBJECT(button), "mask-bit", GUINT_TO_POINTER(bit));
    g_object_set_data(G_OBJECT(button), "mask-source", GUINT_TO_POINTER(source));
    g_signal_connect(button, "toggled", G_CALLBACK(mask_option_toggled), console);
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_add_suffix(row, GTK_WIDGET(button));
    adw_action_row_set_activatable_widget(row, GTK_WIDGET(button));
    add_preference_child(container, GTK_WIDGET(row));
    return button;
}

static GtkWidget *build_control_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "控制台");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(page));
    console->control_page = scroll;

    AdwPreferencesGroup *safety_group = make_group("输出许可");
    adw_preferences_group_set_description(
        safety_group,
        "1064 nm 不可见，设为 0% 时输出端仍可能有约 100 mW。启用前确认护目镜、联锁和防护区域。");
    AdwActionRow *safety_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(safety_row), "已确认防护区域和联锁状态");
    console->safety_ack = GTK_CHECK_BUTTON(gtk_check_button_new());
    adw_action_row_add_suffix(safety_row, GTK_WIDGET(console->safety_ack));
    adw_action_row_set_activatable_widget(safety_row, GTK_WIDGET(console->safety_ack));
    g_signal_connect(console->safety_ack, "toggled", G_CALLBACK(safety_changed), console);
    adw_preferences_group_add(safety_group, GTK_WIDGET(safety_row));
    adw_preferences_page_add(page, safety_group);

    AdwPreferencesGroup *parameter_group = make_group("实时参数写入");
    console->power_spin = make_spin(0, 100, 1, 0);
    console->frequency_spin = make_spin(1, 4000, 1, 100);
    console->pulse_spin = make_spin(1, 500, 1, 20);
    console->simmer_spin = make_spin(0, 1000, 1, 0);
    add_spin_setting(GTK_WIDGET(parameter_group), "功率 (%)", console->power_spin, JPT_CMD_POWER, 4, console);
    add_spin_setting(GTK_WIDGET(parameter_group), "频率 (kHz)", console->frequency_spin, JPT_CMD_FREQUENCY, 3, console);
    add_spin_setting(GTK_WIDGET(parameter_group), "脉宽 (ns)", console->pulse_spin, JPT_CMD_PULSE_WIDTH, 2, console);
    add_spin_setting(GTK_WIDGET(parameter_group), "Simmer", console->simmer_spin, JPT_CMD_SIMMER, G_MAXUINT, console);
    console->apply_parameters_button = GTK_BUTTON(gtk_button_new());
    AdwButtonContent *apply_content = ADW_BUTTON_CONTENT(adw_button_content_new());
    adw_button_content_set_icon_name(apply_content, "emblem-ok-symbolic");
    adw_button_content_set_label(apply_content, "应用实时参数");
    gtk_button_set_child(console->apply_parameters_button, GTK_WIDGET(apply_content));
    gtk_widget_add_css_class(GTK_WIDGET(console->apply_parameters_button), "suggested-action");
    g_signal_connect(console->apply_parameters_button, "clicked",
                     G_CALLBACK(apply_parameters_clicked), console);
    adw_preferences_group_set_header_suffix(parameter_group,
                                             GTK_WIDGET(console->apply_parameters_button));
    adw_preferences_page_add(page, parameter_group);

    AdwPreferencesGroup *output_group = make_group("控制来源与输出");
    adw_preferences_group_set_description(
        output_group,
        "连接后沿用设备当前模式；只有明确切换时才会写入控制模式。外控模式下由 JCZ/DB25 提供出光信号。");
    adw_preferences_group_add(output_group,
                               GTK_WIDGET(make_property_row("设备当前模式", &console->control_source_label)));
    AdwActionRow *mode_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mode_row), "切换控制来源");
    adw_action_row_set_subtitle(mode_row, "切换会立即写入设备控制模式");
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    console->free_mode = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("JCZ/DB25 外控"));
    console->gui_mode = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("MS_LEP 内控"));
    gtk_check_button_set_group(console->gui_mode, console->free_mode);
    gtk_check_button_set_active(console->free_mode, TRUE);
    gtk_box_append(GTK_BOX(mode_box), GTK_WIDGET(console->free_mode));
    gtk_box_append(GTK_BOX(mode_box), GTK_WIDGET(console->gui_mode));
    adw_action_row_add_suffix(mode_row, mode_box);
    g_signal_connect(console->free_mode, "toggled", G_CALLBACK(mode_toggled), console);
    g_signal_connect(console->gui_mode, "toggled", G_CALLBACK(mode_toggled), console);
    adw_preferences_group_add(output_group, GTK_WIDGET(mode_row));

    AdwActionRow *output_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(output_row), "输出控制");
    adw_action_row_set_subtitle(output_row, "PA、MO 和红光信号");
    GtkWidget *output_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    console->laser_button = GTK_BUTTON(gtk_button_new_with_label("开启激光"));
    console->red_button = GTK_BUTTON(gtk_button_new_with_label("开启红光"));
    console->mo_button = GTK_BUTTON(gtk_button_new_with_label("开启 MO"));
    gtk_widget_add_css_class(GTK_WIDGET(console->laser_button), "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(console->red_button), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(console->mo_button), "flat");
    g_signal_connect(console->laser_button, "clicked", G_CALLBACK(output_button_clicked), console);
    g_signal_connect(console->red_button, "clicked", G_CALLBACK(output_button_clicked), console);
    g_signal_connect(console->mo_button, "clicked", G_CALLBACK(output_button_clicked), console);
    gtk_box_append(GTK_BOX(output_box), GTK_WIDGET(console->laser_button));
    gtk_box_append(GTK_BOX(output_box), GTK_WIDGET(console->red_button));
    gtk_box_append(GTK_BOX(output_box), GTK_WIDGET(console->mo_button));
    adw_action_row_add_suffix(output_row, output_box);
    adw_preferences_group_add(output_group, GTK_WIDGET(output_row));
    adw_preferences_page_add(page, output_group);

    return scroll;
}

static GtkWidget *build_settings_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "设备设置");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(page));
    console->settings_page = scroll;

    AdwPreferencesGroup *control_group = make_group("控制来源");
    const gchar *control_names[] = {"GUI MO", "GUI PA", "GUI 脉宽", "GUI 频率", "GUI 功率"};
    for (guint i = 0; i < G_N_ELEMENTS(control_names); i++) {
        console->control_options[i] = add_control_option(GTK_WIDGET(control_group), control_names[i], i, console);
    }
    adw_preferences_page_add(page, control_group);

    AdwPreferencesGroup *parameter_group = make_group("外部接口");
    const gchar *parameter_names[] = {
        "Pin9 功率锁存", "Pin23 急停", "外部频率", "风扇调速", "红光模式", "增强模式"
    };
    const guint parameter_bits[] = {0, 1, 3, 4, 5, 6};
    for (guint i = 0; i < G_N_ELEMENTS(parameter_names); i++) {
        console->parameter_options[i] = add_mask_option(GTK_WIDGET(parameter_group), parameter_names[i],
                                                        JPT_CMD_PARAMETER_SELECT, parameter_bits[i], 0,
                                                        console);
    }
    adw_preferences_page_add(page, parameter_group);

    AdwPreferencesGroup *lock_group = make_group("默认与锁定参数");
    const gchar *lock_names[] = {"锁定频率", "锁定功率"};
    for (guint i = 0; i < G_N_ELEMENTS(lock_names); i++) {
        console->lock_options[i] = add_mask_option(GTK_WIDGET(lock_group), lock_names[i],
                                                   JPT_CMD_LOCK_PARAMETER, i, 1, console);
    }
    console->default_frequency_spin = make_spin(1, 4000, 1, 100);
    console->default_pulse_spin = make_spin(1, 500, 1, 20);
    console->default_simmer_spin = make_spin(0, 1000, 1, 0);
    console->lock_power_spin = make_spin(0, 100, 1, 0);
    console->red_power_spin = make_spin(0, 100, 1, 0);
    add_spin_setting(GTK_WIDGET(lock_group), "锁定频率 (kHz)", console->default_frequency_spin,
                     JPT_CMD_LOCK_FREQUENCY, G_MAXUINT, console);
    add_spin_setting(GTK_WIDGET(lock_group), "默认脉宽 (ns)", console->default_pulse_spin,
                     JPT_CMD_DEFAULT_PULSE_WIDTH, G_MAXUINT, console);
    add_spin_setting(GTK_WIDGET(lock_group), "默认 Simmer", console->default_simmer_spin,
                     JPT_CMD_DEFAULT_SIMMER, G_MAXUINT, console);
    add_spin_setting(GTK_WIDGET(lock_group), "锁定功率 (%)", console->lock_power_spin,
                     JPT_CMD_LOCK_POWER, G_MAXUINT, console);
    add_spin_setting(GTK_WIDGET(lock_group), "红光功率 (%)", console->red_power_spin,
                     JPT_CMD_RED_LIGHT_POWER, G_MAXUINT, console);
    adw_preferences_page_add(page, lock_group);

    return scroll;
}

static GtkWidget *build_monitor_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "监控");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(page));

    AdwPreferencesGroup *metrics_group = make_group("实时监控");
    const struct {
        const gchar *title;
        GtkLabel **value;
    } metrics[] = {
        {"实时功率", &console->real_power_label},
        {"实时频率", &console->real_frequency_label},
        {"实时脉宽", &console->real_pulse_label},
        {"传感器 1", &console->sensor_1_label},
        {"传感器 2", &console->sensor_2_label},
        {"种子源 TEC", &console->tec_label},
        {"输出电流", &console->current_label},
        {"SN", &console->serial_label},
    };
    for (guint i = 0; i < G_N_ELEMENTS(metrics); i++) {
        adw_preferences_group_add(metrics_group,
                                   GTK_WIDGET(make_property_row(metrics[i].title, metrics[i].value)));
    }
    AdwActionRow *power_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(power_row), "实时功率");
    console->power_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(console->power_bar, TRUE);
    gtk_progress_bar_set_text(console->power_bar, "0%");
    gtk_widget_set_hexpand(GTK_WIDGET(console->power_bar), TRUE);
    adw_action_row_add_suffix(power_row, GTK_WIDGET(console->power_bar));
    adw_preferences_group_add(metrics_group, GTK_WIDGET(power_row));
    adw_preferences_page_add(page, metrics_group);

    AdwPreferencesGroup *signal_group = make_group("信号状态");
    const gchar *signal_names[] = {"PA", "MO", "红光", "泵浦"};
    for (guint i = 0; i < G_N_ELEMENTS(signal_names); i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), signal_names[i]);
        console->status_indicators[i] = GTK_LABEL(gtk_label_new("关闭"));
        gtk_widget_add_css_class(GTK_WIDGET(console->status_indicators[i]), "dim-label");
        adw_action_row_add_suffix(row, GTK_WIDGET(console->status_indicators[i]));
        adw_preferences_group_add(signal_group, GTK_WIDGET(row));
    }

    const gchar *protocol_names[] = {"控制模式", "状态字", "报警字"};
    GtkLabel **protocol_values[] = {
        &console->control_mode_label, &console->status_label, &console->alarm_state_label
    };
    for (guint i = 0; i < G_N_ELEMENTS(protocol_names); i++) {
        adw_preferences_group_add(signal_group,
                                   GTK_WIDGET(make_property_row(protocol_names[i], protocol_values[i])));
    }

    AdwActionRow *external_power_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(external_power_row), "DB25 功率输入");
    console->external_power_label = GTK_LABEL(gtk_label_new("0 / 255  (0%)"));
    adw_action_row_add_suffix(external_power_row, GTK_WIDGET(console->external_power_label));
    adw_preferences_group_add(signal_group, GTK_WIDGET(external_power_row));
    adw_preferences_page_add(page, signal_group);

    AdwPreferencesGroup *input_group = make_group("DB25 输入");
    for (guint i = 0; i < G_N_ELEMENTS(console->power_indicators); i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        gchar *title = g_strdup_printf("D%u", 7 - i);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
        g_free(title);
        console->power_indicators[i] = GTK_LABEL(gtk_label_new("0"));
        gtk_widget_add_css_class(GTK_WIDGET(console->power_indicators[i]), "dim-label");
        adw_action_row_add_suffix(row, GTK_WIDGET(console->power_indicators[i]));
        adw_preferences_group_add(input_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(page, input_group);

    AdwPreferencesGroup *alarm_group = make_group("报警状态与计数");
    for (guint i = 0; i < G_N_ELEMENTS(alarm_names); i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), alarm_names[i]);
        console->alarm_indicators[i] = GTK_LABEL(gtk_label_new("关闭"));
        console->alarm_counts[i] = GTK_LABEL(gtk_label_new("0"));
        gtk_widget_add_css_class(GTK_WIDGET(console->alarm_indicators[i]), "dim-label");
        adw_action_row_add_suffix(row, GTK_WIDGET(console->alarm_indicators[i]));
        adw_action_row_add_suffix(row, GTK_WIDGET(console->alarm_counts[i]));
        adw_preferences_group_add(alarm_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(page, alarm_group);

    console->alarm_actions_group = make_group("报警与累计值维护");
    const struct {
        const gchar *title;
        guint command;
    } alarm_actions[] = {
        {"复位当前报警", JPT_CMD_ALARM_RESET},
        {"清除报警次数", JPT_CMD_ALARM_CLEAR_COUNT},
        {"清除累计出光时间", JPT_CMD_LASER_TIME_CLEAR},
    };
    for (guint i = 0; i < G_N_ELEMENTS(alarm_actions); i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), alarm_actions[i].title);
        GtkButton *action = GTK_BUTTON(gtk_button_new_with_label("执行"));
        g_object_set_data(G_OBJECT(action), "command", GUINT_TO_POINTER(alarm_actions[i].command));
        g_signal_connect(action, "clicked", G_CALLBACK(alarm_action_clicked), console);
        adw_action_row_add_suffix(row, GTK_WIDGET(action));
        adw_preferences_group_add(console->alarm_actions_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(page, console->alarm_actions_group);
    return scroll;
}

static GtkWidget *build_protocol_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "协议工具");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(page));
    console->protocol_page = scroll;

    AdwPreferencesGroup *command_group = make_group("JPT 命令");
    adw_preferences_group_set_description(
        command_group,
        "命令请求和响应会记录在通信日志中。");
    AdwActionRow *ack_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ack_row), "已确认设备处于安全维护状态");
    console->protocol_ack = GTK_CHECK_BUTTON(gtk_check_button_new());
    adw_action_row_add_suffix(ack_row, GTK_WIDGET(console->protocol_ack));
    adw_action_row_set_activatable_widget(ack_row, GTK_WIDGET(console->protocol_ack));
    g_signal_connect(console->protocol_ack, "toggled", G_CALLBACK(safety_changed), console);
    adw_preferences_group_add(command_group, GTK_WIDGET(ack_row));
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(protocol_commands); i++) {
        gtk_string_list_append(model, protocol_commands[i].label);
    }
    console->raw_command_dropdown = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(model), NULL));
    gtk_drop_down_set_enable_search(console->raw_command_dropdown, TRUE);
    g_object_unref(model);
    AdwActionRow *command_row = make_widget_row("命令", GTK_WIDGET(console->raw_command_dropdown));
    adw_preferences_group_add(command_group, GTK_WIDGET(command_row));

    console->raw_command_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(console->raw_command_entry, "0x3F");
    gtk_widget_set_size_request(GTK_WIDGET(console->raw_command_entry), 120, -1);
    AdwActionRow *address_row = make_widget_row("命令地址（HEX）", GTK_WIDGET(console->raw_command_entry));
    adw_preferences_group_add(command_group, GTK_WIDGET(address_row));

    console->raw_payload_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(console->raw_payload_entry, "00 00");
    gtk_widget_set_hexpand(GTK_WIDGET(console->raw_payload_entry), TRUE);
    AdwActionRow *payload_row = make_widget_row("数据（HEX）", GTK_WIDGET(console->raw_payload_entry));
    adw_preferences_group_add(command_group, GTK_WIDGET(payload_row));

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    console->raw_read_button = GTK_BUTTON(gtk_button_new_with_label("读取"));
    console->raw_send_button = GTK_BUTTON(gtk_button_new_with_label("发送"));
    gtk_widget_add_css_class(GTK_WIDGET(console->raw_send_button), "suggested-action");
    g_signal_connect(console->raw_read_button, "clicked", G_CALLBACK(raw_read_clicked), console);
    g_signal_connect(console->raw_send_button, "clicked", G_CALLBACK(raw_send_clicked), console);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(console->raw_read_button));
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(console->raw_send_button));
    adw_preferences_group_set_header_suffix(command_group, actions);

    GtkWidget *result_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(result_row, 6);
    console->raw_result_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(console->raw_result_label, 0.0f);
    gtk_label_set_wrap(console->raw_result_label, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(console->raw_result_label), "dim-label");
    gtk_box_append(GTK_BOX(result_row), GTK_WIDGET(console->raw_result_label));
    adw_preferences_group_add(command_group, result_row);
    adw_preferences_page_add(page, command_group);

    AdwPreferencesGroup *network_group = make_group("TCP 地址修改");
    adw_preferences_group_set_description(
        network_group,
        "地址和端口命令写入后需重启激光器生效。请记录新地址，以便重新连接。");
    console->device_ip_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(console->device_ip_entry), "192.168.0.10");
    gtk_entry_set_input_purpose(console->device_ip_entry, GTK_INPUT_PURPOSE_URL);
    adw_preferences_group_add(network_group,
                              GTK_WIDGET(make_widget_row("新 IP 地址", GTK_WIDGET(console->device_ip_entry))));
    console->device_port_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(console->device_port_entry), "5000");
    gtk_entry_set_input_purpose(console->device_port_entry, GTK_INPUT_PURPOSE_DIGITS);
    adw_preferences_group_add(network_group,
                              GTK_WIDGET(make_widget_row("新 TCP 端口", GTK_WIDGET(console->device_port_entry))));
    console->apply_network_address_button = GTK_BUTTON(gtk_button_new_with_label("写入设备地址"));
    gtk_widget_add_css_class(GTK_WIDGET(console->apply_network_address_button), "destructive-action");
    g_signal_connect(console->apply_network_address_button, "clicked",
                     G_CALLBACK(network_address_clicked), console);
    adw_preferences_group_set_header_suffix(network_group,
                                             GTK_WIDGET(console->apply_network_address_button));
    adw_preferences_page_add(page, network_group);

    g_signal_connect(console->raw_command_dropdown, "notify::selected",
                     G_CALLBACK(protocol_command_selected), console);
    gtk_drop_down_set_selected(console->raw_command_dropdown, 10);
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

static GtkWidget *build_alarm_history_page(LaserConsole *console)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *clear = gtk_button_new_with_label("清空记录");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_log_clicked), console->alarm_history_buffer);
    gtk_box_append(GTK_BOX(toolbar), clear);
    gtk_box_append(GTK_BOX(box), toolbar);
    GtkWidget *view = gtk_text_view_new_with_buffer(console->alarm_history_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);
    console->alarm_history_page = box;
    return box;
}

static GtkWidget *build_extension_page(LaserConsole *console)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "型号扩展");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(page));
    console->extension_page = scroll;

    AdwPreferencesGroup *m7_group = make_group("M7 截止频率");
    adw_preferences_group_set_description(
        m7_group,
        "仅适用于设备返回 M7 能力的型号。读取和写入均通过 0x53 / 0x15 完成。");
    for (guint i = 0; i < G_N_ELEMENTS(console->m7_cutoff_spins); i++) {
        gchar *title = g_strdup_printf("截止频率 %u (ns)", i + 10);
        console->m7_cutoff_spins[i] = make_spin(0, 65535, 1, 0);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(console->m7_cutoff_spins[i]), title);
        adw_preferences_group_add(m7_group, GTK_WIDGET(console->m7_cutoff_spins[i]));
        g_free(title);
    }
    GtkWidget *m7_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    console->m7_cutoff_read_button = GTK_BUTTON(gtk_button_new_with_label("读取"));
    console->m7_cutoff_write_button = GTK_BUTTON(gtk_button_new_with_label("写入"));
    gtk_widget_add_css_class(GTK_WIDGET(console->m7_cutoff_write_button), "suggested-action");
    g_signal_connect(console->m7_cutoff_read_button, "clicked",
                     G_CALLBACK(m7_cutoff_read_clicked), console);
    g_signal_connect(console->m7_cutoff_write_button, "clicked",
                     G_CALLBACK(m7_cutoff_write_clicked), console);
    gtk_box_append(GTK_BOX(m7_actions), GTK_WIDGET(console->m7_cutoff_read_button));
    gtk_box_append(GTK_BOX(m7_actions), GTK_WIDGET(console->m7_cutoff_write_button));
    adw_preferences_group_set_header_suffix(m7_group, m7_actions);
    adw_preferences_page_add(page, m7_group);

    AdwPreferencesGroup *cache_group = make_group("报警缓存");
    adw_preferences_group_set_description(
        cache_group,
        "设备支持报警缓存时显示历史参数摘要；不支持时会在状态区报告设备拒绝。");
    console->alarm_cache_refresh_button = GTK_BUTTON(gtk_button_new_with_label("刷新缓存"));
    g_signal_connect(console->alarm_cache_refresh_button, "clicked",
                     G_CALLBACK(alarm_cache_refresh_clicked), console);
    adw_preferences_group_set_header_suffix(cache_group,
                                             GTK_WIDGET(console->alarm_cache_refresh_button));
    GtkWidget *cache_view = gtk_text_view_new_with_buffer(console->alarm_cache_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(cache_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(cache_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cache_view), GTK_WRAP_WORD_CHAR);
    GtkWidget *cache_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(cache_scroll, -1, 220);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cache_scroll), cache_view);
    adw_preferences_group_add(cache_group, cache_scroll);
    console->extension_status_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(console->extension_status_label), "dim-label");
    adw_preferences_group_add(cache_group, GTK_WIDGET(console->extension_status_label));
    adw_preferences_page_add(page, cache_group);
    return scroll;
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
    g_ptr_array_unref(console->serial_ports);
    g_object_unref(console->serial_port_model);
    g_free(console);
    return FALSE;
}

GtkWidget *laser_console_create(AdwApplication *application)
{
    LaserConsole *console = g_new0(LaserConsole, 1);
    console->client = jpt_client_new(trace_callback, console);
    console->write_buttons = g_ptr_array_new();
    console->serial_ports = g_ptr_array_new_with_free_func(serial_port_free);
    console->serial_port_model = gtk_string_list_new(NULL);

    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(application));
    console->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "MS LEP | JPT Laser");
    gtk_window_set_default_size(GTK_WINDOW(window), 1240, 840);
    gtk_widget_set_size_request(window, 980, 620);
    g_signal_connect(window, "close-request", G_CALLBACK(window_close_request), console);

    GtkWidget *header = adw_header_bar_new();
    AdwWindowTitle *title = ADW_WINDOW_TITLE(adw_window_title_new("MS LEP", "JPT YDFLP-E3-100-M7 调试控制台"));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(title));
    console->emergency_button = GTK_BUTTON(gtk_button_new_with_label("紧急关闭"));
    gtk_widget_add_css_class(GTK_WIDGET(console->emergency_button), "destructive-action");
    g_signal_connect(console->emergency_button, "clicked", G_CALLBACK(emergency_clicked), console);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), GTK_WIDGET(console->emergency_button));
    console->show_sidebar_button = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(console->show_sidebar_button), "sidebar-show-symbolic");
    gtk_widget_set_visible(GTK_WIDGET(console->show_sidebar_button), FALSE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(console->show_sidebar_button), "显示导航");
    g_signal_connect(console->show_sidebar_button, "toggled",
                     G_CALLBACK(show_sidebar_toggled), console);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), GTK_WIDGET(console->show_sidebar_button));

    const gchar *transport_options[] = {"串口", "TCP", NULL};
    console->transport_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(transport_options));
    gtk_drop_down_set_selected(console->transport_dropdown, 0);
    gtk_widget_set_size_request(GTK_WIDGET(console->transport_dropdown), 160, 44);
    g_signal_connect(console->transport_dropdown, "notify::selected",
                     G_CALLBACK(transport_mode_changed), console);

    console->serial_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(console->serial_controls, TRUE);
    console->serial_port_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(console->serial_port_model), NULL));
    gtk_widget_add_css_class(GTK_WIDGET(console->serial_port_dropdown), "card");
    gtk_drop_down_set_enable_search(console->serial_port_dropdown, FALSE);
    gtk_widget_set_size_request(GTK_WIDGET(console->serial_port_dropdown), 360, -1);
    gtk_widget_set_hexpand(GTK_WIDGET(console->serial_port_dropdown), TRUE);
    g_signal_connect(console->serial_port_dropdown, "notify::selected",
                     G_CALLBACK(serial_port_selection_changed), console);
    console->refresh_serial_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_size_request(GTK_WIDGET(console->refresh_serial_button), 44, 44);
    gtk_widget_set_tooltip_text(GTK_WIDGET(console->refresh_serial_button), "刷新串口设备");
    g_signal_connect(console->refresh_serial_button, "clicked",
                     G_CALLBACK(refresh_serial_clicked), console);
    console->manual_serial_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_add_css_class(GTK_WIDGET(console->manual_serial_entry), "card");
    gtk_editable_set_text(GTK_EDITABLE(console->manual_serial_entry), "/dev/ttyUSB0");
    gtk_entry_set_placeholder_text(console->manual_serial_entry, "/dev/ttyUSB0");
    gtk_widget_set_size_request(GTK_WIDGET(console->manual_serial_entry), 240, -1);
    console->select_serial_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-list-symbolic"));
    gtk_widget_set_size_request(GTK_WIDGET(console->select_serial_button), 44, 44);
    gtk_widget_set_tooltip_text(GTK_WIDGET(console->select_serial_button), "选择检测到的串口");
    g_signal_connect(console->select_serial_button, "clicked",
                     G_CALLBACK(select_serial_list_clicked), console);
    gtk_box_append(GTK_BOX(console->serial_controls), GTK_WIDGET(console->serial_port_dropdown));
    gtk_box_append(GTK_BOX(console->serial_controls), GTK_WIDGET(console->refresh_serial_button));
    gtk_box_append(GTK_BOX(console->serial_controls), GTK_WIDGET(console->manual_serial_entry));
    gtk_box_append(GTK_BOX(console->serial_controls), GTK_WIDGET(console->select_serial_button));

    console->endpoint_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_add_css_class(GTK_WIDGET(console->endpoint_entry), "card");
    gtk_editable_set_text(GTK_EDITABLE(console->endpoint_entry), "192.168.0.10:5000");
    gtk_entry_set_placeholder_text(console->endpoint_entry, "主机或 IP 地址:端口");
    gtk_widget_set_size_request(GTK_WIDGET(console->endpoint_entry), 360, 44);
    console->connect_button = GTK_BUTTON(gtk_button_new_with_label("连接"));
    gtk_widget_set_size_request(GTK_WIDGET(console->connect_button), 92, 44);
    gtk_widget_add_css_class(GTK_WIDGET(console->connect_button), "connection-primary");
    g_signal_connect(console->connect_button, "clicked", G_CALLBACK(connect_clicked), console);

    console->disconnect_button = GTK_BUTTON(gtk_button_new_with_label("断开连接"));
    gtk_widget_add_css_class(GTK_WIDGET(console->disconnect_button), "flat");
    g_signal_connect(console->disconnect_button, "clicked", G_CALLBACK(connect_clicked), console);

    console->main_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(console->main_stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(GTK_WIDGET(console->main_stack), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(console->main_stack), TRUE);
    console->connection_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(console->connection_page, TRUE);
    gtk_stack_add_named(console->main_stack, GTK_WIDGET(console->connection_page), "connection");
    build_connection_page(console);

    console->view_stack = ADW_VIEW_STACK(adw_view_stack_new());
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_margin_start(status_bar, 16);
    gtk_widget_set_margin_end(status_bar, 16);
    gtk_widget_set_margin_top(status_bar, 10);
    gtk_widget_set_margin_bottom(status_bar, 8);
    gtk_box_append(GTK_BOX(status_bar),
                   make_status_item("连接", "未连接", &console->connection_indicator,
                                    &console->connection_label));
    gtk_box_append(GTK_BOX(status_bar),
                   make_status_item("设备", "报警状态未知", &console->device_indicator,
                                    &console->device_status_label));
    console->serial_status_label = GTK_LABEL(gtk_label_new("SN：未读取"));
    gtk_widget_add_css_class(GTK_WIDGET(console->serial_status_label), "dim-label");
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->serial_status_label));
    gtk_box_append(GTK_BOX(status_bar),
                   make_status_item("命令", "等待命令", &console->command_indicator,
                                    &console->last_command_label));
    console->last_error_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_hexpand(GTK_WIDGET(console->last_error_label), TRUE);
    gtk_label_set_xalign(console->last_error_label, 1.0f);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->last_error_label));
    gtk_widget_add_css_class(GTK_WIDGET(console->disconnect_button), "flat");
    gtk_widget_set_visible(GTK_WIDGET(console->disconnect_button), FALSE);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(console->disconnect_button));
    gtk_box_append(GTK_BOX(root), status_bar);

    gtk_widget_set_hexpand(GTK_WIDGET(console->view_stack), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(console->view_stack), TRUE);
    GtkWidget *control_page = build_control_page(console);
    GtkWidget *monitor_page = build_monitor_page(console);
    GtkWidget *settings_page = build_settings_page(console);
    console->log_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *log_page = build_log_page(console);
    GtkWidget *protocol_page = build_protocol_page(console);
    console->alarm_history_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *alarm_history_page = build_alarm_history_page(console);
    console->alarm_cache_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *extension_page = build_extension_page(console);
    adw_view_stack_add_titled_with_icon(console->view_stack, control_page,
                                        "control", "控制台", "preferences-system-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, monitor_page,
                                        "monitor", "监控", "applications-utilities-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, settings_page,
                                        "settings", "设备设置", "emblem-system-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, log_page,
                                        "log", "HEX 日志", "utilities-terminal-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, protocol_page,
                                        "protocol", "协议工具", "utilities-terminal-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, alarm_history_page,
                                        "alarm-history", "报警记录", "dialog-warning-symbolic");
    adw_view_stack_add_titled_with_icon(console->view_stack, extension_page,
                                        "extension", "型号扩展", "applications-engineering-symbolic");
    gtk_widget_set_vexpand(GTK_WIDGET(console->view_stack), TRUE);
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(console->view_stack));

    console->navigation_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(console->navigation_list, GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(console->navigation_list, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(console->navigation_list), "navigation-sidebar");
    g_signal_connect(console->navigation_list, "row-activated",
                     G_CALLBACK(navigation_row_activated), console);
    const struct {
        const gchar *name;
        const gchar *title;
        const gchar *icon;
    } navigation[] = {
        {"control", "控制台", "preferences-system-symbolic"},
        {"monitor", "监控", "applications-utilities-symbolic"},
        {"settings", "设备设置", "emblem-system-symbolic"},
        {"log", "HEX 日志", "utilities-terminal-symbolic"},
        {"protocol", "协议工具", "utilities-terminal-symbolic"},
        {"alarm-history", "报警记录", "dialog-warning-symbolic"},
        {"extension", "型号扩展", "applications-engineering-symbolic"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(navigation); i++) {
        gtk_list_box_append(console->navigation_list,
                            make_navigation_row(navigation[i].name, navigation[i].title, navigation[i].icon));
    }
    gtk_list_box_select_row(console->navigation_list,
                            gtk_list_box_get_row_at_index(console->navigation_list, 0));
    GtkWidget *navigation_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(navigation_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(navigation_scroll),
                                  GTK_WIDGET(console->navigation_list));

    console->workbench_split_view = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
    adw_overlay_split_view_set_sidebar_width_fraction(console->workbench_split_view, 0.22);
    adw_overlay_split_view_set_min_sidebar_width(console->workbench_split_view, 190);
    adw_overlay_split_view_set_max_sidebar_width(console->workbench_split_view, 280);
    adw_overlay_split_view_set_sidebar(console->workbench_split_view, navigation_scroll);
    adw_overlay_split_view_set_content(console->workbench_split_view, root);
    g_signal_connect(console->workbench_split_view, "notify::show-sidebar",
                     G_CALLBACK(sidebar_visibility_changed), console);
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbar_view, header);
    gtk_stack_add_named(console->main_stack, GTK_WIDGET(console->workbench_split_view), "workbench");
    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(console->main_stack));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), GTK_WIDGET(toolbar_view));
    AdwBreakpointCondition *condition = adw_breakpoint_condition_new_length(
        ADW_BREAKPOINT_CONDITION_MAX_WIDTH, 760, ADW_LENGTH_UNIT_SP);
    AdwBreakpoint *breakpoint = adw_breakpoint_new(condition);
    GValue orientation = G_VALUE_INIT;
    g_value_init(&orientation, GTK_TYPE_ORIENTATION);
    g_value_set_enum(&orientation, GTK_ORIENTATION_VERTICAL);
    adw_breakpoint_add_setter(breakpoint, G_OBJECT(console->connection_split_box),
                              "orientation", &orientation);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window), breakpoint);
    g_value_unset(&orientation);

    AdwBreakpointCondition *compact_condition = adw_breakpoint_condition_new_length(
        ADW_BREAKPOINT_CONDITION_MAX_WIDTH, 760, ADW_LENGTH_UNIT_SP);
    AdwBreakpoint *compact_breakpoint = adw_breakpoint_new(compact_condition);
    GValue collapsed = G_VALUE_INIT;
    g_value_init(&collapsed, G_TYPE_BOOLEAN);
    g_value_set_boolean(&collapsed, TRUE);
    adw_breakpoint_add_setter(compact_breakpoint, G_OBJECT(console->workbench_split_view),
                              "collapsed", &collapsed);
    GValue sidebar_button_visible = G_VALUE_INIT;
    g_value_init(&sidebar_button_visible, G_TYPE_BOOLEAN);
    g_value_set_boolean(&sidebar_button_visible, TRUE);
    adw_breakpoint_add_setter(compact_breakpoint, G_OBJECT(console->show_sidebar_button),
                              "visible", &sidebar_button_visible);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window), compact_breakpoint);
    g_value_unset(&collapsed);
    g_value_unset(&sidebar_button_visible);
    adw_overlay_split_view_set_show_sidebar(console->workbench_split_view, TRUE);
    set_connection_state(console, "未连接", FALSE, FALSE);
    update_device_status(console);
    set_status_item(console->command_indicator, console->last_command_label,
                    "等待命令", "dim-label");
    update_output_buttons(console);
    return window;
}
