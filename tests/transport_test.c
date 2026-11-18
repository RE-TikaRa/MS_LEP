#include "jpt_transport.h"

#include <gio/gio.h>
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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/transport/partial-frame-timeout", test_partial_frame_timeout_disconnects_transport);
    return g_test_run();
}
