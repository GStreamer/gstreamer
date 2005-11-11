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
  union {
    struct {
      gint width, height;
      gdouble fps, old_fps;
      enum PixelFormat pix_fmt;
    } video;
    struct {
      gint channels, samplerate;
    } audio;
  } format;
  gboolean waiting_for_key;
  guint64 next_ts, synctime;

  /* parsing */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;

  GstBuffer *last_buffer;

  GValue *par;		/* pixel aspect ratio of incoming data */

  gint hurry_up, lowres;
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
static void gst_ffmpegdec_dispose (GObject * object);

static gboolean gst_ffmpegdec_query (GstPad * pad, GstQuery *query);
static gboolean gst_ffmpegdec_event (GstPad * pad, GstEvent * event);

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
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
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

  gobject_class->dispose = gst_ffmpegdec_dispose;
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
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (ffmpegdec->sinkpad, gst_ffmpegdec_setcaps);
  gst_pad_set_event_function (ffmpegdec->sinkpad, gst_ffmpegdec_sink_event);
  gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain);
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegdec->srcpad);
  gst_pad_set_event_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_event));
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
  ffmpegdec->waiting_for_key = FALSE;
  ffmpegdec->hurry_up = ffmpegdec->lowres = 0;

  ffmpegdec->last_buffer = NULL;

  ffmpegdec->format.video.fps = -1.0;
  ffmpegdec->format.video.old_fps = -1.0;
}

static void
gst_ffmpegdec_dispose (GObject * object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  G_OBJECT_CLASS (parent_class)->dispose (object);
  /* old session should have been closed in element_class->dispose */
  g_assert (!ffmpegdec->opened);

  /* clean up remaining allocated data */
  av_free (ffmpegdec->context);
  av_free (ffmpegdec->picture);
}

static gboolean
gst_ffmpegdec_query (GstPad * pad, GstQuery *query)
{
  GstFFMpegDec *ffmpegdec;
  GstPad *peer;
  GstFormat bfmt;

  bfmt = GST_FORMAT_BYTES;
  ffmpegdec = (GstFFMpegDec *) GST_PAD_PARENT (pad);
  peer = GST_PAD_PEER (ffmpegdec->sinkpad);

  if (!peer)
    goto no_peer;

  /* just forward to peer */
  if (gst_pad_query (peer, query))
    return TRUE;

#if 0
  /* ok, do bitrate calc... */
  if ((type != GST_QUERY_POSITION && type != GST_QUERY_TOTAL) ||
           *fmt != GST_FORMAT_TIME || ffmpegdec->context->bit_rate == 0 ||
           !gst_pad_query (peer, type, &bfmt, value))
    return FALSE;

  if (ffmpegdec->pcache && type == GST_QUERY_POSITION)
    *value -= GST_BUFFER_SIZE (ffmpegdec->pcache);
  *value *= GST_SECOND / ffmpegdec->context->bit_rate;
#endif

  return FALSE;

no_peer:
  {
    return FALSE;
  }
}

static gboolean
gst_ffmpegdec_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec;
  GstPad *peer;
  
  ffmpegdec = (GstFFMpegDec *) GST_PAD_PARENT (pad);
  peer = GST_PAD_PEER (ffmpegdec->sinkpad);

  if (!peer)
    return FALSE;
  
  gst_event_ref (event);
  if (gst_pad_send_event (peer, event)) {
    gst_event_unref (event);
    return TRUE;
  }

  gst_event_unref (event);

  return FALSE; /* .. */
}

static void
gst_ffmpegdec_close (GstFFMpegDec *ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  if (ffmpegdec->par) {
    g_free (ffmpegdec->par);
    ffmpegdec->par = NULL;
  }

  if (ffmpegdec->context->priv_data)
    avcodec_close (ffmpegdec->context);
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

  ffmpegdec->format.video.fps = -1.0;
  ffmpegdec->format.video.old_fps = -1.0;
}

static gboolean
gst_ffmpegdec_open (GstFFMpegDec *ffmpegdec)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  ffmpegdec->opened = TRUE;

  GST_LOG ("Opened ffmpeg codec %s", oclass->in_plugin->name);

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

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegdec_close (ffmpegdec);
    GST_DEBUG ("ffdec_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static GstPadLinkReturn
gst_ffmpegdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) (GST_OBJECT_PARENT (pad));
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstStructure *structure;
  const GValue *par;

  GST_DEBUG ("setcaps called");

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

  if (!ffmpegdec->context->time_base.den ||
      !ffmpegdec->context->time_base.num) {
    GST_DEBUG ("forcing 25/1 framerate");
    ffmpegdec->context->time_base.num = 1;
    ffmpegdec->context->time_base.den = 25;
  }

  /* get pixel aspect ratio if it's set */
  structure = gst_caps_get_structure (caps, 0);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    GST_DEBUG_OBJECT (ffmpegdec, "sink caps have pixel-aspect-ratio");
    ffmpegdec->par = g_new0 (GValue, 1);
    gst_value_init_and_copy (ffmpegdec->par, par);
  }

  if (gst_structure_has_field (structure, "framerate")) {
    ffmpegdec->format.video.old_fps = ffmpegdec->format.video.fps;
    gst_structure_get_double (structure, "framerate",
        &ffmpegdec->format.video.fps);
  } else {
    ffmpegdec->format.video.old_fps = ffmpegdec->format.video.fps;
    ffmpegdec->format.video.fps = -1.0;
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
  if (!gst_ffmpegdec_open (ffmpegdec)) {
    if (ffmpegdec->par) {
      g_free (ffmpegdec->par);
      ffmpegdec->par = NULL;
    }
    return FALSE;
  }

  return TRUE;
}

static int
gst_ffmpegdec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstBuffer *buf = NULL;
  gulong bufsize = 0;
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) context->opaque; 
  int width  = context->width;
  int height = context->height;

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      
      avcodec_align_dimensions(context, &width, &height);
      
      bufsize = avpicture_get_size (context->pix_fmt,
				    width, height);
      
      if((width != context->width) || (height != context->height)) {
#ifdef FORCE_OUR_GET_BUFFER
	context->width = width;
	context->height = height;
#else	
	/* revert to ffmpeg's default functions */
	ffmpegdec->context->get_buffer = avcodec_default_get_buffer;
	ffmpegdec->context->release_buffer = avcodec_default_release_buffer;       	

	return avcodec_default_get_buffer(context, picture);
#endif
	
      } 
      
      if (!gst_ffmpegdec_negotiate (ffmpegdec)) {
	GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
			   ("Failed to link ffmpeg decoder to next element"));
	return avcodec_default_get_buffer(context, picture);
      }
      
      if (gst_pad_alloc_buffer (ffmpegdec->srcpad, GST_BUFFER_OFFSET_NONE, 
				bufsize, GST_PAD_CAPS (ffmpegdec->srcpad), &buf) != GST_FLOW_OK)
        return -1;
      ffmpegdec->last_buffer = buf;
      
      gst_ffmpeg_avpicture_fill ((AVPicture *) picture,
          GST_BUFFER_DATA (buf),
          context->pix_fmt, context->width, context->height);
      break;
      
    case CODEC_TYPE_AUDIO:
    default:
      g_assert (0);
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
  picture->opaque = buf;
  gst_buffer_ref (buf);

  GST_LOG_OBJECT (ffmpegdec, "END");

  return 0;
}

static void
gst_ffmpegdec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstBuffer *buf = GST_BUFFER (picture->opaque);
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) context->opaque; 
  

  g_return_if_fail (buf != NULL);
  g_return_if_fail (picture->type == FF_BUFFER_TYPE_USER);

  if (buf == ffmpegdec->last_buffer)
    ffmpegdec->last_buffer = NULL;
  gst_buffer_unref (buf);

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}

static gboolean
gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstCaps *caps;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      if (ffmpegdec->format.video.width == ffmpegdec->context->width &&
          ffmpegdec->format.video.height == ffmpegdec->context->height &&
          ffmpegdec->format.video.fps == ffmpegdec->format.video.old_fps &&
	  ffmpegdec->format.video.pix_fmt == ffmpegdec->context->pix_fmt)
        return TRUE;
      GST_DEBUG ("Renegotiating video from %dx%d@%0.2ffps to %dx%d@%0.2ffps",
          ffmpegdec->format.video.width, ffmpegdec->format.video.height,
          ffmpegdec->format.video.old_fps, ffmpegdec->context->width,
          ffmpegdec->context->height, ffmpegdec->format.video.old_fps);
      ffmpegdec->format.video.width = ffmpegdec->context->width;
      ffmpegdec->format.video.height = ffmpegdec->context->height;
      ffmpegdec->format.video.old_fps = ffmpegdec->format.video.fps;
      ffmpegdec->format.video.pix_fmt = ffmpegdec->context->pix_fmt;
      break;
    case CODEC_TYPE_AUDIO:
      if (ffmpegdec->format.audio.samplerate ==
              ffmpegdec->context->sample_rate &&
          ffmpegdec->format.audio.channels == ffmpegdec->context->channels)
        return TRUE;
      GST_DEBUG ("Renegotiating audio from %dHz@%dchannels to %dHz@%dchannels",
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

 if (caps) {
   /* If a demuxer provided a framerate then use it (#313970) */
   if (ffmpegdec->format.video.fps != -1.0) {
     gst_structure_set (gst_caps_get_structure (caps, 0), "framerate",
         G_TYPE_DOUBLE, ffmpegdec->format.video.fps, NULL);
   }

  /* Add pixel-aspect-ratio if we have it. Prefer
   * ffmpeg PAR over sink PAR (since it's provided
   * by the codec, which is more often correct).
   */
   if (ffmpegdec->context->sample_aspect_ratio.num &&
       ffmpegdec->context->sample_aspect_ratio.den) {
     GST_DEBUG ("setting ffmpeg provided pixel-aspect-ratio");
     gst_structure_set (gst_caps_get_structure (caps, 0),
         "pixel-aspect-ratio", GST_TYPE_FRACTION,
         ffmpegdec->context->sample_aspect_ratio.num,
         ffmpegdec->context->sample_aspect_ratio.den,
         NULL);
    } else if (ffmpegdec->par) {
      GST_DEBUG ("passing on pixel-aspect-ratio from sink");
      gst_structure_set (gst_caps_get_structure (caps, 0),
          "pixel-aspect-ratio", GST_TYPE_FRACTION,
           gst_value_get_fraction_numerator (ffmpegdec->par),
           gst_value_get_fraction_denominator (ffmpegdec->par),
           NULL);
    }
  }

  if (caps == NULL ||
      !gst_pad_set_caps (ffmpegdec->srcpad, caps)) {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Failed to link ffmpeg decoder (%s) to next element",
        oclass->in_plugin->name));

    if (caps != NULL)
      gst_caps_unref (caps);

    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}

static gint
gst_ffmpegdec_frame (GstFFMpegDec * ffmpegdec,
    guint8 * data, guint size, gint * got_data, guint64 * in_ts,
    GstBuffer * inbuf, GstFlowReturn * ret)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (ffmpegdec->context->codec == NULL)
    return -1;

  ffmpegdec->context->frame_number++;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      ffmpegdec->picture->pict_type = -1; /* in case we skip frames */

      ffmpegdec->context->opaque = ffmpegdec;
      
      len = avcodec_decode_video (ffmpegdec->context,
          ffmpegdec->picture, &have_data, data, size);
      GST_DEBUG_OBJECT (ffmpegdec,
          "Decoded video: len=%d, have_data=%d", len, have_data);

      if (ffmpegdec->waiting_for_key) {
        if (ffmpegdec->picture->pict_type == FF_I_TYPE) {
          ffmpegdec->waiting_for_key = FALSE;
        } else {
          GST_WARNING_OBJECT (ffmpegdec,
              "Dropping non-keyframe (seek/init)");
          have_data = 0;
          break;
        }
      }

      /* note that ffmpeg sometimes gets the FPS wrong.
       * For B-frame containing movies, we get all pictures delayed
       * except for the I frames, so we synchronize only on I frames
       * and keep an internal counter based on FPS for the others. */
      if (!(oclass->in_plugin->capabilities & CODEC_CAP_DELAY) ||
          ((ffmpegdec->picture->pict_type == FF_I_TYPE ||
            !GST_CLOCK_TIME_IS_VALID (ffmpegdec->next_ts)) &&
        GST_CLOCK_TIME_IS_VALID (*in_ts))) {
        ffmpegdec->next_ts = *in_ts;
        *in_ts = GST_CLOCK_TIME_NONE;
      }

      /* precise seeking.... */
      if (GST_CLOCK_TIME_IS_VALID (ffmpegdec->synctime)) {
        if (ffmpegdec->next_ts >= ffmpegdec->synctime) {
          ffmpegdec->synctime = GST_CLOCK_TIME_NONE;
        } else {
          GST_WARNING_OBJECT (ffmpegdec,
              "Dropping frame for synctime %" GST_TIME_FORMAT ", expected(next_ts) %"
              GST_TIME_FORMAT, GST_TIME_ARGS (ffmpegdec->synctime),
              GST_TIME_ARGS (ffmpegdec->next_ts));
	  if (ffmpegdec->last_buffer)
	    gst_buffer_unref(ffmpegdec->last_buffer);
          have_data = 0;
          /* don´t break here! Timestamps are updated below */
        }
      }

      if (ffmpegdec->waiting_for_key &&
          ffmpegdec->picture->pict_type != FF_I_TYPE) {
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
	  gint fsize = gst_ffmpeg_avpicture_get_size (ffmpegdec->context->pix_fmt,
            ffmpegdec->context->width, ffmpegdec->context->height);

	  if (!gst_ffmpegdec_negotiate (ffmpegdec))
	    return -1;	
	  
	  if ((*ret = gst_pad_alloc_buffer (ffmpegdec->srcpad, GST_BUFFER_OFFSET_NONE, fsize, GST_PAD_CAPS (ffmpegdec->srcpad), &outbuf)) != GST_FLOW_OK)
            return -1;

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
				  ffmpegdec->context->width, 
				  ffmpegdec->context->height);
	}	
	
	ffmpegdec->waiting_for_key = FALSE;

	if (!ffmpegdec->picture->key_frame) {
	  GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
	}

        /* If we have used the framerate from the demuxer then
         * also use the demuxer's timestamp information (#317596) */		
        if (ffmpegdec->format.video.fps != -1.0 && inbuf != NULL) {
          gst_buffer_stamp (outbuf, inbuf);
        } else {
          GST_BUFFER_TIMESTAMP (outbuf) = ffmpegdec->next_ts;
          if (ffmpegdec->context->time_base.num != 0 &&
              ffmpegdec->context->time_base.den != 0) {
            GST_BUFFER_DURATION (outbuf) = GST_SECOND *
                ffmpegdec->context->time_base.num /
                ffmpegdec->context->time_base.den;

            /* Take repeat_pict into account */
            GST_BUFFER_DURATION (outbuf) += GST_BUFFER_DURATION (outbuf)
                * ffmpegdec->picture->repeat_pict / 2;

            ffmpegdec->next_ts += GST_BUFFER_DURATION (outbuf);
          } else {
            ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
          }
        }
      } else if (ffmpegdec->picture->pict_type != -1 &&
		 oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
        /* update time for skip-frame */
        if ((ffmpegdec->picture->pict_type == FF_I_TYPE ||
             !GST_CLOCK_TIME_IS_VALID (ffmpegdec->next_ts)) &&
            GST_CLOCK_TIME_IS_VALID (*in_ts)) {
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

          ffmpegdec->next_ts += dur;
        } else {
          ffmpegdec->next_ts = GST_CLOCK_TIME_NONE;
        }
      }
      break;

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
    *got_data = 1; //(ffmpegdec->pctx || have_data) ? 1 : 0;
  }
  
  if (have_data) {
    GST_DEBUG_OBJECT (ffmpegdec, "Decoded data, now pushing (%"
        GST_TIME_FORMAT ")", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegdec->srcpad));
    *ret = gst_pad_push (ffmpegdec->srcpad, outbuf);
  }

  return len;
}

static gboolean
gst_ffmpegdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) GST_OBJECT_PARENT (pad);
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (ffmpegdec,
      "Handling event of type %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
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
      goto forward;
    case GST_EVENT_FLUSH_STOP:
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);
      }
      goto forward;
    case GST_EVENT_NEWSEGMENT: {
      gint64 base, start, end;
      gdouble rate;
      GstFormat fmt;

      gst_event_parse_newsegment (event, NULL, &rate, &fmt, &start, &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        ffmpegdec->next_ts = start;
        GST_DEBUG_OBJECT (ffmpegdec, "Discont to time (next_ts) %" GST_TIME_FORMAT" -- %"GST_TIME_FORMAT,
            GST_TIME_ARGS (start), GST_TIME_ARGS (end));
      } else if (ffmpegdec->context->bit_rate && fmt == GST_FORMAT_BYTES) {
        ffmpegdec->next_ts = start * GST_SECOND / ffmpegdec->context->bit_rate;
        GST_DEBUG_OBJECT (ffmpegdec,
            "Newsegment in bytes from byte %" G_GINT64_FORMAT
            " (time %" GST_TIME_FORMAT ") to byte % "G_GINT64_FORMAT
            " (time %" GST_TIME_FORMAT ")",
            start, GST_TIME_ARGS (ffmpegdec->next_ts),
            end,
            GST_TIME_ARGS (end * GST_SECOND / ffmpegdec->context->bit_rate));
        gst_event_unref (event);
        event = gst_event_new_newsegment (FALSE, rate, fmt,
            start * GST_SECOND / ffmpegdec->context->bit_rate,
            end == -1 ? -1 : end * GST_SECOND / ffmpegdec->context->bit_rate,
            base * GST_SECOND / ffmpegdec->context->bit_rate);
      } else {
        GST_WARNING_OBJECT (ffmpegdec,
            "Received discont with no useful value...");
      }
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);

        if (ffmpegdec->context->codec_id == CODEC_ID_MPEG2VIDEO ||
            ffmpegdec->context->codec_id == CODEC_ID_MPEG4 ||
            ffmpegdec->context->codec_id == CODEC_ID_H264) {
          ffmpegdec->waiting_for_key = TRUE;
        }
      }
      ffmpegdec->waiting_for_key = TRUE;
      ffmpegdec->synctime = ffmpegdec->next_ts;
      /* fall-through */
    }
    default:
    forward:
      return gst_pad_event_default (ffmpegdec->sinkpad, event);
  }

  return TRUE;
}

static GstFlowReturn
gst_ffmpegdec_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) (GST_PAD_PARENT (pad));
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  guint8 *bdata, *data;
  gint bsize, size, len, have_data;
  guint64 in_ts = GST_BUFFER_TIMESTAMP (inbuf);
  GstFlowReturn ret = GST_FLOW_OK;

  if (!ffmpegdec->opened)
    goto not_negotiated;
  
  GST_DEBUG_OBJECT (ffmpegdec,
      "Received new data of size %d, time %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (inbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  /* parse cache joining */
  if (ffmpegdec->pcache) {
    inbuf = gst_buffer_span (ffmpegdec->pcache, 0, inbuf,
	    GST_BUFFER_SIZE (ffmpegdec->pcache) + GST_BUFFER_SIZE (inbuf));
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
          &data, &size, bdata, bsize,
          ffpts, ffpts);

      GST_DEBUG_OBJECT (ffmpegdec, "Parsed video frame, res=%d, size=%d",
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

  if ((ffmpegdec->pctx || oclass->in_plugin->id == CODEC_ID_MP3) &&
      bsize > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Keeping %d bytes of data", bsize);

    ffmpegdec->pcache = gst_buffer_create_sub (inbuf,
        GST_BUFFER_SIZE (inbuf) - bsize, bsize);
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
    return GST_FLOW_NOT_NEGOTIATED;
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
      gst_ffmpegdec_close (ffmpegdec);
      if (ffmpegdec->last_buffer != NULL) {
	gst_buffer_unref (ffmpegdec->last_buffer);
        ffmpegdec->last_buffer = NULL;
      }
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
      ffmpegdec->lowres = ffmpegdec->context->lowres =
          g_value_get_enum (value);
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
      g_warning ("Add decoder %s (%d) please",
          in_plugin->name, in_plugin->id);
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
      if (sinkcaps) gst_caps_unref (sinkcaps);
      if (srccaps) gst_caps_unref (srccaps);
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
