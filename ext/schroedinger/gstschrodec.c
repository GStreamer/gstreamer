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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideodecoder.h>
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
  GstVideoDecoder base_video_decoder;

  SchroDecoder *decoder;

  gboolean seq_header_buffer_seen;
};

struct _GstSchroDecClass
{
  GstVideoDecoderClass base_video_decoder_class;
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

static gboolean gst_schro_dec_start (GstVideoDecoder * dec);
static gboolean gst_schro_dec_stop (GstVideoDecoder * dec);
static gboolean gst_schro_dec_flush (GstVideoDecoder * dec);
static GstFlowReturn gst_schro_dec_parse (GstVideoDecoder *
    base_video_decoder, GstVideoCodecFrame * frame, GstAdapter * adapter,
    gboolean at_eos);
static GstFlowReturn gst_schro_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_schro_dec_finish (GstVideoDecoder * base_video_decoder);
static void gst_schrodec_send_tags (GstSchroDec * schro_dec);
static gboolean gst_schro_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SCHRO_YUV_LIST))
    );

#define parent_class gst_schro_dec_parent_class
G_DEFINE_TYPE (GstSchroDec, gst_schro_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_schro_dec_class_init (GstSchroDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoDecoderClass *base_video_decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  base_video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_schro_dec_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_schro_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_schro_dec_sink_template));

  gst_element_class_set_static_metadata (element_class, "Dirac Decoder",
      "Codec/Decoder/Video",
      "Decode Dirac streams", "David Schleef <ds@schleef.org>");

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_schro_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_schro_dec_stop);
  base_video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_schro_dec_flush);
  base_video_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_schro_dec_parse);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_schro_dec_handle_frame);
  base_video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_schro_dec_finish);
  base_video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_schro_dec_decide_allocation);
}

static void
gst_schro_dec_init (GstSchroDec * schro_dec)
{
  GST_DEBUG ("gst_schro_dec_init");

  schro_dec->decoder = schro_decoder_new ();
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (schro_dec), FALSE);
}

static gboolean
gst_schro_dec_start (GstVideoDecoder * dec)
{

  return TRUE;
}

static gboolean
gst_schro_dec_stop (GstVideoDecoder * dec)
{

  return TRUE;
}

static gboolean
gst_schro_dec_flush (GstVideoDecoder * dec)
{
  GstSchroDec *schro_dec;

  schro_dec = GST_SCHRO_DEC (dec);

  GST_DEBUG ("flush");

  if (schro_dec->decoder)
    schro_decoder_reset (schro_dec->decoder);

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
  GstVideoCodecState *state = NULL;
  int bit_depth;
  GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;

  GST_DEBUG_OBJECT (schro_dec, "parse_sequence_header size=%d", size);

  schro_dec->seq_header_buffer_seen = TRUE;

  ret = schro_parse_decode_sequence_header (data + 13, size - 13,
      &video_format);
  if (!ret) {
    /* FIXME : Isn't this meant to be a *fatal* error ? */
    GST_WARNING ("Failed to get frame rate from sequence header");
    goto beach;
  }
#if SCHRO_CHECK_VERSION(1,0,11)
  bit_depth = schro_video_format_get_bit_depth (&video_format);
#else
  bit_depth = 8;
#endif

  if (bit_depth == 8) {
    if (video_format.chroma_format == SCHRO_CHROMA_444) {
      fmt = GST_VIDEO_FORMAT_AYUV;
    } else if (video_format.chroma_format == SCHRO_CHROMA_422) {
      fmt = GST_VIDEO_FORMAT_UYVY;
    } else if (video_format.chroma_format == SCHRO_CHROMA_420) {
      fmt = GST_VIDEO_FORMAT_I420;
    }
#if SCHRO_CHECK_VERSION(1,0,11)
  } else if (bit_depth <= 10) {
    if (video_format.colour_matrix == SCHRO_COLOUR_MATRIX_REVERSIBLE) {
      fmt = GST_VIDEO_FORMAT_ARGB;
    } else {
      fmt = GST_VIDEO_FORMAT_v210;
    }
  } else if (bit_depth <= 16) {
    fmt = GST_VIDEO_FORMAT_AYUV64;
  } else {
    GST_ERROR ("bit depth too large (%d > 16)", bit_depth);
    fmt = GST_VIDEO_FORMAT_AYUV64;
#endif
  }

  state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (schro_dec),
      fmt, video_format.width, video_format.height, NULL);

  GST_DEBUG ("Frame dimensions are %d x %d\n", state->info.width,
      state->info.height);

  state->info.fps_n = video_format.frame_rate_numerator;
  state->info.fps_d = video_format.frame_rate_denominator;
  GST_DEBUG_OBJECT (schro_dec, "Frame rate is %d/%d", state->info.fps_n,
      state->info.fps_d);

  state->info.par_n = video_format.aspect_ratio_numerator;
  state->info.par_d = video_format.aspect_ratio_denominator;
  GST_DEBUG ("Pixel aspect ratio is %d/%d", state->info.par_n,
      state->info.par_d);

  gst_video_decoder_negotiate (GST_VIDEO_DECODER (schro_dec));

beach:
  if (state)
    gst_video_codec_state_unref (state);
  gst_schrodec_send_tags (schro_dec);
}


static GstFlowReturn
gst_schro_dec_parse (GstVideoDecoder * base_video_decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstSchroDec *schro_decoder;
  unsigned char header[SCHRO_PARSE_HEADER_SIZE];
  int next;
  int prev;
  int parse_code;
  int av, loc;

  GST_DEBUG_OBJECT (base_video_decoder, "parse");

  schro_decoder = GST_SCHRO_DEC (base_video_decoder);
  av = gst_adapter_available (adapter);

  if (av < SCHRO_PARSE_HEADER_SIZE) {
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  GST_DEBUG ("available %d", av);

  /* Check for header */
  loc =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x42424344, 0,
      av - 3);
  if (G_UNLIKELY (loc == -1)) {
    GST_DEBUG_OBJECT (schro_decoder, "No header");
    gst_adapter_flush (adapter, av - 3);
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  /* Skip data until header */
  if (loc > 0)
    gst_adapter_flush (adapter, loc);

  gst_adapter_copy (adapter, header, 0, SCHRO_PARSE_HEADER_SIZE);

  parse_code = header[4];
  next = GST_READ_UINT32_BE (header + 5);
  prev = GST_READ_UINT32_BE (header + 9);

  GST_DEBUG ("%08x %02x %08x %08x",
      GST_READ_UINT32_BE (header), parse_code, next, prev);

  if (memcmp (header, "BBCD", 4) != 0 ||
      (next & 0xf0000000) || (prev & 0xf0000000)) {
    gst_adapter_flush (adapter, 1);
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  if (SCHRO_PARSE_CODE_IS_END_OF_SEQUENCE (parse_code)) {
    if (next != 0 && next != SCHRO_PARSE_HEADER_SIZE) {
      GST_WARNING ("next is not 0 or 13 in EOS packet (%d)", next);
    }

    gst_video_decoder_add_to_frame (base_video_decoder,
        SCHRO_PARSE_HEADER_SIZE);

    SCHRO_DEBUG ("eos");

    return gst_video_decoder_have_frame (base_video_decoder);
  }

  if (gst_adapter_available (adapter) < next) {
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  if (SCHRO_PARSE_CODE_IS_SEQ_HEADER (parse_code)) {
    guint8 *data;

    data = g_malloc (next);

    gst_adapter_copy (adapter, data, 0, next);
    parse_sequence_header (schro_decoder, data, next);

    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

#if 0
    if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->last_sink_timestamp)) {
      base_video_decoder->current_frame->pts =
          base_video_decoder->last_sink_timestamp;
      GST_DEBUG ("got timestamp %" G_GINT64_FORMAT,
          base_video_decoder->last_sink_timestamp);
    } else if (base_video_decoder->last_sink_offset_end != -1) {
      GstVideoCodecState *state;

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
      state = gst_video_decoder_get_state (base_video_decoder);
      base_video_decoder->current_frame->pts =
          gst_util_uint64_scale (granulepos_to_frame
          (base_video_decoder->last_sink_offset_end), state->fps_d * GST_SECOND,
          state->fps_n);
    } else {
      base_video_decoder->current_frame->pts = -1;
    }
#endif

    g_free (data);
  }

  if (!schro_decoder->seq_header_buffer_seen) {
    gst_adapter_flush (adapter, next);
    return GST_FLOW_OK;
  }

  if (SCHRO_PARSE_CODE_IS_PICTURE (parse_code)) {
    guint8 tmp[4];

    gst_adapter_copy (adapter, tmp, SCHRO_PARSE_HEADER_SIZE, 4);

    /* What is the point of this ? BaseVideoDecoder doesn't
     * do anything with presentation_frame_number */
    frame->presentation_frame_number = GST_READ_UINT32_BE (tmp);

    gst_video_decoder_add_to_frame (base_video_decoder, next);

    return gst_video_decoder_have_frame (base_video_decoder);
  } else {
    gst_video_decoder_add_to_frame (base_video_decoder, next);
  }

  return GST_FLOW_OK;
}

static void
gst_schrodec_send_tags (GstSchroDec * schro_dec)
{
  GstTagList *list;

  list = gst_tag_list_new_empty ();
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_VIDEO_CODEC, "Dirac", NULL);

  gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (schro_dec),
      gst_event_new_tag (list));
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
        GstVideoCodecState *state;
        SchroFrame *schro_frame;

        GST_DEBUG ("need frame");

        state =
            gst_video_decoder_get_output_state (GST_VIDEO_DECODER (schro_dec));
        outbuf =
            gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER
            (schro_dec));
        schro_frame = gst_schro_buffer_wrap (outbuf, TRUE, &state->info);
        schro_decoder_add_output_picture (schro_dec->decoder, schro_frame);
        gst_video_codec_state_unref (state);
        break;
      }
      case SCHRO_DECODER_OK:
      {
        SchroFrame *schro_frame;
        SchroTag *tag;
        GstVideoCodecFrame *frame;

        GST_DEBUG ("got frame");

        tag = schro_decoder_get_picture_tag (schro_dec->decoder);
        schro_frame = schro_decoder_pull (schro_dec->decoder);
        frame = tag->value;

        if (schro_frame) {
          if ((frame->output_buffer = gst_schro_frame_get_buffer (schro_frame))) {
            GstFlowReturn flow_ret;

            flow_ret =
                gst_video_decoder_finish_frame (GST_VIDEO_DECODER
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
gst_schro_dec_handle_frame (GstVideoDecoder * base_video_decoder,
    GstVideoCodecFrame * frame)
{
  GstSchroDec *schro_dec;
  SchroBuffer *input_buffer;

  schro_dec = GST_SCHRO_DEC (base_video_decoder);

  GST_DEBUG ("handle frame");

  input_buffer = gst_schro_wrap_gst_buffer (frame->input_buffer);
  frame->input_buffer = NULL;

  input_buffer->tag = schro_tag_new (frame, NULL);

  schro_decoder_autoparse_push (schro_dec->decoder, input_buffer);

  return gst_schro_dec_process (schro_dec, FALSE);
}

gboolean
gst_schro_dec_finish (GstVideoDecoder * base_video_decoder)
{
  GstSchroDec *schro_dec;

  schro_dec = GST_SCHRO_DEC (base_video_decoder);

  GST_DEBUG ("finish");

  schro_decoder_autoparse_push_end_of_sequence (schro_dec->decoder);

  return gst_schro_dec_process (schro_dec, TRUE);
}

static gboolean
gst_schro_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}
