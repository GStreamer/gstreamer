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

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"

/* define to enable alternative buffer refcounting algorithm */
#undef EXTRA_REF

#define TS_MAP_COUNT 0xFF
#define TS_MAP_INC(ind) ind = (ind + 1) & TS_MAP_COUNT

typedef struct _GstDataPassThrough GstDataPassThrough;

struct _GstDataPassThrough
{
  guint64 ts;
  guint64 offset;
};

typedef struct _GstTSMap GstTSMap;

struct _GstTSMap
{
  /* timestamp */
  guint64 ts;

  /* offset */
  gint64 offset;

  /* buffer size */
  gint size;
};

typedef struct _GstTSHandler GstTSHandler;

struct _GstTSHandler
{
  /* ts list indexes */
  gint buf_head;
  gint buf_tail;
  guint buf_count;

  /* incomming buffer timestamp tracking */
  GstTSMap buffers[TS_MAP_COUNT + 1];
};

typedef struct _GstFFMpegDec GstFFMpegDec;

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
  union
  {
    struct
    {
      gint width, height;
      gint clip_width, clip_height;
      gint fps_n, fps_d;
      gint old_fps_n, old_fps_d;
      gboolean interlaced;

      enum PixelFormat pix_fmt;
    } video;
    struct
    {
      gint channels;
      gint samplerate;
      gint depth;
    } audio;
  } format;
  gboolean waiting_for_key;
  gboolean discont;
  gboolean clear_ts;
  guint64 next_ts;
  guint64 in_ts;
  gint64 in_offset;
  GstClockTime last_out;
  gboolean ts_is_dts;
  gboolean has_b_frames;

  /* parsing */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;
  guint8 *padded;
  guint padded_size;

  GValue *par;                  /* pixel aspect ratio of incoming data */
  gboolean current_dr;          /* if direct rendering is enabled */
  gboolean extra_ref;           /* keep extra ref around in get/release */

  /* some properties */
  gint hurry_up;
  gint lowres;
  gboolean direct_rendering;
  gboolean do_padding;
  gboolean debug_mv;
  gboolean crop;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;

  /* clipping segment */
  GstSegment segment;

  gboolean is_realvideo;

  GstTSHandler ts_handler;

  GList *opaque;

  /* reverse playback queue */
  GList *queued;

  /* Can downstream allocate 16bytes aligned data. */
  gboolean can_allocate_aligned;
};

typedef struct _GstFFMpegDecClass GstFFMpegDecClass;

struct _GstFFMpegDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

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
#define DEFAULT_DO_PADDING		TRUE
#define DEFAULT_DEBUG_MV		FALSE
#define DEFAULT_CROP			TRUE

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DO_PADDING,
  PROP_DEBUG_MV,
  PROP_CROP,
  PROP_LAST
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegdec_base_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_class_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec);
static void gst_ffmpegdec_finalize (GObject * object);

static gboolean gst_ffmpegdec_query (GstPad * pad, GstQuery * query);
static gboolean gst_ffmpegdec_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_ffmpegdec_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_ffmpegdec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_ffmpegdec_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_ffmpegdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_ffmpegdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec,
    gboolean force);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegdec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegdec_release_buffer (AVCodecContext * context,
    AVFrame * picture);

static void gst_ffmpegdec_drain (GstFFMpegDec * ffmpegdec);

static void gst_ts_handler_init (GstFFMpegDec * ffmpegdec);
static void gst_ts_handler_append (GstFFMpegDec * ffmpegdec,
    GstBuffer * buffer);
static void gst_ts_handler_consume (GstFFMpegDec * ffmpegdec, gint size);
static guint64 gst_ts_handler_get_ts (GstFFMpegDec * ffmpegdec,
    gint64 * offset);

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
  GstElementDetails details;
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  AVCodec *in_plugin;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFDEC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFmpeg %s decoder",
      in_plugin->long_name);
  details.klass = g_strdup_printf ("Codec/Decoder/%s",
      (in_plugin->type == CODEC_TYPE_VIDEO) ? "Video" : "Audio");
  details.description = g_strdup_printf ("FFmpeg %s decoder", in_plugin->name);
  details.author = "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* get the caps */
  sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for decoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }
  if (in_plugin->type == CODEC_TYPE_VIDEO) {
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
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

  if (klass->in_plugin->type == CODEC_TYPE_VIDEO) {
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
    g_object_class_install_property (gobject_class, PROP_DO_PADDING,
        g_param_spec_boolean ("do-padding", "Do Padding",
            "Add 0 padding before decoding data", DEFAULT_DO_PADDING,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, PROP_DEBUG_MV,
        g_param_spec_boolean ("debug-mv", "Debug motion vectors",
            "Whether ffmpeg should print motion vectors on top of the image",
            DEFAULT_DEBUG_MV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#if 0
    g_object_class_install_property (gobject_class, PROP_CROP,
        g_param_spec_boolean ("crop", "Crop",
            "Crop images to the display region",
            DEFAULT_CROP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
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
  gst_pad_set_setcaps_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_setcaps));
  gst_pad_set_event_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_sink_event));
  gst_pad_set_chain_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_chain));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegdec->srcpad);
  gst_pad_set_event_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_src_event));
  gst_pad_set_query_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_query));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context ();
  ffmpegdec->picture = avcodec_alloc_frame ();
  ffmpegdec->pctx = NULL;
  ffmpegdec->pcache = NULL;
  ffmpegdec->par = NULL;
  ffmpegdec->opened = FALSE;
  ffmpegdec->waiting_for_key = TRUE;
  ffmpegdec->hurry_up = ffmpegdec->lowres = 0;
  ffmpegdec->direct_rendering = DEFAULT_DIRECT_RENDERING;
  ffmpegdec->do_padding = DEFAULT_DO_PADDING;
  ffmpegdec->debug_mv = DEFAULT_DEBUG_MV;
  ffmpegdec->crop = DEFAULT_CROP;
  ffmpegdec->opaque = NULL;

  gst_ts_handler_init (ffmpegdec);

  ffmpegdec->format.video.fps_n = -1;
  ffmpegdec->format.video.old_fps_n = -1;
  gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);

  /* We initially assume downstream can allocate 16 bytes aligned buffers */
  ffmpegdec->can_allocate_aligned = TRUE;
}

static void
gst_ffmpegdec_finalize (GObject * object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  if (ffmpegdec->context != NULL) {
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  if (ffmpegdec->picture != NULL) {
    av_free (ffmpegdec->picture);
    ffmpegdec->picture = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ffmpegdec_query (GstPad * pad, GstQuery * query)
{
  GstFFMpegDec *ffmpegdec;
  GstPad *peer;
  gboolean res;

  ffmpegdec = (GstFFMpegDec *) gst_pad_get_parent (pad);

  res = FALSE;

  if ((peer = gst_pad_get_peer (ffmpegdec->sinkpad))) {
    /* just forward to peer */
    res = gst_pad_query (peer, query);
    gst_object_unref (peer);
  }
#if 0
  {
    GstFormat bfmt;

    bfmt = GST_FORMAT_BYTES;

    /* ok, do bitrate calc... */
    if ((type != GST_QUERY_POSITION && type != GST_QUERY_TOTAL) ||
        *fmt != GST_FORMAT_TIME || ffmpegdec->context->bit_rate == 0 ||
        !gst_pad_query (peer, type, &bfmt, value))
      return FALSE;

    if (ffmpegdec->pcache && type == GST_QUERY_POSITION)
      *value -= GST_BUFFER_SIZE (ffmpegdec->pcache);
    *value *= GST_SECOND / ffmpegdec->context->bit_rate;
  }
#endif

  gst_object_unref (ffmpegdec);

  return res;
}

static void
gst_ffmpegdec_update_qos (GstFFMpegDec * ffmpegdec, gdouble proportion,
    GstClockTime time)
{
  GST_OBJECT_LOCK (ffmpegdec);
  ffmpegdec->proportion = proportion;
  ffmpegdec->earliest_time = time;
  GST_OBJECT_UNLOCK (ffmpegdec);
}

static void
gst_ffmpegdec_reset_qos (GstFFMpegDec * ffmpegdec)
{
  gst_ffmpegdec_update_qos (ffmpegdec, 0.5, GST_CLOCK_TIME_NONE);
}

static void
gst_ffmpegdec_read_qos (GstFFMpegDec * ffmpegdec, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (ffmpegdec);
  *proportion = ffmpegdec->proportion;
  *time = ffmpegdec->earliest_time;
  GST_OBJECT_UNLOCK (ffmpegdec);
}

static gboolean
gst_ffmpegdec_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec;
  gboolean res;

  ffmpegdec = (GstFFMpegDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

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

  gst_object_unref (ffmpegdec);

  return res;
}

/* with LOCK */
static void
gst_ffmpegdec_close (GstFFMpegDec * ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  GST_LOG_OBJECT (ffmpegdec, "closing ffmpeg codec");

  if (ffmpegdec->par) {
    g_free (ffmpegdec->par);
    ffmpegdec->par = NULL;
  }

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

  ffmpegdec->format.video.fps_n = -1;
  ffmpegdec->format.video.old_fps_n = -1;
  ffmpegdec->format.video.interlaced = FALSE;
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
      ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
      if (ffmpegdec->pctx)
        GST_LOG_OBJECT (ffmpegdec, "Using parser %p", ffmpegdec->pctx);
      else
        GST_LOG_OBJECT (ffmpegdec, "No parser for codec");
      break;
  }

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      ffmpegdec->format.video.width = 0;
      ffmpegdec->format.video.height = 0;
      ffmpegdec->format.video.clip_width = -1;
      ffmpegdec->format.video.clip_height = -1;
      ffmpegdec->format.video.pix_fmt = PIX_FMT_NB;
      ffmpegdec->format.video.interlaced = FALSE;
      break;
    case CODEC_TYPE_AUDIO:
      ffmpegdec->format.audio.samplerate = 0;
      ffmpegdec->format.audio.channels = 0;
      ffmpegdec->format.audio.depth = 0;
      break;
    default:
      break;
  }

  ffmpegdec->last_out = GST_CLOCK_TIME_NONE;
  ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
  /* FIXME, reset_qos holds the LOCK */
  ffmpegdec->proportion = 0.0;
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
gst_ffmpegdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegDec *ffmpegdec;
  GstFFMpegDecClass *oclass;
  GstStructure *structure;
  const GValue *par;
  const GValue *fps;
  gboolean ret = TRUE;

  ffmpegdec = (GstFFMpegDec *) (gst_pad_get_parent (pad));
  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (pad, "setcaps called");

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
  ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;
  ffmpegdec->context->draw_horiz_band = NULL;

  /* assume PTS as input, we will adapt when we detect timestamp reordering
   * in the output frames. */
  ffmpegdec->ts_is_dts = FALSE;
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
  if (par) {
    GST_DEBUG_OBJECT (ffmpegdec, "sink caps have pixel-aspect-ratio of %d:%d",
        gst_value_get_fraction_numerator (par),
        gst_value_get_fraction_denominator (par));
    /* should be NULL */
    if (ffmpegdec->par)
      g_free (ffmpegdec->par);
    ffmpegdec->par = g_new0 (GValue, 1);
    gst_value_init_and_copy (ffmpegdec->par, par);
  }

  /* get the framerate from incomming caps. fps_n is set to -1 when
   * there is no valid framerate */
  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    ffmpegdec->format.video.fps_n = gst_value_get_fraction_numerator (fps);
    ffmpegdec->format.video.fps_d = gst_value_get_fraction_denominator (fps);
    GST_DEBUG_OBJECT (ffmpegdec, "Using framerate %d/%d from incoming caps",
        ffmpegdec->format.video.fps_n, ffmpegdec->format.video.fps_d);
  } else {
    ffmpegdec->format.video.fps_n = -1;
    GST_DEBUG_OBJECT (ffmpegdec, "Using framerate from codec");
  }

  /* figure out if we can use direct rendering */
  ffmpegdec->current_dr = FALSE;
  ffmpegdec->extra_ref = FALSE;
  if (ffmpegdec->direct_rendering) {
    GST_DEBUG_OBJECT (ffmpegdec, "trying to enable direct rendering");
    if (oclass->in_plugin->capabilities & CODEC_CAP_DR1) {
      if (oclass->in_plugin->id == CODEC_ID_H264) {
        GST_DEBUG_OBJECT (ffmpegdec, "disable direct rendering setup for H264");
        /* does not work, many stuff reads outside of the planes */
        ffmpegdec->current_dr = FALSE;
        ffmpegdec->extra_ref = TRUE;
      } else {
        GST_DEBUG_OBJECT (ffmpegdec, "enabled direct rendering");
        ffmpegdec->current_dr = TRUE;
      }
    } else {
      GST_DEBUG_OBJECT (ffmpegdec, "direct rendering not supported");
    }
  }
  if (ffmpegdec->current_dr) {
    /* do *not* draw edges when in direct rendering, for some reason it draws
     * outside of the memory. */
    ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;
  }

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->error_recognition = 1;

  /* for slow cpus */
  ffmpegdec->context->lowres = ffmpegdec->lowres;
  ffmpegdec->context->hurry_up = ffmpegdec->hurry_up;

  /* ffmpeg can draw motion vectors on top of the image (not every decoder
   * supports it) */
  ffmpegdec->context->debug_mv = ffmpegdec->debug_mv;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegdec_open (ffmpegdec))
    goto open_failed;

  /* clipping region */
  gst_structure_get_int (structure, "width",
      &ffmpegdec->format.video.clip_width);
  gst_structure_get_int (structure, "height",
      &ffmpegdec->format.video.clip_height);

  /* take into account the lowres property */
  if (ffmpegdec->format.video.clip_width != -1)
    ffmpegdec->format.video.clip_width >>= ffmpegdec->lowres;
  if (ffmpegdec->format.video.clip_height != -1)
    ffmpegdec->format.video.clip_height >>= ffmpegdec->lowres;

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  gst_object_unref (ffmpegdec);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    if (ffmpegdec->par) {
      g_free (ffmpegdec->par);
      ffmpegdec->par = NULL;
    }
    ret = FALSE;
    goto done;
  }
}

static GstFlowReturn
alloc_output_buffer (GstFFMpegDec * ffmpegdec, GstBuffer ** outbuf,
    gint width, gint height)
{
  GstFlowReturn ret;
  gint fsize;

  ret = GST_FLOW_ERROR;
  *outbuf = NULL;

  GST_LOG_OBJECT (ffmpegdec, "alloc output buffer");

  /* see if we need renegotiation */
  if (G_UNLIKELY (!gst_ffmpegdec_negotiate (ffmpegdec, FALSE)))
    goto negotiate_failed;

  /* get the size of the gstreamer output buffer given a
   * width/height/format */
  fsize = gst_ffmpeg_avpicture_get_size (ffmpegdec->context->pix_fmt,
      width, height);

  if (!ffmpegdec->context->palctrl && ffmpegdec->can_allocate_aligned) {
    GST_LOG_OBJECT (ffmpegdec, "calling pad_alloc");
    /* no pallete, we can use the buffer size to alloc */
    ret = gst_pad_alloc_buffer_and_set_caps (ffmpegdec->srcpad,
        GST_BUFFER_OFFSET_NONE, fsize,
        GST_PAD_CAPS (ffmpegdec->srcpad), outbuf);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto alloc_failed;

    /* If buffer isn't 128-bit aligned, create a memaligned one ourselves */
    if (((uintptr_t) GST_BUFFER_DATA (*outbuf)) % 16) {
      GST_DEBUG_OBJECT (ffmpegdec,
          "Downstream can't allocate aligned buffers.");
      ffmpegdec->can_allocate_aligned = FALSE;
      gst_buffer_unref (*outbuf);
      *outbuf = new_aligned_buffer (fsize, GST_PAD_CAPS (ffmpegdec->srcpad));
    }
  } else {
    GST_LOG_OBJECT (ffmpegdec,
        "not calling pad_alloc, we have a pallete or downstream can't give 16 byte aligned buffers.");
    /* for paletted data we can't use pad_alloc_buffer(), because
     * fsize contains the size of the palette, so the overall size
     * is bigger than ffmpegcolorspace's unit size, which will
     * prompt GstBaseTransform to complain endlessly ... */
    *outbuf = new_aligned_buffer (fsize, GST_PAD_CAPS (ffmpegdec->srcpad));
    ret = GST_FLOW_OK;
  }
  return ret;

  /* special cases */
negotiate_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "negotiate failed");
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "pad_alloc failed");
    return ret;
  }
}

static int
gst_ffmpegdec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstBuffer *buf = NULL;
  GstFFMpegDec *ffmpegdec;
  gint width, height;
  gint coded_width, coded_height;
  gint res;

  ffmpegdec = (GstFFMpegDec *) context->opaque;

  GST_DEBUG_OBJECT (ffmpegdec, "getting buffer, apply pts %" G_GINT64_FORMAT,
      ffmpegdec->in_ts);

  /* apply the last timestamp we have seen to this picture, when we get the
   * picture back from ffmpeg we can use this to correctly timestamp the output
   * buffer */
  picture->pts = ffmpegdec->in_ts;
  picture->reordered_opaque = context->reordered_opaque;
  /* make sure we don't free the buffer when it's not ours */
  picture->opaque = NULL;

  /* take width and height before clipping */
  width = context->width;
  height = context->height;
  coded_width = context->coded_width;
  coded_height = context->coded_height;

  GST_LOG_OBJECT (ffmpegdec, "dimension %dx%d, coded %dx%d", width, height,
      coded_width, coded_height);
  if (!ffmpegdec->current_dr) {
    GST_LOG_OBJECT (ffmpegdec, "direct rendering disabled, fallback alloc");
    res = avcodec_default_get_buffer (context, picture);

    GST_LOG_OBJECT (ffmpegdec, "linsize %d %d %d", picture->linesize[0],
        picture->linesize[1], picture->linesize[2]);
    GST_LOG_OBJECT (ffmpegdec, "data %u %u %u", 0,
        (guint) (picture->data[1] - picture->data[0]),
        (guint) (picture->data[2] - picture->data[0]));
    return res;
  }

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      /* some ffmpeg video plugins don't see the point in setting codec_type ... */
    case CODEC_TYPE_UNKNOWN:
    {
      GstFlowReturn ret;
      gint clip_width, clip_height;

      /* take final clipped output size */
      if ((clip_width = ffmpegdec->format.video.clip_width) == -1)
        clip_width = width;
      if ((clip_height = ffmpegdec->format.video.clip_height) == -1)
        clip_height = height;

      GST_LOG_OBJECT (ffmpegdec, "raw outsize %d/%d", width, height);

      /* this is the size ffmpeg needs for the buffer */
      avcodec_align_dimensions (context, &width, &height);

      GST_LOG_OBJECT (ffmpegdec, "aligned outsize %d/%d, clip %d/%d",
          width, height, clip_width, clip_height);

      if (width != clip_width || height != clip_height) {
        /* We can't alloc if we need to clip the output buffer later */
        GST_LOG_OBJECT (ffmpegdec, "we need clipping, fallback alloc");
        return avcodec_default_get_buffer (context, picture);
      }

      /* alloc with aligned dimensions for ffmpeg */
      ret = alloc_output_buffer (ffmpegdec, &buf, width, height);
      if (G_UNLIKELY (ret != GST_FLOW_OK)) {
        /* alloc default buffer when we can't get one from downstream */
        GST_LOG_OBJECT (ffmpegdec, "alloc failed, fallback alloc");
        return avcodec_default_get_buffer (context, picture);
      }

      /* copy the right pointers and strides in the picture object */
      gst_ffmpeg_avpicture_fill ((AVPicture *) picture,
          GST_BUFFER_DATA (buf), context->pix_fmt, width, height);
      break;
    }
    case CODEC_TYPE_AUDIO:
    default:
      GST_ERROR_OBJECT (ffmpegdec,
          "_get_buffer() should never get called for non-video buffers !");
      g_assert_not_reached ();
      break;
  }

  /* tell ffmpeg we own this buffer, tranfer the ref we have on the buffer to
   * the opaque data. */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = 256 * 256 * 256 * 64;
  picture->opaque = buf;

#ifdef EXTRA_REF
  if (picture->reference != 0 || ffmpegdec->extra_ref) {
    GST_DEBUG_OBJECT (ffmpegdec, "adding extra ref");
    gst_buffer_ref (buf);
  }
#endif

  GST_LOG_OBJECT (ffmpegdec, "returned buffer %p", buf);

  return 0;
}

static void
gst_ffmpegdec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstBuffer *buf;
  GstFFMpegDec *ffmpegdec;

  ffmpegdec = (GstFFMpegDec *) context->opaque;

  /* check if it was our buffer */
  if (picture->opaque == NULL) {
    GST_DEBUG_OBJECT (ffmpegdec, "default release buffer");
    avcodec_default_release_buffer (context, picture);
    return;
  }

  /* we remove the opaque data now */
  buf = GST_BUFFER_CAST (picture->opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "release buffer %p", buf);
  picture->opaque = NULL;

#ifdef EXTRA_REF
  if (picture->reference != 0 || ffmpegdec->extra_ref) {
    GST_DEBUG_OBJECT (ffmpegdec, "remove extra ref");
    gst_buffer_unref (buf);
  }
#else
  gst_buffer_unref (buf);
#endif

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}

static void
gst_ffmpegdec_add_pixel_aspect_ratio (GstFFMpegDec * ffmpegdec,
    GstStructure * s)
{
  gboolean demuxer_par_set = FALSE;
  gboolean decoder_par_set = FALSE;
  gint demuxer_num = 1, demuxer_denom = 1;
  gint decoder_num = 1, decoder_denom = 1;

  GST_OBJECT_LOCK (ffmpegdec);

  if (ffmpegdec->par) {
    demuxer_num = gst_value_get_fraction_numerator (ffmpegdec->par);
    demuxer_denom = gst_value_get_fraction_denominator (ffmpegdec->par);
    demuxer_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Demuxer PAR: %d:%d", demuxer_num,
        demuxer_denom);
  }

  if (ffmpegdec->context->sample_aspect_ratio.num &&
      ffmpegdec->context->sample_aspect_ratio.den) {
    decoder_num = ffmpegdec->context->sample_aspect_ratio.num;
    decoder_denom = ffmpegdec->context->sample_aspect_ratio.den;
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
    gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, decoder_num,
        decoder_denom, NULL);
    return;
  }

use_demuxer_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Setting demuxer provided pixel-aspect-ratio of %u:%u", demuxer_num,
        demuxer_denom);
    gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, demuxer_num,
        demuxer_denom, NULL);
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Neither demuxer nor codec provide a pixel-aspect-ratio");
    return;
  }
}

static gboolean
gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec, gboolean force)
{
  GstFFMpegDecClass *oclass;
  GstCaps *caps;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      if (!force && ffmpegdec->format.video.width == ffmpegdec->context->width
          && ffmpegdec->format.video.height == ffmpegdec->context->height
          && ffmpegdec->format.video.fps_n == ffmpegdec->format.video.old_fps_n
          && ffmpegdec->format.video.fps_d == ffmpegdec->format.video.old_fps_d
          && ffmpegdec->format.video.pix_fmt == ffmpegdec->context->pix_fmt)
        return TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "Renegotiating video from %dx%d@ %d/%d fps to %dx%d@ %d/%d fps",
          ffmpegdec->format.video.width, ffmpegdec->format.video.height,
          ffmpegdec->format.video.old_fps_n, ffmpegdec->format.video.old_fps_n,
          ffmpegdec->context->width, ffmpegdec->context->height,
          ffmpegdec->format.video.fps_n, ffmpegdec->format.video.fps_d);
      ffmpegdec->format.video.width = ffmpegdec->context->width;
      ffmpegdec->format.video.height = ffmpegdec->context->height;
      ffmpegdec->format.video.old_fps_n = ffmpegdec->format.video.fps_n;
      ffmpegdec->format.video.old_fps_d = ffmpegdec->format.video.fps_d;
      ffmpegdec->format.video.pix_fmt = ffmpegdec->context->pix_fmt;
      break;
    case CODEC_TYPE_AUDIO:
    {
      gint depth = av_smp_format_depth (ffmpegdec->context->sample_fmt);
      if (!force && ffmpegdec->format.audio.samplerate ==
          ffmpegdec->context->sample_rate &&
          ffmpegdec->format.audio.channels == ffmpegdec->context->channels &&
          ffmpegdec->format.audio.depth == depth)
        return TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "Renegotiating audio from %dHz@%dchannels (%d) to %dHz@%dchannels (%d)",
          ffmpegdec->format.audio.samplerate, ffmpegdec->format.audio.channels,
          ffmpegdec->format.audio.depth,
          ffmpegdec->context->sample_rate, ffmpegdec->context->channels, depth);
      ffmpegdec->format.audio.samplerate = ffmpegdec->context->sample_rate;
      ffmpegdec->format.audio.channels = ffmpegdec->context->channels;
      ffmpegdec->format.audio.depth = depth;
    }
      break;
    default:
      break;
  }

  caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
      ffmpegdec->context, oclass->in_plugin->id, FALSE);

  if (caps == NULL)
    goto no_caps;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
    {
      gint width, height;
      gboolean interlaced;

      width = ffmpegdec->format.video.clip_width;
      height = ffmpegdec->format.video.clip_height;
      interlaced = ffmpegdec->format.video.interlaced;

      if (width != -1 && height != -1) {
        /* overwrite the output size with the dimension of the
         * clipping region */
        gst_caps_set_simple (caps,
            "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
      }
      gst_caps_set_simple (caps, "interlaced", G_TYPE_BOOLEAN, interlaced,
          NULL);

      /* If a demuxer provided a framerate then use it (#313970) */
      if (ffmpegdec->format.video.fps_n != -1) {
        gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, ffmpegdec->format.video.fps_n,
            ffmpegdec->format.video.fps_d, NULL);
      }
      gst_ffmpegdec_add_pixel_aspect_ratio (ffmpegdec,
          gst_caps_get_structure (caps, 0));
      break;
    }
    case CODEC_TYPE_AUDIO:
    {
      break;
    }
    default:
      break;
  }

  if (!gst_pad_set_caps (ffmpegdec->srcpad, caps))
    goto caps_failed;

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("could not find caps for codec (), unknown type"));
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
 * Sets the hurry_up flag and if things are really bad, skips to the next
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

  *mode_switch = FALSE;

  /* no timestamp, can't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp)))
    goto no_qos;

  /* get latest QoS observation values */
  gst_ffmpegdec_read_qos (ffmpegdec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    /* no hurry_up initialy */
    ffmpegdec->context->hurry_up = 0;
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
    /* if we're more than two seconds late, switch to the next keyframe */
    /* FIXME, let the demuxer decide what's the best since we might be dropping
     * a lot of frames when the keyframe is far away or we even might not get a new
     * keyframe at all.. */
    if (diff > ((GstClockTimeDiff) GST_SECOND * 2)
        && !ffmpegdec->waiting_for_key) {
      goto skip_to_keyframe;
    } else if (diff >= 0) {
      /* we're too slow, try to speed up */
      if (ffmpegdec->waiting_for_key) {
        /* we were waiting for a keyframe, that's ok */
        goto skipping;
      }
      /* switch to hurry_up mode */
      goto hurry_up;
    }
  }

no_qos:
  return TRUE;

skipping:
  {
    return FALSE;
  }
normal_mode:
  {
    if (ffmpegdec->context->hurry_up != 0) {
      ffmpegdec->context->hurry_up = 0;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec, "QOS: normal mode %g < 0.4", proportion);
    }
    return TRUE;
  }
skip_to_keyframe:
  {
    ffmpegdec->context->hurry_up = 1;
    ffmpegdec->waiting_for_key = TRUE;
    *mode_switch = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec,
        "QOS: keyframe, %" G_GINT64_FORMAT " > GST_SECOND/2", diff);
    /* we can skip the current frame */
    return FALSE;
  }
hurry_up:
  {
    if (ffmpegdec->context->hurry_up != 1) {
      ffmpegdec->context->hurry_up = 1;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "QOS: hurry up, diff %" G_GINT64_FORMAT " >= 0", diff);
    }
    return TRUE;
  }
}

/* returns TRUE if buffer is within segment, else FALSE.
 * if Buffer is on segment border, it's timestamp and duration will be clipped */
static gboolean
clip_video_buffer (GstFFMpegDec * dec, GstBuffer * buf, GstClockTime in_ts,
    GstClockTime in_dur)
{
  gboolean res = TRUE;
  gint64 cstart, cstop;
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

  ret = GST_FLOW_OK;
  *outbuf = NULL;

  if (ffmpegdec->picture->opaque != NULL) {
    /* we allocated a picture already for ffmpeg to decode into, let's pick it
     * up and use it now. */
    *outbuf = (GstBuffer *) ffmpegdec->picture->opaque;
    GST_LOG_OBJECT (ffmpegdec, "using opaque buffer %p", *outbuf);
#ifndef EXTRA_REF
    gst_buffer_ref (*outbuf);
#endif
  } else {
    AVPicture pic, *outpic;
    gint width, height;

    GST_LOG_OBJECT (ffmpegdec, "get output buffer");

    /* figure out size of output buffer, this is the clipped output size because
     * we will copy the picture into it. */
    if ((width = ffmpegdec->format.video.clip_width) == -1)
      width = ffmpegdec->context->width;
    if ((height = ffmpegdec->format.video.clip_height) == -1)
      height = ffmpegdec->context->height;

    GST_LOG_OBJECT (ffmpegdec, "clip width %d/height %d", width, height);

    ret = alloc_output_buffer (ffmpegdec, outbuf, width, height);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto alloc_failed;

    /* original ffmpeg code does not handle odd sizes correctly.
     * This patched up version does */
    gst_ffmpeg_avpicture_fill (&pic, GST_BUFFER_DATA (*outbuf),
        ffmpegdec->context->pix_fmt, width, height);

    outpic = (AVPicture *) ffmpegdec->picture;

    GST_LOG_OBJECT (ffmpegdec, "linsize %d %d %d", outpic->linesize[0],
        outpic->linesize[1], outpic->linesize[2]);
    GST_LOG_OBJECT (ffmpegdec, "data %u %u %u", 0,
        (guint) (outpic->data[1] - outpic->data[0]),
        (guint) (outpic->data[2] - outpic->data[0]));

    av_picture_copy (&pic, outpic, ffmpegdec->context->pix_fmt, width, height);
  }
  ffmpegdec->picture->pts = -1;

  return ret;

  /* special cases */
alloc_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "pad_alloc failed");
    return ret;
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

static gpointer
opaque_store (GstFFMpegDec * ffmpegdec, guint64 ts, guint64 offset)
{
  GstDataPassThrough *opaque = g_slice_new0 (GstDataPassThrough);
  opaque->ts = ts;
  opaque->offset = offset;
  ffmpegdec->opaque = g_list_append (ffmpegdec->opaque, (gpointer) opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "Stored ts:%" GST_TIME_FORMAT ", offset:%"
      G_GUINT64_FORMAT " as opaque %p", GST_TIME_ARGS (ts), offset,
      (gpointer) opaque);
  return opaque;
}

static gboolean
opaque_find (GstFFMpegDec * ffmpegdec, gpointer opaque_val, guint64 * _ts,
    gint64 * _offset)
{
  GstClockTime ts = GST_CLOCK_TIME_NONE;
  gint64 offset = GST_BUFFER_OFFSET_NONE;
  GList *i;
  for (i = ffmpegdec->opaque; i != NULL; i = g_list_next (i)) {
    if (i->data == (gpointer) opaque_val) {
      ts = ((GstDataPassThrough *) i->data)->ts;
      offset = ((GstDataPassThrough *) i->data)->offset;
      GST_DEBUG_OBJECT (ffmpegdec,
          "Found opaque %p - ts:%" GST_TIME_FORMAT ", offset:%" G_GINT64_FORMAT,
          i->data, GST_TIME_ARGS (ts), offset);
      if (_ts)
        *_ts = ts;
      if (_offset)
        *_offset = offset;
      g_slice_free (GstDataPassThrough, i->data);
      ffmpegdec->opaque = g_list_delete_link (ffmpegdec->opaque, i);
      return TRUE;
    }
  }
  return FALSE;
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
    GstClockTime in_timestamp, GstClockTime in_duration,
    gint64 in_offset, GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data;
  gboolean iskeyframe;
  gboolean mode_switch;
  gboolean decode;
  gint hurry_up = 0;
  GstClockTime out_timestamp, out_duration, out_pts;
  gint64 out_offset;

  *ret = GST_FLOW_OK;
  *outbuf = NULL;

  ffmpegdec->context->opaque = ffmpegdec;

  /* in case we skip frames */
  ffmpegdec->picture->pict_type = -1;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  decode = gst_ffmpegdec_do_qos (ffmpegdec, in_timestamp, &mode_switch);

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
    /* no decoding needed, save previous hurry_up value and brutely skip
     * decoding everything */
    hurry_up = ffmpegdec->context->hurry_up;
    ffmpegdec->context->hurry_up = 2;
  }

  GST_DEBUG_OBJECT (ffmpegdec,
      "Going to store opaque values, current ts:%" GST_TIME_FORMAT ", offset: %"
      G_GINT64_FORMAT, GST_TIME_ARGS (in_timestamp), in_offset);

  out_timestamp = gst_ts_handler_get_ts (ffmpegdec, &out_offset);
  /* Never do this at home...
   * 1) We know that ffmpegdec->context->reordered_opaque is 64-bit, and thus
   * is capable of holding virtually anything including pointers
   * (unless we're on 128-bit platform...)
   */
  ffmpegdec->context->reordered_opaque = (gint64)
      GPOINTER_TO_SIZE (opaque_store (ffmpegdec, out_timestamp, out_offset));

  /* now decode the frame */
  len = avcodec_decode_video (ffmpegdec->context,
      ffmpegdec->picture, &have_data, data, size);

  gst_ts_handler_consume (ffmpegdec, len);

  /* restore previous state */
  if (!decode)
    ffmpegdec->context->hurry_up = hurry_up;

  GST_DEBUG_OBJECT (ffmpegdec, "after decode: len %d, have_data %d",
      len, have_data);

  /* when we are in hurry_up mode, don't complain when ffmpeg returned
   * no data because we told it to skip stuff. */
  if (len < 0 && (mode_switch || ffmpegdec->context->hurry_up))
    len = 0;

  if (len > 0 && have_data <= 0 && (mode_switch
          || ffmpegdec->context->hurry_up)) {
    /* we consumed some bytes but nothing decoded and we are skipping frames,
     * disable the interpollation of DTS timestamps */
    ffmpegdec->ts_is_dts = FALSE;
    ffmpegdec->last_out = -1;
  }

  /* no data, we're done */
  if (len < 0 || have_data <= 0)
    goto beach;

  /* recuperate the reordered timestamp */
  if (!opaque_find (ffmpegdec,
          (gpointer) (gulong) ffmpegdec->picture->reordered_opaque, &out_pts,
          &out_offset)) {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to find opaque %p",
        (gpointer) (gulong) ffmpegdec->picture->reordered_opaque);
    out_pts = -1;
    out_offset = GST_BUFFER_OFFSET_NONE;
  } else {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Found opaque values, current ts:%" GST_TIME_FORMAT ", offset: %"
        G_GINT64_FORMAT, GST_TIME_ARGS (in_timestamp), in_offset);
  }

  GST_DEBUG_OBJECT (ffmpegdec, "ts-handler: pts %" G_GUINT64_FORMAT, out_pts);
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
  GST_DEBUG_OBJECT (ffmpegdec, "repeat_pict:%d",
      ffmpegdec->picture->repeat_pict);

  if (G_UNLIKELY (ffmpegdec->picture->interlaced_frame !=
          ffmpegdec->format.video.interlaced)) {
    GST_WARNING ("Change in interlacing ! picture:%d, recorded:%d",
        ffmpegdec->picture->interlaced_frame,
        ffmpegdec->format.video.interlaced);
    ffmpegdec->format.video.interlaced = ffmpegdec->picture->interlaced_frame;
    gst_ffmpegdec_negotiate (ffmpegdec, TRUE);
  }

  /* check if we are dealing with a keyframe here, this will also check if we
   * are dealing with B frames. */
  iskeyframe = check_keyframe (ffmpegdec);

  /* check that the timestamps go upwards */
  if (ffmpegdec->last_out != -1) {
    if (ffmpegdec->last_out > out_pts) {
      /* timestamps go backwards, this means frames were reordered and we must
       * be dealing with DTS as the buffer timestamps */
      ffmpegdec->ts_is_dts = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "timestamp discont, we have DTS as timestamps");
    }
  }

  if (out_pts == 0 && out_pts == ffmpegdec->last_out) {
    GST_LOG_OBJECT (ffmpegdec, "ffmpeg returns 0 timestamps, ignoring");
    /* some codecs only output 0 timestamps, when that happens, make us select an
     * output timestamp based on the input timestamp. We do this by making the
     * ffmpeg timestamp and the interpollated next timestamp invalid. */
    out_pts = -1;
    ffmpegdec->next_ts = -1;
  } else
    ffmpegdec->last_out = out_pts;

  if (ffmpegdec->ts_is_dts) {
    /* we are dealing with DTS as the timestamps, only copy the DTS on the picture
     * to the PTS of the output frame if we are dealing with a non-reference
     * frame, else we leave the timestamp as -1, which will interpollate from the
     * last outputted value. */
    if (ffmpegdec->context->has_b_frames && ffmpegdec->has_b_frames &&
        ffmpegdec->picture->reference && ffmpegdec->next_ts != -1) {
      /* we have b frames and this picture is a reference picture, don't use the
       * DTS as the PTS, same for offset */
      GST_DEBUG_OBJECT (ffmpegdec, "DTS as timestamps, interpolate");
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
  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp) && ffmpegdec->next_ts != -1) {
    out_timestamp = ffmpegdec->next_ts;
    GST_LOG_OBJECT (ffmpegdec, "using next timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (out_timestamp));
  }
  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
    out_timestamp = in_timestamp;
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
    /* out_offset already contains the offset from opaque_find() call */
  } else if (out_timestamp != GST_CLOCK_TIME_NONE) {
    GstFormat out_fmt = GST_FORMAT_DEFAULT;
    GST_LOG_OBJECT (ffmpegdec, "Using offset converted from timestamp");
    gst_pad_query_peer_convert (ffmpegdec->sinkpad,
        GST_FORMAT_TIME, out_timestamp, &out_fmt, &out_offset);
  } else if (in_offset != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (ffmpegdec, "using in_offset %" G_GINT64_FORMAT, in_offset);
    out_offset = in_offset;
  } else {
    GST_LOG_OBJECT (ffmpegdec, "no valid offset found");
    out_offset = GST_BUFFER_OFFSET_NONE;
  }
  GST_BUFFER_OFFSET (*outbuf) = out_offset;

  /*
   * Duration:
   *
   *  1) Copy input duration if valid
   *  2) else use input framerate
   *  3) else use ffmpeg framerate
   */
  out_duration = -1;
  if (!GST_CLOCK_TIME_IS_VALID (in_duration)) {
    /* if we have an input framerate, use that */
    if (ffmpegdec->format.video.fps_n != -1 &&
        (ffmpegdec->format.video.fps_n != 1000 &&
            ffmpegdec->format.video.fps_d != 1)) {
      GST_LOG_OBJECT (ffmpegdec, "using input framerate for duration");
      out_duration = gst_util_uint64_scale_int (GST_SECOND,
          ffmpegdec->format.video.fps_d, ffmpegdec->format.video.fps_n);
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
  } else {
    GST_LOG_OBJECT (ffmpegdec, "using in_duration");
    out_duration = in_duration;
  }

  /* Take repeat_pict into account */
  if (GST_CLOCK_TIME_IS_VALID (out_duration)) {
    out_duration += out_duration * ffmpegdec->picture->repeat_pict / 2;
  }
  GST_BUFFER_DURATION (*outbuf) = out_duration;

  if (out_timestamp != -1 && out_duration != -1)
    ffmpegdec->next_ts = out_timestamp + out_duration;

  /* palette is not part of raw video frame in gst and the size
   * of the outgoing buffer needs to be adjusted accordingly */
  if (ffmpegdec->context->palctrl != NULL)
    GST_BUFFER_SIZE (*outbuf) -= AVPALETTE_SIZE;

  /* now see if we need to clip the buffer against the segment boundaries. */
  if (G_UNLIKELY (!clip_video_buffer (ffmpegdec, *outbuf, out_timestamp,
              out_duration)))
    goto clipped;

  /* mark as keyframe or delta unit */
  if (!iskeyframe)
    GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (ffmpegdec->picture->top_field_first)
    GST_BUFFER_FLAG_SET (*outbuf, GST_VIDEO_BUFFER_TFF);


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
  gint64 diff, ctime, cstop;
  gboolean res = TRUE;

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT ", duration:%" GST_TIME_FORMAT
      ", size %u", GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur),
      GST_BUFFER_SIZE (buf));

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

    GST_BUFFER_SIZE (buf) -= diff;
    GST_BUFFER_DATA (buf) += diff;
  }
  if (G_UNLIKELY ((diff = stop - cstop) > 0)) {
    /* bring clipped time to bytes */
    diff =
        gst_util_uint64_scale_int (diff, dec->format.audio.samplerate,
        GST_SECOND) * (dec->format.audio.depth * dec->format.audio.channels);

    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

    GST_BUFFER_SIZE (buf) -= diff;
  }
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
    GstClockTime in_timestamp, GstClockTime in_duration,
    gint64 in_offset, GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data = AVCODEC_MAX_AUDIO_FRAME_SIZE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "size:%d, offset:%" G_GINT64_FORMAT ", ts:%" GST_TIME_FORMAT ", dur:%"
      GST_TIME_FORMAT ", ffmpegdec->next_ts:%" GST_TIME_FORMAT, size,
      in_offset, GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration),
      GST_TIME_ARGS (ffmpegdec->next_ts));

  *outbuf =
      new_aligned_buffer (AVCODEC_MAX_AUDIO_FRAME_SIZE,
      GST_PAD_CAPS (ffmpegdec->srcpad));

  len = avcodec_decode_audio2 (ffmpegdec->context,
      (int16_t *) GST_BUFFER_DATA (*outbuf), &have_data, data, size);
  GST_DEBUG_OBJECT (ffmpegdec,
      "Decode audio: len=%d, have_data=%d", len, have_data);

  if (len >= 0 && have_data > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Creating output buffer");
    if (!gst_ffmpegdec_negotiate (ffmpegdec, FALSE)) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      len = -1;
      goto beach;
    }

    /* Buffer size */
    GST_BUFFER_SIZE (*outbuf) = have_data;

    /*
     * Timestamps:
     *
     *  1) Copy input timestamp if valid
     *  2) else interpolate from previous input timestamp
     */
    /* always take timestamps from the input buffer if any */
    if (!GST_CLOCK_TIME_IS_VALID (in_timestamp)) {
      in_timestamp = ffmpegdec->next_ts;
    }

    /*
     * Duration:
     *
     *  1) calculate based on number of samples
     */
    in_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        ffmpegdec->format.audio.depth * ffmpegdec->format.audio.channels *
        ffmpegdec->format.audio.samplerate);

    GST_DEBUG_OBJECT (ffmpegdec,
        "Buffer created. Size:%d , timestamp:%" GST_TIME_FORMAT " , duration:%"
        GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration));

    GST_BUFFER_TIMESTAMP (*outbuf) = in_timestamp;
    GST_BUFFER_DURATION (*outbuf) = in_duration;
    GST_BUFFER_OFFSET (*outbuf) = in_offset;

    /* the next timestamp we'll use when interpolating */
    ffmpegdec->next_ts = in_timestamp + in_duration;

    /* now see if we need to clip the buffer against the segment boundaries. */
    if (G_UNLIKELY (!clip_audio_buffer (ffmpegdec, *outbuf, in_timestamp,
                in_duration)))
      goto clipped;

  } else if (len > 0 && have_data == 0) {
    /* cache output, because it may be used for caching (in-place) */
    *outbuf = NULL;
  } else {
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  /* If we don't error out after the first failed read with the AAC decoder,
   * we must *not* carry on pushing data, else we'll cause segfaults... */
  if (len == -1 && in_plugin->id == CODEC_ID_AAC) {
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
    guint8 * data, guint size, gint * got_data,
    GstClockTime in_timestamp, GstClockTime in_duration, gint64 in_offset,
    GstFlowReturn * ret)
{
  GstFFMpegDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec,
      "data:%p, size:%d, offset:%" G_GINT64_FORMAT ", ts:%" GST_TIME_FORMAT
      ", dur:%" GST_TIME_FORMAT, data, size, in_offset,
      GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration));

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      len =
          gst_ffmpegdec_video_frame (ffmpegdec, data, size, in_timestamp,
          in_duration, in_offset, &outbuf, ret);
      break;
    case CODEC_TYPE_AUDIO:
      len =
          gst_ffmpegdec_audio_frame (ffmpegdec, oclass->in_plugin, data, size,
          in_timestamp, in_duration, in_offset, &outbuf, ret);

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
    /* set caps */
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegdec->srcpad));

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

      len = gst_ffmpegdec_frame (ffmpegdec, NULL, 0, &have_data,
          GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, -1, &ret);
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
  if (ffmpegdec->pcache) {
    gst_buffer_unref (ffmpegdec->pcache);
    ffmpegdec->pcache = NULL;
  }
  if (ffmpegdec->pctx) {
    GstFFMpegDecClass *oclass;

    oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

    av_parser_close (ffmpegdec->pctx);
    ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
  }
}

static gboolean
gst_ffmpegdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec;
  GstFFMpegDecClass *oclass;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegDec *) gst_pad_get_parent (pad);
  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

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
      ffmpegdec->last_out = GST_CLOCK_TIME_NONE;
      ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
      gst_ffmpegdec_reset_qos (ffmpegdec);
      gst_ffmpegdec_flush_pcache (ffmpegdec);
      gst_ts_handler_init (ffmpegdec);
      ffmpegdec->waiting_for_key = TRUE;
      gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
      clear_queued (ffmpegdec);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat fmt;
      gint64 start, stop, time;
      gdouble rate, arate;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
          &start, &stop, &time);

      switch (fmt) {
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
          if (start != -1)
            start = gst_util_uint64_scale_int (start, GST_SECOND, bit_rate);
          if (stop != -1)
            stop = gst_util_uint64_scale_int (stop, GST_SECOND, bit_rate);
          if (time != -1)
            time = gst_util_uint64_scale_int (time, GST_SECOND, bit_rate);

          /* unref old event */
          gst_event_unref (event);

          /* create new converted time segment */
          fmt = GST_FORMAT_TIME;
          /* FIXME, bitrate is not good enough too find a good stop, let's
           * hope start and time were 0... meh. */
          stop = -1;
          event = gst_event_new_new_segment (update, rate, fmt,
              start, stop, time);
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

      GST_DEBUG_OBJECT (ffmpegdec,
          "NEWSEGMENT in time start %" GST_TIME_FORMAT " -- stop %"
          GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      /* and store the values */
      gst_segment_set_newsegment_full (&ffmpegdec->segment, update,
          rate, arate, fmt, start, stop, time);
      break;
    }
    default:
      break;
  }

  /* and push segment downstream */
  ret = gst_pad_push_event (ffmpegdec->srcpad, event);

done:
  gst_object_unref (ffmpegdec);

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

static GstFlowReturn
gst_ffmpegdec_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFFMpegDec *ffmpegdec;
  GstFFMpegDecClass *oclass;
  guint8 *data, *bdata, *pdata;
  gint size, bsize, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_timestamp, in_duration;
  gboolean discont;
  gint64 in_offset;

  ffmpegdec = (GstFFMpegDec *) (GST_PAD_PARENT (pad));

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
    ffmpegdec->last_out = GST_CLOCK_TIME_NONE;
    ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
  }
  /* by default we clear the input timestamp after decoding each frame so that
   * interpollation can work. */
  ffmpegdec->clear_ts = TRUE;

  /* append the unaltered buffer timestamp to list */
  gst_ts_handler_append (ffmpegdec, inbuf);

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* do early keyframe check pretty bad to rely on the keyframe flag in the
   * source for this as it might not even be parsed (UDP/file/..).  */
  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    GST_DEBUG_OBJECT (ffmpegdec, "waiting for keyframe");
    if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DELTA_UNIT) &&
        oclass->in_plugin->type != CODEC_TYPE_AUDIO)
      goto skip_keyframe;

    GST_DEBUG_OBJECT (ffmpegdec, "got keyframe");
    ffmpegdec->waiting_for_key = FALSE;
  }

  in_timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  in_duration = GST_BUFFER_DURATION (inbuf);
  in_offset = GST_BUFFER_OFFSET (inbuf);

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %u, offset:%" G_GUINT64_FORMAT ", ts:%"
      GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (inbuf), GST_BUFFER_OFFSET (inbuf),
      GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration));

  /* parse cache joining. If there is cached data, its timestamp will be what we
   * send to the parse. */
  if (ffmpegdec->pcache) {
    /* use timestamp and duration of what is in the cache */
    in_timestamp = GST_BUFFER_TIMESTAMP (ffmpegdec->pcache);
    in_duration = GST_BUFFER_DURATION (ffmpegdec->pcache);
    in_offset = GST_BUFFER_OFFSET (ffmpegdec->pcache);

    /* join with previous data */
    inbuf = gst_buffer_join (ffmpegdec->pcache, inbuf);

    GST_LOG_OBJECT (ffmpegdec,
        "joined parse cache, inbuf now has offset %" G_GINT64_FORMAT ", ts:%"
        GST_TIME_FORMAT, in_offset, GST_TIME_ARGS (in_timestamp));

    /* no more cached data, we assume we can consume the complete cache */
    ffmpegdec->pcache = NULL;
  }

  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == CODEC_ID_SVQ3) {
    inbuf = gst_buffer_make_writable (inbuf);
  }

  bdata = GST_BUFFER_DATA (inbuf);
  bsize = GST_BUFFER_SIZE (inbuf);

  do {
    /* parse, if at all possible */
    if (ffmpegdec->pctx) {
      gint res;

      GST_LOG_OBJECT (ffmpegdec,
          "Calling av_parser_parse with offset %" G_GINT64_FORMAT ", ts:%"
          GST_TIME_FORMAT, in_offset, GST_TIME_ARGS (in_timestamp));

      /* feed the parser. We store the raw gstreamer timestamp because
       * converting it to ffmpeg timestamps can corrupt it if the framerate is
       * wrong. */
      res = av_parser_parse (ffmpegdec->pctx, ffmpegdec->context,
          &data, &size, bdata, bsize, in_timestamp, in_timestamp);

      GST_LOG_OBJECT (ffmpegdec,
          "parser returned res %d and size %d", res, size);

      /* store pts for get_buffer */
      ffmpegdec->in_ts = ffmpegdec->pctx->pts;

      GST_LOG_OBJECT (ffmpegdec, "consuming %d bytes. ts:%"
          GST_TIME_FORMAT, size, GST_TIME_ARGS (ffmpegdec->pctx->pts));

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
      data = bdata;
      size = bsize;

      ffmpegdec->in_ts = in_timestamp;
      ffmpegdec->in_offset = in_offset;
    }

    if (ffmpegdec->do_padding) {
      /* add padding */
      if (ffmpegdec->padded_size < size + FF_INPUT_BUFFER_PADDING_SIZE) {
        ffmpegdec->padded_size = size + FF_INPUT_BUFFER_PADDING_SIZE;
        ffmpegdec->padded =
            g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
        GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
            ffmpegdec->padded_size);
      }
      memcpy (ffmpegdec->padded, data, size);
      memset (ffmpegdec->padded + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

      pdata = ffmpegdec->padded;
    } else {
      pdata = data;
    }

    /* decode a frame of audio/video now */
    len =
        gst_ffmpegdec_frame (ffmpegdec, pdata, size, &have_data, in_timestamp,
        in_duration, in_offset, &ret);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow retun, make sure we discard all data and exit */
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
    } else {
      ffmpegdec->clear_ts = TRUE;
    }

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  /* keep left-over */
  if (ffmpegdec->pctx && bsize > 0) {
    in_timestamp = GST_BUFFER_TIMESTAMP (inbuf);
    in_offset = GST_BUFFER_OFFSET (inbuf);

    GST_LOG_OBJECT (ffmpegdec,
        "Keeping %d bytes of data with offset %" G_GINT64_FORMAT ", timestamp %"
        GST_TIME_FORMAT, bsize, in_offset, GST_TIME_ARGS (in_timestamp));

    ffmpegdec->pcache = gst_buffer_create_sub (inbuf,
        GST_BUFFER_SIZE (inbuf) - bsize, bsize);
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
      ffmpegdec->can_allocate_aligned = TRUE;
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
      ffmpegdec->hurry_up = ffmpegdec->context->hurry_up =
          g_value_get_enum (value);
      break;
    case PROP_DIRECT_RENDERING:
      ffmpegdec->direct_rendering = g_value_get_boolean (value);
      break;
    case PROP_DO_PADDING:
      ffmpegdec->do_padding = g_value_get_boolean (value);
      break;
    case PROP_DEBUG_MV:
      ffmpegdec->debug_mv = ffmpegdec->context->debug_mv =
          g_value_get_boolean (value);
      break;
    case PROP_CROP:
      ffmpegdec->crop = g_value_get_boolean (value);
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
      g_value_set_enum (value, ffmpegdec->context->hurry_up);
      break;
    case PROP_DIRECT_RENDERING:
      g_value_set_boolean (value, ffmpegdec->direct_rendering);
      break;
    case PROP_DO_PADDING:
      g_value_set_boolean (value, ffmpegdec->do_padding);
      break;
    case PROP_DEBUG_MV:
      g_value_set_boolean (value, ffmpegdec->context->debug_mv);
      break;
    case PROP_CROP:
      g_value_set_boolean (value, ffmpegdec->crop);
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
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_F64LE)) {
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
        !strcmp (in_plugin->name, "mpeg4aac")) {
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
      case CODEC_ID_DVVIDEO:
        /* we have a good dv decoder, fast on both ppc as well as x86. they say
           libdv's quality is better though. leave as secondary.
           note: if you change this, see the code in gstdv.c in good/ext/dv. */
        rank = GST_RANK_SECONDARY;
        break;
      case CODEC_ID_AAC:
        /* The ffmpeg AAC decoder isn't complete, and there's no way to figure out
         * before decoding whether it will support the given stream or not.
         * We therefore set it to NONE until it can handle the full specs. */
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

/** initialize the timestamp handler */
static void
gst_ts_handler_init (GstFFMpegDec * ffmpegdec)
{
  GstTSHandler *ts_handler = &ffmpegdec->ts_handler;
  memset (ts_handler, 0, sizeof (GstTSHandler));
  ts_handler->buf_tail = 1;
}

/** add a new entry to the list from a GstBuffer */
static void
gst_ts_handler_append (GstFFMpegDec * ffmpegdec, GstBuffer * buffer)
{
  GstTSHandler *ts_handler = &ffmpegdec->ts_handler;
  guint64 ts = GST_BUFFER_TIMESTAMP (buffer);
  gint size = GST_BUFFER_SIZE (buffer);
  guint64 offset = GST_BUFFER_OFFSET (buffer);
  gint ind = ts_handler->buf_head;

  if ((ts != -1) || (ts == -1 && !ts_handler->buf_count)) {
    /* null timestamps are only valid for the first entry */
    TS_MAP_INC (ind);
    /** debugging trace
    printf ("app [%02X] %4d\t%6.3f\t%8d\n",
        ind, ts_handler->buf_count,
        ts != -1 ? (double) ts / GST_SECOND : -1.0, size);
    **/
    GST_LOG_OBJECT (ffmpegdec, "store timestamp @ index [%02X] buf_count: %d"
        " ts: %" GST_TIME_FORMAT ", offset: %" G_GUINT64_FORMAT ", size: %d",
        ind, ts_handler->buf_count, GST_TIME_ARGS (ts), offset, size);
    ts_handler->buffers[ind].ts = ts;
    ts_handler->buffers[ind].offset = offset;
    ts_handler->buffers[ind].size = size;
    ts_handler->buf_head = ind;
    ts_handler->buf_count++;
  } else {
    /* append size to existing entry */
    GST_LOG_OBJECT (ffmpegdec, "Extending index [%02X] buf_count: %d"
        " ts: %" GST_TIME_FORMAT ", offset: %" G_GUINT64_FORMAT
        ", new size: %d",
        ind, ts_handler->buf_count,
        GST_TIME_ARGS (ts_handler->buffers[ind].ts),
        ts_handler->buffers[ind].offset, ts_handler->buffers[ind].size);
    ts_handler->buffers[ind].size += size;
  }
}

/** indicate that the decoder has consumed some data */
static void
gst_ts_handler_consume (GstFFMpegDec * ffmpegdec, gint size)
{
  GstTSHandler *ts_handler = &ffmpegdec->ts_handler;
  gint buf = ts_handler->buf_tail;

  /* eat some bytes from the buffer map */
  while (size > 0 && ts_handler->buf_count > 0) {
    GST_LOG_OBJECT (ffmpegdec, "Stepping over %d bytes @ index %d has %d bytes",
        size, buf, ts_handler->buffers[buf].size);
    if (size >= ts_handler->buffers[buf].size) {
      size -= ts_handler->buffers[buf].size;
      /* reset this entry */
      memset (ts_handler->buffers + buf, -1, sizeof (GstTSMap));
      TS_MAP_INC (buf);
      /* Decrement the active buffer count */
      ts_handler->buf_count--;
      /* update the buffer tail */
      ts_handler->buf_tail = buf;
    } else {
      ts_handler->buffers[buf].size -= size;
      size = 0;
    }
  }
  if (size == -1 && ts_handler->buf_count > 0) {
    GST_LOG_OBJECT (ffmpegdec, "Stepping over %d bytes @ index %d has %d bytes",
        size, buf, ts_handler->buffers[buf].size);
    /* just consume the one buffer regardless */
    memset (ts_handler->buffers + buf, -1, sizeof (GstTSMap));
    /* Decrement the active buffer count */
    ts_handler->buf_count--;
    TS_MAP_INC (buf);
    /* update the buffer tail */
    ts_handler->buf_tail = buf;
    size = 0;
  }
}

/** get the timestamp from the tail of the list */
static guint64
gst_ts_handler_get_ts (GstFFMpegDec * ffmpegdec, gint64 * _offset)
{
  GstTSHandler *ts_handler = &ffmpegdec->ts_handler;
  guint64 ts = ts_handler->buffers[ts_handler->buf_tail].ts;
  gint64 offset = ts_handler->buffers[ts_handler->buf_tail].offset;
  GST_LOG_OBJECT (ffmpegdec, "Index %d yielded ts %" GST_TIME_FORMAT
      " offset %" G_GINT64_FORMAT, ts_handler->buf_tail,
      GST_TIME_ARGS (ts), offset);
  if (_offset)
    *_offset = offset;
  ts_handler->buffers[ts_handler->buf_tail].ts = -1;
  ts_handler->buffers[ts_handler->buf_tail].offset = -1;
  return ts;
}
