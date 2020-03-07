/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstd3d11bufferpool.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_d3d11_buffer_pool_debug

struct _GstD3D11BufferPoolPrivate
{
  GstD3D11Device *device;
  GstD3D11Allocator *allocator;

  /* initial buffer used for calculating buffer size */
  GstBuffer *initial_buffer;

  gboolean add_videometa;
  GstD3D11AllocationParams *d3d11_params;
};

#define gst_d3d11_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11BufferPool,
    gst_d3d11_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_d3d11_buffer_pool_dispose (GObject * object);
static const gchar **gst_d3d11_buffer_pool_get_options (GstBufferPool * pool);
static gboolean gst_d3d11_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static GstFlowReturn gst_d3d11_buffer_pool_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static void gst_d3d11_buffer_pool_flush_start (GstBufferPool * pool);
static void gst_d3d11_buffer_pool_flush_stop (GstBufferPool * pool);

static void
gst_d3d11_buffer_pool_class_init (GstD3D11BufferPoolClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->dispose = gst_d3d11_buffer_pool_dispose;

  bufferpool_class->get_options = gst_d3d11_buffer_pool_get_options;
  bufferpool_class->set_config = gst_d3d11_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_d3d11_buffer_pool_alloc;
  bufferpool_class->flush_start = gst_d3d11_buffer_pool_flush_start;
  bufferpool_class->flush_stop = gst_d3d11_buffer_pool_flush_stop;

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_buffer_pool_debug, "d3d11bufferpool", 0,
      "d3d11bufferpool object");
}

static void
gst_d3d11_buffer_pool_init (GstD3D11BufferPool * self)
{
  self->priv = gst_d3d11_buffer_pool_get_instance_private (self);
}

static void
gst_d3d11_buffer_pool_dispose (GObject * object)
{
  GstD3D11BufferPool *self = GST_D3D11_BUFFER_POOL (object);
  GstD3D11BufferPoolPrivate *priv = self->priv;

  if (priv->d3d11_params)
    gst_d3d11_allocation_params_free (priv->d3d11_params);
  priv->d3d11_params = NULL;

  gst_clear_buffer (&priv->initial_buffer);
  gst_clear_object (&priv->device);
  gst_clear_object (&priv->allocator);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const gchar **
gst_d3d11_buffer_pool_get_options (GstBufferPool * pool)
{
  /* NOTE: d3d11 memory does not support alignment */
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };

  return options;
}

static gboolean
gst_d3d11_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstD3D11BufferPool *self = GST_D3D11_BUFFER_POOL (pool);
  GstD3D11BufferPoolPrivate *priv = self->priv;
  GstVideoInfo info;
  GstCaps *caps = NULL;
  guint min_buffers, max_buffers;
  GstAllocator *allocator = NULL;
  gboolean ret = TRUE;
  D3D11_TEXTURE2D_DESC *desc;
  gint i;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
    goto wrong_config;

  gst_clear_buffer (&priv->initial_buffer);
  gst_clear_object (&priv->allocator);

  if (allocator) {
    if (!GST_IS_D3D11_ALLOCATOR (allocator)) {
      goto wrong_allocator;
    } else {
      priv->allocator = gst_object_ref (allocator);
    }
  } else {
    priv->allocator = gst_d3d11_allocator_new (priv->device);
    g_assert (priv->allocator);
  }

  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (priv->d3d11_params)
    gst_d3d11_allocation_params_free (priv->d3d11_params);
  priv->d3d11_params =
      gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!priv->d3d11_params) {
    /* allocate memory with resource format by default */
    priv->d3d11_params =
        gst_d3d11_allocation_params_new (priv->device, &info, 0, 0);
  }

  desc = priv->d3d11_params->desc;

  /* resolution of semi-planar formats must be multiple of 2 */
  if (desc[0].Format == DXGI_FORMAT_NV12 || desc[0].Format == DXGI_FORMAT_P010
      || desc[0].Format == DXGI_FORMAT_P016) {
    if (desc[0].Width % 2 || desc[0].Height % 2) {
      gint width, height;
      GstVideoAlignment align;

      GST_WARNING_OBJECT (self, "Resolution %dx%d is not mutiple of 2, fixing",
          desc[0].Width, desc[0].Height);

      width = GST_ROUND_UP_2 (desc[0].Width);
      height = GST_ROUND_UP_2 (desc[0].Height);

      gst_video_alignment_reset (&align);
      align.padding_right = width - desc[0].Width;
      align.padding_bottom = height - desc[0].Height;

      gst_d3d11_allocation_params_alignment (priv->d3d11_params, &align);
    }
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    GST_LOG_OBJECT (self, "Direct3D11 Allocation params");
    GST_LOG_OBJECT (self, "\tD3D11AllocationFlags: 0x%x",
        priv->d3d11_params->flags);
    for (i = 0; GST_VIDEO_MAX_PLANES; i++) {
      if (desc[i].Format == DXGI_FORMAT_UNKNOWN)
        break;
      GST_LOG_OBJECT (self, "\t[plane %d] %dx%d, DXGI format %d",
          i, desc[i].Width, desc[i].Height, desc[i].Format);
      GST_LOG_OBJECT (self, "\t[plane %d] MipLevel %d, ArraySize %d",
          i, desc[i].MipLevels, desc[i].ArraySize);
      GST_LOG_OBJECT (self,
          "\t[plane %d] SampleDesc.Count %d, SampleDesc.Quality %d",
          i, desc[i].SampleDesc.Count, desc[i].SampleDesc.Quality);
      GST_LOG_OBJECT (self, "\t[plane %d] Usage %d", i, desc[i].Usage);
      GST_LOG_OBJECT (self,
          "\t[plane %d] BindFlags 0x%x", i, desc[i].BindFlags);
      GST_LOG_OBJECT (self,
          "\t[plane %d] CPUAccessFlags 0x%x", i, desc[i].CPUAccessFlags);
      GST_LOG_OBJECT (self,
          "\t[plane %d] MiscFlags 0x%x", i, desc[i].MiscFlags);
    }
  }
#endif

  if ((priv->d3d11_params->flags & GST_D3D11_ALLOCATION_FLAG_TEXTURE_ARRAY)) {
    guint max_array_size = 0;

    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (desc[i].Format == DXGI_FORMAT_UNKNOWN)
        break;

      if (desc[i].ArraySize > max_array_size)
        max_array_size = desc[i].ArraySize;
    }

    if (max_buffers == 0 || max_buffers > max_array_size) {
      GST_WARNING_OBJECT (pool,
          "Array pool is requested but allowed pool size %d > ArraySize %d",
          max_buffers, max_array_size);
      max_buffers = max_array_size;
    }
  }

  gst_d3d11_buffer_pool_alloc (pool, &priv->initial_buffer, NULL);

  if (!priv->initial_buffer) {
    GST_ERROR_OBJECT (pool, "Could not create initial buffer");
    return FALSE;
  }

  self->buffer_size = gst_buffer_get_size (priv->initial_buffer);

  gst_buffer_pool_config_set_params (config,
      caps, self->buffer_size, min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config) && ret;

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
wrong_allocator:
  {
    GST_WARNING_OBJECT (pool, "Incorrect allocator type for this pool");
    return FALSE;
  }
}

static GstFlowReturn
gst_d3d11_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstD3D11BufferPool *self = GST_D3D11_BUFFER_POOL (pool);
  GstD3D11BufferPoolPrivate *priv = self->priv;
  GstMemory *mem;
  GstBuffer *buf;
  GstD3D11AllocationParams *d3d11_params = priv->d3d11_params;
  GstVideoInfo *info = &d3d11_params->info;
  GstVideoInfo *aligned_info = &d3d11_params->aligned_info;
  gint n_texture = 0;
  gint i;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };

  /* consume pre-allocated buffer if any */
  if (G_UNLIKELY (priv->initial_buffer)) {
    *buffer = priv->initial_buffer;
    priv->initial_buffer = NULL;

    return GST_FLOW_OK;
  }

  buf = gst_buffer_new ();

  if (d3d11_params->d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (n_texture = 0; n_texture < GST_VIDEO_INFO_N_PLANES (info); n_texture++) {
      d3d11_params->plane = n_texture;
      mem = gst_d3d11_allocator_alloc (priv->allocator, d3d11_params);
      if (!mem)
        goto error;

      gst_buffer_append_memory (buf, mem);
    }
  } else {
    d3d11_params->plane = 0;
    mem = gst_d3d11_allocator_alloc (priv->allocator, priv->d3d11_params);
    n_texture++;

    if (!mem)
      goto error;

    gst_buffer_append_memory (buf, mem);
  }

  /* calculate offset */
  for (i = 0; i < n_texture && i < GST_VIDEO_MAX_PLANES - 1; i++) {
    offset[i + 1] = offset[i] +
        d3d11_params->stride[i] * GST_VIDEO_INFO_COMP_HEIGHT (aligned_info, i);
  }

  if (priv->add_videometa) {
    GST_DEBUG_OBJECT (self, "adding GstVideoMeta");
    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        offset, d3d11_params->stride);
  }

  *buffer = buf;

  return GST_FLOW_OK;

error:
  gst_buffer_unref (buf);

  GST_ERROR_OBJECT (self, "cannot create texture memory");

  return GST_FLOW_ERROR;
}

static void
gst_d3d11_buffer_pool_flush_start (GstBufferPool * pool)
{
  GstD3D11BufferPool *self = GST_D3D11_BUFFER_POOL (pool);
  GstD3D11BufferPoolPrivate *priv = self->priv;

  if (priv->allocator)
    gst_d3d11_allocator_set_flushing (priv->allocator, TRUE);
}

static void
gst_d3d11_buffer_pool_flush_stop (GstBufferPool * pool)
{
  GstD3D11BufferPool *self = GST_D3D11_BUFFER_POOL (pool);
  GstD3D11BufferPoolPrivate *priv = self->priv;

  if (priv->allocator)
    gst_d3d11_allocator_set_flushing (priv->allocator, FALSE);
}

GstBufferPool *
gst_d3d11_buffer_pool_new (GstD3D11Device * device)
{
  GstD3D11BufferPool *pool;
  GstD3D11Allocator *alloc;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  pool = g_object_new (GST_TYPE_D3D11_BUFFER_POOL, NULL);
  alloc = gst_d3d11_allocator_new (device);

  pool->priv->device = gst_object_ref (device);
  pool->priv->allocator = alloc;

  return GST_BUFFER_POOL_CAST (pool);
}

GstD3D11AllocationParams *
gst_buffer_pool_config_get_d3d11_allocation_params (GstStructure * config)
{
  GstD3D11AllocationParams *ret;

  if (!gst_structure_get (config, "d3d11-allocation-params",
          GST_TYPE_D3D11_ALLOCATION_PARAMS, &ret, NULL))
    ret = NULL;

  return ret;
}

void
gst_buffer_pool_config_set_d3d11_allocation_params (GstStructure * config,
    GstD3D11AllocationParams * params)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (params != NULL);

  gst_structure_set (config, "d3d11-allocation-params",
      GST_TYPE_D3D11_ALLOCATION_PARAMS, params, NULL);
}
