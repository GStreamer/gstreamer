/* 
 * GStreame
 * Copyright (C) 2007 Matthew Waters <ystreet00@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GST_GL_META_H_
#define _GST_GL_META_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstgldisplay.h"
#include "gstglmemory.h"

G_BEGIN_DECLS

typedef struct _GstGLMeta GstGLMeta;

GType gst_gl_meta_api_get_type (void);
#define GST_GL_META_API_TYPE  (gst_gl_meta_api_get_type())
#define GST_GL_META_INFO  (gst_gl_meta_get_info())
const GstMetaInfo * gst_gl_meta_get_info (void);

/**
 * GstGLMeta:
 * @meta: parent #GstMeta
 * @buffer: #GstBuffer this meta belongs to
 * @video_meta: the #GstVideoMeta associated with this meta
 * @display: the #GstGLDisplay
 * @texture_id: the GL texture associated with this meta
 *
 * Extra buffer metadata describing OpenGL objects
 */
struct _GstGLMeta {
    GstMeta meta;
    GstBuffer *buffer;

    GstGLDisplay *display;

    GstGLMemory *memory;
};

#define GST_GL_VIDEO_FORMATS "RGBA"
#define GST_GL_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_VIDEO_FORMATS)

#ifndef OPENGL_ES2

# define GST_GL_UPLOAD_FORMATS "{ RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, xBGR, ARGB, ABGR, I420, YV12, YUY2, UYVY, AYUV }"
# define GST_GL_DOWNLOAD_FORMATS "{ RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, xBGR, ARGB, ABGR, I420, YV12, YUY2, UYVY, AYUV }"
# define GST_GL_UPLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_UPLOAD_FORMATS)
# define GST_GL_DOWNLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_DOWNLOAD_FORMATS)

#else /* OPENGL_ES2 */

# define GST_GL_UPLOAD_FORMATS "{ RGB, RGBx, RGBA, I420, YV12, YUY2, UYVY, AYUV }"
# define GST_GL_DOWNLOAD_FORMATS "{ RGB, RGBx, RGBA, I420, YV12, YUY2, UYVY, AYUV }"
# define GST_GL_UPLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_UPLOAD_FORMATS)
# define GST_GL_DOWNLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_DOWNLOAD_FORMATS)

#endif /* OPENGL_ES2 */

#define gst_buffer_get_gl_meta(b) ((GstGLMeta*)gst_buffer_get_meta((b),GST_GL_META_API_TYPE))
GstGLMeta * gst_buffer_add_gl_meta (GstBuffer * buffer, GstGLDisplay * display);

gboolean gst_gl_meta_map (GstVideoMeta *meta, guint plane, GstMapInfo *info,
                          gpointer *data, gint * stride, GstMapFlags flags);
gboolean gst_gl_meta_unmap (GstVideoMeta *meta, guint plane, GstMapInfo *info);

G_END_DECLS

#endif /* _GST_GL_META_H_ */
