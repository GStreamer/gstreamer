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
  GstCaps *caps;

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

static void
gst_d3d11_buffer_pool_class_init (GstD3D11BufferPoolClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->dispose = gst_d3d11_buffer_pool_dispose;

  bufferpool_class->get_options = gst_d3d11_buffer_pool_get_options;
  bufferpool_class->set_config = gst_d3d11_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_d3d11_buffer_pool_alloc;

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
  gst_clear_object (&priv->device);
  gst_clear_object (&priv->allocator);
  gst_clear_caps (&priv->caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const gchar **
gst_d3d11_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT,
    NULL
  };
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
  guint max_align, n;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  gboolean ret = TRUE;
  D3D11_TEXTURE2D_DESC *desc;

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

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &alloc_params))
    goto wrong_config;

  gst_caps_replace (&priv->caps, caps);

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  if (allocator) {
    if (!GST_IS_D3D11_ALLOCATOR (allocator)) {
      gst_object_unref (allocator);
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
  if (!priv->d3d11_params)
    priv->d3d11_params = gst_d3d11_allocation_params_new (&alloc_params,
        &info, NULL);

  desc = &priv->d3d11_params->desc;

  GST_LOG_OBJECT (self, "Direct3D11 Allocation params");
  GST_LOG_OBJECT (self, "\t%dx%d, DXGI format %d",
      desc->Width, desc->Height, desc->Format);
  GST_LOG_OBJECT (self, "\tMipLevel %d, ArraySize %d",
      desc->MipLevels, desc->ArraySize);
  GST_LOG_OBJECT (self, "\tSampleDesc.Count %d, SampleDesc.Quality %d",
      desc->SampleDesc.Count, desc->SampleDesc.Quality);
  GST_LOG_OBJECT (self, "\tUsage %d", desc->Usage);
  GST_LOG_OBJECT (self, "\tBindFlags 0x%x", desc->BindFlags);
  GST_LOG_OBJECT (self, "\tCPUAccessFlags 0x%x", desc->CPUAccessFlags);
  GST_LOG_OBJECT (self, "\tMiscFlags 0x%x", desc->MiscFlags);

  max_align = alloc_params.align;

  if (gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    priv->add_videometa = TRUE;

    gst_buffer_pool_config_get_video_alignment (config,
        &priv->d3d11_params->align);

    for (n = 0; n < GST_VIDEO_MAX_PLANES; ++n)
      max_align |= priv->d3d11_params->align.stride_align[n];

    for (n = 0; n < GST_VIDEO_MAX_PLANES; ++n)
      priv->d3d11_params->align.stride_align[n] = max_align;

    gst_video_info_align (&priv->d3d11_params->info,
        &priv->d3d11_params->align);

    gst_buffer_pool_config_set_video_alignment (config,
        &priv->d3d11_params->align);
  }

  if (alloc_params.align < max_align) {
    GST_WARNING_OBJECT (pool, "allocation params alignment %u is smaller "
        "than the max specified video stride alignment %u, fixing",
        (guint) alloc_params.align, max_align);

    alloc_params.align = priv->d3d11_params->parent.align = max_align;
    gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
    gst_allocation_params_copy (&alloc_params);
  }

  gst_buffer_pool_config_set_params (config,
      caps, GST_VIDEO_INFO_SIZE (&priv->d3d11_params->info), min_buffers,
      max_buffers);

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
  GstVideoInfo *info = &priv->d3d11_params->info;

  mem = gst_d3d11_allocator_alloc (priv->allocator, priv->d3d11_params);

  if (!mem) {
    GST_ERROR_OBJECT (self, "cannot create texture memory");
    return GST_FLOW_ERROR;
  }

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  if (priv->add_videometa) {
    GstD3D11Memory *dmem = (GstD3D11Memory *) mem;

    GST_DEBUG_OBJECT (self, "adding GstVideoMeta");
    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        dmem->offset, dmem->stride);
  }

  *buffer = buf;

  return GST_FLOW_OK;
}

GstD3D11BufferPool *
gst_d3d11_buffer_pool_new (GstD3D11Device * device)
{
  GstD3D11BufferPool *pool;
  GstD3D11Allocator *alloc;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  pool = g_object_new (GST_TYPE_D3D11_BUFFER_POOL, NULL);
  alloc = gst_d3d11_allocator_new (device);

  pool->priv->device = gst_object_ref (device);
  pool->priv->allocator = alloc;

  return pool;
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
