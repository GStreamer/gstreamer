/* Schrodinger
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/video/gstbasevideodecoder.h>
#include <string.h>
#include <schroedinger/schro.h>
#include <math.h>
#include "gstschroutils.h"

#include <schroedinger/schroparse.h>

GST_DEBUG_CATEGORY_EXTERN (schro_debug);
#define GST_CAT_DEFAULT schro_debug

#define GST_TYPE_SCHRO_DEC \
  (gst_schro_dec_get_type())
#define GST_SCHRO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SCHRO_DEC,GstSchroDec))
#define GST_SCHRO_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SCHRO_DEC,GstSchroDecClass))
#define GST_IS_SCHRO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SCHRO_DEC))
#define GST_IS_SCHRO_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SCHRO_DEC))

typedef struct _GstSchroDec GstSchroDec;
typedef struct _GstSchroDecClass GstSchroDecClass;

struct _GstSchroDec
{
  GstBaseVideoDecoder base_video_decoder;

  SchroDecoder *decoder;

  gboolean seq_header_buffer_seen;
};

struct _GstSchroDecClass
{
  GstBaseVideoDecoderClass base_video_decoder_class;
};

GType gst_schro_dec_get_type (void);


/* GstSchroDec signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_schro_dec_finalize (GObject * object);

static gboolean gst_schro_dec_sink_query (GstPad * pad, GstQuery * query);

static gboolean gst_schro_dec_start (GstBaseVideoDecoder * dec);
static gboolean gst_schro_dec_stop (GstBaseVideoDecoder * dec);
static gboolean gst_schro_dec_reset (GstBaseVideoDecoder * dec);
static GstFlowReturn gst_schro_dec_parse_data (GstBaseVideoDecoder *
    base_video_decoder, gboolean at_eos);
static GstFlowReturn gst_schro_dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrame * frame);
static gboolean gst_schro_dec_finish (GstBaseVideoDecoder * base_video_decoder);
static void gst_schrodec_send_tags (GstSchroDec * schro_dec);

static GstStaticPadTemplate gst_schro_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac")
    );

static GstStaticPadTemplate gst_schro_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, AYUV }"))
    );

GST_BOILERPLATE (GstSchroDec, gst_schro_dec, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER);

static void
gst_schro_dec_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_schro_dec_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_schro_dec_sink_template);

  gst_element_class_set_details_simple (element_class, "Dirac Decoder",
      "Codec/Decoder/Video",
      "Decode Dirac streams", "David Schleef <ds@schleef.org>");
}

static void
gst_schro_dec_class_init (GstSchroDecClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseVideoDecoderClass *base_video_decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  base_video_decoder_class = GST_BASE_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_schro_dec_finalize;

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_schro_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_schro_dec_stop);
  base_video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_schro_dec_reset);
  base_video_decoder_class->parse_data =
      GST_DEBUG_FUNCPTR (gst_schro_dec_parse_data);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_schro_dec_handle_frame);
  base_video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_schro_dec_finish);

  gst_base_video_decoder_class_set_capture_pattern (base_video_decoder_class,
      0xffffffff, 0x42424344);
}

static void
gst_schro_dec_init (GstSchroDec * schro_dec, GstSchroDecClass * klass)
{
  GST_DEBUG ("gst_schro_dec_init");

  gst_pad_set_query_function (GST_BASE_VIDEO_CODEC_SINK_PAD (schro_dec),
      gst_schro_dec_sink_query);

  schro_dec->decoder = schro_decoder_new ();
}

#define OGG_DIRAC_GRANULE_SHIFT 22
#define OGG_DIRAC_GRANULE_LOW_MASK ((1ULL<<OGG_DIRAC_GRANULE_SHIFT)-1)

static gint64
granulepos_to_frame (gint64 granulepos)
{
  guint64 pt;

  if (granulepos == -1)
    return -1;

  pt = ((granulepos >> 22) + (granulepos & OGG_DIRAC_GRANULE_LOW_MASK)) >> 9;
  /* dist_h = (granulepos >> 22) & 0xff;
   * dist_l = granulepos & 0xff;
   * dist = (dist_h << 8) | dist_l;
   * delay = (granulepos >> 9) & 0x1fff;
   * dt = pt - delay; */

  return pt >> 1;
}

static gboolean
gst_schro_dec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSchroDec *dec;
  GstVideoState *state;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_SCHRO_DEC (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a decoding state */

  state = gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER (dec));

  res = FALSE;
  if (src_format == GST_FORMAT_DEFAULT && *dest_format == GST_FORMAT_TIME) {
    if (state->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (granulepos_to_frame (src_value),
          state->fps_d * GST_SECOND, state->fps_n);
      res = TRUE;
    } else {
      res = FALSE;
    }
  }

  gst_object_unref (dec);

  return res;
}

static gboolean
gst_schro_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstSchroDec *dec;
  gboolean res = FALSE;

  dec = GST_SCHRO_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_schro_dec_sink_convert (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (dec);

  return res;
error:
  GST_DEBUG_OBJECT (dec, "query failed");
  goto done;
}

static gboolean
gst_schro_dec_start (GstBaseVideoDecoder * dec)
{

  return TRUE;
}

static gboolean
gst_schro_dec_stop (GstBaseVideoDecoder * dec)
{

  return TRUE;
}

static gboolean
gst_schro_dec_reset (GstBaseVideoDecoder * dec)
{
  GstSchroDec *schro_dec;

  schro_dec = GST_SCHRO_DEC (dec);

  GST_DEBUG ("reset");

  if (schro_dec->decoder) {
    schro_decoder_reset (schro_dec->decoder);
  }

  return TRUE;
}

static void
gst_schro_dec_finalize (GObject * object)
{
  GstSchroDec *schro_dec;

  g_return_if_fail (GST_IS_SCHRO_DEC (object));
  schro_dec = GST_SCHRO_DEC (object);

  if (schro_dec->decoder) {
    schro_decoder_free (schro_dec->decoder);
    schro_dec->decoder = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
parse_sequence_header (GstSchroDec * schro_dec, guint8 * data, int size)
{
  SchroVideoFormat video_format;
  int ret;
  GstVideoState *state;

  GST_DEBUG_OBJECT (schro_dec, "parse_sequence_header size=%d", size);

  state = gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER (schro_dec));

  schro_dec->seq_header_buffer_seen = TRUE;

  ret = schro_parse_decode_sequence_header (data + 13, size - 13,
      &video_format);
  if (ret) {
    if (video_format.chroma_format == SCHRO_CHROMA_444) {
      state->format = GST_VIDEO_FORMAT_AYUV;
    } else if (video_format.chroma_format == SCHRO_CHROMA_422) {
      state->format = GST_VIDEO_FORMAT_YUY2;
    } else if (video_format.chroma_format == SCHRO_CHROMA_420) {
      state->format = GST_VIDEO_FORMAT_I420;
    }
    state->fps_n = video_format.frame_rate_numerator;
    state->fps_d = video_format.frame_rate_denominator;
    GST_DEBUG_OBJECT (schro_dec, "Frame rate is %d/%d", state->fps_n,
        state->fps_d);

    state->width = video_format.width;
    state->height = video_format.height;
    GST_DEBUG ("Frame dimensions are %d x %d\n", state->width, state->height);

    state->clean_width = video_format.clean_width;
    state->clean_height = video_format.clean_height;
    state->clean_offset_left = video_format.left_offset;
    state->clean_offset_top = video_format.top_offset;

    state->par_n = video_format.aspect_ratio_numerator;
    state->par_d = video_format.aspect_ratio_denominator;
    GST_DEBUG ("Pixel aspect ratio is %d/%d", state->par_n, state->par_d);

    gst_base_video_decoder_set_src_caps (GST_BASE_VIDEO_DECODER (schro_dec));
  } else {
    GST_WARNING ("Failed to get frame rate from sequence header");
  }

  gst_schrodec_send_tags (schro_dec);
}


static GstFlowReturn
gst_schro_dec_parse_data (GstBaseVideoDecoder * base_video_decoder,
    gboolean at_eos)
{
  GstSchroDec *schro_decoder;
  unsigned char header[SCHRO_PARSE_HEADER_SIZE];
  int next;
  int prev;
  int parse_code;

  GST_DEBUG_OBJECT (base_video_decoder, "parse_data");

  schro_decoder = GST_SCHRO_DEC (base_video_decoder);

  if (gst_adapter_available (base_video_decoder->input_adapter) <
      SCHRO_PARSE_HEADER_SIZE) {
    return GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  GST_DEBUG ("available %d",
      gst_adapter_available (base_video_decoder->input_adapter));

  gst_adapter_copy (base_video_decoder->input_adapter, header, 0,
      SCHRO_PARSE_HEADER_SIZE);

  parse_code = header[4];
  next = GST_READ_UINT32_BE (header + 5);
  prev = GST_READ_UINT32_BE (header + 9);

  GST_DEBUG ("%08x %02x %08x %08x",
      GST_READ_UINT32_BE (header), parse_code, next, prev);

  if (memcmp (header, "BBCD", 4) != 0 ||
      (next & 0xf0000000) || (prev & 0xf0000000)) {
    gst_base_video_decoder_lost_sync (base_video_decoder);
    return GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  if (SCHRO_PARSE_CODE_IS_END_OF_SEQUENCE (parse_code)) {
    GstVideoFrame *frame;

    if (next != 0 && next != SCHRO_PARSE_HEADER_SIZE) {
      GST_WARNING ("next is not 0 or 13 in EOS packet (%d)", next);
    }

    gst_base_video_decoder_add_to_frame (base_video_decoder,
        SCHRO_PARSE_HEADER_SIZE);

    frame = base_video_decoder->current_frame;
    frame->is_eos = TRUE;

    SCHRO_DEBUG ("eos");

    return gst_base_video_decoder_have_frame (base_video_decoder);
  }

  if (gst_adapter_available (base_video_decoder->input_adapter) < next) {
    return GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  if (SCHRO_PARSE_CODE_IS_SEQ_HEADER (parse_code)) {
    guint8 *data;

    data = g_malloc (next);

    gst_adapter_copy (base_video_decoder->input_adapter, data, 0, next);
    parse_sequence_header (schro_decoder, data, next);

    gst_base_video_decoder_set_sync_point (base_video_decoder);

#if 0
    if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->last_sink_timestamp)) {
      base_video_decoder->current_frame->presentation_timestamp =
          base_video_decoder->last_sink_timestamp;
      GST_DEBUG ("got timestamp %" G_GINT64_FORMAT,
          base_video_decoder->last_sink_timestamp);
    } else if (base_video_decoder->last_sink_offset_end != -1) {
      GstVideoState *state;

#if 0
      /* FIXME perhaps should use this to determine if the granulepos
       * is valid */
      {
        guint64 pt;
        int dist_h;
        int dist_l;
        int dist;
        int delay;
        guint64 dt;
        gint64 granulepos = base_video_decoder->last_sink_offset_end;

        pt = ((granulepos >> 22) +
            (granulepos & OGG_DIRAC_GRANULE_LOW_MASK)) >> 9;
        dist_h = (granulepos >> 22) & 0xff;
        dist_l = granulepos & 0xff;
        dist = (dist_h << 8) | dist_l;
        delay = (granulepos >> 9) & 0x1fff;
        dt = pt - delay;
        GST_DEBUG ("gp pt %lld dist %d delay %d dt %lld", pt, dist, delay, dt);
      }
#endif
      state = gst_base_video_decoder_get_state (base_video_decoder);
      base_video_decoder->current_frame->presentation_timestamp =
          gst_util_uint64_scale (granulepos_to_frame
          (base_video_decoder->last_sink_offset_end), state->fps_d * GST_SECOND,
          state->fps_n);
    } else {
      base_video_decoder->current_frame->presentation_timestamp = -1;
    }
#endif

    g_free (data);
  }

  if (!schro_decoder->seq_header_buffer_seen) {
    gst_adapter_flush (base_video_decoder->input_adapter, next);
    return GST_FLOW_OK;
  }

  if (SCHRO_PARSE_CODE_IS_PICTURE (parse_code)) {
    GstVideoFrame *frame;
    guint8 tmp[4];

    frame = base_video_decoder->current_frame;

    gst_adapter_copy (base_video_decoder->input_adapter, tmp,
        SCHRO_PARSE_HEADER_SIZE, 4);

    frame->presentation_frame_number = GST_READ_UINT32_BE (tmp);

    gst_base_video_decoder_add_to_frame (base_video_decoder, next);

    return gst_base_video_decoder_have_frame (base_video_decoder);
  } else {
    gst_base_video_decoder_add_to_frame (base_video_decoder, next);
  }

  return GST_FLOW_OK;
}

static void
gst_schrodec_send_tags (GstSchroDec * schro_dec)
{
  GstTagList *list;

  list = gst_tag_list_new ();
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_VIDEO_CODEC, "Dirac", NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT_CAST (schro_dec),
      GST_BASE_VIDEO_CODEC_SRC_PAD (schro_dec), list);
}

static GstFlowReturn
gst_schro_dec_process (GstSchroDec * schro_dec, gboolean eos)
{
  gboolean go;
  GstFlowReturn ret;

  ret = GST_FLOW_OK;
  go = TRUE;
  while (go) {
    int it;

    it = schro_decoder_autoparse_wait (schro_dec->decoder);

    switch (it) {
      case SCHRO_DECODER_FIRST_ACCESS_UNIT:
        break;
      case SCHRO_DECODER_NEED_BITS:
        GST_DEBUG ("need bits");
        go = 0;
        break;
      case SCHRO_DECODER_NEED_FRAME:
      {
        GstBuffer *outbuf;
        GstVideoState *state;
        SchroFrame *schro_frame;

        GST_DEBUG ("need frame");

        state =
            gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER
            (schro_dec));
        outbuf =
            gst_base_video_decoder_alloc_src_buffer (GST_BASE_VIDEO_DECODER
            (schro_dec));
        schro_frame =
            gst_schro_buffer_wrap (outbuf, state->format, state->width,
            state->height);
        schro_decoder_add_output_picture (schro_dec->decoder, schro_frame);
        break;
      }
      case SCHRO_DECODER_OK:
      {
        SchroFrame *schro_frame;
        SchroTag *tag;
        GstVideoFrame *frame;

        GST_DEBUG ("got frame");

        tag = schro_decoder_get_picture_tag (schro_dec->decoder);
        schro_frame = schro_decoder_pull (schro_dec->decoder);
        frame = tag->value;

        if (schro_frame) {
          if (schro_frame->priv) {
            GstFlowReturn flow_ret;

            frame->src_buffer = gst_buffer_ref (GST_BUFFER (schro_frame->priv));

            flow_ret =
                gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER
                (schro_dec), frame);
            if (flow_ret != GST_FLOW_OK) {
              GST_DEBUG ("finish frame returned %d", flow_ret);
              return flow_ret;
            }
          } else {
            GST_DEBUG ("skipped frame");
          }

          schro_frame_unref (schro_frame);
        }
        if (tag)
          schro_tag_free (tag);
        if (!eos) {
          go = FALSE;
        }
      }

        break;
      case SCHRO_DECODER_EOS:
        GST_DEBUG ("eos");
        go = FALSE;
        break;
      case SCHRO_DECODER_ERROR:
        go = FALSE;
        GST_DEBUG ("codec error");
        ret = GST_FLOW_ERROR;
        break;
      default:
        break;
    }
  }
  return ret;
}

GstFlowReturn
gst_schro_dec_handle_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstSchroDec *schro_dec;
  SchroBuffer *input_buffer;

  schro_dec = GST_SCHRO_DEC (base_video_decoder);

  GST_DEBUG ("handle frame");

  input_buffer = gst_schro_wrap_gst_buffer (frame->sink_buffer);
  frame->sink_buffer = NULL;

  input_buffer->tag = schro_tag_new (frame, NULL);

  schro_decoder_autoparse_push (schro_dec->decoder, input_buffer);

  return gst_schro_dec_process (schro_dec, FALSE);
}

gboolean
gst_schro_dec_finish (GstBaseVideoDecoder * base_video_decoder)
{
  GstSchroDec *schro_dec;

  schro_dec = GST_SCHRO_DEC (base_video_decoder);

  GST_DEBUG ("finish");

  schro_decoder_autoparse_push_end_of_sequence (schro_dec->decoder);

  return gst_schro_dec_process (schro_dec, TRUE);
}
