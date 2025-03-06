/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-h265ccextractor
 * @title: h265ccextractor
 *
 * Extracts closed caption data from H.265 stream and outputs in display order
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265ccextractor.h"

GST_DEBUG_CATEGORY_STATIC (gst_h265_cc_extractor_debug);
#define GST_CAT_DEFAULT gst_h265_cc_extractor_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, alignment=(string) au, "
        "parsed=(boolean) true"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("closedcaption/x-cea-608,format={ (string) raw, (string) s334-1a}; "
        "closedcaption/x-cea-708,format={ (string) cc_data, (string) cdp }"));

static void gst_h265_cc_extractor_finalize (GObject * object);

static gboolean gst_h265_cc_extractor_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_h265_cc_extractor_negotiate (GstVideoDecoder * decoder);
static gboolean gst_h265_cc_extractor_transform_meta (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstMeta * meta);
static GstFlowReturn
gst_h265_cc_extractor_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_h265_cc_extractor_finish (GstVideoDecoder * decoder);

static GstFlowReturn
gst_h265_cc_extractor_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size);
static GstFlowReturn
gst_h265_cc_extractor_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture);
static GstFlowReturn
gst_h265_cc_extractor_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb);
static GstFlowReturn
gst_h265_cc_extractor_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1);
static GstFlowReturn
gst_h265_cc_extractor_output_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture);

typedef struct _CaptionData
{
  GstVideoCaptionType caption_type;
  GstBuffer *buffer;
} CaptionData;

struct _GstH265CCExtractor
{
  GstH265Decoder parent;

  GstVideoCaptionType caption_type;
  GstVecDeque *cur_data;
  GstVecDeque *out_data;
  gboolean on_eos;
  gint fps_n;
  gint fps_d;
  gboolean need_negotiate;
};

#define gst_h265_cc_extractor_parent_class parent_class
G_DEFINE_TYPE (GstH265CCExtractor, gst_h265_cc_extractor,
    GST_TYPE_H265_DECODER);

GST_ELEMENT_REGISTER_DEFINE (h265ccextractor, "h265ccextractor",
    GST_RANK_NONE, GST_TYPE_H265_CC_EXTRACTOR);

static void
gst_h265_cc_extractor_class_init (GstH265CCExtractorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH265DecoderClass *h265_class = GST_H265_DECODER_CLASS (klass);

  object_class->finalize = gst_h265_cc_extractor_finalize;

  gst_element_class_set_static_metadata (element_class,
      "H.265 Closed Caption Extractor",
      "Codec/Video/Filter",
      "Extract GstVideoCaptionMeta from input H.265 stream",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_set_format);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_negotiate);
  decoder_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_transform_meta);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_handle_frame);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_finish);

  h265_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_new_sequence);
  h265_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_new_picture);
  h265_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_start_picture);
  h265_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_decode_slice);
  h265_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_h265_cc_extractor_output_picture);

  GST_DEBUG_CATEGORY_INIT (gst_h265_cc_extractor_debug, "h265ccextractor",
      0, "h265ccextractor");
}

static void
caption_data_clear_func (CaptionData * data)
{
  data->caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
  gst_clear_buffer (&data->buffer);
}

static GstVecDeque *
caption_data_queue_new (void)
{
  GstVecDeque *array = gst_vec_deque_new_for_struct (sizeof (CaptionData), 2);
  gst_vec_deque_set_clear_func (array,
      (GDestroyNotify) caption_data_clear_func);
  return array;
}

static void
gst_h265_cc_extractor_init (GstH265CCExtractor * self)
{
  self->cur_data = caption_data_queue_new ();
  self->out_data = gst_vec_deque_new_for_struct (sizeof (CaptionData), 2);
  self->caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
  self->fps_n = 0;
  self->fps_d = 1;
}

static void
gst_h265_cc_extractor_finalize (GObject * object)
{
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (object);

  if (self->cur_data)
    gst_vec_deque_free (self->cur_data);

  gst_vec_deque_free (self->out_data);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h265_cc_extractor_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (decoder);
  GstVideoCodecState *out_state;
  GstCaps *caps;
  gboolean ret;

  self->need_negotiate = TRUE;

  /* Assume caption type is cea708 raw which is common cc type
   * embedded in SEI */
  if (self->caption_type == GST_VIDEO_CAPTION_TYPE_UNKNOWN)
    self->caption_type = GST_VIDEO_CAPTION_TYPE_CEA708_RAW;

  /* Create dummy output state. Otherwise decoder baseclass will try to create
   * video caps on GAP event */
  out_state = gst_video_decoder_set_output_state (decoder,
      GST_VIDEO_FORMAT_NV12, state->info.width, state->info.height, NULL);
  caps = gst_video_caption_type_to_caps (self->caption_type);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
      state->info.fps_n, state->info.fps_d, NULL);
  out_state->caps = caps;
  gst_video_codec_state_unref (out_state);

  ret = GST_VIDEO_DECODER_CLASS (parent_class)->set_format (decoder, state);

  gst_video_decoder_negotiate (decoder);

  return ret;
}

static gboolean
gst_h265_cc_extractor_negotiate (GstVideoDecoder * decoder)
{
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (decoder);
  GstCaps *caps;

  if (!self->need_negotiate)
    return TRUE;

  caps = gst_video_caption_type_to_caps (self->caption_type);

  gst_caps_set_simple (caps,
      "framerate", GST_TYPE_FRACTION, self->fps_n, self->fps_d, NULL);

  gst_pad_set_caps (decoder->srcpad, caps);
  gst_caps_unref (caps);

  self->need_negotiate = FALSE;

  return TRUE;
}

static gboolean
gst_h265_cc_extractor_transform_meta (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstMeta * meta)
{
  /* do not copy any meta */
  return FALSE;
}

static GstFlowReturn
gst_h265_cc_extractor_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (decoder);
  GstVideoTimeCodeMeta *tc_meta;
  GstVideoCaptionMeta *cc_meta;
  gpointer iter = NULL;
  GstFlowReturn ret;

  if (self->cur_data)
    gst_vec_deque_clear (self->cur_data);

  tc_meta = gst_buffer_get_video_time_code_meta (frame->input_buffer);

  while ((cc_meta = (GstVideoCaptionMeta *)
          gst_buffer_iterate_meta_filtered (frame->input_buffer, &iter,
              GST_VIDEO_CAPTION_META_API_TYPE))) {
    CaptionData data;
    data.caption_type = cc_meta->caption_type;
    data.buffer = gst_buffer_new_memdup (cc_meta->data, cc_meta->size);
    GST_BUFFER_DTS (data.buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_PTS (data.buffer) = GST_BUFFER_PTS (frame->input_buffer);
    GST_BUFFER_DURATION (data.buffer) =
        GST_BUFFER_DURATION (frame->input_buffer);

    if (tc_meta)
      gst_buffer_add_video_time_code_meta (data.buffer, &tc_meta->tc);

    if (!self->cur_data)
      self->cur_data = caption_data_queue_new ();

    gst_vec_deque_push_tail_struct (self->cur_data, &data);
  }

  GST_DEBUG_OBJECT (self, "Queued captions %" G_GSIZE_FORMAT,
      self->cur_data ? gst_vec_deque_get_length (self->cur_data) : 0);

  ret = GST_VIDEO_DECODER_CLASS (parent_class)->handle_frame (decoder, frame);

  if (self->cur_data)
    gst_vec_deque_clear (self->cur_data);

  return ret;
}

static GstFlowReturn
gst_h265_cc_extractor_finish (GstVideoDecoder * decoder)
{
  GST_VIDEO_DECODER_CLASS (parent_class)->finish (decoder);

  /* baseclass will post error message if there was no output buffer
   * and subclass returns OK. Return flow EOS to avoid the error message */
  return GST_FLOW_EOS;
}

static GstFlowReturn
gst_h265_cc_extractor_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_cc_extractor_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_cc_extractor_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (decoder);
  GstVecDeque *pic_data;

  if (!self->cur_data || !gst_vec_deque_get_length (self->cur_data))
    return GST_FLOW_OK;

  pic_data = gst_h265_picture_get_user_data (picture);
  if (!pic_data) {
    GST_DEBUG_OBJECT (self, "Creating new picture data, caption size: %"
        G_GSIZE_FORMAT, gst_vec_deque_get_length (self->cur_data));
    gst_h265_picture_set_user_data (picture,
        g_steal_pointer (&self->cur_data), (GDestroyNotify) gst_vec_deque_free);
  } else {
    gpointer caption_data;

    GST_DEBUG_OBJECT (self, "Appending %" G_GSIZE_FORMAT
        " caption buffers, prev size: %" G_GSIZE_FORMAT,
        gst_vec_deque_get_length (self->cur_data),
        gst_vec_deque_get_length (pic_data));

    while ((caption_data = gst_vec_deque_pop_head_struct (self->cur_data)))
      gst_vec_deque_push_tail_struct (pic_data, caption_data);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_cc_extractor_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_cc_extractor_output_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture)
{
  GstVideoDecoder *videodec = GST_VIDEO_DECODER (decoder);
  GstH265CCExtractor *self = GST_H265_CC_EXTRACTOR (decoder);
  gint fps_n = 0;
  gint fps_d = 1;
  gboolean updated = FALSE;
  GstCodecPicture *codec_pic = GST_CODEC_PICTURE (picture);
  GstVecDeque *pic_data;
  CaptionData *caption_data = NULL;
  GstBuffer *front_buf = NULL;
  GstClockTime pts, dur;
  GstFlowReturn ret = GST_FLOW_OK;

  pic_data = gst_h265_picture_get_user_data (picture);

  /* Move caption buffer to our temporary storage */
  if (pic_data) {
    while ((caption_data = gst_vec_deque_pop_head_struct (pic_data)))
      gst_vec_deque_push_tail_struct (self->out_data, caption_data);
  }

  fps_n = decoder->input_state->info.fps_n;
  fps_d = decoder->input_state->info.fps_d;

  if (codec_pic->discont_state) {
    fps_n = codec_pic->discont_state->info.fps_n;
    fps_d = codec_pic->discont_state->info.fps_d;
  }

  if (fps_n != self->fps_n || fps_d != self->fps_d) {
    updated = TRUE;
    self->fps_n = fps_n;
    self->fps_d = fps_d;
  }

  GST_DEBUG_OBJECT (self, "picture is holding %" G_GSIZE_FORMAT
      " caption buffers", gst_vec_deque_get_length (self->out_data));

  if (gst_vec_deque_get_length (self->out_data)) {
    caption_data = gst_vec_deque_pop_head_struct (self->out_data);
    front_buf = caption_data->buffer;
    if (caption_data->caption_type != self->caption_type) {
      GST_DEBUG_OBJECT (self, "Caption type changed, need new caps");
      self->caption_type = caption_data->caption_type;
      updated = TRUE;
    }
  }

  if (updated) {
    self->need_negotiate = TRUE;
    gst_video_decoder_negotiate (videodec);
  }

  gst_h265_picture_unref (picture);

  pts = GST_BUFFER_PTS (frame->input_buffer);
  dur = GST_BUFFER_DURATION (frame->input_buffer);

  if (!front_buf) {
    GstEvent *gap;

    GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);
    ret = gst_video_decoder_finish_frame (videodec, frame);
    gap = gst_event_new_gap (pts, dur);
    gst_pad_push_event (videodec->srcpad, gap);
    return ret;
  }

  frame->output_buffer = front_buf;
  ret = gst_video_decoder_finish_frame (videodec, frame);

  /* Drain other caption data */
  while ((caption_data = gst_vec_deque_pop_head_struct (self->out_data))) {
    if (ret == GST_FLOW_OK)
      ret = gst_pad_push (videodec->srcpad, caption_data->buffer);
    else
      gst_buffer_unref (caption_data->buffer);
  }

  return ret;
}
