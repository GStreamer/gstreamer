/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gstvapool.h"

#include "gstvaallocator.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_pool_debug);
#define GST_CAT_DEFAULT gst_va_pool_debug

struct _GstVaPool
{
  GstBufferPool parent;

  GstVideoInfo alloc_info;
  GstVideoInfo caps_info;
  GstAllocator *allocator;
  guint32 usage_hint;
  gboolean add_videometa;
  gboolean need_alignment;
  GstVideoAlignment video_align;
};

#define gst_va_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaPool, gst_va_pool, GST_TYPE_BUFFER_POOL,
    GST_DEBUG_CATEGORY_INIT (gst_va_pool_debug, "vapool", 0, "VA Pool"));

static const gchar **
gst_va_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };
  return options;
}

static inline gboolean
gst_buffer_pool_config_get_va_allocation_params (GstStructure * config,
    guint32 * usage_hint)
{
  if (!gst_structure_get (config, "usage-hint", G_TYPE_UINT, usage_hint, NULL))
    *usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;

  return TRUE;
}

static gboolean
gst_va_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstAllocator *allocator;
  GstCaps *caps;
  GstVaPool *vpool = GST_VA_POOL (pool);
  GstVideoAlignment video_align = { 0, };
  GstVideoInfo caps_info, alloc_info;
  gint width, height;
  guint size, min_buffers, max_buffers;
  guint32 usage_hint;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!caps)
    goto no_caps;

  if (!gst_video_info_from_caps (&caps_info, caps))
    goto wrong_caps;

  if (size < GST_VIDEO_INFO_SIZE (&caps_info))
    goto wrong_size;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
    goto wrong_config;

  if (!(allocator && (GST_IS_VA_DMABUF_ALLOCATOR (allocator)
              || GST_IS_VA_ALLOCATOR (allocator))))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_va_allocation_params (config, &usage_hint))
    goto wrong_config;

  width = GST_VIDEO_INFO_WIDTH (&caps_info);
  height = GST_VIDEO_INFO_HEIGHT (&caps_info);

  GST_LOG_OBJECT (vpool, "%dx%d - %u | caps %" GST_PTR_FORMAT, width, height,
      size, caps);

  if (vpool->allocator)
    gst_object_unref (vpool->allocator);
  if ((vpool->allocator = allocator))
    gst_object_ref (allocator);

  /* enable metadata based on config of the pool */
  vpool->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  vpool->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (vpool->need_alignment && vpool->add_videometa) {
    gst_buffer_pool_config_get_video_alignment (config, &video_align);

    width += video_align.padding_left + video_align.padding_right;
    height += video_align.padding_bottom + video_align.padding_top;

    /* apply the alignment to the info */
    if (!gst_video_info_align (&caps_info, &video_align))
      goto failed_to_align;

    gst_buffer_pool_config_set_video_alignment (config, &video_align);
  }

  GST_VIDEO_INFO_SIZE (&caps_info) =
      MAX (size, GST_VIDEO_INFO_SIZE (&caps_info));

  alloc_info = caps_info;
  GST_VIDEO_INFO_WIDTH (&alloc_info) = width;
  GST_VIDEO_INFO_HEIGHT (&alloc_info) = height;

  vpool->caps_info = caps_info;
  vpool->alloc_info = alloc_info;
  vpool->usage_hint = usage_hint;
  vpool->video_align = video_align;

  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (&caps_info), min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (vpool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (vpool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (vpool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
wrong_size:
  {
    GST_WARNING_OBJECT (vpool,
        "Provided size is to small for the caps: %u < %" G_GSIZE_FORMAT, size,
        GST_VIDEO_INFO_SIZE (&caps_info));
    return FALSE;
  }
failed_to_align:
  {
    GST_WARNING_OBJECT (pool, "Failed to align");
    return FALSE;
  }
}

static GstFlowReturn
gst_va_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstBuffer *buf;
  GstVideoMeta *vmeta;
  GstVaPool *vpool = GST_VA_POOL (pool);
  GstVaAllocationParams alloc_params = {
    .info = vpool->alloc_info,
    .usage_hint = vpool->usage_hint,
  };

  buf = gst_buffer_new ();

  if (GST_IS_VA_DMABUF_ALLOCATOR (vpool->allocator)) {
    if (!gst_va_dmabuf_setup_buffer (vpool->allocator, buf, &alloc_params))
      goto no_memory;
  } else if (GST_IS_VA_ALLOCATOR (vpool->allocator)) {
    GstMemory *mem = gst_va_allocator_alloc (vpool->allocator, &alloc_params);
    if (!mem)
      goto no_memory;
    gst_buffer_append_memory (buf, mem);
  } else
    goto no_memory;

  if (vpool->add_videometa) {
    /* GstVaAllocator may update offset/stride given the physical
     * memory */
    vmeta = gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&vpool->caps_info),
        GST_VIDEO_INFO_WIDTH (&vpool->caps_info),
        GST_VIDEO_INFO_HEIGHT (&vpool->caps_info),
        GST_VIDEO_INFO_N_PLANES (&vpool->caps_info),
        alloc_params.info.offset, alloc_params.info.stride);

    if (vpool->need_alignment)
      gst_video_meta_set_alignment (vmeta, vpool->video_align);
  }

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
no_memory:
  {
    gst_buffer_unref (buf);
    GST_WARNING_OBJECT (vpool, "can't create memory");
    return GST_FLOW_ERROR;
  }
}

static void
gst_va_pool_dispose (GObject * object)
{
  GstVaPool *pool = GST_VA_POOL (object);

  GST_LOG_OBJECT (pool, "finalize video buffer pool %p", pool);

  gst_clear_object (&pool->allocator);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_pool_class_init (GstVaPoolClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *gstbufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->dispose = gst_va_pool_dispose;

  gstbufferpool_class->get_options = gst_va_pool_get_options;
  gstbufferpool_class->set_config = gst_va_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_va_pool_alloc;
}

static void
gst_va_pool_init (GstVaPool * self)
{
}

GstBufferPool *
gst_va_pool_new (void)
{
  GstVaPool *pool;

  pool = g_object_new (GST_TYPE_VA_POOL, NULL);
  gst_object_ref_sink (pool);

  GST_LOG_OBJECT (pool, "new va video buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

void
gst_buffer_pool_config_set_va_allocation_params (GstStructure * config,
    guint usage_hint)
{
  gst_structure_set (config, "usage-hint", G_TYPE_UINT, usage_hint, NULL);
}
