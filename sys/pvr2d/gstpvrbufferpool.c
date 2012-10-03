/*
 * GStreamer
 * Copyright (c) 2010, Texas Instruments Incorporated
 * Copyright (c) 2011, Collabora Ltd
 *  @author: Edward Hervey <edward@collabora.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpvrbufferpool.h"

/* Debugging category */
#include <gst/gstinfo.h>

/* Helper functions */
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>


GST_DEBUG_CATEGORY_EXTERN (gst_debug_pvrvideosink);
#define GST_CAT_DEFAULT gst_debug_pvrvideosink

static void
gst_pvr_meta_free (GstPVRMeta * meta, GstBuffer * buffer)
{
  GstPVRVideoSink *pvrsink = (GstPVRVideoSink *) meta->sink;

  GST_LOG ("Releasing PVRMeta for buffer %p (src_mem:%p)",
      buffer, meta->src_mem);

  if (meta->src_mem) {
    PVR2DERROR pvr_error;

    GST_OBJECT_LOCK (pvrsink);
    if (pvrsink->dcontext == NULL || pvrsink->dcontext->pvr_context == NULL) {
      GST_OBJECT_UNLOCK (pvrsink);
      goto done;
    }
    pvr_error = PVR2DMemFree (pvrsink->dcontext->pvr_context, meta->src_mem);
    GST_OBJECT_UNLOCK (pvrsink);

    if (pvr_error != PVR2D_OK)
      GST_ERROR ("Failed to unwrap PVR memory buffer. Error : %s",
          gst_pvr2d_error_get_string (pvr_error));
  }

done:
  gst_pvrvideosink_untrack_buffer (pvrsink, buffer);
  gst_object_unref (pvrsink);
}

const GstMetaInfo *
gst_pvr_meta_get_info (void)
{
  static const GstMetaInfo *pvr_meta_info = NULL;

  if (g_once_init_enter (&pvr_meta_info)) {
    const GstMetaInfo *meta = gst_meta_register ("GstPVRMeta", "GstPVRMeta",
        sizeof (GstPVRMeta),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_pvr_meta_free,
        (GstMetaCopyFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&pvr_meta_info, meta);
  }
  return pvr_meta_info;

}

/* Wrap existing buffers */
GstPVRMeta *
gst_buffer_add_pvr_meta (GstBuffer * buffer, GstElement * sink)
{
  guint8 *data;
  gsize size;
  GstPVRMeta *meta;
  PVR2DERROR pvr_error;
  GstPVRVideoSink *pvrsink = (GstPVRVideoSink *) sink;

  g_return_val_if_fail (gst_buffer_n_memory (buffer) > 0, NULL);
  g_return_val_if_fail (pvrsink != NULL, NULL);

  GST_LOG_OBJECT (pvrsink, "Adding PVRMeta to buffer %p", buffer);

  /* Add the meta */
  meta = (GstPVRMeta *) gst_buffer_add_meta (buffer, GST_PVR_META_INFO, NULL);
  meta->src_mem = NULL;
  meta->sink = gst_object_ref (pvrsink);
  gst_pvrvideosink_track_buffer (pvrsink, buffer);

  data = gst_buffer_map (buffer, &size, NULL, GST_MAP_READ);

  GST_LOG_OBJECT (pvrsink, "data:%p, size:%" G_GSIZE_FORMAT, data, size);

  GST_OBJECT_LOCK (pvrsink);
  if (pvrsink->dcontext == NULL || pvrsink->dcontext->pvr_context == NULL)
    goto no_pvr_context;
  /* Map the memory and wrap it */
  pvr_error =
      PVR2DMemWrap (pvrsink->dcontext->pvr_context, data, 0, size, NULL,
      &(meta->src_mem));
  GST_OBJECT_UNLOCK (pvrsink);

  gst_buffer_unmap (buffer, data, size);

  if (pvr_error != PVR2D_OK)
    goto wrap_error;

  return meta;

wrap_error:
  {
    GST_WARNING_OBJECT (pvrsink, "Failed to Wrap buffer memory. Error : %s",
        gst_pvr2d_error_get_string (pvr_error));
    gst_buffer_remove_meta (buffer, (GstMeta *) meta);

    return NULL;
  }

no_pvr_context:
  {
    GST_OBJECT_UNLOCK (pvrsink);
    GST_WARNING_OBJECT (pvrsink, "No PVR2D context available");
    gst_buffer_remove_meta (buffer, (GstMeta *) meta);
    return NULL;
  }
}

/*
 * GstDucatiBufferPool
 */
static void gst_pvr_buffer_pool_finalize (GObject * object);

#define gst_pvr_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstPVRBufferPool, gst_pvr_buffer_pool, GST_TYPE_BUFFER_POOL);
static const gchar **
pvr_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
pvr_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstPVRBufferPool *pvrpool = GST_PVR_BUFFER_POOL_CAST (pool);
  GstVideoInfo info;
  guint size, align;
  gboolean ret;
  const GstCaps *caps;

  if (!gst_buffer_pool_config_get (config, &caps, &size, NULL, NULL, NULL,
          &align))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, size:%u, align:%u caps %" GST_PTR_FORMAT,
      info.width, info.height, size, align, caps);

  if (pvrpool->caps)
    gst_caps_unref (pvrpool->caps);
  pvrpool->caps = gst_caps_copy (caps);
  pvrpool->info = info;
  pvrpool->size = size;
  gst_allocation_params_init (&pvrpool->params);
  pvrpool->params.align = align;
  pvrpool->padded_width = GST_VIDEO_INFO_WIDTH (&info);
  pvrpool->padded_height = GST_VIDEO_INFO_HEIGHT (&info);

  /* enable metadata based on config of the pool */
  pvrpool->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

#if 0
  /* parse extra alignment info */
  priv->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    GST_LOG_OBJECT (pool, "padding %u-%ux%u-%u", priv->align.padding_top,
        priv->align.padding_left, priv->align.padding_left,
        priv->align.padding_bottom);

    /* we need the video metadata too now */
    priv->add_metavideo = TRUE;
  }

  /* add the padding */
  priv->padded_width =
      GST_VIDEO_INFO_WIDTH (&info) + priv->align.padding_left +
      priv->align.padding_right;
  priv->padded_height =
      GST_VIDEO_INFO_HEIGHT (&info) + priv->align.padding_top +
      priv->align.padding_bottom;
#endif

  GST_DEBUG_OBJECT (pool, "before calling parent class");

  ret = GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  GST_DEBUG_OBJECT (pool, "parent_class returned %d", ret);

  return ret;

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

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstFlowReturn
pvr_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstPVRBufferPool *pvrpool = GST_PVR_BUFFER_POOL_CAST (pool);
  GstVideoInfo *info;
  GstBuffer *pvr;
  GstPVRMeta *meta;

  info = &pvrpool->info;

  pvr = gst_buffer_new_allocate (NULL, pvrpool->size, &pvrpool->params);
  meta = gst_buffer_add_pvr_meta (pvr, pvrpool->pvrsink);
  if (meta == NULL) {
    gst_buffer_unref (pvr);
    goto no_buffer;
  }

  if (pvrpool->add_metavideo) {
    GstVideoMeta *meta;

    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
    /* these are just the defaults for now */
    meta = gst_buffer_add_video_meta (pvr, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), pvrpool->padded_width,
        pvrpool->padded_height);
    if (G_UNLIKELY (meta == NULL))
      GST_WARNING_OBJECT (pool, "Failed to add GstVideoMeta");

#if 0
    const GstVideoFormatInfo *vinfo = info->finfo;
    gint i;

    if (pvrpool->need_alignment) {
      meta->width = GST_VIDEO_INFO_WIDTH (&pvrpool->info);
      meta->height = GST_VIDEO_INFO_HEIGHT (&pvrpool->info);

      /* FIXME, not quite correct, NV12 would apply the vedge twice on the second
       * plane */
      for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (info); i++) {
        gint vedge, hedge, plane;

        hedge =
            GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (vinfo, i,
            pvrpool->align.padding_left);
        vedge =
            GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (vinfo, i,
            pvrpool->align.padding_top);
        plane = GST_VIDEO_FORMAT_INFO_PLANE (vinfo, i);

        GST_LOG_OBJECT (pool, "comp %d, plane %d: hedge %d, vedge %d", i,
            plane, hedge, vedge);

        meta->offset[plane] += (vedge * meta->stride[plane]) + hedge;
      }
    }
#endif
  }

  *buffer = pvr;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create image");
    return GST_FLOW_ERROR;
  }
}

/** create new bufferpool
 */
GstBufferPool *
gst_pvr_buffer_pool_new (GstElement * pvrsink)
{
  GstPVRBufferPool *pool;

  g_return_val_if_fail (GST_IS_PVRVIDEOSINK (pvrsink), NULL);

  GST_DEBUG_OBJECT (pvrsink, "Creating new GstPVRBufferPool");

  pool = g_object_new (GST_TYPE_PVR_BUFFER_POOL, NULL);
  pool->pvrsink = gst_object_ref (pvrsink);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_pvr_buffer_pool_class_init (GstPVRBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_pvr_buffer_pool_finalize;

  gstbufferpool_class->get_options = pvr_buffer_pool_get_options;
  gstbufferpool_class->set_config = pvr_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = pvr_buffer_pool_alloc;
}

static void
gst_pvr_buffer_pool_init (GstPVRBufferPool * pool)
{

}

static void
gst_pvr_buffer_pool_finalize (GObject * object)
{
  GstPVRBufferPool *pool = GST_PVR_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (pool, "finalize PVR buffer pool %p", pool);

  if (pool->caps)
    gst_caps_unref (pool->caps);
  gst_object_unref (pool->pvrsink);

  G_OBJECT_CLASS (gst_pvr_buffer_pool_parent_class)->finalize (object);
}
