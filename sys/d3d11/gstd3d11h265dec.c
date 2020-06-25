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

#include <gst/codecs/gsth265decoder.h>
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
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN,
    0x5b11d51b, 0x2f4c, 0x4452, 0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10,
    0x107af0e0, 0xef1a, 0x4d19, 0xab, 0xa8, 0x67, 0xa1, 0x63, 0x07, 0x3d, 0x13);

typedef struct _GstD3D11H265Dec
{
  GstH265Decoder parent;

  GstVideoCodecState *output_state;

  GstD3D11Device *device;

  guint width, height;
  guint coded_width, coded_height;
  guint bitdepth;
  guint chroma_format_idc;
  GstVideoFormat out_format;

  /* Array of DXVA_Slice_HEVC_Short */
  GArray *slice_list;
  gboolean submit_iq_data;

  GstD3D11Decoder *d3d11_decoder;

  /* Pointing current bitstream buffer */
  gboolean bad_aligned_bitstream_buffer;
  guint written_buffer_size;
  guint remaining_buffer_size;
  guint8 *bitstream_buffer_data;

  gboolean use_d3d11_output;

  DXVA_PicEntry_HEVC ref_pic_list[15];
  INT pic_order_cnt_val_list[15];
  UCHAR ref_pic_set_st_curr_before[8];
  UCHAR ref_pic_set_st_curr_after[8];
  UCHAR ref_pic_set_lt_curr[8];
} GstD3D11H265Dec;

typedef struct _GstD3D11H265DecClass
{
  GstH265DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11H265DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_H265_DEC(object) ((GstD3D11H265Dec *) (object))
#define GST_D3D11_H265_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11H265DecClass))

static void gst_d3d11_h265_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_h265_dec_dispose (GObject * object);
static void gst_d3d11_h265_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_h265_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_h265_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstH265Decoder */
static gboolean gst_d3d11_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size);
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
gst_d3d11_h265_dec_class_init (GstD3D11H265DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH265DecoderClass *h265decoder_class = GST_H265_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_h265_dec_get_property;
  gobject_class->dispose = gst_d3d11_h265_dec_dispose;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, cdata->adapter,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  parent_class = g_type_class_peek_parent (klass);

  klass->adapter = cdata->adapter;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_set_context);

  long_name = g_strdup_printf ("Direct3D11 H.265 %s Decoder",
      cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based H.265 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_close);
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
  self->slice_list = g_array_new (FALSE, TRUE, sizeof (DXVA_Slice_HEVC_Short));
}

static void
gst_d3d11_h265_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_uint (value, klass->adapter);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, klass->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, klass->vendor_id);
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
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_h265_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (self);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), klass->adapter,
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

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstH265Decoder *h265dec = GST_H265_DECODER (decoder);

  if (!gst_d3d11_decoder_negotiate (decoder, h265dec->input_state,
          self->out_format, self->width, self->height, &self->output_state,
          &self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (decoder, query, self->device,
          GST_D3D11_CODEC_H265, self->use_d3d11_output))
    return FALSE;

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
    const GstH265SPS * sps, gint max_dpb_size)
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
    GST_INFO_OBJECT (self, "resolution changed %dx%d (%dx%d)",
        crop_width, crop_height, sps->width, sps->height);
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

    gst_video_info_set_format (&info,
        self->out_format, self->width, self->height);

    gst_d3d11_decoder_reset (self->d3d11_decoder);
    if (!gst_d3d11_decoder_open (self->d3d11_decoder, GST_D3D11_CODEC_H265,
            &info, self->coded_width, self->coded_height,
            /* Additional 4 views margin for zero-copy rendering */
            max_dpb_size + 4, &profile_guid, 1)) {
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
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &self->remaining_buffer_size,
          (gpointer *) & self->bitstream_buffer_data)) {
    GST_ERROR_OBJECT (self, "Faild to get bitstream buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Got bitstream buffer %p with size %d",
      self->bitstream_buffer_data, self->remaining_buffer_size);
  self->written_buffer_size = 0;
  if ((self->remaining_buffer_size & 127) != 0) {
    GST_WARNING_OBJECT (self,
        "The size of bitstream buffer is not 128 bytes aligned");
    self->bad_aligned_bitstream_buffer = TRUE;
  } else {
    self->bad_aligned_bitstream_buffer = FALSE;
  }

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
  gint i;
  for (i = 0; i < G_N_ELEMENTS (self->ref_pic_list); i++) {
    if (self->ref_pic_list[i].Index7Bits == view_id)
      return i;
  }

  return 0xff;
}

static gboolean
gst_d3d11_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
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
    self->ref_pic_list[i].bPicEntry = 0xff;
    self->pic_order_cnt_val_list[i] = 0;
  }

  for (i = 0; i < 8; i++) {
    self->ref_pic_set_st_curr_before[i] = 0xff;
    self->ref_pic_set_st_curr_after[i] = 0xff;
    self->ref_pic_set_lt_curr[i] = 0xff;
  }

  dpb_array = gst_h265_dpb_get_pictures_all (dpb);

  GST_LOG_OBJECT (self, "DPB size %d", dpb_array->len);

  for (i = 0; i < dpb_array->len && i < G_N_ELEMENTS (self->ref_pic_list); i++) {
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

    self->ref_pic_list[i].Index7Bits = id;
    self->ref_pic_list[i].AssociatedFlag = other->long_term;
    self->pic_order_cnt_val_list[i] = other->pic_order_cnt;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (self->ref_pic_set_st_curr_before); i++) {
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

    self->ref_pic_set_st_curr_before[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (self->ref_pic_set_st_curr_after); i++) {
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

    self->ref_pic_set_st_curr_after[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (self->ref_pic_set_lt_curr); i++) {
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

    self->ref_pic_set_lt_curr[i] = id;
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

  return TRUE;
}

static GstFlowReturn
gst_d3d11_h265_dec_output_picture (GstH265Decoder * decoder,
    GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *output_buffer = NULL;
  GstFlowReturn ret;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self,
      "Outputting picture %p, poc %d", picture, picture->pic_order_cnt);

  view_buffer = (GstBuffer *) gst_h265_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    return GST_FLOW_ERROR;
  }

  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (self),
      picture->system_frame_number);

  /* if downstream is d3d11 element and forward playback case,
   * expose our decoder view without copy. In case of reverse playback, however,
   * we cannot do that since baseclass will store the decoded buffer
   * up to gop size but our dpb pool cannot be increased */
  if (self->use_d3d11_output &&
      gst_d3d11_decoder_supports_direct_rendering (self->d3d11_decoder) &&
      GST_VIDEO_DECODER (self)->input_segment.rate > 0) {
    GstMemory *mem;

    output_buffer = gst_buffer_ref (view_buffer);
    mem = gst_buffer_peek_memory (output_buffer, 0);
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  } else {
    output_buffer =
        gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));
  }

  if (!output_buffer) {
    GST_ERROR_OBJECT (self, "Couldn't allocate output buffer");
    return GST_FLOW_ERROR;
  }

  if (!frame) {
    GST_WARNING_OBJECT (self,
        "Failed to find codec frame for picture %p", picture);

    GST_BUFFER_PTS (output_buffer) = picture->pts;
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) = GST_CLOCK_TIME_NONE;
  } else {
    frame->output_buffer = output_buffer;
    GST_BUFFER_PTS (output_buffer) = GST_BUFFER_PTS (frame->input_buffer);
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) =
        GST_BUFFER_DURATION (frame->input_buffer);
  }

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder,
          &self->output_state->info,
          GST_VIDEO_INFO_WIDTH (&self->output_state->info),
          GST_VIDEO_INFO_HEIGHT (&self->output_state->info),
          view_buffer, output_buffer)) {
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
  DXVA_Slice_HEVC_Short *slice_data;

  if (self->slice_list->len < 1) {
    GST_WARNING_OBJECT (self, "Nothing to submit");
    return FALSE;
  }

  slice_data = &g_array_index (self->slice_list, DXVA_Slice_HEVC_Short,
      self->slice_list->len - 1);

  /* DXVA2 spec is saying that written bitstream data must be 128 bytes
   * aligned if the bitstream buffer contains end of slice
   * (i.e., wBadSliceChopping == 0 or 2) */
  if (slice_data->wBadSliceChopping == 0 || slice_data->wBadSliceChopping == 2) {
    guint padding =
        MIN (GST_ROUND_UP_128 (self->written_buffer_size) -
        self->written_buffer_size, self->remaining_buffer_size);

    if (padding) {
      GST_TRACE_OBJECT (self,
          "Written bitstream buffer size %u is not 128 bytes aligned, "
          "add padding %u bytes", self->written_buffer_size, padding);
      memset (self->bitstream_buffer_data, 0, padding);
      self->written_buffer_size += padding;
      slice_data->SliceBytesInBuffer += padding;
    }
  }

  GST_TRACE_OBJECT (self, "Getting slice control buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &buffer_size, &buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");
    return FALSE;
  }

  data = buffer;
  for (i = 0; i < self->slice_list->len; i++) {
    slice_data = &g_array_index (self->slice_list, DXVA_Slice_HEVC_Short, i);

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

  if (!self->bad_aligned_bitstream_buffer
      && (self->written_buffer_size & 127) != 0) {
    GST_WARNING_OBJECT (self,
        "Written bitstream buffer size %u is not 128 bytes aligned",
        self->written_buffer_size);
  }

  buffer_desc[buffer_count].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
  buffer_desc[buffer_count].DataOffset = 0;
  buffer_desc[buffer_count].DataSize = self->written_buffer_size;
  buffer_count++;

  ret = gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
      buffer_count, buffer_desc);

  self->written_buffer_size = 0;
  self->bitstream_buffer_data = NULL;
  self->remaining_buffer_size = 0;
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
      for (i = 0; i < pps->num_tile_columns_minus1 &&
          i < G_N_ELEMENTS (params->column_width_minus1); i++)
        COPY_FIELD (column_width_minus1[i]);

      for (i = 0; i < pps->num_tile_rows_minus1 &&
          i < G_N_ELEMENTS (params->row_height_minus1); i++)
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
        slice_header->short_term_ref_pic_sets.NumDeltaPocsOfRefRpsIdx;
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
  GstH265SPS *sps;
  GstH265PPS *pps;
  DXVA_PicParams_HEVC pic_params = { 0, };
  DXVA_Qmatrix_HEVC iq_matrix = { 0, };
  guint d3d11_buffer_size = 0;
  gpointer d3d11_buffer = NULL;
  gint i;
  GstD3D11DecoderOutputView *view;
  GstH265ScalingList *scaling_list = NULL;

  pps = slice->header.pps;
  sps = pps->sps;

  view = gst_d3d11_h265_dec_get_output_view_from_picture (self, picture);

  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view");
    return FALSE;
  }

  gst_d3d11_h265_dec_fill_picture_params (self, &slice->header, &pic_params);

  pic_params.CurrPic.Index7Bits = view->view_id;
  pic_params.IrapPicFlag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);
  pic_params.IdrPicFlag = GST_H265_IS_NAL_TYPE_IDR (slice->nalu.type);
  pic_params.IntraPicFlag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);
  pic_params.CurrPicOrderCntVal = picture->pic_order_cnt;

  memcpy (pic_params.RefPicList, self->ref_pic_list,
      sizeof (pic_params.RefPicList));
  memcpy (pic_params.PicOrderCntValList, self->pic_order_cnt_val_list,
      sizeof (pic_params.PicOrderCntValList));
  memcpy (pic_params.RefPicSetStCurrBefore, self->ref_pic_set_st_curr_before,
      sizeof (pic_params.RefPicSetStCurrBefore));
  memcpy (pic_params.RefPicSetStCurrAfter, self->ref_pic_set_st_curr_after,
      sizeof (pic_params.RefPicSetStCurrAfter));
  memcpy (pic_params.RefPicSetLtCurr, self->ref_pic_set_lt_curr,
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

  if (pps->scaling_list_data_present_flag ||
      (sps->scaling_list_enabled_flag
          && !sps->scaling_list_data_present_flag)) {
    scaling_list = &pps->scaling_list;
  } else if (sps->scaling_list_enabled_flag &&
      sps->scaling_list_data_present_flag) {
    scaling_list = &sps->scaling_list;
  }

  if (scaling_list) {
    self->submit_iq_data = TRUE;

    memcpy (iq_matrix.ucScalingLists0, scaling_list->scaling_lists_4x4,
        sizeof (iq_matrix.ucScalingLists0));
    memcpy (iq_matrix.ucScalingLists1, scaling_list->scaling_lists_8x8,
        sizeof (iq_matrix.ucScalingLists1));
    memcpy (iq_matrix.ucScalingLists2, scaling_list->scaling_lists_16x16,
        sizeof (iq_matrix.ucScalingLists2));
    memcpy (iq_matrix.ucScalingLists3, scaling_list->scaling_lists_32x32,
        sizeof (iq_matrix.ucScalingLists3));

    for (i = 0; i < 6; i++)
      iq_matrix.ucScalingListDCCoefSizeID2[i] =
          scaling_list->scaling_list_dc_coef_minus8_16x16[i] + 8;

    for (i = 0; i < 2; i++)
      iq_matrix.ucScalingListDCCoefSizeID3[i] =
          scaling_list->scaling_list_dc_coef_minus8_32x32[i] + 8;

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

      if (self->remaining_buffer_size < to_write && self->slice_list->len > 0) {
        if (!gst_d3d11_h265_dec_submit_slice_data (self)) {
          GST_ERROR_OBJECT (self, "Failed to submit bitstream buffers");
          return FALSE;
        }

        if (!gst_d3d11_h265_dec_get_bitstream_buffer (self)) {
          GST_ERROR_OBJECT (self, "Failed to get bitstream buffer");
          return FALSE;
        }
      }

      /* remaining_buffer_size: the size of remaining d3d11 decoder
       *                        bitstream memory allowed to write more
       * written_buffer_size: the size of written bytes to this d3d11 decoder
       *                      bitstream memory
       * bytes_to_copy: the size of which we would write to d3d11 decoder
       *                bitstream memory in this loop
       */

      bytes_to_copy = to_write;

      if (bytes_to_copy > self->remaining_buffer_size) {
        /* if the size of this slice is larger than the size of remaining d3d11
         * decoder bitstream memory, write the data up to the remaining d3d11
         * decoder bitstream memory size and the rest would be written to the
         * next d3d11 bitstream memory */
        bytes_to_copy = self->remaining_buffer_size;
        is_last = FALSE;
      }

      if (bytes_to_copy >= 3 && is_first) {
        /* normal case */
        self->bitstream_buffer_data[0] = 0;
        self->bitstream_buffer_data[1] = 0;
        self->bitstream_buffer_data[2] = 1;
        memcpy (self->bitstream_buffer_data + 3,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy - 3);
      } else {
        /* when this nal unit date is splitted into two buffer */
        memcpy (self->bitstream_buffer_data,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy);
      }

      /* For wBadSliceChopping value 0 or 1, BSNALunitDataLocation means
       * the offset of the first start code of this slice in this d3d11
       * memory buffer.
       * 1) If this is the first slice of picture, it should be zero
       *    since we write start code at offset 0 (written size before this
       *    slice also must be zero).
       * 2) If this is not the first slice of picture but this is the first
       *    d3d11 bitstream buffer (meaning that one bitstream buffer contains
       *    multiple slices), then this is the written size of buffer
       *    excluding this loop.
       * And for wBadSliceChopping value 2 or 3, this should be zero by spec */
      if (is_first)
        slice_short.BSNALunitDataLocation = self->written_buffer_size;
      else
        slice_short.BSNALunitDataLocation = 0;
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
      self->remaining_buffer_size -= bytes_to_copy;
      self->written_buffer_size += bytes_to_copy;
      self->bitstream_buffer_data += bytes_to_copy;
      is_first = FALSE;
      to_write -= bytes_to_copy;
    }
  }

  return TRUE;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11H265DecResolution;

void
gst_d3d11_h265_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  GUID profile;
  GTypeInfo type_info = {
    sizeof (GstD3D11H265DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_h265_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11H265Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_h265_dec_init,
  };
  static const GUID *main_10_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10;
  static const GUID *main_guid = &GST_GUID_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN;
  /* values were taken from chromium.
   * Note that since chromium does not support hevc decoding, this list is
   * the combination of lists for avc and vp9.
   * See supported_profile_helper.cc */
  GstD3D11H265DecResolution resolutions_to_check[] = {
    {1920, 1088}, {2560, 1440}, {3840, 2160}, {4096, 2160},
    {4096, 2304}, {7680, 4320}, {8192, 4320}, {8192, 8192}
  };
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  gboolean have_main10 = FALSE;
  gboolean have_main = FALSE;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

  have_main10 = gst_d3d11_decoder_get_supported_decoder_profile (decoder,
      &main_10_guid, 1, &profile);
  if (!have_main10) {
    GST_DEBUG_OBJECT (device, "decoder does not support HEVC_VLD_MAIN10");
  } else {
    have_main10 &=
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_P010);
    have_main10 &=
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_NV12);
    if (!have_main10) {
      GST_FIXME_OBJECT (device,
          "device does not support P010 and/or NV12 format");
    }
  }

  have_main = gst_d3d11_decoder_get_supported_decoder_profile (decoder,
      &main_guid, 1, &profile);
  if (!have_main) {
    GST_DEBUG_OBJECT (device, "decoder does not support HEVC_VLD_MAIN");
  } else {
    have_main =
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_NV12);
    if (!have_main) {
      GST_FIXME_OBJECT (device, "device does not support NV12 format");
    }
  }

  if (!have_main10 && !have_main) {
    GST_INFO_OBJECT (device, "device does not support h.265 decoding");
    return;
  }

  if (have_main) {
    profile = *main_guid;
    format = DXGI_FORMAT_NV12;
  } else {
    profile = *main_10_guid;
    format = DXGI_FORMAT_P010;
  }

  for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    if (gst_d3d11_decoder_supports_resolution (decoder, &profile,
            format, resolutions_to_check[i].width,
            resolutions_to_check[i].height)) {
      max_width = resolutions_to_check[i].width;
      max_height = resolutions_to_check[i].height;

      GST_DEBUG_OBJECT (device,
          "device support resolution %dx%d", max_width, max_height);
    } else {
      break;
    }
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (device, "Couldn't query supported resolution");
    return;
  }

  sink_caps = gst_caps_from_string ("video/x-h265, "
      "stream-format=(string) { hev1, hvc1, byte-stream }, "
      "alignment= (string) au, " "framerate = " GST_VIDEO_FPS_RANGE);
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), "
      "framerate = " GST_VIDEO_FPS_RANGE ";"
      "video/x-raw, " "framerate = " GST_VIDEO_FPS_RANGE);

  if (have_main10) {
    /* main10 profile covers main and main10 */
    GValue profile_list = G_VALUE_INIT;
    GValue profile_value = G_VALUE_INIT;
    GValue format_list = G_VALUE_INIT;
    GValue format_value = G_VALUE_INIT;

    g_value_init (&profile_list, GST_TYPE_LIST);

    g_value_init (&profile_value, G_TYPE_STRING);
    g_value_set_string (&profile_value, "main");
    gst_value_list_append_and_take_value (&profile_list, &profile_value);

    g_value_init (&profile_value, G_TYPE_STRING);
    g_value_set_string (&profile_value, "main-10");
    gst_value_list_append_and_take_value (&profile_list, &profile_value);


    g_value_init (&format_list, GST_TYPE_LIST);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "NV12");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "P010_10LE");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    gst_caps_set_value (sink_caps, "profile", &profile_list);
    gst_caps_set_value (src_caps, "format", &format_list);
    g_value_unset (&profile_list);
    g_value_unset (&format_list);
  } else {
    gst_caps_set_simple (sink_caps, "profile", G_TYPE_STRING, "main", NULL);
    gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);
  }

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, sink_caps, src_caps);

  type_name = g_strdup ("GstD3D11H265Dec");
  feature_name = g_strdup ("d3d11h265dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11H265Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11h265device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_H265_DECODER,
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
