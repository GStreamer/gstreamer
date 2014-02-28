/* GStreamer
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>

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

#include "waylandpool.h"
#include "wldisplay.h"
#include "wlvideoformat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

/* wl metadata */
GType
gst_wl_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { "memory", "size", "colorspace", "orientation", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstWlMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static void
gst_wl_meta_free (GstWlMeta * meta, GstBuffer * buffer)
{
  g_object_unref (meta->display);
  munmap (meta->data, meta->size);
  wl_buffer_destroy (meta->wbuffer);
}

const GstMetaInfo *
gst_wl_meta_get_info (void)
{
  static const GstMetaInfo *wl_meta_info = NULL;

  if (g_once_init_enter (&wl_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_WL_META_API_TYPE, "GstWlMeta",
        sizeof (GstWlMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_wl_meta_free,
        (GstMetaTransformFunction) NULL);
    g_once_init_leave (&wl_meta_info, meta);
  }
  return wl_meta_info;
}

/* bufferpool */
static void gst_wayland_buffer_pool_finalize (GObject * object);
static gboolean gst_wayland_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static gboolean gst_wayland_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_wayland_buffer_pool_stop (GstBufferPool * pool);
static GstFlowReturn gst_wayland_buffer_pool_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);

#define gst_wayland_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstWaylandBufferPool, gst_wayland_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static void
gst_wayland_buffer_pool_class_init (GstWaylandBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_wayland_buffer_pool_finalize;

  gstbufferpool_class->set_config = gst_wayland_buffer_pool_set_config;
  gstbufferpool_class->start = gst_wayland_buffer_pool_start;
  gstbufferpool_class->stop = gst_wayland_buffer_pool_stop;
  gstbufferpool_class->alloc_buffer = gst_wayland_buffer_pool_alloc;
}

static void
gst_wayland_buffer_pool_init (GstWaylandBufferPool * self)
{
  gst_video_info_init (&self->info);
}

static void
gst_wayland_buffer_pool_finalize (GObject * object)
{
  GstWaylandBufferPool *pool = GST_WAYLAND_BUFFER_POOL_CAST (object);

  if (pool->wl_pool)
    gst_wayland_buffer_pool_stop (GST_BUFFER_POOL (pool));

  g_object_unref (pool->display);

  G_OBJECT_CLASS (gst_wayland_buffer_pool_parent_class)->finalize (object);
}

static gboolean
gst_wayland_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstWaylandBufferPool *self = GST_WAYLAND_BUFFER_POOL_CAST (pool);
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&self->info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT,
      GST_VIDEO_INFO_WIDTH (&self->info), GST_VIDEO_INFO_HEIGHT (&self->info),
      caps);

  /*Fixme: Enable metadata checking handling based on the config of pool */

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static gboolean
gst_wayland_buffer_pool_start (GstBufferPool * pool)
{
  GstWaylandBufferPool *self = GST_WAYLAND_BUFFER_POOL (pool);
  guint size = 0;
  int fd;
  char filename[1024];
  static int init = 0;

  GST_DEBUG_OBJECT (self, "Initializing wayland buffer pool");

  /* configure */
  size = GST_VIDEO_INFO_SIZE (&self->info) * 15;

  /* allocate shm pool */
  snprintf (filename, 1024, "%s/%s-%d-%s", g_get_user_runtime_dir (),
      "wayland-shm", init++, "XXXXXX");

  fd = mkstemp (filename);
  if (fd < 0) {
    GST_ERROR_OBJECT (pool, "opening temp file %s failed: %s", filename,
        strerror (errno));
    return FALSE;
  }
  if (ftruncate (fd, size) < 0) {
    GST_ERROR_OBJECT (pool, "ftruncate failed: %s", strerror (errno));
    close (fd);
    return FALSE;
  }

  self->data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (self->data == MAP_FAILED) {
    GST_ERROR_OBJECT (pool, "mmap failed: %s", strerror (errno));
    close (fd);
    return FALSE;
  }

  self->wl_pool = wl_shm_create_pool (self->display->shm, fd, size);
  unlink (filename);
  close (fd);

  self->size = size;
  self->used = 0;

  return TRUE;
}

static gboolean
gst_wayland_buffer_pool_stop (GstBufferPool * pool)
{
  GstWaylandBufferPool *self = GST_WAYLAND_BUFFER_POOL (pool);

  GST_DEBUG_OBJECT (self, "Stopping wayland buffer pool");

  munmap (self->data, self->size);
  wl_shm_pool_destroy (self->wl_pool);

  self->wl_pool = NULL;
  self->size = 0;
  self->used = 0;

  return TRUE;
}

static GstFlowReturn
gst_wayland_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstWaylandBufferPool *self = GST_WAYLAND_BUFFER_POOL_CAST (pool);
  gint width, height, stride;
  gsize size;
  enum wl_shm_format format;
  gint offset;
  void *data;
  GstWlMeta *meta;

  width = GST_VIDEO_INFO_WIDTH (&self->info);
  height = GST_VIDEO_INFO_HEIGHT (&self->info);
  stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0);
  size = GST_VIDEO_INFO_SIZE (&self->info);
  format =
      gst_video_format_to_wayland_format (GST_VIDEO_INFO_FORMAT (&self->info));

  GST_DEBUG_OBJECT (self, "Allocating buffer of size %" G_GSSIZE_FORMAT
      " (%d x %d, stride %d), format %s", size, width, height, stride,
      gst_wayland_format_to_string (format));

  /* try to reserve another memory block from the shm pool */
  if (self->used + size > self->size)
    goto no_buffer;

  offset = self->used;
  self->used += size;
  data = ((gchar *) self->data) + offset;

  /* create buffer and its metadata object */
  *buffer = gst_buffer_new ();
  meta = (GstWlMeta *) gst_buffer_add_meta (*buffer, GST_WL_META_INFO, NULL);
  meta->display = g_object_ref (self->display);
  meta->wbuffer = wl_shm_pool_create_buffer (self->wl_pool, offset,
      width, height, stride, format);
  meta->data = data;
  meta->size = size;

  /* add the allocated memory on the GstBuffer */
  gst_buffer_append_memory (*buffer,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, data,
          size, 0, size, NULL, NULL));

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create buffer");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_wayland_buffer_pool_new (GstWlDisplay * display)
{
  GstWaylandBufferPool *pool;

  g_return_val_if_fail (GST_IS_WL_DISPLAY (display), NULL);
  pool = g_object_new (GST_TYPE_WAYLAND_BUFFER_POOL, NULL);
  pool->display = g_object_ref (display);

  return GST_BUFFER_POOL_CAST (pool);
}
