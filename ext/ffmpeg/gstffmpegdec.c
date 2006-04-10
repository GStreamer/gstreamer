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
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

//#define FORCE_OUR_GET_BUFFER

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
      gint fps_n, fps_d;
      gint old_fps_n, old_fps_d;

      enum PixelFormat pix_fmt;
    } video;
    struct
    {
      gint channels, samplerate;
    } audio;
  } format;
  gboolean waiting_for_key;
  guint64 next_ts, synctime;

  /* parsing */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;

  GstBuffer *last_buffer;

  GValue *par;                  /* pixel aspect ratio of incoming data */

  gint hurry_up, lowres;

  /* QoS stuff */ /* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;

  /* clipping segment */
  GstSegment segment;
};

typedef struct _GstFFMpegDecClass GstFFMpegDecClass;

struct _GstFFMpegDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

typedef struct _GstFFMpegDecClassParams GstFFMpegDecClassParams;

struct _GstFFMpegDecClassParams
{
  AVCodec *in_plugin;
  GstCaps *srccaps, *sinkcaps;
};

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegDec))
#define GST_FFMPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

enum
{
  ARG_0,
  ARG_LOWRES,
  ARG_SKIPFRAME
};

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
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

static gboolean gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegdec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegdec_release_buffer (AVCodecContext * context,
    AVFrame * picture);

static GstElementClass *parent_class = NULL;

#define GST_FFMPEGDEC_TYPE_LOWRES (gst_ffmpegdec_lowres_get_type())
static GType
gst_ffmpegdec_lowres_get_type (void)
{
  static GType ffmpegdec_lowres_type = 0;

  if (!ffmpegdec_lowres_type) {
    static GEnumValue ffmpegdec_lowres[] = {
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
    static GEnumValue ffmpegdec_skipframe[] = {
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
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDecClassParams *params;
  GstElementDetails details;
  GstPadTemplate *sinktempl, *srctempl;

  params = g_hash_table_lookup (global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins, GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s decoder",
      gst_ffmpeg_get_codecid_longname (params->in_plugin->id));
  details.klass = g_strdup_printf ("Codec/Decoder/%s",
      (params->in_plugin->type == CODEC_TYPE_VIDEO) ? "Video" : "Audio");
  details.description = g_strdup_printf ("FFMPEG %s decoder",
      params->in_plugin->name);
  details.author = "Wim Taymans <wim@fluendo.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, params->sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = params->in_plugin;
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
  gstelement_class->change_state = gst_ffmpegdec_change_state;

  g_object_class_install_property (gobject_class, ARG_SKIPFRAME,
      g_param_spec_enum ("skip-frame", "Skip frames",
          "Which types of frames to skip during decoding",
          GST_FFMPEGDEC_TYPE_SKIPFRAME, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_LOWRES,
      g_param_spec_enum ("lowres", "Low resolution",
          "At which resolution to decode images",
          GST_FFMPEGDEC_TYPE_LOWRES, 0, G_PARAM_READWRITE));
}

static void
gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;
  
  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (ffmpegdec->sinkpad, gst_ffmpegdec_setcaps);
  gst_pad_set_event_function (ffmpegdec->sinkpad, gst_ffmpegdec_sink_event);
  gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain);
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

  ffmpegdec->last_buffer = NULL;

  ffmpegdec->format.video.fps_n = -1;
  ffmpegdec->format.video.old_fps_n = -1;
  gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
}

static void
gst_ffmpegdec_finalize (GObject * object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  g_assert (!ffmpegdec->opened);

  /* clean up remaining allocated data */
  av_free (ffmpegdec->context);
  av_free (ffmpegdec->picture);

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

/* FIXME, make me ATOMIC */
static void
gst_ffmpegdec_update_qos (GstFFMpegDec *ffmpegdec, gdouble proportion, GstClockTime time)
{
  GST_OBJECT_LOCK (ffmpegdec);
  ffmpegdec->proportion = proportion;
  ffmpegdec->earliest_time = time;
  GST_OBJECT_UNLOCK (ffmpegdec);
}

static void
gst_ffmpegdec_reset_qos (GstFFMpegDec *ffmpegdec)
{
  gst_ffmpegdec_update_qos (ffmpegdec, 0.5, GST_CLOCK_TIME_NONE);
}

static void
gst_ffmpegdec_read_qos (GstFFMpegDec *ffmpegdec, gdouble *proportion, GstClockTime *time)
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

  GST_LOG_OBJECT (ffmpegdec, "Opened ffmpeg codec %s", oclass->in_plugin->name);

  /* open a parser if we can - exclude mpeg4, because it is already
   * framed (divx), mp3 because it doesn't work (?) and mjpeg because
   * of $(see mpeg4)... */
  if (oclass->in_plugin->id != CODEC_ID_MPEG4 &&
      oclass->in_plugin->id != CODEC_ID_MJPEG &&
      oclass->in_plugin->id != CODEC_ID_MP3 &&
      oclass->in_plugin->id != CODEC_ID_H264) {
    ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
  }

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      ffmpegdec->format.video.width = 0;
      ffmpegdec->format.video.height = 0;
      ffmpegdec->format.video.pix_fmt = PIX_FMT_NB;
      break;
    case CODEC_TYPE_AUDIO:
      ffmpegdec->format.audio.samplerate = 0;
      ffmpegdec->format.audio.channels = 0;
      break;
    default:
      break;
  }
  ffmpegdec->next_ts = 0;
  ffmpegdec->synctime = GST_CLOCK_TIME_NONE;
  ffmpegdec->last_buffer = NULL;
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

static GstPadLinkReturn
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

  /* close old session */
  gst_ffmpegdec_close (ffmpegdec);

  /* set defaults */
  avcodec_get_context_defaults (ffmpegdec->context);

  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;

  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, caps, ffmpegdec->context);

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

  /* do *not* draw edges */
  ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;

  /* for slow cpus */
  ffmpegdec->context->lowres = ffmpegdec->lowres;
  ffmpegdec->context->hurry_up = ffmpegdec->hurry_up;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegdec_open (ffmpegdec)) 
    goto open_failed;
  
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

static int
gst_ffmpegdec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstBuffer *buf = NULL;
  gulong bufsize = 0;
  GstFFMpegDec *ffmpegdec;
  int width;
  int height;
  
  ffmpegdec = (GstFFMpegDec *) context->opaque;

  width = context->width;
  height = context->height;

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      avcodec_align_dimensions (context, &width, &height);

      bufsize = avpicture_get_size (context->pix_fmt, width, height);

      if ((width != context->width) || (height != context->height) || 1) {
#ifdef FORCE_OUR_GET_BUFFER
        context->width = width;
        context->height = height;
#else
        /* revert to ffmpeg's default functions */
        ffmpegdec->context->get_buffer = avcodec_default_get_buffer;
        ffmpegdec->context->release_buffer = avcodec_default_release_buffer;

        return avcodec_default_get_buffer (context, picture);
#endif
      }

      if (!gst_ffmpegdec_negotiate (ffmpegdec)) {
        GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
            ("Failed to link ffmpeg decoder to next element"));
        return avcodec_default_get_buffer (context, picture);
      }

      if (gst_pad_alloc_buffer_and_set_caps (ffmpegdec->srcpad, GST_BUFFER_OFFSET_NONE,
              bufsize, GST_PAD_CAPS (ffmpegdec->srcpad), &buf) != GST_FLOW_OK)
        return -1;
      ffmpegdec->last_buffer = buf;

      gst_ffmpeg_avpicture_fill ((AVPicture *) picture,
          GST_BUFFER_DATA (buf),
          context->pix_fmt, context->width, context->height);
      break;
    case CODEC_TYPE_AUDIO:
    default:
      g_assert_not_reached ();
      break;
  }

  /* tell ffmpeg we own this buffer
   *
   * we also use an evil hack (keep buffer in base[0])
   * to keep a reference to the buffer in release_buffer(),
   * so that we can ref() it here and unref() it there
   * so that we don't need to copy data */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = G_MAXINT;
  gst_buffer_ref (buf);
  picture->opaque = buf;

  GST_LOG_OBJECT (ffmpegdec, "END");

  return 0;
}

static void
gst_ffmpegdec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstBuffer *buf;
  GstFFMpegDec *ffmpegdec;
  
  g_return_if_fail (picture->type == FF_BUFFER_TYPE_USER);

  buf = GST_BUFFER (picture->opaque);
  g_return_if_fail (buf != NULL);

  ffmpegdec = (GstFFMpegDec *) context->opaque;

  if (buf == ffmpegdec->last_buffer)
    ffmpegdec->last_buffer = NULL;
  gst_buffer_unref (buf);

  picture->opaque = NULL;

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
    GST_DEBUG_OBJECT (ffmpegdec, "Demuxer PAR: %d:%d", demuxer_num, demuxer_denom);
  }

  if (ffmpegdec->context->sample_aspect_ratio.num &&
      ffmpegdec->context->sample_aspect_ratio.den) {
    decoder_num = ffmpegdec->context->sample_aspect_ratio.num;
    decoder_denom = ffmpegdec->context->sample_aspect_ratio.den;
    decoder_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Decoder PAR: %d:%d", decoder_num, decoder_denom);
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
   * that is not 1:1. If both are non-1:1, use the pixel aspect
   * ratio provided by the codec */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom)
    goto use_decoder_par;

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom)
    goto use_demuxer_par;

  /* fall through and use decoder pixel aspect ratio */
use_decoder_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Setting decoder provided pixel-aspect-ratio of %u:%u",
        decoder_num, decoder_denom);
    gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        decoder_num, decoder_denom, NULL);
    return;
  }

use_demuxer_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Setting demuxer provided pixel-aspect-ratio of %u:%u",
        demuxer_num, demuxer_denom);
    gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        demuxer_num, demuxer_denom, NULL);
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Neither demuxer nor codec provide a pixel-aspect-ratio");
    return;
  }
}

static gboolean
gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass;
  GstCaps *caps;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      if (ffmpegdec->format.video.width == ffmpegdec->context->width &&
          ffmpegdec->format.video.height == ffmpegdec->context->height &&
          ffmpegdec->format.video.fps_n == ffmpegdec->format.video.old_fps_n &&
          ffmpegdec->format.video.fps_d == ffmpegdec->format.video.old_fps_d &&
          ffmpegdec->format.video.pix_fmt == ffmpegdec->context->pix_fmt)
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
      if (ffmpegdec->format.audio.samplerate ==
          ffmpegdec->context->sample_rate &&
          ffmpegdec->format.audio.channels == ffmpegdec->context->channels)
        return TRUE;
      GST_DEBUG_OBJECT (ffmpegdec, "Renegotiating audio from %dHz@%dchannels to %dHz@%dchannels",
          ffmpegdec->format.audio.samplerate, ffmpegdec->format.audio.channels,
          ffmpegdec->context->sample_rate, ffmpegdec->context->channels);
      ffmpegdec->format.audio.samplerate = ffmpegdec->context->sample_rate;
      ffmpegdec->format.audio.channels = ffmpegdec->context->channels;
      break;
    default:
      break;
  }

  caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
      ffmpegdec->context);

  if (caps == NULL)
    goto no_caps;
  
  /* If a demuxer provided a framerate then use it (#313970) */
  if (ffmpegdec->format.video.fps_n != -1) {
    gst_structure_set (gst_caps_get_structure (caps, 0), "framerate",
        GST_TYPE_FRACTION, ffmpegdec->format.video.fps_n,
        ffmpegdec->format.video.fps_d, NULL);
  }
  gst_ffmpegdec_add_pixel_aspect_ratio (ffmpegdec,
      gst_caps_get_structure (caps, 0));

  if (!gst_pad_set_caps (ffmpegdec->srcpad, caps))
    goto caps_failed;

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("could not find caps for codec (%s), unknown type",
            oclass->in_plugin->name));
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
gst_ffmpegdec_do_qos (GstFFMpegDec *ffmpegdec, GstClockTime timestamp, gboolean *mode_switch)
{
  GstClockTimeDiff diff;
  gdouble proportion;
  GstClockTime qostime, earliest_time;

  *mode_switch = FALSE;

  /* no timestamp, can't do QoS */
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    goto no_qos;

  /* get latest QoS observation values */
  gst_ffmpegdec_read_qos (ffmpegdec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) */
  if (!GST_CLOCK_TIME_IS_VALID (earliest_time)) {
    /* no hurry_up initialy */
    ffmpegdec->context->hurry_up = 0;
    goto no_qos;
  }

  /* qos is done on running time */
  qostime = gst_segment_to_running_time (&ffmpegdec->segment, GST_FORMAT_TIME,
			timestamp);

  /* see how our next timestamp relates to the latest qos timestamp. negative
   * values mean we are early, positive values mean we are too late. */
  diff = GST_CLOCK_DIFF (qostime, earliest_time);

  GST_DEBUG_OBJECT (ffmpegdec, "QOS: qostime %"GST_TIME_FORMAT
			", earliest %"GST_TIME_FORMAT, GST_TIME_ARGS (qostime),
			GST_TIME_ARGS (earliest_time));

  /* if we using less than 40% of the available time, we can try to
   * speed up again when we were slow. */
  if (proportion < 0.4 && diff < 0) {
    goto normal_mode;
  }
  else {
    /* if we're more than two seconds late, switch to the next keyframe */
    /* FIXME, let the demuxer decide what's the best since we might be dropping
     * a lot of frames when the keyframe is far away or we even might not get a new
     * keyframe at all.. */
    if (diff > ((GstClockTimeDiff) GST_SECOND * 2) && !ffmpegdec->waiting_for_key) {
      goto skip_to_keyframe;
    }
    else if (diff >= 0) {
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
    GST_DEBUG_OBJECT (ffmpegdec, "QOS: keyframe, %"G_GINT64_FORMAT" > GST_SECOND/2", diff);
    /* we can skip the current frame */
    return FALSE;
  }
hurry_up:
  {
    if (ffmpegdec->context->hurry_up != 1) {
      ffmpegdec->context->hurry_up = 1;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec, "QOS: hurry up, diff %"G_GINT64_FORMAT" >= 0", diff);
    }
    return TRUE;
  }
}

static gint
gst_ffmpegdec_frame (GstFFMpegDec * ffmpegdec,
    guint8 * data, guint size, gint * got_data, guint64 * in_ts,
    GstBuffer * inbuf, GstFlowReturn * ret)
{
  GstFFMpegDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (ffmpegdec->context->codec == NULL)
    return -1;

  GST_LOG_OBJECT (ffmpegdec,
      "data:%p, size:%d, *in_ts:%" GST_TIME_FORMAT " inbuf:%p  inbuf.ts:%"
      GST_TIME_FORMAT, data, size, GST_TIME_ARGS (*in_ts), inbuf,
      GST_TIME_ARGS ((inbuf) ? GST_BUFFER_TIMESTAMP (inbuf) : 0));

  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
    {
      gboolean	iskeyframe = FALSE;
      gboolean	is_itype = FALSE;
      gboolean	is_reference = FALSE;
      gboolean  mode_switch = FALSE;
      
      ffmpegdec->context->opaque = ffmpegdec;

      /* run QoS code, returns FALSE if we can skip decoding this
       * frame entirely. */
      if (!gst_ffmpegdec_do_qos (ffmpegdec, *in_ts, &mode_switch)) {
	have_data = 0;
	break;
      }

      ffmpegdec->picture->pict_type = -1;       /* in case we skip frames */

      len = avcodec_decode_video (ffmpegdec->context,
          ffmpegdec->picture, &have_data, data, size);

      /* when we are in hurry_up mode, don't complain when ffmpeg returned
       * no data because we told it to skip stuff. */
      if (len < 0 && (mode_switch || ffmpegdec->context->hurry_up))
	len = 0;

      is_itype = (ffmpegdec->picture->pict_type == FF_I_TYPE);
      is_reference = (ffmpegdec->picture->reference == 1);
      iskeyframe = (is_itype || is_reference || ffmpegdec->picture->key_frame)
	|| (oclass->in_plugin->id == CODEC_ID_INDEO3)
	|| (oclass->in_plugin->id == CODEC_ID_MSZH)
	|| (oclass->in_plugin->id == CODEC_ID_ZLIB)
	|| (oclass->in_plugin->id == CODEC_ID_VP3)
	|| (oclass->in_plugin->id == CODEC_ID_HUFFYUV);
      GST_LOG_OBJECT (ffmpegdec,
          "Decoded video: len=%d, have_data=%d, "
          "is_keyframe:%d, is_itype:%d, is_reference:%d",
          len, have_data, iskeyframe, is_itype, is_reference);

      if (ffmpegdec->waiting_for_key) {
        if (iskeyframe) {
          ffmpegdec->waiting_for_key = FALSE;
        } else {
          GST_WARNING_OBJECT (ffmpegdec, "Dropping non-keyframe (seek/init)");
          have_data = 0;
          break;
        }
      }

      /* note that ffmpeg sometimes gets the FPS wrong.
       * For B-frame containing movies, we get all pictures delayed
       * except for the I frames, so we synchronize only on I frames
       * and keep an internal counter based on FPS for the others. */
      if (!(oclass->in_plugin->capabilities & CODEC_CAP_DELAY) ||
	  ((iskeyframe || !GST_CLOCK_TIME_IS_VALID (ffmpegdec->next_ts)) &&
	   GST_CLOCK_TIME_IS_VALID (*in_ts))) {
	GST_LOG_OBJECT (ffmpegdec, "setting next_ts to %" GST_TIME_FORMAT,
			  GST_TIME_ARGS (*in_ts));
	ffmpegdec->next_ts = *in_ts;
	*in_ts = GST_CLOCK_TIME_NONE;
      }

      /* precise seeking.... */
      if (GST_CLOCK_TIME_IS_VALID (ffmpegdec->synctime)) {
        if (ffmpegdec->next_ts >= ffmpegdec->synctime) {
          ffmpegdec->synctime = GST_CLOCK_TIME_NONE;
        } else {
          GST_WARNING_OBJECT (ffmpegdec,
              "Dropping frame for synctime %" GST_TIME_FORMAT
              ", expected(next_ts) %" GST_TIME_FORMAT,
              GST_TIME_ARGS (ffmpegdec->synctime),
              GST_TIME_ARGS (ffmpegdec->next_ts));

          if (ffmpegdec->picture->opaque != NULL) {
            outbuf = (GstBuffer *) ffmpegdec->picture->opaque;
            gst_buffer_unref (outbuf);
          }

          have_data = 0;
          /* don´t break here! Timestamps are updated below */
        }
      }

      if (ffmpegdec->waiting_for_key && !iskeyframe) {
        have_data = 0;
      } else if (len >= 0 && have_data > 0) {
        /* libavcodec constantly crashes on stupid buffer allocation
         * errors inside. This drives me crazy, so we let it allocate
         * its own buffers and copy to our own buffer afterwards... */

        if (ffmpegdec->picture->opaque != NULL) {
          outbuf = (GstBuffer *) ffmpegdec->picture->opaque;
          if (outbuf == ffmpegdec->last_buffer)
            ffmpegdec->last_buffer = NULL;
        } else {
          AVPicture pic;
          gint fsize =
              gst_ffmpeg_avpicture_get_size (ffmpegdec->context->pix_fmt,
              ffmpegdec->context->width, ffmpegdec->context->height);

          if (!gst_ffmpegdec_negotiate (ffmpegdec))
            return -1;

          if (!ffmpegdec->context->palctrl) {
            if ((*ret =
                    gst_pad_alloc_buffer_and_set_caps (ffmpegdec->srcpad,
                        GST_BUFFER_OFFSET_NONE, fsize,
                        GST_PAD_CAPS (ffmpegdec->srcpad),
                        &outbuf)) != GST_FLOW_OK)
              return -1;
          } else {
            /* for paletted data we can't use pad_alloc_buffer(), because
             * fsize contains the size of the palette, so the overall size
             * is bigger than ffmpegcolorspace's unit size, which will
             * prompt GstBaseTransform to complain endlessly ... */
            outbuf = gst_buffer_new_and_alloc (fsize);
            gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegdec->srcpad));
          }

          /* original ffmpeg code does not handle odd sizes correctly.
           * This patched up version does */
          gst_ffmpeg_avpicture_fill (&pic, GST_BUFFER_DATA (outbuf),
              ffmpegdec->context->pix_fmt,
              ffmpegdec->context->width, ffmpegdec->context->height);

          /* the original convert function did not do the right thing, this
           * is a patched up version that adjust widht/height so that the
           * ffmpeg one works correctly. */
          gst_ffmpeg_img_convert (&pic, ffmpegdec->context->pix_fmt,
              (AVPicture *) ffmpegdec->picture,
              ffmpegdec->context->pix_fmt,
              ffmpegdec->context->width, ffmpegdec->context->height);
        }

        ffmpegdec->waiting_for_key = FALSE;

        if (!iskeyframe) {
          GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
        }

        /* If we have used the framerate from the demuxer then
         * also use the demuxer's timestamp information (#317596) */
        if (ffmpegdec->format.video.fps_n != -1 && inbuf != NULL) {
          GST_LOG_OBJECT (ffmpegdec, "using incoming buffer's timestamps");
          GST_LOG_OBJECT (ffmpegdec, "incoming timestamp %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));
          gst_buffer_stamp (outbuf, inbuf);
        } else {
          GST_LOG_OBJECT (ffmpegdec, "using decoder's timestamps");
          GST_BUFFER_TIMESTAMP (outbuf) = ffmpegdec->next_ts;
          if (ffmpegdec->context->time_base.num != 0 &&
              ffmpegdec->context->time_base.den != 0) {
            GST_BUFFER_DURATION (outbuf) =
                gst_util_uint64_scale_int (GST_SECOND,
                ffmpegdec->context->time_base.num,
                ffmpegdec->context->time_base.den);

            /* Take repeat_pict into account */
            GST_BUFFER_DURATION (outbuf) += GST_BUFFER_DURATION (outbuf)
                * ffmpegdec->picture->repeat_pict / 2;
            GST_DEBUG_OBJECT (ffmpegdec,
                "advancing next_ts by duration of %" GST_TIME_FORMAT,
                GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
            ffmpegdec->next_ts += GST_BUFFER_DURATION (outbuf);
          } else {
            GST_DEBUG_OBJECT (ffmpegdec, "setting next_ts to NONE");
            ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
          }
        }
        GST_LOG_OBJECT (ffmpegdec, "outgoing timestamp %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
      } else if (ffmpegdec->picture->pict_type != -1 &&
          oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
        /* update time for skip-frame */
        if ((have_data == 0) ||
	    ((iskeyframe || !GST_CLOCK_TIME_IS_VALID (ffmpegdec->next_ts))
            && GST_CLOCK_TIME_IS_VALID (*in_ts))) {
          GST_DEBUG_OBJECT (ffmpegdec, "setting next_ts to *in_ts");
          ffmpegdec->next_ts = *in_ts;
          *in_ts = GST_CLOCK_TIME_NONE;
        }

        if (ffmpegdec->context->time_base.num != 0 &&
            ffmpegdec->context->time_base.den != 0) {
          guint64 dur = GST_SECOND *
              ffmpegdec->context->time_base.num /
              ffmpegdec->context->time_base.den;

          /* Take repeat_pict into account */
          dur += dur * ffmpegdec->picture->repeat_pict / 2;
          GST_DEBUG_OBJECT (ffmpegdec,
              "Advancing next_ts by dur:%" GST_TIME_FORMAT,
              GST_TIME_ARGS (dur));
          ffmpegdec->next_ts += dur;
        } else {
          GST_DEBUG_OBJECT (ffmpegdec, "setting next_ts to NONE");
          ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
        }
      }
      /* palette is not part of raw video frame in gst and the size
       * of the outgoing buffer needs to be adjusted accordingly */
      if (ffmpegdec->context->palctrl != NULL && outbuf != NULL)
        GST_BUFFER_SIZE (outbuf) -= AVPALETTE_SIZE;
      break;
    }
    case CODEC_TYPE_AUDIO:
      if (!ffmpegdec->last_buffer)
        outbuf = gst_buffer_new_and_alloc (AVCODEC_MAX_AUDIO_FRAME_SIZE);
      else {
        outbuf = ffmpegdec->last_buffer;
        ffmpegdec->last_buffer = NULL;
      }
      len = avcodec_decode_audio (ffmpegdec->context,
          (int16_t *) GST_BUFFER_DATA (outbuf), &have_data, data, size);
      GST_DEBUG_OBJECT (ffmpegdec,
          "Decode audio: len=%d, have_data=%d", len, have_data);

      if (len >= 0 && have_data > 0) {
        if (!gst_ffmpegdec_negotiate (ffmpegdec)) {
          gst_buffer_unref (outbuf);
          return -1;
        }

        GST_BUFFER_SIZE (outbuf) = have_data;
        if (GST_CLOCK_TIME_IS_VALID (*in_ts)) {
          ffmpegdec->next_ts = *in_ts;
        }
        GST_BUFFER_TIMESTAMP (outbuf) = ffmpegdec->next_ts;
        GST_BUFFER_DURATION (outbuf) = (have_data * GST_SECOND) /
            (2 * ffmpegdec->context->channels *
            ffmpegdec->context->sample_rate);
        ffmpegdec->next_ts += GST_BUFFER_DURATION (outbuf);
        if (GST_CLOCK_TIME_IS_VALID (*in_ts))
          *in_ts += GST_BUFFER_DURATION (outbuf);
      } else if (len > 0 && have_data == 0) {
        /* cache output, because it may be used for caching (in-place) */
        ffmpegdec->last_buffer = outbuf;
      } else {
        gst_buffer_unref (outbuf);
      }
      break;
    default:
      g_assert (0);
      break;
  }

  if (len < 0 || have_data < 0) {
    GST_ERROR_OBJECT (ffmpegdec,
        "ffdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, have_data);
    *got_data = 0;
    return len;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    return 0;
  } else {
    /* this is where I lost my last clue on ffmpeg... */
    *got_data = 1;              //(ffmpegdec->pctx || have_data) ? 1 : 0;
  }

  if (have_data) {
    GST_LOG_OBJECT (ffmpegdec, "Decoded data, now pushing with timestamp %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegdec->srcpad));
    *ret = gst_pad_push (ffmpegdec->srcpad, outbuf);
  }

  return len;
}

static void
gst_ffmpegdec_flush_pcache (GstFFMpegDec *ffmpegdec)
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
    case GST_EVENT_EOS:{
      if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
        gint have_data, len, try = 0;

        do {
          GstFlowReturn ret;

          len = gst_ffmpegdec_frame (ffmpegdec, NULL, 0, &have_data,
              &ffmpegdec->next_ts, NULL, &ret);
          if (len < 0 || have_data == 0)
            break;
        } while (try++ < 10);
      }
      ret = gst_pad_push_event (ffmpegdec->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);
      }
      gst_ffmpegdec_reset_qos (ffmpegdec);
      gst_ffmpegdec_flush_pcache (ffmpegdec);
      ffmpegdec->waiting_for_key = TRUE;
      gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
      ret = gst_pad_push_event (ffmpegdec->srcpad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      gboolean update;
      GstFormat fmt;
      gint64 start, stop, time;
      gdouble rate;

      gst_event_parse_new_segment (event, &update, &rate, &fmt, &start, &stop,
          &time);

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      switch (fmt) {
	case GST_FORMAT_TIME:
          /* fine, our native segment format */
          break;
	case GST_FORMAT_BYTES:
	{
          /* convert to time or fail */
          if (!ffmpegdec->context->bit_rate)
	    goto no_bitrate;

	  /* convert values to TIME */
	  if (start != -1)
	    start = gst_util_uint64_scale_int (start, GST_SECOND,
			  ffmpegdec->context->bit_rate);
	  if (stop != -1)
	    stop = gst_util_uint64_scale_int (stop, GST_SECOND,
			  ffmpegdec->context->bit_rate);
	  if (time != -1)
	    time = gst_util_uint64_scale_int (time, GST_SECOND,
			  ffmpegdec->context->bit_rate);

	  /* unref old event */
          gst_event_unref (event);

	  /* create new converted time segment */
          event = gst_event_new_new_segment (update, rate, fmt,
			  start, stop, time);
          break;
	}
	default:
	  /* invalid format */
	  goto invalid_format;
      }

      GST_DEBUG_OBJECT (ffmpegdec,
            "NEWSEGMENT in time (next_ts) %" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      /* and store the values */
      gst_segment_set_newsegment (&ffmpegdec->segment, update,
               rate, fmt, start, stop, time);

      /* FIXME, newsegment does not define the next timestamp */
      ffmpegdec->next_ts = start;
      ffmpegdec->synctime = start;

      /* FIXME, newsegment does not mean a DISCONT */
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);
      }
      ffmpegdec->waiting_for_key = TRUE;

      /* and push segment downstream */
      ret = gst_pad_push_event (ffmpegdec->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (ffmpegdec->srcpad, event);
      break;
  }

done:
  gst_object_unref (ffmpegdec);

  return ret;

  /* ERRORS */
newseg_wrong_rate:
  {
    GST_WARNING_OBJECT (ffmpegdec, "negative rates not supported yet");
    gst_event_unref (event);
    goto done;
  }
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
  guint8 *bdata, *data;
  gint bsize, size, len, have_data;
  guint64 in_ts;
  GstFlowReturn ret = GST_FLOW_OK;

  ffmpegdec = (GstFFMpegDec *) (GST_PAD_PARENT (pad));

  oclass = (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (G_UNLIKELY (!ffmpegdec->opened))
    goto not_negotiated;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (ffmpegdec, "received DISCONT");
    gst_ffmpegdec_flush_pcache (ffmpegdec);
    ffmpegdec->waiting_for_key = TRUE;
  }

  in_ts = GST_BUFFER_TIMESTAMP (inbuf);

  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DELTA_UNIT))
      goto skip_keyframe;

    GST_DEBUG_OBJECT (ffmpegdec, "got keyframe %"GST_TIME_FORMAT, GST_TIME_ARGS (in_ts));
    ffmpegdec->waiting_for_key = FALSE;
  }

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %d, time %" GST_TIME_FORMAT " next_ts %"
      GST_TIME_FORMAT, GST_BUFFER_SIZE (inbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
      GST_TIME_ARGS (ffmpegdec->next_ts));

  /* parse cache joining */
  if (ffmpegdec->pcache) {
    GstClockTime timestamp = GST_CLOCK_TIME_NONE;
    GstClockTime duration = GST_CLOCK_TIME_NONE;

    /* decide on resulting timestamp/duration before we give away our ref */
    /* since the cache is all data that did not result in an outgoing frame,
     * we should timestamp with the new incoming buffer.  This is probably
     * not entirely correct though, but better than nothing. */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (inbuf))
      timestamp = GST_BUFFER_TIMESTAMP (inbuf);

    if (GST_BUFFER_DURATION_IS_VALID (ffmpegdec->pcache)
        && GST_BUFFER_DURATION_IS_VALID (inbuf))
      duration = GST_BUFFER_DURATION (ffmpegdec->pcache) +
          GST_BUFFER_DURATION (inbuf);

    inbuf = gst_buffer_join (ffmpegdec->pcache, inbuf);

    /* update time info as appropriate */
    GST_BUFFER_TIMESTAMP (inbuf) = timestamp;
    GST_BUFFER_DURATION (inbuf) = duration;
    GST_LOG_OBJECT (ffmpegdec, "joined parse cache, inbuf now has ts %" GST_TIME_FORMAT
        " and duration %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (duration));
    ffmpegdec->pcache = NULL;
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  }
  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  else if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == CODEC_ID_SVQ3) {
    inbuf = gst_buffer_make_writable (inbuf);
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  } else {
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  }

  do {
    /* parse, if at all possible */
    if (ffmpegdec->pctx) {
      gint res;
      gint64 ffpts;

      ffpts = gst_ffmpeg_time_gst_to_ff (in_ts, ffmpegdec->context->time_base);
      res = av_parser_parse (ffmpegdec->pctx, ffmpegdec->context,
          &data, &size, bdata, bsize, ffpts, ffpts);

      GST_LOG_OBJECT (ffmpegdec, "Parsed video frame, res=%d, size=%d",
          res, size);

      in_ts = gst_ffmpeg_time_ff_to_gst (ffmpegdec->pctx->pts,
          ffmpegdec->context->time_base);

      if (res == 0 || size == 0)
        break;
      else {
        bsize -= res;
        bdata += res;
      }
    } else {
      data = bdata;
      size = bsize;
    }

    if ((len = gst_ffmpegdec_frame (ffmpegdec, data, size,
                &have_data, &in_ts, inbuf, &ret)) < 0 || ret != GST_FLOW_OK)
      break;

    if (!ffmpegdec->pctx) {
      bsize -= len;
      bdata += len;
    }

    if (!have_data) {
      break;
    }
  } while (bsize > 0);

  if ((ffmpegdec->pctx || oclass->in_plugin->id == CODEC_ID_MP3) && bsize > 0) {
    GST_LOG_OBJECT (ffmpegdec, "Keeping %d bytes of data", bsize);

    ffmpegdec->pcache = gst_buffer_create_sub (inbuf,
        GST_BUFFER_SIZE (inbuf) - bsize, bsize);
    /* we keep timestamp, even though all we really know is that the correct
     * timestamp is not below the one from inbuf */
    GST_BUFFER_TIMESTAMP (ffmpegdec->pcache) = GST_BUFFER_TIMESTAMP (inbuf);
  } else if (bsize > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);
  }
  gst_buffer_unref (inbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
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
      if (ffmpegdec->last_buffer != NULL) {
        gst_buffer_unref (ffmpegdec->last_buffer);
        ffmpegdec->last_buffer = NULL;
      }
      GST_OBJECT_UNLOCK (ffmpegdec);
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
    case ARG_LOWRES:
      ffmpegdec->lowres = ffmpegdec->context->lowres = g_value_get_enum (value);
      break;
    case ARG_SKIPFRAME:
      ffmpegdec->hurry_up = ffmpegdec->context->hurry_up =
          g_value_get_enum (value);
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
    case ARG_LOWRES:
      g_value_set_enum (value, ffmpegdec->context->lowres);
      break;
    case ARG_SKIPFRAME:
      g_value_set_enum (value, ffmpegdec->context->hurry_up);
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

  in_plugin = first_avcodec;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    GstFFMpegDecClassParams *params;
    GstCaps *srccaps, *sinkcaps;
    gchar *type_name;

    /* no quasi-codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_S24DAUD)) {
      goto next;
    }

    /* only decoders */
    if (!in_plugin->decode) {
      goto next;
    }

    /* name */
    if (!gst_ffmpeg_get_codecid_longname (in_plugin->id)) {
      g_warning ("Add decoder %s (%d) please", in_plugin->name, in_plugin->id);
      goto next;
    }

    /* first make sure we've got a supported type */
    sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
    } else {
      srccaps = gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL);
    }
    if (!sinkcaps || !srccaps) {
      if (sinkcaps)
        gst_caps_unref (sinkcaps);
      if (srccaps)
        gst_caps_unref (srccaps);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("ffdec_%s", in_plugin->name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      goto next;
    }

    params = g_new0 (GstFFMpegDecClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->sinkcaps = sinkcaps;
    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the gtype now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * VC1/WMV3 are not working and thus unpreferred for now. */
    switch (in_plugin->id) {
      case CODEC_ID_MPEG4:
      case CODEC_ID_MSMPEG4V3:
      case CODEC_ID_H264:
        rank = GST_RANK_PRIMARY;
        break;
      default:
        rank = GST_RANK_MARGINAL;
        break;
      case CODEC_ID_WMV3:
      case CODEC_ID_VC9:
        /* what's that? */
      case CODEC_ID_SP5X:
        /* MP3 and MPEG2 have better alternatives and
           the ffmpeg versions don't work properly feel
           free to assign rank if you fix them */
      case CODEC_ID_MP3:
      case CODEC_ID_MPEG2VIDEO:
        rank = GST_RANK_NONE;
        break;
    }
    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_warning ("Failed to register %s", type_name);
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (type), (gpointer) params);

  next:
    in_plugin = in_plugin->next;
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
