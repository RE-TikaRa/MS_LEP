#define _XOPEN_SOURCE 700

#include "jpt_transport.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void port_free(gpointer data)
{
    JptSerialPort *port = data;
    free(port->path);
    g_free(port->name);
    g_free(port);
}

static gint compare_ports(gconstpointer left, gconstpointer right)
{
    const JptSerialPort *first = *(JptSerialPort *const *)left;
    const JptSerialPort *second = *(JptSerialPort *const *)right;
    return strcmp(first->name, second->name);
}

GPtrArray *jpt_serial_list_ports(const gchar *device_directory)
{
    GPtrArray *ports = g_ptr_array_new_with_free_func(port_free);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    const gchar *directories[] = {"serial/by-id", "serial/by-path", ""};
    for (guint index = 0; index < G_N_ELEMENTS(directories); index++) {
        gchar *directory = g_build_filename(device_directory, directories[index], NULL);
        GDir *entries = g_dir_open(directory, 0, NULL);
        if (entries != NULL) {
            const gchar *name;
            while ((name = g_dir_read_name(entries)) != NULL) {
                if (!directories[index][0] && !g_str_has_prefix(name, "ttyUSB") &&
                    !g_str_has_prefix(name, "ttyACM") && !g_str_has_prefix(name, "ttyS") &&
                    !g_str_has_prefix(name, "rfcomm")) {
                    continue;
                }
                gchar *path = g_build_filename(directory, name, NULL);
                gchar *target = realpath(path, NULL);
                g_free(path);
                struct stat info;
                if (target == NULL || stat(target, &info) || !S_ISCHR(info.st_mode) ||
                    g_hash_table_contains(seen, target)) {
                    free(target);
                    continue;
                }
                JptSerialPort *port = g_new(JptSerialPort, 1);
                port->name = g_filename_display_name(name);
                port->path = target;
                g_hash_table_add(seen, target);
                g_ptr_array_add(ports, port);
            }
            g_dir_close(entries);
        }
        g_free(directory);
    }
    g_hash_table_unref(seen);
    g_ptr_array_sort(ports, compare_ports);
    return ports;
}
