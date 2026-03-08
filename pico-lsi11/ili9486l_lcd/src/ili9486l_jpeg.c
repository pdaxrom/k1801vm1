#include "ili9486l_jpeg.h"

#include "ili9486l.h"
#include "tjpgd/tjpgd.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ILI9486L_JPEG_WORKBUF_SIZE 4096u

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
    uint16_t origin_x;
    uint16_t origin_y;
} ili9486l_jpeg_stream_t;

typedef union {
    uint32_t align;
    uint8_t bytes[ILI9486L_JPEG_WORKBUF_SIZE];
} ili9486l_jpeg_workspace_t;

static size_t ili9486l_jpeg_input(JDEC *decoder, uint8_t *buffer, size_t bytes_requested)
{
    ili9486l_jpeg_stream_t *stream = (ili9486l_jpeg_stream_t *)decoder->device;
    size_t available = (size_t)(stream->end - stream->cursor);

    if (bytes_requested > available) {
        bytes_requested = available;
    }

    if (buffer != NULL) {
        memcpy(buffer, stream->cursor, bytes_requested);
    }

    stream->cursor += bytes_requested;
    return bytes_requested;
}

static int ili9486l_jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    const ili9486l_jpeg_stream_t *stream = (const ili9486l_jpeg_stream_t *)decoder->device;

    ili9486l_draw_rgb888_as_rgb666_rect(
        (const uint8_t *)bitmap,
        (uint16_t)(stream->origin_x + rect->left),
        (uint16_t)(stream->origin_y + rect->top),
        (uint16_t)(rect->right - rect->left + 1u),
        (uint16_t)(rect->bottom - rect->top + 1u));
    return 1;
}

static bool ili9486l_jpeg_prepare(
    const uint8_t *jpeg_data,
    size_t jpeg_size,
    JDEC *decoder,
    ili9486l_jpeg_workspace_t *workspace,
    ili9486l_jpeg_stream_t *stream)
{
    if (jpeg_data == NULL || jpeg_size == 0u || decoder == NULL || workspace == NULL || stream == NULL) {
        return false;
    }

    stream->cursor = jpeg_data;
    stream->end = jpeg_data + jpeg_size;
    stream->origin_x = 0u;
    stream->origin_y = 0u;

    return jd_prepare(decoder, ili9486l_jpeg_input, workspace->bytes, sizeof(workspace->bytes), stream) == JDR_OK;
}

bool ili9486l_jpeg_get_info(const uint8_t *jpeg_data, size_t jpeg_size, ili9486l_jpeg_info_t *out_info)
{
    static ili9486l_jpeg_workspace_t workspace;
    ili9486l_jpeg_stream_t stream;
    JDEC decoder;

    if (!ili9486l_jpeg_prepare(jpeg_data, jpeg_size, &decoder, &workspace, &stream)) {
        return false;
    }

    if (out_info != NULL) {
        out_info->width = decoder.width;
        out_info->height = decoder.height;
    }

    return true;
}

bool ili9486l_jpeg_draw(const uint8_t *jpeg_data, size_t jpeg_size, uint16_t x, uint16_t y)
{
    static ili9486l_jpeg_workspace_t workspace;
    ili9486l_jpeg_stream_t stream;
    JDEC decoder;

    if (!ili9486l_jpeg_prepare(jpeg_data, jpeg_size, &decoder, &workspace, &stream)) {
        return false;
    }

    if ((uint32_t)x + decoder.width > ili9486l_width() || (uint32_t)y + decoder.height > ili9486l_height()) {
        return false;
    }

    stream.origin_x = x;
    stream.origin_y = y;
    return jd_decomp(&decoder, ili9486l_jpeg_output, 0) == JDR_OK;
}
