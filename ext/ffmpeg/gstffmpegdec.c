/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <assert.h>
#include <string.h>

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <libavcodec/avcodec.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

typedef struct _GstFFMpegDec GstFFMpegDec;

#define MAX_TS_MASK 0xff

/* for each incomming buffer we keep all timing info in a structure like this.
 * We keep a circular array of these structures around to store the timing info.
 * The index in the array is what we pass as opaque data (to pictures) and
 * pts (to parsers) so that ffmpeg can remember them for us. */
typedef struct
{
  gint idx;
  GstClockTime timestamp;
  GstClockTime duration;
  gint64 offset;
} GstTSInfo;

struct _GstFFMpegDec
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  /* decoding */
  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
  GstBufferPool *pool;

  /* from incoming caps */
  gint in_width;
  gint in_height;
  gint in_par_n;
  gint in_par_d;
  gint in_fps_n;
  gint in_fps_d;

  /* current context */
  enum PixelFormat ctx_pix_fmt;
  gint ctx_width;
  gint ctx_height;
  gint ctx_par_n;
  gint ctx_par_d;
  gint ctx_ticks;
  gint ctx_time_d;
  gint ctx_time_n;
  gint ctx_interlaced;

  /* current output format */
  GstVideoInfo out_info;

  union
  {
    struct
    {
      gint channels;
      gint samplerate;
      gint depth;

      GstAudioChannelPosition ffmpeg_layout[64], gst_layout[64];
    } audio;
  } format;


  gboolean waiting_for_key;
  gboolean discont;
  gboolean clear_ts;

  /* for tracking DTS/PTS */
  gboolean has_b_frames;
  gboolean reordered_in;
  GstClockTime last_in;
  GstClockTime last_diff;
  guint last_frames;
  gboolean reordered_out;
  GstClockTime last_out;
  GstClockTime next_out;

  /* parsing */
  gboolean turnoff_parser;      /* used for turning off aac raw parsing
                                 * See bug #566250 */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;
  guint8 *padded;
  guint padded_size;

  gboolean current_dr;          /* if direct rendering is enabled */

  /* some properties */
  enum AVDiscard skip_frame;
  gint lowres;
  gboolean direct_rendering;
  gboolean debug_mv;
  int max_threads;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
  gint64 processed;
  gint64 dropped;

  /* clipping segment */
  GstSegment segment;

  gboolean is_realvideo;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  /* reverse playback queue */
  GList *queued;

  /* prevent reopening the decoder on GST_EVENT_CAPS when caps are same as last time. */
  GstCaps *last_caps;
};

typedef struct _GstFFMpegDecClass GstFFMpegDecClass;

struct _GstFFMpegDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

#define GST_TS_INFO_NONE &ts_info_none
static const GstTSInfo ts_info_none = { -1, -1, -1, -1 };

static const GstTSInfo *
gst_ts_info_store (GstFFMpegDec * dec, GstClockTime timestamp,
    GstClockTime duration, gint64 offset)
{
  gint idx = dec->ts_idx;
  dec->ts_info[idx].idx = idx;
  dec->ts_info[idx].timestamp = timestamp;
  dec->ts_info[idx].duration = duration;
  dec->ts_info[idx].offset = offset;
  dec->ts_idx = (idx + 1) & MAX_TS_MASK;

  return &dec->ts_info[idx];
}

static const GstTSInfo *
gst_ts_info_get (GstFFMpegDec * dec, gint idx)
{
  if (G_UNLIKELY (idx < 0 || idx > MAX_TS_MASK))
    return GST_TS_INFO_NONE;

  return &dec->ts_info[idx];
}

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegDec))
#define GST_FFMPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

#define DEFAULT_LOWRES			0
#define DEFAULT_SKIPFRAME		0
#define DEFAULT_DIRECT_RENDERING	TRUE
#define DEFAULT_DEBUG_MV		FALSE
#define DEFAULT_MAX_THREADS		1

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DEBUG_MV,
  PROP_MAX_THREADS,
  PROP_LAST
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegdec_base_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_class_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec);
static void gst_ffmpegdec_finalize (GObject * object);

static gboolean gst_ffmpegdec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_ffmpegdec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_ffmpegdec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ffmpegdec_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_ffmpegdec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static GstStateChangeReturn gst_ffmpegdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_ffmpegdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegdec_video_negotiate (GstFFMpegDec * ffmpegdec,
    gboolean force);
static gboolean gst_ffmpegdec_audio_negotiate (GstFFMpegDec * ffmpegdec,
    gboolean force);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegdec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegdec_release_buffer (AVCodecContext * context,
    AVFrame * picture);

static void gst_ffmpegdec_drain (GstFFMpegDec * ffmpegdec);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("ffdec-params")

static GstElementClass *parent_class = NULL;

#define GST_FFMPEGDEC_TYPE_LOWRES (gst_ffmpegdec_lowres_get_type())
static GType
gst_ffmpegdec_lowres_get_type (void)
{
  static GType ffmpegdec_lowres_type = 0;

  if (!ffmpegdec_lowres_type) {
    static const GEnumValue ffmpegdec_lowres[] = {
      {0, "0", "full"},
      {1, "1", "1/2-size"},
      {2, "2", "1/4-size"},
      {0, NULL, NULL},
    };

    ffmpegdec_lowres_type =
        g_enum_register_static ("GstFFMpegDecLowres", ffmpegdec_lowres);
  }

  return ffmpegdec_lowres_type;
}

#define GST_FFMPEGDEC_TYPE_SKIPFRAME (gst_ffmpegdec_skipframe_get_type())
static GType
gst_ffmpegdec_skipframe_get_type (void)
{
  static GType ffmpegdec_skipframe_type = 0;

  if (!ffmpegdec_skipframe_type) {
    static const GEnumValue ffmpegdec_skipframe[] = {
      {0, "0", "Skip nothing"},
      {1, "1", "Skip B-frames"},
      {2, "2", "Skip IDCT/Dequantization"},
      {5, "5", "Skip everything"},
      {0, NULL, NULL},
    };

    ffmpegdec_skipframe_type =
        g_enum_register_static ("GstFFMpegDecSkipFrame", ffmpegdec_skipframe);
  }

  return ffmpegdec_skipframe_type;
}

static void
gst_ffmpegdec_base_init (GstFFMpegDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  AVCodec *in_plugin;
  gchar *longname, *classification, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFDEC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("FFmpeg %s decoder", in_plugin->long_name);
  classification = g_strdup_printf ("Codec/Decoder/%s",
      (in_plugin->type == AVMEDIA_TYPE_VIDEO) ? "Video" : "Audio");
  description = g_strdup_printf ("FFmpeg %s decoder", in_plugin->name);
  gst_element_class_set_details_simple (element_class, longname, classification,
      description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>");
  g_free (longname);
  g_free (classification);
  g_free (description);

  /* get the caps */
  sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for decoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }
  if (in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    srccaps = gst_caps_from_string ("video/x-raw");
  } else {
    srccaps = gst_ffmpeg_codectype_to_audio_caps (NULL,
        in_plugin->id, FALSE, in_plugin);
  }
  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for decoder '%s'", in_plugin->name);
    srccaps = gst_caps_from_string ("unknown/unknown");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
}

static void
gst_ffmpegdec_class_init (GstFFMpegDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ffmpegdec_finalize;

  gobject_class->set_property = gst_ffmpegdec_set_property;
  gobject_class->get_property = gst_ffmpegdec_get_property;

  if (klass->in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    int caps;

    g_object_class_install_property (gobject_class, PROP_SKIPFRAME,
        g_param_spec_enum ("skip-frame", "Skip frames",
            "Which types of frames to skip during decoding",
            GST_FFMPEGDEC_TYPE_SKIPFRAME, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, PROP_LOWRES,
        g_param_spec_enum ("lowres", "Low resolution",
            "At which resolution to decode images", GST_FFMPEGDEC_TYPE_LOWRES,
            0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, PROP_DIRECT_RENDERING,
        g_param_spec_boolean ("direct-rendering", "Direct Rendering",
            "Enable direct rendering", DEFAULT_DIRECT_RENDERING,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, PROP_DEBUG_MV,
        g_param_spec_boolean ("debug-mv", "Debug motion vectors",
            "Whether ffmpeg should print motion vectors on top of the image",
            DEFAULT_DEBUG_MV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    caps = klass->in_plugin->capabilities;
    if (caps & (CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS)) {
      g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_THREADS,
          g_param_spec_int ("max-threads", "Maximum decode threads",
              "Maximum number of worker threads to spawn. (0 = auto)",
              0, G_MAXINT, DEFAULT_MAX_THREADS,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    }
  }

  gstelement_class->change_state = gst_ffmpegdec_change_state;
}

static void
gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_event_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_sink_event));
  gst_pad_set_query_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_sink_query));
  gst_pad_set_chain_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_chain));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegdec->srcpad);
  gst_pad_set_event_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_src_event));
  gst_pad_set_query_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_src_query));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context ();
  ffmpegdec->picture = avcodec_alloc_frame ();
  ffmpegdec->pctx = NULL;
  ffmpegdec->pcache = NULL;
  ffmpegdec->opened = FALSE;
  ffmpegdec->waiting_for_key = TRUE;
  ffmpegdec->skip_frame = ffmpegdec->lowres = 0;
  ffmpegdec->direct_rendering = DEFAULT_DIRECT_RENDERING;
  ffmpegdec->debug_mv = DEFAULT_DEBUG_MV;
  ffmpegdec->max_threads = DEFAULT_MAX_THREADS;

  gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
}

static void
gst_ffmpegdec_finalize (GObject * object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  if (ffmpegdec->context != NULL)
    av_free (ffmpegdec->context);

  if (ffmpegdec->picture != NULL)
    av_free (ffmpegdec->picture);

  if (ffmpegdec->pool)
    gst_object_unref (ffmpegdec->pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ffmpegdec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFFMpegDec *ffmpegdec;
  gboolean res = FALSE;

  ffmpegdec = (GstFFMpegDec *) parent;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GST_DEBUG_OBJECT (ffmpegdec, "latency query %d",
          ffmpegdec->context->has_b_frames);
      if ((res = gst_pad_peer_query (ffmpegdec->sinkpad, query))) {
        if (ffmpegdec->context->has_b_frames) {
          gboolean live;
          GstClockTime min_lat, max_lat, our_lat;

          gst_query_parse_latency (query, &live, &min_lat, &max_lat);
          if (ffmpegdec->out_info.fps_n > 0)
            our_lat =
                gst_util_uint64_scale_int (ffmpegdec->context->has_b_frames *
                GST_SECOND, ffmpegdec->out_info.fps_d,
                ffmpegdec->out_info.fps_n);
          else
            our_lat =
                gst_util_uint64_scale_int (ffmpegdec->context->has_b_frames *
                GST_SECOND, 1, 25);
          if (min_lat != -1)
            min_lat += our_lat;
          if (max_lat != -1)
            max_lat += our_lat;
          gst_query_set_latency (query, live, min_lat, max_lat);
        }
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
gst_ffmpegdec_reset_ts (GstFFMpegDec * ffmpegdec)
{
  ffmpegdec->last_in = GST_CLOCK_TIME_NONE;
  ffmpegdec->last_diff = GST_CLOCK_TIME_NONE;
  ffmpegdec->last_frames = 0;
  ffmpegdec->last_out = GST_CLOCK_TIME_NONE;
  ffmpegdec->next_out = GST_CLOCK_TIME_NONE;
  ffmpegdec->reordered_in = FALSE;
  ffmpegdec->reordered_out = FALSE;
}

static void
gst_ffmpegdec_update_qos (GstFFMpegDec * ffmpegdec, gdouble proportion,
    GstClockTime timestamp)
{
  GST_LOG_OBJECT (ffmpegdec, "update QOS: %f, %" GST_TIME_FORMAT,
      proportion, GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (ffmpegdec);
  ffmpegdec->proportion = proportion;
  ffmpegdec->earliest_time = timestamp;
  GST_OBJECT_UNLOCK (ffmpegdec);
}

static void
gst_ffmpegdec_reset_qos (GstFFMpegDec * ffmpegdec)
{
  gst_ffmpegdec_update_qos (ffmpegdec, 0.5, GST_CLOCK_TIME_NONE);
  ffmpegdec->processed = 0;
  ffmpegdec->dropped = 0;
}

static void
gst_ffmpegdec_read_qos (GstFFMpegDec * ffmpegdec, gdouble * proportion,
    GstClockTime * timestamp)
{
  GST_OBJECT_LOCK (ffmpegdec);
  *proportion = ffmpegdec->proportion;
  *timestamp = ffmpegdec->earliest_time;
  GST_OBJECT_UNLOCK (ffmpegdec);
}

static gboolean
gst_ffmpegdec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec;
  gboolean res;

  ffmpegdec = (GstFFMpegDec *) parent;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      GstQOSType type;
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

      /* update our QoS values */
      gst_ffmpegdec_update_qos (ffmpegdec, proportion, timestamp + diff);

      /* forward upstream */
      res = gst_pad_push_event (ffmpegdec->sinkpad, event);
      break;
    }
    default:
      /* forward upstream */
      res = gst_pad_push_event (ffmpegdec->sinkpad, event);
      break;
  }

  return res;
}

/* with LOCK */
static void
gst_ffmpegdec_close (GstFFMpegDec * ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  GST_LOG_OBJECT (ffmpegdec, "closing ffmpeg codec");

  gst_caps_replace (&ffmpegdec->last_caps, NULL);

  if (ffmpegdec->context->priv_data)
    gst_ffmpeg_avcodec_close (ffmpegdec->context);
  ffmpegdec->opened = FALSE;

  if (ffmpegdec->context->palctrl) {
    av_free (ffmpegdec->context->palctrl);
    ffmpegdec->context->palctrl = NULL;
  }

  if (ffmpegdec->context->extradata) {
    av_free (ffmpegdec->context->extradata);
    ffmpegdec->context->extradata = NULL;
  }

  if (ffmpegdec->pctx) {
    if (ffmpegdec->pcache) {
      gst_buffer_unref (ffmpegdec->pcache);
      ffmpegdec->pcache = NULL;
    }
    av_parser_close (ffmpegdec->pctx);
    ffmpegdec->pctx = NULL;
  }
}

/* with LOCK */
static gboolean
gst_ffmpegdec_open (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  ffmpegdec->opened = TRUE;
  ffmpegdec->is_realvideo = FALSE;

  GST_LOG_OBJECT (ffmpegdec, "Opened ffmpeg codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  /* open a parser if we can */
  switch (oclass->in_plugin->id) {
    case CODEC_ID_MPEG4:
    case CODEC_ID_MJPEG:
    case CODEC_ID_VC1:
      GST_LOG_OBJECT (ffmpegdec, "not using parser, blacklisted codec");
      ffmpegdec->pctx = NULL;
      break;
    case CODEC_ID_H264:
      /* For H264, only use a parser if there is no context data, if there is, 
       * we're talking AVC */
      if (ffmpegdec->context->extradata_size == 0) {
        GST_LOG_OBJECT (ffmpegdec, "H264 with no extradata, creating parser");
        ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
      } else {
        GST_LOG_OBJECT (ffmpegdec,
            "H264 with extradata implies framed data - not using parser");
        ffmpegdec->pctx = NULL;
      }
      break;
    case CODEC_ID_RV10:
    case CODEC_ID_RV30:
    case CODEC_ID_RV20:
    case CODEC_ID_RV40:
      ffmpegdec->is_realvideo = TRUE;
      break;
    default:
      if (!ffmpegdec->turnoff_parser) {
        ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
        if (ffmpegdec->pctx)
          GST_LOG_OBJECT (ffmpegdec, "Using parser %p", ffmpegdec->pctx);
        else
          GST_LOG_OBJECT (ffmpegdec, "No parser for codec");
      } else {
        GST_LOG_OBJECT (ffmpegdec, "Parser deactivated for format");
      }
      break;
  }

  switch (oclass->in_plugin->type) {
    case AVMEDIA_TYPE_VIDEO:
      /* clear values */
      ffmpegdec->ctx_pix_fmt = PIX_FMT_NB;
      ffmpegdec->ctx_width = 0;
      ffmpegdec->ctx_height = 0;
      ffmpegdec->ctx_ticks = 1;
      ffmpegdec->ctx_time_n = 0;
      ffmpegdec->ctx_time_d = 0;
      ffmpegdec->ctx_par_n = 0;
      ffmpegdec->ctx_par_d = 0;
      break;
    case AVMEDIA_TYPE_AUDIO:
      ffmpegdec->format.audio.samplerate = 0;
      ffmpegdec->format.audio.channels = 0;
      ffmpegdec->format.audio.depth = 0;
      break;
    default:
      break;
  }

  gst_ffmpegdec_reset_ts (ffmpegdec);
  /* FIXME, reset_qos will take the LOCK and this function is already called
   * with the LOCK */
  ffmpegdec->proportion = 0.5;
  ffmpegdec->earliest_time = -1;

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegdec_close (ffmpegdec);
    GST_DEBUG_OBJECT (ffmpegdec, "ffdec_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static gboolean
gst_ffmpegdec_setcaps (GstFFMpegDec * ffmpegdec, GstCaps * caps)
{
  GstFFMpegDecClass *oclass;
  GstStructure *structure;
  const GValue *par;
  const GValue *fps;
  gboolean ret = TRUE;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);

  /* stupid check for VC1 */
  if ((oclass->in_plugin->id == CODEC_ID_WMV3) ||
      (oclass->in_plugin->id == CODEC_ID_VC1))
    oclass->in_plugin->id = gst_ffmpeg_caps_to_codecid (caps, NULL);

  /* close old session */
  if (ffmpegdec->opened) {
    GST_OBJECT_UNLOCK (ffmpegdec);
    gst_ffmpegdec_drain (ffmpegdec);
    GST_OBJECT_LOCK (ffmpegdec);
    gst_ffmpegdec_close (ffmpegdec);

    /* and reset the defaults that were set when a context is created */
    avcodec_get_context_defaults (ffmpegdec->context);
  }

  /* set buffer functions */
  if (oclass->in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
    ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;
    ffmpegdec->context->draw_horiz_band = NULL;
  }

  /* default is to let format decide if it needs a parser */
  ffmpegdec->turnoff_parser = FALSE;

  ffmpegdec->has_b_frames = FALSE;

  GST_LOG_OBJECT (ffmpegdec, "size %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, caps, ffmpegdec->context);

  GST_LOG_OBJECT (ffmpegdec, "size after %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  if (!ffmpegdec->context->time_base.den || !ffmpegdec->context->time_base.num) {
    GST_DEBUG_OBJECT (ffmpegdec, "forcing 25/1 framerate");
    ffmpegdec->context->time_base.num = 1;
    ffmpegdec->context->time_base.den = 25;
  }

  /* get pixel aspect ratio if it's set */
  structure = gst_caps_get_structure (caps, 0);

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par != NULL && GST_VALUE_HOLDS_FRACTION (par)) {
    ffmpegdec->in_par_n = gst_value_get_fraction_numerator (par);
    ffmpegdec->in_par_d = gst_value_get_fraction_denominator (par);
    GST_DEBUG_OBJECT (ffmpegdec, "sink caps have pixel-aspect-ratio of %d:%d",
        ffmpegdec->in_par_n, ffmpegdec->in_par_d);
  } else {
    GST_DEBUG_OBJECT (ffmpegdec, "no input pixel-aspect-ratio");
    ffmpegdec->in_par_n = 0;
    ffmpegdec->in_par_d = 0;
  }

  /* get the framerate from incoming caps. fps_n is set to 0 when
   * there is no valid framerate */
  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    ffmpegdec->in_fps_n = gst_value_get_fraction_numerator (fps);
    ffmpegdec->in_fps_d = gst_value_get_fraction_denominator (fps);
    GST_DEBUG_OBJECT (ffmpegdec, "sink caps have framerate of %d/%d",
        ffmpegdec->in_fps_n, ffmpegdec->in_fps_d);
  } else {
    GST_DEBUG_OBJECT (ffmpegdec, "no input framerate ");
    ffmpegdec->in_fps_n = 0;
    ffmpegdec->in_fps_d = 0;
  }

  /* for AAC we only use av_parse if not on stream-format==raw or ==loas */
  if (oclass->in_plugin->id == CODEC_ID_AAC
      || oclass->in_plugin->id == CODEC_ID_AAC_LATM) {
    const gchar *format = gst_structure_get_string (structure, "stream-format");

    if (format == NULL || strcmp (format, "raw") == 0) {
      ffmpegdec->turnoff_parser = TRUE;
    }
  }

  /* for FLAC, don't parse if it's already parsed */
  if (oclass->in_plugin->id == CODEC_ID_FLAC) {
    if (gst_structure_has_field (structure, "streamheader"))
      ffmpegdec->turnoff_parser = TRUE;
  }

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->error_recognition = 1;

  /* for slow cpus */
  ffmpegdec->context->lowres = ffmpegdec->lowres;
  ffmpegdec->context->skip_frame = ffmpegdec->skip_frame;

  /* ffmpeg can draw motion vectors on top of the image (not every decoder
   * supports it) */
  ffmpegdec->context->debug_mv = ffmpegdec->debug_mv;

  if (ffmpegdec->max_threads == 0) {
    if (!(oclass->in_plugin->capabilities & CODEC_CAP_AUTO_THREADS))
      ffmpegdec->context->thread_count = gst_ffmpeg_auto_max_threads ();
    else
      ffmpegdec->context->thread_count = 0;
  } else
    ffmpegdec->context->thread_count = ffmpegdec->max_threads;

  ffmpegdec->context->thread_type = FF_THREAD_SLICE;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegdec_open (ffmpegdec))
    goto open_failed;

  /* clipping region. take into account the lowres property */
  if (gst_structure_get_int (structure, "width", &ffmpegdec->in_width))
    ffmpegdec->in_width >>= ffmpegdec->lowres;
  else
    ffmpegdec->in_width = -1;

  if (gst_structure_get_int (structure, "height", &ffmpegdec->in_height))
    ffmpegdec->in_height >>= ffmpegdec->lowres;
  else
    ffmpegdec->in_height = -1;

  GST_DEBUG_OBJECT (ffmpegdec, "clipping to %dx%d",
      ffmpegdec->in_width, ffmpegdec->in_height);

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    ret = FALSE;
    goto done;
  }
}

static void
gst_ffmpegdec_fill_picture (GstFFMpegDec * ffmpegdec, GstVideoFrame * frame,
    AVFrame * picture)
{
  guint i;

  /* setup data pointers and strides */
  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
    picture->data[i] = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    picture->linesize[i] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);

    GST_LOG_OBJECT (ffmpegdec, "plane %d: data %p, linesize %d", i,
        picture->data[i], picture->linesize[i]);
  }
}

/* called when ffmpeg wants us to allocate a buffer to write the decoded frame
 * into. We try to give it memory from our pool */
static int
gst_ffmpegdec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstBuffer *buf = NULL;
  GstFFMpegDec *ffmpegdec;
  GstFlowReturn ret;
  GstVideoFrame frame;

  ffmpegdec = (GstFFMpegDec *) context->opaque;

  ffmpegdec->context->pix_fmt = context->pix_fmt;

  GST_DEBUG_OBJECT (ffmpegdec, "getting buffer");

  /* apply the last info we have seen to this picture, when we get the
   * picture back from ffmpeg we can use this to correctly timestamp the output
   * buffer */
  picture->reordered_opaque = context->reordered_opaque;
  /* make sure we don't free the buffer when it's not ours */
  picture->opaque = NULL;

  /* see if we need renegotiation */
  if (G_UNLIKELY (!gst_ffmpegdec_video_negotiate (ffmpegdec, FALSE)))
    goto negotiate_failed;

  if (!ffmpegdec->current_dr)
    goto no_dr;

  /* alloc with aligned dimensions for ffmpeg */
  GST_LOG_OBJECT (ffmpegdec, "doing alloc from pool");
  ret = gst_buffer_pool_acquire_buffer (ffmpegdec->pool, &buf, NULL);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto alloc_failed;

  if (!gst_video_frame_map (&frame, &ffmpegdec->out_info, buf,
          GST_MAP_READWRITE))
    goto invalid_frame;

  gst_ffmpegdec_fill_picture (ffmpegdec, &frame, picture);

  /* tell ffmpeg we own this buffer, tranfer the ref we have on the buffer to
   * the opaque data. */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = 256 * 256 * 256 * 64;
  picture->opaque = g_slice_dup (GstVideoFrame, &frame);

  GST_LOG_OBJECT (ffmpegdec, "returned buffer %p in frame %p", buf,
      picture->opaque);

  return 0;

  /* fallbacks */
negotiate_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "negotiate failed");
    goto fallback;
  }
no_dr:
  {
    GST_LOG_OBJECT (ffmpegdec, "direct rendering disabled, fallback alloc");
    goto fallback;
  }
alloc_failed:
  {
    /* alloc default buffer when we can't get one from downstream */
    GST_LOG_OBJECT (ffmpegdec, "alloc failed, fallback alloc");
    goto fallback;
  }
invalid_frame:
  {
    /* alloc default buffer when we can't get one from downstream */
    GST_LOG_OBJECT (ffmpegdec, "failed to map frame, fallback alloc");
    gst_buffer_unref (buf);
    goto fallback;
  }
fallback:
  {
    return avcodec_default_get_buffer (context, picture);
  }
}

/* called when ffmpeg is done with our buffer */
static void
gst_ffmpegdec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstBuffer *buf;
  GstFFMpegDec *ffmpegdec;
  GstVideoFrame *frame;

  ffmpegdec = (GstFFMpegDec *) context->opaque;

  /* check if it was our buffer */
  if (picture->opaque == NULL) {
    GST_DEBUG_OBJECT (ffmpegdec, "default release buffer");
    avcodec_default_release_buffer (context, picture);
    return;
  }

  /* we remove the opaque data now */
  frame = picture->opaque;
  picture->opaque = NULL;

  /* unmap buffer data */
  gst_video_frame_unmap (frame);
  buf = frame->buffer;

  GST_DEBUG_OBJECT (ffmpegdec, "release buffer %p in frame %p", buf, frame);

  g_slice_free (GstVideoFrame, frame);
  gst_buffer_unref (buf);

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}

static void
gst_ffmpegdec_update_par (GstFFMpegDec * ffmpegdec, gint * par_n, gint * par_d)
{
  gboolean demuxer_par_set = FALSE;
  gboolean decoder_par_set = FALSE;
  gint demuxer_num = 1, demuxer_denom = 1;
  gint decoder_num = 1, decoder_denom = 1;

  GST_OBJECT_LOCK (ffmpegdec);

  if (ffmpegdec->in_par_n && ffmpegdec->in_par_d) {
    demuxer_num = ffmpegdec->in_par_n;
    demuxer_denom = ffmpegdec->in_par_d;
    demuxer_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Demuxer PAR: %d:%d", demuxer_num,
        demuxer_denom);
  }

  if (ffmpegdec->ctx_par_n && ffmpegdec->ctx_par_d) {
    decoder_num = ffmpegdec->ctx_par_n;
    decoder_denom = ffmpegdec->ctx_par_d;
    decoder_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Decoder PAR: %d:%d", decoder_num,
        decoder_denom);
  }

  GST_OBJECT_UNLOCK (ffmpegdec);

  if (!demuxer_par_set && !decoder_par_set)
    goto no_par;

  if (demuxer_par_set && !decoder_par_set)
    goto use_demuxer_par;

  if (decoder_par_set && !demuxer_par_set)
    goto use_decoder_par;

  /* Both the demuxer and the decoder provide a PAR. If one of
   * the two PARs is 1:1 and the other one is not, use the one
   * that is not 1:1. */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom)
    goto use_decoder_par;

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom)
    goto use_demuxer_par;

  /* Both PARs are non-1:1, so use the PAR provided by the demuxer */
  goto use_demuxer_par;

use_decoder_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Setting decoder provided pixel-aspect-ratio of %u:%u", decoder_num,
        decoder_denom);
    *par_n = decoder_num;
    *par_d = decoder_denom;
    return;
  }

use_demuxer_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Setting demuxer provided pixel-aspect-ratio of %u:%u", demuxer_num,
        demuxer_denom);
    *par_n = demuxer_num;
    *par_d = demuxer_denom;
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Neither demuxer nor codec provide a pixel-aspect-ratio");
    *par_n = 1;
    *par_d = 1;
    return;
  }
}

static gboolean
gst_ffmpegdec_bufferpool (GstFFMpegDec * ffmpegdec, GstCaps * caps)
{
  GstQuery *query;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  guint edge;
  AVCodecContext *context = ffmpegdec->context;
  gboolean have_videometa, have_alignment;
  GstAllocationParams params = { 0, 0, 0, 15, };

  GST_DEBUG_OBJECT (ffmpegdec, "setting up bufferpool");

  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (caps, TRUE);

  if (gst_pad_peer_query (ffmpegdec->srcpad, query)) {
    have_videometa =
        gst_query_has_allocation_meta (query, GST_VIDEO_META_API_TYPE);
  } else {
    /* use query defaults */
    GST_DEBUG_OBJECT (ffmpegdec, "peer query failed, using defaults");
    have_videometa = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    size = MAX (size, ffmpegdec->out_info.size);
  } else {
    pool = NULL;
    size = ffmpegdec->out_info.size;
    min = max = 0;
  }

  gst_query_unref (query);

  if (pool == NULL) {
    /* we did not get a pool, make one ourselves then */
    pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  /* we are happy with the default allocator but we would like to have 16 bytes
   * aligned memory */
  gst_buffer_pool_config_set_allocator (config, NULL, &params);

  have_alignment =
      gst_buffer_pool_has_option (pool, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  /* we can only enable the alignment if downstream supports the
   * videometa api */
  if (have_alignment && have_videometa) {
    GstVideoAlignment align;
    gint width, height;
    gint linesize_align[4];
    gint i;

    width = ffmpegdec->ctx_width;
    height = ffmpegdec->ctx_height;
    /* let ffmpeg find the alignment and padding */
    avcodec_align_dimensions2 (context, &width, &height, linesize_align);
    edge = context->flags & CODEC_FLAG_EMU_EDGE ? 0 : avcodec_get_edge_width ();
    /* increase the size for the padding */
    width += edge << 1;
    height += edge << 1;

    align.padding_top = edge;
    align.padding_left = edge;
    align.padding_right = width - ffmpegdec->ctx_width - edge;
    align.padding_bottom = height - ffmpegdec->ctx_height - edge;
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
      align.stride_align[i] =
          (linesize_align[i] > 0 ? linesize_align[i] - 1 : 0);

    GST_DEBUG_OBJECT (ffmpegdec, "aligned dimension %dx%d -> %dx%d "
        "padding t:%u l:%u r:%u b:%u, stride_align %d:%d:%d:%d",
        ffmpegdec->ctx_width, ffmpegdec->ctx_height, width, height,
        align.padding_top, align.padding_left, align.padding_right,
        align.padding_bottom, align.stride_align[0], align.stride_align[1],
        align.stride_align[2], align.stride_align[3]);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    if (ffmpegdec->direct_rendering) {
      GstFFMpegDecClass *oclass;

      GST_DEBUG_OBJECT (ffmpegdec, "trying to enable direct rendering");

      oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

      if (oclass->in_plugin->capabilities & CODEC_CAP_DR1) {
        GST_DEBUG_OBJECT (ffmpegdec, "enabled direct rendering");
        ffmpegdec->current_dr = TRUE;
      } else {
        GST_DEBUG_OBJECT (ffmpegdec, "direct rendering not supported");
      }
    }
  } else {
    GST_DEBUG_OBJECT (ffmpegdec,
        "alignment or videometa not supported, disable direct rendering");
    /* disable direct rendering. This will make us use the fallback ffmpeg
     * picture allocation code with padding etc. We will then do the final
     * copy (with cropping) into a buffer from our pool */
    ffmpegdec->current_dr = FALSE;
  }

  /* and store */
  gst_buffer_pool_set_config (pool, config);

  if (ffmpegdec->pool) {
    gst_buffer_pool_set_active (ffmpegdec->pool, FALSE);
    gst_object_unref (ffmpegdec->pool);
  }
  ffmpegdec->pool = pool;

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

  return TRUE;
}

static gboolean
update_video_context (GstFFMpegDec * ffmpegdec, gboolean force)
{
  AVCodecContext *context = ffmpegdec->context;

  if (!force && ffmpegdec->ctx_width == context->width
      && ffmpegdec->ctx_height == context->height
      && ffmpegdec->ctx_ticks == context->ticks_per_frame
      && ffmpegdec->ctx_time_n == context->time_base.num
      && ffmpegdec->ctx_time_d == context->time_base.den
      && ffmpegdec->ctx_pix_fmt == context->pix_fmt
      && ffmpegdec->ctx_par_n == context->sample_aspect_ratio.num
      && ffmpegdec->ctx_par_d == context->sample_aspect_ratio.den)
    return FALSE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating video from %dx%d@ %d:%d PAR %d/%d fps to %dx%d@ %d:%d PAR %d/%d fps pixfmt %d",
      ffmpegdec->ctx_width, ffmpegdec->ctx_height,
      ffmpegdec->ctx_par_n, ffmpegdec->ctx_par_d,
      ffmpegdec->ctx_time_n, ffmpegdec->ctx_time_d,
      context->width, context->height,
      context->sample_aspect_ratio.num,
      context->sample_aspect_ratio.den,
      context->time_base.num, context->time_base.den, context->pix_fmt);

  ffmpegdec->ctx_width = context->width;
  ffmpegdec->ctx_height = context->height;
  ffmpegdec->ctx_ticks = context->ticks_per_frame;
  ffmpegdec->ctx_time_n = context->time_base.num;
  ffmpegdec->ctx_time_d = context->time_base.den;
  ffmpegdec->ctx_pix_fmt = context->pix_fmt;
  ffmpegdec->ctx_par_n = context->sample_aspect_ratio.num;
  ffmpegdec->ctx_par_d = context->sample_aspect_ratio.den;

  return TRUE;
}

static gboolean
gst_ffmpegdec_video_negotiate (GstFFMpegDec * ffmpegdec, gboolean force)
{
  GstFFMpegDecClass *oclass;
  GstCaps *caps;
  gint width, height;
  gint fps_n, fps_d;
  GstVideoInfo info;
  GstVideoFormat fmt;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  force |= gst_pad_check_reconfigure (ffmpegdec->srcpad);

  /* first check if anything changed */
  if (!update_video_context (ffmpegdec, force))
    return TRUE;

  /* now we're going to construct the video info for the final output
   * format */
  gst_video_info_init (&info);

  fmt = gst_ffmpeg_pixfmt_to_video_format (ffmpegdec->ctx_pix_fmt);
  if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
    goto unknown_format;

  /* determine the width and height, start with the dimension of the
   * context */
  width = ffmpegdec->ctx_width;
  height = ffmpegdec->ctx_height;

  /* if there is a width/height specified in the input, use that */
  if (ffmpegdec->in_width != -1 && ffmpegdec->in_width < width)
    width = ffmpegdec->in_width;
  if (ffmpegdec->in_height != -1 && ffmpegdec->in_height < height)
    height = ffmpegdec->in_height;

  /* now store the values */
  gst_video_info_set_format (&info, fmt, width, height);

  /* set the interlaced flag */
  if (ffmpegdec->ctx_interlaced)
    info.interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
  else
    info.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  /* try to find a good framerate */
  if (ffmpegdec->in_fps_d) {
    /* take framerate from input when it was specified (#313970) */
    fps_n = ffmpegdec->in_fps_n;
    fps_d = ffmpegdec->in_fps_d;
  } else {
    fps_n = ffmpegdec->ctx_time_d / ffmpegdec->ctx_ticks;
    fps_d = ffmpegdec->ctx_time_n;

    if (!fps_d) {
      GST_LOG_OBJECT (ffmpegdec, "invalid framerate: %d/0, -> %d/1", fps_n,
          fps_n);
      fps_d = 1;
    }
    if (gst_util_fraction_compare (fps_n, fps_d, 1000, 1) > 0) {
      GST_LOG_OBJECT (ffmpegdec, "excessive framerate: %d/%d, -> 0/1", fps_n,
          fps_d);
      fps_n = 0;
      fps_d = 1;
    }
  }
  GST_LOG_OBJECT (ffmpegdec, "setting framerate: %d/%d", fps_n, fps_d);
  info.fps_n = fps_n;
  info.fps_d = fps_d;

  /* calculate and update par now */
  gst_ffmpegdec_update_par (ffmpegdec, &info.par_n, &info.par_d);

  caps = gst_video_info_to_caps (&info);

  if (!gst_pad_set_caps (ffmpegdec->srcpad, caps))
    goto caps_failed;

  ffmpegdec->out_info = info;

  /* now figure out a bufferpool */
  if (!gst_ffmpegdec_bufferpool (ffmpegdec, caps))
    goto no_bufferpool;

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
unknown_format:
  {
#ifdef HAVE_FFMPEG_UNINSTALLED
    /* using internal ffmpeg snapshot */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for FFmpeg pixfmt %d.",
            ffmpegdec->ctx_pix_fmt), (NULL));
#else
    /* using external ffmpeg */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for FFmpeg codec '%s', and "
            "you are using an external libavcodec. This is most likely due to "
            "a packaging problem and/or libavcodec having been upgraded to a "
            "version that is not compatible with this version of "
            "gstreamer-ffmpeg. Make sure your gstreamer-ffmpeg and libavcodec "
            "packages come from the same source/repository.",
            oclass->in_plugin->name), (NULL));
#endif
    return FALSE;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Could not set caps for ffmpeg decoder (%s), not fixed?",
            oclass->in_plugin->name));
    gst_caps_unref (caps);

    return FALSE;
  }
no_bufferpool:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Could not create bufferpool for fmpeg decoder (%s)",
            oclass->in_plugin->name));
    gst_caps_unref (caps);

    return FALSE;
  }
}

static gboolean
update_audio_context (GstFFMpegDec * ffmpegdec, gboolean force)
{
  AVCodecContext *context = ffmpegdec->context;
  gint depth;
  GstAudioChannelPosition pos[64] = { 0, };

  depth = av_smp_format_depth (context->sample_fmt);

  gst_ffmpeg_channel_layout_to_gst (context, pos);

  if (!force && ffmpegdec->format.audio.samplerate ==
      context->sample_rate &&
      ffmpegdec->format.audio.channels == context->channels &&
      ffmpegdec->format.audio.depth == depth &&
      memcmp (ffmpegdec->format.audio.ffmpeg_layout, pos,
          sizeof (GstAudioChannelPosition) * context->channels) == 0)
    return FALSE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating audio from %dHz@%dchannels (%d) to %dHz@%dchannels (%d)",
      ffmpegdec->format.audio.samplerate, ffmpegdec->format.audio.channels,
      ffmpegdec->format.audio.depth,
      context->sample_rate, context->channels, depth);

  ffmpegdec->format.audio.samplerate = context->sample_rate;
  ffmpegdec->format.audio.channels = context->channels;
  ffmpegdec->format.audio.depth = depth;
  memcpy (ffmpegdec->format.audio.ffmpeg_layout, pos,
      sizeof (GstAudioChannelPosition) * context->channels);

  return TRUE;
}

static gboolean
gst_ffmpegdec_audio_negotiate (GstFFMpegDec * ffmpegdec, gboolean force)
{
  GstFFMpegDecClass *oclass;
  GstCaps *caps;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (!update_audio_context (ffmpegdec, force))
    return TRUE;

  /* convert the raw output format to caps */
  caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
      ffmpegdec->context, oclass->in_plugin->id, FALSE);
  if (caps == NULL)
    goto no_caps;

  /* Get GStreamer channel layout */
  memcpy (ffmpegdec->format.audio.gst_layout,
      ffmpegdec->format.audio.ffmpeg_layout,
      sizeof (GstAudioChannelPosition) * ffmpegdec->format.audio.channels);
  gst_audio_channel_positions_to_valid_order (ffmpegdec->format.
      audio.gst_layout, ffmpegdec->format.audio.channels);

  GST_LOG_OBJECT (ffmpegdec, "output caps %" GST_PTR_FORMAT, caps);

  if (!gst_pad_set_caps (ffmpegdec->srcpad, caps))
    goto caps_failed;

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
no_caps:
  {
#ifdef HAVE_FFMPEG_UNINSTALLED
    /* using internal ffmpeg snapshot */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for FFmpeg codec '%s'.",
            oclass->in_plugin->name), (NULL));
#else
    /* using external ffmpeg */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for FFmpeg codec '%s', and "
            "you are using an external libavcodec. This is most likely due to "
            "a packaging problem and/or libavcodec having been upgraded to a "
            "version that is not compatible with this version of "
            "gstreamer-ffmpeg. Make sure your gstreamer-ffmpeg and libavcodec "
            "packages come from the same source/repository.",
            oclass->in_plugin->name), (NULL));
#endif
    return FALSE;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Could not set caps for ffmpeg decoder (%s), not fixed?",
            oclass->in_plugin->name));
    gst_caps_unref (caps);

    return FALSE;
  }
}

/* perform qos calculations before decoding the next frame.
 *
 * Sets the skip_frame flag and if things are really bad, skips to the next
 * keyframe.
 * 
 * Returns TRUE if the frame should be decoded, FALSE if the frame can be dropped
 * entirely.
 */
static gboolean
gst_ffmpegdec_do_qos (GstFFMpegDec * ffmpegdec, GstClockTime timestamp,
    gboolean * mode_switch)
{
  GstClockTimeDiff diff;
  gdouble proportion;
  GstClockTime qostime, earliest_time;
  gboolean res = TRUE;

  *mode_switch = FALSE;

  /* no timestamp, can't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp)))
    goto no_qos;

  /* get latest QoS observation values */
  gst_ffmpegdec_read_qos (ffmpegdec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    /* no skip_frame initialy */
    ffmpegdec->context->skip_frame = AVDISCARD_DEFAULT;
    goto no_qos;
  }

  /* qos is done on running time of the timestamp */
  qostime = gst_segment_to_running_time (&ffmpegdec->segment, GST_FORMAT_TIME,
      timestamp);

  /* timestamp can be out of segment, then we don't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (qostime)))
    goto no_qos;

  /* see how our next timestamp relates to the latest qos timestamp. negative
   * values mean we are early, positive values mean we are too late. */
  diff = GST_CLOCK_DIFF (qostime, earliest_time);

  GST_DEBUG_OBJECT (ffmpegdec, "QOS: qostime %" GST_TIME_FORMAT
      ", earliest %" GST_TIME_FORMAT, GST_TIME_ARGS (qostime),
      GST_TIME_ARGS (earliest_time));

  /* if we using less than 40% of the available time, we can try to
   * speed up again when we were slow. */
  if (proportion < 0.4 && diff < 0) {
    goto normal_mode;
  } else {
    if (diff >= 0) {
      /* we're too slow, try to speed up */
      if (ffmpegdec->waiting_for_key) {
        /* we were waiting for a keyframe, that's ok */
        goto skipping;
      }
      /* switch to skip_frame mode */
      goto skip_frame;
    }
  }

no_qos:
  ffmpegdec->processed++;
  return TRUE;

skipping:
  {
    res = FALSE;
    goto drop_qos;
  }
normal_mode:
  {
    if (ffmpegdec->context->skip_frame != AVDISCARD_DEFAULT) {
      ffmpegdec->context->skip_frame = AVDISCARD_DEFAULT;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec, "QOS: normal mode %g < 0.4", proportion);
    }
    ffmpegdec->processed++;
    return TRUE;
  }
skip_frame:
  {
    if (ffmpegdec->context->skip_frame != AVDISCARD_NONREF) {
      ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "QOS: hurry up, diff %" G_GINT64_FORMAT " >= 0", diff);
    }
    goto drop_qos;
  }
drop_qos:
  {
    GstClockTime stream_time, jitter;
    GstMessage *qos_msg;

    ffmpegdec->dropped++;
    stream_time =
        gst_segment_to_stream_time (&ffmpegdec->segment, GST_FORMAT_TIME,
        timestamp);
    jitter = GST_CLOCK_DIFF (qostime, earliest_time);
    qos_msg =
        gst_message_new_qos (GST_OBJECT_CAST (ffmpegdec), FALSE, qostime,
        stream_time, timestamp, GST_CLOCK_TIME_NONE);
    gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
    gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
        ffmpegdec->processed, ffmpegdec->dropped);
    gst_element_post_message (GST_ELEMENT_CAST (ffmpegdec), qos_msg);

    return res;
  }
}

/* returns TRUE if buffer is within segment, else FALSE.
 * if Buffer is on segment border, it's timestamp and duration will be clipped */
static gboolean
clip_video_buffer (GstFFMpegDec * dec, GstBuffer * buf, GstClockTime in_ts,
    GstClockTime in_dur)
{
  gboolean res = TRUE;
  guint64 cstart, cstop;
  GstClockTime stop;

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT " , duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur));

  /* can't clip without TIME segment */
  if (G_UNLIKELY (dec->segment.format != GST_FORMAT_TIME))
    goto beach;

  /* we need a start time */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (in_ts)))
    goto beach;

  /* generate valid stop, if duration unknown, we have unknown stop */
  stop =
      GST_CLOCK_TIME_IS_VALID (in_dur) ? (in_ts + in_dur) : GST_CLOCK_TIME_NONE;

  /* now clip */
  res =
      gst_segment_clip (&dec->segment, GST_FORMAT_TIME, in_ts, stop, &cstart,
      &cstop);
  if (G_UNLIKELY (!res))
    goto beach;

  /* we're pretty sure the duration of this buffer is not till the end of this
   * segment (which _clip will assume when the stop is -1) */
  if (stop == GST_CLOCK_TIME_NONE)
    cstop = GST_CLOCK_TIME_NONE;

  /* update timestamp and possibly duration if the clipped stop time is
   * valid */
  GST_BUFFER_TIMESTAMP (buf) = cstart;
  if (GST_CLOCK_TIME_IS_VALID (cstop))
    GST_BUFFER_DURATION (buf) = cstop - cstart;

  GST_LOG_OBJECT (dec,
      "clipped timestamp:%" GST_TIME_FORMAT " , duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (cstart), GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

beach:
  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;
}


/* figure out if the current picture is a keyframe, return TRUE if that is
 * the case. */
static gboolean
check_keyframe (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;
  gboolean is_itype = FALSE;
  gboolean is_reference = FALSE;
  gboolean iskeyframe;

  /* figure out if we are dealing with a keyframe */
  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* remember that we have B frames, we need this for the DTS -> PTS conversion
   * code */
  if (!ffmpegdec->has_b_frames && ffmpegdec->picture->pict_type == FF_B_TYPE) {
    GST_DEBUG_OBJECT (ffmpegdec, "we have B frames");
    ffmpegdec->has_b_frames = TRUE;
    /* Emit latency message to recalculate it */
    gst_element_post_message (GST_ELEMENT_CAST (ffmpegdec),
        gst_message_new_latency (GST_OBJECT_CAST (ffmpegdec)));
  }

  is_itype = (ffmpegdec->picture->pict_type == FF_I_TYPE);
  is_reference = (ffmpegdec->picture->reference == 1);

  iskeyframe = (is_itype || is_reference || ffmpegdec->picture->key_frame)
      || (oclass->in_plugin->id == CODEC_ID_INDEO3)
      || (oclass->in_plugin->id == CODEC_ID_MSZH)
      || (oclass->in_plugin->id == CODEC_ID_ZLIB)
      || (oclass->in_plugin->id == CODEC_ID_VP3)
      || (oclass->in_plugin->id == CODEC_ID_HUFFYUV);

  GST_LOG_OBJECT (ffmpegdec,
      "current picture: type: %d, is_keyframe:%d, is_itype:%d, is_reference:%d",
      ffmpegdec->picture->pict_type, iskeyframe, is_itype, is_reference);

  return iskeyframe;
}

/* get an outbuf buffer with the current picture */
static GstFlowReturn
get_output_buffer (GstFFMpegDec * ffmpegdec, GstBuffer ** outbuf)
{
  GstFlowReturn ret;

  if (ffmpegdec->picture->opaque != NULL) {
    GstVideoFrame *frame;

    /* we allocated a picture already for ffmpeg to decode into, let's pick it
     * up and use it now. */
    frame = ffmpegdec->picture->opaque;
    *outbuf = frame->buffer;
    GST_LOG_OBJECT (ffmpegdec, "using opaque buffer %p on frame %p", *outbuf,
        frame);
    gst_buffer_ref (*outbuf);
  } else {
    GstVideoFrame frame;
    AVPicture *src, *dest;
    AVFrame pic;
    gint width, height;
    GstBuffer *buf;

    GST_LOG_OBJECT (ffmpegdec, "allocating an output buffer");

    if (G_UNLIKELY (!gst_ffmpegdec_video_negotiate (ffmpegdec, FALSE)))
      goto negotiate_failed;

    ret = gst_buffer_pool_acquire_buffer (ffmpegdec->pool, &buf, NULL);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto alloc_failed;

    if (!gst_video_frame_map (&frame, &ffmpegdec->out_info, buf,
            GST_MAP_READWRITE))
      goto invalid_frame;

    gst_ffmpegdec_fill_picture (ffmpegdec, &frame, &pic);

    width = ffmpegdec->out_info.width;
    height = ffmpegdec->out_info.height;

    src = (AVPicture *) ffmpegdec->picture;
    dest = (AVPicture *) & pic;

    GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
        "copy picture to output buffer %dx%d", width, height);
    av_picture_copy (dest, src, ffmpegdec->context->pix_fmt, width, height);

    gst_video_frame_unmap (&frame);

    *outbuf = buf;
  }
  ffmpegdec->picture->reordered_opaque = -1;

  return GST_FLOW_OK;

  /* special cases */
negotiate_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "negotiation failed");
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "buffer alloc failed");
    return ret;
  }
invalid_frame:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "could not map frame");
    return GST_FLOW_ERROR;
  }
}

static void
clear_queued (GstFFMpegDec * ffmpegdec)
{
  g_list_foreach (ffmpegdec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (ffmpegdec->queued);
  ffmpegdec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstFFMpegDec * ffmpegdec)
{
  GstFlowReturn res = GST_FLOW_OK;

  while (ffmpegdec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (ffmpegdec->queued->data);

    GST_LOG_OBJECT (ffmpegdec, "pushing buffer %p, offset %"
        G_GUINT64_FORMAT ", timestamp %"
        GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
        GST_BUFFER_OFFSET (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* iterate ouput queue an push downstream */
    res = gst_pad_push (ffmpegdec->srcpad, buf);

    ffmpegdec->queued =
        g_list_delete_link (ffmpegdec->queued, ffmpegdec->queued);
  }
  return res;
}

static void
gst_avpacket_init (AVPacket * packet, guint8 * data, guint size)
{
  memset (packet, 0, sizeof (AVPacket));
  packet->data = data;
  packet->size = size;
}

/* gst_ffmpegdec_[video|audio]_frame:
 * ffmpegdec:
 * data: pointer to the data to decode
 * size: size of data in bytes
 * in_timestamp: incoming timestamp.
 * in_duration: incoming duration.
 * in_offset: incoming offset (frame number).
 * outbuf: outgoing buffer. Different from NULL ONLY if it contains decoded data.
 * ret: Return flow.
 *
 * Returns: number of bytes used in decoding. The check for successful decode is
 *   outbuf being non-NULL.
 */
static gint
gst_ffmpegdec_video_frame (GstFFMpegDec * ffmpegdec,
    guint8 * data, guint size,
    const GstTSInfo * dec_info, GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data;
  gboolean iskeyframe;
  gboolean mode_switch;
  gboolean decode;
  gint skip_frame = AVDISCARD_DEFAULT;
  GstClockTime out_timestamp, out_duration, out_pts;
  gint64 out_offset;
  const GstTSInfo *out_info;
  AVPacket packet;

  *ret = GST_FLOW_OK;
  *outbuf = NULL;

  ffmpegdec->context->opaque = ffmpegdec;

  /* in case we skip frames */
  ffmpegdec->picture->pict_type = -1;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  decode = gst_ffmpegdec_do_qos (ffmpegdec, dec_info->timestamp, &mode_switch);

  if (ffmpegdec->is_realvideo && data != NULL) {
    gint slice_count;
    gint i;

    /* setup the slice table for realvideo */
    if (ffmpegdec->context->slice_offset == NULL)
      ffmpegdec->context->slice_offset = g_malloc (sizeof (guint32) * 1000);

    slice_count = (*data++) + 1;
    ffmpegdec->context->slice_count = slice_count;

    for (i = 0; i < slice_count; i++) {
      data += 4;
      ffmpegdec->context->slice_offset[i] = GST_READ_UINT32_LE (data);
      data += 4;
    }
  }

  if (!decode) {
    /* no decoding needed, save previous skip_frame value and brutely skip
     * decoding everything */
    skip_frame = ffmpegdec->context->skip_frame;
    ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
  }

  /* save reference to the timing info */
  ffmpegdec->context->reordered_opaque = (gint64) dec_info->idx;
  ffmpegdec->picture->reordered_opaque = (gint64) dec_info->idx;

  GST_DEBUG_OBJECT (ffmpegdec, "stored opaque values idx %d", dec_info->idx);

  /* now decode the frame */
  gst_avpacket_init (&packet, data, size);
  len = avcodec_decode_video2 (ffmpegdec->context,
      ffmpegdec->picture, &have_data, &packet);

  /* restore previous state */
  if (!decode)
    ffmpegdec->context->skip_frame = skip_frame;

  GST_DEBUG_OBJECT (ffmpegdec, "after decode: len %d, have_data %d",
      len, have_data);

  /* when we are in skip_frame mode, don't complain when ffmpeg returned
   * no data because we told it to skip stuff. */
  if (len < 0 && (mode_switch || ffmpegdec->context->skip_frame))
    len = 0;

  if (len > 0 && have_data <= 0 && (mode_switch
          || ffmpegdec->context->skip_frame)) {
    /* we consumed some bytes but nothing decoded and we are skipping frames,
     * disable the interpollation of DTS timestamps */
    ffmpegdec->last_out = -1;
  }

  /* no data, we're done */
  if (len < 0 || have_data <= 0)
    goto beach;

  /* get the output picture timing info again */
  out_info = gst_ts_info_get (ffmpegdec, ffmpegdec->picture->reordered_opaque);
  out_pts = out_info->timestamp;
  out_duration = out_info->duration;
  out_offset = out_info->offset;

  GST_DEBUG_OBJECT (ffmpegdec,
      "pts %" G_GUINT64_FORMAT " duration %" G_GUINT64_FORMAT " offset %"
      G_GINT64_FORMAT, out_pts, out_duration, out_offset);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: pts %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->pts);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: num %d",
      ffmpegdec->picture->coded_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: ref %d",
      ffmpegdec->picture->reference);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: display %d",
      ffmpegdec->picture->display_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: opaque %p",
      ffmpegdec->picture->opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: reordered opaque %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->reordered_opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "repeat_pict:%d",
      ffmpegdec->picture->repeat_pict);
  GST_DEBUG_OBJECT (ffmpegdec, "interlaced_frame:%d",
      ffmpegdec->picture->interlaced_frame);

  if (G_UNLIKELY (ffmpegdec->picture->interlaced_frame !=
          ffmpegdec->ctx_interlaced)) {
    GST_WARNING ("Change in interlacing ! picture:%d, recorded:%d",
        ffmpegdec->picture->interlaced_frame, ffmpegdec->ctx_interlaced);
    ffmpegdec->ctx_interlaced = ffmpegdec->picture->interlaced_frame;
    gst_ffmpegdec_video_negotiate (ffmpegdec, TRUE);
  }
#if 0
  /* Whether a frame is interlaced or not is unknown at the time of
     buffer allocation, so caps on the buffer in opaque will have
     the previous frame's interlaced flag set. So if interlacedness
     has changed since allocation, we update the buffer (if any)
     caps now with the correct interlaced flag. */
  if (ffmpegdec->picture->opaque != NULL) {
    GstBuffer *buffer = ffmpegdec->picture->opaque;
    if (GST_BUFFER_CAPS (buffer) && GST_PAD_CAPS (ffmpegdec->srcpad)) {
      GstStructure *s = gst_caps_get_structure (GST_BUFFER_CAPS (buffer), 0);
      gboolean interlaced;
      gboolean found = gst_structure_get_boolean (s, "interlaced", &interlaced);
      if (!found || (! !interlaced != ! !ffmpegdec->format.video.interlaced)) {
        GST_DEBUG_OBJECT (ffmpegdec,
            "Buffer interlacing does not match pad, updating");
        buffer = gst_buffer_make_metadata_writable (buffer);
        gst_buffer_set_caps (buffer, GST_PAD_CAPS (ffmpegdec->srcpad));
        ffmpegdec->picture->opaque = buffer;
      }
    }
  }
#endif

  /* check if we are dealing with a keyframe here, this will also check if we
   * are dealing with B frames. */
  iskeyframe = check_keyframe (ffmpegdec);

  /* check that the timestamps go upwards */
  if (ffmpegdec->last_out != -1 && ffmpegdec->last_out > out_pts) {
    /* timestamps go backwards, this means frames were reordered and we must
     * be dealing with DTS as the buffer timestamps */
    if (!ffmpegdec->reordered_out) {
      GST_DEBUG_OBJECT (ffmpegdec, "detected reordered out timestamps");
      ffmpegdec->reordered_out = TRUE;
    }
    if (ffmpegdec->reordered_in) {
      /* we reset the input reordering here because we want to recover from an
       * occasionally wrong reordered input timestamp */
      GST_DEBUG_OBJECT (ffmpegdec, "assuming DTS input timestamps");
      ffmpegdec->reordered_in = FALSE;
    }
  }

  if (out_pts == 0 && out_pts == ffmpegdec->last_out) {
    GST_LOG_OBJECT (ffmpegdec, "ffmpeg returns 0 timestamps, ignoring");
    /* some codecs only output 0 timestamps, when that happens, make us select an
     * output timestamp based on the input timestamp. We do this by making the
     * ffmpeg timestamp and the interpollated next timestamp invalid. */
    out_pts = -1;
    ffmpegdec->next_out = -1;
  } else
    ffmpegdec->last_out = out_pts;

  /* we assume DTS as input timestamps unless we see reordered input
   * timestamps */
  if (!ffmpegdec->reordered_in && ffmpegdec->reordered_out) {
    /* PTS and DTS are the same for keyframes */
    if (!iskeyframe && ffmpegdec->next_out != -1) {
      /* interpolate all timestamps except for keyframes, FIXME, this is
       * wrong when QoS is active. */
      GST_DEBUG_OBJECT (ffmpegdec, "interpolate timestamps");
      out_pts = -1;
      out_offset = -1;
    }
  }

  /* when we're waiting for a keyframe, see if we have one or drop the current
   * non-keyframe */
  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    if (G_LIKELY (!iskeyframe))
      goto drop_non_keyframe;

    /* we have a keyframe, we can stop waiting for one */
    ffmpegdec->waiting_for_key = FALSE;
  }

  /* get a handle to the output buffer */
  *ret = get_output_buffer (ffmpegdec, outbuf);
  if (G_UNLIKELY (*ret != GST_FLOW_OK))
    goto no_output;

  /*
   * Timestamps:
   *
   *  1) Copy picture timestamp if valid
   *  2) else interpolate from previous output timestamp
   *  3) else copy input timestamp
   */
  out_timestamp = -1;
  if (out_pts != -1) {
    /* Get (interpolated) timestamp from FFMPEG */
    out_timestamp = (GstClockTime) out_pts;
    GST_LOG_OBJECT (ffmpegdec, "using timestamp %" GST_TIME_FORMAT
        " returned by ffmpeg", GST_TIME_ARGS (out_timestamp));
  }
  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp) && ffmpegdec->next_out != -1) {
    out_timestamp = ffmpegdec->next_out;
    GST_LOG_OBJECT (ffmpegdec, "using next timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (out_timestamp));
  }
  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
    out_timestamp = dec_info->timestamp;
    GST_LOG_OBJECT (ffmpegdec, "using in timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (out_timestamp));
  }
  GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;

  /*
   * Offset:
   *  0) Use stored input offset (from opaque)
   *  1) Use value converted from timestamp if valid
   *  2) Use input offset if valid
   */
  if (out_offset != GST_BUFFER_OFFSET_NONE) {
    /* out_offset already contains the offset from ts_info */
    GST_LOG_OBJECT (ffmpegdec, "Using offset returned by ffmpeg");
  } else if (out_timestamp != GST_CLOCK_TIME_NONE) {
    GST_LOG_OBJECT (ffmpegdec, "Using offset converted from timestamp");
    /* FIXME, we should really remove this as it's not nice at all to do
     * upstream queries for each frame to get the frame offset. We also can't
     * really remove this because it is the only way of setting frame offsets
     * on outgoing buffers. We should have metadata so that the upstream peer
     * can set a frame number on the encoded data. */
    gst_pad_peer_query_convert (ffmpegdec->sinkpad,
        GST_FORMAT_TIME, out_timestamp, GST_FORMAT_DEFAULT, &out_offset);
  } else if (dec_info->offset != GST_BUFFER_OFFSET_NONE) {
    /* FIXME, the input offset is input media specific and might not
     * be the same for the output media. (byte offset as input, frame number
     * as output, for example) */
    GST_LOG_OBJECT (ffmpegdec, "using in_offset %" G_GINT64_FORMAT,
        dec_info->offset);
    out_offset = dec_info->offset;
  } else {
    GST_LOG_OBJECT (ffmpegdec, "no valid offset found");
    out_offset = GST_BUFFER_OFFSET_NONE;
  }
  GST_BUFFER_OFFSET (*outbuf) = out_offset;

  /*
   * Duration:
   *
   *  1) Use reordered input duration if valid
   *  2) Else use input duration
   *  3) else use input framerate
   *  4) else use ffmpeg framerate
   */
  if (GST_CLOCK_TIME_IS_VALID (out_duration)) {
    /* We have a valid (reordered) duration */
    GST_LOG_OBJECT (ffmpegdec, "Using duration returned by ffmpeg");
  } else if (GST_CLOCK_TIME_IS_VALID (dec_info->duration)) {
    GST_LOG_OBJECT (ffmpegdec, "using in_duration");
    out_duration = dec_info->duration;
  } else if (GST_CLOCK_TIME_IS_VALID (ffmpegdec->last_diff)) {
    GST_LOG_OBJECT (ffmpegdec, "using last-diff");
    out_duration = ffmpegdec->last_diff;
  } else {
    /* if we have an input framerate, use that */
    if (ffmpegdec->out_info.fps_n != -1 &&
        (ffmpegdec->out_info.fps_n != 1000 && ffmpegdec->out_info.fps_d != 1)) {
      GST_LOG_OBJECT (ffmpegdec, "using input framerate for duration");
      out_duration = gst_util_uint64_scale_int (GST_SECOND,
          ffmpegdec->out_info.fps_d, ffmpegdec->out_info.fps_n);
    } else {
      /* don't try to use the decoder's framerate when it seems a bit abnormal,
       * which we assume when den >= 1000... */
      if (ffmpegdec->context->time_base.num != 0 &&
          (ffmpegdec->context->time_base.den > 0 &&
              ffmpegdec->context->time_base.den < 1000)) {
        GST_LOG_OBJECT (ffmpegdec, "using decoder's framerate for duration");
        out_duration = gst_util_uint64_scale_int (GST_SECOND,
            ffmpegdec->context->time_base.num *
            ffmpegdec->context->ticks_per_frame,
            ffmpegdec->context->time_base.den);
      } else {
        GST_LOG_OBJECT (ffmpegdec, "no valid duration found");
      }
    }
  }

  /* Take repeat_pict into account */
  if (GST_CLOCK_TIME_IS_VALID (out_duration)) {
    out_duration += out_duration * ffmpegdec->picture->repeat_pict / 2;
  }
  GST_BUFFER_DURATION (*outbuf) = out_duration;

  if (out_timestamp != -1 && out_duration != -1 && out_duration != 0)
    ffmpegdec->next_out = out_timestamp + out_duration;
  else
    ffmpegdec->next_out = -1;

  /* palette is not part of raw video frame in gst and the size
   * of the outgoing buffer needs to be adjusted accordingly */
  if (ffmpegdec->context->palctrl != NULL) {

    gst_buffer_resize (*outbuf, 0,
        gst_buffer_get_size (*outbuf) - AVPALETTE_SIZE);
  }

  /* now see if we need to clip the buffer against the segment boundaries. */
  if (G_UNLIKELY (!clip_video_buffer (ffmpegdec, *outbuf, out_timestamp,
              out_duration)))
    goto clipped;

  /* mark as keyframe or delta unit */
  if (!iskeyframe)
    GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (ffmpegdec->picture->top_field_first)
    GST_BUFFER_FLAG_SET (*outbuf, GST_VIDEO_BUFFER_FLAG_TFF);


beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
  return len;

  /* special cases */
drop_non_keyframe:
  {
    GST_WARNING_OBJECT (ffmpegdec, "Dropping non-keyframe (seek/init)");
    goto beach;
  }
no_output:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "no output buffer");
    len = -1;
    goto beach;
  }
clipped:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "buffer clipped");
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    goto beach;
  }
}

/* returns TRUE if buffer is within segment, else FALSE.
 * if Buffer is on segment border, it's timestamp and duration will be clipped */
static gboolean
clip_audio_buffer (GstFFMpegDec * dec, GstBuffer * buf, GstClockTime in_ts,
    GstClockTime in_dur)
{
  GstClockTime stop;
  gint64 diff;
  guint64 ctime, cstop;
  gboolean res = TRUE;
  gsize size, offset;

  size = gst_buffer_get_size (buf);
  offset = 0;

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT ", duration:%" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur),
      size);

  /* can't clip without TIME segment */
  if (G_UNLIKELY (dec->segment.format != GST_FORMAT_TIME))
    goto beach;

  /* we need a start time */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (in_ts)))
    goto beach;

  /* trust duration */
  stop = in_ts + in_dur;

  res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME, in_ts, stop, &ctime,
      &cstop);
  if (G_UNLIKELY (!res))
    goto out_of_segment;

  /* see if some clipping happened */
  if (G_UNLIKELY ((diff = ctime - in_ts) > 0)) {
    /* bring clipped time to bytes */
    diff =
        gst_util_uint64_scale_int (diff, dec->format.audio.samplerate,
        GST_SECOND) * (dec->format.audio.depth * dec->format.audio.channels);

    GST_DEBUG_OBJECT (dec, "clipping start to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (ctime), diff);

    offset += diff;
    size -= diff;
  }
  if (G_UNLIKELY ((diff = stop - cstop) > 0)) {
    /* bring clipped time to bytes */
    diff =
        gst_util_uint64_scale_int (diff, dec->format.audio.samplerate,
        GST_SECOND) * (dec->format.audio.depth * dec->format.audio.channels);

    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

    size -= diff;
  }
  gst_buffer_resize (buf, offset, size);
  GST_BUFFER_TIMESTAMP (buf) = ctime;
  GST_BUFFER_DURATION (buf) = cstop - ctime;

beach:
  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;

  /* ERRORS */
out_of_segment:
  {
    GST_LOG_OBJECT (dec, "out of segment");
    goto beach;
  }
}

static gint
gst_ffmpegdec_audio_frame (GstFFMpegDec * ffmpegdec,
    AVCodec * in_plugin, guint8 * data, guint size,
    const GstTSInfo * dec_info, GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data = AVCODEC_MAX_AUDIO_FRAME_SIZE;
  GstClockTime out_timestamp, out_duration;
  GstMapInfo map;
  gint64 out_offset;
  int16_t *odata;
  AVPacket packet;

  GST_DEBUG_OBJECT (ffmpegdec,
      "size:%d, offset:%" G_GINT64_FORMAT ", ts:%" GST_TIME_FORMAT ", dur:%"
      GST_TIME_FORMAT ", ffmpegdec->next_out:%" GST_TIME_FORMAT, size,
      dec_info->offset, GST_TIME_ARGS (dec_info->timestamp),
      GST_TIME_ARGS (dec_info->duration), GST_TIME_ARGS (ffmpegdec->next_out));

  *outbuf = new_aligned_buffer (AVCODEC_MAX_AUDIO_FRAME_SIZE);

  gst_buffer_map (*outbuf, &map, GST_MAP_WRITE);
  odata = (int16_t *) map.data;

  gst_avpacket_init (&packet, data, size);
  len = avcodec_decode_audio3 (ffmpegdec->context, odata, &have_data, &packet);

  GST_DEBUG_OBJECT (ffmpegdec,
      "Decode audio: len=%d, have_data=%d", len, have_data);

  if (len >= 0 && have_data > 0) {
    GstAudioFormat fmt;

    /* Buffer size */
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_resize (*outbuf, 0, have_data);

    GST_DEBUG_OBJECT (ffmpegdec, "Creating output buffer");
    if (!gst_ffmpegdec_audio_negotiate (ffmpegdec, FALSE)) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      len = -1;
      goto beach;
    }

    /*
     * Timestamps:
     *
     *  1) Copy input timestamp if valid
     *  2) else interpolate from previous input timestamp
     */
    /* always take timestamps from the input buffer if any */
    if (GST_CLOCK_TIME_IS_VALID (dec_info->timestamp)) {
      out_timestamp = dec_info->timestamp;
    } else {
      out_timestamp = ffmpegdec->next_out;
    }

    /*
     * Duration:
     *
     *  1) calculate based on number of samples
     */
    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        ffmpegdec->format.audio.depth * ffmpegdec->format.audio.channels *
        ffmpegdec->format.audio.samplerate);

    /* offset:
     *
     * Just copy
     */
    out_offset = dec_info->offset;

    GST_DEBUG_OBJECT (ffmpegdec,
        "Buffer created. Size:%d , timestamp:%" GST_TIME_FORMAT " , duration:%"
        GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (out_timestamp), GST_TIME_ARGS (out_duration));

    GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;
    GST_BUFFER_DURATION (*outbuf) = out_duration;
    GST_BUFFER_OFFSET (*outbuf) = out_offset;

    /* the next timestamp we'll use when interpolating */
    if (GST_CLOCK_TIME_IS_VALID (out_timestamp))
      ffmpegdec->next_out = out_timestamp + out_duration;

    /* now see if we need to clip the buffer against the segment boundaries. */
    if (G_UNLIKELY (!clip_audio_buffer (ffmpegdec, *outbuf, out_timestamp,
                out_duration)))
      goto clipped;


    /* Reorder channels to the GStreamer channel order */
    /* Only the width really matters here... and it's stored as depth */
    fmt =
        gst_audio_format_build_integer (TRUE, G_BYTE_ORDER,
        ffmpegdec->format.audio.depth * 8, ffmpegdec->format.audio.depth * 8);

    gst_audio_buffer_reorder_channels (*outbuf, fmt,
        ffmpegdec->format.audio.channels,
        ffmpegdec->format.audio.ffmpeg_layout,
        ffmpegdec->format.audio.gst_layout);
  } else {
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  /* If we don't error out after the first failed read with the AAC decoder,
   * we must *not* carry on pushing data, else we'll cause segfaults... */
  if (len == -1 && (in_plugin->id == CODEC_ID_AAC
          || in_plugin->id == CODEC_ID_AAC_LATM)) {
    GST_ELEMENT_ERROR (ffmpegdec, STREAM, DECODE, (NULL),
        ("Decoding of AAC stream by FFMPEG failed."));
    *ret = GST_FLOW_ERROR;
  }

beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
  return len;

  /* ERRORS */
clipped:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "buffer clipped");
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    goto beach;
  }
}

/* gst_ffmpegdec_frame:
 * ffmpegdec:
 * data: pointer to the data to decode
 * size: size of data in bytes
 * got_data: 0 if no data was decoded, != 0 otherwise.
 * in_time: timestamp of data
 * in_duration: duration of data
 * ret: GstFlowReturn to return in the chain function
 *
 * Decode the given frame and pushes it downstream.
 *
 * Returns: Number of bytes used in decoding, -1 on error/failure.
 */

static gint
gst_ffmpegdec_frame (GstFFMpegDec * ffmpegdec,
    guint8 * data, guint size, gint * got_data, const GstTSInfo * dec_info,
    GstFlowReturn * ret)
{
  GstFFMpegDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec, "data:%p, size:%d, id:%d", data, size,
      dec_info->idx);

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  switch (oclass->in_plugin->type) {
    case AVMEDIA_TYPE_VIDEO:
      len =
          gst_ffmpegdec_video_frame (ffmpegdec, data, size, dec_info, &outbuf,
          ret);
      break;
    case AVMEDIA_TYPE_AUDIO:
      len =
          gst_ffmpegdec_audio_frame (ffmpegdec, oclass->in_plugin, data, size,
          dec_info, &outbuf, ret);

      /* if we did not get an output buffer and we have a pending discont, don't
       * clear the input timestamps, we will put them on the next buffer because
       * else we might create the first buffer with a very big timestamp gap. */
      if (outbuf == NULL && ffmpegdec->discont) {
        GST_DEBUG_OBJECT (ffmpegdec, "no buffer but keeping timestamp");
        ffmpegdec->clear_ts = FALSE;
      }
      break;
    default:
      GST_ERROR_OBJECT (ffmpegdec, "Asked to decode non-audio/video frame !");
      g_assert_not_reached ();
      break;
  }

  if (outbuf)
    have_data = 1;

  if (len < 0 || have_data < 0) {
    GST_WARNING_OBJECT (ffmpegdec,
        "ffdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, have_data);
    *got_data = 0;
    goto beach;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    goto beach;
  } else {
    /* this is where I lost my last clue on ffmpeg... */
    *got_data = 1;
  }

  if (outbuf) {
    GST_LOG_OBJECT (ffmpegdec,
        "Decoded data, now pushing buffer %p with offset %" G_GINT64_FORMAT
        ", timestamp %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT,
        outbuf, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    /* mark pending discont */
    if (ffmpegdec->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      ffmpegdec->discont = FALSE;
    }
    if (ffmpegdec->segment.rate > 0.0) {
      /* and off we go */
      *ret = gst_pad_push (ffmpegdec->srcpad, outbuf);
    } else {
      /* reverse playback, queue frame till later when we get a discont. */
      GST_DEBUG_OBJECT (ffmpegdec, "queued frame");
      ffmpegdec->queued = g_list_prepend (ffmpegdec->queued, outbuf);
      *ret = GST_FLOW_OK;
    }
  } else {
    GST_DEBUG_OBJECT (ffmpegdec, "We didn't get a decoded buffer");
  }

beach:
  return len;

  /* ERRORS */
no_codec:
  {
    GST_ERROR_OBJECT (ffmpegdec, "no codec context");
    return -1;
  }
}

static void
gst_ffmpegdec_drain (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, len, try = 0;

    GST_LOG_OBJECT (ffmpegdec,
        "codec has delay capabilities, calling until ffmpeg has drained everything");

    do {
      GstFlowReturn ret;

      len =
          gst_ffmpegdec_frame (ffmpegdec, NULL, 0, &have_data, &ts_info_none,
          &ret);
      if (len < 0 || have_data == 0)
        break;
    } while (try++ < 10);
  }
  if (ffmpegdec->segment.rate < 0.0) {
    /* if we have some queued frames for reverse playback, flush them now */
    flush_queued (ffmpegdec);
  }
}

static void
gst_ffmpegdec_flush_pcache (GstFFMpegDec * ffmpegdec)
{
  if (ffmpegdec->pctx) {
    gint size, bsize;
    guint8 *data;
    guint8 bdata[FF_INPUT_BUFFER_PADDING_SIZE];

    bsize = FF_INPUT_BUFFER_PADDING_SIZE;
    memset (bdata, 0, bsize);

    /* parse some dummy data to work around some ffmpeg weirdness where it keeps
     * the previous pts around */
    av_parser_parse2 (ffmpegdec->pctx, ffmpegdec->context,
        &data, &size, bdata, bsize, -1, -1, -1);
    ffmpegdec->pctx->pts = -1;
    ffmpegdec->pctx->dts = -1;
  }

  if (ffmpegdec->pcache) {
    gst_buffer_unref (ffmpegdec->pcache);
    ffmpegdec->pcache = NULL;
  }
}

static gboolean
gst_ffmpegdec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegDec *) parent;

  GST_DEBUG_OBJECT (ffmpegdec, "Handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      gst_ffmpegdec_drain (ffmpegdec);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);
      }
      gst_ffmpegdec_reset_ts (ffmpegdec);
      gst_ffmpegdec_reset_qos (ffmpegdec);
      gst_ffmpegdec_flush_pcache (ffmpegdec);
      ffmpegdec->waiting_for_key = TRUE;
      gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
      clear_queued (ffmpegdec);
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      if (!ffmpegdec->last_caps
          || !gst_caps_is_equal (ffmpegdec->last_caps, caps)) {
        ret = gst_ffmpegdec_setcaps (ffmpegdec, caps);
        if (ret) {
          gst_caps_replace (&ffmpegdec->last_caps, caps);
        }
      } else {
        ret = TRUE;
      }

      gst_event_unref (event);
      goto done;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      switch (segment.format) {
        case GST_FORMAT_TIME:
          /* fine, our native segment format */
          break;
        case GST_FORMAT_BYTES:
        {
          gint bit_rate;

          bit_rate = ffmpegdec->context->bit_rate;

          /* convert to time or fail */
          if (!bit_rate)
            goto no_bitrate;

          GST_DEBUG_OBJECT (ffmpegdec, "bitrate: %d", bit_rate);

          /* convert values to TIME */
          if (segment.start != -1)
            segment.start =
                gst_util_uint64_scale_int (segment.start, GST_SECOND, bit_rate);
          if (segment.stop != -1)
            segment.stop =
                gst_util_uint64_scale_int (segment.stop, GST_SECOND, bit_rate);
          if (segment.time != -1)
            segment.time =
                gst_util_uint64_scale_int (segment.time, GST_SECOND, bit_rate);

          /* unref old event */
          gst_event_unref (event);

          /* create new converted time segment */
          segment.format = GST_FORMAT_TIME;
          /* FIXME, bitrate is not good enough too find a good stop, let's
           * hope start and time were 0... meh. */
          segment.stop = -1;
          event = gst_event_new_segment (&segment);
          break;
        }
        default:
          /* invalid format */
          goto invalid_format;
      }

      /* drain pending frames before trying to use the new segment, queued
       * buffers belonged to the previous segment. */
      if (ffmpegdec->context->codec)
        gst_ffmpegdec_drain (ffmpegdec);

      GST_DEBUG_OBJECT (ffmpegdec, "SEGMENT in time %" GST_SEGMENT_FORMAT,
          &segment);

      /* and store the values */
      gst_segment_copy_into (&segment, &ffmpegdec->segment);
      break;
    }
    default:
      break;
  }

  /* and push segment downstream */
  ret = gst_pad_push_event (ffmpegdec->srcpad, event);

done:

  return ret;

  /* ERRORS */
no_bitrate:
  {
    GST_WARNING_OBJECT (ffmpegdec, "no bitrate to convert BYTES to TIME");
    gst_event_unref (event);
    goto done;
  }
invalid_format:
  {
    GST_WARNING_OBJECT (ffmpegdec, "unknown format received in NEWSEGMENT");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_ffmpegdec_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFFMpegDec *ffmpegdec;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegDec *) parent;

  GST_DEBUG_OBJECT (ffmpegdec, "Handling %s query",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstPadTemplate *templ;

      ret = FALSE;
      if ((templ = GST_PAD_PAD_TEMPLATE (pad))) {
        GstCaps *tcaps;

        if ((tcaps = GST_PAD_TEMPLATE_CAPS (templ))) {
          GstCaps *caps;

          gst_query_parse_accept_caps (query, &caps);
          gst_query_set_accept_caps_result (query,
              gst_caps_is_subset (caps, tcaps));
          ret = TRUE;
        }
      }
      break;
    }
    case GST_QUERY_ALLOCATION:
    {
      GstAllocationParams params;

      gst_allocation_params_init (&params);
      params.flags = GST_MEMORY_FLAG_ZERO_PADDED;
      params.padding = FF_INPUT_BUFFER_PADDING_SIZE;
      /* we would like to have some padding so that we don't have to
       * memcpy. We don't suggest an allocator. */
      gst_query_add_allocation_param (query, NULL, &params);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}

static GstFlowReturn
gst_ffmpegdec_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFFMpegDec *ffmpegdec;
  GstFFMpegDecClass *oclass;
  guint8 *data, *bdata;
  GstMapInfo map;
  gint size, bsize, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gboolean discont, do_padding;
  gint64 in_offset;
  const GstTSInfo *in_info;
  const GstTSInfo *dec_info;

  ffmpegdec = (GstFFMpegDec *) parent;

  if (G_UNLIKELY (!ffmpegdec->opened))
    goto not_negotiated;

  discont = GST_BUFFER_IS_DISCONT (inbuf);

  /* The discont flags marks a buffer that is not continuous with the previous
   * buffer. This means we need to clear whatever data we currently have. We
   * currently also wait for a new keyframe, which might be suboptimal in the
   * case of a network error, better show the errors than to drop all data.. */
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (ffmpegdec, "received DISCONT");
    /* drain what we have queued */
    gst_ffmpegdec_drain (ffmpegdec);
    gst_ffmpegdec_flush_pcache (ffmpegdec);
    avcodec_flush_buffers (ffmpegdec->context);
    ffmpegdec->discont = TRUE;
    gst_ffmpegdec_reset_ts (ffmpegdec);
  }
  /* by default we clear the input timestamp after decoding each frame so that
   * interpollation can work. */
  ffmpegdec->clear_ts = TRUE;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* do early keyframe check pretty bad to rely on the keyframe flag in the
   * source for this as it might not even be parsed (UDP/file/..).  */
  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    GST_DEBUG_OBJECT (ffmpegdec, "waiting for keyframe");
    if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DELTA_UNIT) &&
        oclass->in_plugin->type != AVMEDIA_TYPE_AUDIO)
      goto skip_keyframe;

    GST_DEBUG_OBJECT (ffmpegdec, "got keyframe");
    ffmpegdec->waiting_for_key = FALSE;
  }
  /* parse cache joining. If there is cached data */
  if (ffmpegdec->pcache) {
    /* join with previous data */
    GST_LOG_OBJECT (ffmpegdec, "join parse cache");
    inbuf = gst_buffer_join (ffmpegdec->pcache, inbuf);
    /* no more cached data, we assume we can consume the complete cache */
    ffmpegdec->pcache = NULL;
  }

  in_timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  in_duration = GST_BUFFER_DURATION (inbuf);
  in_offset = GST_BUFFER_OFFSET (inbuf);

  /* get handle to timestamp info, we can pass this around to ffmpeg */
  in_info = gst_ts_info_store (ffmpegdec, in_timestamp, in_duration, in_offset);

  if (in_timestamp != -1) {
    /* check for increasing timestamps if they are jumping backwards, we
     * probably are dealing with PTS as timestamps */
    if (!ffmpegdec->reordered_in && ffmpegdec->last_in != -1) {
      if (in_timestamp < ffmpegdec->last_in) {
        GST_LOG_OBJECT (ffmpegdec, "detected reordered input timestamps");
        ffmpegdec->reordered_in = TRUE;
        ffmpegdec->last_diff = GST_CLOCK_TIME_NONE;
      } else if (in_timestamp > ffmpegdec->last_in) {
        GstClockTime diff;
        /* keep track of timestamp diff to estimate duration */
        diff = in_timestamp - ffmpegdec->last_in;
        /* need to scale with amount of frames in the interval */
        if (ffmpegdec->last_frames)
          diff /= ffmpegdec->last_frames;

        GST_LOG_OBJECT (ffmpegdec, "estimated duration %" GST_TIME_FORMAT " %u",
            GST_TIME_ARGS (diff), ffmpegdec->last_frames);

        ffmpegdec->last_diff = diff;
      }
    }
    ffmpegdec->last_in = in_timestamp;
    ffmpegdec->last_frames = 0;
  }

  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == CODEC_ID_SVQ3) {
    inbuf = gst_buffer_make_writable (inbuf);
  }

  gst_buffer_map (inbuf, &map, GST_MAP_READ);

  bdata = map.data;
  bsize = map.size;

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %u, offset:%" G_GUINT64_FORMAT ", ts:%"
      GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT ", info %d",
      bsize, in_offset, GST_TIME_ARGS (in_timestamp),
      GST_TIME_ARGS (in_duration), in_info->idx);

  if (!GST_MEMORY_IS_ZERO_PADDED (map.memory)
      || (map.maxsize - map.size) < FF_INPUT_BUFFER_PADDING_SIZE) {
    /* add padding */
    if (ffmpegdec->padded_size < bsize + FF_INPUT_BUFFER_PADDING_SIZE) {
      ffmpegdec->padded_size = bsize + FF_INPUT_BUFFER_PADDING_SIZE;
      ffmpegdec->padded = g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
      GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
          ffmpegdec->padded_size);
    }
    GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
        "Copy input to add padding");
    memcpy (ffmpegdec->padded, bdata, bsize);
    memset (ffmpegdec->padded + bsize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    bdata = ffmpegdec->padded;
  }

  do {
    guint8 tmp_padding[FF_INPUT_BUFFER_PADDING_SIZE];

    /* parse, if at all possible */
    if (ffmpegdec->pctx) {
      gint res;

      do_padding = TRUE;
      GST_LOG_OBJECT (ffmpegdec,
          "Calling av_parser_parse2 with offset %" G_GINT64_FORMAT ", ts:%"
          GST_TIME_FORMAT " size %d", in_offset, GST_TIME_ARGS (in_timestamp),
          bsize);

      /* feed the parser. We pass the timestamp info so that we can recover all
       * info again later */
      res = av_parser_parse2 (ffmpegdec->pctx, ffmpegdec->context,
          &data, &size, bdata, bsize, in_info->idx, in_info->idx, in_offset);

      GST_LOG_OBJECT (ffmpegdec,
          "parser returned res %d and size %d, id %" G_GINT64_FORMAT, res, size,
          (gint64) ffmpegdec->pctx->pts);

      /* store pts for decoding */
      if (ffmpegdec->pctx->pts != AV_NOPTS_VALUE && ffmpegdec->pctx->pts != -1)
        dec_info = gst_ts_info_get (ffmpegdec, ffmpegdec->pctx->pts);
      else {
        /* ffmpeg sometimes loses track after a flush, help it by feeding a
         * valid start time */
        ffmpegdec->pctx->pts = in_info->idx;
        ffmpegdec->pctx->dts = in_info->idx;
        dec_info = in_info;
      }

      GST_LOG_OBJECT (ffmpegdec, "consuming %d bytes. id %d", size,
          dec_info->idx);

      if (res) {
        /* there is output, set pointers for next round. */
        bsize -= res;
        bdata += res;
      } else {
        /* Parser did not consume any data, make sure we don't clear the
         * timestamp for the next round */
        ffmpegdec->clear_ts = FALSE;
      }

      /* if there is no output, we must break and wait for more data. also the
       * timestamp in the context is not updated. */
      if (size == 0) {
        if (bsize > 0)
          continue;
        else
          break;
      }
    } else {
      do_padding = FALSE;
      data = bdata;
      size = bsize;

      dec_info = in_info;
    }

    if (do_padding) {
      /* add temporary padding */
      GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
          "Add temporary input padding");
      memcpy (tmp_padding, data + size, FF_INPUT_BUFFER_PADDING_SIZE);
      memset (data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    /* decode a frame of audio/video now */
    len =
        gst_ffmpegdec_frame (ffmpegdec, data, size, &have_data, dec_info, &ret);

    if (do_padding) {
      memcpy (data + size, tmp_padding, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow return, make sure we discard all data and exit */
      bsize = 0;
      break;
    }
    if (!ffmpegdec->pctx) {
      if (len == 0 && !have_data) {
        /* nothing was decoded, this could be because no data was available or
         * because we were skipping frames.
         * If we have no context we must exit and wait for more data, we keep the
         * data we tried. */
        GST_LOG_OBJECT (ffmpegdec, "Decoding didn't return any data, breaking");
        break;
      } else if (len < 0) {
        /* a decoding error happened, we must break and try again with next data. */
        GST_LOG_OBJECT (ffmpegdec, "Decoding error, breaking");
        bsize = 0;
        break;
      }
      /* prepare for the next round, for codecs with a context we did this
       * already when using the parser. */
      bsize -= len;
      bdata += len;
    } else {
      if (len == 0) {
        /* nothing was decoded, this could be because no data was available or
         * because we were skipping frames. Since we have a parser we can
         * continue with the next frame */
        GST_LOG_OBJECT (ffmpegdec,
            "Decoding didn't return any data, trying next");
      } else if (len < 0) {
        /* we have a context that will bring us to the next frame */
        GST_LOG_OBJECT (ffmpegdec, "Decoding error, trying next");
      }
    }

    /* make sure we don't use the same old timestamp for the next frame and let
     * the interpollation take care of it. */
    if (ffmpegdec->clear_ts) {
      in_timestamp = GST_CLOCK_TIME_NONE;
      in_duration = GST_CLOCK_TIME_NONE;
      in_offset = GST_BUFFER_OFFSET_NONE;
      in_info = GST_TS_INFO_NONE;
    } else {
      ffmpegdec->clear_ts = TRUE;
    }
    ffmpegdec->last_frames++;
    do_padding = TRUE;

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  gst_buffer_unmap (inbuf, &map);

  /* keep left-over */
  if (ffmpegdec->pctx && bsize > 0) {
    in_timestamp = GST_BUFFER_TIMESTAMP (inbuf);
    in_offset = GST_BUFFER_OFFSET (inbuf);

    GST_LOG_OBJECT (ffmpegdec,
        "Keeping %d bytes of data with offset %" G_GINT64_FORMAT ", timestamp %"
        GST_TIME_FORMAT, bsize, in_offset, GST_TIME_ARGS (in_timestamp));

    ffmpegdec->pcache = gst_buffer_copy_region (inbuf, GST_BUFFER_COPY_ALL,
        gst_buffer_get_size (inbuf) - bsize, bsize);
    /* we keep timestamp, even though all we really know is that the correct
     * timestamp is not below the one from inbuf */
    GST_BUFFER_TIMESTAMP (ffmpegdec->pcache) = in_timestamp;
    GST_BUFFER_OFFSET (ffmpegdec->pcache) = in_offset;
  } else if (bsize > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);
  }
  gst_buffer_unref (inbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("ffdec_%s: input format was not set before data start",
            oclass->in_plugin->name));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
skip_keyframe:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "skipping non keyframe");
    gst_buffer_unref (inbuf);
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_ffmpegdec_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (ffmpegdec);
      gst_ffmpegdec_close (ffmpegdec);
      GST_OBJECT_UNLOCK (ffmpegdec);
      clear_queued (ffmpegdec);
      g_free (ffmpegdec->padded);
      ffmpegdec->padded = NULL;
      ffmpegdec->padded_size = 0;
      if (ffmpegdec->pool) {
        gst_buffer_pool_set_active (ffmpegdec->pool, FALSE);
        gst_object_unref (ffmpegdec->pool);
      }
      ffmpegdec->pool = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_ffmpegdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  switch (prop_id) {
    case PROP_LOWRES:
      ffmpegdec->lowres = ffmpegdec->context->lowres = g_value_get_enum (value);
      break;
    case PROP_SKIPFRAME:
      ffmpegdec->skip_frame = ffmpegdec->context->skip_frame =
          g_value_get_enum (value);
      break;
    case PROP_DIRECT_RENDERING:
      ffmpegdec->direct_rendering = g_value_get_boolean (value);
      break;
    case PROP_DEBUG_MV:
      ffmpegdec->debug_mv = ffmpegdec->context->debug_mv =
          g_value_get_boolean (value);
      break;
    case PROP_MAX_THREADS:
      ffmpegdec->max_threads = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  switch (prop_id) {
    case PROP_LOWRES:
      g_value_set_enum (value, ffmpegdec->context->lowres);
      break;
    case PROP_SKIPFRAME:
      g_value_set_enum (value, ffmpegdec->context->skip_frame);
      break;
    case PROP_DIRECT_RENDERING:
      g_value_set_boolean (value, ffmpegdec->direct_rendering);
      break;
    case PROP_DEBUG_MV:
      g_value_set_boolean (value, ffmpegdec->context->debug_mv);
      break;
    case PROP_MAX_THREADS:
      g_value_set_int (value, ffmpegdec->max_threads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegdec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegDecClass),
    (GBaseInitFunc) gst_ffmpegdec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegdec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegDec),
    0,
    (GInstanceInitFunc) gst_ffmpegdec_init,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;

  in_plugin = av_codec_next (NULL);

  GST_LOG ("Registering decoders");

  while (in_plugin) {
    gchar *type_name;
    gchar *plugin_name;

    /* only decoders */
    if (!in_plugin->decode) {
      goto next;
    }

    /* no quasi-codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        in_plugin->id == CODEC_ID_V210 ||
        in_plugin->id == CODEC_ID_V210X ||
        in_plugin->id == CODEC_ID_R210 ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_BLURAY)) {
      goto next;
    }

    /* No decoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library decoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      goto next;
    }

    /* No vdpau plugins until we can figure out how to properly use them
     * outside of ffmpeg. */
    if (g_str_has_suffix (in_plugin->name, "_vdpau")) {
      GST_DEBUG
          ("Ignoring VDPAU decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (g_str_has_suffix (in_plugin->name, "_xvmc")) {
      GST_DEBUG
          ("Ignoring XVMC decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    /* MPEG1VIDEO : the mpeg2video decoder is preferred */
    /* MP1 : Use MP3 for decoding */
    /* MP2 : Use MP3 for decoding */
    /* Theora: Use libtheora based theoradec */
    if (!strcmp (in_plugin->name, "gif") ||
        !strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "theora") ||
        !strcmp (in_plugin->name, "mpeg1video") ||
        !strcmp (in_plugin->name, "wavpack") ||
        !strcmp (in_plugin->name, "mp1") ||
        !strcmp (in_plugin->name, "mp2") ||
        !strcmp (in_plugin->name, "libfaad") ||
        !strcmp (in_plugin->name, "mpeg4aac") ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "srt") ||
        !strcmp (in_plugin->name, "pgssub") ||
        !strcmp (in_plugin->name, "dvdsub") ||
        !strcmp (in_plugin->name, "dvbsub")) {
      GST_LOG ("Ignoring decoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    plugin_name = g_strdup ((gchar *) in_plugin->name);
    g_strdelimit (plugin_name, NULL, '_');
    type_name = g_strdup_printf ("ffdec_%s", plugin_name);
    g_free (plugin_name);

    type = g_type_from_name (type_name);

    if (!type) {
      /* create the gtype now */
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_FFDEC_PARAMS_QDATA, (gpointer) in_plugin);
    }

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * VC1/WMV3 are not working and thus unpreferred for now. */
    switch (in_plugin->id) {
      case CODEC_ID_MPEG4:
      case CODEC_ID_MSMPEG4V3:
      case CODEC_ID_H264:
      case CODEC_ID_RA_144:
      case CODEC_ID_RA_288:
      case CODEC_ID_RV10:
      case CODEC_ID_RV20:
      case CODEC_ID_RV30:
      case CODEC_ID_RV40:
      case CODEC_ID_COOK:
        rank = GST_RANK_PRIMARY;
        break;
        /* DVVIDEO: we have a good dv decoder, fast on both ppc as well as x86.
         * They say libdv's quality is better though. leave as secondary.
         * note: if you change this, see the code in gstdv.c in good/ext/dv.
         *
         * SIPR: decoder should have a higher rank than realaudiodec.
         */
      case CODEC_ID_DVVIDEO:
      case CODEC_ID_SIPR:
        rank = GST_RANK_SECONDARY;
        break;
      case CODEC_ID_MP3:
        rank = GST_RANK_NONE;
        break;
      default:
        rank = GST_RANK_MARGINAL;
        break;
    }
    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_warning ("Failed to register %s", type_name);
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
