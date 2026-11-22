#define _POSIX_C_SOURCE 200809L

#include "jpt_transport.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_partial_frame_timeout_disconnects_transport(void)
{
    gint sockets[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);

    GError *error = NULL;
    GSocket *socket = g_socket_new_from_fd(sockets[0], &error);
    g_assert_no_error(error);
    GSocketConnection *connection = g_socket_connection_factory_create_connection(socket);
    g_object_unref(socket);

    JptTransport transport;
    jpt_transport_init(&transport);
    transport.connection = connection;
    g_socket_set_timeout(g_socket_connection_get_socket(connection), 1);

    const guint8 partial_response[] = {JPT_FRAME_START, 0x38};
    g_assert_cmpint(write(sockets[1], partial_response, sizeof(partial_response)), ==,
                    sizeof(partial_response));

    JptExchange *exchange = jpt_transport_exchange(&transport, 0x38, NULL, 0, &error);
    g_assert_null(exchange);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_assert_false(jpt_transport_is_connected(&transport));

    g_clear_error(&error);
    jpt_transport_disconnect(&transport);
    close(sockets[1]);
}

static void test_serial_aliases(void)
{
    g_autofree gchar *directory = g_dir_make_tmp("ms-lep-ports-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_autofree gchar *serial = g_build_filename(directory, "serial", NULL);
    g_autofree gchar *by_id = g_build_filename(serial, "by-id", NULL);
    g_autofree gchar *alias = g_build_filename(by_id, "fixture-adapter", NULL);
    g_autofree gchar *port = g_build_filename(directory, "ttyACM0", NULL);
    g_autofree gchar *regular = g_build_filename(directory, "ttyUSB1", NULL);
    g_assert_cmpint(g_mkdir_with_parents(by_id, 0700), ==, 0);
    g_assert_cmpint(symlink("/dev/null", port), ==, 0);
    g_assert_cmpint(symlink("../../ttyACM0", alias), ==, 0);
    g_assert_true(g_file_set_contents(regular, "", 0, NULL));
    GPtrArray *ports = jpt_serial_list_ports(directory);
    g_assert_cmpuint(ports->len, ==, 1);
    JptSerialPort *found = g_ptr_array_index(ports, 0);
    g_assert_cmpstr(found->name, ==, "fixture-adapter");
    g_assert_cmpstr(found->path, ==, "/dev/null");
    g_ptr_array_unref(ports);
    g_remove(regular);
    g_remove(alias);
    g_remove(port);
    g_rmdir(by_id);
    g_rmdir(serial);
    g_rmdir(directory);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/transport/partial-frame-timeout", test_partial_frame_timeout_disconnects_transport);
    g_test_add_func("/transport/serial/aliases", test_serial_aliases);
    return g_test_run();
}
