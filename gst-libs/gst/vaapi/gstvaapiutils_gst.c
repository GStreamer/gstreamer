/*
 *  gstvaapiutils_gst.c - GST utilties
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

#include "config.h"
#include "gstvaapiutils_gst.h"
#include "gstvaapivideosink.h"
#include "gstvaapivideobuffer.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static GstVaapiDisplay *
lookup_through_vaapisink_iface(GstElement *element)
{
    GstVaapiDisplay *display;
    GstVaapiVideoSink *sink;

    GST_DEBUG("looking for a downstream vaapisink");

    /* Look for a downstream vaapisink */
    sink = gst_vaapi_video_sink_lookup(element);
    if (!sink)
        return NULL;

    display = gst_vaapi_video_sink_get_display(sink);
    GST_DEBUG("  found display %p", display);
    return display;
}

static GstVaapiDisplay *
lookup_through_peer_buffer(GstElement *element)
{
    GstVaapiDisplay *display;
    GstPad *pad;
    GstBuffer *buffer;
    GstFlowReturn ret;

    GST_DEBUG("looking for a GstVaapiVideoBuffer from peer");

    /* Look for a GstVaapiVideoBuffer from peer */
    pad = gst_element_get_static_pad(element, "src");
    if (!pad)
        return NULL;

    buffer = NULL;
    ret = gst_pad_alloc_buffer(pad, 0, 0, GST_PAD_CAPS(pad), &buffer);
    g_object_unref(pad);
    if (ret != GST_FLOW_OK || !buffer)
        return NULL;

    display = GST_VAAPI_IS_VIDEO_BUFFER(buffer) ?
        gst_vaapi_video_buffer_get_display(GST_VAAPI_VIDEO_BUFFER(buffer)) :
        NULL;
    gst_buffer_unref(buffer);

    GST_DEBUG("  found display %p", display);
    return display;
}

/**
 * gst_vaapi_display_lookup_downstream:
 * @element: a #GstElement
 *
 * Finds a suitable #GstVaapiDisplay downstream from @element.
 *
 *   1. Check whether a downstream element implements the
 *      #GstVaapiVideoSinkInterface interface.
 *
 *   2. Check whether the @element peer implements a custom
 *      buffer_alloc() function that allocates #GstVaapiVideoBuffer
 *      buffers.
 *
 * Return value: a downstream allocated #GstVaapiDisplay object, or
 *   %NULL if none was found
 */
GstVaapiDisplay *
gst_vaapi_display_lookup_downstream(GstElement *element)
{
    GstVaapiDisplay *display;

    display = lookup_through_vaapisink_iface(element);
    if (display)
        return display;

    display = lookup_through_peer_buffer(element);
    if (display)
        return display;

    return NULL;
}
