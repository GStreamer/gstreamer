/*
 *  gstvaapidecoder_priv.h - VA decoder abstraction (private definitions)
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPI_DECODER_PRIV_H
#define GST_VAAPI_DECODER_PRIV_H

#include <glib.h>
#include <gst/base/gstadapter.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapicontext.h>

G_BEGIN_DECLS

#define GST_VAAPI_DECODER_CAST(decoder) ((GstVaapiDecoder *)(decoder))

/**
 * GST_VAAPI_DECODER_DISPLAY:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiDisplay of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_DISPLAY
#define GST_VAAPI_DECODER_DISPLAY(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->display

/**
 * GST_VAAPI_DECODER_CONTEXT:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiContext of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CONTEXT
#define GST_VAAPI_DECODER_CONTEXT(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->context

/**
 * GST_VAAPI_DECODER_CODEC:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiCodec of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC
#define GST_VAAPI_DECODER_CODEC(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->codec

#define GST_VAAPI_DECODER_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER,        \
                                 GstVaapiDecoderPrivate))

struct _GstVaapiDecoderPrivate {
    GstVaapiDisplay    *display;
    GstVaapiContext    *context;
    GstVaapiCodec       codec;
    GstAdapter         *adapter;
    GMutex             *adapter_mutex;
    GCond              *adapter_cond;
    GQueue              surfaces;
    GMutex             *surfaces_mutex;
    GCond              *surfaces_cond;
    GThread            *decoder_thread;
    guint               decoder_thread_cancel   : 1;
};

gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder    *decoder,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
) attribute_hidden;

guint
gst_vaapi_decoder_copy(
    GstVaapiDecoder *decoder,
    guint            offset,
    guchar          *buf,
    guint            buf_size
) attribute_hidden;

guint
gst_vaapi_decoder_read_avail(GstVaapiDecoder *decoder)
    attribute_hidden;

guint
gst_vaapi_decoder_read(GstVaapiDecoder *decoder, guchar *buf, guint buf_size)
    attribute_hidden;

void
gst_vaapi_decoder_flush(GstVaapiDecoder *decoder, guint buf_size)
    attribute_hidden;

gboolean
gst_vaapi_decoder_push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface
) attribute_hidden;

G_END_DECLS

#endif /* GST_VAAPI_DECODER_PRIV_H */

