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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavprotocol.h"

typedef struct _GstFFMpegMux GstFFMpegMux;
typedef struct _GstFFMpegMuxPad GstFFMpegMuxPad;

struct _GstFFMpegMuxPad
{
  GstCollectData collect;       /* we extend the CollectData */

  gint padnum;
};

struct _GstFFMpegMux
{
  GstElement element;

  GstCollectPads *collect;
  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;

  AVFormatContext *context;
  gboolean opened;

  guint videopads, audiopads;

  /*< private > */
  /* event_function is the collectpads default eventfunction */
  GstPadEventFunction event_function;
  int max_delay;
  int preload;
};

typedef struct _GstFFMpegMuxClass GstFFMpegMuxClass;

struct _GstFFMpegMuxClass
{
  GstElementClass parent_class;

  AVOutputFormat *in_plugin;
};

#define GST_TYPE_FFMPEGMUX \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGMUX,GstFFMpegMux))
#define GST_FFMPEGMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGMUX,GstFFMpegMuxClass))
#define GST_IS_FFMPEGMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGMUX))
#define GST_IS_FFMPEGMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGMUX))

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_PRELOAD,
  PROP_MAXDELAY
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass);
static void gst_ffmpegmux_base_init (gpointer g_class);
static void gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux,
    GstFFMpegMuxClass * g_class);
static void gst_ffmpegmux_finalize (GObject * object);

static gboolean gst_ffmpegmux_setcaps (GstPad * pad, GstCaps * caps);
static GstPad *gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static GstFlowReturn gst_ffmpegmux_collected (GstCollectPads * pads,
    gpointer user_data);

static gboolean gst_ffmpegmux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_ffmpegmux_change_state (GstElement * element,
    GstStateChange transition);

static void gst_ffmpegmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ffmpegmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_ffmpegmux_get_id_caps (enum AVCodecID *id_list);
static void gst_ffmpeg_mux_simple_caps_set_int_list (GstCaps * caps,
    const gchar * field, guint num, const gint * values);

#define GST_FFMUX_PARAMS_QDATA g_quark_from_static_string("avmux-params")

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegmux_signals[LAST_SIGNAL] = { 0 }; */

typedef struct
{
  const char *name;
  const char *replacement;
} GstFFMpegMuxReplacement;

static const char *
gst_ffmpegmux_get_replacement (const char *name)
{
  static const GstFFMpegMuxReplacement blacklist[] = {
    {"avi", "avimux"},
    {"matroska", "matroskamux"},
    {"mov", "qtmux"},
    {"mpegts", "mpegtsmux"},
    {"mp4", "mp4mux"},
    {"mpjpeg", "multipartmux"},
    {"ogg", "oggmux"},
    {"wav", "wavenc"},
    {"webm", "webmmux"},
    {"mxf", "mxfmux"},
    {"3gp", "gppmux"},
    {"yuv4mpegpipe", "y4menc"},
    {"aiff", "aiffmux"},
    {"adts", "aacparse"},
    {"asf", "asfmux"},
    {"asf_stream", "asfmux"},
    {"flv", "flvmux"},
    {"mp3", "id3v2mux"},
    {"mp2", "id3v2mux"}
  };
  int i;

  for (i = 0; i < sizeof (blacklist) / sizeof (blacklist[0]); i++) {
    if (strcmp (blacklist[i].name, name) == 0) {
      return blacklist[i].replacement;
    }
  }

  return NULL;
}

static gboolean
gst_ffmpegmux_is_formatter (const char *name)
{
  static const char *replace[] = {
    "mp2", "mp3", NULL
  };
  int i;

  for (i = 0; replace[i]; i++)
    if (strcmp (replace[i], name) == 0)
      return TRUE;
  return FALSE;
}

static void
gst_ffmpegmux_base_init (gpointer g_class)
{
  GstFFMpegMuxClass *klass = (GstFFMpegMuxClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *videosinktempl, *audiosinktempl, *srctempl;
  AVOutputFormat *in_plugin;
  GstCaps *srccaps, *audiosinkcaps, *videosinkcaps;
  enum AVCodecID *video_ids = NULL, *audio_ids = NULL;
  gchar *longname, *description, *name;
  const char *replacement;
  gboolean is_formatter;

  in_plugin =
      (AVOutputFormat *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFMUX_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  name = g_strdup (in_plugin->name);
  g_strdelimit (name, ".,|-<> ", '_');

  /* construct the element details struct */
  replacement = gst_ffmpegmux_get_replacement (in_plugin->name);
  is_formatter = gst_ffmpegmux_is_formatter (in_plugin->name);
  if (replacement != NULL) {
    longname =
        g_strdup_printf ("libav %s %s (not recommended, use %s instead)",
        in_plugin->long_name, is_formatter ? "formatter" : "muxer",
        replacement);
    description =
        g_strdup_printf ("libav %s %s (not recommended, use %s instead)",
        in_plugin->long_name, is_formatter ? "formatter" : "muxer",
        replacement);
  } else {
    longname = g_strdup_printf ("libav %s %s", in_plugin->long_name,
        is_formatter ? "formatter" : "muxer");
    description = g_strdup_printf ("libav %s %s", in_plugin->long_name,
        is_formatter ? "formatter" : "muxer");
  }
  gst_element_class_set_metadata (element_class, longname,
      is_formatter ? "Formatter/Metadata" : "Codec/Muxer", description,
      "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  g_free (longname);
  g_free (description);

  /* Try to find the caps that belongs here */
  srccaps = gst_ffmpeg_formatid_to_caps (name);
  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for muxer '%s', skipping", name);
    goto beach;
  }

  if (!gst_ffmpeg_formatid_get_codecids (in_plugin->name,
          &video_ids, &audio_ids, in_plugin)) {
    gst_caps_unref (srccaps);
    GST_DEBUG ("Couldn't get sink caps for muxer '%s'. Most likely because "
        "no input format mapping exists.", name);
    goto beach;
  }

  videosinkcaps = video_ids ? gst_ffmpegmux_get_id_caps (video_ids) : NULL;
  audiosinkcaps = audio_ids ? gst_ffmpegmux_get_id_caps (audio_ids) : NULL;

  /* fix up allowed caps for some muxers */
  /* FIXME : This should be in gstffmpegcodecmap.c ! */
  if (strcmp (in_plugin->name, "flv") == 0) {
    const gint rates[] = { 44100, 22050, 11025 };

    gst_ffmpeg_mux_simple_caps_set_int_list (audiosinkcaps, "rate", 3, rates);
  } else if (strcmp (in_plugin->name, "dv") == 0) {
    gst_caps_set_simple (audiosinkcaps,
        "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 2, NULL);

  } else if (strcmp (in_plugin->name, "gif") == 0) {
    if (videosinkcaps)
      gst_caps_unref (videosinkcaps);

    videosinkcaps = gst_caps_from_string ("video/x-raw, format=(string)RGB");
  }

  /* pad templates */
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);
  gst_element_class_add_pad_template (element_class, srctempl);
  gst_caps_unref (srccaps);

  if (audiosinkcaps) {
    audiosinktempl = gst_pad_template_new ("audio_%u",
        GST_PAD_SINK, GST_PAD_REQUEST, audiosinkcaps);
    gst_element_class_add_pad_template (element_class, audiosinktempl);
    gst_caps_unref (audiosinkcaps);
  }

  if (videosinkcaps) {
    videosinktempl = gst_pad_template_new ("video_%u",
        GST_PAD_SINK, GST_PAD_REQUEST, videosinkcaps);
    gst_element_class_add_pad_template (element_class, videosinktempl);
    gst_caps_unref (videosinkcaps);
  }

beach:
  klass->in_plugin = in_plugin;

  g_free (name);
}

static void
gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_ffmpegmux_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_ffmpegmux_get_property);

  g_object_class_install_property (gobject_class, PROP_PRELOAD,
      g_param_spec_int ("preload", "preload",
          "Set the initial demux-decode delay (in microseconds)",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXDELAY,
      g_param_spec_int ("maxdelay", "maxdelay",
          "Set the maximum demux-decode delay (in microseconds)", 0, G_MAXINT,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad = gst_ffmpegmux_request_new_pad;
  gstelement_class->change_state = gst_ffmpegmux_change_state;
  gobject_class->finalize = gst_ffmpegmux_finalize;
}

static void
gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux, GstFFMpegMuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (g_class);
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass *) klass;
  GstPadTemplate *templ = gst_element_class_get_pad_template (klass, "src");

  ffmpegmux->srcpad = gst_pad_new_from_template (templ, "src");
  gst_pad_set_caps (ffmpegmux->srcpad, gst_pad_template_get_caps (templ));
  gst_element_add_pad (GST_ELEMENT (ffmpegmux), ffmpegmux->srcpad);

  ffmpegmux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (ffmpegmux->collect,
      (GstCollectPadsFunction) gst_ffmpegmux_collected, ffmpegmux);

  ffmpegmux->context = avformat_alloc_context ();
  ffmpegmux->context->oformat = oclass->in_plugin;
  ffmpegmux->context->nb_streams = 0;
  ffmpegmux->opened = FALSE;

  ffmpegmux->videopads = 0;
  ffmpegmux->audiopads = 0;
  ffmpegmux->max_delay = 0;
}

static void
gst_ffmpegmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFFMpegMux *src;

  src = (GstFFMpegMux *) object;

  switch (prop_id) {
    case PROP_PRELOAD:
      src->preload = g_value_get_int (value);
      break;
    case PROP_MAXDELAY:
      src->max_delay = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegmux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFFMpegMux *src;

  src = (GstFFMpegMux *) object;

  switch (prop_id) {
    case PROP_PRELOAD:
      g_value_set_int (value, src->preload);
      break;
    case PROP_MAXDELAY:
      g_value_set_int (value, src->max_delay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_ffmpegmux_finalize (GObject * object)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) object;

  avformat_free_context (ffmpegmux->context);
  ffmpegmux->context = NULL;

  gst_object_unref (ffmpegmux->collect);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) element;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstFFMpegMuxPad *collect_pad;
  gchar *padname;
  GstPad *pad;
  AVStream *st;
  enum AVMediaType type;
  gint bitrate = 0, framesize = 0;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (ffmpegmux->opened == FALSE, NULL);

  /* figure out a name that *we* like */
  if (templ == gst_element_class_get_pad_template (klass, "video_%u")) {
    padname = g_strdup_printf ("video_%u", ffmpegmux->videopads++);
    type = AVMEDIA_TYPE_VIDEO;
    bitrate = 64 * 1024;
    framesize = 1152;
  } else if (templ == gst_element_class_get_pad_template (klass, "audio_%u")) {
    padname = g_strdup_printf ("audio_%u", ffmpegmux->audiopads++);
    type = AVMEDIA_TYPE_AUDIO;
    bitrate = 285 * 1024;
  } else {
    g_warning ("avmux: unknown pad template!");
    return NULL;
  }

  /* create pad */
  pad = gst_pad_new_from_template (templ, padname);
  collect_pad = (GstFFMpegMuxPad *)
      gst_collect_pads_add_pad (ffmpegmux->collect, pad,
      sizeof (GstFFMpegMuxPad), NULL, TRUE);
  collect_pad->padnum = ffmpegmux->context->nb_streams;

  /* small hack to put our own event pad function and chain up to collect pad */
  ffmpegmux->event_function = GST_PAD_EVENTFUNC (pad);
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_ffmpegmux_sink_event));

  gst_element_add_pad (element, pad);

  /* AVStream needs to be created */
  st = avformat_new_stream (ffmpegmux->context, NULL);
  st->id = collect_pad->padnum;
  st->codec->codec_type = type;
  st->codec->codec_id = AV_CODEC_ID_NONE;       /* this is a check afterwards */
  st->codec->bit_rate = bitrate;
  st->codec->frame_size = framesize;
  /* we fill in codec during capsnego */

  /* we love debug output (c) (tm) (r) */
  GST_DEBUG ("Created %s pad for avmux_%s element",
      padname, ((GstFFMpegMuxClass *) klass)->in_plugin->name);
  g_free (padname);

  return pad;
}

/**
 * gst_ffmpegmux_setcaps
 * @pad: #GstPad
 * @caps: New caps.
 *
 * Set caps to pad.
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_ffmpegmux_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (gst_pad_get_parent (pad));
  GstFFMpegMuxPad *collect_pad;
  AVStream *st;

  collect_pad = (GstFFMpegMuxPad *) gst_pad_get_element_private (pad);

  st = ffmpegmux->context->streams[collect_pad->padnum];
  av_opt_set_int (ffmpegmux->context, "preload", ffmpegmux->preload, 0);
  ffmpegmux->context->max_delay = ffmpegmux->max_delay;

  /* for the format-specific guesses, we'll go to
   * our famous codec mapper */
  if (gst_ffmpeg_caps_to_codecid (caps, st->codec) == AV_CODEC_ID_NONE)
    goto not_accepted;

  /* copy over the aspect ratios, ffmpeg expects the stream aspect to match the
   * codec aspect. */
  st->sample_aspect_ratio = st->codec->sample_aspect_ratio;

  GST_LOG_OBJECT (pad, "accepted caps %" GST_PTR_FORMAT, caps);
  return TRUE;

  /* ERRORS */
not_accepted:
  {
    GST_LOG_OBJECT (pad, "rejecting caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}


static gboolean
gst_ffmpegmux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) parent;
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *taglist;
      GstTagSetter *setter = GST_TAG_SETTER (ffmpegmux);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      gst_event_parse_tag (event, &taglist);
      gst_tag_setter_merge_tags (setter, taglist, mode);
      break;
    }
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      if (!(res = gst_ffmpegmux_setcaps (pad, caps)))
        goto beach;
      break;
    }
    default:
      break;
  }

  /* chaining up to collectpads default event function */
  res = ffmpegmux->event_function (pad, parent, event);

beach:
  return res;
}

static GstFlowReturn
gst_ffmpegmux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) user_data;
  GSList *collected;
  GstFFMpegMuxPad *best_pad;
  GstClockTime best_time;
#if 0
  /* Re-enable once converted to new AVMetaData API
   * See #566605
   */
  const GstTagList *tags;
#endif

  /* open "file" (gstreamer protocol to next element) */
  if (!ffmpegmux->opened) {
    int open_flags = AVIO_FLAG_WRITE;

    /* we do need all streams to have started capsnego,
     * or things will go horribly wrong */
    for (collected = ffmpegmux->collect->data; collected;
        collected = g_slist_next (collected)) {
      GstFFMpegMuxPad *collect_pad = (GstFFMpegMuxPad *) collected->data;
      AVStream *st = ffmpegmux->context->streams[collect_pad->padnum];

      /* check whether the pad has successfully completed capsnego */
      if (st->codec->codec_id == AV_CODEC_ID_NONE) {
        GST_ELEMENT_ERROR (ffmpegmux, CORE, NEGOTIATION, (NULL),
            ("no caps set on stream %d (%s)", collect_pad->padnum,
                (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) ?
                "video" : "audio"));
        return GST_FLOW_ERROR;
      }
      /* set framerate for audio */
      if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        switch (st->codec->codec_id) {
          case AV_CODEC_ID_PCM_S16LE:
          case AV_CODEC_ID_PCM_S16BE:
          case AV_CODEC_ID_PCM_U16LE:
          case AV_CODEC_ID_PCM_U16BE:
          case AV_CODEC_ID_PCM_S8:
          case AV_CODEC_ID_PCM_U8:
            st->codec->frame_size = 1;
            break;
          default:
          {
            GstBuffer *buffer;

            /* FIXME : This doesn't work for RAW AUDIO...
             * in fact I'm wondering if it even works for any kind of audio... */
            buffer = gst_collect_pads_peek (ffmpegmux->collect,
                (GstCollectData *) collect_pad);
            if (buffer) {
              st->codec->frame_size =
                  st->codec->sample_rate *
                  GST_BUFFER_DURATION (buffer) / GST_SECOND;
              gst_buffer_unref (buffer);
            }
          }
        }
      }
    }

#if 0
    /* Re-enable once converted to new AVMetaData API
     * See #566605
     */

    /* tags */
    tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (ffmpegmux));
    if (tags) {
      gint i;
      gchar *s;

      /* get the interesting ones */
      if (gst_tag_list_get_string (tags, GST_TAG_TITLE, &s)) {
        strncpy (ffmpegmux->context->title, s,
            sizeof (ffmpegmux->context->title));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &s)) {
        strncpy (ffmpegmux->context->author, s,
            sizeof (ffmpegmux->context->author));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_COPYRIGHT, &s)) {
        strncpy (ffmpegmux->context->copyright, s,
            sizeof (ffmpegmux->context->copyright));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_COMMENT, &s)) {
        strncpy (ffmpegmux->context->comment, s,
            sizeof (ffmpegmux->context->comment));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &s)) {
        strncpy (ffmpegmux->context->album, s,
            sizeof (ffmpegmux->context->album));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_GENRE, &s)) {
        strncpy (ffmpegmux->context->genre, s,
            sizeof (ffmpegmux->context->genre));
      }
      if (gst_tag_list_get_int (tags, GST_TAG_TRACK_NUMBER, &i)) {
        ffmpegmux->context->track = i;
      }
    }
#endif

    /* set the streamheader flag for gstffmpegprotocol if codec supports it */
    if (!strcmp (ffmpegmux->context->oformat->name, "flv")) {
      open_flags |= GST_FFMPEG_URL_STREAMHEADER;
    }

    /* some house-keeping for downstream before starting data flow */
    /* stream-start (FIXME: create id based on input ids) */
    {
      gchar s_id[32];

      g_snprintf (s_id, sizeof (s_id), "avmux-%08x", g_random_int ());
      gst_pad_push_event (ffmpegmux->srcpad, gst_event_new_stream_start (s_id));
    }
    /* segment */
    {
      GstSegment segment;

      /* let downstream know we think in BYTES and expect to do seeking later on */
      gst_segment_init (&segment, GST_FORMAT_BYTES);
      gst_pad_push_event (ffmpegmux->srcpad, gst_event_new_segment (&segment));
    }

    if (gst_ffmpegdata_open (ffmpegmux->srcpad, open_flags,
            &ffmpegmux->context->pb) < 0) {
      GST_ELEMENT_ERROR (ffmpegmux, LIBRARY, TOO_LAZY, (NULL),
          ("Failed to open stream context in avmux"));
      return GST_FLOW_ERROR;
    }

    /* now open the mux format */
    if (avformat_write_header (ffmpegmux->context, NULL) < 0) {
      GST_ELEMENT_ERROR (ffmpegmux, LIBRARY, SETTINGS, (NULL),
          ("Failed to write file header - check codec settings"));
      return GST_FLOW_ERROR;
    }

    /* we're now opened */
    ffmpegmux->opened = TRUE;

    /* flush the header so it will be used as streamheader */
    avio_flush (ffmpegmux->context->pb);
  }

  /* take the one with earliest timestamp,
   * and push it forward */
  best_pad = NULL;
  best_time = GST_CLOCK_TIME_NONE;
  for (collected = ffmpegmux->collect->data; collected;
      collected = g_slist_next (collected)) {
    GstFFMpegMuxPad *collect_pad = (GstFFMpegMuxPad *) collected->data;
    GstBuffer *buffer = gst_collect_pads_peek (ffmpegmux->collect,
        (GstCollectData *) collect_pad);

    /* if there's no buffer, just continue */
    if (buffer == NULL) {
      continue;
    }

    /* if we have no buffer yet, just use the first one */
    if (best_pad == NULL) {
      best_pad = collect_pad;
      best_time = GST_BUFFER_TIMESTAMP (buffer);
      goto next_pad;
    }

    /* if we do have one, only use this one if it's older */
    if (GST_BUFFER_TIMESTAMP (buffer) < best_time) {
      best_time = GST_BUFFER_TIMESTAMP (buffer);
      best_pad = collect_pad;
    }

  next_pad:
    gst_buffer_unref (buffer);

    /* Mux buffers with invalid timestamp first */
    if (!GST_CLOCK_TIME_IS_VALID (best_time))
      break;
  }

  /* now handle the buffer, or signal EOS if we have
   * no buffers left */
  if (best_pad != NULL) {
    GstBuffer *buf;
    AVPacket pkt;
    gboolean need_free = FALSE;
    GstMapInfo map;

    /* push out current buffer */
    buf =
        gst_collect_pads_pop (ffmpegmux->collect, (GstCollectData *) best_pad);

    ffmpegmux->context->streams[best_pad->padnum]->codec->frame_number++;

    /* set time */
    pkt.pts = gst_ffmpeg_time_gst_to_ff (GST_BUFFER_TIMESTAMP (buf),
        ffmpegmux->context->streams[best_pad->padnum]->time_base);
    pkt.dts = pkt.pts;

    if (strcmp (ffmpegmux->context->oformat->name, "gif") == 0) {
      AVStream *st = ffmpegmux->context->streams[best_pad->padnum];
      AVPicture src, dst;

      need_free = TRUE;
      pkt.size = st->codec->width * st->codec->height * 3;
      pkt.data = g_malloc (pkt.size);

      dst.data[0] = pkt.data;
      dst.data[1] = NULL;
      dst.data[2] = NULL;
      dst.linesize[0] = st->codec->width * 3;

      gst_buffer_map (buf, &map, GST_MAP_READ);
      gst_ffmpeg_avpicture_fill (&src, map.data,
          AV_PIX_FMT_RGB24, st->codec->width, st->codec->height);

      av_picture_copy (&dst, &src, AV_PIX_FMT_RGB24,
          st->codec->width, st->codec->height);
      gst_buffer_unmap (buf, &map);
    } else {
      gst_buffer_map (buf, &map, GST_MAP_READ);
      pkt.data = map.data;
      pkt.size = map.size;
    }

    pkt.stream_index = best_pad->padnum;
    pkt.flags = 0;

    if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT))
      pkt.flags |= AV_PKT_FLAG_KEY;

    if (GST_BUFFER_DURATION_IS_VALID (buf))
      pkt.duration =
          gst_ffmpeg_time_gst_to_ff (GST_BUFFER_DURATION (buf),
          ffmpegmux->context->streams[best_pad->padnum]->time_base);
    else
      pkt.duration = 0;
    av_write_frame (ffmpegmux->context, &pkt);
    if (need_free) {
      g_free (pkt.data);
    } else {
      gst_buffer_unmap (buf, &map);
    }
    gst_buffer_unref (buf);
  } else {
    /* close down */
    av_write_trailer (ffmpegmux->context);
    ffmpegmux->opened = FALSE;
    avio_flush (ffmpegmux->context->pb);
    gst_ffmpegdata_close (ffmpegmux->context->pb);
    gst_pad_push_event (ffmpegmux->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_ffmpegmux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (ffmpegmux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (ffmpegmux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_tag_setter_reset_tags (GST_TAG_SETTER (ffmpegmux));
      if (ffmpegmux->opened) {
        ffmpegmux->opened = FALSE;
        gst_ffmpegdata_close (ffmpegmux->context->pb);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_ffmpegmux_get_id_caps (enum AVCodecID *id_list)
{
  GstCaps *caps, *t;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; id_list[i] != AV_CODEC_ID_NONE; i++) {
    if ((t = gst_ffmpeg_codecid_to_caps (id_list[i], NULL, TRUE)))
      gst_caps_append (caps, t);
  }
  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return NULL;
  }

  return caps;
}

/* set a list of integer values on the caps, e.g. for sample rates */
static void
gst_ffmpeg_mux_simple_caps_set_int_list (GstCaps * caps, const gchar * field,
    guint num, const gint * values)
{
  GValue list = { 0, };
  GValue val = { 0, };
  gint i;

  g_return_if_fail (GST_CAPS_IS_SIMPLE (caps));

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&val, G_TYPE_INT);

  for (i = 0; i < num; ++i) {
    g_value_set_int (&val, values[i]);
    gst_value_list_append_value (&list, &val);
  }

  gst_structure_set_value (gst_caps_get_structure (caps, 0), field, &list);

  g_value_unset (&val);
  g_value_unset (&list);
}

gboolean
gst_ffmpegmux_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegMuxClass),
    (GBaseInitFunc) gst_ffmpegmux_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegmux_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegMux),
    0,
    (GInstanceInitFunc) gst_ffmpegmux_init,
  };
  static const GInterfaceInfo tag_setter_info = {
    NULL, NULL, NULL
  };
  GType type;
  AVOutputFormat *in_plugin;

  in_plugin = av_oformat_next (NULL);

  GST_LOG ("Registering muxers");

  while (in_plugin) {
    gchar *type_name;
    GstRank rank = GST_RANK_MARGINAL;

    if ((!strncmp (in_plugin->name, "u16", 3)) ||
        (!strncmp (in_plugin->name, "s16", 3)) ||
        (!strncmp (in_plugin->name, "u24", 3)) ||
        (!strncmp (in_plugin->name, "s24", 3)) ||
        (!strncmp (in_plugin->name, "u8", 2)) ||
        (!strncmp (in_plugin->name, "s8", 2)) ||
        (!strncmp (in_plugin->name, "u32", 3)) ||
        (!strncmp (in_plugin->name, "s32", 3)) ||
        (!strncmp (in_plugin->name, "f32", 3)) ||
        (!strncmp (in_plugin->name, "f64", 3)) ||
        (!strncmp (in_plugin->name, "raw", 3)) ||
        (!strncmp (in_plugin->name, "crc", 3)) ||
        (!strncmp (in_plugin->name, "null", 4)) ||
        (!strncmp (in_plugin->name, "gif", 3)) ||
        (!strncmp (in_plugin->name, "fifo", 4)) ||
        (!strncmp (in_plugin->name, "frame", 5)) ||
        (!strncmp (in_plugin->name, "image", 5)) ||
        (!strncmp (in_plugin->name, "mulaw", 5)) ||
        (!strncmp (in_plugin->name, "alaw", 4)) ||
        (!strncmp (in_plugin->name, "h26", 3)) ||
        (!strncmp (in_plugin->name, "rtp", 3)) ||
        (!strncmp (in_plugin->name, "ass", 3)) ||
        (!strncmp (in_plugin->name, "ffmetadata", 10)) ||
        (!strncmp (in_plugin->name, "srt", 3)) ||
        !strcmp (in_plugin->name, "segment") ||
        !strcmp (in_plugin->name, "stream_segment,ssegment") ||
        !strcmp (in_plugin->name, "jacosub") ||
        !strcmp (in_plugin->name, "webvtt") ||
        !strcmp (in_plugin->name, "lrc") ||
        !strcmp (in_plugin->name, "microdvd") ||
        !strcmp (in_plugin->name, "tee") ||
        !strncmp (in_plugin->name, "webm", 4)
        ) {
      GST_LOG ("Ignoring muxer %s", in_plugin->name);
      goto next;
    }

    if ((!strncmp (in_plugin->long_name, "raw ", 4))) {
      GST_LOG ("Ignoring raw muxer %s", in_plugin->name);
      goto next;
    }

    if (gst_ffmpegmux_get_replacement (in_plugin->name))
      rank = GST_RANK_NONE;

    /* FIXME : We need a fast way to know whether we have mappings for this
     * muxer type. */

    /* construct the type */
    type_name = g_strdup_printf ("avmux_%s", in_plugin->name);
    g_strdelimit (type_name, ".,|-<> ", '_');

    type = g_type_from_name (type_name);

    if (!type) {
      /* create the type now */
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_FFMUX_PARAMS_QDATA, (gpointer) in_plugin);
      g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_oformat_next (in_plugin);
  }

  GST_LOG ("Finished registering muxers");

  return TRUE;
}
