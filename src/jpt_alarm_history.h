#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _JptAlarmHistory JptAlarmHistory;

typedef struct {
    gint64 timestamp;
    guint16 raised;
} JptAlarmEvent;

JptAlarmHistory *jpt_alarm_history_open(const gchar *directory, const gchar *serial, GError **error);
GArray *jpt_alarm_history_observe(JptAlarmHistory *history, guint16 state, gint64 timestamp, GError **error);
void jpt_alarm_history_free(JptAlarmHistory *history);

G_END_DECLS
