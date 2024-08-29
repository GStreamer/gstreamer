/*
 * GStreamer
 * Copyright © 2024 Advanced Micro Devices, Inc.
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

#include <ext/gl/gstgldmabufbufferpool.h>

#include <gst/allocators/gstdmabuf.h>
#include <gst/gl/gstglcontext.h>
#include <gst/gl/gstglfuncs.h>
#include <gst/gl/gstglmemory.h>
#include <gst/gl/gstglsyncmeta.h>
#include <gst/gl/egl/gsteglimage.h>

#define GST_GL_DMABUF_EGLIMAGE "gst.gl.dmabuf.eglimage"

typedef struct _GstGLDMABufBufferPoolPrivate
{
  GstBufferPool *dmabuf_pool;
  GstGLMemoryAllocator *allocator;
  GstGLVideoAllocationParams *glparams;

  gboolean add_glsyncmeta;
} GstGLDMABufBufferPoolPrivate;

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_DMABUF_BUFFER_POOL);
#define GST_CAT_DEFAULT GST_CAT_GL_DMABUF_BUFFER_POOL

#define gst_gl_dmabuf_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLDMABufBufferPool, gst_gl_dmabuf_buffer_pool,
    GST_TYPE_GL_BUFFER_POOL, G_ADD_PRIVATE (GstGLDMABufBufferPool)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_DMABUF_BUFFER_POOL,
        "gldmabufbufferpool", 0, "GL-DMABuf Buffer Pool"));

static gboolean
gst_gl_dmabuf_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (pool);

  GstStructure *dma_config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstGLAllocationParams *glparams;
  GstVideoAlignment video_align = { 0 };
  GstCaps *caps;
  guint size;
  guint min;
  guint max;
  guint i;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    goto wrong_config;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &alloc_params)) {
    goto wrong_config;
  }

  gst_clear_object (&self->priv->allocator);

  if (allocator) {
    if (!GST_IS_GL_MEMORY_ALLOCATOR (allocator)) {
      gst_clear_object (&allocator);
      goto wrong_allocator;
    } else {
      self->priv->allocator = gst_object_ref (allocator);
    }
  } else {
    self->priv->allocator =
        gst_gl_memory_allocator_get_default (GST_GL_BUFFER_POOL
        (pool)->context);
  }

  /*
   * This alignment is needed by nearly all AMD GPUs and should work fine on
   * Mali as well. To my knowledge there is no API to query it at runtime, so
   * it has to be hardcoded here. Users of the pool can still override the
   * values with GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT added to the config.
   */
  for (i = 0; i != GST_VIDEO_MAX_PLANES; ++i) {
    video_align.stride_align[i] = 256 - 1;
  }

  if (!gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &video_align);
  }

  gst_buffer_pool_config_get_video_alignment (config, &video_align);
  alloc_params.align = MAX (alloc_params.align, video_align.stride_align[0]);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);

  glparams = gst_buffer_pool_config_get_gl_allocation_params (config);
  if (glparams) {
    g_clear_pointer (&glparams->alloc_params, gst_allocation_params_free);
    glparams->alloc_params = gst_allocation_params_copy (&alloc_params);
    gst_buffer_pool_config_set_gl_allocation_params (config, glparams);
    g_clear_pointer (&glparams, gst_gl_allocation_params_free);
  }

  self->priv->add_glsyncmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_GL_SYNC_META);

  /* Now configure the dma-buf pool, and sync the config */
  dma_config = gst_buffer_pool_get_config (self->priv->dmabuf_pool);
  gst_buffer_pool_config_set_params (dma_config, caps, size, min, max);
  gst_buffer_pool_config_add_option (dma_config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (dma_config, &video_align);

  if (!gst_buffer_pool_set_config (self->priv->dmabuf_pool, dma_config)) {
    dma_config = gst_buffer_pool_get_config (self->priv->dmabuf_pool);

    if (!gst_buffer_pool_config_validate_params (dma_config, caps, size, min,
            max)) {
      gst_structure_free (config);
      return FALSE;
    }

    if (!gst_buffer_pool_config_get_params (dma_config, &caps, &size, &min,
            &max)) {
      gst_structure_free (dma_config);
      goto wrong_config;
    }

    if (!gst_buffer_pool_set_config (self->priv->dmabuf_pool, dma_config))
      return FALSE;

    gst_buffer_pool_config_set_params (config, caps, size, min, max);
  }

  if (!GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config)) {
    return FALSE;
  }

  g_clear_pointer ((GstGLAllocationParams **) & self->priv->glparams,
      gst_gl_allocation_params_free);
  self->priv->glparams = (GstGLVideoAllocationParams *)
      gst_gl_buffer_pool_get_gl_allocation_params (GST_GL_BUFFER_POOL (pool));

  self->priv->glparams->parent.alloc_flags |=
      GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_GPU_HANDLE;

  return TRUE;

wrong_config:
  {
    GST_WARNING_OBJECT (pool, "Incorrect config for this pool");
    return FALSE;
  }
wrong_allocator:
  {
    GST_WARNING_OBJECT (pool, "Incorrect allocator type for this pool");
    return FALSE;
  }
}

static gboolean
gst_gl_dmabuf_buffer_pool_start (GstBufferPool * pool)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (pool);

  if (!gst_buffer_pool_set_active (self->priv->dmabuf_pool, TRUE)) {
    return FALSE;
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
}

static gboolean
gst_gl_dmabuf_buffer_pool_stop (GstBufferPool * pool)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (pool);

  if (!gst_buffer_pool_set_active (self->priv->dmabuf_pool, FALSE)) {
    return FALSE;
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

typedef struct
{
  GstEGLImage *eglimage[GST_VIDEO_MAX_PLANES];
  gpointer gpuhandle[GST_VIDEO_MAX_PLANES];
  guint n_planes;
} WrapDMABufData;

static void
_wrap_dmabuf_eglimage (GstGLContext * context, gpointer data)
{
  WrapDMABufData *d = data;
  const GstGLFuncs *gl = context->gl_vtable;
  GLuint tex_ids[GST_VIDEO_MAX_PLANES];
  guint i;

  gl->GenTextures (d->n_planes, tex_ids);

  for (i = 0; i < d->n_planes; ++i) {
    gl->BindTexture (GL_TEXTURE_2D, tex_ids[i]);
    gl->EGLImageTargetTexture2D (GL_TEXTURE_2D,
        gst_egl_image_get_image (d->eglimage[i]));

    d->gpuhandle[i] = GUINT_TO_POINTER (tex_ids[i]);
  }
}

static GstFlowReturn
gst_gl_dmabuf_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (pool);
  GstGLBufferPool *glpool = GST_GL_BUFFER_POOL (pool);

  GstBuffer *buf;

  if (!(buf = gst_buffer_new ())) {
    goto no_buffer;
  }

  if (self->priv->add_glsyncmeta) {
    gst_buffer_add_gl_sync_meta (glpool->context, buf);
  }

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (self, "Could not create DMABuf buffer");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_gl_dmabuf_buffer_pool_acquire_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (pool);
  GstGLBufferPool *glpool = GST_GL_BUFFER_POOL (pool);

  GstVideoInfo *v_info = self->priv->glparams->v_info;
  GstFlowReturn ret;
  GstBuffer *dmabuf;
  GstBuffer *buf;
  WrapDMABufData data;
  guint i;

  ret = gst_buffer_pool_acquire_buffer (self->priv->dmabuf_pool, &dmabuf, NULL);
  if (ret != GST_FLOW_OK) {
    goto no_buffer;
  }

  data.n_planes = GST_VIDEO_INFO_N_PLANES (v_info);

  for (i = 0; i < data.n_planes; ++i) {
    guint mem_idx, length;
    gsize skip;
    GstMemory *dmabufmem;

    if (!gst_buffer_find_memory (dmabuf, GST_VIDEO_INFO_PLANE_OFFSET (v_info,
                i), 1, &mem_idx, &length, &skip)) {
      GST_WARNING_OBJECT (self, "Could not find memory for plane %d", i);
      return GST_FLOW_ERROR;
    }

    dmabufmem = gst_buffer_peek_memory (dmabuf, mem_idx);

    g_assert (gst_is_dmabuf_memory (dmabufmem));

    data.eglimage[i] = gst_egl_image_from_dmabuf (glpool->context,
        gst_dmabuf_memory_get_fd (dmabufmem), v_info, i, skip);
  }

  gst_gl_context_thread_add (glpool->context, _wrap_dmabuf_eglimage, &data);

  ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (pool, &buf,
      params);
  if (ret != GST_FLOW_OK) {
    return GST_FLOW_ERROR;
  }

  if (!gst_gl_memory_setup_buffer (self->priv->allocator, buf,
          self->priv->glparams, NULL, data.gpuhandle, data.n_planes)) {
    goto mem_create_failed;
  }

  for (i = 0; i < data.n_planes; ++i) {
    GstMemory *mem = gst_buffer_peek_memory (buf, i);

    /* Unset wrapped flag, we want the texture be freed with the memory. */
    GST_GL_MEMORY_CAST (mem)->texture_wrapped = FALSE;

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
        g_quark_from_static_string (GST_GL_DMABUF_EGLIMAGE),
        data.eglimage[i], (GDestroyNotify) gst_egl_image_unref);
  }

  gst_buffer_add_parent_buffer_meta (buf, dmabuf);
  gst_clear_buffer (&dmabuf);

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (self, "Could not create DMABuf buffer");
    return GST_FLOW_ERROR;
  }
mem_create_failed:
  {
    GST_WARNING_OBJECT (self, "Could not create GL Memory");
    return GST_FLOW_ERROR;
  }
}

static void
gst_gl_dmabuf_buffer_pool_reset_buffer (GstBufferPool * pool,
    GstBuffer * buffer)
{
  gst_buffer_remove_all_memory (buffer);

  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);
}

gboolean
gst_is_gl_dmabuf_buffer (GstBuffer * buffer)
{
  return GST_IS_GL_DMABUF_BUFFER_POOL (buffer->pool);
}

GstBuffer *
gst_gl_dmabuf_buffer_unwrap (GstBuffer * buffer)
{
  GstGLDMABufBufferPool *pool;
  GstParentBufferMeta *meta;
  GstBuffer *wrapped_dmabuf = NULL;

  g_return_val_if_fail (gst_is_gl_dmabuf_buffer (buffer), NULL);

  pool = GST_GL_DMABUF_BUFFER_POOL (buffer->pool);

  if (gst_buffer_peek_memory (buffer, 0)->allocator !=
      GST_ALLOCATOR (pool->priv->allocator)) {
    return NULL;
  }

  meta = gst_buffer_get_parent_buffer_meta (buffer);

  if (meta && meta->buffer) {
    wrapped_dmabuf = gst_buffer_ref (meta->buffer);

    gst_buffer_remove_meta (buffer, (GstMeta *) meta);

    gst_buffer_copy_into (wrapped_dmabuf, buffer, GST_BUFFER_COPY_FLAGS |
        GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META, 0, -1);
  }

  return wrapped_dmabuf;
}

GstBufferPool *
gst_gl_dmabuf_buffer_pool_new (GstGLContext * context,
    GstBufferPool * dmabuf_pool)
{
  GstGLDMABufBufferPool *pool;

  pool = g_object_new (GST_TYPE_GL_DMABUF_BUFFER_POOL, NULL);
  gst_object_ref_sink (pool);

  GST_GL_BUFFER_POOL (pool)->context = gst_object_ref (context);

  pool->priv->dmabuf_pool = gst_object_ref (dmabuf_pool);

  GST_LOG_OBJECT (pool, "new GL-DMABuf buffer pool for pool %" GST_PTR_FORMAT
      " and context %" GST_PTR_FORMAT, dmabuf_pool, context);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_gl_dmabuf_buffer_pool_init (GstGLDMABufBufferPool * pool)
{
  pool->priv = gst_gl_dmabuf_buffer_pool_get_instance_private (pool);
}

static void
gst_gl_dmabuf_buffer_pool_finalize (GObject * object)
{
  GstGLDMABufBufferPool *self = GST_GL_DMABUF_BUFFER_POOL (object);

  GST_LOG_OBJECT (self, "finalize GL-DMABuf buffer pool");

  gst_clear_object (&self->priv->dmabuf_pool);
  g_clear_pointer ((GstGLAllocationParams **) & self->priv->glparams,
      gst_gl_allocation_params_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_dmabuf_buffer_pool_class_init (GstGLDMABufBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_gl_dmabuf_buffer_pool_finalize;

  gstbufferpool_class->set_config = gst_gl_dmabuf_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_gl_dmabuf_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_gl_dmabuf_buffer_pool_acquire_buffer;
  gstbufferpool_class->reset_buffer = gst_gl_dmabuf_buffer_pool_reset_buffer;
  gstbufferpool_class->start = gst_gl_dmabuf_buffer_pool_start;
  gstbufferpool_class->stop = gst_gl_dmabuf_buffer_pool_stop;
}
