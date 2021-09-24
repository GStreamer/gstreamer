/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>

#if defined (USE_OMX_TARGET_RPI) && defined(__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#if defined (HAVE_GST_GL)
#include <gst/gl/egl/gstglmemoryegl.h>
#endif

#if defined (USE_OMX_TARGET_RPI) && defined(__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#include <string.h>

#include "gstomxbufferpool.h"
#include "gstomxvideo.h"
#include "gstomxvideodec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_dec_debug_category

/* prototypes */
static void gst_omx_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_start (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_omx_video_dec_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec,
    GstQuery * query);
static gboolean gst_omx_video_dec_propose_allocation (GstVideoDecoder * bdec,
    GstQuery * query);

static GstFlowReturn gst_omx_video_dec_drain (GstVideoDecoder * decoder);

static OMX_ERRORTYPE gst_omx_video_dec_allocate_output_buffers (GstOMXVideoDec *
    self);
static gboolean gst_omx_video_dec_deallocate_output_buffers (GstOMXVideoDec
    * self);

enum
{
  PROP_0,
  PROP_INTERNAL_ENTROPY_BUFFERS,
};

#define GST_OMX_VIDEO_DEC_INTERNAL_ENTROPY_BUFFERS_DEFAULT (5)

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoDec, gst_omx_video_dec,
    GST_TYPE_VIDEO_DECODER, DEBUG_INIT);

static void
gst_omx_video_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);
#endif

  switch (prop_id) {
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_INTERNAL_ENTROPY_BUFFERS:
      self->internal_entropy_buffers = g_value_get_uint (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_video_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);
#endif

  switch (prop_id) {
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_INTERNAL_ENTROPY_BUFFERS:
      g_value_set_uint (value, self->internal_entropy_buffers);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_video_dec_class_init (GstOMXVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_dec_finalize;
  gobject_class->set_property = gst_omx_video_dec_set_property;
  gobject_class->get_property = gst_omx_video_dec_get_property;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  g_object_class_install_property (gobject_class, PROP_INTERNAL_ENTROPY_BUFFERS,
      g_param_spec_uint ("internal-entropy-buffers", "Internal entropy buffers",
          "Number of internal buffers used by the decoder to smooth out entropy decoding performance. "
          "Increasing it may improve the frame rate when decoding high bitrate streams. "
          "Decreasing it reduces the memory footprint",
          2, 16, GST_OMX_VIDEO_DEC_INTERNAL_ENTROPY_BUFFERS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_change_state);

  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_video_dec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_video_dec_close);
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_dec_stop);
  video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_video_dec_flush);
  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_omx_video_dec_finish);
  video_decoder_class->drain = GST_DEBUG_FUNCPTR (gst_omx_video_dec_drain);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_decide_allocation);
  video_decoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_propose_allocation);

  klass->cdata.type = GST_OMX_COMPONENT_TYPE_FILTER;
  klass->cdata.default_src_template_caps =
#if defined (HAVE_GST_GL)
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
      "RGBA") "; "
#endif
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_FORMAT_INTERLACED,
      GST_OMX_VIDEO_DEC_SUPPORTED_FORMATS)
      ", interlace-mode = (string) alternate ; "
#endif
      GST_VIDEO_CAPS_MAKE (GST_OMX_VIDEO_DEC_SUPPORTED_FORMATS);
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self)
{
  self->dmabuf = FALSE;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  self->internal_entropy_buffers =
      GST_OMX_VIDEO_DEC_INTERNAL_ENTROPY_BUFFERS_DEFAULT;
#endif

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);
  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (self), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (self));

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS

#define CHECK_ERR(setting) \
  if (err == OMX_ErrorUnsupportedIndex || err == OMX_ErrorUnsupportedSetting) { \
    GST_WARNING_OBJECT (self, \
        "Setting " setting " parameters not supported by the component"); \
  } else if (err != OMX_ErrorNone) { \
    GST_ERROR_OBJECT (self, \
        "Failed to set " setting " parameters: %s (0x%08x)", \
        gst_omx_error_to_string (err), err); \
    return FALSE; \
  }

static gboolean
set_zynqultrascaleplus_props (GstOMXVideoDec * self)
{
  OMX_ERRORTYPE err;

  {
    OMX_ALG_VIDEO_PARAM_INTERNAL_ENTROPY_BUFFERS entropy_buffers;

    GST_OMX_INIT_STRUCT (&entropy_buffers);
    entropy_buffers.nPortIndex = self->dec_in_port->index;
    entropy_buffers.nNumInternalEntropyBuffers = self->internal_entropy_buffers;

    GST_DEBUG_OBJECT (self, "setting number of internal entropy buffers to %d",
        self->internal_entropy_buffers);

    err =
        gst_omx_component_set_parameter (self->dec,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoInternalEntropyBuffers,
        &entropy_buffers);
    CHECK_ERR ("internal entropy buffers");
  }

  return TRUE;
}
#endif

static gboolean
gst_omx_video_dec_open (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);
  gint in_port_index, out_port_index;

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->dec =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;

  if (!self->dec)
    return FALSE;

  if (gst_omx_component_get_state (self->dec,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamVideoInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }
  self->dec_in_port = gst_omx_component_add_port (self->dec, in_port_index);
  self->dec_out_port = gst_omx_component_add_port (self->dec, out_port_index);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  GST_DEBUG_OBJECT (self, "Configure decoder output to export dmabuf");
  self->dmabuf = gst_omx_port_set_dmabuf (self->dec_out_port, TRUE);
#endif

  if (!self->dec_in_port || !self->dec_out_port)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  GST_DEBUG_OBJECT (self, "Opening EGL renderer");
  self->egl_render =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      "OMX.broadcom.egl_render", NULL, klass->cdata.hacks);

  if (!self->egl_render)
    return FALSE;

  if (gst_omx_component_get_state (self->egl_render,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->egl_render,
        OMX_IndexParamVideoInit, &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u", param.nPorts,
          param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }

  self->egl_in_port =
      gst_omx_component_add_port (self->egl_render, in_port_index);
  self->egl_out_port =
      gst_omx_component_add_port (self->egl_render, out_port_index);

  if (!self->egl_in_port || !self->egl_out_port)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Opened EGL renderer");
#endif

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (!set_zynqultrascaleplus_props (self))
    return FALSE;
#endif

  return TRUE;
}

static gboolean
gst_omx_video_dec_shutdown (GstOMXVideoDec * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down decoder");

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  state = gst_omx_component_get_state (self->egl_render, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->egl_render, OMX_StateIdle);
      gst_omx_component_set_state (self->dec, OMX_StateIdle);
      gst_omx_component_get_state (self->egl_render, 5 * GST_SECOND);
      gst_omx_component_get_state (self->dec, 1 * GST_SECOND);
    }
    gst_omx_component_set_state (self->egl_render, OMX_StateLoaded);
    gst_omx_component_set_state (self->dec, OMX_StateLoaded);

    gst_omx_port_deallocate_buffers (self->dec_in_port);
    gst_omx_video_dec_deallocate_output_buffers (self);
    gst_omx_close_tunnel (self->dec_out_port, self->egl_in_port);
    if (state > OMX_StateLoaded) {
      gst_omx_component_get_state (self->egl_render, 5 * GST_SECOND);
      gst_omx_component_get_state (self->dec, 1 * GST_SECOND);
    }
  }

  /* Otherwise we didn't use EGL and just fall back to 
   * shutting down the decoder */
#endif

  state = gst_omx_component_get_state (self->dec, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->dec, OMX_StateIdle);
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->dec, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->dec_in_port);
    gst_omx_video_dec_deallocate_output_buffers (self);
    if (state > OMX_StateLoaded) {
      if (self->dec_out_port->buffers)
        /* Don't wait for the state transition if the pool still has outstanding
         * buffers as it will timeout anyway */
        GST_WARNING_OBJECT (self,
            "Output buffers haven't been freed; still owned downstream?");
      else
        gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
    }
  }

  return TRUE;
}

static gboolean
gst_omx_video_dec_close (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (!gst_omx_video_dec_shutdown (self))
    return FALSE;

  self->dec_in_port = NULL;
  self->dec_out_port = NULL;
  if (self->dec)
    gst_omx_component_unref (self->dec);
  self->dec = NULL;

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  self->egl_in_port = NULL;
  self->egl_out_port = NULL;
  if (self->egl_render)
    gst_omx_component_unref (self->egl_render);
  self->egl_render = NULL;
#endif

  self->started = FALSE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_video_dec_finalize (GObject * object)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

  G_OBJECT_CLASS (gst_omx_video_dec_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      self->use_buffers = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->dec_in_port)
        gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
      if (self->dec_out_port)
        gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
      if (self->egl_in_port)
        gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, TRUE);
      if (self->egl_out_port)
        gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, TRUE);
#endif

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_omx_video_dec_parent_class)->change_state
      (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_omx_video_dec_fill_buffer (GstOMXVideoDec * self,
    GstOMXBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
  GstVideoInfo *vinfo = &state->info;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->dec_out_port->port_def;
  gboolean ret = FALSE;
  GstVideoFrame frame;

  if (vinfo->width != port_def->format.video.nFrameWidth ||
      GST_VIDEO_INFO_FIELD_HEIGHT (vinfo) !=
      port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Resolution do not match: port=%ux%u vinfo=%dx%d",
        (guint) port_def->format.video.nFrameWidth,
        (guint) port_def->format.video.nFrameHeight,
        vinfo->width, GST_VIDEO_INFO_FIELD_HEIGHT (vinfo));
    goto done;
  }

  /* Same strides and everything */
  if (gst_buffer_get_size (outbuf) == inbuf->omx_buf->nFilledLen) {
    GstMapInfo map = GST_MAP_INFO_INIT;

    if (!gst_buffer_map (outbuf, &map, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Failed to map output buffer");
      goto done;
    }

    memcpy (map.data,
        inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset,
        inbuf->omx_buf->nFilledLen);
    gst_buffer_unmap (outbuf, &map);
    ret = TRUE;
    goto done;
  }

  /* Different strides */
  if (gst_video_frame_map (&frame, vinfo, outbuf, GST_MAP_WRITE)) {
    const guint nstride = port_def->format.video.nStride;
    const guint nslice = port_def->format.video.nSliceHeight;
    guint src_stride[GST_VIDEO_MAX_PLANES] = { nstride, 0, };
    guint src_size[GST_VIDEO_MAX_PLANES] = { nstride * nslice, 0, };
    gint dst_width[GST_VIDEO_MAX_PLANES] = { 0, };
    gint dst_height[GST_VIDEO_MAX_PLANES] =
        { GST_VIDEO_INFO_FIELD_HEIGHT (vinfo), 0, };
    const guint8 *src;
    guint p;

    switch (GST_VIDEO_INFO_FORMAT (vinfo)) {
      case GST_VIDEO_FORMAT_ABGR:
      case GST_VIDEO_FORMAT_ARGB:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo) * 4;
        break;
      case GST_VIDEO_FORMAT_RGB16:
      case GST_VIDEO_FORMAT_BGR16:
      case GST_VIDEO_FORMAT_YUY2:
      case GST_VIDEO_FORMAT_UYVY:
      case GST_VIDEO_FORMAT_YVYU:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo) * 2;
        break;
      case GST_VIDEO_FORMAT_GRAY8:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo);
        break;
      case GST_VIDEO_FORMAT_I420:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo);
        src_stride[1] = nstride / 2;
        src_size[1] = (src_stride[1] * nslice) / 2;
        dst_width[1] = GST_VIDEO_INFO_WIDTH (vinfo) / 2;
        dst_height[1] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo) / 2;
        src_stride[2] = nstride / 2;
        src_size[2] = (src_stride[1] * nslice) / 2;
        dst_width[2] = GST_VIDEO_INFO_WIDTH (vinfo) / 2;
        dst_height[2] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo) / 2;
        break;
      case GST_VIDEO_FORMAT_NV12:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo);
        src_stride[1] = nstride;
        src_size[1] = src_stride[1] * nslice / 2;
        dst_width[1] = GST_VIDEO_INFO_WIDTH (vinfo);
        dst_height[1] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo) / 2;
        break;
      case GST_VIDEO_FORMAT_NV16:
        dst_width[0] = GST_VIDEO_INFO_WIDTH (vinfo);
        src_stride[1] = nstride;
        src_size[1] = src_stride[1] * nslice;
        dst_width[1] = GST_VIDEO_INFO_WIDTH (vinfo);
        dst_height[1] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo);
        break;
      case GST_VIDEO_FORMAT_NV12_10LE32:
        /* Need ((width + 2) / 3) 32-bits words */
        dst_width[0] = (GST_VIDEO_INFO_WIDTH (vinfo) + 2) / 3 * 4;
        dst_width[1] = dst_width[0];
        src_stride[1] = nstride;
        src_size[1] = src_stride[1] * nslice / 2;
        dst_height[1] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo) / 2;
        break;
      case GST_VIDEO_FORMAT_NV16_10LE32:
        /* Need ((width + 2) / 3) 32-bits words */
        dst_width[0] = (GST_VIDEO_INFO_WIDTH (vinfo) + 2) / 3 * 4;
        dst_width[1] = dst_width[0];
        src_stride[1] = nstride;
        src_size[1] = src_stride[1] * nslice;
        dst_height[1] = GST_VIDEO_INFO_FIELD_HEIGHT (vinfo);
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
    for (p = 0; p < GST_VIDEO_INFO_N_PLANES (vinfo); p++) {
      const guint8 *data;
      guint8 *dst;
      guint h;

      dst = GST_VIDEO_FRAME_PLANE_DATA (&frame, p);
      data = src;
      for (h = 0; h < dst_height[p]; h++) {
        memcpy (dst, data, dst_width[p]);
        dst += GST_VIDEO_FRAME_PLANE_STRIDE (&frame, p);
        data += src_stride[p];
      }
      src += src_size[p];
    }

    gst_video_frame_unmap (&frame);
    ret = TRUE;
  } else {
    GST_ERROR_OBJECT (self, "Can't map output buffer to frame");
    goto done;
  }

done:
  if (ret) {
    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (GST_OMX_GET_TICKS (inbuf->omx_buf->nTimeStamp),
        GST_SECOND, OMX_TICKS_PER_SECOND);
    if (inbuf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (inbuf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);
  }

  gst_video_codec_state_unref (state);

  return ret;
}

static GstBuffer *
gst_omx_try_importing_buffer (GstOMXVideoDec * self, GstBufferPool * pool,
    GstOMXPort * port, GstVideoInfo * v_info, guint i, GstVideoFrame ** frame)
{
  GstBufferPoolAcquireParams params = { 0, };
  GstBuffer *buffer = NULL;
  GstMemory *mem;
  GstMapFlags flags = GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
  gboolean is_mapped = FALSE;

  *frame = NULL;

  if (gst_buffer_pool_acquire_buffer (pool, &buffer, &params) != GST_FLOW_OK) {
    GST_INFO_OBJECT (self, "Failed to acquire %d-th buffer", i);
    return NULL;
  }

  if (gst_buffer_n_memory (buffer) != 1) {
    GST_INFO_OBJECT (self, "%d-th buffer has more than one memory (%d)", i,
        gst_buffer_n_memory (buffer));
    goto out;
  }

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!mem) {
    GST_INFO_OBJECT (self, "Failed to acquire memory of %d-th buffer", i);
    goto out;
  }

  if (self->dmabuf && !gst_is_dmabuf_memory (mem)) {
    GST_INFO_OBJECT (self,
        "%d-th buffer doesn't contain dmabuf while the decoder is in dmabuf mode",
        i);
    goto out;
  }

  *frame = g_slice_new0 (GstVideoFrame);

  is_mapped = gst_video_frame_map (*frame, v_info, buffer, flags);
  if (!is_mapped) {
    GST_INFO_OBJECT (self, "Failed to map %d-th buffer", i);
    goto out;
  }

  if (GST_VIDEO_FRAME_SIZE (*frame) < port->port_def.nBufferSize) {
    GST_INFO_OBJECT (self,
        "Frame size of %d-th buffer (%" G_GSIZE_FORMAT
        ") is too small for port buffer size (%d)", i,
        GST_VIDEO_FRAME_SIZE (*frame), (guint32) port->port_def.nBufferSize);
    goto out;
  }

  return buffer;

out:
  if (*frame) {
    if (is_mapped)
      gst_video_frame_unmap (*frame);
    g_slice_free (GstVideoFrame, *frame);
    *frame = NULL;
  }
  gst_buffer_unref (buffer);
  return NULL;
}

static OMX_ERRORTYPE
gst_omx_video_dec_allocate_output_buffers (GstOMXVideoDec * self)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GstOMXPort *port;
  GstBufferPool *pool;
  GstStructure *config;
  gboolean eglimage = FALSE, add_videometa = FALSE;
  GstCaps *caps = NULL;
  guint min = 0, max = 0;
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  port = self->eglimage ? self->egl_out_port : self->dec_out_port;
#else
  port = self->dec_out_port;
#endif

  pool = gst_video_decoder_get_buffer_pool (GST_VIDEO_DECODER (self));
  if (pool) {
    GstAllocator *allocator;

    config = gst_buffer_pool_get_config (pool);
    if (!gst_buffer_pool_config_get_params (config, &caps, NULL, &min, &max)) {
      GST_ERROR_OBJECT (self, "Can't get buffer pool params");
      gst_structure_free (config);
      err = OMX_ErrorUndefined;
      goto done;
    }
    if (!gst_buffer_pool_config_get_allocator (config, &allocator, NULL)) {
      GST_ERROR_OBJECT (self, "Can't get buffer pool allocator");
      gst_structure_free (config);
      err = OMX_ErrorUndefined;
      goto done;
    }

    /* Need at least 4 buffers for anything meaningful */
    min = MAX (min + port->port_def.nBufferCountMin, 4);
    if (max == 0) {
      max = min;
    } else if (max < min) {
      /* Can't use pool because can't have enough buffers */
      GST_DEBUG_OBJECT (self,
          "pool can only provide %d buffers but %d are required", max, min);
      caps = NULL;
    } else {
      min = max;
    }

    add_videometa = gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_structure_free (config);

#if defined (HAVE_GST_GL)
    eglimage = self->eglimage
        && (allocator && GST_IS_GL_MEMORY_EGL_ALLOCATOR (allocator));
#else
    eglimage = FALSE;
#endif
    caps = caps ? gst_caps_ref (caps) : NULL;

    GST_DEBUG_OBJECT (self, "Trying to use pool %p with caps %" GST_PTR_FORMAT
        " and memory type %s", pool, caps,
        (allocator ? allocator->mem_type : "(null)"));
  } else {
    gst_caps_replace (&caps, NULL);
    min = max = port->port_def.nBufferCountMin;
    GST_DEBUG_OBJECT (self, "No pool available, not negotiated yet");
  }

#if defined (HAVE_GST_GL)
  /* Will retry without EGLImage */
  if (self->eglimage && !eglimage) {
    GST_DEBUG_OBJECT (self,
        "Wanted to use EGLImage but downstream doesn't support it");
    err = OMX_ErrorUndefined;
    goto done;
  }
#endif

  if (caps)
    self->out_port_pool =
        gst_omx_buffer_pool_new (GST_ELEMENT_CAST (self), self->dec, port,
        self->dmabuf ? GST_OMX_BUFFER_MODE_DMABUF :
        GST_OMX_BUFFER_MODE_SYSTEM_MEMORY);

#if defined (HAVE_GST_GL)
  if (eglimage) {
    GList *buffers = NULL;
    GList *images = NULL;
    gint i;
    GstBufferPoolAcquireParams params = { 0, };
    gpointer egl_display = 0;

    GST_DEBUG_OBJECT (self, "Trying to allocate %d EGLImages", min);

    for (i = 0; i < min; i++) {
      GstBuffer *buffer = NULL;
      GstMemory *mem;
      GstGLMemoryEGL *gl_mem;

      if (gst_buffer_pool_acquire_buffer (pool, &buffer, &params) != GST_FLOW_OK
          || gst_buffer_n_memory (buffer) != 1
          || !(mem = gst_buffer_peek_memory (buffer, 0))
          || !GST_IS_GL_MEMORY_EGL_ALLOCATOR (mem->allocator)) {
        GST_INFO_OBJECT (self, "Failed to allocated %d-th EGLImage", i);
        gst_buffer_replace (&buffer, NULL);
        g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
        g_list_free (images);
        buffers = NULL;
        images = NULL;
        err = OMX_ErrorUndefined;
        goto done;
      }
      gl_mem = (GstGLMemoryEGL *) mem;
      buffers = g_list_append (buffers, buffer);
      images = g_list_append (images, gst_gl_memory_egl_get_image (gl_mem));
      if (!egl_display)
        egl_display = gst_gl_memory_egl_get_display (gl_mem);
    }

    GST_DEBUG_OBJECT (self, "Allocated %d EGLImages successfully", min);

    /* Everything went fine? */
    if (eglimage) {
      GST_DEBUG_OBJECT (self, "Setting EGLDisplay");
      port->port_def.format.video.pNativeWindow = egl_display;
      err = gst_omx_port_update_port_definition (port, &port->port_def);
      if (err != OMX_ErrorNone) {
        GST_INFO_OBJECT (self,
            "Failed to set EGLDisplay on port: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
        g_list_free (images);
        goto done;
      } else {
        GList *l;

        if (min != port->port_def.nBufferCountActual) {
          err = gst_omx_port_update_port_definition (port, NULL);
          if (err == OMX_ErrorNone) {
            port->port_def.nBufferCountActual = min;
            err = gst_omx_port_update_port_definition (port, &port->port_def);
          }

          if (err != OMX_ErrorNone) {
            GST_INFO_OBJECT (self,
                "Failed to configure %u output buffers: %s (0x%08x)", min,
                gst_omx_error_to_string (err), err);
            g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
            g_list_free (images);
            goto done;
          }
#if OMX_VERSION_MINOR == 2
          /* In OMX-IL 1.2.0, the nBufferCountActual change is propagated to the
           * the input port upon call to the SetParameter on out port above. This
           * propagation triggers a SettingsChanged event. It is up to the client
           * to decide if this event should lead to reconfigure the port. Here
           * this is clearly informal so lets just acknowledge the event to avoid
           * input port reconfiguration. Note that the SettingsChanged event will
           * be sent in-context of the SetParameter call above. So the event is
           * garantie to be proceeded in the handle_message call below. */
          err = gst_omx_port_mark_reconfigured (self->dec_in_port);

          if (err != OMX_ErrorNone) {
            GST_ERROR_OBJECT (self,
                "Failed to acknowledge port settings changed: %s (0x%08x)",
                gst_omx_error_to_string (err), err);
            g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
            g_list_free (images);
            goto done;
          }
#endif
        }

        if (!gst_omx_port_is_enabled (port)) {
          err = gst_omx_port_set_enabled (port, TRUE);
          if (err != OMX_ErrorNone) {
            GST_INFO_OBJECT (self,
                "Failed to enable port: %s (0x%08x)",
                gst_omx_error_to_string (err), err);
            g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
            g_list_free (images);
            goto done;
          }
        }

        err = gst_omx_port_use_eglimages (port, images);
        g_list_free (images);

        if (err != OMX_ErrorNone) {
          GST_INFO_OBJECT (self,
              "Failed to pass EGLImages to port: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
          goto done;
        }

        err = gst_omx_port_wait_enabled (port, 2 * GST_SECOND);
        if (err != OMX_ErrorNone) {
          GST_INFO_OBJECT (self,
              "Failed to wait until port is enabled: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
          goto done;
        }

        GST_DEBUG_OBJECT (self, "Populating internal buffer pool");
        GST_OMX_BUFFER_POOL (self->out_port_pool)->other_pool =
            GST_BUFFER_POOL (gst_object_ref (pool));
        for (l = buffers; l; l = l->next) {
          g_ptr_array_add (GST_OMX_BUFFER_POOL (self->out_port_pool)->buffers,
              l->data);
        }
        g_list_free (buffers);
        /* All good and done, set caps below */
      }
    }
  }
#endif /* defined (HAVE_GST_GL) */

  /* If not using EGLImage or trying to use EGLImage failed */
  if (!eglimage) {
    gboolean was_enabled = TRUE;
    GList *buffers = NULL;
    GList *l = NULL;

    if (min != port->port_def.nBufferCountActual) {
      err = gst_omx_port_update_port_definition (port, NULL);
      if (err == OMX_ErrorNone) {
        port->port_def.nBufferCountActual = min;
        err = gst_omx_port_update_port_definition (port, &port->port_def);
      }

      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to configure %u output buffers: %s (0x%08x)", min,
            gst_omx_error_to_string (err), err);
        goto done;
      }
#if OMX_VERSION_MINOR == 2
      /* In OMX-IL 1.2.0, the nBufferCountActual change is propagated to the
       * the input port upon call to the SetParameter on out port above. This
       * propagation triggers a SettingsChanged event. It is up to the client
       * to decide if this event should lead to reconfigure the port. Here
       * this is clearly informal so lets just acknowledge the event to avoid
       * input port reconfiguration. Note that the SettingsChanged event will
       * be sent in-context of the SetParameter call above. So the event is
       * garantie to be proceeded in the handle_message call below. */
      err = gst_omx_port_mark_reconfigured (self->dec_in_port);

      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to acknowledge port settings changed: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto done;
      }
#endif
    }

    if (!gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_INFO_OBJECT (self,
            "Failed to enable port: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto done;
      }
      was_enabled = FALSE;
    }

    if (!caps)
      self->use_buffers = FALSE;

    if (self->use_buffers) {
      GList *images = NULL;
      GList *frames = NULL;
      GstVideoInfo v_info;
      gint i;

      if (!gst_video_info_from_caps (&v_info, caps)) {
        GST_INFO_OBJECT (self,
            "Failed to get video info from caps %" GST_PTR_FORMAT, caps);
        err = OMX_ErrorUndefined;
        self->use_buffers = FALSE;
      }

      GST_DEBUG_OBJECT (self, "Trying to use %d buffers", min);

      for (i = 0; i < min && self->use_buffers; i++) {
        GstBuffer *buffer = NULL;
        GstVideoFrame *frame = NULL;

        buffer =
            gst_omx_try_importing_buffer (self, pool, port, &v_info, i, &frame);
        if (!buffer) {
          /* buffer does not match minimal requirement to try OMX_UseBuffer */
          GST_DEBUG_OBJECT (self, "Failed to import %d-th buffer", i);
          g_list_free (images);
          g_list_free_full (frames, (GDestroyNotify) gst_video_frame_unmap);
          g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
          buffers = NULL;
          images = NULL;
          err = OMX_ErrorUndefined;
          self->use_buffers = FALSE;
          break;
        } else {
          /* if downstream pool is 1 n_mem then always try to use buffers
           * and retry without using them if it fails */
          GstMemory *mem;

          buffers = g_list_append (buffers, buffer);
          frames = g_list_append (frames, frame);

          mem = gst_buffer_peek_memory (buffer, 0);
          if (self->dmabuf && gst_is_dmabuf_memory (mem))
            /* Use the imported fd rather than mapped address in dmabuf mode */
            images =
                g_list_append (images,
                GUINT_TO_POINTER (gst_dmabuf_memory_get_fd (mem)));
          else
            images =
                g_list_append (images, GST_VIDEO_FRAME_PLANE_DATA (frame, 0));
        }
      }

      /* buffers match minimal requirements then
       * now try to actually use them */
      if (images) {
        err = gst_omx_port_use_buffers (port, images);
        g_list_free (images);
        g_list_free_full (frames, (GDestroyNotify) gst_video_frame_unmap);

        if (err == OMX_ErrorNone) {
          GST_DEBUG_OBJECT (self, "Using %d buffers", min);
        } else {
          GST_INFO_OBJECT (self,
              "Failed to OMX_UseBuffer on port: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
          self->use_buffers = FALSE;
        }
      }
    }

    if (!self->use_buffers)
      err = gst_omx_port_allocate_buffers (port);

    if (err != OMX_ErrorNone && min > port->port_def.nBufferCountMin) {
      GST_ERROR_OBJECT (self,
          "Failed to allocate required number of buffers %d, trying less and copying",
          min);
      min = port->port_def.nBufferCountMin;

      if (!was_enabled) {
        err = gst_omx_port_set_enabled (port, FALSE);
        if (err != OMX_ErrorNone) {
          GST_INFO_OBJECT (self,
              "Failed to disable port again: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          goto done;
        }
      }

      if (min != port->port_def.nBufferCountActual) {
        err = gst_omx_port_update_port_definition (port, NULL);
        if (err == OMX_ErrorNone) {
          port->port_def.nBufferCountActual = min;
          err = gst_omx_port_update_port_definition (port, &port->port_def);
        }

        if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (self,
              "Failed to configure %u output buffers: %s (0x%08x)", min,
              gst_omx_error_to_string (err), err);
          goto done;
        }
      }

      err = gst_omx_port_allocate_buffers (port);

      /* Can't provide buffers downstream in this case */
      gst_caps_replace (&caps, NULL);
    }

    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to allocate %d buffers: %s (0x%08x)", min,
          gst_omx_error_to_string (err), err);
      goto done;
    }

    if (!was_enabled) {
      err = gst_omx_port_wait_enabled (port, 2 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to wait until port is enabled: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto done;
      }
    }

    if (self->use_buffers) {
      GST_DEBUG_OBJECT (self, "Populating internal buffer pool");
      GST_OMX_BUFFER_POOL (self->out_port_pool)->other_pool =
          GST_BUFFER_POOL (gst_object_ref (pool));
      for (l = buffers; l; l = l->next) {
        g_ptr_array_add (GST_OMX_BUFFER_POOL (self->out_port_pool)->buffers,
            l->data);
      }
      g_list_free (buffers);
    }

  }

  err = OMX_ErrorNone;

  if (caps) {
    config = gst_buffer_pool_get_config (self->out_port_pool);

    if (add_videometa)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_params (config, caps,
        self->dec_out_port->port_def.nBufferSize, min, max);

    if (!gst_buffer_pool_set_config (self->out_port_pool, config)) {
      GST_INFO_OBJECT (self, "Failed to set config on internal pool");
      gst_object_unref (self->out_port_pool);
      self->out_port_pool = NULL;
      goto done;
    }

    /* This now allocates all the buffers */
    if (!gst_buffer_pool_set_active (self->out_port_pool, TRUE)) {
      GST_INFO_OBJECT (self, "Failed to activate internal pool");
      gst_object_unref (self->out_port_pool);
      self->out_port_pool = NULL;
    } else if (!self->use_buffers) {
      gst_buffer_pool_set_active (pool, FALSE);
    }
  } else if (self->out_port_pool) {
    gst_object_unref (self->out_port_pool);
    self->out_port_pool = NULL;
  }

done:
  if (!self->out_port_pool && err == OMX_ErrorNone)
    GST_DEBUG_OBJECT (self,
        "Not using our internal pool and copying buffers for downstream");

  if (caps)
    gst_caps_unref (caps);
  if (pool)
    gst_object_unref (pool);
  if (state)
    gst_video_codec_state_unref (state);

  return err;
}

static gboolean
gst_omx_video_dec_deallocate_output_buffers (GstOMXVideoDec * self)
{
  if (self->out_port_pool) {
    /* Pool will free buffers when stopping */
    gst_buffer_pool_set_active (self->out_port_pool, FALSE);
#if 0
    gst_buffer_pool_wait_released (self->out_port_pool);
#endif
    GST_OMX_BUFFER_POOL (self->out_port_pool)->deactivated = TRUE;
    gst_object_unref (self->out_port_pool);
    self->out_port_pool = NULL;
  } else {
    OMX_ERRORTYPE err;

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
    err =
        gst_omx_port_deallocate_buffers (self->eglimage ? self->
        egl_out_port : self->dec_out_port);
#else
    err = gst_omx_port_deallocate_buffers (self->dec_out_port);
#endif

    return err == OMX_ErrorNone;
  }

  return TRUE;
}

static GstVideoInterlaceMode
gst_omx_video_dec_get_output_interlace_info (GstOMXVideoDec * self)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ERRORTYPE err;
  OMX_ALG_COMMON_PARAM_SEQUENCE_PICTURE_MODE seq_pic_mode;

  GST_OMX_INIT_STRUCT (&seq_pic_mode);
  seq_pic_mode.nPortIndex = self->dec_out_port->index;

  err = gst_omx_component_get_parameter (self->dec,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamCommonSequencePictureModeCurrent,
      &seq_pic_mode);

  if (err != OMX_ErrorNone) {
    if (err == OMX_ErrorUnsupportedIndex) {
      GST_WARNING_OBJECT (self,
          "Picture sequence mode not supported by the component");
    } else {
      GST_DEBUG_OBJECT (self,
          "Failed to get picture sequence mode: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
    }

    return GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  }

  if (seq_pic_mode.eMode == OMX_ALG_SEQUENCE_PICTURE_FIELD) {
    GST_DEBUG_OBJECT (self, "Decoding interlaced video frames");
    return GST_VIDEO_INTERLACE_MODE_ALTERNATE;
  } else if (seq_pic_mode.eMode == OMX_ALG_SEQUENCE_PICTURE_FRAME) {
    GST_DEBUG_OBJECT (self, "Decoding progressive video frames");
    return GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  } else {
    GST_ERROR_OBJECT (self, "Unsupported interlace format: (0x%08x)",
        seq_pic_mode.eMode);
    return GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  }

#endif
  return GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
}

#if defined (HAVE_GST_GL)
static void
add_caps_gl_memory_feature (GstCaps * caps)
{
  GstCapsFeatures *old, *features;

  features = gst_caps_features_new_empty ();
  old = gst_caps_get_features (caps, 0);

  if (old) {
    guint i;

    /* Copy the existing features ignoring memory ones as we are changing
     * it to GL. */
    for (i = 0; i < gst_caps_features_get_size (old); i++) {
      const gchar *f = gst_caps_features_get_nth (old, i);

      if (!g_str_has_prefix (f, "memory:"))
        gst_caps_features_add (features, f);
    }
  }

  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_caps_set_features (caps, 0, features);
}
#endif

static OMX_ERRORTYPE
gst_omx_video_dec_reconfigure_output_port (GstOMXVideoDec * self)
{
  GstOMXPort *port;
  OMX_ERRORTYPE err;
  GstVideoCodecState *state;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GstVideoFormat format;
  GstVideoInterlaceMode interlace_mode;
  guint frame_height;

  /* At this point the decoder output port is disabled */
  interlace_mode = gst_omx_video_dec_get_output_interlace_info (self);

#if defined (HAVE_GST_GL)
  {
#if defined (USE_OMX_TARGET_RPI)
    OMX_STATETYPE egl_state;
#endif

    if (self->eglimage) {
      /* Nothing to do here, we could however fall back to non-EGLImage in theory */
#if defined (USE_OMX_TARGET_RPI)
      port = self->egl_out_port;
#else
      port = self->dec_out_port;
#endif
      err = OMX_ErrorNone;
      goto enable_port;
    } else {
      /* Set up egl_render */

      self->eglimage = TRUE;

      gst_omx_port_get_port_definition (self->dec_out_port, &port_def);
      GST_VIDEO_DECODER_STREAM_LOCK (self);

      frame_height = port_def.format.video.nFrameHeight;
      /* OMX's frame height is actually the field height in alternate mode
       * while it's always the full frame height in gst. */
      if (interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE ||
          interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) {
        frame_height *= 2;
        /* Decoder outputs interlaced content using the alternate mode */
        interlace_mode = GST_VIDEO_INTERLACE_MODE_ALTERNATE;
      }

      state =
          gst_video_decoder_set_interlaced_output_state (GST_VIDEO_DECODER
          (self), GST_VIDEO_FORMAT_RGBA, interlace_mode,
          port_def.format.video.nFrameWidth, frame_height, self->input_state);

      /* at this point state->caps is NULL */
      if (state->caps)
        gst_caps_unref (state->caps);
      state->caps = gst_video_info_to_caps (&state->info);
      add_caps_gl_memory_feature (state->caps);

      /* try to negotiate with caps feature */
      if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {

        GST_DEBUG_OBJECT (self,
            "Failed to negotiate with feature %s",
            GST_CAPS_FEATURE_MEMORY_GL_MEMORY);

        if (state->caps)
          gst_caps_replace (&state->caps, NULL);

#if defined (USE_OMX_TARGET_RPI)
        /* fallback: try to use EGLImage even if it is not in the caps feature */
        if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
          gst_video_codec_state_unref (state);
          GST_DEBUG_OBJECT (self, "Failed to negotiate RGBA for EGLImage");
          GST_VIDEO_DECODER_STREAM_UNLOCK (self);
          goto no_egl;
        }
#else
        gst_video_codec_state_unref (state);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        goto no_egl;
#endif
      }

      gst_video_codec_state_unref (state);
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);

#if defined (USE_OMX_TARGET_RPI)
      /* Now link it all together */

      err = gst_omx_port_set_enabled (self->egl_in_port, FALSE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_wait_enabled (self->egl_in_port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_set_enabled (self->egl_out_port, FALSE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_wait_enabled (self->egl_out_port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto no_egl;

      {
#define OMX_IndexParamBrcmVideoEGLRenderDiscardMode 0x7f0000db
        OMX_CONFIG_PORTBOOLEANTYPE discardMode;
        memset (&discardMode, 0, sizeof (discardMode));
        discardMode.nSize = sizeof (discardMode);
        discardMode.nPortIndex = 220;
        discardMode.nVersion.nVersion = OMX_VERSION;
        discardMode.bEnabled = OMX_FALSE;
        if (gst_omx_component_set_parameter (self->egl_render,
                OMX_IndexParamBrcmVideoEGLRenderDiscardMode,
                &discardMode) != OMX_ErrorNone)
          goto no_egl;
#undef OMX_IndexParamBrcmVideoEGLRenderDiscardMode
      }

      err = gst_omx_setup_tunnel (self->dec_out_port, self->egl_in_port);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_set_enabled (self->egl_in_port, TRUE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_component_set_state (self->egl_render, OMX_StateIdle);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_wait_enabled (self->egl_in_port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto no_egl;

      if (gst_omx_component_get_state (self->egl_render,
              GST_CLOCK_TIME_NONE) != OMX_StateIdle)
        goto no_egl;

      err = gst_omx_video_dec_allocate_output_buffers (self);
      if (err != OMX_ErrorNone)
        goto no_egl;

      if (gst_omx_component_set_state (self->egl_render,
              OMX_StateExecuting) != OMX_ErrorNone)
        goto no_egl;

      if (gst_omx_component_get_state (self->egl_render,
              GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
        goto no_egl;

      err =
          gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err =
          gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, FALSE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err =
          gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, FALSE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_populate (self->egl_out_port);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_set_enabled (self->dec_out_port, TRUE);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_wait_enabled (self->dec_out_port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto no_egl;


      err = gst_omx_port_mark_reconfigured (self->dec_out_port);
      if (err != OMX_ErrorNone)
        goto no_egl;

      err = gst_omx_port_mark_reconfigured (self->egl_out_port);
      if (err != OMX_ErrorNone)
        goto no_egl;

      goto done;
#else
      port = self->dec_out_port;
      err = OMX_ErrorNone;
      goto enable_port;
#endif /* defined (USE_OMX_TARGET_RPI) */
    }

  no_egl:

#if defined (USE_OMX_TARGET_RPI)
    gst_omx_port_set_enabled (self->dec_out_port, FALSE);
    gst_omx_port_wait_enabled (self->dec_out_port, 1 * GST_SECOND);
    egl_state = gst_omx_component_get_state (self->egl_render, 0);
    if (egl_state > OMX_StateLoaded || egl_state == OMX_StateInvalid) {
      if (egl_state > OMX_StateIdle) {
        gst_omx_component_set_state (self->egl_render, OMX_StateIdle);
        gst_omx_component_get_state (self->egl_render, 5 * GST_SECOND);
      }
      gst_omx_component_set_state (self->egl_render, OMX_StateLoaded);

      gst_omx_video_dec_deallocate_output_buffers (self);
      gst_omx_close_tunnel (self->dec_out_port, self->egl_in_port);

      if (egl_state > OMX_StateLoaded) {
        gst_omx_component_get_state (self->egl_render, 5 * GST_SECOND);
      }
    }
#endif

    /* After this egl_render should be deactivated
     * and the decoder's output port disabled */
    self->eglimage = FALSE;
  }
#endif /* defined (HAVE_GST_GL) */

  port = self->dec_out_port;

  /* Update caps */
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  gst_omx_port_get_port_definition (port, &port_def);
  g_assert (port_def.format.video.eCompressionFormat == OMX_VIDEO_CodingUnused);

  format =
      gst_omx_video_get_format_from_omx (port_def.format.video.eColorFormat);

  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Unsupported color format: %d",
        port_def.format.video.eColorFormat);
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    err = OMX_ErrorUndefined;
    goto done;
  }

  frame_height = port_def.format.video.nFrameHeight;
  /* OMX's frame height is actually the field height in alternate mode
   * while it's always the full frame height in gst. */
  if (interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE ||
      interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) {
    frame_height *= 2;
    /* Decoder outputs interlaced content using the alternate mode */
    interlace_mode = GST_VIDEO_INTERLACE_MODE_ALTERNATE;
  }

  GST_DEBUG_OBJECT (self,
      "Setting output state: format %s (%d), width %u, height %u",
      gst_video_format_to_string (format),
      port_def.format.video.eColorFormat,
      (guint) port_def.format.video.nFrameWidth, frame_height);

  state =
      gst_video_decoder_set_interlaced_output_state (GST_VIDEO_DECODER (self),
      format, interlace_mode, port_def.format.video.nFrameWidth,
      frame_height, self->input_state);

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
    gst_video_codec_state_unref (state);
    GST_ERROR_OBJECT (self, "Failed to negotiate");
    err = OMX_ErrorUndefined;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    goto done;
  }

  gst_video_codec_state_unref (state);

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

#if defined (HAVE_GST_GL)
enable_port:
#endif

  err = gst_omx_video_dec_allocate_output_buffers (self);
  if (err != OMX_ErrorNone) {
#if defined (HAVE_GST_GL)
    /* TODO: works on desktop but need to try on RPI. */
#if !defined (USE_OMX_TARGET_RPI)
    if (self->eglimage) {
      GST_INFO_OBJECT (self, "Fallback to non eglimage");
      goto no_egl;
    }
#endif
#endif
    goto done;
  }

  err = gst_omx_port_populate (port);
  if (err != OMX_ErrorNone)
    goto done;

  err = gst_omx_port_mark_reconfigured (port);
  if (err != OMX_ErrorNone)
    goto done;

done:

  return err;
}

static void
gst_omx_video_dec_clean_older_frames (GstOMXVideoDec * self,
    GstOMXBuffer * buf, GList * frames)
{
  GList *l;
  GstClockTime timestamp;

  timestamp =
      gst_util_uint64_scale (GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp),
      GST_SECOND, OMX_TICKS_PER_SECOND);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    /* We could release all frames stored with pts < timestamp since the
     * decoder will likely output frames in display order */
    for (l = frames; l; l = l->next) {
      GstVideoCodecFrame *tmp = l->data;

      if (tmp->pts < timestamp) {
        GST_LOG_OBJECT (self,
            "discarding ghost frame %p (#%d) PTS:%" GST_TIME_FORMAT " DTS:%"
            GST_TIME_FORMAT, tmp, tmp->system_frame_number,
            GST_TIME_ARGS (tmp->pts), GST_TIME_ARGS (tmp->dts));
        gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), tmp);
      } else {
        gst_video_codec_frame_unref (tmp);
      }
    }
  } else {
    /* We will release all frames with invalid timestamp because we don't even
     * know if they will be output some day. */
    for (l = frames; l; l = l->next) {
      GstVideoCodecFrame *tmp = l->data;

      if (!GST_CLOCK_TIME_IS_VALID (tmp->pts)) {
        GST_LOG_OBJECT (self,
            "discarding frame %p (#%d) with invalid PTS:%" GST_TIME_FORMAT
            " DTS:%" GST_TIME_FORMAT, tmp, tmp->system_frame_number,
            GST_TIME_ARGS (tmp->pts), GST_TIME_ARGS (tmp->dts));
        gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), tmp);
      } else {
        gst_video_codec_frame_unref (tmp);
      }
    }
  }

  g_list_free (frames);
}

/* copy_frame() will consume @outpuf resulting in the buffer being released to
 * the pool and so reset fields such as outbuf->omx_buf->nFlags.
 * Make sure to handle them all before. */
static GstBuffer *
copy_frame (const GstVideoInfo * info, GstBuffer * outbuf)
{
  GstVideoInfo out_info, tmp_info;
  GstBuffer *tmpbuf;
  GstVideoFrame out_frame, tmp_frame;

  out_info = *info;
  tmp_info = *info;

  tmpbuf = gst_buffer_new_and_alloc (out_info.size);

  gst_video_frame_map (&out_frame, &out_info, outbuf, GST_MAP_READ);
  gst_video_frame_map (&tmp_frame, &tmp_info, tmpbuf, GST_MAP_WRITE);
  gst_video_frame_copy (&tmp_frame, &out_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&tmp_frame);

  /* Use gst_video_frame_copy() to copy the content of the buffer so it
   * will handle the stride/offset/etc from the source buffer.
   * It doesn't copy buffer flags so do it manually. */
  gst_buffer_copy_into (tmpbuf, outbuf, GST_BUFFER_COPY_FLAGS, 0, -1);

  gst_buffer_unref (outbuf);

  return tmpbuf;
}

static void
gst_omx_video_dec_pause_loop (GstOMXVideoDec * self, GstFlowReturn flow_ret)
{
  g_mutex_lock (&self->drain_lock);
  if (self->draining) {
    self->draining = FALSE;
    g_cond_broadcast (&self->drain_cond);
  }
  gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
  self->downstream_flow_ret = flow_ret;
  self->started = FALSE;
  g_mutex_unlock (&self->drain_lock);
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static void
set_outbuffer_interlace_flags (GstOMXBuffer * buf, GstBuffer * outbuf)
{
  if (buf->omx_buf->nFlags & OMX_ALG_BUFFERFLAG_TOP_FIELD) {
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TOP_FIELD);
  } else if (buf->omx_buf->nFlags & OMX_ALG_BUFFERFLAG_BOT_FIELD) {
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD);
  }
}
#endif // USE_OMX_TARGET_ZYNQ_USCALE_PLUS

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXPort *port;
  GstOMXBuffer *buf = NULL;
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  port = self->eglimage ? self->egl_out_port : self->dec_out_port;
#else
  port = self->dec_out_port;
#endif

  acq_return = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (self)) ||
      acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    GstVideoCodecState *state;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    GstVideoFormat format;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    /* Reallocate all buffers */
    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE
        && gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      if (!gst_omx_video_dec_deallocate_output_buffers (self))
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* We have the possibility to reconfigure everything now */
      err = gst_omx_video_dec_reconfigure_output_port (self);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    } else {
      GstVideoInterlaceMode interlace_mode;

      /* Just update caps */
      GST_VIDEO_DECODER_STREAM_LOCK (self);

      gst_omx_port_get_port_definition (port, &port_def);
      g_assert (port_def.format.video.eCompressionFormat ==
          OMX_VIDEO_CodingUnused);

      format =
          gst_omx_video_get_format_from_omx (port_def.format.video.
          eColorFormat);

      if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_ERROR_OBJECT (self, "Unsupported color format: %d",
            port_def.format.video.eColorFormat);
        if (buf)
          gst_omx_port_release_buffer (port, buf);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        goto caps_failed;
      }

      GST_DEBUG_OBJECT (self,
          "Setting output state: format %s (%d), width %u, height %u",
          gst_video_format_to_string (format),
          port_def.format.video.eColorFormat,
          (guint) port_def.format.video.nFrameWidth,
          (guint) port_def.format.video.nFrameHeight);
      interlace_mode = gst_omx_video_dec_get_output_interlace_info (self);

      state =
          gst_video_decoder_set_interlaced_output_state (GST_VIDEO_DECODER
          (self), format, interlace_mode, port_def.format.video.nFrameWidth,
          port_def.format.video.nFrameHeight, self->input_state);

      /* Take framerate and pixel-aspect-ratio from sinkpad caps */

      if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
        if (buf)
          gst_omx_port_release_buffer (port, buf);
        gst_video_codec_state_unref (state);
        goto caps_failed;
      }

      gst_video_codec_state_unref (state);

      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK) {
      return;
    }
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);

  /* This prevents a deadlock between the srcpad stream
   * lock and the videocodec stream lock, if ::reset()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (port, buf);
    goto flushing;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x (%s) %" G_GUINT64_FORMAT,
      (guint) buf->omx_buf->nFlags,
      gst_omx_buffer_flags_to_string (buf->omx_buf->nFlags),
      (guint64) GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp));

  frame = gst_omx_video_find_nearest_frame (GST_ELEMENT_CAST (self), buf,
      gst_video_decoder_get_frames (GST_VIDEO_DECODER (self)));

  /* So we have a timestamped OMX buffer and get, or not, corresponding frame.
   * Assuming decoder output frames in display order, frames preceding this
   * frame could be discarded as they seems useless due to e.g interlaced
   * stream, corrupted input data...
   * In any cases, not likely to be seen again. so drop it before they pile up
   * and use all the memory. */
  gst_omx_video_dec_clean_older_frames (self, buf,
      gst_video_decoder_get_frames (GST_VIDEO_DECODER (self)));

  if (!frame && (buf->omx_buf->nFilledLen > 0 || buf->eglimage)) {
    GstBuffer *outbuf = NULL;

    /* This sometimes happens at EOS or if the input is not properly framed,
     * let's handle it gracefully by allocating a new buffer for the current
     * caps and filling it
     */

    GST_ERROR_OBJECT (self, "No corresponding frame found");

    if (self->out_port_pool) {
      gint i, n;
      GstBufferPoolAcquireParams params = { 0, };

      n = port->buffers->len;
      for (i = 0; i < n; i++) {
        GstOMXBuffer *tmp = g_ptr_array_index (port->buffers, i);

        if (tmp == buf)
          break;
      }
      g_assert (i != n);

      GST_OMX_BUFFER_POOL (self->out_port_pool)->current_buffer_index = i;
      flow_ret =
          gst_buffer_pool_acquire_buffer (self->out_port_pool, &outbuf,
          &params);
      if (flow_ret != GST_FLOW_OK) {
        gst_omx_port_release_buffer (port, buf);
        goto invalid_buffer;
      }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      set_outbuffer_interlace_flags (buf, outbuf);
#endif

      if (GST_OMX_BUFFER_POOL (self->out_port_pool)->need_copy)
        outbuf =
            copy_frame (&GST_OMX_BUFFER_POOL (self->out_port_pool)->video_info,
            outbuf);

      buf = NULL;
    } else {
      outbuf =
          gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));
      if (!gst_omx_video_dec_fill_buffer (self, buf, outbuf)) {
        gst_buffer_unref (outbuf);
        gst_omx_port_release_buffer (port, buf);
        goto invalid_buffer;
      }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      set_outbuffer_interlace_flags (buf, outbuf);
#endif
    }

    flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
  } else if (buf->omx_buf->nFilledLen > 0 || buf->eglimage) {
    if (self->out_port_pool) {
      gint i, n;
      GstBuffer *outbuf;
      GstBufferPoolAcquireParams params = { 0, };

      n = port->buffers->len;
      for (i = 0; i < n; i++) {
        GstOMXBuffer *tmp = g_ptr_array_index (port->buffers, i);

        if (tmp == buf)
          break;
      }
      g_assert (i != n);

      GST_OMX_BUFFER_POOL (self->out_port_pool)->current_buffer_index = i;
      flow_ret =
          gst_buffer_pool_acquire_buffer (self->out_port_pool,
          &outbuf, &params);
      if (flow_ret != GST_FLOW_OK) {
        flow_ret =
            gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
        frame = NULL;
        gst_omx_port_release_buffer (port, buf);
        goto invalid_buffer;
      }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      set_outbuffer_interlace_flags (buf, outbuf);
#endif

      if (GST_OMX_BUFFER_POOL (self->out_port_pool)->need_copy)
        outbuf =
            copy_frame (&GST_OMX_BUFFER_POOL (self->out_port_pool)->video_info,
            outbuf);

      frame->output_buffer = outbuf;

      flow_ret =
          gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
      frame = NULL;
      buf = NULL;
    } else {
      if ((flow_ret =
              gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER
                  (self), frame)) == GST_FLOW_OK) {
        /* FIXME: This currently happens because of a race condition too.
         * We first need to reconfigure the output port and then the input
         * port if both need reconfiguration.
         */
        if (!gst_omx_video_dec_fill_buffer (self, buf, frame->output_buffer)) {
          gst_buffer_replace (&frame->output_buffer, NULL);
          flow_ret =
              gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
          frame = NULL;
          gst_omx_port_release_buffer (port, buf);
          goto invalid_buffer;
        }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
        set_outbuffer_interlace_flags (buf, frame->output_buffer);
#endif

        flow_ret =
            gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
        frame = NULL;
      }
    }
  } else if (frame != NULL) {
    /* Just ignore empty buffers, don't drop a frame for that */
    flow_ret = GST_FLOW_OK;
    gst_video_codec_frame_unref (frame);
    frame = NULL;
  }

  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  if (buf) {
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (self);
  self->downstream_flow_ret = flow_ret;
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_dec_pause_loop (self, GST_FLOW_ERROR);
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_omx_video_dec_pause_loop (self, GST_FLOW_FLUSHING);
    return;
  }

eos:
  {
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GstQuery *query = gst_query_new_drain ();

      /* Drain the pipeline to reclaim all memories back to the pool */
      if (!gst_pad_peer_query (GST_VIDEO_DECODER_SRC_PAD (self), query))
        GST_DEBUG_OBJECT (self, "drain query failed");
      gst_query_unref (query);

      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    GST_VIDEO_DECODER_STREAM_LOCK (self);
    self->downstream_flow_ret = flow_ret;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
    } else if (flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
    } else if (flow_ret == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    }
    gst_omx_video_dec_pause_loop (self, flow_ret);
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_dec_pause_loop (self, GST_FLOW_ERROR);
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_dec_pause_loop (self, GST_FLOW_NOT_NEGOTIATED);
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_dec_pause_loop (self, GST_FLOW_NOT_NEGOTIATED);
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_dec_pause_loop (self, GST_FLOW_ERROR);
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_omx_video_dec_start (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->use_buffers = FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_dec_stop (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, TRUE);
#endif

  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->dec, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->dec, OMX_StateIdle);
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  if (gst_omx_component_get_state (self->egl_render, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->egl_render, OMX_StateIdle);
#endif

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  gst_omx_component_get_state (self->egl_render, 1 * GST_SECOND);
#endif

  gst_buffer_replace (&self->codec_data, NULL);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_negotiate (GstOMXVideoDec * self)
{
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GstCaps *comp_supported_caps;
  GList *negotiation_map = NULL, *l;
  GstCaps *templ_caps, *intersection;
  GstVideoFormat format;
  GstStructure *s;
  const gchar *format_str;

  GST_DEBUG_OBJECT (self, "Trying to negotiate a video format with downstream");

  templ_caps = gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  intersection =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (self), templ_caps);
  gst_caps_unref (templ_caps);

  GST_DEBUG_OBJECT (self, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  negotiation_map =
      gst_omx_video_get_supported_colorformats (self->dec_out_port,
      self->input_state);

  comp_supported_caps = gst_omx_video_get_caps_for_map (negotiation_map);

  GST_DEBUG_OBJECT (self, "Decoder supported caps: %" GST_PTR_FORMAT,
      comp_supported_caps);

  if (!gst_caps_is_empty (comp_supported_caps)) {
    GstCaps *tmp;

    tmp = gst_caps_intersect (comp_supported_caps, intersection);
    gst_caps_unref (intersection);
    intersection = tmp;
  }
  gst_caps_unref (comp_supported_caps);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (self, "Empty caps");
    g_list_free_full (negotiation_map,
        (GDestroyNotify) gst_omx_video_negotiation_map_free);
    return FALSE;
  }

  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  if (!format_str ||
      (format =
          gst_video_format_from_string (format_str)) ==
      GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (intersection);
    g_list_free_full (negotiation_map,
        (GDestroyNotify) gst_omx_video_negotiation_map_free);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->dec_out_port->index;

  err = gst_omx_component_get_parameter (self->dec,
      OMX_IndexParamVideoPortFormat, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to get video port format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  for (l = negotiation_map; l; l = l->next) {
    GstOMXVideoNegotiationMap *m = l->data;

    if (m->format == format) {
      param.eColorFormat = m->type;
      break;
    }
  }

  GST_DEBUG_OBJECT (self, "Negotiating color format %s (%d)", format_str,
      param.eColorFormat);

  /* We must find something here */
  g_assert (l != NULL);
  g_list_free_full (negotiation_map,
      (GDestroyNotify) gst_omx_video_negotiation_map_free);

  err =
      gst_omx_component_set_parameter (self->dec,
      OMX_IndexParamVideoPortFormat, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set video port format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  gst_caps_unref (intersection);
  return (err == OMX_ErrorNone);
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static void
gst_omx_video_dec_set_latency (GstOMXVideoDec * self)
{
  GstClockTime latency;
  OMX_ALG_PARAM_REPORTED_LATENCY param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  err =
      gst_omx_component_get_parameter (self->dec,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamReportedLatency, &param);

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self, "Couldn't retrieve latency: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return;
  }

  GST_DEBUG_OBJECT (self, "retrieved latency of %d ms",
      (guint32) param.nLatency);

  /* Convert to ns */
  latency = param.nLatency * GST_MSECOND;

  gst_video_decoder_set_latency (GST_VIDEO_DECODER (self), latency, latency);
}
#endif

static gboolean
gst_omx_video_dec_disable (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  GstOMXPort *out_port =
      self->eglimage ? self->egl_out_port : self->dec_out_port;
#else
  GstOMXPort *out_port = self->dec_out_port;
#endif

  GST_DEBUG_OBJECT (self, "Need to disable and drain decoder");

  gst_omx_video_dec_drain (GST_VIDEO_DECODER (self));
  gst_omx_port_set_flushing (out_port, 5 * GST_SECOND, TRUE);

  if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    gst_omx_video_dec_stop (GST_VIDEO_DECODER (self));
    gst_omx_video_dec_close (GST_VIDEO_DECODER (self));
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (!gst_omx_video_dec_open (GST_VIDEO_DECODER (self)))
      return FALSE;

    self->disabled = FALSE;
  } else {
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
    if (self->eglimage) {
      gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
      gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);
      gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, TRUE);
      gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, TRUE);
    }
#endif

    /* Disabling at the same time input port and output port is only
     * required when a buffer is shared between the ports. This cannot
     * be the case for a decoder because its input and output buffers
     * are of different nature. So let's disable ports sequencially.
     * Starting from IL 1.2.0, this point has been clarified.
     * OMX_SendCommand will return an error if the IL client attempts to
     * call it when there is already an on-going command being processed.
     * The exception is for buffer sharing above and the event
     * OMX_EventPortNeedsDisable will be sent to request disabling the
     * other port at the same time. */
    if (gst_omx_port_set_enabled (self->dec_in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_buffers_released (self->dec_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_deallocate_buffers (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_enabled (self->dec_in_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_port_set_enabled (out_port, FALSE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_buffers_released (out_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (!gst_omx_video_dec_deallocate_output_buffers (self))
      return FALSE;
    if (gst_omx_port_wait_enabled (out_port, 1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
    if (self->eglimage) {
      OMX_STATETYPE egl_state;

      egl_state = gst_omx_component_get_state (self->egl_render, 0);
      if (egl_state > OMX_StateLoaded || egl_state == OMX_StateInvalid) {

        if (egl_state > OMX_StateIdle) {
          gst_omx_component_set_state (self->egl_render, OMX_StateIdle);
          gst_omx_component_set_state (self->dec, OMX_StateIdle);
          egl_state = gst_omx_component_get_state (self->egl_render,
              5 * GST_SECOND);
          gst_omx_component_get_state (self->dec, 1 * GST_SECOND);
        }
        gst_omx_component_set_state (self->egl_render, OMX_StateLoaded);
        gst_omx_component_set_state (self->dec, OMX_StateLoaded);

        gst_omx_close_tunnel (self->dec_out_port, self->egl_in_port);

        if (egl_state > OMX_StateLoaded) {
          gst_omx_component_get_state (self->egl_render, 5 * GST_SECOND);
        }

        gst_omx_component_set_state (self->dec, OMX_StateIdle);

        gst_omx_component_set_state (self->dec, OMX_StateExecuting);
        gst_omx_component_get_state (self->dec, GST_CLOCK_TIME_NONE);
      }
      self->eglimage = FALSE;
    }
#endif

    self->disabled = TRUE;
  }
  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  GST_DEBUG_OBJECT (self, "Decoder drained and disabled");
  return TRUE;
}

static gboolean
gst_omx_video_dec_allocate_in_buffers (GstOMXVideoDec * self)
{
  switch (self->input_allocation) {
    case GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER:
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      break;
    case GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC:
      if (gst_omx_port_use_dynamic_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      break;
    case GST_OMX_BUFFER_ALLOCATION_USE_BUFFER:
    default:
      /* Not supported */
      g_return_val_if_reached (FALSE);
  }

  return TRUE;
}

static gboolean
check_input_alignment (GstOMXVideoDec * self, GstMapInfo * map)
{
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->dec_in_port->port_def;

  if (port_def->nBufferAlignment &&
      (GPOINTER_TO_UINT (map->data) & (port_def->nBufferAlignment - 1)) != 0) {
    GST_DEBUG_OBJECT (self,
        "input buffer is not properly aligned (address: %p alignment: %u bytes), can't use dynamic allocation",
        map->data, (guint32) port_def->nBufferAlignment);
    return FALSE;
  }

  return TRUE;
}

/* Check if @inbuf's alignment matches the requirements to use the
 * dynamic buffer mode. */
static gboolean
can_use_dynamic_buffer_mode (GstOMXVideoDec * self, GstBuffer * inbuf)
{
  gboolean result = TRUE;
  guint i;

  for (i = 0; i < gst_buffer_n_memory (inbuf) && result; i++) {
    GstMemory *mem = gst_buffer_peek_memory (inbuf, i);
    GstMapInfo map;

    if (!gst_memory_map (mem, &map, GST_MAP_READ)) {
      GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
          ("failed to map input buffer"));
      return FALSE;
    }

    result = check_input_alignment (self, &map);

    gst_memory_unmap (mem, &map);
  }

  return result;
}

/* Choose the allocation mode for input buffers depending of what's supported by
 * the component and the size/alignment of the input buffer. */
static GstOMXBufferAllocation
gst_omx_video_dec_pick_input_allocation_mode (GstOMXVideoDec * self,
    GstBuffer * inbuf)
{
  if (!gst_omx_is_dynamic_allocation_supported ())
    return GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;

  if (can_use_dynamic_buffer_mode (self, inbuf)) {
    GST_DEBUG_OBJECT (self,
        "input buffer is properly aligned, use dynamic allocation");
    return GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC;
  }

  GST_DEBUG_OBJECT (self, "let input buffer allocate its buffers");
  return GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;
}

static gboolean
gst_omx_video_dec_ensure_nb_in_buffers (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  if ((klass->cdata.hacks & GST_OMX_HACK_ENSURE_BUFFER_COUNT_ACTUAL)) {
    if (!gst_omx_port_ensure_buffer_count_actual (self->dec_in_port, 0))
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_video_dec_enable (GstOMXVideoDec * self, GstBuffer * input)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Enabling component");

  self->input_allocation = gst_omx_video_dec_pick_input_allocation_mode (self,
      input);

  if (self->disabled) {
    if (!gst_omx_video_dec_ensure_nb_in_buffers (self))
      return FALSE;
    if (gst_omx_port_set_enabled (self->dec_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (!gst_omx_video_dec_allocate_in_buffers (self))
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->dec_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->dec_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_mark_reconfigured (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (!gst_omx_video_dec_negotiate (self))
      GST_LOG_OBJECT (self, "Negotiation failed, will get output format later");

    if (!gst_omx_video_dec_ensure_nb_in_buffers (self))
      return FALSE;

    if (!(klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      /* Disable output port */
      if (gst_omx_port_set_enabled (self->dec_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (!gst_omx_video_dec_allocate_in_buffers (self))
        return FALSE;
    } else {
      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (!gst_omx_video_dec_allocate_in_buffers (self))
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->dec,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->dec) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->dec),
        gst_omx_component_get_last_error (self->dec));
    return FALSE;
  }

  self->disabled = FALSE;

  return TRUE;
}

static OMX_COLOR_FORMATTYPE
get_color_format_from_chroma (const gchar * chroma_format,
    guint bit_depth_luma, guint bit_depth_chroma)
{
  if (chroma_format == NULL)
    goto out;

  if (!g_strcmp0 (chroma_format, "4:0:0") && bit_depth_chroma == 0) {
    switch (bit_depth_luma) {
      case 1:
        return OMX_COLOR_FormatMonochrome;
      case 2:
        return OMX_COLOR_FormatL2;
      case 4:
        return OMX_COLOR_FormatL4;
      case 8:
        return OMX_COLOR_FormatL8;
      case 16:
        return OMX_COLOR_FormatL16;
      case 24:
        return OMX_COLOR_FormatL24;
      case 32:
        return OMX_COLOR_FormatL32;
    }
    goto out;
  }

  if (bit_depth_luma == 8 && bit_depth_chroma == 8) {
    if (!g_strcmp0 (chroma_format, "4:2:0"))
      return OMX_COLOR_FormatYUV420SemiPlanar;
    else if (!g_strcmp0 (chroma_format, "4:2:2"))
      return OMX_COLOR_FormatYUV422SemiPlanar;
  }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (bit_depth_luma == 10 && bit_depth_chroma == 10) {
    if (!g_strcmp0 (chroma_format, "4:2:0"))
      return (OMX_COLOR_FORMATTYPE)
          OMX_ALG_COLOR_FormatYUV420SemiPlanar10bitPacked;
    else if (!g_strcmp0 (chroma_format, "4:2:2"))
      return (OMX_COLOR_FORMATTYPE)
          OMX_ALG_COLOR_FormatYUV422SemiPlanar10bitPacked;
  }
#endif

out:
  return OMX_COLOR_FormatUnused;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static gboolean
gst_omx_video_dec_set_interlacing_parameters (GstOMXVideoDec * self,
    GstVideoInfo * info)
{
  OMX_ERRORTYPE err;
  OMX_ALG_COMMON_PARAM_SEQUENCE_PICTURE_MODE seq_pic_mode;

  GST_OMX_INIT_STRUCT (&seq_pic_mode);
  seq_pic_mode.nPortIndex = self->dec_in_port->index;

  err = gst_omx_component_get_parameter (self->dec,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamCommonSequencePictureModeCurrent,
      &seq_pic_mode);

  if (err != OMX_ErrorNone) {
    if (err == OMX_ErrorUnsupportedIndex) {
      GST_WARNING_OBJECT (self,
          "Picture sequence mode not supported by the component");
    } else {
      GST_DEBUG_OBJECT (self,
          "Failed to get picture sequence mode: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
    }

    return FALSE;
  }

  if (info->interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE ||
      info->interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED)
    seq_pic_mode.eMode = OMX_ALG_SEQUENCE_PICTURE_FIELD;
  else if (info->interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)
    seq_pic_mode.eMode = OMX_ALG_SEQUENCE_PICTURE_FRAME;
  else {
    /* Caps templates should ensure this doesn't happen but just to be safe.. */
    GST_ERROR_OBJECT (self, "Video interlacing mode %s not supported",
        gst_video_interlace_mode_to_string (info->interlace_mode));
    return FALSE;
  }

  err = gst_omx_component_set_parameter (self->dec,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamCommonSequencePictureModeCurrent,
      &seq_pic_mode);

  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting picture sequence mode not supported by the component");
  } else if (err == OMX_ErrorUnsupportedSetting) {
    GST_WARNING_OBJECT (self,
        "Interlaced picture sequence mode not supported by the component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set picture sequence mode: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "Video interlacing mode %s set on component",
        gst_video_interlace_mode_to_string (info->interlace_mode));
  }

  return TRUE;
}
#endif // USE_OMX_TARGET_ZYNQ_USCALE_PLUS

static gboolean
gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstVideoInfo *info = &state->info;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_U32 framerate_q16 = gst_omx_video_calculate_framerate_q16 (info);

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

  if (!self->dmabuf
      && gst_caps_features_contains (gst_caps_get_features (state->caps, 0),
          GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    GST_WARNING_OBJECT (self,
        "caps has the 'memory:DMABuf' feature but decoder cannot produce dmabuf");
    return FALSE;
  }

  gst_omx_port_get_port_definition (self->dec_in_port, &port_def);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_format_change |= port_def.format.video.nFrameWidth != info->width;
  is_format_change |=
      port_def.format.video.nFrameHeight != GST_VIDEO_INFO_FIELD_HEIGHT (info);
  is_format_change |= (port_def.format.video.xFramerate == 0
      && info->fps_n != 0)
      || !gst_omx_video_is_equal_framerate_q16 (port_def.format.
      video.xFramerate, framerate_q16);
  is_format_change |= (self->codec_data != state->codec_data);
  if (klass->is_format_change)
    is_format_change |=
        klass->is_format_change (self, self->dec_in_port, state);

  needs_disable =
      gst_omx_component_get_state (self->dec,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    if (self->input_state)
      gst_video_codec_state_unref (self->input_state);
    self->input_state = gst_video_codec_state_ref (state);
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    if (!gst_omx_video_dec_disable (self))
      return FALSE;

    if (!self->disabled) {
      /* The local port_def is now obsolete so get it again. */
      gst_omx_port_get_port_definition (self->dec_in_port, &port_def);
    }
  }

  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = GST_VIDEO_INFO_HEIGHT (info);
  /*We cannot use GST_VIDEO_INFO_FIELD_HEIGHT() as encoded content may use either
   * interlace-mode=interleaved or alternate. In both case we'll output alternate
   * so the OMX frame height needs to be halfed. */
  if (GST_VIDEO_INFO_IS_INTERLACED (info))
    port_def.format.video.nFrameHeight =
        GST_ROUND_UP_2 (port_def.format.video.nFrameHeight / 2);
  port_def.format.video.xFramerate = framerate_q16;

  if (klass->cdata.hacks & GST_OMX_HACK_PASS_COLOR_FORMAT_TO_DECODER) {
    /* Let the decoder know the colar format of the encoded input stream.
     * It may use it to pre-allocate its internal buffers and so save time when
     * it will actually start to decode. */
    GstStructure *s;
    const gchar *chroma_format;
    guint bit_depth_luma, bit_depth_chroma;

    s = gst_caps_get_structure (state->caps, 0);
    chroma_format = gst_structure_get_string (s, "chroma-format");
    if (s && gst_structure_get_uint (s, "bit-depth-luma", &bit_depth_luma) &&
        gst_structure_get_uint (s, "bit-depth-chroma", &bit_depth_chroma)) {
      OMX_COLOR_FORMATTYPE color_format;

      color_format =
          get_color_format_from_chroma (chroma_format,
          bit_depth_luma, bit_depth_chroma);
      if (color_format != OMX_COLOR_FormatUnused) {
        GST_DEBUG_OBJECT (self, "Setting input eColorFormat to %d",
            color_format);
        port_def.format.video.eColorFormat = color_format;
      } else {
        GST_WARNING_OBJECT (self,
            "Unsupported input color format: %s (luma %d bits, chroma %d bits)",
            chroma_format, bit_depth_luma, bit_depth_chroma);
      }
    } else {
      GST_DEBUG_OBJECT (self,
          "Input color format info not present in caps, can't pass them to decoder");
    }
  }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (!gst_omx_video_dec_set_interlacing_parameters (self, info))
    return FALSE;
#endif

  GST_DEBUG_OBJECT (self, "Setting inport port definition");

  if (gst_omx_port_update_port_definition (self->dec_in_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

  if (klass->set_format) {
    if (!klass->set_format (self, self->dec_in_port, state)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "Updating ports definition");
  if (gst_omx_port_update_port_definition (self->dec_out_port,
          NULL) != OMX_ErrorNone)
    return FALSE;
  if (gst_omx_port_update_port_definition (self->dec_in_port,
          NULL) != OMX_ErrorNone)
    return FALSE;

  gst_buffer_replace (&self->codec_data, state->codec_data);
  self->input_state = gst_video_codec_state_ref (state);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  gst_omx_video_dec_set_latency (self);
#endif

  self->downstream_flow_ret = GST_FLOW_OK;
  return TRUE;
}

static gboolean
gst_omx_video_dec_flush (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);
  OMX_ERRORTYPE err = OMX_ErrorNone;

  GST_DEBUG_OBJECT (self, "Flushing decoder");

  if (gst_omx_component_get_state (self->dec, 0) == OMX_StateLoaded)
    return TRUE;

  /* 0) Pause the components */
  if (gst_omx_component_get_state (self->dec, 0) == OMX_StateExecuting) {
    gst_omx_component_set_state (self->dec, OMX_StatePause);
    gst_omx_component_get_state (self->dec, GST_CLOCK_TIME_NONE);
  }
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  if (self->eglimage) {
    if (gst_omx_component_get_state (self->egl_render, 0) == OMX_StateExecuting) {
      gst_omx_component_set_state (self->egl_render, OMX_StatePause);
      gst_omx_component_get_state (self->egl_render, GST_CLOCK_TIME_NONE);
    }
  }
#endif

  /* 1) Flush the ports */
  GST_DEBUG_OBJECT (self, "flushing ports");
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  if (self->eglimage) {
    gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, TRUE);
    gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, TRUE);
  }
#endif

  /* 2) Wait until the srcpad loop is stopped,
   * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));
  GST_DEBUG_OBJECT (self, "Flushing -- task stopped");
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  /* 3) Resume components */
  gst_omx_component_set_state (self->dec, OMX_StateExecuting);
  gst_omx_component_get_state (self->dec, GST_CLOCK_TIME_NONE);
#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  if (self->eglimage) {
    gst_omx_component_set_state (self->egl_render, OMX_StateExecuting);
    gst_omx_component_get_state (self->egl_render, GST_CLOCK_TIME_NONE);
  }
#endif

  /* 4) Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_GL)
  if (self->eglimage) {
    gst_omx_port_set_flushing (self->egl_in_port, 5 * GST_SECOND, FALSE);
    gst_omx_port_set_flushing (self->egl_out_port, 5 * GST_SECOND, FALSE);
    err = gst_omx_port_populate (self->egl_out_port);
    gst_omx_port_mark_reconfigured (self->egl_out_port);
  } else {
    err = gst_omx_port_populate (self->dec_out_port);
  }
#else
  err = gst_omx_port_populate (self->dec_out_port);
#endif

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self, "Failed to populate output port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  /* Reset our state */
  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  GST_DEBUG_OBJECT (self, "Flush finished");

  return TRUE;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoDec *self;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0, size;
  GstClockTime timestamp, duration;
  OMX_ERRORTYPE err;
  gboolean done = FALSE;
  gboolean first_ouput_buffer = TRUE;
  guint memory_idx = 0;         /* only used in dynamic buffer mode */
  gboolean last_subframe = GST_BUFFER_FLAG_IS_SET (frame->input_buffer,
      GST_VIDEO_BUFFER_FLAG_MARKER);
  gboolean header =
      GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_BUFFER_FLAG_HEADER);
  gboolean subframe_mode = gst_video_decoder_get_subframe_mode (decoder);

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self,
      "Handling frame %p last_subframe=%d header %d subframes %d", frame,
      last_subframe, header, frame->abidata.ABI.num_subframes);

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    return self->downstream_flow_ret;
  }

  if (!self->started) {
    if (!GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) && !header) {
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
      return GST_FLOW_OK;
    }

    if (gst_omx_port_is_flushing (self->dec_out_port)) {
      if (!gst_omx_video_dec_enable (self, frame->input_buffer))
        goto enable_error;
    }

    GST_DEBUG_OBJECT (self, "Starting task");
    gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
        (GstTaskFunction) gst_omx_video_dec_loop, decoder, NULL);
  }

  timestamp = frame->pts;
  duration = frame->duration;
  port = self->dec_in_port;

  size = gst_buffer_get_size (frame->input_buffer);
  while (!done) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      if (!gst_omx_video_dec_ensure_nb_in_buffers (self)) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      if (!gst_omx_video_dec_allocate_in_buffers (self)) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      continue;
    }
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      goto flow_error;
    }

    if (self->codec_data) {
      GST_DEBUG_OBJECT (self, "Passing codec data to the component");

      codec_data = self->codec_data;

      if (self->input_allocation ==
          GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC) {
        /* Map the full buffer, this may lead to copying if for some reason its
         * content is split on more than one memory but that seems unlikely and
         * the codec data aren't supposed to be that big anyway. */
        if (!gst_omx_buffer_map_buffer (buf, codec_data))
          goto map_failed;
      } else {
        if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
            gst_buffer_get_size (codec_data)) {
          gst_omx_port_release_buffer (port, buf);
          goto too_large_codec_data;
        }

        buf->omx_buf->nFilledLen = gst_buffer_get_size (codec_data);;
        gst_buffer_extract (codec_data, 0,
            buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
            buf->omx_buf->nFilledLen);
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;

      if (GST_CLOCK_TIME_IS_VALID (timestamp))
        GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
            gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND,
                GST_SECOND));
      else
        GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp, G_GUINT64_CONSTANT (0));
      buf->omx_buf->nTickCount = 0;

      self->started = TRUE;
      err = gst_omx_port_release_buffer (port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      if (err != OMX_ErrorNone)
        goto release_error;
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */

    if (self->input_allocation == GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC) {
      /* Transfer the buffer content per memory rather than mapping the full
       * buffer to prevent copies. */
      GstMemory *mem = gst_buffer_peek_memory (frame->input_buffer, memory_idx);

      GST_LOG_OBJECT (self,
          "Transferring %" G_GSIZE_FORMAT " bytes to the component",
          gst_memory_get_sizes (mem, NULL, NULL));

      if (!gst_omx_buffer_map_memory (buf, mem))
        goto map_failed;

      if (!check_input_alignment (self, &buf->map)) {
        GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
            ("input buffer now has wrong alignment/stride, can't use dynamic allocation any more"));
        return FALSE;
      }

      memory_idx++;
      if (memory_idx == gst_buffer_n_memory (frame->input_buffer))
        done = TRUE;
    } else {
      /* Copy the buffer content in chunks of size as requested
       * by the port */
      buf->omx_buf->nFilledLen =
          MIN (size - offset, buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);

      GST_LOG_OBJECT (self,
          "Copying %d bytes (frame offset %d) to the component",
          (guint) buf->omx_buf->nFilledLen, offset);

      gst_buffer_extract (frame->input_buffer, offset,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);

      offset += buf->omx_buf->nFilledLen;
      if (offset == size)
        done = TRUE;
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND));
      self->last_upstream_ts = timestamp;
    } else {
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp, G_GUINT64_CONSTANT (0));
    }

    if (duration != GST_CLOCK_TIME_NONE && first_ouput_buffer) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (duration, OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts += duration;
    } else {
      buf->omx_buf->nTickCount = 0;
    }

    if (first_ouput_buffer && GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

    if (header)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     */

    if (done) {
      /* If the input buffer is a subframe mark the OMX buffer as such */
      if (subframe_mode && !last_subframe) {
#ifdef OMX_BUFFERFLAG_ENDOFSUBFRAME
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFSUBFRAME;
#endif
      } else {
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
        if (subframe_mode && last_subframe)
          gst_video_decoder_have_last_subframe (decoder, frame);
      }
    }

    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;

    first_ouput_buffer = FALSE;
  }

  gst_video_codec_frame_unref (frame);

  GST_DEBUG_OBJECT (self, "Passed frame to component");

  return self->downstream_flow_ret;

full_buffer:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }

flow_error:
  {
    gst_video_codec_frame_unref (frame);

    return self->downstream_flow_ret;
  }

too_large_codec_data:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port "
            "(%" G_GSIZE_FORMAT " > %u)", gst_buffer_get_size (codec_data),
            (guint) self->dec_in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

map_failed:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("failed to map input buffer"));
    return GST_FLOW_ERROR;
  }

enable_error:
  {
    /* Report the OMX error, if any */
    if (gst_omx_component_get_last_error (self->dec) != OMX_ErrorNone)
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
          ("Failed to enable OMX decoder: %s (0x%08x)",
              gst_omx_component_get_last_error_string (self->dec),
              gst_omx_component_get_last_error (self->dec)));
    else
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
          ("Failed to enable OMX decoder"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

component_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    gst_video_codec_frame_unref (frame);
    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
release_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_video_dec_drain (GstVideoDecoder * decoder)
{
  gboolean ret;
  ret = gst_omx_video_dec_finish (decoder);
  gst_omx_video_dec_flush (decoder);
  return ret;
}

static GstFlowReturn
gst_omx_video_dec_finish (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->dec_in_port, &buf, GST_OMX_WAIT);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
      gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
          GST_SECOND));
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  err = gst_omx_port_release_buffer (self->dec_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (self, "Waiting until component is drained");

  if (G_UNLIKELY (self->dec->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
    gint64 wait_until = g_get_monotonic_time () + G_TIME_SPAN_SECOND / 2;

    if (!g_cond_wait_until (&self->drain_cond, &self->drain_lock, wait_until))
      GST_WARNING_OBJECT (self, "Drain timed out");
    else
      GST_DEBUG_OBJECT (self, "Drained component");

  } else {
    g_cond_wait (&self->drain_cond, &self->drain_lock);
    GST_DEBUG_OBJECT (self, "Drained component");
  }

  g_mutex_unlock (&self->drain_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}

static gboolean
gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (bdec);
  guint i;

#if defined (HAVE_GST_GL)
  {
    GstCaps *caps;
    gint i, n;
    GstVideoInfo info;

    gst_query_parse_allocation (query, &caps, NULL);
    if (caps && gst_video_info_from_caps (&info, caps)
        && info.finfo->format == GST_VIDEO_FORMAT_RGBA) {
      gboolean found = FALSE;
      GstCapsFeatures *feature = gst_caps_get_features (caps, 0);
      /* Prefer an EGLImage allocator if available and we want to use it */
      n = gst_query_get_n_allocation_params (query);
      for (i = 0; i < n; i++) {
        GstAllocator *allocator;
        GstAllocationParams params;

        gst_query_parse_nth_allocation_param (query, i, &allocator, &params);
        if (allocator) {
          if (GST_IS_GL_MEMORY_EGL_ALLOCATOR (allocator)) {
            found = TRUE;
            gst_query_set_nth_allocation_param (query, 0, allocator, &params);
            while (gst_query_get_n_allocation_params (query) > 1)
              gst_query_remove_nth_allocation_param (query, 1);
          }

          gst_object_unref (allocator);

          if (found)
            break;
        }
      }

      /* if try to negotiate with caps feature memory:EGLImage
       * and if allocator is not of type memory EGLImage then fails */
      if (feature
          && gst_caps_features_contains (feature,
              GST_CAPS_FEATURE_MEMORY_GL_MEMORY) && !found) {
        return FALSE;
      }
    }
  }
#endif /* defined (HAVE_GST_GL) */

  self->use_buffers = FALSE;

  /* Importing OMX buffers from downstream isn't supported.
   * That wouldn't bring us much as the dynamic buffer mode already
   * prevent copies between OMX components. */
  i = 0;
  while (i < gst_query_get_n_allocation_pools (query)) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (GST_IS_OMX_BUFFER_POOL (pool)) {
      GST_DEBUG_OBJECT (self, "Discard OMX pool from downstream");
      gst_query_remove_nth_allocation_pool (query, i);
    } else {
      GST_DEBUG_OBJECT (self,
          "Try using downstream buffers with OMX_UseBuffer");
      self->use_buffers = TRUE;
      i++;
    }

    if (pool)
      gst_object_unref (pool);
  }

  if (!GST_VIDEO_DECODER_CLASS
      (gst_omx_video_dec_parent_class)->decide_allocation (bdec, query))
    return FALSE;

  g_assert (gst_query_get_n_allocation_pools (query) > 0);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  g_assert (pool != NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_omx_video_dec_propose_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (bdec);
  guint size, num_buffers;

  size = self->dec_in_port->port_def.nBufferSize;
  num_buffers = self->dec_in_port->port_def.nBufferCountMin + 1;

  GST_DEBUG_OBJECT (self,
      "request at least %d buffers of size %d", num_buffers, size);
  gst_query_add_allocation_pool (query, NULL, size, num_buffers, 0);

  return
      GST_VIDEO_DECODER_CLASS
      (gst_omx_video_dec_parent_class)->propose_allocation (bdec, query);
}
