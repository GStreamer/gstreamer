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

/* Object header */
#include "gstwaylandsink.h"

/* Debugging category */
#include <gst/gstinfo.h>

/* Helper functions */
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

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
  gst_object_unref (meta->sink);
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

#define gst_wayland_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstWaylandBufferPool, gst_wayland_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static gboolean
wayland_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstWaylandBufferPool *wpool = GST_WAYLAND_BUFFER_POOL_CAST (pool);
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  /*Fixme: Enable metadata checking handling based on the config of pool */

  wpool->caps = gst_caps_ref (caps);
  wpool->info = info;
  wpool->width = info.width;
  wpool->height = info.height;

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

static struct wl_shm_pool *
make_shm_pool (struct display *display, int size, void **data)
{
  struct wl_shm_pool *pool;
  int fd;
  char filename[1024];
  static int init = 0;

  snprintf (filename, 256, "%s-%d-%s", "/tmp/wayland-shm", init++, "XXXXXX");

  fd = mkstemp (filename);
  if (fd < 0) {
    GST_ERROR ("open %s failed:", filename);
    return NULL;
  }
  if (ftruncate (fd, size) < 0) {
    GST_ERROR ("ftruncate failed:..!");
    close (fd);
    return NULL;
  }

  *data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (*data == MAP_FAILED) {
    GST_ERROR ("mmap failed: ");
    close (fd);
    return NULL;
  }

  pool = wl_shm_create_pool (display->shm, fd, size);

  close (fd);

  return pool;
}

static struct shm_pool *
shm_pool_create (struct display *display, size_t size)
{
  struct shm_pool *pool = malloc (sizeof *pool);

  if (!pool)
    return NULL;

  pool->pool = make_shm_pool (display, size, &pool->data);
  if (!pool->pool) {
    free (pool);
    return NULL;
  }

  pool->size = size;
  pool->used = 0;

  return pool;
}

static void *
shm_pool_allocate (struct shm_pool *pool, size_t size, int *offset)
{
  if (pool->used + size > pool->size)
    return NULL;

  *offset = pool->used;
  pool->used += size;

  return (char *) pool->data + *offset;
}

/* Start allocating from the beginning of the pool again */
static void
shm_pool_reset (struct shm_pool *pool)
{
  pool->used = 0;
}

static GstWlMeta *
gst_buffer_add_wayland_meta (GstBuffer * buffer, GstWaylandBufferPool * wpool)
{
  GstWlMeta *wmeta;
  GstWaylandSink *sink;
  void *data;
  gint offset;
  guint stride = 0;
  guint size = 0;

  sink = wpool->sink;
  stride = wpool->width * 4;
  size = stride * wpool->height;

  wmeta = (GstWlMeta *) gst_buffer_add_meta (buffer, GST_WL_META_INFO, NULL);
  wmeta->sink = gst_object_ref (sink);

  /*Fixme: size calculation should be more grcefull, have to consider the padding */
  if (!sink->shm_pool) {
    sink->shm_pool = shm_pool_create (sink->display, size * 15);
    shm_pool_reset (sink->shm_pool);
  }

  if (!sink->shm_pool) {
    GST_ERROR ("Failed to create shm_pool");
    return NULL;
  }

  data = shm_pool_allocate (sink->shm_pool, size, &offset);
  if (!data)
    return NULL;

  wmeta->wbuffer = wl_shm_pool_create_buffer (sink->shm_pool->pool, offset,
      sink->video_width, sink->video_height, stride, sink->format);

  wmeta->data = data;
  wmeta->size = size;

  gst_buffer_append_memory (buffer,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, data,
          size, 0, size, NULL, NULL));


  return wmeta;
}

static GstFlowReturn
wayland_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstWaylandBufferPool *w_pool = GST_WAYLAND_BUFFER_POOL_CAST (pool);
  GstBuffer *w_buffer;
  GstWlMeta *meta;

  w_buffer = gst_buffer_new ();
  meta = gst_buffer_add_wayland_meta (w_buffer, w_pool);
  if (meta == NULL) {
    gst_buffer_unref (w_buffer);
    goto no_buffer;
  }
  *buffer = w_buffer;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create buffer");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_wayland_buffer_pool_new (GstWaylandSink * waylandsink)
{
  GstWaylandBufferPool *pool;

  g_return_val_if_fail (GST_IS_WAYLAND_SINK (waylandsink), NULL);
  pool = g_object_new (GST_TYPE_WAYLAND_BUFFER_POOL, NULL);
  pool->sink = gst_object_ref (waylandsink);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_wayland_buffer_pool_class_init (GstWaylandBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_wayland_buffer_pool_finalize;

  gstbufferpool_class->set_config = wayland_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = wayland_buffer_pool_alloc;
}

static void
gst_wayland_buffer_pool_init (GstWaylandBufferPool * pool)
{
}

static void
gst_wayland_buffer_pool_finalize (GObject * object)
{
  GstWaylandBufferPool *pool = GST_WAYLAND_BUFFER_POOL_CAST (object);

  gst_object_unref (pool->sink);

  G_OBJECT_CLASS (gst_wayland_buffer_pool_parent_class)->finalize (object);
}
