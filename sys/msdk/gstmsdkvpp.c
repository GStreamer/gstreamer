/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Author: Sreerenj Balachaandran <sreerenj.balachandran@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>

#include "gstmsdkvpp.h"
#include "gstmsdkbufferpool.h"
#include "gstmsdkvideomemory.h"
#include "gstmsdksystemmemory.h"
#include "gstmsdkcontextutil.h"
#include "gstmsdkvpputil.h"

#ifndef _WIN32
#include "gstmsdkallocator_libva.h"
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvpp_debug);
#define GST_CAT_DEFAULT gst_msdkvpp_debug

static GstStaticPadTemplate gst_msdkvpp_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ NV12, YV12, I420, YUY2, UYVY, BGRA, BGRx, P010_10LE }")
        ", " "interlace-mode = (string){ progressive, interleaved, mixed }" ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF,
            "{ NV12, BGRA, YUY2, UYVY, P010_10LE}")));

static GstStaticPadTemplate gst_msdkvpp_src_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF,
            "{ BGRA, YUY2, UYVY, NV12, BGRx, P010_10LE}") ";"
        GST_VIDEO_CAPS_MAKE ("{ BGRA, NV12, YUY2, UYVY, BGRx, P010_10LE }") ", "
        "interlace-mode = (string){ progressive, interleaved, mixed }" ";"));

enum
{
  PROP_0,
  PROP_HARDWARE,
  PROP_ASYNC_DEPTH,
  PROP_DENOISE,
  PROP_ROTATION,
  PROP_DEINTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_DETAIL,
  PROP_MIRRORING,
  PROP_SCALING_MODE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_FRC_ALGORITHM,
  PROP_N,
};

#define PROP_HARDWARE_DEFAULT            TRUE
#define PROP_ASYNC_DEPTH_DEFAULT         1
#define PROP_DENOISE_DEFAULT             0
#define PROP_ROTATION_DEFAULT            MFX_ANGLE_0
#define PROP_DEINTERLACE_MODE_DEFAULT    GST_MSDKVPP_DEINTERLACE_MODE_AUTO
#define PROP_DEINTERLACE_METHOD_DEFAULT  MFX_DEINTERLACING_BOB
#define PROP_HUE_DEFAULT                 0
#define PROP_SATURATION_DEFAULT          1
#define PROP_BRIGHTNESS_DEFAULT          0
#define PROP_CONTRAST_DEFAULT            1
#define PROP_DETAIL_DEFAULT              0
#define PROP_MIRRORING_DEFAULT           MFX_MIRRORING_DISABLED
#define PROP_SCALING_MODE_DEFAULT        MFX_SCALING_MODE_DEFAULT
#define PROP_FORCE_ASPECT_RATIO_DEFAULT  TRUE
#define PROP_FRC_ALGORITHM_DEFAULT       _MFX_FRC_ALGORITHM_NONE

#define gst_msdkvpp_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVPP, gst_msdkvpp, GST_TYPE_BASE_TRANSFORM);

typedef struct
{
  mfxFrameSurface1 *surface;
  GstBuffer *buf;
} MsdkSurface;

static void
free_msdk_surface (MsdkSurface * surface)
{
  if (surface->buf)
    gst_buffer_unref (surface->buf);
  g_slice_free (MsdkSurface, surface);
}

static void
gst_msdkvpp_add_extra_param (GstMsdkVPP * thiz, mfxExtBuffer * param)
{
  if (thiz->num_extra_params < MAX_EXTRA_PARAMS) {
    thiz->extra_params[thiz->num_extra_params] = param;
    thiz->num_extra_params++;
  }
}

static gboolean
ensure_context (GstBaseTransform * trans)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);

  if (gst_msdk_context_prepare (GST_ELEMENT_CAST (thiz), &thiz->context)) {
    GST_INFO_OBJECT (thiz, "Found context from neighbour %" GST_PTR_FORMAT,
        thiz->context);

    if (gst_msdk_context_get_job_type (thiz->context) & GST_MSDK_JOB_VPP) {
      GstMsdkContext *parent_context, *msdk_context;

      parent_context = thiz->context;
      msdk_context = gst_msdk_context_new_with_parent (parent_context);

      if (!msdk_context) {
        GST_ERROR_OBJECT (thiz, "Context creation failed");
        return FALSE;
      }

      thiz->context = msdk_context;
      gst_object_unref (parent_context);

      GST_INFO_OBJECT (thiz,
          "Creating new context %" GST_PTR_FORMAT " with joined session",
          thiz->context);
    } else {
      gst_msdk_context_add_job_type (thiz->context, GST_MSDK_JOB_VPP);
    }
  } else {
    if (!gst_msdk_context_ensure_context (GST_ELEMENT_CAST (thiz),
            thiz->hardware, GST_MSDK_JOB_VPP))
      return FALSE;
    GST_INFO_OBJECT (thiz, "Creating new context %" GST_PTR_FORMAT,
        thiz->context);
  }

  gst_msdk_context_add_shared_async_depth (thiz->context, thiz->async_depth);

  return TRUE;
}

static GstBuffer *
create_output_buffer (GstMsdkVPP * thiz)
{
  GstBuffer *outbuf;
  GstFlowReturn ret;
  GstBufferPool *pool = thiz->srcpad_buffer_pool;

  g_return_val_if_fail (pool != NULL, NULL);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE))
    goto error_activate_pool;

  outbuf = NULL;
  ret = gst_buffer_pool_acquire_buffer (pool, &outbuf, NULL);
  if (ret != GST_FLOW_OK || !outbuf)
    goto error_create_buffer;

  return outbuf;

  /* ERRORS */
error_activate_pool:
  {
    GST_ERROR_OBJECT (thiz, "failed to activate output video buffer pool");
    return NULL;
  }
error_create_buffer:
  {
    GST_ERROR_OBJECT (thiz, "failed to create output video buffer");
    return NULL;
  }
}

static GstFlowReturn
gst_msdkvpp_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf_ptr)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);

  if (gst_base_transform_is_passthrough (trans)) {
    *outbuf_ptr = inbuf;
    return GST_FLOW_OK;
  }

  *outbuf_ptr = create_output_buffer (thiz);
  return *outbuf_ptr ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static GstBufferPool *
gst_msdkvpp_create_buffer_pool (GstMsdkVPP * thiz, GstPadDirection direction,
    GstCaps * caps, guint min_num_buffers)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  GstVideoInfo *pool_info = NULL;
  GstVideoAlignment align;
  GstAllocationParams params = { 0, 31, 0, 0, };
  mfxFrameAllocResponse *alloc_resp = NULL;
  gboolean use_dmabuf = FALSE;

  if (direction == GST_PAD_SINK) {
    alloc_resp = &thiz->in_alloc_resp;
    pool_info = &thiz->sinkpad_buffer_pool_info;
    use_dmabuf = thiz->use_sinkpad_dmabuf;
  } else if (direction == GST_PAD_SRC) {
    alloc_resp = &thiz->out_alloc_resp;
    pool_info = &thiz->srcpad_buffer_pool_info;
    use_dmabuf = thiz->use_srcpad_dmabuf;
  }

  pool = gst_msdk_buffer_pool_new (thiz->context, alloc_resp);
  if (!pool)
    goto error_no_pool;

  if (!gst_video_info_from_caps (&info, caps))
    goto error_no_video_info;

  gst_msdk_set_video_alignment (&info, &align);
  gst_video_info_align (&info, &align);

  if (use_dmabuf)
    allocator =
        gst_msdk_dmabuf_allocator_new (thiz->context, &info, alloc_resp);
  else if (thiz->use_video_memory)
    allocator = gst_msdk_video_allocator_new (thiz->context, &info, alloc_resp);
  else
    allocator = gst_msdk_system_allocator_new (&info);

  if (!allocator)
    goto error_no_allocator;

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (config, caps, info.size, min_num_buffers,
      0);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  if (thiz->use_video_memory) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY);
    if (use_dmabuf)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_MSDK_USE_DMABUF);
  }

  gst_buffer_pool_config_set_video_alignment (config, &align);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_object_unref (allocator);

  if (!gst_buffer_pool_set_config (pool, config))
    goto error_pool_config;

  /* Updating pool_info with algined info of allocator */
  *pool_info = info;

  return pool;

error_no_pool:
  {
    GST_INFO_OBJECT (thiz, "Failed to create bufferpool");
    return NULL;
  }
error_no_video_info:
  {
    GST_INFO_OBJECT (thiz, "Failed to get Video info from caps");
    gst_object_unref (pool);
    return NULL;
  }
error_no_allocator:
  {
    GST_INFO_OBJECT (thiz, "Failed to create allocator");
    gst_object_unref (pool);
    return NULL;
  }
error_pool_config:
  {
    GST_INFO_OBJECT (thiz, "Failed to set config");
    gst_object_unref (pool);
    gst_object_unref (allocator);
    return NULL;
  }
}

static gboolean
_gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, i);
    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;
    if (gst_caps_features_contains (features, feature))
      return TRUE;
  }
  return FALSE;
}

static gboolean
gst_msdkvpp_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstCaps *caps;
  guint size = 0, min_buffers = 0, max_buffers = 0;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  gboolean update_pool = FALSE;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (thiz, "Failed to parse the decide_allocation caps");
    return FALSE;
  }
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (thiz, "Failed to get video info");
    return FALSE;
  }
  /* if downstream allocation query supports dmabuf-capsfeatures,
   * we do allocate dmabuf backed memory */
  if (_gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    GST_INFO_OBJECT (thiz, "MSDK VPP srcpad uses DMABuf memory");
    thiz->use_srcpad_dmabuf = TRUE;
  }

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL))
    thiz->add_video_meta = TRUE;
  else
    thiz->add_video_meta = FALSE;

  /* Check whether the query has pool */
  if (gst_query_get_n_allocation_pools (query) > 0)
    update_pool = TRUE;

  /* increase the min_buffers with number of concurrent vpp operations */
  min_buffers += thiz->async_depth;

  /* invalidate the cached pool if there is an allocation_query */
  if (thiz->srcpad_buffer_pool)
    gst_object_unref (thiz->srcpad_buffer_pool);

  /* Always create a pool for vpp out buffers. Each of the msdk element
   * has to create it's own mfxsurfacepool which is an msdk contraint.
   * For eg: Each Msdk component (vpp, dec and enc) will invoke the external
   * Frame allocator for video-memory usage.So sharing the pool between
   * gst-msdk elements might not be a good idea, rather each element
   * can check the buffer type (whether it is from msdk-buffer pool)
   * to make sure there is no copy. Since we share the context between
   * msdk elements, using buffers from one sdk's framealloator in another
   * sdk-components is perfectly fine */
  pool = gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SRC, caps, min_buffers);
  thiz->srcpad_buffer_pool = pool;

  /* get the configured pool properties inorder to set in query */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
      &max_buffers);
  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min_buffers,
        max_buffers);
  else
    gst_query_add_allocation_pool (query, pool, size, min_buffers, max_buffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  /* Fixme if downstream doesn't have videometa support, msdkvpp should
   * copy the output buffers */

  return TRUE;
}

static gboolean
gst_msdkvpp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  GstCaps *caps;
  GstStructure *config;
  gboolean need_pool;
  GstAllocationParams params;
  guint size;
  guint min_buffers = thiz->async_depth + 1;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (thiz, "Failed to parse the allocation caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (thiz, "Failed to get video info");
    return FALSE;
  }

  /* if upstream allocation query supports dmabuf-capsfeatures,
   * we do allocate dmabuf backed memory */
  if (_gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    GST_INFO_OBJECT (thiz, "MSDK VPP srcpad uses DMABuf memory");
    thiz->use_sinkpad_dmabuf = TRUE;
  }

  if (need_pool) {
    /* alwys provide a new pool for upstream to help re-negotiation
     * more info here: https://bugzilla.gnome.org/show_bug.cgi?id=748344 */
    pool = gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SINK, caps,
        min_buffers);
  }

  /* Update the internal pool if any allocation attribute changed */
  if (!gst_video_info_is_equal (&thiz->sinkpad_buffer_pool_info, &info)) {
    gst_object_unref (thiz->sinkpad_buffer_pool);
    thiz->sinkpad_buffer_pool = gst_msdkvpp_create_buffer_pool (thiz,
        GST_PAD_SINK, caps, min_buffers);
  }

  /* get the size and allocator params from configured pool and set it in query */
  if (!need_pool)
    pool = gst_object_ref (thiz->sinkpad_buffer_pool);
  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);
  gst_structure_free (config);

  /* if upstream does't have a pool requirement, set only
   *  size, min_buffers and max_buffers in query */
  gst_query_add_allocation_pool (query, need_pool ? pool : NULL, size,
      min_buffers, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);
}

static MsdkSurface *
get_surface_from_pool (GstMsdkVPP * thiz, GstBufferPool * pool,
    GstBufferPoolAcquireParams * params)
{
  GstBuffer *new_buffer;
  mfxFrameSurface1 *new_surface;
  MsdkSurface *msdk_surface;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (pool, "failed to activate buffer pool");
    return NULL;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &new_buffer, params) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (pool, "failed to acquire a buffer from pool");
    return NULL;
  }

  if (gst_msdk_is_msdk_buffer (new_buffer))
    new_surface = gst_msdk_get_surface_from_buffer (new_buffer);
  else {
    GST_ERROR_OBJECT (pool, "the acquired memory is not MSDK memory");
    return NULL;
  }

  msdk_surface = g_slice_new0 (MsdkSurface);
  msdk_surface->surface = new_surface;
  msdk_surface->buf = new_buffer;

  return msdk_surface;
}

#ifndef _WIN32
static gboolean
import_dmabuf_to_msdk_surface (GstMsdkVPP * thiz, GstBuffer * buf,
    MsdkSurface * msdk_surface)
{
  GstMemory *mem = NULL;
  GstVideoInfo vinfo;
  GstVideoMeta *vmeta;
  GstMsdkMemoryID *msdk_mid = NULL;
  mfxFrameSurface1 *mfx_surface = NULL;
  gint fd, i;

  mem = gst_buffer_peek_memory (buf, 0);
  fd = gst_dmabuf_memory_get_fd (mem);
  if (fd < 0)
    return FALSE;

  vinfo = thiz->sinkpad_info;

  /* Update offset/stride/size if there is VideoMeta attached to
   * the buffer */
  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta) {
    if (GST_VIDEO_INFO_FORMAT (&vinfo) != vmeta->format ||
        GST_VIDEO_INFO_WIDTH (&vinfo) != vmeta->width ||
        GST_VIDEO_INFO_HEIGHT (&vinfo) != vmeta->height ||
        GST_VIDEO_INFO_N_PLANES (&vinfo) != vmeta->n_planes) {
      GST_ERROR_OBJECT (thiz, "VideoMeta attached to buffer is not matching"
          "the negotiated width/height/format");
      return FALSE;
    }
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&vinfo); ++i) {
      GST_VIDEO_INFO_PLANE_OFFSET (&vinfo, i) = vmeta->offset[i];
      GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, i) = vmeta->stride[i];
    }
    GST_VIDEO_INFO_SIZE (&vinfo) = gst_buffer_get_size (buf);
  }

  /* Upstream neither accepted the msdk pool nor the msdk buffer size restrictions.
   * Current media-driver and GMMLib will fail due to strict memory size restrictions.
   * Ideally, media-driver should accept what ever memory coming from other drivers
   * in case of dmabuf-import and this is how the intel-vaapi-driver works.
   * For now, in order to avoid any crash we check the buffer size and fallback
   * to copy frame method.
   *
   * See this: https://github.com/intel/media-driver/issues/169
   * */
  if (GST_VIDEO_INFO_SIZE (&vinfo) <
      GST_VIDEO_INFO_SIZE (&thiz->sinkpad_buffer_pool_info))
    return FALSE;

  mfx_surface = msdk_surface->surface;
  msdk_mid = (GstMsdkMemoryID *) mfx_surface->Data.MemId;

  /* release the internal memory storage of associated mfxSurface */
  gst_msdk_replace_mfx_memid (thiz->context, mfx_surface, VA_INVALID_ID);

  /* export dmabuf to vasurface */
  if (!gst_msdk_export_dmabuf_to_vasurface (thiz->context, &vinfo, fd,
          msdk_mid->surface))
    return FALSE;

  return TRUE;
}
#endif

static MsdkSurface *
get_msdk_surface_from_input_buffer (GstMsdkVPP * thiz, GstBuffer * inbuf)
{
  GstVideoFrame src_frame, out_frame;
  MsdkSurface *msdk_surface;
  GstMemory *mem = NULL;

  if (gst_msdk_is_msdk_buffer (inbuf)) {
    msdk_surface = g_slice_new0 (MsdkSurface);
    msdk_surface->surface = gst_msdk_get_surface_from_buffer (inbuf);
    msdk_surface->buf = gst_buffer_ref (inbuf);
    return msdk_surface;
  }

  /* If upstream hasn't accpeted the proposed msdk bufferpool,
   * just copy frame (if not dmabuf backed) to msdk buffer and
   * take a surface from it.   */
  if (!(msdk_surface =
          get_surface_from_pool (thiz, thiz->sinkpad_buffer_pool, NULL)))
    goto error;

#ifndef _WIN32
  /************ dmabuf-import ************* */
  /* if upstream provided a dmabuf backed memory, but not an msdk
   * buffer, we could export the dmabuf to underlined vasurface */
  mem = gst_buffer_peek_memory (inbuf, 0);
  if (gst_is_dmabuf_memory (mem)) {
    if (import_dmabuf_to_msdk_surface (thiz, inbuf, msdk_surface))
      return msdk_surface;
    else
      GST_INFO_OBJECT (thiz, "Upstream dmabuf-backed memory is not imported"
          "to the msdk surface, fall back to the copy input frame method");
  }
#endif

  if (!gst_video_frame_map (&src_frame, &thiz->sinkpad_info, inbuf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (thiz, "failed to map the frame for source");
    goto error;
  }

  if (!gst_video_frame_map (&out_frame, &thiz->sinkpad_buffer_pool_info,
          msdk_surface->buf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (thiz, "failed to map the frame for destination");
    gst_video_frame_unmap (&src_frame);
    goto error;
  }

  if (!gst_video_frame_copy (&out_frame, &src_frame)) {
    GST_ERROR_OBJECT (thiz, "failed to copy frame");
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&src_frame);
    goto error;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&src_frame);

  return msdk_surface;

error:
  return NULL;
}

static GstFlowReturn
gst_msdkvpp_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_OK;
  mfxSession session;
  mfxSyncPoint sync_point = NULL;
  mfxStatus status;
  MsdkSurface *in_surface = NULL;
  MsdkSurface *out_surface = NULL;

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);

  in_surface = get_msdk_surface_from_input_buffer (thiz, inbuf);
  if (!in_surface)
    return GST_FLOW_ERROR;

  if (gst_msdk_is_msdk_buffer (outbuf)) {
    out_surface = g_slice_new0 (MsdkSurface);
    out_surface->surface = gst_msdk_get_surface_from_buffer (outbuf);
  } else {
    GST_ERROR ("Failed to get msdk outsurface!");
    return GST_FLOW_ERROR;
  }

  session = gst_msdk_context_get_session (thiz->context);

  /* outer loop is for handling FrameRate Control and deinterlace use cases */
  do {
    for (;;) {
      status =
          MFXVideoVPP_RunFrameVPPAsync (session, in_surface->surface,
          out_surface->surface, NULL, &sync_point);
      if (status != MFX_WRN_DEVICE_BUSY)
        break;
      /* If device is busy, wait 1ms and retry, as per MSDK's recommendation */
      g_usleep (1000);
    };

    if (status != MFX_ERR_NONE && status != MFX_ERR_MORE_DATA
        && status != MFX_ERR_MORE_SURFACE)
      goto vpp_error;

    /* No output generated */
    if (status == MFX_ERR_MORE_DATA)
      goto error_more_data;

    /* Wait for vpp operation to complete, the magic number 300000 below
     * is used in MSDK samples
     * #define MSDK_VPP_WAIT_INTERVAL 300000
     */
    if (sync_point &&
        MFXVideoCORE_SyncOperation (session, sync_point,
            300000) != MFX_ERR_NONE)
      GST_WARNING_OBJECT (thiz, "failed to do sync operation");

    /* More than one output buffers are generated */
    if (status == MFX_ERR_MORE_SURFACE) {
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      GST_BUFFER_DURATION (outbuf) = thiz->buffer_duration;
      timestamp += thiz->buffer_duration;
      ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (trans), outbuf);
      if (ret != GST_FLOW_OK)
        goto error_push_buffer;
      outbuf = create_output_buffer (thiz);
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      GST_BUFFER_DURATION (outbuf) = thiz->buffer_duration;
    }
  } while (status == MFX_ERR_MORE_SURFACE);

  free_msdk_surface (in_surface);
  return ret;

vpp_error:
  GST_ERROR_OBJECT (thiz, "MSDK Failed to do VPP");
  free_msdk_surface (in_surface);
  free_msdk_surface (out_surface);
  return GST_FLOW_ERROR;

error_more_data:
  GST_WARNING_OBJECT (thiz,
      "MSDK Requries additional input for processing, "
      "Retruning FLOW_DROPPED since no output buffer was generated");
  free_msdk_surface (in_surface);
  return GST_BASE_TRANSFORM_FLOW_DROPPED;

error_push_buffer:
  {
    free_msdk_surface (in_surface);
    free_msdk_surface (out_surface);
    GST_DEBUG_OBJECT (thiz, "failed to push output buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static void
gst_msdkvpp_close (GstMsdkVPP * thiz)
{
  mfxStatus status;

  if (!thiz->context)
    return;

  GST_DEBUG_OBJECT (thiz, "Closing VPP 0x%p", thiz->context);
  status = MFXVideoVPP_Close (gst_msdk_context_get_session (thiz->context));
  if (status != MFX_ERR_NONE && status != MFX_ERR_NOT_INITIALIZED) {
    GST_WARNING_OBJECT (thiz, "VPP close failed (%s)",
        msdk_status_to_string (status));
  }

  if (thiz->context)
    gst_object_replace ((GstObject **) & thiz->context, NULL);

  memset (&thiz->param, 0, sizeof (thiz->param));

  if (thiz->sinkpad_buffer_pool)
    gst_object_unref (thiz->sinkpad_buffer_pool);
  thiz->sinkpad_buffer_pool = NULL;
  if (thiz->srcpad_buffer_pool)
    gst_object_unref (thiz->srcpad_buffer_pool);
  thiz->srcpad_buffer_pool = NULL;

  thiz->buffer_duration = GST_CLOCK_TIME_NONE;
  gst_video_info_init (&thiz->sinkpad_info);
  gst_video_info_init (&thiz->srcpad_info);
}

static void
ensure_filters (GstMsdkVPP * thiz)
{

  /* Denoise */
  if (thiz->flags & GST_MSDK_FLAG_DENOISE) {
    mfxExtVPPDenoise *mfx_denoise = &thiz->mfx_denoise;
    mfx_denoise->Header.BufferId = MFX_EXTBUFF_VPP_DENOISE;
    mfx_denoise->Header.BufferSz = sizeof (mfxExtVPPDenoise);
    mfx_denoise->DenoiseFactor = thiz->denoise_factor;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_denoise);
  }

  /* Rotation */
  if (thiz->flags & GST_MSDK_FLAG_ROTATION) {
    mfxExtVPPRotation *mfx_rotation = &thiz->mfx_rotation;
    mfx_rotation->Header.BufferId = MFX_EXTBUFF_VPP_ROTATION;
    mfx_rotation->Header.BufferSz = sizeof (mfxExtVPPRotation);
    mfx_rotation->Angle = thiz->rotation;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_rotation);
  }

  /* Deinterlace */
  if (thiz->flags & GST_MSDK_FLAG_DEINTERLACE) {
    mfxExtVPPDeinterlacing *mfx_deinterlace = &thiz->mfx_deinterlace;
    mfx_deinterlace->Header.BufferId = MFX_EXTBUFF_VPP_DEINTERLACING;
    mfx_deinterlace->Header.BufferSz = sizeof (mfxExtVPPDeinterlacing);
    mfx_deinterlace->Mode = thiz->deinterlace_method;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_deinterlace);
  }

  /* Colorbalance(ProcAmp) */
  if (thiz->flags & (GST_MSDK_FLAG_HUE | GST_MSDK_FLAG_SATURATION |
          GST_MSDK_FLAG_BRIGHTNESS | GST_MSDK_FLAG_CONTRAST)) {
    mfxExtVPPProcAmp *mfx_procamp = &thiz->mfx_procamp;
    mfx_procamp->Header.BufferId = MFX_EXTBUFF_VPP_PROCAMP;
    mfx_procamp->Header.BufferSz = sizeof (mfxExtVPPProcAmp);
    mfx_procamp->Hue = thiz->hue;
    mfx_procamp->Saturation = thiz->saturation;
    mfx_procamp->Brightness = thiz->brightness;
    mfx_procamp->Contrast = thiz->contrast;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_procamp);
  }

  /* Detail/Edge enhancement */
  if (thiz->flags & GST_MSDK_FLAG_DETAIL) {
    mfxExtVPPDetail *mfx_detail = &thiz->mfx_detail;
    mfx_detail->Header.BufferId = MFX_EXTBUFF_VPP_DETAIL;
    mfx_detail->Header.BufferSz = sizeof (mfxExtVPPDetail);
    mfx_detail->DetailFactor = thiz->detail;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_detail);
  }

  /* Mirroring */
  if (thiz->flags & GST_MSDK_FLAG_MIRRORING) {
    mfxExtVPPMirroring *mfx_mirroring = &thiz->mfx_mirroring;
    mfx_mirroring->Header.BufferId = MFX_EXTBUFF_VPP_MIRRORING;
    mfx_mirroring->Header.BufferSz = sizeof (mfxExtVPPMirroring);
    mfx_mirroring->Type = thiz->mirroring;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_mirroring);
  }

  /* Scaling Mode */
  if (thiz->flags & GST_MSDK_FLAG_SCALING_MODE) {
    mfxExtVPPScaling *mfx_scaling = &thiz->mfx_scaling;
    mfx_scaling->Header.BufferId = MFX_EXTBUFF_VPP_SCALING;
    mfx_scaling->Header.BufferSz = sizeof (mfxExtVPPScaling);
    mfx_scaling->ScalingMode = thiz->scaling_mode;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_scaling);
  }

  /* FRC */
  if (thiz->flags & GST_MSDK_FLAG_FRC) {
    mfxExtVPPFrameRateConversion *mfx_frc = &thiz->mfx_frc;
    mfx_frc->Header.BufferId = MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION;
    mfx_frc->Header.BufferSz = sizeof (mfxExtVPPFrameRateConversion);
    mfx_frc->Algorithm = thiz->frc_algm;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_frc);
  }
}

static void
gst_msdkvpp_set_passthrough (GstMsdkVPP * thiz)
{
  gboolean passthrough = TRUE;

  /* no passthrough if any of the filter algorithm is enabled */
  if (thiz->flags)
    passthrough = FALSE;

  /* vpp could be needed in some specific circumstances, for eg:
   * input surface is dmabuf and output must be videomemory. So far
   * the underline iHD driver doesn't seems to support dmabuf mapping,
   * so we could explicitly ask msdkvpp to provide non-dambuf videomemory
   * surfaces as output thourgh capsfileters */
  if (thiz->need_vpp)
    passthrough = FALSE;

  /* no passthrough if there is change in out width,height or format */
  if (GST_VIDEO_INFO_WIDTH (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_WIDTH (&thiz->srcpad_info)
      || GST_VIDEO_INFO_HEIGHT (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_HEIGHT (&thiz->srcpad_info)
      || GST_VIDEO_INFO_FORMAT (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_FORMAT (&thiz->srcpad_info))
    passthrough = FALSE;

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (thiz), passthrough);
}

static gboolean
gst_msdkvpp_initialize (GstMsdkVPP * thiz)
{
  mfxSession session;
  mfxStatus status;
  mfxFrameAllocRequest request[2];

  if (!thiz->context) {
    GST_WARNING_OBJECT (thiz, "No MSDK Context");
    return FALSE;
  }

  GST_OBJECT_LOCK (thiz);
  session = gst_msdk_context_get_session (thiz->context);

  /* Close the current session if the session has been initialized,
   * otherwise the subsequent function call of MFXVideoVPP_Init() will
   * fail
   */
  if (thiz->initialized)
    MFXVideoVPP_Close (session);

  if (thiz->use_video_memory) {
    gst_msdk_set_frame_allocator (thiz->context);
    thiz->param.IOPattern =
        MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
  } else {
    thiz->param.IOPattern =
        MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
  }

  /* update input video attributes */
  gst_msdk_set_mfx_frame_info_from_video_info (&thiz->param.vpp.In,
      &thiz->sinkpad_info);

  /* update output video attributes, only CSC and Scaling are supported for now */
  gst_msdk_set_mfx_frame_info_from_video_info (&thiz->param.vpp.Out,
      &thiz->srcpad_info);

  /* use msdk frame rarte control if there is a mismatch in In & OUt fps  */
  if (GST_VIDEO_INFO_FPS_N (&thiz->srcpad_info) &&
      (GST_VIDEO_INFO_FPS_N (&thiz->sinkpad_info) !=
          GST_VIDEO_INFO_FPS_N (&thiz->srcpad_info)
          || GST_VIDEO_INFO_FPS_D (&thiz->sinkpad_info) !=
          GST_VIDEO_INFO_FPS_D (&thiz->srcpad_info))) {
    thiz->flags |= GST_MSDK_FLAG_FRC;
    /* So far this is the only algorithm which is working somewhat good */
    thiz->frc_algm = MFX_FRCALGM_PRESERVE_TIMESTAMP;
  }

  /* work-around to avoid zero fps in msdk structure */
  if (!thiz->param.vpp.In.FrameRateExtN)
    thiz->param.vpp.In.FrameRateExtN = 30;
  if (!thiz->param.vpp.Out.FrameRateExtN)
    thiz->param.vpp.Out.FrameRateExtN = thiz->param.vpp.In.FrameRateExtN;

  /* set vpp out picstruct as progressive if deinterlacing enabled */
  if (thiz->flags & GST_MSDK_FLAG_DEINTERLACE)
    thiz->param.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  /* Enable the required filters */
  ensure_filters (thiz);

  /* Add exteneded buffers */
  if (thiz->num_extra_params) {
    thiz->param.NumExtParam = thiz->num_extra_params;
    thiz->param.ExtParam = thiz->extra_params;
  }

  /* validate parameters and allow the Media SDK to make adjustments */
  status = MFXVideoVPP_Query (session, &thiz->param, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Video VPP Query failed (%s)",
        msdk_status_to_string (status));
    goto no_vpp;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Video VPP Query returned: %s",
        msdk_status_to_string (status));
  }

  status = MFXVideoVPP_QueryIOSurf (session, &thiz->param, request);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "VPP Query IO surfaces failed (%s)",
        msdk_status_to_string (status));
    goto no_vpp;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "VPP Query IO surfaces returned: %s",
        msdk_status_to_string (status));
  }

  if (thiz->use_video_memory) {
    /* Input surface pool pre-allocation */
    request[0].Type |= MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET;
    if (thiz->use_sinkpad_dmabuf)
      request[0].Type |= MFX_MEMTYPE_EXPORT_FRAME;
    gst_msdk_frame_alloc (thiz->context, &(request[0]), &thiz->in_alloc_resp);

    /* Output surface pool pre-allocation */
    request[1].Type |= MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET;
    if (thiz->use_srcpad_dmabuf)
      request[1].Type |= MFX_MEMTYPE_EXPORT_FRAME;
    gst_msdk_frame_alloc (thiz->context, &(request[1]), &thiz->out_alloc_resp);
  }

  thiz->in_num_surfaces = request[0].NumFrameSuggested;
  thiz->out_num_surfaces = request[1].NumFrameSuggested;


  status = MFXVideoVPP_Init (session, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Init failed (%s)", msdk_status_to_string (status));
    goto no_vpp;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Init returned: %s",
        msdk_status_to_string (status));
  }

  thiz->initialized = TRUE;
  GST_OBJECT_UNLOCK (thiz);
  return TRUE;

no_vpp:
  GST_OBJECT_UNLOCK (thiz);
  if (thiz->context)
    gst_object_replace ((GstObject **) & thiz->context, NULL);
  return FALSE;
}

static gboolean
gst_msdkvpp_set_caps (GstBaseTransform * trans, GstCaps * caps,
    GstCaps * out_caps)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstVideoInfo in_info, out_info;
  gboolean sinkpad_info_changed = FALSE;
  gboolean srcpad_info_changed = FALSE;
  gboolean deinterlace;

  if (gst_caps_get_features (caps, 0) != gst_caps_get_features (out_caps, 0))
    thiz->need_vpp = 1;

  gst_video_info_from_caps (&in_info, caps);
  gst_video_info_from_caps (&out_info, out_caps);

  if (!gst_video_info_is_equal (&in_info, &thiz->sinkpad_info))
    sinkpad_info_changed = TRUE;
  if (!gst_video_info_is_equal (&out_info, &thiz->srcpad_info))
    srcpad_info_changed = TRUE;

  if (!sinkpad_info_changed && !srcpad_info_changed && thiz->initialized)
    return TRUE;

  thiz->sinkpad_info = in_info;
  thiz->srcpad_info = out_info;
#ifndef _WIN32
  thiz->use_video_memory = TRUE;
#else
  thiz->use_video_memory = FALSE;
#endif

  /* check for deinterlace requirement */
  deinterlace = gst_msdkvpp_is_deinterlace_enabled (thiz, &in_info);
  if (deinterlace)
    thiz->flags |= GST_MSDK_FLAG_DEINTERLACE;

  thiz->buffer_duration = GST_VIDEO_INFO_FPS_N (&out_info) > 0 ?
      gst_util_uint64_scale (GST_SECOND, GST_VIDEO_INFO_FPS_D (&out_info),
      GST_VIDEO_INFO_FPS_N (&out_info)) : 0;

  if (!gst_msdkvpp_initialize (thiz))
    return FALSE;

  /* set passthrough according to filter operation change */
  gst_msdkvpp_set_passthrough (thiz);

  /* Ensure sinkpad buffer pool */
  thiz->sinkpad_buffer_pool =
      gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SINK, caps,
      thiz->in_num_surfaces);
  if (!thiz->sinkpad_buffer_pool) {
    GST_ERROR_OBJECT (thiz, "Failed to ensure the sinkpad buffer pool");
    return FALSE;
  }
  /* Ensure a srcpad buffer pool */
  thiz->srcpad_buffer_pool =
      gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SRC, out_caps,
      thiz->out_num_surfaces);
  if (!thiz->srcpad_buffer_pool) {
    GST_ERROR_OBJECT (thiz, "Failed to ensure the srcpad buffer pool");
    return FALSE;
  }

  return TRUE;
}

static gboolean
pad_can_dmabuf (GstMsdkVPP * thiz, GstPadDirection direction, GstCaps * filter)
{
  gboolean ret = FALSE;
  GstCaps *caps, *out_caps;
  GstPad *pad;
  GstBaseTransform *trans = GST_BASE_TRANSFORM (thiz);

  if (direction == GST_PAD_SRC)
    pad = GST_BASE_TRANSFORM_SRC_PAD (trans);
  else
    pad = GST_BASE_TRANSFORM_SINK_PAD (trans);

  /* make a copy of filter caps since we need to alter the structure
   * by adding dmabuf-capsfeatures */
  caps = gst_caps_copy (filter);
  gst_caps_set_features (caps, 0,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_DMABUF));

  out_caps = gst_pad_peer_query_caps (pad, caps);
  if (!out_caps)
    goto done;

  if (gst_caps_is_any (out_caps) || gst_caps_is_empty (out_caps)
      || out_caps == caps)
    goto done;

  if (_gst_caps_has_feature (out_caps, GST_CAPS_FEATURE_MEMORY_DMABUF))
    ret = TRUE;
done:
  if (caps)
    gst_caps_unref (caps);
  if (out_caps)
    gst_caps_unref (out_caps);
  return ret;
}

static GstCaps *
gst_msdkvpp_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstCaps *result = NULL;
  gboolean *use_dmabuf;

  if (direction == GST_PAD_SRC) {
    result = gst_caps_fixate (result);
    use_dmabuf = &thiz->use_sinkpad_dmabuf;
  } else {
    result = gst_msdkvpp_fixate_srccaps (thiz, caps, othercaps);
    use_dmabuf = &thiz->use_srcpad_dmabuf;
  }

  GST_DEBUG_OBJECT (trans, "fixated to %" GST_PTR_FORMAT, result);
  gst_caps_unref (othercaps);

  if (pad_can_dmabuf (thiz,
          direction == GST_PAD_SRC ? GST_PAD_SINK : GST_PAD_SRC, result)) {
    gst_caps_set_features (result, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DMABUF, NULL));
    *use_dmabuf = TRUE;
  }

  return result;
}

/* Generic code for now, requires changes in future when we
 * add hardware query for supported formats, Framerate control etc */
static GstCaps *
gst_msdkvpp_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *out_caps;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SRC)
    out_caps = gst_static_pad_template_get_caps (&gst_msdkvpp_sink_factory);
  else
    out_caps = gst_static_pad_template_get_caps (&gst_msdkvpp_src_factory);

  if (out_caps && filter) {
    GstCaps *intersection;

    intersection = gst_caps_intersect_full (out_caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (out_caps);
    out_caps = intersection;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, out_caps);
  return out_caps;
}

static gboolean
gst_msdkvpp_start (GstBaseTransform * trans)
{
  if (!ensure_context (trans))
    return FALSE;
  return TRUE;
}

static gboolean
gst_msdkvpp_stop (GstBaseTransform * trans)
{
  gst_msdkvpp_close (GST_MSDKVPP (trans));
  return TRUE;
}

static void
gst_msdkvpp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (object);

  switch (prop_id) {
    case PROP_HARDWARE:
      thiz->hardware = g_value_get_boolean (value);
      break;
    case PROP_ASYNC_DEPTH:
      thiz->async_depth = g_value_get_uint (value);
      break;
    case PROP_DENOISE:
      thiz->denoise_factor = g_value_get_uint (value);
      thiz->flags |= GST_MSDK_FLAG_DENOISE;
      break;
    case PROP_ROTATION:
      thiz->rotation = g_value_get_enum (value);
      thiz->flags |= GST_MSDK_FLAG_ROTATION;
      break;
    case PROP_DEINTERLACE_MODE:
      thiz->deinterlace_mode = g_value_get_enum (value);
      break;
    case PROP_DEINTERLACE_METHOD:
      thiz->deinterlace_method = g_value_get_enum (value);
      break;
    case PROP_HUE:
      thiz->hue = g_value_get_float (value);
      thiz->flags |= GST_MSDK_FLAG_HUE;
      break;
    case PROP_SATURATION:
      thiz->saturation = g_value_get_float (value);
      thiz->flags |= GST_MSDK_FLAG_SATURATION;
      break;
    case PROP_BRIGHTNESS:
      thiz->brightness = g_value_get_float (value);
      thiz->flags |= GST_MSDK_FLAG_BRIGHTNESS;
      break;
    case PROP_CONTRAST:
      thiz->contrast = g_value_get_float (value);
      thiz->flags |= GST_MSDK_FLAG_CONTRAST;
      break;
    case PROP_DETAIL:
      thiz->detail = g_value_get_uint (value);
      thiz->flags |= GST_MSDK_FLAG_DETAIL;
      break;
    case PROP_MIRRORING:
      thiz->mirroring = g_value_get_enum (value);
      thiz->flags |= GST_MSDK_FLAG_MIRRORING;
      break;
    case PROP_SCALING_MODE:
      thiz->scaling_mode = g_value_get_enum (value);
      thiz->flags |= GST_MSDK_FLAG_SCALING_MODE;
      break;
    case PROP_FORCE_ASPECT_RATIO:
      thiz->keep_aspect = g_value_get_boolean (value);
      break;
    case PROP_FRC_ALGORITHM:
      thiz->frc_algm = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_msdkvpp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (object);

  switch (prop_id) {
    case PROP_HARDWARE:
      g_value_set_boolean (value, thiz->hardware);
      break;
    case PROP_ASYNC_DEPTH:
      g_value_set_uint (value, thiz->async_depth);
      break;
    case PROP_DENOISE:
      g_value_set_uint (value, thiz->denoise_factor);
      break;
    case PROP_ROTATION:
      g_value_set_enum (value, thiz->rotation);
      break;
    case PROP_DEINTERLACE_MODE:
      g_value_set_enum (value, thiz->deinterlace_mode);
      break;
    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, thiz->deinterlace_method);
      break;
    case PROP_HUE:
      g_value_set_float (value, thiz->hue);
      break;
    case PROP_SATURATION:
      g_value_set_float (value, thiz->saturation);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_float (value, thiz->brightness);
      break;
    case PROP_CONTRAST:
      g_value_set_float (value, thiz->contrast);
      break;
    case PROP_DETAIL:
      g_value_set_uint (value, thiz->detail);
      break;
    case PROP_MIRRORING:
      g_value_set_enum (value, thiz->mirroring);
      break;
    case PROP_SCALING_MODE:
      g_value_set_enum (value, thiz->scaling_mode);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, thiz->keep_aspect);
      break;
    case PROP_FRC_ALGORITHM:
      g_value_set_enum (value, thiz->frc_algm);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_msdkvpp_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_msdkvpp_set_context (GstElement * element, GstContext * context)
{
  GstMsdkContext *msdk_context = NULL;
  GstMsdkVPP *thiz = GST_MSDKVPP (element);

  if (gst_msdk_context_get_context (context, &msdk_context)) {
    gst_object_replace ((GstObject **) & thiz->context,
        (GstObject *) msdk_context);
    gst_object_unref (msdk_context);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static void
gst_msdkvpp_class_init (GstMsdkVPPClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;
  GParamSpec *obj_properties[PROP_N] = { NULL, };

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_msdkvpp_set_property;
  gobject_class->get_property = gst_msdkvpp_get_property;
  gobject_class->finalize = gst_msdkvpp_finalize;

  element_class->set_context = gst_msdkvpp_set_context;

  gst_element_class_add_static_pad_template (element_class,
      &gst_msdkvpp_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &gst_msdkvpp_sink_factory);

  gst_element_class_set_static_metadata (element_class,
      "MSDK Video Postprocessor",
      "Filter/Converter/Video;Filter/Converter/Video/Scaler;"
      "Filter/Effect/Video;Filter/Effect/Video/Deinterlace",
      "A MediaSDK Video Postprocessing Filter",
      "Sreerenj Balachandrn <sreerenj.balachandran@intel.com>");

  trans_class->start = GST_DEBUG_FUNCPTR (gst_msdkvpp_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_msdkvpp_stop);
  trans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_msdkvpp_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_msdkvpp_fixate_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_msdkvpp_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_msdkvpp_transform);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_msdkvpp_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_msdkvpp_decide_allocation);
  trans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_msdkvpp_prepare_output_buffer);

  obj_properties[PROP_HARDWARE] =
      g_param_spec_boolean ("hardware", "Hardware", "Enable hardware VPP",
      PROP_HARDWARE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_ASYNC_DEPTH] =
      g_param_spec_uint ("async-depth", "Async Depth",
      "Depth of asynchronous pipeline",
      1, 1, PROP_ASYNC_DEPTH_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_DENOISE] =
      g_param_spec_uint ("denoise", "Denoising factor",
      "Denoising Factor",
      0, 100, PROP_DENOISE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_ROTATION] =
      g_param_spec_enum ("rotation", "Rotation",
      "Rotation Angle", gst_msdkvpp_rotation_get_type (),
      PROP_ROTATION_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_DEINTERLACE_MODE] =
      g_param_spec_enum ("deinterlace-mode", "Deinterlace Mode",
      "Deinterlace mode to use", gst_msdkvpp_deinterlace_mode_get_type (),
      PROP_DEINTERLACE_MODE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_DEINTERLACE_METHOD] =
      g_param_spec_enum ("deinterlace-method", "Deinterlace Method",
      "Deinterlace method to use", gst_msdkvpp_deinterlace_method_get_type (),
      PROP_DEINTERLACE_METHOD_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_HUE] =
      g_param_spec_float ("hue", "Hue",
      "The hue of the video",
      -180, 180, PROP_HUE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_SATURATION] =
      g_param_spec_float ("saturation", "Saturation",
      "The Saturation of the video",
      0, 10, PROP_SATURATION_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_BRIGHTNESS] =
      g_param_spec_float ("brightness", "Brightness",
      "The Brightness of the video",
      -100, 100, PROP_BRIGHTNESS_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_CONTRAST] =
      g_param_spec_float ("contrast", "Contrast",
      "The Contrast of the video",
      0, 10, PROP_CONTRAST_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_DETAIL] =
      g_param_spec_uint ("detail", "Detail",
      "The factor of detail/edge enhancement filter algorithm",
      0, 100, PROP_DETAIL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_MIRRORING] =
      g_param_spec_enum ("mirroring", "Mirroring",
      "The Mirroring type", gst_msdkvpp_mirroring_get_type (),
      PROP_MIRRORING_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_SCALING_MODE] =
      g_param_spec_enum ("scaling-mode", "Scaling Mode",
      "The Scaling mode to use", gst_msdkvpp_scaling_mode_get_type (),
      PROP_SCALING_MODE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_FORCE_ASPECT_RATIO] =
      g_param_spec_boolean ("force-aspect-ratio", "Force Aspect Ratio",
      "When enabled, scaling will respect original aspect ratio",
      PROP_FORCE_ASPECT_RATIO_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[PROP_FRC_ALGORITHM] =
      g_param_spec_enum ("frc-algorithm", "FrameRateControl Algorithm",
      "The Framerate Control Alogorithm to use",
      gst_msdkvpp_frc_algorithm_get_type (), PROP_FRC_ALGORITHM_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_N, obj_properties);
}

static void
gst_msdkvpp_init (GstMsdkVPP * thiz)
{
  thiz->initialized = FALSE;
  thiz->hardware = PROP_HARDWARE_DEFAULT;
  thiz->async_depth = PROP_ASYNC_DEPTH_DEFAULT;
  thiz->denoise_factor = PROP_DENOISE_DEFAULT;
  thiz->rotation = PROP_ROTATION_DEFAULT;
  thiz->deinterlace_mode = PROP_DEINTERLACE_MODE_DEFAULT;
  thiz->deinterlace_method = PROP_DEINTERLACE_METHOD_DEFAULT;
  thiz->buffer_duration = GST_CLOCK_TIME_NONE;
  thiz->hue = PROP_HUE_DEFAULT;
  thiz->saturation = PROP_SATURATION_DEFAULT;
  thiz->brightness = PROP_BRIGHTNESS_DEFAULT;
  thiz->contrast = PROP_CONTRAST_DEFAULT;
  thiz->detail = PROP_DETAIL_DEFAULT;
  thiz->mirroring = PROP_MIRRORING_DEFAULT;
  thiz->scaling_mode = PROP_SCALING_MODE_DEFAULT;
  thiz->keep_aspect = PROP_FORCE_ASPECT_RATIO_DEFAULT;
  thiz->frc_algm = PROP_FRC_ALGORITHM_DEFAULT;
  gst_video_info_init (&thiz->sinkpad_info);
  gst_video_info_init (&thiz->srcpad_info);
}
