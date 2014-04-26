/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
 * SECTION:element-daaladec
 * @see_also: daalaenc, oggdemux
 *
 * This element decodes daala streams into raw video
 * <ulink url="http://www.xiph.org/daala/">Daala</ulink> is a royalty-free
 * video codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! daaladec ! xvimagesink
 * ]| This example pipeline will decode an ogg stream and decodes the daala video. Refer to
 * the daalaenc example to create the ogg file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstdaaladec.h"
#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#define GST_CAT_DEFAULT daaladec_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

/* This was removed from the base class, this is used as a
   temporary return to signal the need to call _drop_frame,
   and does not leave daalaenc. */
#define GST_CUSTOM_FLOW_DROP GST_FLOW_CUSTOM_SUCCESS_1

enum
{
  PROP_0
};

static GstStaticPadTemplate daala_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { I420, Y444 }, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate daala_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-daala")
    );

#define gst_daala_dec_parent_class parent_class
G_DEFINE_TYPE (GstDaalaDec, gst_daala_dec, GST_TYPE_VIDEO_DECODER);

static gboolean daala_dec_start (GstVideoDecoder * decoder);
static gboolean daala_dec_stop (GstVideoDecoder * decoder);
static gboolean daala_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn daala_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn daala_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean daala_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

static GstFlowReturn daala_dec_decode_buffer (GstDaalaDec * dec,
    GstBuffer * buf, GstVideoCodecFrame * frame);

static void
gst_daala_dec_class_init (GstDaalaDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&daala_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&daala_dec_sink_factory));
  gst_element_class_set_static_metadata (element_class,
      "Daala video decoder", "Codec/Decoder/Video",
      "Decode raw Daala streams to raw YUV video",
      "Sebastian Dröge <slomo@circular-chaos.org>");

  video_decoder_class->start = GST_DEBUG_FUNCPTR (daala_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (daala_dec_stop);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (daala_dec_set_format);
  video_decoder_class->parse = GST_DEBUG_FUNCPTR (daala_dec_parse);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (daala_dec_handle_frame);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (daala_dec_decide_allocation);

  GST_DEBUG_CATEGORY_INIT (daaladec_debug, "daaladec", 0, "Daala decoder");
}

static void
gst_daala_dec_init (GstDaalaDec * dec)
{
  /* input is packetized,
   * but is not marked that way so data gets parsed and keyframes marked */
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (dec), FALSE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (dec), TRUE);
}

static void
daala_dec_reset (GstDaalaDec * dec)
{
  dec->need_keyframe = TRUE;
}

static gboolean
daala_dec_start (GstVideoDecoder * decoder)
{
  GstDaalaDec *dec = GST_DAALA_DEC (decoder);

  GST_DEBUG_OBJECT (dec, "start");
  daala_info_clear (&dec->info);
  daala_comment_clear (&dec->comment);
  GST_DEBUG_OBJECT (dec, "Setting have_header to FALSE");
  dec->have_header = FALSE;
  daala_dec_reset (dec);

  return TRUE;
}

static gboolean
daala_dec_stop (GstVideoDecoder * decoder)
{
  GstDaalaDec *dec = GST_DAALA_DEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");
  daala_info_clear (&dec->info);
  daala_comment_clear (&dec->comment);
  daala_setup_free (dec->setup);
  dec->setup = NULL;
  daala_decode_free (dec->decoder);
  dec->decoder = NULL;
  daala_dec_reset (dec);
  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }
  if (dec->output_state) {
    gst_video_codec_state_unref (dec->output_state);
    dec->output_state = NULL;
  }

  return TRUE;
}

static GstFlowReturn
daala_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  gint av;
  const guint8 *data;

  av = gst_adapter_available (adapter);

  if (av > 0) {
    data = gst_adapter_map (adapter, 1);
    /* check for keyframe; must not be header packet */
    if (!(data[0] & 0x80) && (data[0] & 0x40)) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    }
    gst_adapter_unmap (adapter);
  }

  /* and pass along all */
  gst_video_decoder_add_to_frame (decoder, av);
  return gst_video_decoder_have_frame (decoder);
}


static gboolean
daala_dec_set_format (GstVideoDecoder * bdec, GstVideoCodecState * state)
{
  GstDaalaDec *dec;

  dec = GST_DAALA_DEC (bdec);

  /* Keep a copy of the input state */
  if (dec->input_state)
    gst_video_codec_state_unref (dec->input_state);
  dec->input_state = gst_video_codec_state_ref (state);

  /* FIXME : Interesting, we always accept any kind of caps ? */
  if (state->codec_data) {
    GstBuffer *buffer;
    GstMapInfo minfo;
    guint8 *data;
    guint size;
    guint offset;

    buffer = state->codec_data;
    gst_buffer_map (buffer, &minfo, GST_MAP_READ);

    offset = 0;
    size = minfo.size;
    data = (guint8 *) minfo.data;

    while (size > 2) {
      guint psize;
      GstBuffer *buf;

      psize = (data[0] << 8) | data[1];
      /* skip header */
      data += 2;
      size -= 2;
      offset += 2;

      /* make sure we don't read too much */
      psize = MIN (psize, size);

      buf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, offset, psize);

      /* first buffer is a discont buffer */
      if (offset == 2)
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);

      /* now feed it to the decoder we can ignore the error */
      daala_dec_decode_buffer (dec, buf, NULL);
      gst_buffer_unref (buf);

      /* skip the data */
      size -= psize;
      data += psize;
      offset += psize;
    }

    gst_buffer_unmap (buffer, &minfo);
  }

  GST_DEBUG_OBJECT (dec, "Done");

  return TRUE;
}

static GstFlowReturn
daala_handle_comment_packet (GstDaalaDec * dec, ogg_packet * packet)
{
  gchar *encoder = NULL;
  GstTagList *list;

  GST_DEBUG_OBJECT (dec, "parsing comment packet");

  list =
      gst_tag_list_from_vorbiscomment (packet->packet, packet->bytes,
      (guint8 *) "\201daala", 6, &encoder);

  if (!list) {
    GST_ERROR_OBJECT (dec, "couldn't decode comments");
    list = gst_tag_list_new_empty ();
  }
  if (encoder) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
    g_free (encoder);
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_ENCODER_VERSION, dec->info.version_major,
      GST_TAG_VIDEO_CODEC, "Daala", NULL);

  gst_video_decoder_merge_tags (GST_VIDEO_DECODER (dec),
      list, GST_TAG_MERGE_REPLACE);

  gst_tag_list_unref (list);

  return GST_FLOW_OK;
}

static GstFlowReturn
daala_handle_type_packet (GstDaalaDec * dec)
{
  gint par_num, par_den;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecState *state;
  GstVideoFormat fmt;
  GstVideoInfo *info;

  if (!dec->input_state)
    return GST_FLOW_NOT_NEGOTIATED;

  info = &dec->input_state->info;

  GST_DEBUG_OBJECT (dec, "fps %d/%d, PAR %d/%d",
      dec->info.timebase_numerator, dec->info.timebase_denominator,
      dec->info.pixel_aspect_numerator, dec->info.pixel_aspect_denominator);

  /* calculate par
   * the info.aspect_* values reflect PAR;
   * 0:x and x:0 are allowed and can be interpreted as 1:1.
   */
  par_num = GST_VIDEO_INFO_PAR_N (info);
  par_den = GST_VIDEO_INFO_PAR_D (info);

  /* If we have a default PAR, see if the decoder specified a different one */
  if (par_num == 1 && par_den == 1 &&
      (dec->info.pixel_aspect_numerator != 0
          && dec->info.pixel_aspect_denominator != 0)) {
    par_num = dec->info.pixel_aspect_numerator;
    par_den = dec->info.pixel_aspect_denominator;
  }
  /* daala has:
   *
   *  width/height : dimension of the encoded frame 
   *  pic_width/pic_height : dimension of the visible part
   *  pic_x/pic_y : offset in encoded frame where visible part starts
   */
  GST_DEBUG_OBJECT (dec, "dimension %dx%d, PAR %d/%d", dec->info.pic_width,
      dec->info.pic_height, par_num, par_den);

  if (dec->info.nplanes == 3 && dec->info.plane_info[0].xdec == 0 &&
      dec->info.plane_info[0].ydec == 0 &&
      dec->info.plane_info[1].xdec == 1 &&
      dec->info.plane_info[1].ydec == 1 &&
      dec->info.plane_info[2].xdec == 1 && dec->info.plane_info[2].ydec == 1) {
    fmt = GST_VIDEO_FORMAT_I420;
  } else if (dec->info.nplanes == 3 && dec->info.plane_info[0].xdec == 0 &&
      dec->info.plane_info[0].ydec == 0 &&
      dec->info.plane_info[1].xdec == 0 &&
      dec->info.plane_info[1].ydec == 0 &&
      dec->info.plane_info[2].xdec == 0 && dec->info.plane_info[2].ydec == 0) {
    fmt = GST_VIDEO_FORMAT_Y444;
  } else {
    goto unsupported_format;
  }

  GST_VIDEO_INFO_WIDTH (info) = dec->info.pic_width;
  GST_VIDEO_INFO_HEIGHT (info) = dec->info.pic_height;

  /* done */
  dec->decoder = daala_decode_alloc (&dec->info, dec->setup);

  /* Create the output state */
  dec->output_state = state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec), fmt,
      info->width, info->height, dec->input_state);

  /* FIXME : Do we still need to set fps/par now that we pass the reference input stream ? */
  state->info.fps_n = dec->info.timebase_numerator;
  state->info.fps_d = dec->info.timebase_denominator;
  state->info.par_n = par_num;
  state->info.par_d = par_den;

  gst_video_decoder_negotiate (GST_VIDEO_DECODER (dec));

  dec->have_header = TRUE;

  return ret;

  /* ERRORS */
unsupported_format:
  {
    GST_ERROR_OBJECT (dec, "Invalid pixel format");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
daala_handle_header_packet (GstDaalaDec * dec, ogg_packet * packet)
{
  GstFlowReturn res;
  int ret;

  GST_DEBUG_OBJECT (dec, "parsing header packet");

  ret = daala_decode_header_in (&dec->info, &dec->comment, &dec->setup, packet);
  if (ret < 0)
    goto header_read_error;

  switch (packet->packet[0]) {
    case 0x81:
      res = daala_handle_comment_packet (dec, packet);
      break;
    case 0x82:
      res = daala_handle_type_packet (dec);
      break;
    default:
      /* ignore */
      g_warning ("unknown daala header packet found");
    case 0x80:
      /* nothing special, this is the identification header */
      res = GST_FLOW_OK;
      break;
  }
  return res;

  /* ERRORS */
header_read_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read header packet"));
    return GST_FLOW_ERROR;
  }
}

/* Allocate buffer and copy image data into Y444 format */
static GstFlowReturn
daala_handle_image (GstDaalaDec * dec, od_img * img, GstVideoCodecFrame * frame)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  gint width, height, stride;
  GstFlowReturn result;
  gint i, comp;
  guint8 *dest, *src;
  GstVideoFrame vframe;

  result = gst_video_decoder_allocate_output_frame (decoder, frame);

  if (G_UNLIKELY (result != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (dec, "could not get buffer, reason: %s",
        gst_flow_get_name (result));
    return result;
  }

  /* if only libdaala would allow us to give it a destination frame */
  GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, dec,
      "doing unavoidable video frame copy");

  if (G_UNLIKELY (!gst_video_frame_map (&vframe, &dec->output_state->info,
              frame->output_buffer, GST_MAP_WRITE)))
    goto invalid_frame;

  for (comp = 0; comp < 3; comp++) {
    width = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, comp);
    height = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, comp);
    stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, comp);
    dest = GST_VIDEO_FRAME_COMP_DATA (&vframe, comp);

    src = img->planes[comp].data;

    for (i = 0; i < height; i++) {
      memcpy (dest, src, width);

      dest += stride;
      src += img->planes[comp].ystride;
    }
  }
  gst_video_frame_unmap (&vframe);

  return GST_FLOW_OK;
invalid_frame:
  {
    GST_DEBUG_OBJECT (dec, "could not map video frame");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
daala_handle_data_packet (GstDaalaDec * dec, ogg_packet * packet,
    GstVideoCodecFrame * frame)
{
  /* normal data packet */
  od_img img;
  gboolean keyframe;
  GstFlowReturn result;

  if (G_UNLIKELY (!dec->have_header))
    goto not_initialized;

  /* the second most significant bit of the first data byte is cleared 
   * for keyframes. We can only check it if it's not a zero-length packet. */
  keyframe = packet->bytes && ((packet->packet[0] & 0x40));
  if (G_UNLIKELY (keyframe)) {
    GST_DEBUG_OBJECT (dec, "we have a keyframe");
    dec->need_keyframe = FALSE;
  } else if (G_UNLIKELY (dec->need_keyframe)) {
    goto dropping;
  }

  GST_DEBUG_OBJECT (dec, "parsing data packet");

  /* this does the decoding */
  if (G_UNLIKELY (daala_decode_packet_in (dec->decoder, &img, packet) < 0))
    goto decode_error;

  if (frame &&
      (gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (dec),
              frame) < 0))
    goto dropping_qos;

  if (G_UNLIKELY ((img.width != dec->info.pic_width
              || img.height != dec->info.pic_height)))
    goto wrong_dimensions;

  result = daala_handle_image (dec, &img, frame);

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("no header sent yet"));
    return GST_FLOW_ERROR;
  }
dropping:
  {
    GST_WARNING_OBJECT (dec, "dropping frame because we need a keyframe");
    return GST_CUSTOM_FLOW_DROP;
  }
dropping_qos:
  {
    GST_WARNING_OBJECT (dec, "dropping frame because of QoS");
    return GST_CUSTOM_FLOW_DROP;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("daala decoder did not decode data packet"));
    return GST_FLOW_ERROR;
  }
wrong_dimensions:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, FORMAT,
        (NULL), ("dimensions of image do not match header"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
daala_dec_decode_buffer (GstDaalaDec * dec, GstBuffer * buf,
    GstVideoCodecFrame * frame)
{
  ogg_packet packet;
  GstFlowReturn result = GST_FLOW_OK;
  GstMapInfo minfo;

  /* make ogg_packet out of the buffer */
  gst_buffer_map (buf, &minfo, GST_MAP_READ);
  packet.packet = minfo.data;
  packet.bytes = minfo.size;
  packet.granulepos = -1;
  packet.packetno = 0;          /* we don't really care */
  packet.b_o_s = dec->have_header ? 0 : 1;
  /* EOS does not matter for the decoder */
  packet.e_o_s = 0;

  GST_LOG_OBJECT (dec, "decode buffer of size %ld", packet.bytes);

  GST_DEBUG_OBJECT (dec, "header=%02x", packet.bytes ? packet.packet[0] : -1);

  /* switch depending on packet type. A zero byte packet is always a data
   * packet; we don't dereference it in that case. */
  if (packet.bytes && packet.packet[0] & 0x80) {
    if (dec->have_header) {
      GST_WARNING_OBJECT (GST_OBJECT (dec), "Ignoring header");
      GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
          GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);
      result = GST_CUSTOM_FLOW_DROP;
      goto done;
    }
    result = daala_handle_header_packet (dec, &packet);
    /* header packets are not meant to be displayed */
    /* FIXME : This is a temporary hack. The proper fix would be to
     * not call _finish_frame() for these types of packets */
    GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
        GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);
  } else {
    result = daala_handle_data_packet (dec, &packet, frame);
  }

done:
  gst_buffer_unmap (buf, &minfo);

  return result;
}

static GstFlowReturn
daala_dec_handle_frame (GstVideoDecoder * bdec, GstVideoCodecFrame * frame)
{
  GstDaalaDec *dec;
  GstFlowReturn res;

  dec = GST_DAALA_DEC (bdec);

  res = daala_dec_decode_buffer (dec, frame->input_buffer, frame);
  switch (res) {
    case GST_FLOW_OK:
      res = gst_video_decoder_finish_frame (bdec, frame);
      break;
    case GST_CUSTOM_FLOW_DROP:
      res = gst_video_decoder_drop_frame (bdec, frame);
      break;
    default:
      gst_video_codec_frame_unref (frame);
      break;
  }

  return res;
}

static gboolean
daala_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
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

gboolean
gst_daala_dec_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "daaladec",
      GST_RANK_PRIMARY, GST_TYPE_DAALA_DEC);
}
