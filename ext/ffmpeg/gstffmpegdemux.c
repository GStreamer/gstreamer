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

#include <string.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#include <avi.h>
#else
#include <ffmpeg/avformat.h>
#include <ffmpeg/avi.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegDemux GstFFMpegDemux;

struct _GstFFMpegDemux
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *sinkpad;

  AVFormatContext *context;
  gboolean opened;

  GstPad *srcpads[MAX_STREAMS];
  gboolean handled[MAX_STREAMS];
  guint64 last_ts[MAX_STREAMS];
  gint videopads, audiopads;
};

typedef struct _GstFFMpegDemuxClassParams
{
  AVInputFormat *in_plugin;
  GstCaps *sinkcaps, *videosrccaps, *audiosrccaps;
} GstFFMpegDemuxClassParams;

typedef struct _GstFFMpegDemuxClass GstFFMpegDemuxClass;

struct _GstFFMpegDemuxClass
{
  GstElementClass parent_class;

  AVInputFormat *in_plugin;
  GstPadTemplate *sinktempl;
  GstPadTemplate *videosrctempl;
  GstPadTemplate *audiosrctempl;
};

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegDemux))
#define GST_FFMPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegDemuxClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void gst_ffmpegdemux_class_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_init (GstFFMpegDemux * demux);

static void gst_ffmpegdemux_loop (GstElement * element);

static GstElementStateReturn
gst_ffmpegdemux_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdemux_signals[LAST_SIGNAL] = { 0 }; */

static const gchar *
gst_ffmpegdemux_averror (gint av_errno)
{
  const gchar *message = NULL;

  switch (av_errno) {
    default:
    case AVERROR_UNKNOWN:
      message = "Unknown error";
      break;
    case AVERROR_IO:
      message = "Input/output error";
      break;
    case AVERROR_NUMEXPECTED:
      message = "Number syntax expected in filename";
      break;
    case AVERROR_INVALIDDATA:
      message = "Invalid data found";
      break;
    case AVERROR_NOMEM:
      message = "Not enough memory";
      break;
    case AVERROR_NOFMT:
      message = "Unknown format";
      break;
    case AVERROR_NOTSUPP:
      message = "Operation not supported";
      break;
  }

  return message;
}

static void
gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDemuxClassParams *params;
  GstElementDetails details;
  GstPadTemplate *sinktempl, *audiosrctempl, *videosrctempl;

  params = g_hash_table_lookup (global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins, GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s demuxer",
      params->in_plugin->long_name);
  details.klass = "Codec/Demuxer";
  details.description = g_strdup_printf ("FFMPEG %s decoder",
      params->in_plugin->long_name);
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.description);

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, params->sinkcaps);
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, params->videosrccaps);
  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, params->audiosrccaps);

  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = params->in_plugin;
  klass->videosrctempl = videosrctempl;
  klass->audiosrctempl = audiosrctempl;
  klass->sinktempl = sinktempl;
}

static void
gst_ffmpegdemux_class_init (GstFFMpegDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ffmpegdemux_change_state;
}

static void
gst_ffmpegdemux_init (GstFFMpegDemux * demux)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) (G_OBJECT_GET_CLASS (demux));
  gint n;

  demux->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (demux), gst_ffmpegdemux_loop);

  demux->opened = FALSE;
  demux->context = NULL;

  for (n = 0; n < MAX_STREAMS; n++) {
    demux->srcpads[n] = NULL;
    demux->handled[n] = FALSE;
    demux->last_ts[n] = GST_CLOCK_TIME_NONE;
  }
  demux->videopads = 0;
  demux->audiopads = 0;
}

static void
gst_ffmpegdemux_close (GstFFMpegDemux * demux)
{
  gint n;

  if (!demux->opened)
    return;

  /* remove pads from ourselves */
  for (n = 0; n < MAX_STREAMS; n++) {
    if (demux->srcpads[n]) {
      gst_element_remove_pad (GST_ELEMENT (demux), demux->srcpads[n]);
      demux->srcpads[n] = NULL;
    }
    demux->handled[n] = FALSE;
    demux->last_ts[n] = GST_CLOCK_TIME_NONE;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  /* close demuxer context from ffmpeg */
  av_close_input_file (demux->context);
  demux->context = NULL;

  demux->opened = FALSE;
}

static AVStream *
gst_ffmpegdemux_stream_from_pad (GstPad * pad)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);
  AVStream *stream = NULL;
  gint n;

  for (n = 0; n < MAX_STREAMS; n++) {
    if (demux->srcpads[n] == pad) {
      stream = demux->context->streams[n];
      break;
    }
  }

  return stream;
}

static const GstEventMask *
gst_ffmpegdemux_src_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}

static gboolean
gst_ffmpegdemux_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = TRUE;
  gint64 offset;

  if (!stream)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      offset = GST_EVENT_SEEK_OFFSET (event);
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_DEFAULT:
          if (stream->codec->codec_type != CODEC_TYPE_VIDEO) {
            res = FALSE;
            break;
          } else {
            GstFormat fmt = GST_FORMAT_TIME;

            if (!(res = gst_pad_convert (pad, GST_FORMAT_DEFAULT, offset,
                        &fmt, &offset)))
              break;
          }
          /* fall-through */
        case GST_FORMAT_TIME:
          if (av_seek_frame (demux->context, stream->index,
                  offset / (GST_SECOND / AV_TIME_BASE) , 0))
            res = FALSE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstFormat *
gst_ffmpegdemux_src_format_list (GstPad * pad)
{
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  static const GstFormat src_v_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    0
  }, src_a_formats[] = {
  GST_FORMAT_TIME, 0};

  return (stream->codec->codec_type == CODEC_TYPE_VIDEO) ?
      src_v_formats : src_a_formats;
}

static const GstQueryType *
gst_ffmpegdemux_src_query_list (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return src_types;
}

static gboolean
gst_ffmpegdemux_src_query (GstPad * pad,
    GstQueryType type, GstFormat * fmt, gint64 * value)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*fmt) {
        case GST_FORMAT_TIME:
          if (stream) {
            *value = stream->duration * (GST_SECOND / AV_TIME_BASE);
            res = TRUE;
          }
          break;
        case GST_FORMAT_DEFAULT:
          if (stream->codec_info_nb_frames &&
              stream->codec->codec_type == CODEC_TYPE_VIDEO) {
            *value = stream->codec_info_nb_frames;
            res = TRUE;
          }
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL) {
            res = gst_pad_query (GST_PAD_PEER (demux->sinkpad),
                type, fmt, value);
          }
          break;
        default:
          break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*fmt) {
        case GST_FORMAT_TIME:
          if (stream &&
              GST_CLOCK_TIME_IS_VALID (demux->last_ts[stream->index])) {
            *value = demux->last_ts[stream->index];
            res = TRUE;
          }
          break;
        case GST_FORMAT_DEFAULT:
          if (stream && stream->codec->codec_type == CODEC_TYPE_VIDEO &&
              GST_CLOCK_TIME_IS_VALID (demux->last_ts[stream->index])) {
            res = gst_pad_convert (pad, GST_FORMAT_TIME,
                demux->last_ts[stream->index], fmt, value);
          }
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL) {
            res = gst_pad_query (GST_PAD_PEER (demux->sinkpad),
                type, fmt, value);
          }
        default:
          break;
      }
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_ffmpegdemux_src_convert (GstPad * pad,
    GstFormat src_fmt,
    gint64 src_value, GstFormat * dest_fmt, gint64 * dest_value)
{
  /*GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);*/
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = TRUE;

  if (!stream || stream->codec->codec_type != CODEC_TYPE_VIDEO)
    return FALSE;

  switch (src_fmt) {
    case GST_FORMAT_TIME:
      switch (*dest_fmt) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * stream->r_frame_rate.num /
              (GST_SECOND * stream->r_frame_rate.den);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND * stream->r_frame_rate.num /
              stream->r_frame_rate.den;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static gboolean
gst_ffmpegdemux_add (GstFFMpegDemux * demux, AVStream * stream)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);
  GstPadTemplate *templ = NULL;
  GstPad *pad;
  GstCaps *caps;
  gint num;
  gchar *padname;
  const gchar *codec;

  switch (stream->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
      templ = oclass->videosrctempl;
      num = demux->videopads++;
      break;
    case CODEC_TYPE_AUDIO:
      templ = oclass->audiosrctempl;
      num = demux->audiopads++;
      break;
    default:
      GST_WARNING ("Unknown pad type %d", stream->codec->codec_type);
      break;
  }
  if (!templ)
    return FALSE;

  /* create new pad for this stream */
  padname = g_strdup_printf (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ), num);
  pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  gst_pad_use_explicit_caps (pad);
  gst_pad_set_formats_function (pad, gst_ffmpegdemux_src_format_list);
  gst_pad_set_event_mask_function (pad, gst_ffmpegdemux_src_event_mask);
  gst_pad_set_event_function (pad, gst_ffmpegdemux_src_event);
  gst_pad_set_query_type_function (pad, gst_ffmpegdemux_src_query_list);
  gst_pad_set_query_function (pad, gst_ffmpegdemux_src_query);
  gst_pad_set_convert_function (pad, gst_ffmpegdemux_src_convert);

  /* store pad internally */
  demux->srcpads[stream->index] = pad;

  /* get caps that belongs to this stream */
  caps =
      gst_ffmpeg_codecid_to_caps (stream->codec->codec_id, stream->codec, TRUE);
  gst_pad_set_explicit_caps (pad, caps);

  gst_element_add_pad (GST_ELEMENT (demux), pad);

  /* metadata */
  if ((codec = gst_ffmpeg_get_codecid_longname (stream->codec->codec_id))) {
    GstTagList *list = gst_tag_list_new ();

    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        (stream->codec->codec_type == CODEC_TYPE_VIDEO) ?
        GST_TAG_VIDEO_CODEC : GST_TAG_AUDIO_CODEC, codec, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (demux), pad, 0, list);
  }

  return TRUE;
}

static gboolean
gst_ffmpegdemux_open (GstFFMpegDemux * demux)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);
  gchar *location;
  gint res;

  /* to be sure... */
  gst_ffmpegdemux_close (demux);

  /* open via our input protocol hack */
  location = g_strdup_printf ("gstreamer://%p", demux->sinkpad);
  res = av_open_input_file (&demux->context, location,
      oclass->in_plugin, 0, NULL);
  if (res >= 0)
    res = av_find_stream_info (demux->context);
  g_free (location);
  if (res < 0) {
    GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
        (gst_ffmpegdemux_averror (res)));
    return FALSE;
  }

  /* open_input_file() automatically reads the header. We can now map each
   * created AVStream to a GstPad to make GStreamer handle it. */
  for (res = 0; res < demux->context->nb_streams; res++) {
    gst_ffmpegdemux_add (demux, demux->context->streams[res]);
    demux->handled[res] = TRUE;
  }

  demux->opened = TRUE;

  return TRUE;
}

#define GST_FFMPEG_TYPE_FIND_SIZE 4096
static void
gst_ffmpegdemux_type_find (GstTypeFind * tf, gpointer priv)
{
  guint8 *data;
  GstFFMpegDemuxClassParams *params = (GstFFMpegDemuxClassParams *) priv;
  AVInputFormat *in_plugin = params->in_plugin;
  gint res = 0;

  if (in_plugin->read_probe &&
      (data = gst_type_find_peek (tf, 0, GST_FFMPEG_TYPE_FIND_SIZE)) != NULL) {
    AVProbeData probe_data;

    probe_data.filename = "";
    probe_data.buf = data;
    probe_data.buf_size = GST_FFMPEG_TYPE_FIND_SIZE;

    res = in_plugin->read_probe (&probe_data);
    if (res > 0) {
      res = MAX (1, res * GST_TYPE_FIND_MAXIMUM / AVPROBE_SCORE_MAX);
      gst_type_find_suggest (tf, res, params->sinkcaps);
    }
  }
}

static void
gst_ffmpegdemux_loop (GstElement * element)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) (element);
  gint res;
  AVPacket pkt;
  GstPad *pad;

  /* open file if we didn't so already */
  if (!demux->opened) {
    if (!gst_ffmpegdemux_open (demux))
      return;
    gst_element_no_more_pads (element);
    return;
  }

  /* read a package */
  res = av_read_frame (demux->context, &pkt);
  if (res < 0) {
#if 0
    /* This doesn't work - FIXME */
    if (url_feof (&demux->context->pb)) {
      gst_ffmpegdemux_close (demux);
      gst_pad_event_default (demux->sinkpad, gst_event_new (GST_EVENT_EOS));
    } else {
      GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
          (gst_ffmpegdemux_averror (res)));
    }
#endif

    /* so, we do it the hacky way. */
    GstData *data = NULL;

    do {
      data = gst_pad_pull (demux->sinkpad);

      if (!GST_IS_EVENT (data) ||
          GST_EVENT_TYPE (GST_EVENT (data)) != GST_EVENT_EOS) {
        gst_data_unref (data);
        data = NULL;
      }
    } while (!data);

    gst_pad_event_default (demux->sinkpad, GST_EVENT (data));
    //gst_ffmpegdemux_close (demux);

    return;
  }

  /* for stream-generation-while-playing */
  if (!demux->handled[pkt.stream_index]) {
    gst_ffmpegdemux_add (demux, demux->context->streams[pkt.stream_index]);
    demux->handled[pkt.stream_index] = TRUE;
  }

  /* shortcut to pad belonging to this stream */
  pad = demux->srcpads[pkt.stream_index];

  /* and handle the data by pushing it forward... */
  if (pad && GST_PAD_IS_USABLE (pad)) {
    AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (pkt.size);
    memcpy (GST_BUFFER_DATA (outbuf), pkt.data, pkt.size);
    GST_BUFFER_SIZE (outbuf) = pkt.size;

    if (pkt.pts != AV_NOPTS_VALUE) {
      GST_BUFFER_TIMESTAMP (outbuf) = (GstClockTime) (pkt.pts +
          stream->start_time) * GST_SECOND / AV_TIME_BASE;
      demux->last_ts[stream->index] = GST_BUFFER_TIMESTAMP (outbuf);
    }

    if (pkt.flags & PKT_FLAG_KEY)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
    else
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DELTA_UNIT);

    gst_pad_push (pad, GST_DATA (outbuf));
  }

  pkt.destruct (&pkt);
}

static GstElementStateReturn
gst_ffmpegdemux_change_state (GstElement * element)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) (element);
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      gst_ffmpegdemux_close (demux);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_ffmpegdemux_register (GstPlugin * plugin)
{
  GType type;
  AVInputFormat *in_plugin;
  GstFFMpegDemuxClassParams *params;
  gchar **extensions;
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegDemuxClass),
    (GBaseInitFunc) gst_ffmpegdemux_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegdemux_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegDemux),
    0,
    (GInstanceInitFunc) gst_ffmpegdemux_init,
  };

  in_plugin = first_iformat;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name, *typefind_name;
    gchar *p, *name = NULL;
    GstCaps *sinkcaps, *audiosrccaps, *videosrccaps;
    gint rank = GST_RANK_MARGINAL;
    gboolean register_typefind_func = TRUE;

    /* no emulators */
    if (!strncmp (in_plugin->long_name, "raw ", 4) ||
        !strncmp (in_plugin->long_name, "pcm ", 4) ||
        !strcmp (in_plugin->name, "audio_device") ||
        !strncmp (in_plugin->name, "image", 5) ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mjpeg"))
      goto next;

    if (!strcmp (in_plugin->name, "mov,mp4,m4a,3gp,3g2") ||
        !strcmp (in_plugin->name, "avi") ||
        !strcmp (in_plugin->name, "asf") ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mp3") ||
        !strcmp (in_plugin->name, "matroska") ||
        !strcmp (in_plugin->name, "mpeg") ||
        !strcmp (in_plugin->name, "wav") ||
        !strcmp (in_plugin->name, "au") ||
        !strcmp (in_plugin->name, "rm"))
      register_typefind_func = FALSE;

    p = name = g_strdup (in_plugin->name);
    while (*p) {
      if (*p == '.' || *p == ',')
        *p = '_';
      p++;
    }

    /* Try to find the caps that belongs here */
    sinkcaps = gst_ffmpeg_formatid_to_caps (name);
    if (!sinkcaps) {
      goto next;
    }
    /* This is a bit ugly, but we just take all formats
     * for the pad template. We'll get an exact match
     * when we open the stream */
    audiosrccaps = gst_caps_new_any ();
    videosrccaps = gst_caps_new_any ();

    /* construct the type */
    type_name = g_strdup_printf ("ffdemux_%s", name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      gst_caps_free (videosrccaps);
      gst_caps_free (audiosrccaps);
      g_free (type_name);
      goto next;
    }

    typefind_name = g_strdup_printf ("fftype_%s", name);

    /* create a cache for these properties */
    params = g_new0 (GstFFMpegDemuxClassParams, 1);
    params->in_plugin = in_plugin;
    params->sinkcaps = sinkcaps;
    params->videosrccaps = videosrccaps;
    params->audiosrccaps = audiosrccaps;

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (type), (gpointer) params);

    if (in_plugin->extensions)
      extensions = g_strsplit (in_plugin->extensions, " ", 0);
    else
      extensions = NULL;

    if (!gst_element_register (plugin, type_name, rank, type) ||
        (register_typefind_func == TRUE &&
         !gst_type_find_register (plugin, typefind_name, rank,
             gst_ffmpegdemux_type_find, extensions, sinkcaps, params))) {
      g_warning ("Register of type ffdemux_%s failed", name);
      g_free (type_name);
      g_free (typefind_name);
      return FALSE;
    }

    g_free (type_name);
    g_free (typefind_name);
    if (extensions)
      g_strfreev (extensions);

  next:
    g_free (name);
    in_plugin = in_plugin->next;
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
