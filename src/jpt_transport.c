#define _GNU_SOURCE

#include "jpt_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static gboolean serial_wait(JptTransport *transport, short events, GError **error)
{
    struct pollfd descriptor = {
        .fd = transport->serial_fd,
        .events = events,
    };

    gint result;
    do {
        result = poll(&descriptor, 1, (gint)transport->timeout_ms);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "JPT serial operation timed out");
        return FALSE;
    }
    if (result < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "JPT serial poll failed: %s",
                    g_strerror(errno));
        return FALSE;
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                    "JPT serial device closed or reported an I/O error");
        return FALSE;
    }

    return TRUE;
}

static gboolean serial_write_all(JptTransport *transport, const guint8 *data, gsize length, GError **error)
{
    gsize offset = 0;
    while (offset < length) {
        if (!serial_wait(transport, POLLOUT, error)) {
            return FALSE;
        }

        ssize_t written = write(transport->serial_fd, data + offset, length - offset);
        if (written < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (written <= 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "JPT serial write failed: %s",
                        g_strerror(errno));
            return FALSE;
        }
        offset += (gsize)written;
    }

    return TRUE;
}

static gboolean serial_read_all(JptTransport *transport, guint8 *data, gsize length, GError **error)
{
    gsize offset = 0;
    while (offset < length) {
        if (!serial_wait(transport, POLLIN, error)) {
            return FALSE;
        }

        ssize_t count = read(transport->serial_fd, data + offset, length - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (count <= 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "JPT serial read failed: %s",
                        g_strerror(errno));
            return FALSE;
        }
        offset += (gsize)count;
    }

    return TRUE;
}

static gboolean transport_write_all(JptTransport *transport, const guint8 *data, gsize length, GError **error)
{
    if (transport->serial_fd >= 0) {
        return serial_write_all(transport, data, length, error);
    }

    GOutputStream *stream = g_io_stream_get_output_stream(G_IO_STREAM(transport->connection));
    gsize written = 0;
    return g_output_stream_write_all(stream, data, length, &written, NULL, error) && written == length;
}

static gboolean transport_read_all(JptTransport *transport, guint8 *data, gsize length, GError **error)
{
    if (transport->serial_fd >= 0) {
        return serial_read_all(transport, data, length, error);
    }

    GInputStream *stream = g_io_stream_get_input_stream(G_IO_STREAM(transport->connection));
    gsize read_count = 0;
    if (!g_input_stream_read_all(stream, data, length, &read_count, NULL, error)) {
        return FALSE;
    }
    if (read_count != length) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                    "JPT TCP connection ended before the frame was complete");
        return FALSE;
    }
    return TRUE;
}

void jpt_transport_init(JptTransport *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->serial_fd = -1;
    transport->timeout_ms = 750;
}

static speed_t serial_baud_to_speed(guint baud_rate, GError **error)
{
    switch (baud_rate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Unsupported serial baud rate: %u", baud_rate);
        return B0;
    }
}

gboolean jpt_transport_connect_serial(JptTransport *transport, const gchar *path,
                                      const JptSerialConfig *config, GError **error)
{
    jpt_transport_disconnect(transport);

    gint fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot open %s: %s", path,
                    g_strerror(errno));
        return FALSE;
    }

    struct termios settings;
    if (tcgetattr(fd, &settings) != 0) {
        gint saved_errno = errno;
        close(fd);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "Cannot read serial settings for %s: %s", path, g_strerror(saved_errno));
        return FALSE;
    }

    speed_t speed = serial_baud_to_speed(config->baud_rate, error);
    if (speed == B0) {
        close(fd);
        return FALSE;
    }

    settings.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL |
                          IXON | IXOFF | IXANY | INPCK | IGNPAR);
    settings.c_oflag &= ~OPOST;
    settings.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    cfsetispeed(&settings, speed);
    cfsetospeed(&settings, speed);
    settings.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
    switch (config->data_bits) {
    case 5:
        settings.c_cflag |= CS5;
        break;
    case 6:
        settings.c_cflag |= CS6;
        break;
    case 7:
        settings.c_cflag |= CS7;
        break;
    case 8:
        settings.c_cflag |= CS8;
        break;
    default:
        close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Unsupported serial data bits: %u", config->data_bits);
        return FALSE;
    }
    if (config->parity == JPT_SERIAL_PARITY_EVEN) {
        settings.c_cflag |= PARENB;
        settings.c_iflag |= INPCK;
    } else if (config->parity == JPT_SERIAL_PARITY_ODD) {
        settings.c_cflag |= PARENB | PARODD;
        settings.c_iflag |= INPCK;
    } else if (config->parity != JPT_SERIAL_PARITY_NONE) {
        close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Unsupported serial parity");
        return FALSE;
    }
    if (config->stop_bits == 2) {
        settings.c_cflag |= CSTOPB;
    } else if (config->stop_bits != 1) {
        close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Unsupported serial stop bits: %u", config->stop_bits);
        return FALSE;
    }
    settings.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
    if (config->hardware_flow_control) {
        settings.c_cflag |= CRTSCTS;
    } else {
        settings.c_cflag &= ~CRTSCTS;
    }
#else
    if (config->hardware_flow_control) {
        close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Hardware serial flow control is not supported");
        return FALSE;
    }
#endif
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &settings) != 0) {
        gint saved_errno = errno;
        close(fd);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "Cannot configure serial port %s: %s", path, g_strerror(saved_errno));
        return FALSE;
    }

    transport->serial_fd = fd;
    return TRUE;
}

gboolean jpt_transport_connect_tcp(JptTransport *transport, const gchar *host, guint16 port, GError **error)
{
    jpt_transport_disconnect(transport);

    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, MAX(1, transport->timeout_ms / 1000));
    GSocketConnection *connection = g_socket_client_connect_to_host(client, host, port, NULL, error);
    g_object_unref(client);
    if (connection == NULL) {
        return FALSE;
    }

    GSocket *socket = g_socket_connection_get_socket(connection);
    g_socket_set_timeout(socket, MAX(1u, transport->timeout_ms / 1000));
    transport->connection = connection;
    return TRUE;
}

void jpt_transport_disconnect(JptTransport *transport)
{
    if (transport->serial_fd >= 0) {
        close(transport->serial_fd);
        transport->serial_fd = -1;
    }
    if (transport->connection != NULL) {
        g_io_stream_close(G_IO_STREAM(transport->connection), NULL, NULL);
        g_clear_object(&transport->connection);
    }
}

JptExchange *jpt_transport_exchange(JptTransport *transport, guint16 command,
                                    const guint8 *payload, gsize payload_length, GError **error)
{
    if (transport->serial_fd < 0 && transport->connection == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "JPT device is disconnected");
        return NULL;
    }

    GBytes *transmitted = jpt_frame_encode(command, payload, payload_length, error);
    if (transmitted == NULL) {
        return NULL;
    }

    gsize transmit_length = 0;
    const guint8 *transmit_data = g_bytes_get_data(transmitted, &transmit_length);
    if (!transport_write_all(transport, transmit_data, transmit_length, error)) {
        g_bytes_unref(transmitted);
        return NULL;
    }

    guint8 first = 0;
    if (!transport_read_all(transport, &first, 1, error)) {
        g_bytes_unref(transmitted);
        return NULL;
    }
    if (first == JPT_DEVICE_ERROR) {
        guint8 *received_data = g_malloc(1);
        received_data[0] = first;
        JptExchange *exchange = g_new0(JptExchange, 1);
        exchange->transmitted = transmitted;
        exchange->received = g_bytes_new_take(received_data, 1);
        return exchange;
    }
    if (first != JPT_FRAME_START) {
        g_bytes_unref(transmitted);
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_FRAME,
                    "Unexpected JPT response marker 0x%02X", first);
        return NULL;
    }

    guint8 header[3];
    if (!transport_read_all(transport, header, sizeof(header), error)) {
        g_bytes_unref(transmitted);
        return NULL;
    }
    guint response_length = header[2];
    guint8 *received_data = g_malloc(response_length + 5);
    received_data[0] = first;
    memcpy(received_data + 1, header, sizeof(header));
    if (!transport_read_all(transport, received_data + 4, response_length + 1, error)) {
        g_bytes_unref(transmitted);
        g_free(received_data);
        return NULL;
    }
    if (received_data[response_length + 4] != JPT_FRAME_END) {
        g_bytes_unref(transmitted);
        g_free(received_data);
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_FRAME,
                    "JPT response did not end with 0x2A");
        return NULL;
    }

    guint16 response_command = (guint16)received_data[1] | ((guint16)received_data[2] << 8);
    JptExchange *exchange = g_new0(JptExchange, 1);
    exchange->frame = jpt_frame_new(response_command, received_data + 4, response_length);
    exchange->transmitted = transmitted;
    exchange->received = g_bytes_new_take(received_data, response_length + 5);
    return exchange;
}

void jpt_exchange_free(JptExchange *exchange)
{
    if (exchange == NULL) {
        return;
    }

    g_clear_pointer(&exchange->frame, jpt_frame_free);
    g_clear_pointer(&exchange->transmitted, g_bytes_unref);
    g_clear_pointer(&exchange->received, g_bytes_unref);
    g_free(exchange);
}
