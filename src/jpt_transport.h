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

typedef enum {
    JPT_SERIAL_PARITY_NONE,
    JPT_SERIAL_PARITY_EVEN,
    JPT_SERIAL_PARITY_ODD,
} JptSerialParity;

typedef struct {
    guint baud_rate;
    guint data_bits;
    guint stop_bits;
    JptSerialParity parity;
    gboolean hardware_flow_control;
} JptSerialConfig;

void jpt_transport_init(JptTransport *transport);
gboolean jpt_transport_connect_serial(JptTransport *transport, const gchar *path,
                                      const JptSerialConfig *config, GError **error);
gboolean jpt_transport_connect_tcp(JptTransport *transport, const gchar *host, guint16 port, GError **error);
void jpt_transport_disconnect(JptTransport *transport);
JptExchange *jpt_transport_exchange(JptTransport *transport, guint16 command,
                                    const guint8 *payload, gsize payload_length, GError **error);
void jpt_exchange_free(JptExchange *exchange);

G_END_DECLS
