/* GStreamer
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2014 Collabora Ltd.
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
  GST_DEBUG ("destroying wl_buffer %p", meta->wbuffer);
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
  g_mutex_init (&self->buffers_map_mutex);
  self->buffers_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
gst_wayland_buffer_pool_finalize (GObject * object)
{
  GstWaylandBufferPool *pool = GST_WAYLAND_BUFFER_POOL_CAST (object);

  if (pool->wl_pool)
    gst_wayland_buffer_pool_stop (GST_BUFFER_POOL (pool));

  g_mutex_clear (&pool->buffers_map_mutex);
  g_hash_table_unref (pool->buffers_map);

  g_object_unref (pool->display);

  G_OBJECT_CLASS (gst_wayland_buffer_pool_parent_class)->finalize (object);
}

static void
buffer_release (void *data, struct wl_buffer *wl_buffer)
{
  GstWaylandBufferPool *self = data;
  GstBuffer *buffer;
  GstWlMeta *meta;

  g_mutex_lock (&self->buffers_map_mutex);
  buffer = g_hash_table_lookup (self->buffers_map, wl_buffer);

  GST_LOG_OBJECT (self, "wl_buffer::release (GstBuffer: %p)", buffer);

  if (buffer) {
    meta = gst_buffer_get_wl_meta (buffer);
    if (meta->used_by_compositor) {
      meta->used_by_compositor = FALSE;
      /* unlock before unref because stop() may be called from here */
      g_mutex_unlock (&self->buffers_map_mutex);
      gst_buffer_unref (buffer);
      return;
    }
  }
  g_mutex_unlock (&self->buffers_map_mutex);
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release
};

void
gst_wayland_compositor_acquire_buffer (GstWaylandBufferPool * self,
    GstBuffer * buffer)
{
  GstWlMeta *meta;

  meta = gst_buffer_get_wl_meta (buffer);
  g_return_if_fail (meta != NULL);
  g_return_if_fail (meta->pool == self);
  g_return_if_fail (meta->used_by_compositor == FALSE);

  meta->used_by_compositor = TRUE;
  gst_buffer_ref (buffer);
}

static void
unref_used_buffers (gpointer key, gpointer value, gpointer data)
{
  GstBuffer *buffer = value;
  GstWlMeta *meta = gst_buffer_get_wl_meta (buffer);
  GList **to_unref = data;

  if (meta->used_by_compositor) {
    meta->used_by_compositor = FALSE;
    *to_unref = g_list_prepend (*to_unref, buffer);
  }
}

void
gst_wayland_compositor_release_all_buffers (GstWaylandBufferPool * self)
{
  GList *to_unref = NULL;

  g_mutex_lock (&self->buffers_map_mutex);
  g_hash_table_foreach (self->buffers_map, unref_used_buffers, &to_unref);
  g_mutex_unlock (&self->buffers_map_mutex);

  /* unref without the lock because stop() may be called from here */
  if (to_unref) {
    g_list_free_full (to_unref, (GDestroyNotify) gst_buffer_unref);
  }
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

  return GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
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

  /* all buffers are about to be destroyed;
   * we should no longer do anything with them */
  g_mutex_lock (&self->buffers_map_mutex);
  g_hash_table_remove_all (self->buffers_map);
  g_mutex_unlock (&self->buffers_map_mutex);

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
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
  meta->pool = self;
  meta->wbuffer = wl_shm_pool_create_buffer (self->wl_pool, offset,
      width, height, stride, format);
  meta->used_by_compositor = FALSE;

  /* configure listening to wl_buffer.release */
  g_mutex_lock (&self->buffers_map_mutex);
  g_hash_table_insert (self->buffers_map, meta->wbuffer, *buffer);
  g_mutex_unlock (&self->buffers_map_mutex);

  wl_buffer_add_listener (meta->wbuffer, &buffer_listener, self);

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
