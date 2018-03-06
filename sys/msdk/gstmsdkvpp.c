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
#include "msdk-enums.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvpp_debug);
#define GST_CAT_DEFAULT gst_msdkvpp_debug

static GstStaticPadTemplate gst_msdkvpp_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, I420, YUY2, UYVY, BGRA }")
        ", " "interlace-mode = (string){ progressive, interleaved, mixed }"));

static GstStaticPadTemplate gst_msdkvpp_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, BGRA }") ", "
        "interlace-mode = (string){ progressive, interleaved, mixed }"));

enum
{
  PROP_0,
  PROP_HARDWARE,
  PROP_ASYNC_DEPTH,
  PROP_DENOISE,
  PROP_ROTATION,
  PROP_DEINTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_N,
};

#define PROP_HARDWARE_DEFAULT            TRUE
#define PROP_ASYNC_DEPTH_DEFAULT         1
#define PROP_DENOISE_DEFAULT             0
#define PROP_ROTATION_DEFAULT            MFX_ANGLE_0
#define PROP_DEINTERLACE_MODE_DEFAULT    GST_MSDKVPP_DEINTERLACE_MODE_AUTO
#define PROP_DEINTERLACE_METHOD_DEFAULT  MFX_DEINTERLACING_BOB

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
      GstMsdkContext *parent_context;

      parent_context = thiz->context;
      thiz->context = gst_msdk_context_new_with_parent (parent_context);
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

  if (direction == GST_PAD_SINK) {
    alloc_resp = &thiz->in_alloc_resp;
    pool_info = &thiz->sinkpad_buffer_pool_info;
  } else if (direction == GST_PAD_SRC) {
    alloc_resp = &thiz->out_alloc_resp;
    pool_info = &thiz->srcpad_buffer_pool_info;
  }

  pool = gst_msdk_buffer_pool_new (thiz->context, alloc_resp);
  if (!pool)
    goto error_no_pool;

  if (!gst_video_info_from_caps (&info, caps))
    goto error_no_video_info;

  gst_msdk_set_video_alignment (&info, &align);
  gst_video_info_align (&info, &align);

  if (thiz->use_video_memory)
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
  if (thiz->use_video_memory)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY);

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
    return NULL;
  }
error_no_allocator:
  {
    GST_INFO_OBJECT (thiz, "Failed to create allocator");
    if (pool)
      gst_object_unref (pool);
    return NULL;
  }
error_pool_config:
  {
    GST_INFO_OBJECT (thiz, "Failed to set config");
    if (pool)
      gst_object_unref (pool);
    if (allocator)
      gst_object_unref (allocator);
    return NULL;
  }
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

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL))
    thiz->add_video_meta = TRUE;
  else
    thiz->add_video_meta = FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min_buffers,
        &max_buffers);
    update_pool = TRUE;
    size = MAX (size, GST_VIDEO_INFO_SIZE (&info));

    if (pool && !GST_IS_MSDK_BUFFER_POOL (pool)) {
      GST_INFO_OBJECT (thiz, "ignoring non-msdk pool: %" GST_PTR_FORMAT, pool);
      g_clear_object (&pool);
    }
  }

  if (!pool) {
    gst_object_unref (thiz->srcpad_buffer_pool);
    pool =
        gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SRC, caps, min_buffers);
    thiz->srcpad_buffer_pool = pool;

    /* get the configured pool properties inorder to set in query */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
        &max_buffers);
    if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
      gst_query_add_allocation_param (query, allocator, &params);
    gst_structure_free (config);
  }

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
  guint size;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (thiz, "Failed to parse the allocation caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (thiz, "Failed to get video info");
    return FALSE;
  }

  size = MAX (info.size, GST_VIDEO_INFO_SIZE (&thiz->sinkpad_buffer_pool_info));

  /* We already created a pool while setting the caps
   * just to make sure the pipeline works even if there is
   * no allocation query from upstream (theoratical ??).Provide the
   * same pool in query if required/possible */
  if (!gst_video_info_is_equal (&thiz->sinkpad_buffer_pool_info, &info)) {
    gst_object_unref (thiz->sinkpad_buffer_pool);
    thiz->sinkpad_buffer_pool =
        gst_msdkvpp_create_buffer_pool (thiz, GST_PAD_SINK, caps,
        thiz->in_num_surfaces);
  }

  pool = thiz->sinkpad_buffer_pool;

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);
  gst_structure_free (config);

  /* if upstream does't have a pool requirement, set only
   *  size, min_buffers and max_buffers in query */
  if (!need_pool)
    pool = NULL;

  gst_query_add_allocation_pool (query, pool, size, thiz->in_num_surfaces, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

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

static MsdkSurface *
get_msdk_surface_from_input_buffer (GstMsdkVPP * thiz, GstBuffer * inbuf)
{
  GstVideoFrame src_frame, out_frame;
  MsdkSurface *msdk_surface;

  if (gst_msdk_is_msdk_buffer (inbuf)) {
    msdk_surface = g_slice_new0 (MsdkSurface);
    msdk_surface->surface = gst_msdk_get_surface_from_buffer (inbuf);
    msdk_surface->buf = gst_buffer_ref (inbuf);
    return msdk_surface;
  }

  /* If upstream hasn't accpeted the proposed msdk bufferpool,
   * just copy frame to msdk buffer and take a surface from it.
   */
  if (!(msdk_surface =
          get_surface_from_pool (thiz, thiz->sinkpad_buffer_pool, NULL)))
    goto error;

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
  mfxSession session;
  mfxSyncPoint sync_point = NULL;
  mfxStatus status;
  MsdkSurface *in_surface = NULL;
  MsdkSurface *out_surface = NULL;

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
  for (;;) {
    status =
        MFXVideoVPP_RunFrameVPPAsync (session, in_surface->surface,
        out_surface->surface, NULL, &sync_point);
    if (status != MFX_WRN_DEVICE_BUSY)
      break;
    /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
    g_usleep (1000);
  };

  if (status != MFX_ERR_NONE && status != MFX_ERR_MORE_DATA
      && status != MFX_ERR_MORE_SURFACE)
    goto vpp_error;

  /* No output generated */
  if (status == MFX_ERR_MORE_DATA)
    goto error_more_data;
  if (sync_point)
    MFXVideoCORE_SyncOperation (session, sync_point, 10000);

  /* More than one output buffers are generated */
  if (status == MFX_ERR_MORE_SURFACE)
    status = MFX_ERR_NONE;

  gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  free_msdk_surface (in_surface);
  return GST_FLOW_OK;

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
    GST_WARNING_OBJECT (thiz, "Encoder close failed (%s)",
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

  thiz->field_duration = GST_CLOCK_TIME_NONE;
  gst_video_info_init (&thiz->sinkpad_info);
  gst_video_info_init (&thiz->srcpad_info);
}

static void
ensure_filters (GstMsdkVPP * thiz)
{
  guint n_filters = 0;

  /* Denoise */
  if (thiz->flags & GST_MSDK_FLAG_DENOISE) {
    mfxExtVPPDenoise *mfx_denoise = &thiz->mfx_denoise;
    mfx_denoise->Header.BufferId = MFX_EXTBUFF_VPP_DENOISE;
    mfx_denoise->Header.BufferSz = sizeof (mfxExtVPPDenoise);
    mfx_denoise->DenoiseFactor = thiz->denoise_factor;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_denoise);
    thiz->max_filter_algorithms[n_filters] = MFX_EXTBUFF_VPP_DENOISE;
    n_filters++;
  }

  /* Rotation */
  if (thiz->flags & GST_MSDK_FLAG_ROTATION) {
    mfxExtVPPRotation *mfx_rotation = &thiz->mfx_rotation;
    mfx_rotation->Header.BufferId = MFX_EXTBUFF_VPP_ROTATION;
    mfx_rotation->Header.BufferSz = sizeof (mfxExtVPPRotation);
    mfx_rotation->Angle = thiz->rotation;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_rotation);
    thiz->max_filter_algorithms[n_filters] = MFX_EXTBUFF_VPP_ROTATION;
    n_filters++;
  }

  /* Deinterlace */
  if (thiz->flags & GST_MSDK_FLAG_DEINTERLACE) {
    mfxExtVPPDeinterlacing *mfx_deinterlace = &thiz->mfx_deinterlace;
    mfx_deinterlace->Header.BufferId = MFX_EXTBUFF_VPP_DEINTERLACING;
    mfx_deinterlace->Header.BufferSz = sizeof (mfxExtVPPDeinterlacing);
    mfx_deinterlace->Mode = thiz->deinterlace_method;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_deinterlace);
    thiz->max_filter_algorithms[n_filters] = MFX_EXTBUFF_VPP_DEINTERLACING;
    n_filters++;
  }

  /* mfxExtVPPDoUse */
  if (n_filters) {
    mfxExtVPPDoUse *mfx_vpp_douse = &thiz->mfx_vpp_douse;
    mfx_vpp_douse->Header.BufferId = MFX_EXTBUFF_VPP_DOUSE;
    mfx_vpp_douse->Header.BufferSz = sizeof (mfxExtVPPDoUse);
    mfx_vpp_douse->NumAlg = n_filters;
    mfx_vpp_douse->AlgList = thiz->max_filter_algorithms;
    gst_msdkvpp_add_extra_param (thiz, (mfxExtBuffer *) mfx_vpp_douse);
  }
}

static void
gst_msdkvpp_set_passthrough (GstMsdkVPP * thiz)
{
  gboolean passthrough = TRUE;

  /* no passthrough if any of the filter algorithm is enabled */
  if (thiz->flags)
    passthrough = FALSE;

  /* no passthrough if there is change in out width,height or format */
  if (GST_VIDEO_INFO_WIDTH (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_WIDTH (&thiz->srcpad_info)
      || GST_VIDEO_INFO_HEIGHT (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_HEIGHT (&thiz->srcpad_info)
      || GST_VIDEO_INFO_FORMAT (&thiz->sinkpad_info) !=
      GST_VIDEO_INFO_FORMAT (&thiz->srcpad_info))
    passthrough = FALSE;

  GST_OBJECT_UNLOCK (thiz);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (thiz), passthrough);
  GST_OBJECT_LOCK (thiz);
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
  thiz->param.vpp.Out.FrameRateExtN =
      GST_VIDEO_INFO_FPS_N (&thiz->sinkpad_info);
  thiz->param.vpp.Out.FrameRateExtD =
      GST_VIDEO_INFO_FPS_D (&thiz->sinkpad_info);

  /* set vpp out picstruct as progressive if deinterlacing enabled */
  if (thiz->flags & GST_MSDK_FLAG_DEINTERLACE)
    thiz->param.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

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

  /* Enable the required filters */
  ensure_filters (thiz);

  /* set passthrough according to filter operation change */
  gst_msdkvpp_set_passthrough (thiz);

  /* Add exteneded buffers */
  if (thiz->num_extra_params) {
    thiz->param.NumExtParam = thiz->num_extra_params;
    thiz->param.ExtParam = thiz->extra_params;
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
    gst_msdk_frame_alloc (thiz->context, &(request[0]), &thiz->in_alloc_resp);
    /* Output surface pool pre-allocation */
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

  gst_video_info_from_caps (&in_info, caps);
  gst_video_info_from_caps (&out_info, out_caps);

  if (!gst_video_info_is_equal (&in_info, &thiz->sinkpad_info))
    sinkpad_info_changed = TRUE;
  if (!gst_video_info_is_equal (&out_info, &thiz->srcpad_info))
    srcpad_info_changed = TRUE;

  thiz->sinkpad_info = in_info;
  thiz->srcpad_info = out_info;
#ifndef _WIN32
  thiz->use_video_memory = TRUE;
#else
  thiz->use_video_memory = FALSE;
#endif

  if (!sinkpad_info_changed && !srcpad_info_changed)
    return TRUE;

  /* check for deinterlace requirement */
  deinterlace = gst_msdkvpp_is_deinterlace_enabled (thiz, &in_info);
  if (deinterlace)
    thiz->flags |= GST_MSDK_FLAG_DEINTERLACE;
  thiz->field_duration = GST_VIDEO_INFO_FPS_N (&in_info) > 0 ?
      gst_util_uint64_scale (GST_SECOND, GST_VIDEO_INFO_FPS_D (&in_info),
      (1 + deinterlace) * GST_VIDEO_INFO_FPS_N (&in_info)) : 0;

  if (!gst_msdkvpp_initialize (thiz))
    return FALSE;

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

static GstCaps *
gst_msdkvpp_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstMsdkVPP *thiz = GST_MSDKVPP (trans);
  GstCaps *result = NULL;

  if (direction == GST_PAD_SRC)
    result = gst_caps_fixate (result);
  else {
    result = gst_msdkvpp_fixate_srccaps (thiz, caps, othercaps);
  }

  GST_DEBUG_OBJECT (trans, "fixated to %" GST_PTR_FORMAT, result);
  gst_caps_unref (othercaps);
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

  g_object_class_install_properties (gobject_class, PROP_N, obj_properties);
}

static void
gst_msdkvpp_init (GstMsdkVPP * thiz)
{
  thiz->hardware = PROP_HARDWARE_DEFAULT;
  thiz->async_depth = PROP_ASYNC_DEPTH_DEFAULT;
  thiz->denoise_factor = PROP_DENOISE_DEFAULT;
  thiz->rotation = PROP_ROTATION_DEFAULT;
  thiz->deinterlace_mode = PROP_DEINTERLACE_MODE_DEFAULT;
  thiz->deinterlace_method = PROP_DEINTERLACE_METHOD_DEFAULT;
  thiz->field_duration = GST_CLOCK_TIME_NONE;
  gst_video_info_init (&thiz->sinkpad_info);
  gst_video_info_init (&thiz->srcpad_info);
}
