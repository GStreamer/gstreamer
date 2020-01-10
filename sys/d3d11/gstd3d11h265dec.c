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
#include <config.h>
#endif

#include "gstd3d11h265dec.h"
#include "gstd3d11memory.h"
#include "gstd3d11bufferpool.h"
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_h265_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_h265_dec_debug

enum
{
  PROP_0,
  PROP_ADAPTER
};

#define DEFAULT_ADAPTER -1

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN,
    0x5b11d51b, 0x2f4c, 0x4452, 0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10,
    0x107af0e0, 0xef1a, 0x4d19, 0xab, 0xa8, 0x67, 0xa1, 0x63, 0x07, 0x3d, 0x13);

/* worst case 16 + 4 margin */
#define NUM_OUTPUT_VIEW 20

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "stream-format=(string) { hev1, hvc1, byte-stream }, "
        "alignment=(string) au, " "profile = (string) { main-10, main }")
    );

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, "{ NV12, P010_10LE }") "; "
        GST_VIDEO_CAPS_MAKE ("{ NV12, P010_10LE }")));

struct _GstD3D11H265DecPrivate
{
  DXVA_PicEntry_HEVC ref_pic_list[15];
  INT pic_order_cnt_val_list[15];
  UCHAR ref_pic_set_st_curr_before[8];
  UCHAR ref_pic_set_st_curr_after[8];
  UCHAR ref_pic_set_lt_curr[8];
};

#define parent_class gst_d3d11_h265_dec_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11H265Dec,
    gst_d3d11_h265_dec, GST_TYPE_H265_DECODER);

static void gst_d3d11_h265_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_h265_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_h265_dec_dispose (GObject * object);
static void gst_d3d11_h265_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_h265_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_start (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_stop (GstVideoDecoder * decoder);
static GstFlowReturn gst_d3d11_h265_dec_handle_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame);
static gboolean gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_h265_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstH265Decoder */
static gboolean gst_d3d11_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps);
static gboolean gst_d3d11_h265_dec_new_picture (GstH265Decoder * decoder,
    GstH265Picture * picture);
static GstFlowReturn gst_d3d11_h265_dec_output_picture (GstH265Decoder *
    decoder, GstH265Picture * picture);
static gboolean gst_d3d11_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb);
static gboolean gst_d3d11_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice);
static gboolean gst_d3d11_h265_dec_end_picture (GstH265Decoder * decoder,
    GstH265Picture * picture);

static void
gst_d3d11_h265_dec_class_init (GstD3D11H265DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH265DecoderClass *h265decoder_class = GST_H265_DECODER_CLASS (klass);

  gobject_class->set_property = gst_d3d11_h265_dec_set_property;
  gobject_class->get_property = gst_d3d11_h265_dec_get_property;
  gobject_class->dispose = gst_d3d11_h265_dec_dispose;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 H.265 Video Decoder",
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based H.265 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_close);
  decoder_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_stop);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_handle_frame);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_src_query);

  h265decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_new_sequence);
  h265decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_new_picture);
  h265decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_output_picture);
  h265decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_start_picture);
  h265decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_decode_slice);
  h265decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_end_picture);
}

static void
gst_d3d11_h265_dec_init (GstD3D11H265Dec * self)
{
  self->priv = gst_d3d11_h265_dec_get_instance_private (self);
  self->slice_list = g_array_new (FALSE, TRUE, sizeof (DXVA_Slice_HEVC_Short));
  self->adapter = DEFAULT_ADAPTER;
}

static void
gst_d3d11_h265_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_h265_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_h265_dec_dispose (GObject * object)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (object);

  if (self->slice_list) {
    g_array_unref (self->slice_list);
    self->slice_list = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_h265_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (element);

  gst_d3d11_handle_set_context (element, context, self->adapter, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_h265_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), self->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11device");
    return FALSE;
  }

  self->d3d11_decoder = gst_d3d11_decoder_new (self->device);

  if (!self->d3d11_decoder) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11 decoder");
    gst_clear_object (&self->device);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_start (GstVideoDecoder * decoder)
{
  return GST_VIDEO_DECODER_CLASS (parent_class)->start (decoder);
}

static gboolean
gst_d3d11_h265_dec_stop (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  gst_h265_picture_replace (&self->current_picture, NULL);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static GstFlowReturn
gst_d3d11_h265_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstBuffer *in_buf = frame->input_buffer;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  if (!self->current_picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    gst_video_decoder_drop_frame (decoder, frame);

    return GST_FLOW_ERROR;
  }

  gst_video_codec_frame_set_user_data (frame,
      self->current_picture, (GDestroyNotify) gst_h265_picture_unref);
  self->current_picture = NULL;

  gst_video_codec_frame_unref (frame);

  return GST_FLOW_OK;
}

static gboolean
gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstH265Decoder *h265dec = GST_H265_DECODER (decoder);
  GstCaps *peer_caps;

  GST_DEBUG_OBJECT (self, "negotiate");

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  self->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      self->out_format, self->width, self->height, h265dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  self->use_d3d11_output = FALSE;

  if (!peer_caps || gst_caps_is_any (peer_caps)) {
    GST_DEBUG_OBJECT (self,
        "cannot determine output format, use system memory");
  } else {
    GstCapsFeatures *features;
    guint size = gst_caps_get_size (peer_caps);
    guint i;

    for (i = 0; i < size; i++) {
      features = gst_caps_get_features (peer_caps, i);
      if (features && gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
        GST_DEBUG_OBJECT (self, "found D3D11 memory feature");
        gst_caps_set_features (self->output_state->caps, 0,
            gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));

        self->use_d3d11_output = TRUE;
        break;
      }
    }
  }
  gst_clear_caps (&peer_caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;

  GST_DEBUG_OBJECT (self, "decide allocation");

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps) {
    GST_DEBUG_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool && (self->use_d3d11_output && !GST_D3D11_BUFFER_POOL (pool))) {
    gst_object_unref (pool);
    pool = NULL;
  }

  if (!pool) {
    if (self->use_d3d11_output)
      pool = gst_d3d11_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();

    min = max = 0;
    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (self->use_d3d11_output) {
    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params)
      d3d11_params = gst_d3d11_allocation_params_new (&vinfo, 0, 0);

    /* dxva2 decoder uses non-resource format
     * (e.g., use NV12 instead of R8 + R8G8 */
    d3d11_params->desc[0].Width = GST_VIDEO_INFO_WIDTH (&vinfo);
    d3d11_params->desc[0].Height = GST_VIDEO_INFO_HEIGHT (&vinfo);
    d3d11_params->desc[0].Format = d3d11_params->d3d11_format->dxgi_format;

    d3d11_params->flags &= ~GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT;

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  gst_buffer_pool_set_config (pool, config);
  if (self->use_d3d11_output)
    size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_h265_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  gint crop_width, crop_height;
  gboolean modified = FALSE;
  static const GUID *main_10_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10;
  static const GUID *main_guid = &GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN;

  GST_LOG_OBJECT (self, "new sequence");

  if (sps->conformance_window_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  } else {
    crop_width = sps->width;
    crop_height = sps->height;
  }

  if (self->width != crop_width || self->height != crop_height ||
      self->coded_width != sps->width || self->coded_height != sps->height) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d", crop_width, crop_height);
    self->width = crop_width;
    self->height = crop_height;
    self->coded_width = sps->width;
    self->coded_height = sps->height;
    modified = TRUE;
  }

  if (self->bitdepth != sps->bit_depth_luma_minus8 + 8) {
    GST_INFO_OBJECT (self, "bitdepth changed");
    self->bitdepth = sps->bit_depth_luma_minus8 + 8;
    modified = TRUE;
  }

  if (self->chroma_format_idc != sps->chroma_format_idc) {
    GST_INFO_OBJECT (self, "chroma format changed");
    self->chroma_format_idc = sps->chroma_format_idc;
    modified = TRUE;
  }

  if (modified || !self->d3d11_decoder->opened) {
    const GUID *profile_guid = NULL;
    GstVideoInfo info;

    self->out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->bitdepth == 8) {
      if (self->chroma_format_idc == 1) {
        self->out_format = GST_VIDEO_FORMAT_NV12;
        profile_guid = main_guid;
      } else {
        GST_FIXME_OBJECT (self, "Could not support 8bits non-4:2:0 format");
      }
    } else if (self->bitdepth == 10) {
      if (self->chroma_format_idc == 1) {
        self->out_format = GST_VIDEO_FORMAT_P010_10LE;
        profile_guid = main_10_guid;
      } else {
        GST_FIXME_OBJECT (self, "Could not support 10bits non-4:2:0 format");
      }
    }

    if (self->out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return FALSE;
    }

    /* allocated internal pool with coded width/height */
    gst_video_info_set_format (&info,
        self->out_format, self->coded_width, self->coded_height);

    gst_d3d11_decoder_reset (self->d3d11_decoder);
    if (!gst_d3d11_decoder_open (self->d3d11_decoder, GST_D3D11_CODEC_H265,
            &info, NUM_OUTPUT_VIEW, &profile_guid, 1)) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return FALSE;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_get_bitstream_buffer (GstD3D11H265Dec * self)
{
  GST_TRACE_OBJECT (self, "Getting bitstream buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &self->bitstream_buffer_size,
          (gpointer *) & self->bitstream_buffer_bytes)) {
    GST_ERROR_OBJECT (self, "Faild to get bitstream buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Got bitstream buffer %p with size %d",
      self->bitstream_buffer_bytes, self->bitstream_buffer_size);
  self->current_offset = 0;

  return TRUE;
}

static GstD3D11DecoderOutputView *
gst_d3d11_h265_dec_get_output_view_from_picture (GstD3D11H265Dec * self,
    GstH265Picture * picture)
{
  GstBuffer *view_buffer;
  GstD3D11DecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_h265_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view = gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static gint
gst_d3d11_h265_dec_get_ref_index (GstD3D11H265Dec * self, gint view_id)
{
  GstD3D11H265DecPrivate *priv = self->priv;

  gint i;
  for (i = 0; i < G_N_ELEMENTS (priv->ref_pic_list); i++) {
    if (priv->ref_pic_list[i].Index7Bits == view_id)
      return i;
  }

  return 0xff;
}

static gboolean
gst_d3d11_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecPrivate *priv = self->priv;
  GstD3D11DecoderOutputView *view;
  gint i, j;
  GArray *dpb_array;

  view = gst_d3d11_h265_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");

  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  for (i = 0; i < 15; i++) {
    priv->ref_pic_list[i].bPicEntry = 0xff;
    priv->pic_order_cnt_val_list[i] = 0;
  }

  for (i = 0; i < 8; i++) {
    priv->ref_pic_set_st_curr_before[i] = 0xff;
    priv->ref_pic_set_st_curr_after[i] = 0xff;
    priv->ref_pic_set_lt_curr[i] = 0xff;
  }

  dpb_array = gst_h265_dpb_get_pictures_all (dpb);

  GST_LOG_OBJECT (self, "DPB size %d", dpb_array->len);

  for (i = 0; i < dpb_array->len && i < G_N_ELEMENTS (priv->ref_pic_list); i++) {
    GstH265Picture *other = g_array_index (dpb_array, GstH265Picture *, i);
    GstD3D11DecoderOutputView *other_view;
    gint id = 0xff;

    if (!other->ref) {
      GST_LOG_OBJECT (self, "%dth picture in dpb is not reference, skip", i);
      continue;
    }

    other_view = gst_d3d11_h265_dec_get_output_view_from_picture (self, other);

    if (other_view)
      id = other_view->view_id;

    priv->ref_pic_list[i].Index7Bits = id;
    priv->ref_pic_list[i].AssociatedFlag = other->long_term;
    priv->pic_order_cnt_val_list[i] = other->pic_order_cnt;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (priv->ref_pic_set_st_curr_before); i++) {
    GstH265Picture *other = NULL;
    gint id = 0xff;

    while (other == NULL && j < decoder->NumPocStCurrBefore)
      other = decoder->RefPicSetStCurrBefore[j++];

    if (other) {
      GstD3D11DecoderOutputView *other_view;

      other_view =
          gst_d3d11_h265_dec_get_output_view_from_picture (self, other);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (self, other_view->view_id);
    }

    priv->ref_pic_set_st_curr_before[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (priv->ref_pic_set_st_curr_after); i++) {
    GstH265Picture *other = NULL;
    gint id = 0xff;

    while (other == NULL && j < decoder->NumPocStCurrAfter)
      other = decoder->RefPicSetStCurrAfter[j++];

    if (other) {
      GstD3D11DecoderOutputView *other_view;

      other_view =
          gst_d3d11_h265_dec_get_output_view_from_picture (self, other);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (self, other_view->view_id);
    }

    priv->ref_pic_set_st_curr_after[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (priv->ref_pic_set_lt_curr); i++) {
    GstH265Picture *other = NULL;
    gint id = 0xff;

    while (other == NULL && j < decoder->NumPocLtCurr)
      other = decoder->RefPicSetLtCurr[j++];

    if (other) {
      GstD3D11DecoderOutputView *other_view;

      other_view =
          gst_d3d11_h265_dec_get_output_view_from_picture (self, other);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (self, other_view->view_id);
    }

    priv->ref_pic_set_lt_curr[i] = id;
  }

  g_array_unref (dpb_array);
  g_array_set_size (self->slice_list, 0);

  return gst_d3d11_h265_dec_get_bitstream_buffer (self);
}

static gboolean
gst_d3d11_h265_dec_new_picture (GstH265Decoder * decoder,
    GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstBuffer *view_buffer;
  GstD3D11Memory *mem;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (self->d3d11_decoder);
  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "No available output view buffer");
    return FALSE;
  }

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (view_buffer, 0);

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT " (index %d)",
      view_buffer, mem->subresource_index);

  gst_h265_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New h265picture %p", picture);

  gst_h265_picture_replace (&self->current_picture, picture);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_h265_dec_output_picture (GstH265Decoder * decoder,
    GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GList *pending_frames, *iter;
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *output_buffer = NULL;
  GstFlowReturn ret;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self,
      "Outputting picture %p, poc %d", picture, picture->pic_order_cnt);

  view_buffer = (GstBuffer *) gst_h265_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    return FALSE;
  }

  pending_frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

  for (iter = pending_frames; iter; iter = g_list_next (iter)) {
    GstVideoCodecFrame *tmp;
    GstH265Picture *other_pic;

    tmp = (GstVideoCodecFrame *) iter->data;
    other_pic = gst_video_codec_frame_get_user_data (tmp);
    if (!other_pic) {
      /* FIXME: what should we do here? */
      GST_WARNING_OBJECT (self,
          "Codec frame %p does not have corresponding picture object", tmp);
      continue;
    }

    if (other_pic == picture) {
      frame = gst_video_codec_frame_ref (tmp);
      break;
    }
  }

  g_list_free_full (pending_frames,
      (GDestroyNotify) gst_video_codec_frame_unref);

  if (!frame) {
    GST_WARNING_OBJECT (self,
        "Failed to find codec frame for picture %p", picture);

    output_buffer =
        gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));

    if (!output_buffer) {
      GST_ERROR_OBJECT (self, "Couldn't allocate output buffer");
      return GST_FLOW_ERROR;
    }

    GST_BUFFER_PTS (output_buffer) = picture->pts;
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) = GST_CLOCK_TIME_NONE;
  } else {
    ret =
        gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (self),
        frame);

    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate output frame");
      return ret;
    }

    output_buffer = frame->output_buffer;
    GST_BUFFER_PTS (output_buffer) = GST_BUFFER_PTS (frame->input_buffer);
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) =
        GST_BUFFER_DURATION (frame->input_buffer);
  }

  if (!gst_d3d11_decoder_copy_decoder_buffer (self->d3d11_decoder,
          &self->output_state->info, view_buffer, output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    if (frame)
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    else
      gst_buffer_unref (output_buffer);

    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (output_buffer)));

  if (frame) {
    ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
  } else {
    ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), output_buffer);
  }

  return ret;
}

static gboolean
gst_d3d11_h265_dec_submit_slice_data (GstD3D11H265Dec * self)
{
  guint buffer_size;
  gpointer buffer;
  guint8 *data;
  gsize offset = 0;
  gint i;
  D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[4] = { 0, };
  gboolean ret;
  guint buffer_count = 0;

  if (self->slice_list->len < 1) {
    GST_WARNING_OBJECT (self, "Nothing to submit");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Getting slice control buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &buffer_size, &buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");
    return FALSE;
  }

  data = buffer;
  for (i = 0; i < self->slice_list->len; i++) {
    DXVA_Slice_HEVC_Short *slice_data =
        &g_array_index (self->slice_list, DXVA_Slice_HEVC_Short, i);

    memcpy (data + offset, slice_data, sizeof (DXVA_Slice_HEVC_Short));
    offset += sizeof (DXVA_Slice_HEVC_Short);
  }

  GST_TRACE_OBJECT (self, "Release slice control buffer");
  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL)) {
    GST_ERROR_OBJECT (self, "Failed to release slice control buffer");
    return FALSE;
  }

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
    GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");
    return FALSE;
  }

  buffer_desc[buffer_count].BufferType =
      D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
  buffer_desc[buffer_count].DataOffset = 0;
  buffer_desc[buffer_count].DataSize = sizeof (DXVA_PicParams_HEVC);
  buffer_count++;

  if (self->submit_iq_data) {
    buffer_desc[buffer_count].BufferType =
        D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
    buffer_desc[buffer_count].DataOffset = 0;
    buffer_desc[buffer_count].DataSize = sizeof (DXVA_Qmatrix_HEVC);
    buffer_count++;
  }

  buffer_desc[buffer_count].BufferType =
      D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
  buffer_desc[buffer_count].DataOffset = 0;
  buffer_desc[buffer_count].DataSize =
      sizeof (DXVA_Slice_HEVC_Short) * self->slice_list->len;
  buffer_count++;

  buffer_desc[buffer_count].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
  buffer_desc[buffer_count].DataOffset = 0;
  buffer_desc[buffer_count].DataSize = self->current_offset;
  buffer_count++;

  ret = gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
      buffer_count, buffer_desc);

  self->current_offset = 0;
  self->bitstream_buffer_bytes = NULL;
  self->bitstream_buffer_size = 0;
  g_array_set_size (self->slice_list, 0);

  return ret;
}

static gboolean
gst_d3d11_h265_dec_end_picture (GstH265Decoder * decoder,
    GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  GST_LOG_OBJECT (self, "end picture %p, (poc %d)",
      picture, picture->pic_order_cnt);

  if (!gst_d3d11_h265_dec_submit_slice_data (self)) {
    GST_ERROR_OBJECT (self, "Failed to submit slice data");
    return FALSE;
  }

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

static void
gst_d3d11_h265_dec_picture_params_from_sps (GstD3D11H265Dec * self,
    const GstH265SPS * sps, DXVA_PicParams_HEVC * params)
{
#define COPY_FIELD(f) \
  (params)->f = (sps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(sps_,f) = (sps)->f

  params->PicWidthInMinCbsY =
      sps->width >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
  params->PicHeightInMinCbsY =
      sps->height >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
  params->sps_max_dec_pic_buffering_minus1 =
      sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1];

  COPY_FIELD (chroma_format_idc);
  COPY_FIELD (separate_colour_plane_flag);
  COPY_FIELD (bit_depth_luma_minus8);
  COPY_FIELD (bit_depth_chroma_minus8);
  COPY_FIELD (log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (log2_min_luma_coding_block_size_minus3);
  COPY_FIELD (log2_diff_max_min_luma_coding_block_size);
  COPY_FIELD (log2_min_transform_block_size_minus2);
  COPY_FIELD (log2_diff_max_min_transform_block_size);
  COPY_FIELD (max_transform_hierarchy_depth_inter);
  COPY_FIELD (max_transform_hierarchy_depth_intra);
  COPY_FIELD (num_short_term_ref_pic_sets);
  COPY_FIELD (num_long_term_ref_pics_sps);
  COPY_FIELD (scaling_list_enabled_flag);
  COPY_FIELD (amp_enabled_flag);
  COPY_FIELD (sample_adaptive_offset_enabled_flag);
  COPY_FIELD (pcm_enabled_flag);

  if (sps->pcm_enabled_flag) {
    COPY_FIELD (pcm_sample_bit_depth_luma_minus1);
    COPY_FIELD (pcm_sample_bit_depth_chroma_minus1);
    COPY_FIELD (log2_min_pcm_luma_coding_block_size_minus3);
    COPY_FIELD (log2_diff_max_min_pcm_luma_coding_block_size);
  }

  COPY_FIELD (pcm_loop_filter_disabled_flag);
  COPY_FIELD (long_term_ref_pics_present_flag);
  COPY_FIELD_WITH_PREFIX (temporal_mvp_enabled_flag);
  COPY_FIELD (strong_intra_smoothing_enabled_flag);

#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
}

static void
gst_d3d11_h265_dec_picture_params_from_pps (GstD3D11H265Dec * self,
    const GstH265PPS * pps, DXVA_PicParams_HEVC * params)
{
  gint i;

#define COPY_FIELD(f) \
  (params)->f = (pps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(pps_,f) = (pps)->f

  COPY_FIELD (num_ref_idx_l0_default_active_minus1);
  COPY_FIELD (num_ref_idx_l1_default_active_minus1);
  COPY_FIELD (init_qp_minus26);
  COPY_FIELD (dependent_slice_segments_enabled_flag);
  COPY_FIELD (output_flag_present_flag);
  COPY_FIELD (num_extra_slice_header_bits);
  COPY_FIELD (sign_data_hiding_enabled_flag);
  COPY_FIELD (cabac_init_present_flag);
  COPY_FIELD (constrained_intra_pred_flag);
  COPY_FIELD (transform_skip_enabled_flag);
  COPY_FIELD (cu_qp_delta_enabled_flag);
  COPY_FIELD_WITH_PREFIX (slice_chroma_qp_offsets_present_flag);
  COPY_FIELD (weighted_pred_flag);
  COPY_FIELD (weighted_bipred_flag);
  COPY_FIELD (transquant_bypass_enabled_flag);
  COPY_FIELD (tiles_enabled_flag);
  COPY_FIELD (entropy_coding_sync_enabled_flag);
  COPY_FIELD (uniform_spacing_flag);

  if (pps->tiles_enabled_flag)
    COPY_FIELD (loop_filter_across_tiles_enabled_flag);

  COPY_FIELD_WITH_PREFIX (loop_filter_across_slices_enabled_flag);
  COPY_FIELD (deblocking_filter_override_enabled_flag);
  COPY_FIELD_WITH_PREFIX (deblocking_filter_disabled_flag);
  COPY_FIELD (lists_modification_present_flag);
  COPY_FIELD (slice_segment_header_extension_present_flag);
  COPY_FIELD_WITH_PREFIX (cb_qp_offset);
  COPY_FIELD_WITH_PREFIX (cr_qp_offset);

  if (pps->tiles_enabled_flag) {
    COPY_FIELD (num_tile_columns_minus1);
    COPY_FIELD (num_tile_rows_minus1);
    if (!pps->uniform_spacing_flag) {
      for (i = 0; i < pps->num_tile_columns_minus1 + 1; i++)
        COPY_FIELD (column_width_minus1[i]);

      for (i = 0; i < pps->num_tile_rows_minus1 + 1; i++)
        COPY_FIELD (row_height_minus1[i]);
    }
  }

  COPY_FIELD (diff_cu_qp_delta_depth);
  COPY_FIELD_WITH_PREFIX (beta_offset_div2);
  COPY_FIELD_WITH_PREFIX (tc_offset_div2);
  COPY_FIELD (log2_parallel_merge_level_minus2);

#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
}

static void
gst_d3d11_h265_dec_picture_params_from_slice_header (GstD3D11H265Dec *
    self, const GstH265SliceHdr * slice_header, DXVA_PicParams_HEVC * params)
{
  if (slice_header->short_term_ref_pic_set_sps_flag == 0) {
    params->ucNumDeltaPocsOfRefRpsIdx =
        slice_header->short_term_ref_pic_sets.NumDeltaPocs;
    params->wNumBitsForShortTermRPSInSlice =
        slice_header->short_term_ref_pic_set_size;
  }
}

static gboolean
gst_d3d11_h265_dec_fill_picture_params (GstD3D11H265Dec * self,
    const GstH265SliceHdr * slice_header, DXVA_PicParams_HEVC * params)
{
  const GstH265SPS *sps;
  const GstH265PPS *pps;

  g_return_val_if_fail (slice_header->pps != NULL, FALSE);
  g_return_val_if_fail (slice_header->pps->sps != NULL, FALSE);

  pps = slice_header->pps;
  sps = pps->sps;

  memset (params, 0, sizeof (DXVA_PicParams_HEVC));

  /* not related to hevc syntax */
  params->NoPicReorderingFlag = 0;
  params->NoBiPredFlag = 0;
  params->ReservedBits1 = 0;
  params->StatusReportFeedbackNumber = 1;

  gst_d3d11_h265_dec_picture_params_from_sps (self, sps, params);
  gst_d3d11_h265_dec_picture_params_from_pps (self, pps, params);
  gst_d3d11_h265_dec_picture_params_from_slice_header (self,
      slice_header, params);

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_d3d11_h265_dec_dump_pic_params (GstD3D11H265Dec * self,
    DXVA_PicParams_HEVC * params)
{
  gint i;

  GST_TRACE_OBJECT (self, "Dump current DXVA_PicParams_HEVC");

#define DUMP_PIC_PARAMS(p) \
  GST_TRACE_OBJECT (self, "\t" G_STRINGIFY(p) ": %d", (gint)params->p)

  DUMP_PIC_PARAMS (PicWidthInMinCbsY);
  DUMP_PIC_PARAMS (PicHeightInMinCbsY);
  DUMP_PIC_PARAMS (chroma_format_idc);
  DUMP_PIC_PARAMS (separate_colour_plane_flag);
  DUMP_PIC_PARAMS (bit_depth_chroma_minus8);
  DUMP_PIC_PARAMS (NoPicReorderingFlag);
  DUMP_PIC_PARAMS (NoBiPredFlag);
  DUMP_PIC_PARAMS (CurrPic.Index7Bits);
  DUMP_PIC_PARAMS (sps_max_dec_pic_buffering_minus1);
  DUMP_PIC_PARAMS (log2_min_luma_coding_block_size_minus3);
  DUMP_PIC_PARAMS (log2_diff_max_min_luma_coding_block_size);
  DUMP_PIC_PARAMS (log2_min_transform_block_size_minus2);
  DUMP_PIC_PARAMS (log2_diff_max_min_transform_block_size);
  DUMP_PIC_PARAMS (max_transform_hierarchy_depth_inter);
  DUMP_PIC_PARAMS (max_transform_hierarchy_depth_intra);
  DUMP_PIC_PARAMS (num_short_term_ref_pic_sets);
  DUMP_PIC_PARAMS (num_long_term_ref_pics_sps);
  DUMP_PIC_PARAMS (num_ref_idx_l0_default_active_minus1);
  DUMP_PIC_PARAMS (num_ref_idx_l1_default_active_minus1);
  DUMP_PIC_PARAMS (init_qp_minus26);
  DUMP_PIC_PARAMS (ucNumDeltaPocsOfRefRpsIdx);
  DUMP_PIC_PARAMS (wNumBitsForShortTermRPSInSlice);
  DUMP_PIC_PARAMS (scaling_list_enabled_flag);
  DUMP_PIC_PARAMS (amp_enabled_flag);
  DUMP_PIC_PARAMS (sample_adaptive_offset_enabled_flag);
  DUMP_PIC_PARAMS (pcm_enabled_flag);
  DUMP_PIC_PARAMS (pcm_sample_bit_depth_luma_minus1);
  DUMP_PIC_PARAMS (pcm_sample_bit_depth_chroma_minus1);
  DUMP_PIC_PARAMS (log2_min_pcm_luma_coding_block_size_minus3);
  DUMP_PIC_PARAMS (log2_diff_max_min_pcm_luma_coding_block_size);
  DUMP_PIC_PARAMS (pcm_loop_filter_disabled_flag);
  DUMP_PIC_PARAMS (long_term_ref_pics_present_flag);
  DUMP_PIC_PARAMS (sps_temporal_mvp_enabled_flag);
  DUMP_PIC_PARAMS (strong_intra_smoothing_enabled_flag);
  DUMP_PIC_PARAMS (dependent_slice_segments_enabled_flag);
  DUMP_PIC_PARAMS (output_flag_present_flag);
  DUMP_PIC_PARAMS (num_extra_slice_header_bits);
  DUMP_PIC_PARAMS (sign_data_hiding_enabled_flag);
  DUMP_PIC_PARAMS (cabac_init_present_flag);

  DUMP_PIC_PARAMS (constrained_intra_pred_flag);
  DUMP_PIC_PARAMS (transform_skip_enabled_flag);
  DUMP_PIC_PARAMS (cu_qp_delta_enabled_flag);
  DUMP_PIC_PARAMS (pps_slice_chroma_qp_offsets_present_flag);
  DUMP_PIC_PARAMS (weighted_pred_flag);
  DUMP_PIC_PARAMS (weighted_bipred_flag);
  DUMP_PIC_PARAMS (transquant_bypass_enabled_flag);
  DUMP_PIC_PARAMS (tiles_enabled_flag);
  DUMP_PIC_PARAMS (entropy_coding_sync_enabled_flag);
  DUMP_PIC_PARAMS (uniform_spacing_flag);
  DUMP_PIC_PARAMS (loop_filter_across_tiles_enabled_flag);
  DUMP_PIC_PARAMS (pps_loop_filter_across_slices_enabled_flag);
  DUMP_PIC_PARAMS (deblocking_filter_override_enabled_flag);
  DUMP_PIC_PARAMS (pps_deblocking_filter_disabled_flag);
  DUMP_PIC_PARAMS (lists_modification_present_flag);
  DUMP_PIC_PARAMS (IrapPicFlag);
  DUMP_PIC_PARAMS (IdrPicFlag);
  DUMP_PIC_PARAMS (IntraPicFlag);
  DUMP_PIC_PARAMS (pps_cb_qp_offset);
  DUMP_PIC_PARAMS (pps_cr_qp_offset);
  DUMP_PIC_PARAMS (num_tile_columns_minus1);
  DUMP_PIC_PARAMS (num_tile_rows_minus1);
  for (i = 0; i < G_N_ELEMENTS (params->column_width_minus1); i++)
    GST_TRACE_OBJECT (self, "\tcolumn_width_minus1[%d]: %d", i,
        params->column_width_minus1[i]);
  for (i = 0; i < G_N_ELEMENTS (params->row_height_minus1); i++)
    GST_TRACE_OBJECT (self, "\trow_height_minus1[%d]: %d", i,
        params->row_height_minus1[i]);
  DUMP_PIC_PARAMS (diff_cu_qp_delta_depth);
  DUMP_PIC_PARAMS (pps_beta_offset_div2);
  DUMP_PIC_PARAMS (pps_tc_offset_div2);
  DUMP_PIC_PARAMS (log2_parallel_merge_level_minus2);
  DUMP_PIC_PARAMS (CurrPicOrderCntVal);

  for (i = 0; i < G_N_ELEMENTS (params->RefPicList); i++) {
    GST_TRACE_OBJECT (self, "\tRefPicList[%d].Index7Bits: %d", i,
        params->RefPicList[i].Index7Bits);
    GST_TRACE_OBJECT (self, "\tRefPicList[%d].AssociatedFlag: %d", i,
        params->RefPicList[i].AssociatedFlag);
    GST_TRACE_OBJECT (self, "\tPicOrderCntValList[%d]: %d", i,
        params->PicOrderCntValList[i]);
  }

  for (i = 0; i < G_N_ELEMENTS (params->RefPicSetStCurrBefore); i++) {
    GST_TRACE_OBJECT (self, "\tRefPicSetStCurrBefore[%d]: %d", i,
        params->RefPicSetStCurrBefore[i]);
    GST_TRACE_OBJECT (self, "\tRefPicSetStCurrAfter[%d]: %d", i,
        params->RefPicSetStCurrAfter[i]);
    GST_TRACE_OBJECT (self, "\tRefPicSetLtCurr[%d]: %d", i,
        params->RefPicSetLtCurr[i]);
  }

#undef DUMP_PIC_PARAMS
}
#endif

static gboolean
gst_d3d11_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecPrivate *priv = self->priv;
  GstH265PPS *pps;
  DXVA_PicParams_HEVC pic_params = { 0, };
  DXVA_Qmatrix_HEVC iq_matrix = { 0, };
  guint d3d11_buffer_size = 0;
  gpointer d3d11_buffer = NULL;
  gint i, j;
  GstD3D11DecoderOutputView *view;

  pps = slice->header.pps;

  view = gst_d3d11_h265_dec_get_output_view_from_picture (self, picture);

  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view");
    return FALSE;
  }

  gst_d3d11_h265_dec_fill_picture_params (self, &slice->header, &pic_params);

  pic_params.CurrPic.Index7Bits = view->view_id;
  pic_params.IrapPicFlag = IS_IRAP (slice->nalu.type);
  pic_params.IdrPicFlag = IS_IDR (slice->nalu.type);
  pic_params.IntraPicFlag = IS_IRAP (slice->nalu.type);
  pic_params.CurrPicOrderCntVal = picture->pic_order_cnt;

  memcpy (pic_params.RefPicList, priv->ref_pic_list,
      sizeof (pic_params.RefPicList));
  memcpy (pic_params.PicOrderCntValList, priv->pic_order_cnt_val_list,
      sizeof (pic_params.PicOrderCntValList));
  memcpy (pic_params.RefPicSetStCurrBefore, priv->ref_pic_set_st_curr_before,
      sizeof (pic_params.RefPicSetStCurrBefore));
  memcpy (pic_params.RefPicSetStCurrAfter, priv->ref_pic_set_st_curr_after,
      sizeof (pic_params.RefPicSetStCurrAfter));
  memcpy (pic_params.RefPicSetLtCurr, priv->ref_pic_set_lt_curr,
      sizeof (pic_params.RefPicSetLtCurr));

#ifndef GST_DISABLE_GST_DEBUG
  gst_d3d11_h265_dec_dump_pic_params (self, &pic_params);
#endif

  GST_TRACE_OBJECT (self, "Getting picture param decoder buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for picture parameters");
    return FALSE;
  }

  memcpy (d3d11_buffer, &pic_params, sizeof (pic_params));

  GST_TRACE_OBJECT (self, "Release picture param decoder buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  if (pps->scaling_list_data_present_flag) {
    self->submit_iq_data = TRUE;

    for (i = 0; i < 6; i++) {
      for (j = 0; j < 16; j++) {
        iq_matrix.ucScalingLists0[i][j] =
            pps->scaling_list.scaling_lists_4x4[i][j];
      }
    }

    for (i = 0; i < 6; i++) {
      for (j = 0; j < 64; j++) {
        iq_matrix.ucScalingLists1[i][j] =
            pps->scaling_list.scaling_lists_8x8[i][j];
        iq_matrix.ucScalingLists2[i][j] =
            pps->scaling_list.scaling_lists_16x16[i][j];
      }
    }

    for (i = 0; i < 2; i++) {
      for (j = 0; j < 64; j++) {
        iq_matrix.ucScalingLists3[i][j] =
            pps->scaling_list.scaling_lists_32x32[i][j];
      }
    }

    for (i = 0; i < 6; i++)
      iq_matrix.ucScalingListDCCoefSizeID2[i] =
          pps->scaling_list.scaling_list_dc_coef_minus8_16x16[i];

    for (i = 0; i < 2; i++)
      iq_matrix.ucScalingListDCCoefSizeID3[i] =
          pps->scaling_list.scaling_list_dc_coef_minus8_32x32[i];

    GST_TRACE_OBJECT (self, "Getting inverse quantization maxtirx buffer");

    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX,
            &d3d11_buffer_size, &d3d11_buffer)) {
      GST_ERROR_OBJECT (self,
          "Failed to get decoder buffer for inv. quantization matrix");
      return FALSE;
    }

    memcpy (d3d11_buffer, &iq_matrix, sizeof (iq_matrix));

    GST_TRACE_OBJECT (self, "Release inverse quantization maxtirx buffer");

    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX)) {
      GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
      return FALSE;
    }
  } else {
    self->submit_iq_data = FALSE;
  }

  {
    guint to_write = slice->nalu.size + 3;
    gboolean is_first = TRUE;

    while (to_write > 0) {
      guint bytes_to_copy;
      gboolean is_last = TRUE;
      DXVA_Slice_HEVC_Short slice_short = { 0, };

      if (self->bitstream_buffer_size < to_write && self->slice_list->len > 0) {
        if (!gst_d3d11_h265_dec_submit_slice_data (self)) {
          GST_ERROR_OBJECT (self, "Failed to submit bitstream buffers");
          return FALSE;
        }

        if (!gst_d3d11_h265_dec_get_bitstream_buffer (self)) {
          GST_ERROR_OBJECT (self, "Failed to get bitstream buffer");
          return FALSE;
        }
      }

      bytes_to_copy = to_write;

      if (bytes_to_copy > self->bitstream_buffer_size) {
        bytes_to_copy = self->bitstream_buffer_size;
        is_last = FALSE;
      }

      if (bytes_to_copy >= 3 && is_first) {
        /* normal case */
        self->bitstream_buffer_bytes[0] = 0;
        self->bitstream_buffer_bytes[1] = 0;
        self->bitstream_buffer_bytes[2] = 1;
        memcpy (self->bitstream_buffer_bytes + 3,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy - 3);
      } else {
        /* when this nal unit date is splitted into two buffer */
        memcpy (self->bitstream_buffer_bytes,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy);
      }

      slice_short.BSNALunitDataLocation = self->current_offset;
      slice_short.SliceBytesInBuffer = bytes_to_copy;
      /* wBadSliceChopping: (dxva h265 spec.)
       * 0: All bits for the slice are located within the corresponding
       *    bitstream data buffer
       * 1: The bitstream data buffer contains the start of the slice,
       *    but not the entire slice, because the buffer is full
       * 2: The bitstream data buffer contains the end of the slice.
       *    It does not contain the start of the slice, because the start of
       *    the slice was located in the previous bitstream data buffer.
       * 3: The bitstream data buffer does not contain the start of the slice
       *    (because the start of the slice was located in the previous
       *     bitstream data buffer), and it does not contain the end of the slice
       *    (because the current bitstream data buffer is also full).
       */
      if (is_last && is_first) {
        slice_short.wBadSliceChopping = 0;
      } else if (!is_last && is_first) {
        slice_short.wBadSliceChopping = 1;
      } else if (is_last && !is_first) {
        slice_short.wBadSliceChopping = 2;
      } else {
        slice_short.wBadSliceChopping = 3;
      }

      g_array_append_val (self->slice_list, slice_short);
      self->bitstream_buffer_size -= bytes_to_copy;
      self->current_offset += bytes_to_copy;
      self->bitstream_buffer_bytes += bytes_to_copy;
      is_first = FALSE;
      to_write -= bytes_to_copy;
    }
  }

  return TRUE;
}

void
gst_d3d11_h265_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  GstD3D11Decoder *decoder;
  GstVideoInfo info;
  gboolean ret;
  static const GUID *supported_profiles[] = {
    &GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN,
  };

  decoder = gst_d3d11_decoder_new (device);
  if (!decoder) {
    GST_WARNING_OBJECT (device, "decoder interface unavailable");
    return;
  }

  /* FIXME: DXVA does not provide API for query supported resolution
   * maybe we need some tries per standard resolution (e.g., HD, FullHD ...)
   * to check supported resolution */
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_NV12, 1280, 720);

  ret = gst_d3d11_decoder_open (decoder, GST_D3D11_CODEC_H265,
      &info, NUM_OUTPUT_VIEW, supported_profiles,
      G_N_ELEMENTS (supported_profiles));
  gst_object_unref (decoder);

  if (!ret) {
    GST_WARNING_OBJECT (device, "cannot open decoder device");
    return;
  }

  gst_element_register (plugin, "d3d11h265dec", rank, GST_TYPE_D3D11_H265_DEC);
}
