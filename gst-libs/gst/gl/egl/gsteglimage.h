/*
 * GStreamer
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef _GST_EGL_IMAGE_H_
#define _GST_EGL_IMAGE_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/gl/gl.h>
#include <gst/gl/egl/gstegl.h>

G_BEGIN_DECLS

GType gst_egl_image_get_type (void);
#define GST_TYPE_EGL_IMAGE                         (gst_egl_image_get_type())
#define GST_IS_EGL_IMAGE(obj)                      (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_EGL_IMAGE))
#define GST_EGL_IMAGE_CAST(obj)                    ((GstEGLImage *)(obj))
#define GST_EGL_IMAGE(obj)                         (GST_EGL_IMAGE_CAST(obj))

typedef struct _GstEGLImage GstEGLImage;

typedef void (*GstEGLImageDestroyNotify) (GstEGLImage * image,
    gpointer data);

/**
 * GstEGLImage:
 *
 * Opaque #GstEGLImage struct.
 */
struct _GstEGLImage
{
  GstMiniObject parent;

  GstGLContext *context;
  EGLImageKHR image;
  GstGLFormat format;

  /* <private> */
  gpointer destroy_data;
  GstEGLImageDestroyNotify destroy_notify;

  gpointer _padding[GST_PADDING];
};

GstEGLImage *             gst_egl_image_new_wrapped             (GstGLContext * context,
                                                                 EGLImageKHR image,
                                                                 GstGLFormat format,
                                                                 gpointer user_data,
                                                                 GstEGLImageDestroyNotify user_data_destroy);
EGLImageKHR             gst_egl_image_get_image                 (GstEGLImage * image);

GstEGLImage *           gst_egl_image_from_texture              (GstGLContext * context,
                                                                 GstGLMemory * gl_mem,
                                                                 guintptr * attribs);
#if GST_GL_HAVE_DMABUF
GstEGLImage *           gst_egl_image_from_dmabuf               (GstGLContext * context,
                                                                 gint dmabuf,
                                                                 GstVideoInfo * in_info,
                                                                 gint plane,
                                                                 gsize offset);
#endif

/**
 * gst_egl_image_ref:
 * @image: a #GstEGLImage.
 *
 * Increases the refcount of the given image by one.
 *
 * Returns: (transfer full): @image
 */
static inline GstEGLImage *
gst_egl_image_ref (GstEGLImage * image)
{
  return (GstEGLImage *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (image));
}

/**
 * gst_egl_image_unref:
 * @image: (transfer full): a #GstEGLImage.
 *
 * Decreases the refcount of the image. If the refcount reaches 0, the image
 * with the associated metadata and memory will be freed.
 */
static inline void
gst_egl_image_unref (GstEGLImage * image)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (image));
}

G_END_DECLS

#endif /* _GST_EGL_IMAGE_H_ */
