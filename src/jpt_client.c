#include "jpt_client.h"

typedef enum {
    JPT_JOB_CONNECT_SERIAL,
    JPT_JOB_CONNECT_TCP,
    JPT_JOB_DISCONNECT,
    JPT_JOB_INITIAL_READ,
    JPT_JOB_POLL,
    JPT_JOB_REQUEST,
    JPT_JOB_STOP
} JptJobType;

typedef struct {
    JptJobType type;
    gchar *address;
    guint16 port;
    JptSerialConfig serial_config;
    guint16 command;
    GBytes *payload;
    JptClientCallback callback;
    gpointer user_data;
} JptJob;

typedef struct {
    JptClientResult result;
    JptClientCallback callback;
    gpointer user_data;
} JptCompletion;

typedef struct {
    guint16 command;
    GBytes *transmitted;
    GBytes *received;
    GError *error;
    JptTraceCallback callback;
    gpointer user_data;
} JptTraceEvent;

struct _JptClient {
    GAsyncQueue *queue;
    GMainContext *context;
    GThread *worker;
    JptTransport transport;
    JptTraceCallback trace_callback;
    gpointer trace_data;
    gint stopping;
};

static gboolean dispatch_completion(gpointer data)
{
    JptCompletion *completion = data;
    if (completion->callback != NULL) {
        completion->callback(&completion->result, completion->user_data);
    }
    return G_SOURCE_REMOVE;
}

static void free_completion(gpointer data)
{
    JptCompletion *completion = data;
    g_clear_pointer(&completion->result.exchange, jpt_exchange_free);
    g_clear_error(&completion->result.error);
    g_free(completion);
}

static gboolean dispatch_trace(gpointer data)
{
    JptTraceEvent *event = data;
    event->callback(event->command, event->transmitted, event->received, event->error, event->user_data);
    return G_SOURCE_REMOVE;
}

static void free_trace(gpointer data)
{
    JptTraceEvent *event = data;
    g_clear_pointer(&event->transmitted, g_bytes_unref);
    g_clear_pointer(&event->received, g_bytes_unref);
    g_clear_error(&event->error);
    g_free(event);
}

static void schedule_trace(JptClient *client, guint16 command, JptExchange *exchange, const GError *error)
{
    if (client->trace_callback == NULL) {
        return;
    }

    JptTraceEvent *event = g_new0(JptTraceEvent, 1);
    event->command = command;
    event->callback = client->trace_callback;
    event->user_data = client->trace_data;
    if (exchange != NULL) {
        event->transmitted = g_bytes_ref(exchange->transmitted);
        event->received = g_bytes_ref(exchange->received);
    }
    if (error != NULL) {
        event->error = g_error_copy(error);
    }
    g_main_context_invoke_full(client->context, G_PRIORITY_DEFAULT, dispatch_trace, event, free_trace);
}

static JptExchange *client_exchange(JptClient *client, guint16 command,
                                    const guint8 *payload, gsize payload_length, GError **error)
{
    JptExchange *exchange = jpt_transport_exchange(&client->transport, command,
                                                   payload, payload_length, error);
    schedule_trace(client, command, exchange, error == NULL ? NULL : *error);
    if (exchange == NULL) {
        return NULL;
    }

    gsize received_length = 0;
    const guint8 *received = g_bytes_get_data(exchange->received, &received_length);
    if (received_length == 1 && received[0] == JPT_DEVICE_ERROR) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_DEVICE_REJECTED,
                    "JPT rejected command 0x%04X with error 0x45", command);
        jpt_exchange_free(exchange);
        return NULL;
    }
    if (exchange->frame == NULL || exchange->frame->command != command) {
        guint16 actual = exchange->frame == NULL ? 0 : exchange->frame->command;
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_UNEXPECTED_COMMAND,
                    "Expected JPT command 0x%04X but received 0x%04X", command, actual);
        jpt_exchange_free(exchange);
        return NULL;
    }

    return exchange;
}

static gboolean client_read_u16(JptClient *client, guint16 command, guint16 *value, GError **error)
{
    JptExchange *exchange = client_exchange(client, command, (const guint8[]){0, 0}, 2, error);
    if (exchange == NULL) {
        return FALSE;
    }
    gboolean ok = jpt_device_decode_u16(exchange->frame, value, error);
    jpt_exchange_free(exchange);
    return ok;
}

static gboolean client_read_telemetry(JptClient *client, JptTelemetry *telemetry, gboolean initial, GError **error)
{
    if (initial) {
        JptExchange *serial = client_exchange(client, JPT_CMD_SERIAL_NUMBER, (const guint8[]){0, 0}, 2, error);
        if (serial == NULL) {
            return FALSE;
        }
        gboolean ok = jpt_device_decode_serial(serial->frame, telemetry->serial_number,
                                                sizeof(telemetry->serial_number), error);
        jpt_exchange_free(serial);
        if (!ok ||
            !client_read_u16(client, JPT_CMD_READ_DEFAULT_SIMMER, &telemetry->default_simmer, error) ||
            !client_read_u16(client, JPT_CMD_READ_DEFAULT_PULSE_WIDTH, &telemetry->default_pulse_width, error) ||
            !client_read_u16(client, JPT_CMD_READ_LOCK_FREQUENCY, &telemetry->lock_frequency, error) ||
            !client_read_u16(client, JPT_CMD_READ_PARAMETER_SELECT, &telemetry->parameter_select, error) ||
            !client_read_u16(client, JPT_CMD_READ_CONTROL_MODE, &telemetry->control_mode, error) ||
            !client_read_u16(client, JPT_CMD_READ_LOCK_POWER, &telemetry->lock_power, error) ||
            !client_read_u16(client, JPT_CMD_READ_RED_LIGHT_POWER, &telemetry->red_light_power, error) ||
            !client_read_u16(client, JPT_CMD_READ_LOCK_PARAMETER, &telemetry->lock_options, error)) {
            return FALSE;
        }
    }

    if (!client_read_u16(client, JPT_CMD_STATUS, &telemetry->status, error) ||
        !client_read_u16(client, JPT_CMD_REAL_POWER, &telemetry->real_power_percent, error) ||
        !client_read_u16(client, JPT_CMD_REAL_PULSE_WIDTH, &telemetry->real_pulse_width, error) ||
        !client_read_u16(client, JPT_CMD_REAL_FREQUENCY, &telemetry->real_frequency, error)) {
        return FALSE;
    }

    JptExchange *monitoring = client_exchange(client, JPT_CMD_MONITORING, (const guint8[]){0, 0}, 2, error);
    if (monitoring == NULL) {
        return FALSE;
    }
    gboolean ok = jpt_device_decode_monitoring(monitoring->frame, telemetry, error);
    jpt_exchange_free(monitoring);
    if (!ok || !client_read_u16(client, JPT_CMD_ALARM_STATE, &telemetry->alarm_state, error)) {
        return FALSE;
    }

    JptExchange *counts = client_exchange(client, JPT_CMD_ALARM_COUNTS, (const guint8[]){0, 0}, 2, error);
    if (counts == NULL) {
        return FALSE;
    }
    ok = jpt_device_decode_alarm_counts(counts->frame, telemetry, error);
    jpt_exchange_free(counts);
    return ok;
}

static void complete_job(JptClient *client, const JptJob *job, JptClientResult *result)
{
    if (job->callback == NULL) {
        g_clear_pointer(&result->exchange, jpt_exchange_free);
        g_clear_error(&result->error);
        return;
    }

    JptCompletion *completion = g_new0(JptCompletion, 1);
    completion->result = *result;
    completion->callback = job->callback;
    completion->user_data = job->user_data;
    memset(result, 0, sizeof(*result));
    g_main_context_invoke_full(client->context, G_PRIORITY_DEFAULT,
                               dispatch_completion, completion, free_completion);
}

static void free_job(JptJob *job)
{
    g_free(job->address);
    g_clear_pointer(&job->payload, g_bytes_unref);
    g_free(job);
}

static gpointer worker_main(gpointer data)
{
    JptClient *client = data;
    for (;;) {
        JptJob *job = g_async_queue_pop(client->queue);
        if (job->type == JPT_JOB_STOP) {
            free_job(job);
            break;
        }

        JptClientResult result = {0};
        switch (job->type) {
        case JPT_JOB_CONNECT_SERIAL:
            result.type = JPT_CLIENT_CONNECTION;
            result.connected = jpt_transport_connect_serial(&client->transport, job->address,
                                                            &job->serial_config, &result.error);
            break;
        case JPT_JOB_CONNECT_TCP:
            result.type = JPT_CLIENT_CONNECTION;
            result.connected = jpt_transport_connect_tcp(&client->transport, job->address, job->port,
                                                         &result.error);
            break;
        case JPT_JOB_DISCONNECT:
            jpt_transport_disconnect(&client->transport);
            result.type = JPT_CLIENT_DISCONNECTED;
            break;
        case JPT_JOB_INITIAL_READ:
        case JPT_JOB_POLL:
            result.type = JPT_CLIENT_TELEMETRY;
            if (!client_read_telemetry(client, &result.telemetry,
                                       job->type == JPT_JOB_INITIAL_READ, &result.error)) {
                result.type = JPT_CLIENT_TELEMETRY;
            }
            break;
        case JPT_JOB_REQUEST: {
            result.type = JPT_CLIENT_EXCHANGE;
            gsize payload_length = 0;
            const guint8 *payload = g_bytes_get_data(job->payload, &payload_length);
            result.exchange = client_exchange(client, job->command, payload, payload_length, &result.error);
            break;
        }
        case JPT_JOB_STOP:
            break;
        }

        complete_job(client, job, &result);
        free_job(job);
    }
    return NULL;
}

static void enqueue_job(JptClient *client, JptJob *job)
{
    if (g_atomic_int_get(&client->stopping)) {
        JptClientResult result = {
            .type = JPT_CLIENT_DISCONNECTED,
        };
        result.error = g_error_new(G_IO_ERROR, G_IO_ERROR_CLOSED, "JPT client is shutting down");
        complete_job(client, job, &result);
        free_job(job);
        return;
    }
    g_async_queue_push(client->queue, job);
}

static JptJob *new_job(JptJobType type, JptClientCallback callback, gpointer user_data)
{
    JptJob *job = g_new0(JptJob, 1);
    job->type = type;
    job->callback = callback;
    job->user_data = user_data;
    return job;
}

JptClient *jpt_client_new(JptTraceCallback trace_callback, gpointer trace_data)
{
    JptClient *client = g_new0(JptClient, 1);
    client->queue = g_async_queue_new();
    client->context = g_main_context_ref_thread_default();
    client->trace_callback = trace_callback;
    client->trace_data = trace_data;
    jpt_transport_init(&client->transport);
    client->worker = g_thread_new("jpt-io", worker_main, client);
    return client;
}

void jpt_client_connect_serial_async(JptClient *client, const gchar *path,
                                     const JptSerialConfig *config,
                                     JptClientCallback callback, gpointer user_data)
{
    JptJob *job = new_job(JPT_JOB_CONNECT_SERIAL, callback, user_data);
    job->address = g_strdup(path);
    job->serial_config = *config;
    enqueue_job(client, job);
}

void jpt_client_connect_tcp_async(JptClient *client, const gchar *host, guint16 port,
                                  JptClientCallback callback, gpointer user_data)
{
    JptJob *job = new_job(JPT_JOB_CONNECT_TCP, callback, user_data);
    job->address = g_strdup(host);
    job->port = port;
    enqueue_job(client, job);
}

void jpt_client_disconnect_async(JptClient *client, JptClientCallback callback, gpointer user_data)
{
    enqueue_job(client, new_job(JPT_JOB_DISCONNECT, callback, user_data));
}

void jpt_client_read_initial_async(JptClient *client, JptClientCallback callback, gpointer user_data)
{
    enqueue_job(client, new_job(JPT_JOB_INITIAL_READ, callback, user_data));
}

void jpt_client_poll_async(JptClient *client, JptClientCallback callback, gpointer user_data)
{
    enqueue_job(client, new_job(JPT_JOB_POLL, callback, user_data));
}

void jpt_client_request_async(JptClient *client, guint16 command, GBytes *payload,
                              JptClientCallback callback, gpointer user_data)
{
    JptJob *job = new_job(JPT_JOB_REQUEST, callback, user_data);
    job->command = command;
    job->payload = g_bytes_ref(payload);
    enqueue_job(client, job);
}

void jpt_client_write_u16_async(JptClient *client, guint16 command, guint16 value,
                                JptClientCallback callback, gpointer user_data)
{
    guint8 bytes[] = {(guint8)(value & 0xff), (guint8)(value >> 8)};
    GBytes *payload = g_bytes_new(bytes, sizeof(bytes));
    jpt_client_request_async(client, command, payload, callback, user_data);
    g_bytes_unref(payload);
}

void jpt_client_free(JptClient *client)
{
    if (client == NULL) {
        return;
    }

    if (g_atomic_int_compare_and_exchange(&client->stopping, FALSE, TRUE)) {
        JptJob *job = new_job(JPT_JOB_STOP, NULL, NULL);
        g_async_queue_push(client->queue, job);
        g_thread_join(client->worker);
        client->worker = NULL;
    }

    jpt_transport_disconnect(&client->transport);
    while (g_main_context_pending(client->context)) {
        g_main_context_iteration(client->context, FALSE);
    }
    g_async_queue_unref(client->queue);
    g_main_context_unref(client->context);
    g_free(client);
}
