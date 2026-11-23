#define _POSIX_C_SOURCE 200809L

#include "jpt_alarm_history.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

struct _JptAlarmHistory {
    FILE *file;
    off_t offset;
    guint16 previous;
};

enum { ROW_SIZE = sizeof("2000-01-01T00:00:00Z\tFFFF\n") - 1 };

static void file_error(GError **error, const gchar *operation)
{
    gint saved = errno;
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved), "%s: %s", operation, g_strerror(saved));
}

JptAlarmHistory *jpt_alarm_history_open(const gchar *directory, const gchar *serial, GError **error)
{
    if (!serial || !*serial || !g_utf8_validate(serial, -1, NULL)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Missing or invalid device serial number");
        return NULL;
    }
    if (g_mkdir_with_parents(directory, 0700) < 0) {
        file_error(error, "Create alarm directory");
        return NULL;
    }
    g_autofree gchar *escaped = g_uri_escape_string(serial, NULL, FALSE);
    g_autofree gchar *name = g_strdup_printf("sn-%s.tsv", escaped);
    g_autofree gchar *path = g_build_filename(directory, name, NULL);
    gint fd = open(path, O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) {
        file_error(error, "Open alarm history");
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) < 0) {
        file_error(error, "Inspect alarm history");
        close(fd);
        return NULL;
    }
    if (!S_ISREG(info.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE, "Alarm history is not a regular file");
        close(fd);
        return NULL;
    }
    FILE *file = fdopen(fd, "a+");
    if (!file) {
        file_error(error, "Open alarm history stream");
        close(fd);
        return NULL;
    }
    JptAlarmHistory *history = g_new0(JptAlarmHistory, 1);
    history->file = file;
    return history;
}

static void add_event(GArray *events, gint64 timestamp, guint16 previous, guint16 current)
{
    JptAlarmEvent event = {.timestamp = timestamp, .raised = current & ~previous};
    if (event.raised) {
        g_array_append_val(events, event);
    }
}

static gboolean read_row(gchar *row, gint64 *timestamp, guint16 *state)
{
    if (row[19] != 'Z' || row[20] != '\t' || row[25] != '\n') {
        return FALSE;
    }
    row[20] = '\0';
    g_autoptr(GDateTime) time = g_date_time_new_from_iso8601(row, NULL);
    if (!time) {
        return FALSE;
    }
    guint16 word = 0;
    for (guint index = 21; index < 25; index++) {
        gint digit = g_ascii_xdigit_value(row[index]);
        if (digit < 0) {
            return FALSE;
        }
        word = (word << 4) | digit;
    }
    *state = word;
    *timestamp = g_date_time_to_unix(time);
    return TRUE;
}

GArray *jpt_alarm_history_observe(JptAlarmHistory *history, guint16 state, gint64 timestamp, GError **error)
{
    gint fd = fileno(history->file);
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        file_error(error, "Lock alarm history");
        return NULL;
    }
    GArray *events = g_array_new(FALSE, FALSE, sizeof(JptAlarmEvent));
    off_t offset = history->offset;
    guint16 previous = history->previous;
    gboolean ok = FALSE;
    struct stat info;
    if (fstat(fd, &info) < 0 || fseeko(history->file, offset, SEEK_SET) < 0) {
        file_error(error, "Seek alarm history");
        goto done;
    }
    if (info.st_size < offset || info.st_size % ROW_SIZE) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Incomplete alarm history record");
        goto done;
    }
    while (offset < info.st_size) {
        gchar row[ROW_SIZE];
        if (fread(row, 1, ROW_SIZE, history->file) != ROW_SIZE) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot read alarm history record");
            goto done;
        }
        guint16 stored;
        gint64 when;
        if (!read_row(row, &when, &stored)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Invalid alarm history at byte %" G_GINT64_FORMAT, (gint64)offset);
            goto done;
        }
        add_event(events, when, previous, stored);
        previous = stored;
        offset += ROW_SIZE;
    }
    if (state != previous) {
        g_autoptr(GDateTime) time = g_date_time_new_from_unix_utc(timestamp);
        if (!time) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid alarm timestamp");
            goto done;
        }
        g_autofree gchar *utc = g_date_time_format(time, "%FT%TZ");
        if (fseeko(history->file, 0, SEEK_END) < 0 ||
            fprintf(history->file, "%s\t%04X\n", utc, (guint)state) != ROW_SIZE ||
            fflush(history->file) != 0) {
            file_error(error, "Append alarm history");
            goto done;
        }
        add_event(events, timestamp, previous, state);
        offset += ROW_SIZE;
    }
    history->previous = state;
    history->offset = offset;
    ok = TRUE;
done:
    flock(fd, LOCK_UN);
    if (!ok) {
        g_array_unref(events);
        return NULL;
    }
    return events;
}

void jpt_alarm_history_free(JptAlarmHistory *history)
{
    fclose(history->file);
    g_free(history);
}
