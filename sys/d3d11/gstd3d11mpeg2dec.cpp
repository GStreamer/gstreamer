/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:element-d3d11mpeg2dec
 * @title: d3d11mpeg2dec
 *
 * A Direct3D11/DXVA based MPEG-2 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/mpeg2/file ! parsebin ! d3d11mpeg2dec ! d3d11videosink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11mpeg2dec.h"

#include <gst/codecs/gstmpeg2decoder.h>
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

/* *INDENT-OFF* */
G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_mpeg2_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_mpeg2_dec_debug

G_END_DECLS
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* reference list 2 + 4 margin */
#define NUM_OUTPUT_VIEW 6

typedef struct _GstD3D11Mpeg2Dec
{
  GstMpeg2Decoder parent;

  GstD3D11Device *device;
  GstD3D11Decoder *d3d11_decoder;

  gint width, height;
  guint width_in_mb, height_in_mb;
  GstVideoFormat out_format;
  GstMpegVideoSequenceHdr seq;
  GstMpegVideoProfile profile;
  gboolean interlaced;

  /* Array of DXVA_SliceInfo  */
  GArray *slice_list;
  gboolean submit_iq_data;

  /* Pointing current bitstream buffer */
  guint written_buffer_size;
  guint remaining_buffer_size;
  guint8 *bitstream_buffer_data;
} GstD3D11Mpeg2Dec;

typedef struct _GstD3D11Mpeg2DecClass
{
  GstMpeg2DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11Mpeg2DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_MPEG2_DEC(object) ((GstD3D11Mpeg2Dec *) (object))
#define GST_D3D11_MPEG2_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Mpeg2DecClass))

static void gst_d3d11_mpeg2_dec_finalize (GObject * object);
static void gst_d3d11_mpeg2_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_mpeg2_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_mpeg2_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_mpeg2_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_mpeg2_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstMpeg2Decoder */
static gboolean gst_d3d11_mpeg2_dec_new_sequence (GstMpeg2Decoder * decoder,
    const GstMpegVideoSequenceHdr * seq,
    const GstMpegVideoSequenceExt * seq_ext,
    const GstMpegVideoSequenceDisplayExt * seq_display_ext,
    const GstMpegVideoSequenceScalableExt * seq_scalable_ext);
static gboolean gst_d3d11_mpeg2_dec_new_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture);
static gboolean gst_d3d11_mpeg2_dec_new_field_picture (GstMpeg2Decoder *
    decoder, const GstMpeg2Picture * first_field,
    GstMpeg2Picture * second_field);
static gboolean gst_d3d11_mpeg2_dec_start_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice,
    GstMpeg2Picture * prev_picture, GstMpeg2Picture * next_picture);
static gboolean gst_d3d11_mpeg2_dec_decode_slice (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice);
static gboolean gst_d3d11_mpeg2_dec_end_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture);
static GstFlowReturn gst_d3d11_mpeg2_dec_output_picture (GstMpeg2Decoder *
    decoder, GstVideoCodecFrame * frame, GstMpeg2Picture * picture);

static void
gst_d3d11_mpeg2_dec_class_init (GstD3D11Mpeg2DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstMpeg2DecoderClass *mpeg2decoder_class = GST_MPEG2_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_mpeg2_dec_get_property;
  gobject_class->finalize = gst_d3d11_mpeg2_dec_finalize;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, cdata->adapter,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  klass->adapter = cdata->adapter;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_set_context);

  long_name =
      g_strdup_printf ("Direct3D11/DXVA MPEG2 %s Decoder", cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware", "A Direct3D11/DXVA MPEG2 video decoder",
      "Seungha Yang <seungha@centricular.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_src_query);
  decoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_sink_event);

  mpeg2decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_sequence);
  mpeg2decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_picture);
  mpeg2decoder_class->new_field_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_field_picture);
  mpeg2decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_start_picture);
  mpeg2decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_decode_slice);
  mpeg2decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_end_picture);
  mpeg2decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_output_picture);
}

static void
gst_d3d11_mpeg2_dec_init (GstD3D11Mpeg2Dec * self)
{
  self->slice_list = g_array_new (FALSE, TRUE, sizeof (DXVA_SliceInfo));
  self->profile = GST_MPEG_VIDEO_PROFILE_MAIN;
}

static void
gst_d3d11_mpeg2_dec_finalize (GObject * object)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (object);

  g_array_unref (self->slice_list);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_mpeg2_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (object);

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
gst_d3d11_mpeg2_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (element);
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_mpeg2_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (self);

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
gst_d3d11_mpeg2_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_mpeg2_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

  if (!gst_d3d11_decoder_negotiate (self->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_mpeg2_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (self->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_mpeg2_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

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
gst_d3d11_mpeg2_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (self->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (self->d3d11_decoder, decoder, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (self->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (self->d3d11_decoder, decoder, FALSE);
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static gboolean
gst_d3d11_mpeg2_dec_new_sequence (GstMpeg2Decoder * decoder,
    const GstMpegVideoSequenceHdr * seq,
    const GstMpegVideoSequenceExt * seq_ext,
    const GstMpegVideoSequenceDisplayExt * seq_display_ext,
    const GstMpegVideoSequenceScalableExt * seq_scalable_ext)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  gboolean interlaced;
  gboolean modified = FALSE;
  gint width, height;
  GstMpegVideoProfile mpeg_profile;

  GST_LOG_OBJECT (self, "new sequence");

  interlaced = seq_ext ? !seq_ext->progressive : FALSE;
  if (self->interlaced != interlaced) {
    GST_INFO_OBJECT (self, "interlaced sequence change");
    self->interlaced = interlaced;
    modified = TRUE;
  }

  width = seq->width;
  height = seq->height;
  if (seq_ext) {
    width = (width & 0x0fff) | ((guint32) seq_ext->horiz_size_ext << 12);
    height = (height & 0x0fff) | ((guint32) seq_ext->vert_size_ext << 12);
  }

  if (self->width != width || self->height != height) {
    GST_INFO_OBJECT (self, "resolution change %dx%d -> %dx%d",
        self->width, self->height, width, height);
    self->width = width;
    self->height = height;
    self->width_in_mb = GST_ROUND_UP_16 (width) >> 4;
    self->height_in_mb = GST_ROUND_UP_16 (height) >> 4;
    modified = TRUE;
  }

  mpeg_profile = GST_MPEG_VIDEO_PROFILE_MAIN;
  if (seq_ext)
    mpeg_profile = (GstMpegVideoProfile) seq_ext->profile;

  if (mpeg_profile != GST_MPEG_VIDEO_PROFILE_MAIN &&
      mpeg_profile != GST_MPEG_VIDEO_PROFILE_SIMPLE) {
    GST_ERROR_OBJECT (self, "Cannot support profile %d", mpeg_profile);
    return FALSE;
  }

  if (self->profile != mpeg_profile) {
    GST_INFO_OBJECT (self, "Profile change %d -> %d",
        self->profile, mpeg_profile);
    self->profile = mpeg_profile;
    modified = TRUE;
  }

  if (modified || !gst_d3d11_decoder_is_configured (self->d3d11_decoder)) {
    GstVideoInfo info;

    /* FIXME: support I420 */
    self->out_format = GST_VIDEO_FORMAT_NV12;

    gst_video_info_set_format (&info,
        self->out_format, self->width, self->height);
    if (self->interlaced)
      GST_VIDEO_INFO_INTERLACE_MODE (&info) = GST_VIDEO_INTERLACE_MODE_MIXED;

    if (!gst_d3d11_decoder_configure (self->d3d11_decoder,
            GST_D3D11_CODEC_MPEG2, decoder->input_state, &info,
            self->width, self->height, NUM_OUTPUT_VIEW)) {
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
gst_d3d11_mpeg2_dec_new_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (self->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_mpeg2_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New MPEG2 picture %p", picture);

  return TRUE;
}

static gboolean
gst_d3d11_mpeg2_dec_new_field_picture (GstMpeg2Decoder * decoder,
    const GstMpeg2Picture * first_field, GstMpeg2Picture * second_field)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstBuffer *view_buffer;

  view_buffer = (GstBuffer *)
      gst_mpeg2_picture_get_user_data ((GstMpeg2Picture *) first_field);

  if (!view_buffer) {
    GST_WARNING_OBJECT (self, "First picture does not have output view buffer");
    return TRUE;
  }

  GST_LOG_OBJECT (self, "New field picture with buffer %" GST_PTR_FORMAT,
      view_buffer);

  gst_mpeg2_picture_set_user_data (second_field,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return TRUE;
}

static gboolean
gst_d3d11_mpeg2_dec_get_bitstream_buffer (GstD3D11Mpeg2Dec * self)
{
  GST_TRACE_OBJECT (self, "Getting bitstream buffer");

  self->written_buffer_size = 0;
  self->remaining_buffer_size = 0;
  self->bitstream_buffer_data = NULL;

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &self->remaining_buffer_size,
          (gpointer *) & self->bitstream_buffer_data)) {
    GST_ERROR_OBJECT (self, "Faild to get bitstream buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Got bitstream buffer %p with size %d",
      self->bitstream_buffer_data, self->remaining_buffer_size);
  self->written_buffer_size = 0;

  return TRUE;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_mpeg2_dec_get_output_view_from_picture (GstD3D11Mpeg2Dec * self,
    GstMpeg2Picture * picture, guint8 * view_id)
{
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  if (!picture)
    return NULL;

  view_buffer = (GstBuffer *) gst_mpeg2_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer, view_id);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static inline WORD
_pack_f_codes (guint8 f_code[2][2])
{
  return (((WORD) f_code[0][0] << 12)
      | ((WORD) f_code[0][1] << 8)
      | ((WORD) f_code[1][0] << 4)
      | (f_code[1][1]));
}

static inline WORD
_pack_pce_elements (GstMpeg2Slice * slice)
{
  return (((WORD) slice->pic_ext->intra_dc_precision << 14)
      | ((WORD) slice->pic_ext->picture_structure << 12)
      | ((WORD) slice->pic_ext->top_field_first << 11)
      | ((WORD) slice->pic_ext->frame_pred_frame_dct << 10)
      | ((WORD) slice->pic_ext->concealment_motion_vectors << 9)
      | ((WORD) slice->pic_ext->q_scale_type << 8)
      | ((WORD) slice->pic_ext->intra_vlc_format << 7)
      | ((WORD) slice->pic_ext->alternate_scan << 6)
      | ((WORD) slice->pic_ext->repeat_first_field << 5)
      | ((WORD) slice->pic_ext->chroma_420_type << 4)
      | ((WORD) slice->pic_ext->progressive_frame << 3));
}

static gboolean
gst_d3d11_mpeg2_dec_start_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice,
    GstMpeg2Picture * prev_picture, GstMpeg2Picture * next_picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  ID3D11VideoDecoderOutputView *view;
  ID3D11VideoDecoderOutputView *other_view;
  guint8 view_id = 0xff;
  guint8 other_view_id = 0xff;
  DXVA_PictureParameters pic_params = { 0, };
  DXVA_QmatrixData iq_matrix = { 0, };
  guint d3d11_buffer_size = 0;
  gpointer d3d11_buffer = NULL;
  gboolean is_field =
      picture->structure != GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;

  view = gst_d3d11_mpeg2_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");
  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  /* Fill DXVA_PictureParameters */
  pic_params.wDecodedPictureIndex = view_id;
  pic_params.wForwardRefPictureIndex = 0xffff;
  pic_params.wBackwardRefPictureIndex = 0xffff;

  switch (picture->type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:{
      if (next_picture) {
        other_view =
            gst_d3d11_mpeg2_dec_get_output_view_from_picture (self,
            next_picture, &other_view_id);
        if (other_view)
          pic_params.wBackwardRefPictureIndex = other_view_id;
      }
    }
      /* fall-through */
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:{
      if (prev_picture) {
        other_view =
            gst_d3d11_mpeg2_dec_get_output_view_from_picture (self,
            prev_picture, &other_view_id);
        if (other_view)
          pic_params.wForwardRefPictureIndex = other_view_id;
      }
    }
    default:
      break;
  }

  /* *INDENT-OFF* */
  pic_params.wPicWidthInMBminus1 = self->width_in_mb - 1;
  pic_params.wPicHeightInMBminus1 = (self->height_in_mb >> is_field) - 1;
  pic_params.bMacroblockWidthMinus1 = 15;
  pic_params.bMacroblockHeightMinus1 = 15;
  pic_params.bBlockWidthMinus1 = 7;
  pic_params.bBlockHeightMinus1 = 7;
  pic_params.bBPPminus1 = 7;
  pic_params.bPicStructure = (BYTE) picture->structure;
  pic_params.bSecondField = is_field && ! !picture->first_field;
  pic_params.bPicIntra = picture->type == GST_MPEG_VIDEO_PICTURE_TYPE_I;
  pic_params.bPicBackwardPrediction =
      picture->type == GST_MPEG_VIDEO_PICTURE_TYPE_B;
  /* FIXME: 1 -> 4:2:0, 2 -> 4:2:2, 3 -> 4:4:4 */
  pic_params.bChromaFormat = 1;
  pic_params.bPicScanFixed = 1;
  pic_params.bPicScanMethod = slice->pic_ext->alternate_scan;
  pic_params.wBitstreamFcodes = _pack_f_codes (slice->pic_ext->f_code);
  pic_params.wBitstreamPCEelements = _pack_pce_elements (slice);
  /* *INDENT-ON* */

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

  /* Fill DXVA_QmatrixData */
  if (slice->quant_matrix &&
      /* The value in bNewQmatrix[0] and bNewQmatrix[1] must not both be zero.
       * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/dxva/ns-dxva-_dxva_qmatrixdata
       */
      (slice->quant_matrix->load_intra_quantiser_matrix ||
          slice->quant_matrix->load_non_intra_quantiser_matrix)) {
    GstMpegVideoQuantMatrixExt *quant_matrix = slice->quant_matrix;
    self->submit_iq_data = TRUE;

    if (quant_matrix->load_intra_quantiser_matrix) {
      iq_matrix.bNewQmatrix[0] = 1;
      memcpy (iq_matrix.Qmatrix[0], quant_matrix->intra_quantiser_matrix,
          sizeof (quant_matrix->intra_quantiser_matrix));
    }

    if (quant_matrix->load_non_intra_quantiser_matrix) {
      iq_matrix.bNewQmatrix[1] = 1;
      memcpy (iq_matrix.Qmatrix[1], quant_matrix->non_intra_quantiser_matrix,
          sizeof (quant_matrix->non_intra_quantiser_matrix));
    }

    if (quant_matrix->load_chroma_intra_quantiser_matrix) {
      iq_matrix.bNewQmatrix[2] = 1;
      memcpy (iq_matrix.Qmatrix[2], quant_matrix->chroma_intra_quantiser_matrix,
          sizeof (quant_matrix->chroma_intra_quantiser_matrix));
    }

    if (quant_matrix->load_chroma_non_intra_quantiser_matrix) {
      iq_matrix.bNewQmatrix[3] = 1;
      memcpy (iq_matrix.Qmatrix[3],
          quant_matrix->chroma_non_intra_quantiser_matrix,
          sizeof (quant_matrix->chroma_non_intra_quantiser_matrix));
    }

    GST_TRACE_OBJECT (self, "Getting inverse quantization matrix buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX,
            &d3d11_buffer_size, &d3d11_buffer)) {
      GST_ERROR_OBJECT (self,
          "Failed to get decoder buffer for inv. quantization matrix");
      return FALSE;
    }

    memcpy (d3d11_buffer, &iq_matrix, sizeof (DXVA_QmatrixData));

    GST_TRACE_OBJECT (self, "Release inverse quantization matrix buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX)) {
      GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
      return FALSE;
    }
  } else {
    self->submit_iq_data = FALSE;
  }

  g_array_set_size (self->slice_list, 0);

  return gst_d3d11_mpeg2_dec_get_bitstream_buffer (self);
}

static gboolean
gst_d3d11_mpeg2_dec_submit_slice_data (GstD3D11Mpeg2Dec * self,
    GstMpeg2Picture * picture)
{
  guint buffer_size;
  gpointer buffer;
  guint8 *data;
  gsize offset = 0;
  guint i;
  D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[4];
  gboolean ret;
  guint buffer_count = 0;
  DXVA_SliceInfo *slice_data;
  gboolean is_field =
      picture->structure != GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;
  guint mb_count = self->width_in_mb * (self->height_in_mb >> is_field);

  if (self->slice_list->len < 1) {
    GST_WARNING_OBJECT (self, "Nothing to submit");
    return FALSE;
  }

  memset (buffer_desc, 0, sizeof (buffer_desc));

  GST_TRACE_OBJECT (self, "Getting slice control buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &buffer_size, &buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");
    return FALSE;
  }

  data = (guint8 *) buffer;
  for (i = 0; i < self->slice_list->len; i++) {
    slice_data = &g_array_index (self->slice_list, DXVA_SliceInfo, i);

    /* Update the number of MBs per slice */
    if (i == self->slice_list->len - 1) {
      slice_data->wNumberMBsInSlice = mb_count - slice_data->wNumberMBsInSlice;
    } else {
      DXVA_SliceInfo *next =
          &g_array_index (self->slice_list, DXVA_SliceInfo, i + 1);
      slice_data->wNumberMBsInSlice =
          next->wNumberMBsInSlice - slice_data->wNumberMBsInSlice;
    }

    memcpy (data + offset, slice_data, sizeof (DXVA_SliceInfo));
    offset += sizeof (DXVA_SliceInfo);
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
  buffer_desc[buffer_count].DataSize = sizeof (DXVA_PictureParameters);
  buffer_count++;

  if (self->submit_iq_data) {
    buffer_desc[buffer_count].BufferType =
        D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
    buffer_desc[buffer_count].DataOffset = 0;
    buffer_desc[buffer_count].DataSize = sizeof (DXVA_QmatrixData);
    buffer_count++;
  }

  buffer_desc[buffer_count].BufferType =
      D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
  buffer_desc[buffer_count].DataOffset = 0;
  buffer_desc[buffer_count].DataSize =
      sizeof (DXVA_SliceInfo) * self->slice_list->len;
  buffer_count++;

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
gst_d3d11_mpeg2_dec_decode_slice (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstMpegVideoSliceHdr *header = &slice->header;
  GstMpegVideoPacket *packet = &slice->packet;
  /* including start code 4 bytes */
  guint to_write = packet->size + 4;
  DXVA_SliceInfo slice_info = { 0, };

  g_assert (packet->offset >= 4);

  /* FIXME: DXVA wants to know the number of MBs per slice
   * (not sure whether it's actually used by driver). But in case that
   * one slice is splitted into two bitstream buffer, it's almost impossible
   * to know the number of MBs per splitted bitstream buffer.
   * So, we will not support too large bitstream buffer which requires multiple
   * hardware bitstream buffer at this moment.
   */
  if (self->remaining_buffer_size < to_write) {
    /* Submit slice data we have so that release acquired bitstream buffers */
    if (self->bitstream_buffer_data)
      gst_d3d11_mpeg2_dec_submit_slice_data (self, picture);
    self->bitstream_buffer_data = 0;

    GST_ERROR_OBJECT (self, "Slice data is too large");

    return FALSE;
  }

  slice_info.wHorizontalPosition = header->mb_column;
  slice_info.wVerticalPosition = header->mb_row;
  slice_info.dwSliceBitsInBuffer = 8 * to_write;
  slice_info.dwSliceDataLocation = self->written_buffer_size;
  /* XXX: We don't have information about the number of MBs in this slice.
   * Just store offset here, and actual number will be calculated later */
  slice_info.wNumberMBsInSlice =
      (header->mb_row * self->width_in_mb) + header->mb_column;
  slice_info.wQuantizerScaleCode = header->quantiser_scale_code;
  slice_info.wMBbitOffset = header->header_size + 32;
  memcpy (self->bitstream_buffer_data, packet->data + packet->offset - 4,
      to_write);

  g_array_append_val (self->slice_list, slice_info);
  self->remaining_buffer_size -= to_write;
  self->written_buffer_size += to_write;
  self->bitstream_buffer_data += to_write;

  return TRUE;
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_output_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_mpeg2_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder, vdec,
          self->width, self->height, view_buffer, &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  if (picture->buffer_flags != 0) {
    gboolean interlaced =
        (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) != 0;
    gboolean tff = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0;

    GST_TRACE_OBJECT (self,
        "apply buffer flags 0x%x (interlaced %d, top-field-first %d)",
        picture->buffer_flags, interlaced, tff);
    GST_BUFFER_FLAG_SET (frame->output_buffer, picture->buffer_flags);
  }

  gst_mpeg2_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_mpeg2_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static gboolean
gst_d3d11_mpeg2_dec_end_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);

  if (!gst_d3d11_mpeg2_dec_submit_slice_data (self, picture)) {
    GST_ERROR_OBJECT (self, "Failed to submit slice data");
    return FALSE;
  }

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11Mpeg2DecResolution;

void
gst_d3d11_mpeg2_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  GTypeInfo type_info = {
    sizeof (GstD3D11Mpeg2DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_mpeg2_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11Mpeg2Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_mpeg2_dec_init,
  };
  const GUID *supported_profile = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;

  if (!gst_d3d11_decoder_get_supported_decoder_profile (decoder,
          GST_D3D11_CODEC_MPEG2, GST_VIDEO_FORMAT_NV12, &supported_profile)) {
    GST_INFO_OBJECT (device, "device does not support MPEG-2 video decoding");
    return;
  }

  sink_caps = gst_caps_from_string ("video/mpeg, "
      "mpegversion = (int)2, systemstream = (boolean) false, "
      "profile = (string) { main, simple }");
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "); video/x-raw");

  /* NOTE: We are supporting only 4:2:0, main or simple profiles */
  gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);

  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 1, 1920,
      "height", GST_TYPE_INT_RANGE, 1, 1920, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 1, 1920,
      "height", GST_TYPE_INT_RANGE, 1, 1920, NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, sink_caps, src_caps);

  type_name = g_strdup ("GstD3D11Mpeg2Dec");
  feature_name = g_strdup ("d3d11mpeg2dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11Mpeg2Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11mpeg2device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_MPEG2_DECODER,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
