/*
 *  gstvaapidecode.c - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstcompat.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiprofilecaps.h>

#include "gstvaapidecode.h"
#include "gstvaapidecode_props.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"
#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
#include "gstvaapivideometa_texture.h"
#endif
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"

#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>
#include <gst/vaapi/gstvaapidecoder_vp8.h>
#include <gst/vaapi/gstvaapidecoder_h265.h>
#include <gst/vaapi/gstvaapidecoder_vp9.h>
#if GST_VAAPI_USE_AV1_DECODER
#include <gst/vaapi/gstvaapidecoder_av1.h>
#endif

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

#define GST_VAAPI_DECODE_FLOW_PARSE_DATA        GST_FLOW_CUSTOM_SUCCESS_2

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapidecode);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_debug_vaapidecode
#else
#define GST_CAT_DEFAULT NULL
#endif

#define GST_VAAPI_DECODE_PARAMS_QDATA \
  g_quark_from_static_string("vaapidec-params")

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "

/* *INDENT-OFF* */
char *gst_vaapidecode_sink_caps_str = NULL;

static const char gst_vaapidecode_src_caps_str[] =
    GST_VAAPI_MAKE_SURFACE_CAPS "; "
#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
    GST_VAAPI_MAKE_GLTEXUPLOAD_CAPS "; "
#endif
    GST_VIDEO_CAPS_MAKE(GST_VAAPI_FORMATS_ALL);

static GstStaticPadTemplate gst_vaapidecode_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_src_caps_str));
/* *INDENT-ON* */

typedef struct _GstVaapiDecoderMap GstVaapiDecoderMap;
struct _GstVaapiDecoderMap
{
  guint codec;
  guint rank;
  const gchar *name;
  const gchar *caps_str;

  void (*install_properties) (GObjectClass * klass);
};

static const GstVaapiDecoderMap vaapi_decode_map[] = {
  {GST_VAAPI_CODEC_JPEG, GST_RANK_NONE, "jpeg", "image/jpeg", NULL},
  {GST_VAAPI_CODEC_MPEG2, GST_RANK_NONE, "mpeg2",
      "video/mpeg, mpegversion=2, systemstream=(boolean)false", NULL},
  {GST_VAAPI_CODEC_MPEG4, GST_RANK_NONE, "mpeg4",
      "video/mpeg, mpegversion=4", NULL},
  {GST_VAAPI_CODEC_H263, GST_RANK_NONE, "h263", "video/x-h263", NULL},
  {GST_VAAPI_CODEC_H264, GST_RANK_NONE, "h264", "video/x-h264",
      gst_vaapi_decode_h264_install_properties},
  {GST_VAAPI_CODEC_VC1, GST_RANK_NONE, "vc1",
      "video/x-wmv, wmvversion=3, format={WMV3,WVC1}", NULL},
  {GST_VAAPI_CODEC_VP8, GST_RANK_NONE, "vp8", "video/x-vp8", NULL},
  {GST_VAAPI_CODEC_VP9, GST_RANK_NONE, "vp9", "video/x-vp9", NULL},
  {GST_VAAPI_CODEC_H265, GST_RANK_NONE, "h265", "video/x-h265", NULL},
  {GST_VAAPI_CODEC_AV1, GST_RANK_NONE, "av1", "video/x-av1", NULL},
  {0 /* the rest */ , GST_RANK_NONE, NULL, NULL, NULL},
};

static GstElementClass *parent_class = NULL;
GST_VAAPI_PLUGIN_BASE_DEFINE_SET_CONTEXT (parent_class);

static gboolean gst_vaapidecode_update_sink_caps (GstVaapiDecode * decode,
    GstCaps * caps);
static gboolean gst_vaapi_decode_input_state_replace (GstVaapiDecode * decode,
    const GstVideoCodecState * new_state);

/* invoked if actual VASurface size (not the cropped values)
 * changed */
static void
gst_vaapi_decoder_state_changed (GstVaapiDecoder * decoder,
    const GstVideoCodecState * codec_state, gpointer user_data)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (user_data);

  g_assert (decode->decoder == decoder);

  if (!gst_vaapi_decode_input_state_replace (decode, codec_state))
    return;
  if (!gst_vaapidecode_update_sink_caps (decode, decode->input_state->caps))
    return;
}

static GstVideoCodecState *
copy_video_codec_state (const GstVideoCodecState * in_state)
{
  GstVideoCodecState *state;

  g_return_val_if_fail (in_state != NULL, NULL);

  state = g_new0 (GstVideoCodecState, 1);
  state->ref_count = 1;
  state->info = in_state->info;
  state->caps = gst_caps_copy (in_state->caps);
  if (in_state->codec_data)
    state->codec_data = gst_buffer_copy_deep (in_state->codec_data);

  return state;
}

static gboolean
gst_vaapi_decode_input_state_replace (GstVaapiDecode * decode,
    const GstVideoCodecState * new_state)
{
  if (decode->input_state) {
    if (new_state) {
      const GstCaps *curcaps = decode->input_state->caps;
      /* If existing caps are equal of the new state, keep the
       * existing state without renegotiating. */
      if (gst_caps_is_strictly_equal (curcaps, new_state->caps)) {
        GST_DEBUG ("Ignoring new caps %" GST_PTR_FORMAT
            " since are equal to current ones", new_state->caps);
        return FALSE;
      }
    }
    gst_video_codec_state_unref (decode->input_state);
  }

  if (new_state)
    decode->input_state = copy_video_codec_state (new_state);
  else
    decode->input_state = NULL;

  return TRUE;
}

static inline gboolean
gst_vaapidecode_update_sink_caps (GstVaapiDecode * decode, GstCaps * caps)
{
  GST_INFO_OBJECT (decode, "new sink caps = %" GST_PTR_FORMAT, caps);
  gst_caps_replace (&decode->sinkpad_caps, caps);
  return TRUE;
}

static gboolean
gst_vaapidecode_ensure_allowed_srcpad_caps (GstVaapiDecode * decode)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (decode);
  GstCaps *out_caps, *raw_caps, *va_caps, *gltexup_caps, *base_caps;
  GArray *formats;
  gint min_width, min_height, max_width, max_height;
  guint mem_types;
  gboolean ret = FALSE;

  if (decode->allowed_srcpad_caps)
    return TRUE;

  if (!display)
    return FALSE;

  if (!decode->decoder)
    return FALSE;

  gltexup_caps = NULL;

  formats = gst_vaapi_decoder_get_surface_attributes (decode->decoder,
      &min_width, &min_height, &max_width, &max_height, &mem_types);
  if (!formats)
    return FALSE;

  base_caps = gst_vaapi_video_format_new_template_caps_from_list (formats);
  if (!base_caps)
    goto bail;
  gst_vaapi_caps_set_width_and_height_range (base_caps, min_width, min_height,
      max_width, max_height);

  {
    GArray *img_formats = gst_vaapi_display_get_image_formats (display);
    GstVideoFormat decoded_format =
        GST_VIDEO_INFO_FORMAT (&decode->decoded_info);

    if (!img_formats)
      img_formats = g_array_ref (formats);

    if (decoded_format != GST_VIDEO_FORMAT_UNKNOWN) {
      guint decoded_chroma =
          gst_vaapi_video_format_get_chroma_type (decoded_format);
      GArray *new_img_formats =
          g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));
      gint i;

      for (i = 0; i < img_formats->len; i++) {
        const GstVideoFormat fmt =
            g_array_index (img_formats, GstVideoFormat, i);
        if (gst_vaapi_video_format_get_chroma_type (fmt) == decoded_chroma)
          g_array_append_val (new_img_formats, fmt);
      }

      if (new_img_formats->len == 0) {
        g_clear_pointer (&new_img_formats, g_array_unref);
      } else {
        g_clear_pointer (&img_formats, g_array_unref);
        img_formats = new_img_formats;
      }
    }

    raw_caps = gst_vaapi_video_format_new_template_caps_from_list (img_formats);
    gst_vaapi_caps_set_width_and_height_range (raw_caps, min_width, min_height,
        max_width, max_height);
    g_array_unref (img_formats);
  }

  va_caps = gst_caps_copy (base_caps);
  gst_caps_set_features_simple (va_caps,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE));

#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
  if (!GST_VAAPI_PLUGIN_BASE_SRC_PAD_CAN_DMABUF (decode)
      && gst_vaapi_display_has_opengl (GST_VAAPI_PLUGIN_BASE_DISPLAY (decode))) {
    gltexup_caps = gst_caps_from_string (GST_VAAPI_MAKE_GLTEXUPLOAD_CAPS);
    if (gltexup_caps) {
      gst_vaapi_caps_set_width_and_height_range (base_caps, min_width,
          min_height, max_width, max_height);
    }
  }
#endif

  out_caps = va_caps;
  if (gltexup_caps)
    gst_caps_append (out_caps, gltexup_caps);
  gst_caps_append (out_caps, raw_caps);
  decode->allowed_srcpad_caps = out_caps;

  GST_INFO_OBJECT (decode, "allowed srcpad caps: %" GST_PTR_FORMAT,
      decode->allowed_srcpad_caps);

  ret = TRUE;

bail:
  if (formats)
    g_array_unref (formats);
  if (base_caps)
    gst_caps_unref (base_caps);
  return ret;
}

static GstCaps *
gst_vaapidecode_get_allowed_srcpad_caps (GstVaapiDecode * decode)
{
  GstPad *const srcpad = GST_VIDEO_DECODER_SRC_PAD (decode);

  if (gst_vaapidecode_ensure_allowed_srcpad_caps (decode))
    return gst_caps_ref (decode->allowed_srcpad_caps);
  return gst_pad_get_pad_template_caps (srcpad);
}

static gboolean
gst_vaapidecode_update_src_caps (GstVaapiDecode * decode)
{
  GstVideoDecoder *const vdec = GST_VIDEO_DECODER (decode);
  GstPad *const srcpad = GST_VIDEO_DECODER_SRC_PAD (vdec);
  GstCaps *allowed;
  GstVideoCodecState *state, *ref_state;
  GstVaapiCapsFeature feature;
  GstCapsFeatures *features;
  GstCaps *allocation_caps;
  GstVideoInfo *vi;
  GstVideoFormat format;
  GstClockTime latency;
  gint fps_d, fps_n;
  guint width, height;
  const gchar *format_str, *feature_str;

  if (!decode->input_state)
    return FALSE;

  ref_state = decode->input_state;

  format = GST_VIDEO_INFO_FORMAT (&decode->decoded_info);
  allowed = gst_vaapidecode_get_allowed_srcpad_caps (decode);
  feature = gst_vaapi_find_preferred_caps_feature (srcpad, allowed, &format);
  gst_caps_unref (allowed);

  if (feature == GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED)
    return FALSE;

#if (!GST_VAAPI_USE_GLX && !GST_VAAPI_USE_EGL)
  /* This is a very pathological situation. Should not happen. */
  if (feature == GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META)
    return FALSE;
#endif

  if ((feature == GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY ||
          feature == GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE)
      && format != GST_VIDEO_INFO_FORMAT (&decode->decoded_info)) {
    GST_FIXME_OBJECT (decode, "validate if driver can convert from %s to %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
            (&decode->decoded_info)), gst_video_format_to_string (format));
  }

  width = decode->display_width;
  height = decode->display_height;

  if (!width || !height) {
    width = GST_VIDEO_INFO_WIDTH (&ref_state->info);
    height = GST_VIDEO_INFO_HEIGHT (&ref_state->info);
  }

  state = gst_video_decoder_set_output_state (vdec, format, width, height,
      ref_state);
  if (!state)
    return FALSE;

  if (GST_VIDEO_INFO_WIDTH (&state->info) == 0
      || GST_VIDEO_INFO_HEIGHT (&state->info) == 0) {
    gst_video_codec_state_unref (state);
    return FALSE;
  }

  vi = &state->info;
  state->caps = gst_video_info_to_caps (vi);

  switch (feature) {
    case GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META:
    case GST_VAAPI_CAPS_FEATURE_DMABUF:
    case GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE:{
      GstStructure *structure = gst_caps_get_structure (state->caps, 0);
      if (!structure)
        break;
      feature_str = gst_vaapi_caps_feature_to_string (feature);
      features = gst_caps_features_new (feature_str, NULL);
      gst_caps_set_features (state->caps, 0, features);
      break;
    }
    default:
      break;
  }

  /* Allocation query is different from pad's caps */
  allocation_caps = NULL;
  if (GST_VIDEO_INFO_WIDTH (&decode->decoded_info) != width
      || GST_VIDEO_INFO_HEIGHT (&decode->decoded_info) != height) {
    allocation_caps = gst_caps_copy (state->caps);
    format_str = gst_video_format_to_string (format);
    gst_caps_set_simple (allocation_caps,
        "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH (&decode->decoded_info),
        "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT (&decode->decoded_info),
        "format", G_TYPE_STRING, format_str, NULL);
    GST_INFO_OBJECT (decode, "new alloc caps = %" GST_PTR_FORMAT,
        allocation_caps);
  }
  gst_caps_replace (&state->allocation_caps, allocation_caps);
  if (allocation_caps)
    gst_caps_unref (allocation_caps);

  GST_INFO_OBJECT (decode, "new src caps = %" GST_PTR_FORMAT, state->caps);
  gst_caps_replace (&decode->srcpad_caps, state->caps);
  gst_video_codec_state_unref (state);

  fps_n = GST_VIDEO_INFO_FPS_N (vi);
  fps_d = GST_VIDEO_INFO_FPS_D (vi);
  if (fps_n <= 0 || fps_d <= 0) {
    GST_DEBUG_OBJECT (decode, "forcing 25/1 framerate for latency calculation");
    fps_n = 25;
    fps_d = 1;
  }

  /* For parsing/preparation purposes we'd need at least 1 frame
   * latency in general, with perfectly known unit boundaries (NALU,
   * AU), and up to 2 frames when we need to wait for the second frame
   * start to determine the first frame is complete */
  latency = gst_util_uint64_scale (2 * GST_SECOND, fps_d, fps_n);
  gst_video_decoder_set_latency (vdec, latency, latency);

  return TRUE;
}

/* check whether the decoded surface size has changed */
static gboolean
is_surface_resolution_changed (GstVaapiDecode * decode,
    GstVaapiSurface * surface)
{
  GstVideoInfo *vinfo = &decode->decoded_info;
  GstVideoFormat surface_format;
  guint surface_width, surface_height;

  g_return_val_if_fail (surface != NULL, FALSE);

  gst_vaapi_surface_get_size (surface, &surface_width, &surface_height);

  if (GST_VIDEO_INFO_WIDTH (vinfo) == surface_width
      && GST_VIDEO_INFO_HEIGHT (vinfo) == surface_height)
    return FALSE;

  /* doing gst_vaapi_surface_get_format() only if necessary since it
   * execute vaDeriveImage in the background. This will usually get
   * executed only once */
  surface_format = GST_VIDEO_INFO_FORMAT (vinfo);
  if (surface_format == GST_VIDEO_FORMAT_UNKNOWN) {
    surface_format = gst_vaapi_surface_get_format (surface);

    /* if the VA context delivers a currently unrecognized format
     * (ICM3, e.g.), we can assume one according surface chroma
     * type. If fail, then use NV12 "safely" */
    if (surface_format == GST_VIDEO_FORMAT_UNKNOWN
        || surface_format == GST_VIDEO_FORMAT_ENCODED)
      surface_format =
          gst_vaapi_video_format_from_chroma (gst_vaapi_surface_get_chroma_type
          (surface));
    if (surface_format == GST_VIDEO_FORMAT_UNKNOWN)
      surface_format = GST_VIDEO_FORMAT_NV12;
  }

  /* reset allowed source caps since they are dependant of the decoded
   * surface format */
  gst_caps_replace (&decode->allowed_srcpad_caps, NULL);

  gst_video_info_set_format (vinfo, surface_format, surface_width,
      surface_height);

  return TRUE;
}

/* check whether display resolution changed */
static gboolean
is_display_resolution_changed (GstVaapiDecode * decode,
    const GstVaapiRectangle * crop_rect)
{
  GstVideoDecoder *const vdec = GST_VIDEO_DECODER (decode);
  GstVideoCodecState *state;
  guint display_width, display_height;
  guint negotiated_width, negotiated_height;

  display_width = GST_VIDEO_INFO_WIDTH (&decode->decoded_info);
  display_height = GST_VIDEO_INFO_HEIGHT (&decode->decoded_info);
  if (crop_rect) {
    display_width = crop_rect->width;
    display_height = crop_rect->height;
  }

  state = gst_video_decoder_get_output_state (vdec);
  if (G_UNLIKELY (!state))
    goto set_display_res;

  negotiated_width = GST_VIDEO_INFO_WIDTH (&state->info);
  negotiated_height = GST_VIDEO_INFO_HEIGHT (&state->info);
  gst_video_codec_state_unref (state);

  if ((display_width == negotiated_width && display_height == negotiated_height)
      && (decode->display_width == negotiated_width
          && decode->display_height == negotiated_height))
    return FALSE;

set_display_res:
  decode->display_width = display_width;
  decode->display_height = display_height;
  return TRUE;
}

static gboolean
gst_vaapidecode_negotiate (GstVaapiDecode * decode)
{
  GstVideoDecoder *const vdec = GST_VIDEO_DECODER (decode);
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (vdec);

  GST_DEBUG_OBJECT (decode, "input codec state changed: renegotiating");

  GST_VIDEO_DECODER_STREAM_LOCK (vdec);
  if (!gst_vaapidecode_update_src_caps (decode))
    goto caps_negotiation_failed;
  if (!gst_vaapi_plugin_base_set_caps (plugin, NULL, decode->srcpad_caps))
    goto caps_negotiation_failed;
  GST_VIDEO_DECODER_STREAM_UNLOCK (vdec);

  if (!gst_video_decoder_negotiate (vdec))
    return FALSE;

  return TRUE;

caps_negotiation_failed:
  {
    GST_VIDEO_DECODER_STREAM_UNLOCK (vdec);
    return FALSE;
  }
}

static gboolean
is_src_allocator_dmabuf (GstVaapiDecode * decode)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (decode);

  if (!GST_VAAPI_PLUGIN_BASE_SRC_PAD_CAN_DMABUF (plugin))
    return FALSE;
  return
      gst_vaapi_is_dmabuf_allocator (GST_VAAPI_PLUGIN_BASE_SRC_PAD_ALLOCATOR
      (plugin));
}

static GstFlowReturn
gst_vaapidecode_push_decoded_frame (GstVideoDecoder * vdec,
    GstVideoCodecFrame * out_frame)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstVaapiSurfaceProxy *proxy;
  GstVaapiSurface *surface;
  GstFlowReturn ret;
  const GstVaapiRectangle *crop_rect;
  GstVaapiVideoMeta *meta;
  GstBufferPoolAcquireParams *params = NULL;
  GstVaapiVideoBufferPoolAcquireParams vaapi_params = { {0,}, };
  guint flags, out_flags = 0;
  gboolean alloc_renegotiate, caps_renegotiate;

  if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (out_frame)) {
    proxy = gst_video_codec_frame_get_user_data (out_frame);
    surface = GST_VAAPI_SURFACE_PROXY_SURFACE (proxy);
    crop_rect = gst_vaapi_surface_proxy_get_crop_rect (proxy);

    /* in theory, we are not supposed to check the surface resolution
     * change here since it should be advertised before from ligstvaapi.
     * But there are issues with it especially for some vp9 streams where
     * upstream element set un-cropped values in set_format() which make
     * everything a mess. So better doing the explicit check here irrespective
     * of what notification we get from upstream or libgstvaapi.Also, even if
     * we received notification from libgstvaapi, the frame we are going to
     * be pushed at this point might not have the notified resolution if there
     * are queued frames in decoded picture buffer. */
    alloc_renegotiate = is_surface_resolution_changed (decode, surface);
    caps_renegotiate = is_display_resolution_changed (decode, crop_rect);

    if (gst_pad_needs_reconfigure (GST_VIDEO_DECODER_SRC_PAD (vdec))
        || alloc_renegotiate || caps_renegotiate || decode->do_renego) {

      g_atomic_int_set (&decode->do_renego, FALSE);
      if (!gst_vaapidecode_negotiate (decode))
        return GST_FLOW_ERROR;
    }

    if (is_src_allocator_dmabuf (decode)) {
      vaapi_params.proxy = gst_vaapi_surface_proxy_ref (proxy);
      params = (GstBufferPoolAcquireParams *) & vaapi_params;
    }

    ret = gst_video_decoder_allocate_output_frame_with_params (vdec, out_frame,
        params);
    if (params)
      gst_vaapi_surface_proxy_unref (vaapi_params.proxy);
    if (ret != GST_FLOW_OK)
      goto error_create_buffer;

    /* if not dmabuf is negotiated set the vaapi video meta in the
     * proxy */
    if (!params) {
      meta = gst_buffer_get_vaapi_video_meta (out_frame->output_buffer);
      if (!meta)
        goto error_get_meta;
      gst_vaapi_video_meta_set_surface_proxy (meta, proxy);
    }

    flags = gst_vaapi_surface_proxy_get_flags (proxy);
    if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_CORRUPTED)
      out_flags |= GST_BUFFER_FLAG_CORRUPTED;
    if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_INTERLACED) {
      out_flags |= GST_VIDEO_BUFFER_FLAG_INTERLACED;
      if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_TFF)
        out_flags |= GST_VIDEO_BUFFER_FLAG_TFF;
      if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_RFF)
        out_flags |= GST_VIDEO_BUFFER_FLAG_RFF;
      if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_ONEFIELD)
        out_flags |= GST_VIDEO_BUFFER_FLAG_ONEFIELD;
    }
    GST_BUFFER_FLAG_SET (out_frame->output_buffer, out_flags);

    if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_FFB) {
      GST_BUFFER_FLAG_SET (out_frame->output_buffer,
          GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE);
    }
#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
    if (decode->has_texture_upload_meta)
      gst_buffer_ensure_texture_upload_meta (out_frame->output_buffer);
#endif

    /* Generate a system allocated output buffer if downstream doesn't
     * support GstVideoMeta */
    if (GST_VAAPI_PLUGIN_BASE_COPY_OUTPUT_FRAME (vdec)) {
      GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (vdec);
      GstBuffer *sys_buf, *va_buf;

      va_buf = out_frame->output_buffer;
      sys_buf =
          gst_buffer_new_allocate (GST_VAAPI_PLUGIN_BASE_OTHER_ALLOCATOR
          (plugin),
          GST_VIDEO_INFO_SIZE (GST_VAAPI_PLUGIN_BASE_SRC_PAD_INFO (plugin)),
          &GST_VAAPI_PLUGIN_BASE_OTHER_ALLOCATOR_PARAMS (plugin));
      if (!sys_buf)
        goto error_no_sys_buffer;

      if (!gst_vaapi_plugin_copy_va_buffer (plugin, va_buf, sys_buf)) {
        gst_buffer_unref (sys_buf);
        goto error_cannot_copy;
      }

      gst_buffer_replace (&out_frame->output_buffer, sys_buf);
      gst_buffer_unref (sys_buf);
    }
  }

  ret = gst_video_decoder_finish_frame (vdec, out_frame);
  if (ret != GST_FLOW_OK)
    goto error_commit_buffer;
  return GST_FLOW_OK;

  /* ERRORS */
error_create_buffer:
  {
    const GstVaapiID surface_id =
        gst_vaapi_surface_get_id (GST_VAAPI_SURFACE_PROXY_SURFACE (proxy));

    GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
        ("Failed to create sink buffer"),
        ("video sink failed to create video buffer for proxy'ed "
            "surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (surface_id)));
    gst_video_decoder_drop_frame (vdec, out_frame);
    return GST_FLOW_ERROR;
  }
error_get_meta:
  {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
        ("Failed to get vaapi video meta attached to video buffer"),
        ("Failed to get vaapi video meta attached to video buffer"));
    gst_video_decoder_drop_frame (vdec, out_frame);
    return GST_FLOW_ERROR;
  }
error_no_sys_buffer:
  {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
        ("Failed to create system allocated buffer"),
        ("Failed to create system allocated buffer"));
    gst_video_decoder_drop_frame (vdec, out_frame);
    return GST_FLOW_ERROR;
  }
error_cannot_copy:
  {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
        ("Failed to copy system allocated buffer"),
        ("Failed to copy system allocated buffer"));
    gst_video_decoder_drop_frame (vdec, out_frame);
    return GST_FLOW_ERROR;
  }
error_commit_buffer:
  {
    GST_LOG_OBJECT (decode, "downstream element rejected the frame (%s [%d])",
        gst_flow_get_name (ret), ret);
    return ret;
  }
}

static GstFlowReturn
gst_vaapidecode_push_all_decoded_frames (GstVaapiDecode * decode)
{
  GstVideoDecoder *const vdec = GST_VIDEO_DECODER (decode);
  GstVaapiDecoderStatus status;
  GstVideoCodecFrame *out_frame;
  GstFlowReturn ret;

  for (;;) {
    status = gst_vaapi_decoder_get_frame (decode->decoder, &out_frame);

    switch (status) {
      case GST_VAAPI_DECODER_STATUS_SUCCESS:
        /* GstVaapiDecode's queue adds an extra reference */
        gst_video_codec_frame_unref (out_frame);
        ret = gst_vaapidecode_push_decoded_frame (vdec, out_frame);
        if (ret != GST_FLOW_OK)
          return ret;
        break;
      case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        return GST_FLOW_OK;
      default:
        GST_VIDEO_DECODER_ERROR (vdec, 1, STREAM, DECODE, ("Decoding failed"),
            ("Unknown decoding error"), ret);
        return ret;
    }
  }
  g_assert_not_reached ();
}

static GstFlowReturn
gst_vaapidecode_handle_frame (GstVideoDecoder * vdec,
    GstVideoCodecFrame * frame)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstVaapiDecoderStatus status;

  if (!decode->input_state)
    goto not_negotiated;

  /* Decode current frame */
  for (;;) {
    status = gst_vaapi_decoder_decode (decode->decoder, frame);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      goto error_decode;
    break;
  }

  /* Note that gst_vaapi_decoder_decode cannot return success without
     completing the decode and pushing all decoded frames into the output
     queue */
  return gst_vaapidecode_push_all_decoded_frames (decode);

  /* ERRORS */
error_decode:
  {
    GstFlowReturn ret = GST_FLOW_OK;

    GST_WARNING_OBJECT (decode, "decode error %d", status);

    switch (status) {
      case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
      case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
      case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
        ret = GST_FLOW_NOT_SUPPORTED;
        break;
      default:
        GST_VIDEO_DECODER_ERROR (vdec, 1, STREAM, DECODE, ("Decoding error"),
            ("Decode error %d", status), ret);
        GST_INFO_OBJECT (decode, "requesting upstream a key unit");
        gst_pad_push_event (GST_VIDEO_DECODER_SINK_PAD (decode),
            gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
                FALSE, 0));
        break;
    }
    gst_video_decoder_drop_frame (vdec, frame);
    return ret;
  }
not_negotiated:
  {
    GST_ERROR_OBJECT (decode, "not negotiated");
    gst_video_decoder_drop_frame (vdec, frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

/* If there is something in GstVideoDecoder's output adapter, then
   submit the frame for decoding */
static inline void
gst_vaapidecode_flush_output_adapter (GstVaapiDecode * decode)
{
  if (decode->current_frame_size == 0)
    return;
  gst_video_decoder_have_frame (GST_VIDEO_DECODER (decode));
  decode->current_frame_size = 0;
}

static GstFlowReturn
gst_vaapidecode_drain (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);

  if (!decode->decoder)
    return GST_FLOW_NOT_NEGOTIATED;

  GST_LOG_OBJECT (decode, "drain");

  gst_vaapidecode_flush_output_adapter (decode);
  return gst_vaapidecode_push_all_decoded_frames (decode);
}

static GstFlowReturn
gst_vaapidecode_finish (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstVaapiDecoderStatus status;
  GstFlowReturn ret;

  if (!decode->decoder)
    return GST_FLOW_OK;

  gst_vaapidecode_flush_output_adapter (decode);
  status = gst_vaapi_decoder_flush (decode->decoder);
  ret = gst_vaapidecode_push_all_decoded_frames (decode);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto error_decoder_flush;
  return ret;

  /* ERRORS: */
error_decoder_flush:
  {
    GST_WARNING_OBJECT (decode, "failed to flush decoder (status %d)", status);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_vaapidecode_decide_allocation (GstVideoDecoder * vdec, GstQuery * query)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstCaps *caps = NULL;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    goto error_no_caps;

  decode->has_texture_upload_meta = FALSE;

#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
  decode->has_texture_upload_meta =
      gst_query_find_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL) &&
      gst_vaapi_caps_feature_contains (caps,
      GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META);
#endif

  return gst_vaapi_plugin_base_decide_allocation (GST_VAAPI_PLUGIN_BASE (vdec),
      query);

  /* ERRORS */
error_no_caps:
  {
    GST_ERROR_OBJECT (decode, "no caps specified");
    return FALSE;
  }
}

static inline gboolean
gst_vaapidecode_ensure_display (GstVaapiDecode * decode)
{
  return gst_vaapi_plugin_base_ensure_display (GST_VAAPI_PLUGIN_BASE (decode));
}

static gboolean
gst_vaapidecode_create (GstVaapiDecode * decode, GstCaps * caps)
{
  GstVaapiDisplay *dpy;

  if (!gst_vaapidecode_ensure_display (decode))
    return FALSE;
  dpy = GST_VAAPI_PLUGIN_BASE_DISPLAY (decode);

  switch (gst_vaapi_get_codec_from_caps (caps)) {
    case GST_VAAPI_CODEC_MPEG2:
      decode->decoder = gst_vaapi_decoder_mpeg2_new (dpy, caps);
      break;
    case GST_VAAPI_CODEC_MPEG4:
    case GST_VAAPI_CODEC_H263:
      decode->decoder = gst_vaapi_decoder_mpeg4_new (dpy, caps);
      break;
    case GST_VAAPI_CODEC_H264:
      decode->decoder = gst_vaapi_decoder_h264_new (dpy, caps);

      /* Set the stream buffer alignment for better optimizations */
      if (decode->decoder && caps) {
        GstVaapiDecodeH264Private *priv =
            gst_vaapi_decode_h264_get_instance_private (decode);
        GstStructure *const structure = gst_caps_get_structure (caps, 0);
        const gchar *str = NULL;

        if (!structure)
          break;

        if ((str = gst_structure_get_string (structure, "alignment"))) {
          GstVaapiStreamAlignH264 alignment;
          if (g_strcmp0 (str, "au") == 0)
            alignment = GST_VAAPI_STREAM_ALIGN_H264_AU;
          else if (g_strcmp0 (str, "nal") == 0)
            alignment = GST_VAAPI_STREAM_ALIGN_H264_NALU;
          else
            alignment = GST_VAAPI_STREAM_ALIGN_H264_NONE;
          gst_vaapi_decoder_h264_set_alignment (GST_VAAPI_DECODER_H264
              (decode->decoder), alignment);
        }

        if (priv) {
          gst_vaapi_decoder_h264_set_low_latency (GST_VAAPI_DECODER_H264
              (decode->decoder), priv->is_low_latency);
          gst_vaapi_decoder_h264_set_base_only (GST_VAAPI_DECODER_H264
              (decode->decoder), priv->base_only);
        }
      }
      break;
    case GST_VAAPI_CODEC_H265:
      decode->decoder = gst_vaapi_decoder_h265_new (dpy, caps);

      /* Set the stream buffer alignment for better optimizations */
      if (decode->decoder && caps) {
        GstStructure *const structure = gst_caps_get_structure (caps, 0);
        const gchar *str = NULL;

        if (!structure)
          break;

        if ((str = gst_structure_get_string (structure, "alignment"))) {
          GstVaapiStreamAlignH265 alignment;
          if (g_strcmp0 (str, "au") == 0)
            alignment = GST_VAAPI_STREAM_ALIGN_H265_AU;
          else if (g_strcmp0 (str, "nal") == 0)
            alignment = GST_VAAPI_STREAM_ALIGN_H265_NALU;
          else
            alignment = GST_VAAPI_STREAM_ALIGN_H265_NONE;
          gst_vaapi_decoder_h265_set_alignment (GST_VAAPI_DECODER_H265
              (decode->decoder), alignment);
        }
      }
      break;
    case GST_VAAPI_CODEC_WMV3:
    case GST_VAAPI_CODEC_VC1:
      decode->decoder = gst_vaapi_decoder_vc1_new (dpy, caps);
      break;
    case GST_VAAPI_CODEC_JPEG:
      decode->decoder = gst_vaapi_decoder_jpeg_new (dpy, caps);
      break;
    case GST_VAAPI_CODEC_VP8:
      decode->decoder = gst_vaapi_decoder_vp8_new (dpy, caps);
      break;
    case GST_VAAPI_CODEC_VP9:
      decode->decoder = gst_vaapi_decoder_vp9_new (dpy, caps);
      break;
#if GST_VAAPI_USE_AV1_DECODER
    case GST_VAAPI_CODEC_AV1:
      decode->decoder = gst_vaapi_decoder_av1_new (dpy, caps);
      break;
#endif
    default:
      decode->decoder = NULL;
      break;
  }
  if (!decode->decoder)
    return FALSE;

  gst_vaapi_decoder_set_codec_state_changed_func (decode->decoder,
      gst_vaapi_decoder_state_changed, decode);

  return TRUE;
}

static void
gst_vaapidecode_purge (GstVaapiDecode * decode)
{
  GstVaapiDecoderStatus status;

  if (!decode->decoder)
    return;

  status = gst_vaapi_decoder_flush (decode->decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    GST_INFO_OBJECT (decode, "failed to flush decoder (status %d)", status);

  /* Purge all decoded frames as we don't need them (e.g. flush and close)
   * Releasing the frames is important, otherwise the frames are not
   * freed. */
  do {
    GstVideoCodecFrame *frame = NULL;

    status =
        gst_vaapi_decoder_get_frame_with_timeout (decode->decoder, &frame, 0);
    if (frame) {
      gst_video_decoder_release_frame (GST_VIDEO_DECODER (decode), frame);
      gst_video_codec_frame_unref (frame);
    }
  } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
}

static void
gst_vaapidecode_destroy (GstVaapiDecode * decode)
{
  gst_vaapidecode_purge (decode);

  gst_vaapi_decoder_replace (&decode->decoder, NULL);
  /* srcpad caps are decoder's context dependant */
  gst_caps_replace (&decode->allowed_srcpad_caps, NULL);
}

static gboolean
gst_vaapidecode_reset (GstVaapiDecode * decode, GstCaps * caps,
    gboolean force_reset)
{
  /* Reset tracked frame size */
  decode->current_frame_size = 0;

  if (decode->decoder) {
    if (!gst_caps_is_equal (caps, gst_vaapi_decoder_get_caps (decode->decoder))) {
      if (gst_vaapi_decoder_update_caps (decode->decoder, caps)) {
        g_atomic_int_set (&decode->do_renego, TRUE);
        if (!force_reset)
          return TRUE;
      }
    }
    return (gst_vaapi_decoder_reset (decode->decoder) ==
        GST_VAAPI_DECODER_STATUS_SUCCESS);
  }

  return gst_vaapidecode_create (decode, caps);
}

static void
gst_vaapidecode_finalize (GObject * object)
{
  gst_vaapi_plugin_base_finalize (GST_VAAPI_PLUGIN_BASE (object));
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vaapidecode_open (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);

  if (!gst_vaapi_plugin_base_open (GST_VAAPI_PLUGIN_BASE (decode)))
    return FALSE;

  decode->display_width = 0;
  decode->display_height = 0;
  gst_video_info_init (&decode->decoded_info);

  return TRUE;
}

static gboolean
gst_vaapidecode_close (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);

  gst_vaapidecode_destroy (decode);
  gst_caps_replace (&decode->allowed_srcpad_caps, NULL);
  gst_caps_replace (&decode->allowed_sinkpad_caps, NULL);
  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (decode));
  return TRUE;
}

static gboolean
gst_vaapidecode_start (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstVaapiDisplay *const old_display = GST_VAAPI_PLUGIN_BASE_DISPLAY (decode);
  gboolean success;

  /* Let GstVideoContext ask for a proper display to its neighbours */
  /* Note: steal old display that may be allocated from get_caps()
     so that to retain a reference to it, thus avoiding extra
     initialization steps if we turn out to simply re-use the
     existing (cached) VA display */
  GST_VAAPI_PLUGIN_BASE_DISPLAY (decode) = NULL;
  success =
      gst_vaapi_plugin_base_ensure_display (GST_VAAPI_PLUGIN_BASE (decode));
  if (old_display)
    gst_object_unref (old_display);

  /* Disable errors on decode errors */
  gst_video_decoder_set_max_errors (vdec, -1);

  return success;
}

static gboolean
gst_vaapidecode_stop (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);

  gst_vaapidecode_purge (decode);
  gst_vaapi_decode_input_state_replace (decode, NULL);
  gst_vaapi_decoder_replace (&decode->decoder, NULL);
  gst_caps_replace (&decode->sinkpad_caps, NULL);
  gst_caps_replace (&decode->srcpad_caps, NULL);
  return TRUE;
}

static gboolean
gst_vaapidecode_flush (GstVideoDecoder * vdec)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  if (!decode->decoder)
    return FALSE;

  GST_LOG_OBJECT (vdec, "flushing");

  gst_vaapidecode_purge (decode);

  /* There could be issues if we avoid the reset() while doing
   * seeking: we have to reset the internal state */
  return gst_vaapidecode_reset (decode, decode->sinkpad_caps, TRUE);
}

static gboolean
gst_vaapidecode_set_format (GstVideoDecoder * vdec, GstVideoCodecState * state)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (vdec);
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);

  if (!gst_vaapi_decode_input_state_replace (decode, state))
    return TRUE;
  if (gst_vaapidecode_drain (vdec) == GST_FLOW_ERROR)
    return FALSE;
  if (!gst_vaapidecode_update_sink_caps (decode, state->caps))
    return FALSE;
  if (!gst_vaapi_plugin_base_set_caps (plugin, decode->sinkpad_caps, NULL))
    return FALSE;
  if (!gst_vaapidecode_reset (decode, decode->sinkpad_caps, FALSE))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_vaapidecode_parse_frame (GstVideoDecoder * vdec,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstVaapiDecoderStatus status;
  GstFlowReturn ret;
  guint got_unit_size;
  gboolean got_frame;

  status = gst_vaapi_decoder_parse (decode->decoder, frame,
      adapter, at_eos, &got_unit_size, &got_frame);

  switch (status) {
    case GST_VAAPI_DECODER_STATUS_SUCCESS:
      if (got_unit_size > 0) {
        gst_video_decoder_add_to_frame (vdec, got_unit_size);
        decode->current_frame_size += got_unit_size;
      }
      if (got_frame) {
        ret = gst_video_decoder_have_frame (vdec);
        decode->current_frame_size = 0;
      } else
        ret = GST_VAAPI_DECODE_FLOW_PARSE_DATA;
      break;
    case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
      ret = GST_VIDEO_DECODER_FLOW_NEED_DATA;
      break;
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
      GST_WARNING ("parse error %d", status);
      ret = GST_FLOW_NOT_SUPPORTED;
      decode->current_frame_size = 0;
      break;
    default:
      GST_WARNING ("parse error %d", status);
      /* just keep parsing, the decoder should have flushed the broken unit */
      ret = GST_VAAPI_DECODE_FLOW_PARSE_DATA;
      decode->current_frame_size = 0;

      GST_INFO ("requesting upstream a key unit");
      gst_pad_push_event (GST_VIDEO_DECODER_SINK_PAD (decode),
          gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
              FALSE, 0));
      break;
  }
  return ret;
}

static GstFlowReturn
gst_vaapidecode_parse (GstVideoDecoder * vdec,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstFlowReturn ret;

  do {
    ret = gst_vaapidecode_parse_frame (vdec, frame, adapter, at_eos);
  } while (ret == GST_VAAPI_DECODE_FLOW_PARSE_DATA);
  return ret;
}

static gboolean
is_mvc_profile (GstVaapiProfile profile)
{
  return profile == GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH
      || profile == GST_VAAPI_PROFILE_H264_STEREO_HIGH;
}

static gboolean
is_svc_profile (GstVaapiProfile profile)
{
  return profile == GST_VAAPI_PROFILE_H264_SCALABLE_BASELINE
      || profile == GST_VAAPI_PROFILE_H264_SCALABLE_HIGH;
}

static void
find_mvc_and_svc (GArray * profiles, gboolean * have_mvc, gboolean * have_svc)
{
  guint i;

  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);

    *have_mvc |= is_mvc_profile (profile);
    *have_svc |= is_svc_profile (profile);
  }
}

static gboolean
gst_vaapidecode_ensure_allowed_sinkpad_caps (GstVaapiDecode * decode)
{
  GstCaps *caps, *allowed_sinkpad_caps;
  GstPad *const sinkpad = GST_VIDEO_DECODER_SINK_PAD (decode);
  GArray *profiles;
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (decode);
  guint i;
  gboolean base_only = FALSE;
  gboolean have_mvc = FALSE;
  gboolean have_svc = FALSE;

  profiles = gst_vaapi_display_get_decode_profiles (display);
  if (!profiles)
    goto error_no_profiles;

  allowed_sinkpad_caps = gst_caps_new_empty ();
  if (!allowed_sinkpad_caps)
    goto error_no_memory;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (decode), "base-only")) {
    g_object_get (decode, "base-only", &base_only, NULL);
  }

  find_mvc_and_svc (profiles, &have_mvc, &have_svc);

  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);
    const gchar *media_type_name;
    const gchar *profile_name;
    GstStructure *structure;

    media_type_name = gst_vaapi_profile_get_media_type_name (profile);
    if (!media_type_name)
      continue;

    caps = gst_caps_from_string (media_type_name);
    if (!caps)
      continue;
    structure = gst_caps_get_structure (caps, 0);
    if (!structure)
      continue;

    profile_name = gst_vaapi_profile_get_name (profile);
    if (!profile_name)
      goto merge_caps;

    /* Add all according -intra profile for HEVC */
    if (profile == GST_VAAPI_PROFILE_H265_MAIN
        || profile == GST_VAAPI_PROFILE_H265_MAIN10
        || profile == GST_VAAPI_PROFILE_H265_MAIN_422_10
        || profile == GST_VAAPI_PROFILE_H265_MAIN_444
        || profile == GST_VAAPI_PROFILE_H265_MAIN_444_10
        || profile == GST_VAAPI_PROFILE_H265_MAIN12
        || profile == GST_VAAPI_PROFILE_H265_MAIN_444_12
        || profile == GST_VAAPI_PROFILE_H265_MAIN_422_12) {
      gchar *profiles[3], *intra_name;

      intra_name = g_strdup_printf ("%s-intra", profile_name);

      profiles[0] = (gchar *) profile_name;
      profiles[1] = intra_name;
      profiles[2] = NULL;

      gst_vaapi_structure_set_profiles (structure, profiles);
      g_free (intra_name);

    } else if (profile == GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE) {
      /* XXX: artificially adding baseline if constrained_baseline is
       * available. */
      gchar *profiles[] = { (gchar *) profile_name, "baseline", NULL };

      gst_vaapi_structure_set_profiles (structure, profiles);
    } else if (profile == GST_VAAPI_PROFILE_H264_HIGH) {
      gchar *profiles[11] = { (gchar *) profile_name, "progressive-high",
        "constrained-high"
      };
      gint i = 3;

      if (base_only && !have_mvc) {
        GST_DEBUG ("base_only: force adding MVC profiles in caps");

        profiles[i++] = "multiview-high";
        profiles[i++] = "stereo-high";
      }

      if (base_only && !have_svc) {
        GST_DEBUG ("base_only: force adding SVC profiles in caps");

        profiles[i++] = "scalable-constrained-baseline";
        profiles[i++] = "scalable-baseline";
        profiles[i++] = "scalable-high-intra";
        profiles[i++] = "scalable-constrained-high";
        profiles[i++] = "scalable-high";
      }

      profiles[i++] = NULL;

      gst_vaapi_structure_set_profiles (structure, profiles);
    } else {
      gst_structure_set (structure, "profile", G_TYPE_STRING,
          profile_name, NULL);
    }

  merge_caps:
    gst_vaapi_profile_caps_append_decoder (display, profile, structure);
    allowed_sinkpad_caps = gst_caps_merge (allowed_sinkpad_caps, caps);
  }

  caps = gst_pad_get_pad_template_caps (sinkpad);
  decode->allowed_sinkpad_caps =
      gst_caps_intersect (allowed_sinkpad_caps, caps);
  gst_caps_unref (caps);
  gst_caps_unref (allowed_sinkpad_caps);
  decode->allowed_sinkpad_caps =
      gst_caps_simplify (decode->allowed_sinkpad_caps);
  GST_DEBUG_OBJECT (decode, "allowed sink caps %" GST_PTR_FORMAT,
      decode->allowed_sinkpad_caps);

  g_array_unref (profiles);
  return TRUE;

  /* ERRORS */
error_no_profiles:
  {
    GST_ERROR ("failed to retrieve VA decode profiles");
    return FALSE;
  }
error_no_memory:
  {
    GST_ERROR ("failed to allocate allowed-caps set");
    g_array_unref (profiles);
    return FALSE;
  }
}

static GstCaps *
gst_vaapidecode_sink_getcaps (GstVideoDecoder * vdec, GstCaps * filter)
{
  GstVaapiDecode *const decode = GST_VAAPIDECODE (vdec);
  GstCaps *result;

  if (decode->allowed_sinkpad_caps)
    goto bail;

  /* if we haven't a display yet, return our pad's template caps */
  if (!GST_VAAPI_PLUGIN_BASE_DISPLAY (decode))
    goto bail;

  /* if the allowed caps calculation fails, return an empty caps, so
   * the auto-plug can try other decoder */
  if (!gst_vaapidecode_ensure_allowed_sinkpad_caps (decode))
    return gst_caps_new_empty ();

bail:
  result = gst_video_decoder_proxy_getcaps (vdec, decode->allowed_sinkpad_caps,
      filter);

  GST_DEBUG_OBJECT (decode, "Returning sink caps %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_vaapidecode_sink_query (GstVideoDecoder * vdec, GstQuery * query)
{
  gboolean ret = TRUE;
  GstElement *const element = GST_ELEMENT (vdec);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      ret = gst_vaapi_handle_context_query (element, query);
      break;
    }
    default:{
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (vdec, query);
      break;
    }
  }

  return ret;
}

static gboolean
gst_vaapidecode_src_query (GstVideoDecoder * vdec, GstQuery * query)
{
  gboolean ret = TRUE;
  GstElement *const element = GST_ELEMENT (vdec);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      ret = gst_vaapi_handle_context_query (element, query);
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *caps, *filter = NULL;
      gboolean fixed_caps;

      fixed_caps = GST_PAD_IS_FIXED_CAPS (GST_VIDEO_DECODER_SRC_PAD (vdec));
      if (!fixed_caps) {
        gst_query_parse_caps (query, &filter);
        caps = gst_vaapidecode_get_allowed_srcpad_caps (GST_VAAPIDECODE (vdec));

        if (filter) {
          GstCaps *tmp = caps;
          caps =
              gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref (tmp);
        }

        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        break;
      }
      /* else jump to default */
    }
    default:{
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_query (vdec, query);
      break;
    }
  }

  return ret;
}

static gboolean
gst_vaapidecode_transform_meta (GstVideoDecoder *
    vdec, GstVideoCodecFrame * frame, GstMeta * meta)
{
  const GstMetaInfo *info = meta->info;

  if (GST_VIDEO_DECODER_CLASS (parent_class)->transform_meta (vdec, frame,
          meta))
    return TRUE;

  if (!g_strcmp0 (g_type_name (info->type), "GstVideoRegionOfInterestMeta"))
    return TRUE;

  return FALSE;
}

static void
gst_vaapidecode_class_init (GstVaapiDecodeClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *const vdec_class = GST_VIDEO_DECODER_CLASS (klass);
  GstPadTemplate *pad_template;
  GstVaapiDecoderMap *map;
  gchar *name, *longname, *description;
  GstCaps *caps;

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapidecode,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  parent_class = g_type_class_peek_parent (klass);

  gst_vaapi_plugin_base_class_init (GST_VAAPI_PLUGIN_BASE_CLASS (klass));

  object_class->finalize = gst_vaapidecode_finalize;

  vdec_class->open = GST_DEBUG_FUNCPTR (gst_vaapidecode_open);
  vdec_class->close = GST_DEBUG_FUNCPTR (gst_vaapidecode_close);
  vdec_class->start = GST_DEBUG_FUNCPTR (gst_vaapidecode_start);
  vdec_class->stop = GST_DEBUG_FUNCPTR (gst_vaapidecode_stop);
  vdec_class->set_format = GST_DEBUG_FUNCPTR (gst_vaapidecode_set_format);
  vdec_class->flush = GST_DEBUG_FUNCPTR (gst_vaapidecode_flush);
  vdec_class->parse = GST_DEBUG_FUNCPTR (gst_vaapidecode_parse);
  vdec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_vaapidecode_handle_frame);
  vdec_class->finish = GST_DEBUG_FUNCPTR (gst_vaapidecode_finish);
  vdec_class->drain = GST_DEBUG_FUNCPTR (gst_vaapidecode_drain);
  vdec_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vaapidecode_decide_allocation);
  vdec_class->src_query = GST_DEBUG_FUNCPTR (gst_vaapidecode_src_query);
  vdec_class->sink_query = GST_DEBUG_FUNCPTR (gst_vaapidecode_sink_query);
  vdec_class->getcaps = GST_DEBUG_FUNCPTR (gst_vaapidecode_sink_getcaps);
  vdec_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_vaapidecode_transform_meta);

  map = (GstVaapiDecoderMap *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_VAAPI_DECODE_PARAMS_QDATA);

  if (map->codec) {
    name = g_ascii_strup (map->name, -1);
    longname = g_strdup_printf ("VA-API %s decoder", name);
    description = g_strdup_printf ("A VA-API based %s video decoder", name);
    g_free (name);
  } else {
    longname = g_strdup ("VA-API decoder");
    description = g_strdup (GST_PLUGIN_DESC);
  }

  element_class->set_context = gst_vaapi_base_set_context;
  gst_element_class_set_static_metadata (element_class, longname,
      "Codec/Decoder/Video/Hardware", description,
      "Gwenole Beauchesne <gwenole.beauchesne@intel.com>, "
      "Halley Zhao <halley.zhao@intel.com>, "
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>, "
      "Wind Yuan <feng.yuan@intel.com>, Junyan He <junyan.he@intel.com>");

  g_free (longname);
  g_free (description);

  if (map->install_properties)
    map->install_properties (object_class);

  /* sink pad */
  if (map->caps_str) {
    caps = gst_caps_from_string (map->caps_str);
  } else {
    caps = gst_caps_from_string (gst_vaapidecode_sink_caps_str);
    g_free (gst_vaapidecode_sink_caps_str);
  }
  pad_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      caps);
  gst_caps_unref (caps);
  gst_element_class_add_pad_template (element_class, pad_template);

  /* src pad */
  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapidecode_src_factory);
}

static void
gst_vaapidecode_init (GstVaapiDecode * decode)
{
  GstVideoDecoder *const vdec = GST_VIDEO_DECODER (decode);

  gst_vaapi_plugin_base_init (GST_VAAPI_PLUGIN_BASE (decode), GST_CAT_DEFAULT);

  gst_video_decoder_set_packetized (vdec, FALSE);
}

gboolean
gst_vaapidecode_register (GstPlugin * plugin, GArray * decoders)
{
  gboolean ret = FALSE;
  guint i, codec, rank;
  gchar *type_name, *element_name, *sink_caps_str;
  const gchar *name;
  GType type;
  GTypeInfo typeinfo = {
    sizeof (GstVaapiDecodeClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_vaapidecode_class_init,
    NULL,
    NULL,
    sizeof (GstVaapiDecode),
    0,
    (GInstanceInitFunc) gst_vaapidecode_init,
  };

  for (i = 0; i < G_N_ELEMENTS (vaapi_decode_map); i++) {
    codec = vaapi_decode_map[i].codec;
    rank = vaapi_decode_map[i].rank;
    name = vaapi_decode_map[i].name;

    if (codec && !gst_vaapi_codecs_has_codec (decoders, codec))
      continue;

    if (!gst_vaapidecode_sink_caps_str) {
      gst_vaapidecode_sink_caps_str = g_strdup (vaapi_decode_map[i].caps_str);
    } else {
      sink_caps_str = g_strconcat (gst_vaapidecode_sink_caps_str, "; ",
          vaapi_decode_map[i].caps_str, NULL);
      g_clear_pointer (&gst_vaapidecode_sink_caps_str, g_free);
      if (!sink_caps_str)
        break;
      gst_vaapidecode_sink_caps_str = sink_caps_str;
    }

    if (codec) {
      type_name = g_strdup_printf ("GstVaapiDecode_%s", name);
      element_name = g_strdup_printf ("vaapi%sdec", name);
    } else {
      type_name = g_strdup ("GstVaapiDecode");
      element_name = g_strdup_printf ("vaapidecode");
    }

    type = g_type_from_name (type_name);
    if (!type) {
      /* create the gtype now */
      type = g_type_register_static (GST_TYPE_VIDEO_DECODER, type_name,
          &typeinfo, 0);
      gst_vaapi_plugin_base_init_interfaces (type);
      g_type_set_qdata (type, GST_VAAPI_DECODE_PARAMS_QDATA,
          (gpointer) & vaapi_decode_map[i]);
    }

    /* Register GstVaapiDecode as GObject type, but not in GStreamer, so
     * vaapidecodebin can use it internally, but no exposed as a plugin
     * feature */
    if (codec)
      ret |= gst_element_register (plugin, element_name, rank, type);

    g_free (element_name);
    g_free (type_name);
  }

  return ret;
}
