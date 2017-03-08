/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (c) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
 *      Author : Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (c) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 * SECTION:element-daalaenc
 * @title: daalaenc
 * @see_also: daaladec, oggmux
 *
 * This element encodes raw video into a Daala stream.
 * <ulink url="http://www.xiph.org/daala/">Daala</ulink> is a royalty-free
 * video codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 -v videotestsrc num-buffers=1000 ! daalaenc ! oggmux ! filesink location=videotestsrc.ogg
 * ]| This example pipeline will encode a test video source to daala muxed in an
 * ogg container. Refer to the daaladec documentation to decode the create
 * stream.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>             /* free */

#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstdaalaenc.h"

#define GST_CAT_DEFAULT daalaenc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_QUANT,
  PROP_KEYFRAME_RATE
};

#define DEFAULT_QUANT 10
#define DEFAULT_KEYFRAME_RATE  1

static GstStaticPadTemplate daala_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, Y444 }"))
    );

static GstStaticPadTemplate daala_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-daala, "
        "framerate = (fraction) [1/MAX, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstCaps *daala_supported_caps = NULL;

#define gst_daala_enc_parent_class parent_class
G_DEFINE_TYPE (GstDaalaEnc, gst_daala_enc, GST_TYPE_VIDEO_ENCODER);

static gboolean daala_enc_start (GstVideoEncoder * enc);
static gboolean daala_enc_stop (GstVideoEncoder * enc);
static gboolean daala_enc_flush (GstVideoEncoder * enc);
static gboolean daala_enc_set_format (GstVideoEncoder * enc,
    GstVideoCodecState * state);
static GstFlowReturn daala_enc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame);
static GstFlowReturn daala_enc_pre_push (GstVideoEncoder * benc,
    GstVideoCodecFrame * frame);
static GstFlowReturn daala_enc_finish (GstVideoEncoder * enc);
static gboolean daala_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_daala_enc_sink_query (GstVideoEncoder * encoder,
    GstQuery * query);

static GstCaps *daala_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter);
static void daala_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void daala_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void daala_enc_finalize (GObject * object);

static char *
daala_enc_get_supported_formats (void)
{
  daala_enc_ctx *encoder;
  daala_info info;
  struct
  {
    GstVideoFormat fmt;
    gint planes;
    gint xdec[3], ydec[3];
  } formats[] = {
    {
      GST_VIDEO_FORMAT_Y444, 3, {
      0, 0, 0}, {
    0, 0, 0}}, {
      GST_VIDEO_FORMAT_I420, 3, {
      0, 1, 1}, {
    0, 1, 1}}
  };
  GString *string = NULL;
  guint i;

  daala_info_init (&info);
  info.pic_width = 16;
  info.pic_height = 16;
  info.timebase_numerator = 25;
  info.timebase_denominator = 1;
  info.frame_duration = 1;
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    gint j;

    info.nplanes = formats[i].planes;
    for (j = 0; j < formats[i].planes; j++) {
      info.plane_info[j].xdec = formats[i].xdec[j];
      info.plane_info[j].ydec = formats[i].ydec[j];
    }

    encoder = daala_encode_create (&info);
    if (encoder == NULL)
      continue;

    GST_LOG ("format %s is supported",
        gst_video_format_to_string (formats[i].fmt));
    daala_encode_free (encoder);

    if (string == NULL) {
      string = g_string_new (gst_video_format_to_string (formats[i].fmt));
    } else {
      g_string_append (string, ", ");
      g_string_append (string, gst_video_format_to_string (formats[i].fmt));
    }
  }
  daala_info_clear (&info);

  return string == NULL ? NULL : g_string_free (string, FALSE);
}

static void
initialize_supported_caps (void)
{
  char *supported_formats, *caps_string;

  supported_formats = daala_enc_get_supported_formats ();
  if (!supported_formats) {
    GST_WARNING ("no supported formats found. Encoder disabled?");
    daala_supported_caps = gst_caps_new_empty ();
  }

  caps_string = g_strdup_printf ("video/x-raw, "
      "format = (string) { %s }, "
      "framerate = (fraction) [1/MAX, MAX], "
      "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]",
      supported_formats);
  daala_supported_caps = gst_caps_from_string (caps_string);
  g_free (caps_string);
  g_free (supported_formats);
  GST_DEBUG ("Supported caps: %" GST_PTR_FORMAT, daala_supported_caps);
}

static void
gst_daala_enc_class_init (GstDaalaEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstVideoEncoderClass *gstvideo_encoder_class =
      GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (daalaenc_debug, "daalaenc", 0, "Daala encoder");

  gobject_class->set_property = daala_enc_set_property;
  gobject_class->get_property = daala_enc_get_property;
  gobject_class->finalize = daala_enc_finalize;

  g_object_class_install_property (gobject_class, PROP_QUANT,
      g_param_spec_int ("quant", "Quant", "Quant",
          0, 511, DEFAULT_QUANT,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KEYFRAME_RATE,
      g_param_spec_int ("keyframe-rate", "Keyframe Rate", "Keyframe Rate",
          1, G_MAXINT, DEFAULT_KEYFRAME_RATE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class,
      &daala_enc_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &daala_enc_sink_factory);
  gst_element_class_set_static_metadata (element_class, "Daala video encoder",
      "Codec/Encoder/Video", "Encode raw YUV video to a Daala stream",
      "Sebastian Dröge <slomo@circular-chaos.org>");

  gstvideo_encoder_class->start = GST_DEBUG_FUNCPTR (daala_enc_start);
  gstvideo_encoder_class->stop = GST_DEBUG_FUNCPTR (daala_enc_stop);
  gstvideo_encoder_class->flush = GST_DEBUG_FUNCPTR (daala_enc_flush);
  gstvideo_encoder_class->set_format = GST_DEBUG_FUNCPTR (daala_enc_set_format);
  gstvideo_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (daala_enc_handle_frame);
  gstvideo_encoder_class->pre_push = GST_DEBUG_FUNCPTR (daala_enc_pre_push);
  gstvideo_encoder_class->finish = GST_DEBUG_FUNCPTR (daala_enc_finish);
  gstvideo_encoder_class->getcaps = GST_DEBUG_FUNCPTR (daala_enc_getcaps);
  gstvideo_encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_daala_enc_sink_query);
  gstvideo_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (daala_enc_propose_allocation);

  initialize_supported_caps ();
}

static void
gst_daala_enc_init (GstDaalaEnc * enc)
{
  enc->quant = DEFAULT_QUANT;
  enc->keyframe_rate = DEFAULT_KEYFRAME_RATE;
}

static void
daala_enc_finalize (GObject * object)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (object);

  GST_DEBUG_OBJECT (enc, "Finalizing");
  if (enc->encoder)
    daala_encode_free (enc->encoder);
  daala_comment_clear (&enc->comment);
  daala_info_clear (&enc->info);

  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
daala_enc_flush (GstVideoEncoder * benc)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (benc);
  int quant;

  GST_OBJECT_LOCK (enc);
  quant = enc->quant;
  enc->quant_changed = FALSE;
  enc->info.keyframe_rate = enc->keyframe_rate;
  enc->keyframe_rate_changed = FALSE;
  GST_OBJECT_UNLOCK (enc);

  if (enc->encoder)
    daala_encode_free (enc->encoder);
  enc->encoder = daala_encode_create (&enc->info);

  daala_encode_ctl (enc->encoder, OD_SET_QUANT, &quant, sizeof (int));

  return TRUE;
}

static gboolean
daala_enc_start (GstVideoEncoder * benc)
{
  GstDaalaEnc *enc;

  GST_DEBUG_OBJECT (benc, "start: init daala");
  enc = GST_DAALA_ENC (benc);

  enc->packetno = 0;
  enc->initialised = FALSE;

  return TRUE;
}

static gboolean
daala_enc_stop (GstVideoEncoder * benc)
{
  GstDaalaEnc *enc;

  GST_DEBUG_OBJECT (benc, "stop: clearing daala state");
  enc = GST_DAALA_ENC (benc);

  if (enc->encoder) {
    daala_encode_free (enc->encoder);
    enc->encoder = NULL;
  }
  daala_comment_clear (&enc->comment);
  daala_info_clear (&enc->info);

  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);
  enc->input_state = NULL;

  enc->initialised = FALSE;

  return TRUE;
}

static gboolean
gst_daala_enc_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:{
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);

      gst_query_set_accept_caps_result (query,
          gst_caps_is_subset (caps, daala_supported_caps));
      res = TRUE;
    }
      break;
    default:
      res = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return res;
}

static GstCaps *
daala_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  return gst_video_encoder_proxy_getcaps (encoder, daala_supported_caps,
      filter);
}

static gboolean
daala_enc_set_format (GstVideoEncoder * benc, GstVideoCodecState * state)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (benc);
  GstVideoInfo *info = &state->info;

  daala_info_clear (&enc->info);
  daala_info_init (&enc->info);
  enc->info.pic_width = GST_VIDEO_INFO_WIDTH (info);
  enc->info.pic_height = GST_VIDEO_INFO_HEIGHT (info);
  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
      enc->info.nplanes = 3;
      enc->info.plane_info[0].xdec = 0;
      enc->info.plane_info[0].ydec = 0;
      enc->info.plane_info[1].xdec = 1;
      enc->info.plane_info[1].ydec = 1;
      enc->info.plane_info[2].xdec = 1;
      enc->info.plane_info[2].ydec = 1;
      break;
    case GST_VIDEO_FORMAT_Y444:
      enc->info.nplanes = 3;
      enc->info.plane_info[0].xdec = 0;
      enc->info.plane_info[0].ydec = 0;
      enc->info.plane_info[1].xdec = 0;
      enc->info.plane_info[1].ydec = 0;
      enc->info.plane_info[2].xdec = 0;
      enc->info.plane_info[2].ydec = 0;
      break;
    default:
      g_assert_not_reached ();
  }

  enc->info.timebase_numerator = GST_VIDEO_INFO_FPS_N (info);
  enc->info.timebase_denominator = GST_VIDEO_INFO_FPS_D (info);
  enc->info.frame_duration = 1;
  enc->info.pixel_aspect_numerator = GST_VIDEO_INFO_PAR_N (info);
  enc->info.pixel_aspect_denominator = GST_VIDEO_INFO_PAR_D (info);

  /* Save input state */
  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);
  enc->input_state = gst_video_codec_state_ref (state);

  daala_enc_flush (benc);
  enc->initialised = TRUE;

  return TRUE;
}

/* this function does a straight granulepos -> timestamp conversion */
static GstClockTime
granulepos_to_timestamp (GstDaalaEnc * enc, ogg_int64_t granulepos)
{
  guint64 iframe, pframe;
  int shift = enc->info.keyframe_granule_shift;

  if (granulepos < 0)
    return GST_CLOCK_TIME_NONE;

  iframe = granulepos >> shift;
  pframe = granulepos - (iframe << shift);

  /* num and den are 32 bit, so we can safely multiply with GST_SECOND */
  return gst_util_uint64_scale ((guint64) (iframe + pframe),
      GST_SECOND * enc->info.timebase_denominator,
      enc->info.timebase_numerator);
}

static GstFlowReturn
daala_enc_pre_push (GstVideoEncoder * benc, GstVideoCodecFrame * frame)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (benc);
  guint64 pfn;

  /* see ext/ogg/README; OFFSET_END takes "our" granulepos, OFFSET its
   * time representation */
  /* granulepos from sync frame */
  pfn = frame->presentation_frame_number - frame->distance_from_sync;
  /* correct to correspond to linear running time */
  pfn -= enc->pfn_offset;
  pfn += enc->granulepos_offset + 1;
  /* granulepos */
  GST_BUFFER_OFFSET_END (frame->output_buffer) =
      (pfn << enc->info.keyframe_granule_shift) + frame->distance_from_sync;
  GST_BUFFER_OFFSET (frame->output_buffer) = granulepos_to_timestamp (enc,
      GST_BUFFER_OFFSET_END (frame->output_buffer));

  return GST_FLOW_OK;
}

static GstFlowReturn
daala_push_packet (GstDaalaEnc * enc, ogg_packet * packet)
{
  GstVideoEncoder *benc;
  GstFlowReturn ret;
  GstVideoCodecFrame *frame;

  benc = GST_VIDEO_ENCODER (enc);

  frame = gst_video_encoder_get_oldest_frame (benc);
  if (gst_video_encoder_allocate_output_frame (benc, frame,
          packet->bytes) != GST_FLOW_OK) {
    GST_WARNING_OBJECT (enc, "Could not allocate buffer");
    gst_video_codec_frame_unref (frame);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (packet->bytes > 0)
    gst_buffer_fill (frame->output_buffer, 0, packet->packet, packet->bytes);

  /* the second most significant bit of the first data byte is cleared
   * for keyframes */
  if (packet->bytes > 0 && (packet->packet[0] & 0x40) == 0) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  } else {
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
  }
  enc->packetno++;

  ret = gst_video_encoder_finish_frame (benc, frame);

done:
  return ret;
}

static GstCaps *
daala_set_header_on_caps (GstCaps * caps, GList * buffers)
{
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstBuffer *buffer;
  GList *walk;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  /* put copies of the buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);

  for (walk = buffers; walk; walk = walk->next) {
    buffer = walk->data;
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, buffer);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  }

  gst_structure_take_value (structure, "streamheader", &array);

  return caps;
}

static void
daala_enc_init_buffer (GstDaalaEnc * enc, od_img * buf, GstVideoFrame * frame)
{
  guint i;

  buf->nplanes = 3;
  buf->width = GST_VIDEO_FRAME_WIDTH (frame);
  buf->height = GST_VIDEO_FRAME_HEIGHT (frame);

  for (i = 0; i < 3; i++) {
    buf->planes[i].data = GST_VIDEO_FRAME_COMP_DATA (frame, i);
    buf->planes[i].xdec = enc->info.plane_info[i].xdec;
    buf->planes[i].ydec = enc->info.plane_info[i].ydec;
    buf->planes[i].xstride = 1;
    buf->planes[i].ystride = GST_VIDEO_FRAME_COMP_STRIDE (frame, i);
  }
}

static void
daala_enc_reset_ts (GstDaalaEnc * enc, GstClockTime running_time, gint pfn)
{
  enc->granulepos_offset =
      gst_util_uint64_scale (running_time, enc->input_state->info.fps_n,
      GST_SECOND * enc->input_state->info.fps_d);
  enc->timestamp_offset = running_time;
  enc->pfn_offset = pfn;
}

static GstBuffer *
daala_enc_buffer_from_header_packet (GstDaalaEnc * enc, ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf =
      gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER (enc),
      packet->bytes);
  gst_buffer_fill (outbuf, 0, packet->packet, packet->bytes);
  GST_BUFFER_OFFSET (outbuf) = 0;
  GST_BUFFER_OFFSET_END (outbuf) = 0;
  GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_HEADER);

  GST_DEBUG ("created header packet buffer, %u bytes",
      (guint) gst_buffer_get_size (outbuf));
  return outbuf;
}

static GstFlowReturn
daala_enc_handle_frame (GstVideoEncoder * benc, GstVideoCodecFrame * frame)
{
  GstDaalaEnc *enc;
  ogg_packet op;
  GstClockTime timestamp, running_time;
  GstFlowReturn ret;
  gboolean force_keyframe;

  enc = GST_DAALA_ENC (benc);

  /* we keep track of two timelines.
   * - The timestamps from the incoming buffers, which we copy to the outgoing
   *   encoded buffers as-is. We need to do this as we simply forward the
   *   newsegment events.
   * - The running_time of the buffers, which we use to construct the granulepos
   *   in the packets.
   */
  timestamp = frame->pts;

  /* incoming buffers are clipped, so this should be positive */
  running_time =
      gst_segment_to_running_time (&GST_VIDEO_ENCODER_INPUT_SEGMENT (enc),
      GST_FORMAT_TIME, timestamp);
  g_return_val_if_fail (running_time >= 0 || timestamp < 0, GST_FLOW_ERROR);

  GST_OBJECT_LOCK (enc);
  if (enc->quant_changed) {
    int quant = enc->quant;

    daala_encode_ctl (enc->encoder, OD_SET_QUANT, &quant, sizeof (int));
    enc->quant_changed = FALSE;
  }

  /* see if we need to schedule a keyframe */
  force_keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame);
  GST_OBJECT_UNLOCK (enc);

  if (enc->packetno == 0) {
    /* no packets written yet, setup headers */
    GstCaps *caps;
    GstBuffer *buf;
    GList *buffers = NULL;
    int result;
    GstVideoCodecState *state;

    enc->granulepos_offset = 0;
    enc->timestamp_offset = 0;

    GST_DEBUG_OBJECT (enc, "output headers");
    /* Daala streams begin with three headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.  The
       third header holds the bitstream codebook.  We merely need to
       make the headers, then pass them to libdaala one at a time;
       libdaala handles the additional Ogg bitstream constraints */

    /* create the remaining daala headers */
    daala_comment_clear (&enc->comment);
    daala_comment_init (&enc->comment);

    while ((result =
            daala_encode_flush_header (enc->encoder, &enc->comment, &op)) > 0) {
      buf = daala_enc_buffer_from_header_packet (enc, &op);
      buffers = g_list_prepend (buffers, buf);
    }
    if (result < 0) {
      g_list_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
      g_list_free (buffers);
      goto encoder_disabled;
    }

    buffers = g_list_reverse (buffers);

    /* mark buffers and put on caps */
    caps = gst_caps_new_empty_simple ("video/x-daala");
    caps = daala_set_header_on_caps (caps, buffers);
    state = gst_video_encoder_set_output_state (benc, caps, enc->input_state);

    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, state->caps);

    gst_video_codec_state_unref (state);

    gst_video_encoder_negotiate (GST_VIDEO_ENCODER (enc));

    gst_video_encoder_set_headers (benc, buffers);

    daala_enc_reset_ts (enc, running_time, frame->presentation_frame_number);
  }

  {
    od_img img;
    gint res;
    GstVideoFrame vframe;

    if (force_keyframe) {
      /* TODO */
    }

    gst_video_frame_map (&vframe, &enc->input_state->info, frame->input_buffer,
        GST_MAP_READ);
    daala_enc_init_buffer (enc, &img, &vframe);

    res = daala_encode_img_in (enc->encoder, &img, 1);
    gst_video_frame_unmap (&vframe);

    /* none of the failure cases can happen here */
    g_assert (res == 0);

    ret = GST_FLOW_OK;
    while (daala_encode_packet_out (enc->encoder, 0, &op)) {
      ret = daala_push_packet (enc, &op);
      if (ret != GST_FLOW_OK)
        goto beach;
    }
  }

beach:
  gst_video_codec_frame_unref (frame);
  return ret;

  /* ERRORS */
encoder_disabled:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("libdaala has been compiled with the encoder disabled"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
daala_enc_finish (GstVideoEncoder * benc)
{
  GstDaalaEnc *enc;
  ogg_packet op;

  enc = GST_DAALA_ENC (benc);

  if (enc->initialised) {
    /* push last packet with eos flag, should not be called */
    while (daala_encode_packet_out (enc->encoder, 1, &op)) {
      daala_push_packet (enc, &op);
    }
  }

  return TRUE;
}

static gboolean
daala_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
daala_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (object);

  switch (prop_id) {
    case PROP_QUANT:
      GST_OBJECT_LOCK (enc);
      enc->quant = g_value_get_int (value);
      enc->quant_changed = TRUE;
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_KEYFRAME_RATE:
      GST_OBJECT_LOCK (enc);
      enc->keyframe_rate = g_value_get_int (value);
      enc->keyframe_rate_changed = TRUE;
      GST_OBJECT_UNLOCK (enc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
daala_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDaalaEnc *enc = GST_DAALA_ENC (object);

  switch (prop_id) {
    case PROP_QUANT:
      GST_OBJECT_LOCK (enc);
      g_value_set_int (value, enc->quant);
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_KEYFRAME_RATE:
      GST_OBJECT_LOCK (enc);
      g_value_set_int (value, enc->keyframe_rate);
      GST_OBJECT_UNLOCK (enc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_daala_enc_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "daalaenc",
      GST_RANK_PRIMARY, GST_TYPE_DAALA_ENC);
}
