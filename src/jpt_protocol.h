#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define JPT_FRAME_START 0x24
#define JPT_FRAME_END 0x2A
#define JPT_DEVICE_ERROR 0x45

typedef enum {
    JPT_PROTOCOL_ERROR_INVALID_FRAME,
    JPT_PROTOCOL_ERROR_PAYLOAD_TOO_LARGE,
    JPT_PROTOCOL_ERROR_DEVICE_REJECTED,
    JPT_PROTOCOL_ERROR_UNEXPECTED_COMMAND,
    JPT_PROTOCOL_ERROR_INVALID_PAYLOAD
} JptProtocolError;

typedef struct {
    guint16 command;
    GBytes *payload;
} JptFrame;

typedef struct {
    GByteArray *buffer;
} JptParser;

GQuark jpt_protocol_error_quark(void);
GBytes *jpt_frame_encode(guint16 command, const guint8 *payload, gsize payload_length, GError **error);
JptFrame *jpt_frame_new(guint16 command, const guint8 *payload, gsize payload_length);
void jpt_frame_free(JptFrame *frame);
gboolean jpt_frame_read_u16(const JptFrame *frame, gsize offset, guint16 *value, GError **error);

void jpt_parser_init(JptParser *parser);
void jpt_parser_clear(JptParser *parser);
GPtrArray *jpt_parser_push(JptParser *parser, const guint8 *data, gsize length, GError **error);

G_END_DECLS
