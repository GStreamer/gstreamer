/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystree00@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_GL_UPLOAD_H__
#define __GST_GL_UPLOAD_H__

#include <gst/video/video.h>
#include <gst/gstmemory.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

GType gst_gl_upload_get_type (void);
#define GST_TYPE_GL_UPLOAD (gst_gl_upload_get_type())
#define GST_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_UPLOAD,GstGLUpload))
#define GST_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_UPLOAD,GstGLUploadClass))
#define GST_IS_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_UPLOAD))
#define GST_IS_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_CAST(obj) ((GstGLUpload*)(obj))

/**
 * GstGLUpload
 *
 * Opaque #GstGLUpload object
 */
struct _GstGLUpload
{
  /* <private> */
  GstObject        parent;

  GstGLContext    *context;
  GstGLColorConvert *convert;

  /* input data */
  GstVideoInfo     in_info;

  gboolean         initted;

  GstGLMemory     *in_tex[GST_VIDEO_MAX_PLANES];
  GstGLMemory     *out_tex;

  /* <private> */
  GstGLUploadPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

/**
 * GstGLUploadClass:
 *
 * The #GstGLUploadClass struct only contains private data
 */
struct _GstGLUploadClass
{
  GstObjectClass object_class;
};

GstGLUpload * gst_gl_upload_new            (GstGLContext * context);

void           gst_gl_upload_set_format    (GstGLUpload * upload, GstVideoInfo * in_info);
GstVideoInfo * gst_gl_upload_get_format    (GstGLUpload * upload);

gboolean gst_gl_upload_perform_with_buffer (GstGLUpload * upload, GstBuffer * buffer, guint * tex_id);
void gst_gl_upload_release_buffer (GstGLUpload * upload);
gboolean gst_gl_upload_perform_with_data          (GstGLUpload * upload, GLuint * texture_id,
                                                   gpointer data[GST_VIDEO_MAX_PLANES]);

gboolean gst_gl_upload_perform_with_gl_texture_upload_meta (GstGLUpload *upload, GstVideoGLTextureUploadMeta *meta, guint texture_id[4]);

G_END_DECLS

#endif /* __GST_GL_UPLOAD_H__ */
