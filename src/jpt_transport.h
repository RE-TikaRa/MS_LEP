#pragma once

#include "jpt_protocol.h"

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
    JptFrame *frame;
    GBytes *transmitted;
    GBytes *received;
} JptExchange;

typedef struct {
    gint serial_fd;
    GSocketConnection *connection;
    guint timeout_ms;
} JptTransport;

void jpt_transport_init(JptTransport *transport);
gboolean jpt_transport_connect_serial(JptTransport *transport, const gchar *path, GError **error);
gboolean jpt_transport_connect_tcp(JptTransport *transport, const gchar *host, guint16 port, GError **error);
void jpt_transport_disconnect(JptTransport *transport);
JptExchange *jpt_transport_exchange(JptTransport *transport, guint16 command,
                                    const guint8 *payload, gsize payload_length, GError **error);
void jpt_exchange_free(JptExchange *exchange);

G_END_DECLS
