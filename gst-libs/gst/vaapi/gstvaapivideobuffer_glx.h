/*
 *  gstvaapivideobuffer_glx.h - Gstreamer/VA video buffer
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#ifndef GST_VAAPI_VIDEO_BUFFER_GLX_H
#define GST_VAAPI_VIDEO_BUFFER_GLX_H

#include <gst/vaapi/gstvaapidisplay_glx.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/vaapi/gstvaapivideopool.h>
#include <gst/video/gstsurfacebuffer.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_BUFFER_GLX \
    (gst_vaapi_video_buffer_glx_get_type())

#define GST_VAAPI_VIDEO_BUFFER_GLX(obj)                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                                  \
                                GST_VAAPI_TYPE_VIDEO_BUFFER_GLX,        \
                                GstVaapiVideoBufferGLX))

#define GST_VAAPI_VIDEO_BUFFER_GLX_CLASS(klass)                 \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_VIDEO_BUFFER_GLX,   \
                             GstVaapiVideoBufferGLXClass))

#define GST_VAAPI_IS_VIDEO_BUFFER_GLX(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_BUFFER_GLX))

#define GST_VAAPI_IS_VIDEO_BUFFER_GLX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_BUFFER_GLX))

#define GST_VAAPI_VIDEO_BUFFER_GLX_GET_CLASS(obj)               \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_VIDEO_BUFFER_GLX, \
                               GstVaapiVideoBufferGLXClass))

typedef struct _GstVaapiVideoBufferGLX             GstVaapiVideoBufferGLX;
typedef struct _GstVaapiVideoBufferGLXClass        GstVaapiVideoBufferGLXClass;

/**
 * GstVaapiVideoBufferGLX:
 *
 * A #GstBuffer holding video objects (#GstVaapiSurface and #GstVaapiImage).
 */
struct _GstVaapiVideoBufferGLX {
    /*< private >*/
    GstVaapiVideoBuffer parent_instance;
};

/**
 * GstVaapiVideoBufferGLXClass:
 *
 * A #GstBuffer holding video objects
 */
struct _GstVaapiVideoBufferGLXClass {
    /*< private >*/
    GstVaapiVideoBufferClass parent_class;
};

GType      gst_vaapi_video_buffer_glx_get_type        (void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_BUFFER_GLX_H */
