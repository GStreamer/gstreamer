/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013-2019, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *           George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxbufferpool.h"
#include "gstomxvideo.h"

#include <gst/allocators/gstdmabuf.h>

GST_DEBUG_CATEGORY_STATIC (gst_omx_buffer_pool_debug_category);
#define GST_CAT_DEFAULT gst_omx_buffer_pool_debug_category

enum
{
  SIG_ALLOCATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Buffer pool for the buffers of an OpenMAX port.
 *
 * This pool is only used if we either passed buffers from another
 * pool to the OMX port or provide the OMX buffers directly to other
 * elements.
 *
 * An output buffer is in the pool if it is currently owned by the port,
 * i.e. after OMX_FillThisBuffer(). An output buffer is outside
 * the pool after it was taken from the port after it was handled
 * by the port, i.e. FillBufferDone.
 *
 * An input buffer is in the pool if it is currently available to be filled
 * upstream. It will be put back into the pool when it has been processed by
 * OMX, (EmptyBufferDone).
 *
 * Buffers can be allocated by us (OMX_AllocateBuffer()) or allocated
 * by someone else and (temporarily) passed to this pool
 * (OMX_UseBuffer(), OMX_UseEGLImage()). In the latter case the pool of
 * the buffer will be overriden, and restored in free_buffer(). Other
 * buffers are just freed there.
 *
 * The pool always has a fixed number of minimum and maximum buffers
 * and these are allocated while starting the pool and released afterwards.
 * They correspond 1:1 to the OMX buffers of the port, which are allocated
 * before the pool is started.
 *
 * Acquiring an output buffer from this pool happens after the OMX buffer has
 * been acquired from the port. gst_buffer_pool_acquire_buffer() is
 * supposed to return the buffer that corresponds to the OMX buffer.
 *
 * For buffers provided to upstream, the buffer will be passed to
 * the component manually when it arrives and then unreffed. If the
 * buffer is released before reaching the component it will be just put
 * back into the pool as if EmptyBufferDone has happened. If it was
 * passed to the component, it will be back into the pool when it was
 * released and EmptyBufferDone has happened.
 *
 * For buffers provided to downstream, the buffer will be returned
 * back to the component (OMX_FillThisBuffer()) when it is released.
 *
 * This pool uses a special allocator object, GstOMXAllocator. The main purpose
 * of this allocator is to track GstMemory objects in the same way that a
 * GstBufferPool tracks buffers. When a buffer is inserted into this pool
 * (either because it was just allocated or because it was released back to
 * the pool), its memory is ripped off and is tracked separately by the
 * allocator. When a buffer is then acquired, we acquire the corresponding
 * GstMemory from the allocator and put it back in the buffer.
 *
 * This allocator mechanism allows us to track memory that has been shared
 * with buffers that are not part of this pool. When a memory is shared, then
 * its ref count is > 1, which means it will not be released to the allocator
 * until the sub-memory is destroyed.
 *
 * When a memory returns to the allocator, the allocator fires the
 * omxbuf-released signal, which is handled by the buffer pool to return the
 * omx buffer to the port or the queue.
 */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_buffer_pool_debug_category, "omxbufferpool", 0, \
      "debug category for gst-omx buffer pool base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXBufferPool, gst_omx_buffer_pool,
    GST_TYPE_BUFFER_POOL, DEBUG_INIT);

static void gst_omx_buffer_pool_free_buffer (GstBufferPool * bpool,
    GstBuffer * buffer);

static gboolean
gst_omx_buffer_pool_start (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  gboolean has_buffers;
  GstStructure *config;
  guint min, max;
  GstOMXAllocatorForeignMemMode mode;

  /* Only allow to start the pool if we still are attached
   * to a component and port */
  GST_OBJECT_LOCK (pool);
  if (!pool->component || !pool->port) {
    GST_OBJECT_UNLOCK (pool);
    return FALSE;
  }

  pool->port->using_pool = TRUE;

  has_buffers = (pool->port->buffers != NULL);
  GST_OBJECT_UNLOCK (pool);

  config = gst_buffer_pool_get_config (bpool);
  gst_buffer_pool_config_get_params (config, NULL, NULL, &min, &max);
  gst_structure_free (config);
  if (max > min) {
    GST_WARNING_OBJECT (bpool,
        "max (%d) cannot be higher than min (%d) as pool cannot allocate buffers on the fly",
        max, min);
    return FALSE;
  }

  if (!has_buffers) {
    gboolean result = FALSE;

    GST_DEBUG_OBJECT (bpool, "Buffers not yet allocated on port %d of %s",
        pool->port->index, pool->component->name);

    g_signal_emit (pool, signals[SIG_ALLOCATE], 0, &result);

    if (!result) {
      GST_WARNING_OBJECT (bpool,
          "Element failed to allocate buffers, can't start pool");
      return FALSE;
    }
  }

  g_assert (pool->port->buffers);

  if (pool->other_pool)
    /* Importing buffers from downstream, either normal or dmabuf ones */
    mode = GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL;
  else if (pool->output_mode == GST_OMX_BUFFER_MODE_DMABUF)
    /* Exporting dmabuf */
    mode = GST_OMX_ALLOCATOR_FOREIGN_MEM_DMABUF;
  else
    /* Exporting normal buffers */
    mode = GST_OMX_ALLOCATOR_FOREIGN_MEM_NONE;

  if (!gst_omx_allocator_configure (pool->allocator, min, mode))
    return FALSE;

  if (!gst_omx_allocator_set_active (pool->allocator, TRUE))
    return FALSE;

  return
      GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->start (bpool);
}

static gboolean
gst_omx_buffer_pool_stop (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  /* Remove any buffers that are there */
  g_ptr_array_set_size (pool->buffers, 0);

  GST_DEBUG_OBJECT (pool, "deactivating OMX allocator");
  gst_omx_allocator_set_active (pool->allocator, FALSE);

  /* ensure all memories have been deallocated;
   * this may take a while if some memories are being shared
   * and therefore are in use somewhere else in the pipeline */
  gst_omx_allocator_wait_inactive (pool->allocator);

  GST_DEBUG_OBJECT (pool, "deallocate OMX buffers");
  gst_omx_port_deallocate_buffers (pool->port);

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = NULL;

  pool->add_videometa = FALSE;
  pool->deactivated = TRUE;
  pool->port->using_pool = TRUE;

  return GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->stop (bpool);
}

static const gchar **
gst_omx_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *raw_video_options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };
  static const gchar *options[] = { NULL };
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  GST_OBJECT_LOCK (pool);
  if (pool->port && pool->port->port_def.eDomain == OMX_PortDomainVideo
      && pool->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GST_OBJECT_UNLOCK (pool);
    return raw_video_options;
  }
  GST_OBJECT_UNLOCK (pool);

  return options;
}

static gboolean
gst_omx_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstCaps *caps;
  guint size, min;
  GstStructure *fake_config;
  gboolean ret;

  GST_OBJECT_LOCK (pool);

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  if (pool->port && pool->port->port_def.eDomain == OMX_PortDomainVideo
      && pool->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GstVideoInfo info;

    /* now parse the caps from the config */
    if (!gst_video_info_from_caps (&info, caps))
      goto wrong_video_caps;

    /* enable metadata based on config of the pool */
    pool->add_videometa =
        gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    pool->video_info = info;
  }

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = gst_caps_ref (caps);

  /* Ensure max=min as the pool won't be able to allocate more buffers while active */
  gst_buffer_pool_config_set_params (config, caps, size, min, min);

  GST_OBJECT_UNLOCK (pool);

  /* give a fake config to the parent default_set_config() with size == 0
   * this prevents default_release_buffer() from free'ing the buffers, since
   * we release them with no memory */
  fake_config = gst_structure_copy (config);
  gst_buffer_pool_config_set_params (fake_config, caps, 0, min, min);

  ret = GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->set_config
      (bpool, fake_config);
  gst_structure_free (fake_config);

  return ret;

  /* ERRORS */
wrong_config:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_video_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_omx_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstBuffer *buf;
  GstMemory *mem;
  GstMemory *foreign_mem = NULL;

  if (pool->other_pool) {
    guint n;

    buf = g_ptr_array_index (pool->buffers, pool->current_buffer_index);
    g_assert (pool->other_pool == buf->pool);
    gst_object_replace ((GstObject **) & buf->pool, NULL);

    n = gst_buffer_n_memory (buf);
    g_return_val_if_fail (n == 1, GST_FLOW_ERROR);

    /* rip the memory out of the buffer;
     * we like to keep them separate in this pool */
    foreign_mem = gst_buffer_get_memory (buf, 0);
    gst_buffer_remove_all_memory (buf);

    if (pool->add_videometa) {
      GstVideoMeta *meta;

      meta = gst_buffer_get_video_meta (buf);
      if (!meta) {
        gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (&pool->video_info),
            GST_VIDEO_INFO_WIDTH (&pool->video_info),
            GST_VIDEO_INFO_HEIGHT (&pool->video_info));
      }
    }

    pool->need_copy = FALSE;
  } else {
    const guint nstride = pool->port->port_def.format.video.nStride;
    const guint nslice = pool->port->port_def.format.video.nSliceHeight;
    gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
    gint stride[GST_VIDEO_MAX_PLANES] = { nstride, 0, };

    buf = gst_buffer_new ();

    switch (GST_VIDEO_INFO_FORMAT (&pool->video_info)) {
      case GST_VIDEO_FORMAT_ABGR:
      case GST_VIDEO_FORMAT_ARGB:
      case GST_VIDEO_FORMAT_RGB16:
      case GST_VIDEO_FORMAT_BGR16:
      case GST_VIDEO_FORMAT_YUY2:
      case GST_VIDEO_FORMAT_UYVY:
      case GST_VIDEO_FORMAT_YVYU:
      case GST_VIDEO_FORMAT_GRAY8:
        break;
      case GST_VIDEO_FORMAT_I420:
        stride[1] = nstride / 2;
        offset[1] = offset[0] + stride[0] * nslice;
        stride[2] = nstride / 2;
        offset[2] = offset[1] + (stride[1] * nslice / 2);
        break;
      case GST_VIDEO_FORMAT_NV12:
      case GST_VIDEO_FORMAT_NV12_10LE32:
      case GST_VIDEO_FORMAT_NV16:
      case GST_VIDEO_FORMAT_NV16_10LE32:
        stride[1] = nstride;
        offset[1] = offset[0] + stride[0] * nslice;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    if (pool->add_videometa) {
      pool->need_copy = FALSE;
    } else {
      GstVideoInfo info;
      gboolean need_copy = FALSE;
      gint i;

      gst_video_info_init (&info);
      gst_video_info_set_format (&info,
          GST_VIDEO_INFO_FORMAT (&pool->video_info),
          GST_VIDEO_INFO_WIDTH (&pool->video_info),
          GST_VIDEO_INFO_HEIGHT (&pool->video_info));

      for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&pool->video_info); i++) {
        if (info.stride[i] != stride[i] || info.offset[i] != offset[i]) {
          GST_DEBUG_OBJECT (pool,
              "Need to copy output frames because of stride/offset mismatch: plane %d stride %d (expected: %d) offset %"
              G_GSIZE_FORMAT " (expected: %" G_GSIZE_FORMAT
              ") nStride: %d nSliceHeight: %d ", i, stride[i], info.stride[i],
              offset[i], info.offset[i], nstride, nslice);

          need_copy = TRUE;
          break;
        }
      }

      pool->need_copy = need_copy;
    }

    if (pool->need_copy || pool->add_videometa) {
      /* We always add the videometa. It's the job of the user
       * to copy the buffer if pool->need_copy is TRUE
       */
      GstVideoMeta *meta;
      GstVideoAlignment align;

      meta = gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_INFO_FORMAT (&pool->video_info),
          GST_VIDEO_INFO_WIDTH (&pool->video_info),
          GST_VIDEO_INFO_HEIGHT (&pool->video_info),
          GST_VIDEO_INFO_N_PLANES (&pool->video_info), offset, stride);

      if (gst_omx_video_get_port_padding (pool->port, &pool->video_info,
              &align))
        gst_video_meta_set_alignment (meta, align);
    }
  }

  mem = gst_omx_allocator_allocate (pool->allocator, pool->current_buffer_index,
      foreign_mem);
  if (!mem)
    return GST_FLOW_ERROR;

  if (pool->output_mode == GST_OMX_BUFFER_MODE_DMABUF) {
    GstMapInfo map;

    if (!gst_caps_features_contains (gst_caps_get_features (pool->caps, 0),
            GST_CAPS_FEATURE_MEMORY_DMABUF)) {
      /* Check if the memory is actually mappable */
      if (!gst_memory_map (mem, &map, GST_MAP_READWRITE)) {
        GST_ERROR_OBJECT (pool,
            "dmabuf memory is not mappable but caps does not have the 'memory:DMABuf' feature");
        gst_memory_unref (mem);
        return GST_FLOW_ERROR;
      }

      gst_memory_unmap (mem, &map);
    }
  }

  /* mem still belongs to the allocator; do not add it in the buffer just yet */

  *buffer = buf;

  pool->current_buffer_index++;

  return GST_FLOW_OK;
}

/* called by the allocator when we are using other_pool in order
 * to restore the foreign GstMemory back to its original GstBuffer */
static void
on_allocator_foreign_mem_released (GstOMXAllocator * allocator,
    gint index, GstMemory * mem, GstOMXBufferPool * pool)
{
  GstBuffer *buf;

  buf = g_ptr_array_index (pool->buffers, index);
  gst_buffer_append_memory (buf, mem);

  /* the buffer consumed the passed reference.
   * we still need one more reference for the allocator */
  gst_memory_ref (mem);
}

static void
gst_omx_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  /* If the buffers belong to another pool, restore them now */
  GST_OBJECT_LOCK (pool);
  if (pool->other_pool) {
    gst_object_replace ((GstObject **) & buffer->pool,
        (GstObject *) pool->other_pool);
  }
  GST_OBJECT_UNLOCK (pool);

  GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->free_buffer (bpool,
      buffer);
}

static GstFlowReturn
gst_omx_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstMemory *mem;

  if (pool->port->port_def.eDir == OMX_DirOutput) {
    g_return_val_if_fail (pool->current_buffer_index != -1, GST_FLOW_ERROR);

    ret = gst_omx_allocator_acquire (pool->allocator, &mem,
        pool->current_buffer_index, NULL);
    if (ret != GST_FLOW_OK)
      return ret;

    /* If it's our own memory we have to set the sizes */
    if (!pool->other_pool) {
      GstOMXBuffer *omx_buf = gst_omx_memory_get_omx_buf (mem);
      mem->size = omx_buf->omx_buf->nFilledLen;
      mem->offset = omx_buf->omx_buf->nOffset;
    }
  } else {
    /* Acquire any buffer that is available to be filled by upstream */
    GstOMXBuffer *omx_buf;
    GstOMXAcquireBufferReturn r;
    GstOMXWait wait = GST_OMX_WAIT;

    if (params && (params->flags & GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT))
      wait = GST_OMX_DONT_WAIT;

    r = gst_omx_port_acquire_buffer (pool->port, &omx_buf, wait);
    if (r == GST_OMX_ACQUIRE_BUFFER_OK) {
      ret = gst_omx_allocator_acquire (pool->allocator, &mem, -1, omx_buf);
      if (ret != GST_FLOW_OK)
        return ret;
    } else if (r == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      return GST_FLOW_FLUSHING;
    } else {
      return GST_FLOW_ERROR;
    }
  }

  /* get some GstBuffer available in this pool */
  ret = GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->acquire_buffer
      (bpool, buffer, params);

  if (ret == GST_FLOW_OK) {
    /* attach the acquired memory on it */
    gst_buffer_append_memory (*buffer, mem);
  } else {
    gst_memory_unref (mem);
  }

  return ret;
}

static void
gst_omx_buffer_pool_reset_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  guint n;

  n = gst_buffer_n_memory (buffer);
  if (G_UNLIKELY (n != 1)) {
    GST_ERROR_OBJECT (pool, "Released buffer does not have 1 memory... "
        "(n = %u) something went terribly wrong", n);
  }

  /* rip the memory out of the buffer;
   * we like to keep them separate in this pool.
   * if this was the last ref count of the memory, it will be returned
   * to the allocator, otherwise it will be returned later */
  gst_buffer_remove_all_memory (buffer);

  /* reset before removing the TAG_MEMORY flag so that the parent impl
   * doesn't try to restore the original buffer size */
  GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->reset_buffer
      (bpool, buffer);

  /* pretend nothing happened to the memory to avoid discarding the buffer */
  GST_MINI_OBJECT_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
}

static void
on_allocator_omxbuf_released (GstOMXAllocator * allocator,
    GstOMXBuffer * omx_buf, GstOMXBufferPool * pool)
{
  OMX_ERRORTYPE err;

  if (pool->port->port_def.eDir == OMX_DirOutput && !omx_buf->used &&
      !pool->deactivated) {
    /* Release back to the port, can be filled again */
    err = gst_omx_port_release_buffer (pool->port, omx_buf);

    if (err != OMX_ErrorNone) {
      GST_ELEMENT_ERROR (pool->element, LIBRARY, SETTINGS, (NULL),
          ("Failed to relase output buffer to component: %s (0x%08x)",
              gst_omx_error_to_string (err), err));
    }
  } else if (pool->port->port_def.eDir == OMX_DirInput) {
    gst_omx_port_requeue_buffer (pool->port, omx_buf);
  }
}

static void
gst_omx_buffer_pool_finalize (GObject * object)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (object);

  if (pool->element)
    gst_object_unref (pool->element);
  pool->element = NULL;

  if (pool->buffers)
    g_ptr_array_unref (pool->buffers);
  pool->buffers = NULL;

  if (pool->other_pool)
    gst_object_unref (pool->other_pool);
  pool->other_pool = NULL;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = NULL;

  g_clear_pointer (&pool->component, gst_omx_component_unref);

  G_OBJECT_CLASS (gst_omx_buffer_pool_parent_class)->finalize (object);
}

static void
gst_omx_buffer_pool_class_init (GstOMXBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_omx_buffer_pool_finalize;
  gstbufferpool_class->start = gst_omx_buffer_pool_start;
  gstbufferpool_class->stop = gst_omx_buffer_pool_stop;
  gstbufferpool_class->get_options = gst_omx_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_omx_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_omx_buffer_pool_alloc_buffer;
  gstbufferpool_class->free_buffer = gst_omx_buffer_pool_free_buffer;
  gstbufferpool_class->acquire_buffer = gst_omx_buffer_pool_acquire_buffer;
  gstbufferpool_class->reset_buffer = gst_omx_buffer_pool_reset_buffer;

  signals[SIG_ALLOCATE] = g_signal_new ("allocate",
      G_TYPE_FROM_CLASS (gobject_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 0);
}

static void
gst_omx_buffer_pool_init (GstOMXBufferPool * pool)
{
  pool->buffers = g_ptr_array_new ();
}

GstBufferPool *
gst_omx_buffer_pool_new (GstElement * element, GstOMXComponent * component,
    GstOMXPort * port, GstOMXBufferMode output_mode)
{
  GstOMXBufferPool *pool;

  pool = g_object_new (gst_omx_buffer_pool_get_type (), NULL);
  pool->element = gst_object_ref (element);
  pool->component = gst_omx_component_ref (component);
  pool->port = port;
  pool->output_mode = output_mode;
  pool->allocator = gst_omx_allocator_new (component, port);

  g_signal_connect_object (pool->allocator, "omxbuf-released",
      (GCallback) on_allocator_omxbuf_released, pool, 0);
  g_signal_connect_object (pool->allocator, "foreign-mem-released",
      (GCallback) on_allocator_foreign_mem_released, pool, 0);

  return GST_BUFFER_POOL (pool);
}
