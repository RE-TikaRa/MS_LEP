#include "alarm_replay.h"

#include "jpt_device.h"

enum { SERIES_COUNT = 5 };

typedef struct {
    GtkWidget *toggle;
    GtkWidget *plot;
    GtkLabel *range;
    GtkLabel *heading;
    GtkLabel *cells[JPT_ALARM_SAMPLE_COUNT];
    gdouble values[JPT_ALARM_SAMPLE_COUNT];
    gdouble low, high;
} ReplaySeries;

typedef struct {
    GArray *records;
    GtkStack *stack;
    GtkLabel *notice;
    GtkLabel *metadata;
    GtkWidget *selector;
    gulong selection_handler;
    ReplaySeries series[SERIES_COUNT];
} AlarmReplay;

static void replay_free(gpointer data)
{
    AlarmReplay *view = data;
    g_clear_pointer(&view->records, g_array_unref);
    g_free(view);
}

static gchar *record_name(const JptAlarmRecord *record, guint index)
{
    static const gchar *const names[] = {"传感器 1", "传感器 2", "TEC", "无种子源", "一级电流低", "供电电压"};
    if (record->type < G_N_ELEMENTS(names)) {
        return g_strdup_printf("%u · %s", index + 1, names[record->type]);
    }
    return g_strdup_printf("%u · 未知类型 0x%02X", index + 1, record->type);
}

static void update_series(ReplaySeries *series, const gchar *title, const guint16 *raw,
                          JptAlarmExtra kind)
{
    gboolean visible = kind != JPT_EXTRA_ABSENT;
    gtk_widget_set_visible(series->toggle, visible);
    gtk_widget_set_visible(series->plot, visible);
    gtk_widget_set_visible(GTK_WIDGET(series->range), visible);
    gtk_widget_set_visible(GTK_WIDGET(series->heading), visible);
    for (guint index = 0; index < JPT_ALARM_SAMPLE_COUNT; index++) {
        gtk_widget_set_visible(GTK_WIDGET(series->cells[index]), visible);
    }
    if (!visible) {
        return;
    }
    gtk_label_set_text(series->heading, title);
    gtk_check_button_set_label(GTK_CHECK_BUTTON(series->toggle), title);
    guint digits = kind == JPT_EXTRA_VOLTAGE ? 2 :
                   (kind == JPT_EXTRA_TEMPERATURE || kind == JPT_EXTRA_TEC) ? 1 : 0;
    for (guint index = 0; index < JPT_ALARM_SAMPLE_COUNT; index++) {
        gdouble value = jpt_alarm_extra_value(kind, raw[index]);
        series->values[index] = value;
        series->low = index ? MIN(series->low, value) : value;
        series->high = index ? MAX(series->high, value) : value;
        gchar number[32];
        g_snprintf(number, sizeof(number), "%.*f", (gint)digits, value);
        gtk_label_set_text(series->cells[index], number);
    }
    gchar *range = g_strdup_printf("%.*f … %.*f", (gint)digits, series->low, (gint)digits, series->high);
    gtk_label_set_text(series->range, range);
    g_free(range);
    gtk_widget_queue_draw(series->plot);
}

static void show_record(AlarmReplay *view)
{
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(view->selector));
    if (!view->records || selected >= view->records->len) {
        return;
    }
    const JptAlarmRecord *record = &g_array_index(view->records, JptAlarmRecord, selected);
    const gchar *category = NULL;
    if (record->type == JPT_CACHE_NO_SEED) {
        category = record->category == 0 ? "PD" : "背光";
    } else if (record->type == JPT_CACHE_SUPPLY_VOLTAGE) {
        category = record->category == 0 ? "控制板" : "驱动板";
    }
    gchar *metadata = g_strdup_printf("分类 0x%02X%s%s    上电 %u min    出光 %u min",
                                      record->category, category ? " · " : "", category ? category : "",
                                      record->powered_minutes, record->emission_minutes);
    gtk_label_set_text(view->metadata, metadata);
    g_free(metadata);
    update_series(&view->series[0], "功率", record->power, JPT_EXTRA_RAW);
    update_series(&view->series[1], "频率", record->frequency, JPT_EXTRA_RAW);
    update_series(&view->series[2], "脉宽", record->pulse_width, JPT_EXTRA_RAW);
    for (guint channel = 0; channel < 2; channel++) {
        JptAlarmExtra kind = jpt_alarm_extra_kind(record->type, channel);
        const gchar *title = kind == JPT_EXTRA_TEMPERATURE ? "温度 (°C)" :
                             kind == JPT_EXTRA_VOLTAGE ? "电压 (V)" :
                             kind == JPT_EXTRA_TEC ? "TEC" : channel ? "附加原值 2" : "附加原值 1";
        update_series(&view->series[channel + 3], title, record->extra[channel], kind);
    }
}

static void selection_changed(GObject *selector, GParamSpec *spec, gpointer user_data)
{
    (void)selector;
    (void)spec;
    show_record(user_data);
}

static void draw_series(GtkDrawingArea *area, cairo_t *cr, gint width, gint height, gpointer user_data)
{
    ReplaySeries *series = user_data;
    if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(series->toggle)) || width < 6 || height < 6) {
        return;
    }
    GdkRGBA ink;
    gtk_widget_get_color(GTK_WIDGET(area), &ink);
    const gdouble pad = 3.0;
    cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.3 * ink.alpha);
    cairo_move_to(cr, width - pad, pad);
    cairo_line_to(cr, width - pad, height - pad);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    gdk_cairo_set_source_rgba(cr, &ink);
    cairo_set_line_width(cr, 1.75);
    for (guint index = 0; index < JPT_ALARM_SAMPLE_COUNT; index++) {
        gdouble fraction = series->high == series->low ? 0.5 :
                           (series->values[index] - series->low) / (series->high - series->low);
        gdouble horizontal = pad + (width - 2 * pad) * index / (JPT_ALARM_SAMPLE_COUNT - 1);
        gdouble vertical = height - pad - fraction * (height - 2 * pad);
        if (index == 0) {
            cairo_move_to(cr, horizontal, vertical);
        } else {
            cairo_line_to(cr, horizontal, vertical);
        }
    }
    cairo_stroke(cr);
}

static GtkLabel *grid_label(GtkGrid *grid, guint column, guint row, const gchar *text)
{
    GtkLabel *label = GTK_LABEL(gtk_label_new(text));
    gtk_label_set_xalign(label, column ? 1.0f : 0.0f);
    gtk_grid_attach(grid, GTK_WIDGET(label), column, row, 1, 1);
    return label;
}

GtkWidget *alarm_replay_new(void)
{
    AlarmReplay *view = g_new0(AlarmReplay, 1);
    view->stack = GTK_STACK(gtk_stack_new());
    g_object_set_data_full(G_OBJECT(view->stack), "alarm-replay", view, replay_free);
    gtk_stack_set_hhomogeneous(view->stack, FALSE);
    gtk_stack_set_vhomogeneous(view->stack, FALSE);
    view->notice = GTK_LABEL(gtk_label_new("尚未读取报警缓存"));
    gtk_label_set_xalign(view->notice, 0.0f);
    gtk_stack_add_named(view->stack, GTK_WIDGET(view->notice), "notice");
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_stack_add_named(view->stack, page, "records");
    view->selector = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_tooltip_text(view->selector, "选择设备缓存中的报警记录");
    gtk_widget_set_halign(view->selector, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), view->selector);
    view->selection_handler = g_signal_connect(view->selector, "notify::selected",
                                                G_CALLBACK(selection_changed), view);
    view->metadata = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_xalign(view->metadata, 0.0f);
    gtk_label_set_wrap(view->metadata, TRUE);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(view->metadata));

    GtkGrid *plots = GTK_GRID(gtk_grid_new());
    gtk_grid_set_column_spacing(plots, 12);
    gtk_grid_set_row_spacing(plots, 3);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(plots));
    GtkGrid *table = GTK_GRID(gtk_grid_new());
    gtk_grid_set_column_spacing(table, 10);
    gtk_grid_set_row_spacing(table, 2);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(table));
    gtk_box_append(GTK_BOX(page), scroll);
    grid_label(table, 0, 0, "时间序号");
    for (guint index = 0; index < JPT_ALARM_SAMPLE_COUNT; index++) {
        gchar *number = g_strdup_printf("%u", index + 1);
        grid_label(table, 0, index + 1, index + 1 == JPT_ALARM_SAMPLE_COUNT ? "报警时刻" : number);
        g_free(number);
    }
    for (guint channel = 0; channel < SERIES_COUNT; channel++) {
        ReplaySeries *series = &view->series[channel];
        series->toggle = gtk_check_button_new();
        gtk_check_button_set_active(GTK_CHECK_BUTTON(series->toggle), TRUE);
        gtk_grid_attach(plots, series->toggle, 0, channel, 1, 1);
        series->plot = gtk_drawing_area_new();
        gtk_widget_set_hexpand(series->plot, TRUE);
        gtk_widget_set_size_request(series->plot, 72, 34);
        gtk_grid_attach(plots, series->plot, 1, channel, 1, 1);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(series->plot), draw_series, series, NULL);
        g_signal_connect_swapped(series->toggle, "toggled", G_CALLBACK(gtk_widget_queue_draw), series->plot);
        series->range = grid_label(plots, 2, channel, NULL);
        series->heading = grid_label(table, channel + 1, 0, NULL);
        for (guint index = 0; index < JPT_ALARM_SAMPLE_COUNT; index++) {
            series->cells[index] = grid_label(table, channel + 1, index + 1, NULL);
        }
    }
    gtk_stack_set_visible_child_name(view->stack, "notice");
    return GTK_WIDGET(view->stack);
}

void alarm_replay_set(GtkWidget *widget, GArray *records, const gchar *message)
{
    AlarmReplay *view = g_object_get_data(G_OBJECT(widget), "alarm-replay");
    if (records != NULL) {
        g_array_ref(records);
    }
    g_clear_pointer(&view->records, g_array_unref);
    view->records = records;
    g_signal_handler_block(view->selector, view->selection_handler);
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint index = 0; records && index < records->len; index++) {
        gchar *name = record_name(&g_array_index(records, JptAlarmRecord, index), index);
        gtk_string_list_append(model, name);
        g_free(name);
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(view->selector), G_LIST_MODEL(model));
    g_object_unref(model);
    if (records && records->len) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(view->selector), 0);
        show_record(view);
        gtk_stack_set_visible_child_name(view->stack, "records");
    } else {
        gtk_label_set_text(view->notice, message ? message : "设备没有报警缓存记录");
        gtk_stack_set_visible_child_name(view->stack, "notice");
    }
    g_signal_handler_unblock(view->selector, view->selection_handler);
}
