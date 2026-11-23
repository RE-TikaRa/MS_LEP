#include "alarm_replay.h"
#include "jpt_device.h"

static GtkWidget *find_widget(GtkWidget *root, GType type, const gchar *label)
{
    if (!gtk_widget_get_visible(root)) {
        return NULL;
    }
    if (g_type_is_a(G_OBJECT_TYPE(root), type) &&
        (!label || g_strcmp0(gtk_label_get_text(GTK_LABEL(root)), label) == 0)) {
        return root;
    }
    if (!GTK_IS_CONTAINER(root)) {
        return NULL;
    }
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *child = children; child && !found; child = child->next) {
        found = find_widget(child->data, type, label);
    }
    g_list_free(children);
    return found;
}

static void test_switch_records(void)
{
    GtkWidget *view = g_object_ref_sink(alarm_replay_new());
    GtkStack *stack = GTK_STACK(view);
    GArray *records = g_array_new(FALSE, FALSE, sizeof(JptAlarmRecord));
    JptAlarmRecord sensor = {.type = JPT_CACHE_SENSOR_1, .powered_minutes = 77, .emission_minutes = 5,
                             .extra = {{185}}};
    JptAlarmRecord tec = {.type = JPT_CACHE_TEC, .extra = {{285}, {425}}};
    JptAlarmRecord unknown = {.type = 0xfe, .category = 0x37, .extra = {{101}, {202}}};
    g_array_append_val(records, sensor);
    g_array_append_val(records, tec);
    g_array_append_val(records, unknown);

    alarm_replay_set(view, records, NULL);
    g_array_unref(records);
    g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "records");
    GtkWidget *page = gtk_stack_get_visible_child(stack);
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "报警时刻"));
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "-1.5"));
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "分类 0x00    上电 77 min    出光 5 min"));

    GtkComboBox *selector = GTK_COMBO_BOX(find_widget(page, GTK_TYPE_COMBO_BOX, NULL));
    g_assert_nonnull(selector);
    gtk_combo_box_set_active(selector, 1);
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "28.5"));
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "22.5"));
    g_assert_null(find_widget(page, GTK_TYPE_LABEL, "-1.5"));
    gtk_combo_box_set_active(selector, 2);
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "附加原值 1"));
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "附加原值 2"));
    g_assert_nonnull(find_widget(page, GTK_TYPE_LABEL, "202"));
    g_assert_null(find_widget(page, GTK_TYPE_LABEL, "温度 (°C)"));

    alarm_replay_set(view, NULL, "读取失败");
    g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "notice");
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(gtk_stack_get_visible_child(stack))), ==, "读取失败");
    records = g_array_new(FALSE, FALSE, sizeof(JptAlarmRecord));
    alarm_replay_set(view, records, NULL);
    g_array_unref(records);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(gtk_stack_get_visible_child(stack))), ==, "设备没有报警缓存记录");
    g_object_unref(view);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check(&argc, &argv)) {
        return 77;
    }
    g_test_add_func("/ui/alarm-replay/selection", test_switch_records);
    return g_test_run();
}
