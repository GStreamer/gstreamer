/*
 *  gstvaapivideosink.c - VA sink interface
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapivideosink
 * @short_description: An interface for implementing VA-API sink elements
 */

#include "config.h"
#include "gstvaapivideosink.h"

static void
gst_vaapi_video_sink_base_init(gpointer g_class)
{
    static gboolean is_initialized = FALSE;

    if (!is_initialized) {
        is_initialized = TRUE;
    }
}

GType
gst_vaapi_video_sink_get_type(void)
{
    static GType iface_type = 0;

    if (G_UNLIKELY(!iface_type)) {
        static const GTypeInfo info = {
            sizeof(GstVaapiVideoSinkInterface),
            gst_vaapi_video_sink_base_init,     /* base_init */
            NULL,                               /* base_finalize */
        };

        iface_type = g_type_register_static(
            G_TYPE_INTERFACE,
            "GstVaapiVideoSink",
            &info,
            0
        );
    }
    return iface_type;
}

/**
 * gst_vaapi_video_sink_get_display:
 * @sink: a #GstElement
 *
 * Returns the #GstVaapiDisplay created by the VA-API @sink element.
 *
 * Return value: the #GstVaapiDisplay created by the @sink element
 */
GstVaapiDisplay *
gst_vaapi_video_sink_get_display(GstVaapiVideoSink *sink)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_SINK(sink), NULL);

    return GST_VAAPI_VIDEO_SINK_GET_INTERFACE(sink)->get_display(sink);
}

/**
 * gst_vaapi_video_sink_lookup:
 * @element: a #GstElement
 *
 * Traverses the whole downstream elements chain and finds a suitable
 * #GstVaapiDisplay. This is a helper function for intermediate VA-API
 * elements that don't create a #GstVaapiDisplay but require one.
 * e.g. the `vaapiconvert' element.
 *
 * Return value: the #GstVaapiDisplay created by a downstream sink
 * element, or %NULL if none was found
 */
GstVaapiVideoSink *
gst_vaapi_video_sink_lookup(GstElement *element)
{
    GstVaapiVideoSink *sink = NULL;
    GstPad *pad, *peer;

    g_return_val_if_fail(GST_IS_ELEMENT(element), NULL);

    while (!sink) {
        pad = gst_element_get_static_pad(element, "src");
        if (!pad)
            break;

        peer = gst_pad_get_peer(pad);
        g_object_unref(pad);
        if (!peer)
            break;

        element = gst_pad_get_parent_element(peer);
        g_object_unref(peer);
        if (!element)
            break;

        if (GST_VAAPI_IS_VIDEO_SINK(element))
            sink = GST_VAAPI_VIDEO_SINK(element);
        g_object_unref(element);
    }
    return sink;
}
