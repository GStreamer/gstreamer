/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstglmeta.h"

/* GstVideoMeta map/unmap */
gboolean
gst_gl_meta_map (GstVideoMeta * meta, guint plane, GstMapInfo * info,
    gpointer * data, gint * stride, GstMapFlags flags)
{
  return FALSE;
}

gboolean
gst_gl_meta_unmap (GstVideoMeta * meta, guint plane, GstMapInfo * info)
{
  return FALSE;
}

static void
gst_gl_meta_init (GstGLMeta * gl_meta, gpointer params, GstBuffer * buffer)
{
  gl_meta->buffer = buffer;
}

void
gst_gl_meta_free (GstGLMeta * gl_meta, GstBuffer * buffer)
{
  if (gl_meta->display) {
    g_object_unref (gl_meta->display);
    gl_meta->display = NULL;
  }
}

static gboolean
gst_gl_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstGLMeta *dmeta, *smeta;

  smeta = (GstGLMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = data;

    if (!copy->region) {
      /* only copy if the complete data is copied as well */
      dmeta =
          (GstGLMeta *) gst_buffer_add_meta (dest, GST_GL_META_INFO,
          smeta->display);
      dmeta->buffer = dest;

      dmeta->memory =
          (GstGLMemory *) gst_memory_copy (GST_MEMORY_CAST (smeta->memory), 0,
          -1);
    }
  }
  return TRUE;
}

GType
gst_gl_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };      /* don't know what to set here */

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstGLMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_gl_meta_get_info (void)
{
  static const GstMetaInfo *gl_meta_info = NULL;

  if (gl_meta_info == NULL) {
    gl_meta_info =
        gst_meta_register (GST_GL_META_API_TYPE, "GstGLMeta",
        sizeof (GstGLMeta), (GstMetaInitFunction) gst_gl_meta_init,
        (GstMetaFreeFunction) gst_gl_meta_free,
        (GstMetaTransformFunction) gst_gl_meta_transform);
  }
  return gl_meta_info;
}

/**
 * gst_buffer_add_gl_meta:
 * @buffer: a #GstBuffer
 * @display: the #GstGLDisplay to use
 *
 * Creates and adds a #GstGLMeta to a @buffer.
 *
 * Returns: (transfer full): a newly created #GstGLMeta
 */
GstGLMeta *
gst_buffer_add_gl_meta (GstBuffer * buffer, GstGLDisplay * display)
{
  GstGLMeta *gl_meta;

  gl_meta = (GstGLMeta *) gst_buffer_add_meta (buffer, GST_GL_META_INFO, NULL);

  gl_meta->display = g_object_ref (display);

  g_assert (gst_buffer_n_memory (buffer) == 1);

  gl_meta->memory = (GstGLMemory *) gst_buffer_get_memory (buffer, 0);

  return gl_meta;
}
