/*
 *  gstvaapipluginbuffer.c - Private GStreamer/VA video buffers
 *
 *  Copyright (C) 2012 Intel Corporation
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gst/vaapi/gstvaapivideometa.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#if USE_GLX
# include <gst/vaapi/gstvaapivideoconverter_glx.h>
#endif
#include "gstvaapipluginbuffer.h"

static GFunc
get_surface_converter(GstVaapiDisplay *display)
{
    GFunc func;

    switch (gst_vaapi_display_get_display_type(display)) {
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        func = (GFunc)gst_vaapi_video_converter_glx_new;
        break;
#endif
    default:
        func = NULL;
        break;
    }
    return func;
}

static GstBuffer *
get_buffer(GstVaapiVideoMeta *meta)
{
    GstBuffer *buffer;

    if (!meta)
        return NULL;

    gst_vaapi_video_meta_set_surface_converter(meta,
        get_surface_converter(gst_vaapi_video_meta_get_display(meta)));

    buffer = gst_vaapi_video_buffer_new(meta);
    gst_vaapi_video_meta_unref(meta);
    return buffer;
}

GstBuffer *
gst_vaapi_video_buffer_new_from_pool(GstVaapiVideoPool *pool)
{
    return get_buffer(gst_vaapi_video_meta_new_from_pool(pool));
}

GstBuffer *
gst_vaapi_video_buffer_new_from_buffer(GstBuffer *buffer)
{
    return get_buffer(gst_buffer_get_vaapi_video_meta(buffer));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_image(GstVaapiImage *image)
{
    return get_buffer(gst_vaapi_video_meta_new_with_image(image));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface(GstVaapiSurface *surface)
{
    return get_buffer(gst_vaapi_video_meta_new_with_surface(surface));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface_proxy(GstVaapiSurfaceProxy *proxy)
{
    return get_buffer(gst_vaapi_video_meta_new_with_surface_proxy(proxy));
}
