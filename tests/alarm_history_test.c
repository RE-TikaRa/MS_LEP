#include "jpt_alarm_history.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

static GArray *observe(JptAlarmHistory *history, guint16 state, gint64 timestamp, guint expected)
{
    GError *error = NULL;
    GArray *events = jpt_alarm_history_observe(history, state, timestamp, &error);
    g_assert_no_error(error);
    g_assert_nonnull(events);
    g_assert_cmpuint(events->len, ==, expected);
    return events;
}

static void test_event_history(void)
{
    g_autofree gchar *directory = g_dir_make_tmp("ms-lep-history-XXXXXX", NULL);
    g_assert_nonnull(directory);
    GError *error = NULL;
    JptAlarmHistory *first = jpt_alarm_history_open(directory, "../A", &error);
    g_assert_no_error(error);
    g_autoptr(GArray) initial = observe(first, 0x110, 1700000001, 1);
    g_assert_cmpuint(g_array_index(initial, JptAlarmEvent, 0).raised, ==, 0x110);
    g_array_unref(observe(first, 0x110, 1700000002, 0));
    g_array_unref(observe(first, 0x100, 1700000003, 0));
    g_array_unref(observe(first, 0x110, 1700000004, 1));
    jpt_alarm_history_free(first);

    JptAlarmHistory *again = jpt_alarm_history_open(directory, "../A", &error);
    g_assert_no_error(error);
    g_autoptr(GArray) saved = observe(again, 0x110, 1700000010, 2);
    g_assert_cmpint(g_array_index(saved, JptAlarmEvent, 0).timestamp, ==, 1700000001);
    g_assert_cmpint(g_array_index(saved, JptAlarmEvent, 1).timestamp, ==, 1700000004);
    g_assert_cmpuint(g_array_index(saved, JptAlarmEvent, 1).raised, ==, 0x10);
    JptAlarmHistory *peer = jpt_alarm_history_open(directory, "../A", &error);
    g_assert_no_error(error);
    g_array_unref(observe(peer, 0x114, 1700000020, 3));
    g_autoptr(GArray) late = observe(again, 0x114, 1700000030, 1);
    g_assert_cmpint(g_array_index(late, JptAlarmEvent, 0).timestamp, ==, 1700000020);
    g_array_unref(observe(peer, 0x114, 1700000031, 0));
    jpt_alarm_history_free(peer);
    jpt_alarm_history_free(again);

    JptAlarmHistory *other = jpt_alarm_history_open(directory, "B", &error);
    g_assert_no_error(error);
    g_array_unref(observe(other, 0, 1700000040, 0));
    jpt_alarm_history_free(other);
    g_autofree gchar *file_a = g_build_filename(directory, "sn-..%2FA.tsv", NULL);
    g_autofree gchar *file_b = g_build_filename(directory, "sn-B.tsv", NULL);
    g_assert_cmpint(g_remove(file_a), ==, 0);
    g_assert_cmpint(g_remove(file_b), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_damaged_history(void)
{
    g_autofree gchar *directory = g_dir_make_tmp("ms-lep-history-XXXXXX", NULL);
    g_autofree gchar *path = g_build_filename(directory, "sn-A.tsv", NULL);
    const gchar *bad[] = {"truncated", "2026-01-01T00:00:00Z\tZZZZ\n"};
    for (guint index = 0; index < G_N_ELEMENTS(bad); index++) {
        g_assert_true(g_file_set_contents(path, bad[index], -1, NULL));
        GError *error = NULL;
        JptAlarmHistory *history = jpt_alarm_history_open(directory, "A", &error);
        g_assert_no_error(error);
        g_assert_null(jpt_alarm_history_observe(history, 1, 1700000001, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_clear_error(&error);
        jpt_alarm_history_free(history);
        g_autofree gchar *contents = NULL;
        g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
        g_assert_cmpstr(contents, ==, bad[index]);
    }
    g_remove(path);
    g_rmdir(directory);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/history/edges-reopen-isolation", test_event_history);
    g_test_add_func("/history/corrupt-file", test_damaged_history);
    return g_test_run();
}
