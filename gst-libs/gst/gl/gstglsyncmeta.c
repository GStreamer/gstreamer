/*
 * GStreamer
 * Copyright (C) 2014 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstglsyncmeta.h"

#define GST_CAT_DEFAULT gst_gl_sync_meta_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT        0x00000001
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif

GstGLSyncMeta *
gst_buffer_add_gl_sync_meta (GstGLContext * context, GstBuffer * buffer)
{
  GstGLSyncMeta *meta;

  g_return_val_if_fail (GST_GL_IS_CONTEXT (context), NULL);

  meta =
      (GstGLSyncMeta *) gst_buffer_add_meta ((buffer), GST_GL_SYNC_META_INFO,
      NULL);

  if (!meta)
    return NULL;

  meta->context = gst_object_ref (context);
  meta->glsync = NULL;

  return meta;
}

static void
_set_sync_point (GstGLContext * context, GstGLSyncMeta * sync_meta)
{
  const GstGLFuncs *gl = context->gl_vtable;

  if (gl->FenceSync) {
    if (sync_meta->glsync)
      gl->DeleteSync (sync_meta->glsync);
    sync_meta->glsync = gl->FenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    GST_LOG ("setting sync object %p", sync_meta->glsync);
  } else {
    gl->Flush ();
  }
}

void
gst_gl_sync_meta_set_sync_point (GstGLSyncMeta * sync_meta,
    GstGLContext * context)
{
  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _set_sync_point, sync_meta);
}

static void
_wait (GstGLContext * context, GstGLSyncMeta * sync_meta)
{
  const GstGLFuncs *gl = context->gl_vtable;
  GLenum res;

  if (gl->ClientWaitSync) {
    do {
      GST_LOG ("waiting on sync object %p", sync_meta->glsync);
      res =
          gl->ClientWaitSync (sync_meta->glsync, GL_SYNC_FLUSH_COMMANDS_BIT,
          1000000000 /* 1s */ );
    } while (res == GL_TIMEOUT_EXPIRED);
  }
}

void
gst_gl_sync_meta_wait (GstGLSyncMeta * sync_meta, GstGLContext * context)
{
  if (sync_meta->context == context)
    return;

  if (sync_meta->glsync) {
    gst_gl_context_thread_add (context,
        (GstGLContextThreadFunc) _wait, sync_meta);
  }
}

static gboolean
_gst_gl_sync_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstGLSyncMeta *dmeta, *smeta;

  smeta = (GstGLSyncMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = data;

    if (!copy->region) {
      /* only copy if the complete data is copied as well */
      dmeta = gst_buffer_add_gl_sync_meta (smeta->context, dest);

      if (!dmeta)
        return FALSE;

      GST_DEBUG ("copy gl sync metadata");

      dmeta->glsync = smeta->glsync;
    }
  }
  return TRUE;
}

static void
_free_gl_sync_meta (GstGLContext * context, GstGLSyncMeta * sync_meta)
{
  const GstGLFuncs *gl = context->gl_vtable;

  if (sync_meta->glsync)
    gl->DeleteSync (sync_meta->glsync);
  sync_meta->glsync = NULL;
}

static void
_gst_gl_sync_meta_free (GstGLSyncMeta * sync_meta, GstBuffer * buffer)
{
  if (sync_meta->glsync) {
    gst_gl_context_thread_add (sync_meta->context,
        (GstGLContextThreadFunc) _free_gl_sync_meta, sync_meta);
  }
  gst_object_unref (sync_meta->context);
}

static gboolean
_gst_gl_sync_meta_init (GstGLSyncMeta * sync_meta, gpointer params,
    GstBuffer * buffer)
{
  static volatile gsize _init;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_gl_sync_meta_debug, "glsyncmeta", 0,
        "glsyncmeta");
    g_once_init_leave (&_init, 1);
  }

  sync_meta->context = NULL;
  sync_meta->glsync = NULL;

  return TRUE;
}

GType
gst_gl_sync_meta_api_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstGLSyncMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

const GstMetaInfo *
gst_gl_sync_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_GL_SYNC_META_API_TYPE, "GstGLSyncMeta",
        sizeof (GstVideoMeta), (GstMetaInitFunction) _gst_gl_sync_meta_init,
        (GstMetaFreeFunction) _gst_gl_sync_meta_free,
        _gst_gl_sync_meta_transform);
    g_once_init_leave (&meta_info, meta);
  }

  return meta_info;
}
