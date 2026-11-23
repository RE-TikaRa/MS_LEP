#pragma once

#include <gtk/gtk.h>

GtkWidget *alarm_replay_new(void);
void alarm_replay_set(GtkWidget *widget, GArray *records, const gchar *message);
