#pragma once

#include "jpt_device.h"
#include "jpt_transport.h"

G_BEGIN_DECLS

typedef struct _JptClient JptClient;

typedef enum {
    JPT_CLIENT_CONNECTION,
    JPT_CLIENT_DISCONNECTED,
    JPT_CLIENT_TELEMETRY,
    JPT_CLIENT_EXCHANGE
} JptClientResultType;

typedef struct {
    JptClientResultType type;
    gboolean connected;
    JptTelemetry telemetry;
    JptExchange *exchange;
    GError *error;
} JptClientResult;

typedef void (*JptClientCallback)(const JptClientResult *result, gpointer user_data);
typedef void (*JptTraceCallback)(guint16 command, GBytes *transmitted, GBytes *received,
                                 const GError *error, gpointer user_data);

JptClient *jpt_client_new(JptTraceCallback trace_callback, gpointer trace_data);
void jpt_client_connect_serial_async(JptClient *client, const gchar *path,
                                     const JptSerialConfig *config,
                                     JptClientCallback callback, gpointer user_data);
void jpt_client_connect_tcp_async(JptClient *client, const gchar *host, guint16 port,
                                  JptClientCallback callback, gpointer user_data);
void jpt_client_disconnect_async(JptClient *client, JptClientCallback callback, gpointer user_data);
void jpt_client_read_initial_async(JptClient *client, JptClientCallback callback, gpointer user_data);
void jpt_client_poll_async(JptClient *client, JptClientCallback callback, gpointer user_data);
void jpt_client_request_async(JptClient *client, guint16 command, GBytes *payload,
                              JptClientCallback callback, gpointer user_data);
void jpt_client_write_u16_async(JptClient *client, guint16 command, guint16 value,
                                JptClientCallback callback, gpointer user_data);
void jpt_client_free(JptClient *client);

G_END_DECLS
