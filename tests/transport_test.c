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

static void test_socket_alarm_reply(void)
{
    gint sockets[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    GSocket *socket = g_socket_new_from_fd(sockets[0], NULL);
    JptTransport transport;
    jpt_transport_init(&transport);
    transport.connection = g_socket_connection_factory_create_connection(socket);
    g_socket_set_timeout(socket, 1);
    g_object_unref(socket);

    guint8 wire[331] = {0x24, 0x93, 0, 0x45, 1, 3};
    wire[6] = 0x24;
    wire[74] = 0x2a;
    wire[330] = 0x2a;
    const guint8 status[] = {0x24, 0x38, 0, 2, 1, 0, 0x2a};
    const guint8 request[] = {0, 0};
    g_assert_cmpint(write(sockets[1], wire, sizeof(wire)), ==, sizeof(wire));
    g_assert_cmpint(write(sockets[1], status, sizeof(status)), ==, sizeof(status));
    GError *error = NULL;
    JptExchange *reply = jpt_transport_exchange(&transport, JPT_CMD_READ_ALARM_CACHE, request, 2, &error);
    g_assert_no_error(error);
    g_assert_cmpmem(g_bytes_get_data(reply->received, NULL), g_bytes_get_size(reply->received), wire, sizeof(wire));
    const guint8 cache_request[] = {0x24, 0x93, 0, 2, 0, 0, 0x2a};
    g_assert_cmpmem(g_bytes_get_data(reply->transmitted, NULL), g_bytes_get_size(reply->transmitted),
                    cache_request, sizeof(cache_request));
    g_assert_cmpuint(g_bytes_get_size(reply->frame->payload), ==, 325);
    jpt_exchange_free(reply);
    reply = jpt_transport_exchange(&transport, 0x38, request, 2, &error);
    g_assert_no_error(error);
    g_assert_cmpmem(g_bytes_get_data(reply->received, NULL), g_bytes_get_size(reply->received), status, sizeof(status));
    jpt_exchange_free(reply);
    g_assert_cmpint(write(sockets[1], wire, 4), ==, 4);
    g_assert_cmpint(shutdown(sockets[1], SHUT_WR), ==, 0);
    g_assert_null(jpt_transport_exchange(&transport, JPT_CMD_READ_ALARM_CACHE, request, 2, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_assert_false(jpt_transport_is_connected(&transport));
    g_clear_error(&error);
    close(sockets[1]);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/transport/partial-frame-timeout", test_partial_frame_timeout_disconnects_transport);
    g_test_add_func("/transport/serial/aliases", test_serial_aliases);
    g_test_add_func("/transport/socket/extended-alarm-reply", test_socket_alarm_reply);
    return g_test_run();
}
