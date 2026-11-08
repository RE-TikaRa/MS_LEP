#include "jpt_protocol.h"

#include <string.h>

G_DEFINE_QUARK(jpt-protocol-error-quark, jpt_protocol_error)

GBytes *jpt_frame_encode(guint16 command, const guint8 *payload, gsize payload_length, GError **error)
{
    if (payload_length > G_MAXUINT8) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_PAYLOAD_TOO_LARGE,
                    "JPT payload exceeds the one-byte length field");
        return NULL;
    }

    if (payload_length > 0 && payload == NULL) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                    "JPT payload is missing");
        return NULL;
    }

    guint8 *bytes = g_malloc(payload_length + 5);
    bytes[0] = JPT_FRAME_START;
    bytes[1] = (guint8)(command & 0xff);
    bytes[2] = (guint8)(command >> 8);
    bytes[3] = (guint8)payload_length;
    if (payload_length > 0) {
        memcpy(bytes + 4, payload, payload_length);
    }
    bytes[payload_length + 4] = JPT_FRAME_END;

    return g_bytes_new_take(bytes, payload_length + 5);
}

JptFrame *jpt_frame_new(guint16 command, const guint8 *payload, gsize payload_length)
{
    JptFrame *frame = g_new0(JptFrame, 1);
    frame->command = command;
    frame->payload = g_bytes_new(payload, payload_length);
    return frame;
}

void jpt_frame_free(JptFrame *frame)
{
    if (frame == NULL) {
        return;
    }

    g_bytes_unref(frame->payload);
    g_free(frame);
}

gboolean jpt_frame_read_u16(const JptFrame *frame, gsize offset, guint16 *value, GError **error)
{
    gsize length = 0;
    const guint8 *payload = g_bytes_get_data(frame->payload, &length);
    if (offset + 2 > length) {
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_PAYLOAD,
                    "JPT command 0x%04X returned %zu payload bytes; need bytes %zu-%zu",
                    frame->command, length, offset, offset + 1);
        return FALSE;
    }

    *value = (guint16)payload[offset] | ((guint16)payload[offset + 1] << 8);
    return TRUE;
}

void jpt_parser_init(JptParser *parser)
{
    parser->buffer = g_byte_array_new();
}

void jpt_parser_clear(JptParser *parser)
{
    g_clear_pointer(&parser->buffer, g_byte_array_unref);
}

GPtrArray *jpt_parser_push(JptParser *parser, const guint8 *data, gsize length, GError **error)
{
    g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    GPtrArray *frames = g_ptr_array_new_with_free_func((GDestroyNotify)jpt_frame_free);
    if (length > 0) {
        g_byte_array_append(parser->buffer, data, length);
    }

    while (parser->buffer->len > 0) {
        guint start = 0;
        while (start < parser->buffer->len && parser->buffer->data[start] != JPT_FRAME_START) {
            start++;
        }
        if (start > 0) {
            g_byte_array_remove_range(parser->buffer, 0, start);
        }
        if (parser->buffer->len < 4) {
            break;
        }

        guint payload_length = parser->buffer->data[3];
        guint frame_length = payload_length + 5;
        if (parser->buffer->len < frame_length) {
            break;
        }
        if (parser->buffer->data[frame_length - 1] != JPT_FRAME_END) {
            g_byte_array_remove_index(parser->buffer, 0);
            continue;
        }

        guint16 command = (guint16)parser->buffer->data[1] |
                          ((guint16)parser->buffer->data[2] << 8);
        JptFrame *frame = jpt_frame_new(command, parser->buffer->data + 4, payload_length);
        g_ptr_array_add(frames, frame);
        g_byte_array_remove_range(parser->buffer, 0, frame_length);
    }

    if (parser->buffer->len > 260) {
        g_byte_array_set_size(parser->buffer, 0);
        g_set_error(error, jpt_protocol_error_quark(), JPT_PROTOCOL_ERROR_INVALID_FRAME,
                    "JPT receive buffer exceeded the maximum frame size");
        g_ptr_array_unref(frames);
        return NULL;
    }

    return frames;
}
