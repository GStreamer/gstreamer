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
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>

#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegMux GstFFMpegMux;

struct _GstFFMpegMux
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;

  AVFormatContext *context;
  gboolean opened;

  GstPad *sinkpads[MAX_STREAMS];
  gint videopads, audiopads;
  GstBuffer *bufferqueue[MAX_STREAMS];
  gboolean eos[MAX_STREAMS];
};

typedef struct _GstFFMpegMuxClassParams
{
  AVOutputFormat *in_plugin;
  GstCaps *srccaps, *videosinkcaps, *audiosinkcaps;
} GstFFMpegMuxClassParams;

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
#define GST_IS_FFMPEGMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGMUX))

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
static void gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass);
static void gst_ffmpegmux_base_init (GstFFMpegMuxClass * klass);
static void gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux);
static void gst_ffmpegmux_dispose (GObject * object);

static GstPadLinkReturn
gst_ffmpegmux_connect (GstPad * pad, const GstCaps * caps);
static GstPad *gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_ffmpegmux_loop (GstElement * element);

static GstElementStateReturn gst_ffmpegmux_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegmux_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegmux_base_init (GstFFMpegMuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstFFMpegMuxClassParams *params;
  GstPadTemplate *videosinktempl, *audiosinktempl, *srctempl;

  params = g_hash_table_lookup (global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins, GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s Muxer",
      params->in_plugin->name);
  details.klass = g_strdup ("Codec/Muxer");
  details.description = g_strdup_printf ("FFMPEG %s Muxer",
      params->in_plugin->name);
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* pad templates */
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->srccaps);
  audiosinktempl = gst_pad_template_new ("audio_%d",
      GST_PAD_SINK, GST_PAD_REQUEST, params->audiosinkcaps);
  videosinktempl = gst_pad_template_new ("video_%d",
      GST_PAD_SINK, GST_PAD_REQUEST, params->videosinkcaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, videosinktempl);
  gst_element_class_add_pad_template (element_class, audiosinktempl);

  klass->in_plugin = params->in_plugin;
}

static void
gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->request_new_pad = gst_ffmpegmux_request_new_pad;
  gstelement_class->change_state = gst_ffmpegmux_change_state;
  gobject_class->dispose = gst_ffmpegmux_dispose;
}

static void
gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ffmpegmux);
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass *) klass;
  GstPadTemplate *templ = gst_element_class_get_pad_template (klass, "src");

  ffmpegmux->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_set_loop_function (GST_ELEMENT (ffmpegmux), gst_ffmpegmux_loop);
  gst_element_add_pad (GST_ELEMENT (ffmpegmux), ffmpegmux->srcpad);

  ffmpegmux->context = g_new0 (AVFormatContext, 1);
  ffmpegmux->context->oformat = oclass->in_plugin;
  ffmpegmux->context->nb_streams = 0;
  snprintf (ffmpegmux->context->filename,
      sizeof (ffmpegmux->context->filename),
      "gstreamer://%p", ffmpegmux->srcpad);
  ffmpegmux->opened = FALSE;

  ffmpegmux->videopads = 0;
  ffmpegmux->audiopads = 0;
}

static void
gst_ffmpegmux_dispose (GObject * object)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) object;

  if (ffmpegmux->opened) {
    url_fclose (&ffmpegmux->context->pb);
    ffmpegmux->opened = FALSE;
  }

  g_free (ffmpegmux->context);
}

static GstPad *
gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) element;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass *) klass;
  gchar *padname;
  GstPad *pad;
  AVStream *st;
  enum CodecType type;
  gint padnum, bitrate = 0, framesize = 0;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (ffmpegmux->opened == FALSE, NULL);

  /* figure out a name that *we* like */
  if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    padname = g_strdup_printf ("video_%d", ffmpegmux->videopads++);
    type = CODEC_TYPE_VIDEO;
    bitrate = 64 * 1024;
    framesize = 1152;
  } else if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    padname = g_strdup_printf ("audio_%d", ffmpegmux->audiopads++);
    type = CODEC_TYPE_AUDIO;
    bitrate = 285 * 1024;
  } else {
    g_warning ("ffmux: unknown pad template!");
    return NULL;
  }

  /* create pad */
  pad = gst_pad_new_from_template (templ, padname);
  padnum = ffmpegmux->context->nb_streams;
  ffmpegmux->sinkpads[padnum] = pad;
  gst_pad_set_link_function (pad, gst_ffmpegmux_connect);
  gst_element_add_pad (element, pad);

  /* AVStream needs to be created */
  st = av_new_stream (ffmpegmux->context, padnum);
  st->codec.codec_type = type;
  st->codec.codec_id = CODEC_ID_NONE;   /* this is a check afterwards */
  st->stream_copy = 1;          /* we're not the actual encoder */
  st->codec.bit_rate = bitrate;
  st->codec.frame_size = framesize;
  /* we fill in codec during capsnego */

  /* we love debug output (c) (tm) (r) */
  GST_DEBUG ("Created %s pad for ffmux_%s element",
      padname, oclass->in_plugin->name);
  g_free (padname);

  return pad;
}

static GstPadLinkReturn
gst_ffmpegmux_connect (GstPad * pad, const GstCaps * caps)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (gst_pad_get_parent (pad));
  gint i;
  AVStream *st;

  /*g_return_val_if_fail (ffmpegmux->opened == FALSE,
     GST_PAD_LINK_REFUSED); */

  for (i = 0; i < ffmpegmux->context->nb_streams; i++) {
    if (pad == ffmpegmux->sinkpads[i]) {
      break;
    }
  }
  if (i == ffmpegmux->context->nb_streams) {
    g_warning ("Unknown pad given during capsnego: %p", pad);
    return GST_PAD_LINK_REFUSED;
  }
  st = ffmpegmux->context->streams[i];

  /* for the format-specific guesses, we'll go to
   * our famous codec mapper */
  if (gst_ffmpeg_caps_to_codecid (caps, &st->codec) != CODEC_ID_NONE) {
    ffmpegmux->eos[i] = FALSE;
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_ffmpegmux_loop (GstElement * element)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) element;
  gint i, bufnum;

  /* start by filling an internal queue of buffers */
  for (i = 0; i < ffmpegmux->context->nb_streams; i++) {
    GstPad *pad = ffmpegmux->sinkpads[i];

    /* check for "pull'ability" */
    while (pad != NULL &&
        GST_PAD_IS_USABLE (pad) &&
        ffmpegmux->eos[i] == FALSE && ffmpegmux->bufferqueue[i] == NULL) {
      GstData *data;

      /* we can pull a buffer! */
      data = gst_pad_pull (pad);
      if (GST_IS_EVENT (data)) {
        GstEvent *event = GST_EVENT (data);

        switch (GST_EVENT_TYPE (event)) {
          case GST_EVENT_EOS:
            /* flag EOS on this stream */
            ffmpegmux->eos[i] = TRUE;
            gst_event_unref (event);
            break;
          default:
            gst_pad_event_default (pad, event);
            break;
        }
      } else {
        ffmpegmux->bufferqueue[i] = GST_BUFFER (data);
      }
    }
  }

  /* open "file" (gstreamer protocol to next element) */
  if (!ffmpegmux->opened) {
    /* we do need all streams to have started capsnego,
     * or things will go horribly wrong */
    for (i = 0; i < ffmpegmux->context->nb_streams; i++) {
      AVStream *st = ffmpegmux->context->streams[i];

      /* check whether the pad has successfully completed capsnego */
      if (st->codec.codec_id == CODEC_ID_NONE) {
        GST_ELEMENT_ERROR (element, CORE, NEGOTIATION, (NULL),
            ("no caps set on stream %d (%s)", i,
                (st->codec.codec_type == CODEC_TYPE_VIDEO) ?
                "video" : "audio"));
        return;
      }
    }

    if (url_fopen (&ffmpegmux->context->pb,
            ffmpegmux->context->filename, URL_WRONLY) < 0) {
      GST_ELEMENT_ERROR (element, LIBRARY, TOO_LAZY, (NULL),
          ("Failed to open stream context in ffmux"));
      return;
    }

    if (av_set_parameters (ffmpegmux->context, NULL)) {
      GST_ELEMENT_ERROR (element, LIBRARY, INIT, (NULL),
          ("Failed to initialize muxer"));
      return;
    }

    /* we're now opened */
    ffmpegmux->opened = TRUE;

    /* now open the mux format */
    av_write_header (ffmpegmux->context);
  }

  /* take the one with earliest timestamp,
   * and push it forward */
  bufnum = -1;
  for (i = 0; i < ffmpegmux->context->nb_streams; i++) {
    /* if there's no buffer, just continue */
    if (ffmpegmux->bufferqueue[i] == NULL) {
      continue;
    }

    /* if we have no buffer yet, just use the first one */
    if (bufnum == -1) {
      bufnum = i;
      continue;
    }

    /* if we do have one, only use this one if it's older */
    if (GST_BUFFER_TIMESTAMP (ffmpegmux->bufferqueue[i]) <
        GST_BUFFER_TIMESTAMP (ffmpegmux->bufferqueue[bufnum])) {
      bufnum = i;
    }
  }

  /* now handle the buffer, or signal EOS if we have
   * no buffers left */
  if (bufnum >= 0) {
    GstBuffer *buf;
    AVPacket pkt;

    /* push out current buffer */
    buf = ffmpegmux->bufferqueue[bufnum];
    ffmpegmux->bufferqueue[bufnum] = NULL;

    ffmpegmux->context->streams[bufnum]->codec.frame_number++;

    /* set time */
    pkt.pts = GST_BUFFER_TIMESTAMP (buf) * AV_TIME_BASE / GST_SECOND;
    pkt.data = GST_BUFFER_DATA (buf);
    pkt.size = GST_BUFFER_SIZE (buf);
    pkt.stream_index = bufnum;
    pkt.flags = 0;
    if (GST_BUFFER_FLAGS (buf) & GST_BUFFER_KEY_UNIT)
       pkt.flags |= PKT_FLAG_KEY;
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      pkt.duration = GST_BUFFER_DURATION (buf) * AV_TIME_BASE / GST_SECOND;
    else
      pkt.duration = 0;
    av_write_frame (ffmpegmux->context, &pkt);
    gst_buffer_unref (buf);
  } else {
    /* close down */
    av_write_trailer (ffmpegmux->context);
    url_fclose (&ffmpegmux->context->pb);
    ffmpegmux->opened = FALSE;
    gst_element_set_eos (element);
  }
}

static GstElementStateReturn
gst_ffmpegmux_change_state (GstElement * element)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (element);
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      if (ffmpegmux->opened) {
        url_fclose (&ffmpegmux->context->pb);
        ffmpegmux->opened = FALSE;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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
  GType type;
  AVOutputFormat *in_plugin;
  GstFFMpegMuxClassParams *params;
  AVCodec *in_codec;

  in_plugin = first_oformat;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    gchar *p;
    GstCaps *srccaps, *audiosinkcaps, *videosinkcaps;

    /* Try to find the caps that belongs here */
    srccaps = gst_ffmpeg_formatid_to_caps (in_plugin->name);
    if (!srccaps) {
      goto next;
    }
    /* This is a bit ugly, but we just take all formats
     * for the pad template. We'll get an exact match
     * when we open the stream */
    audiosinkcaps = gst_caps_new_empty ();
    videosinkcaps = gst_caps_new_empty ();
    for (in_codec = first_avcodec; in_codec != NULL; in_codec = in_codec->next) {
      GstCaps *temp = gst_ffmpeg_codecid_to_caps (in_codec->id, NULL, TRUE);

      if (!temp) {
        continue;
      }
      switch (in_codec->type) {
        case CODEC_TYPE_VIDEO:
          gst_caps_append (videosinkcaps, temp);
          break;
        case CODEC_TYPE_AUDIO:
          gst_caps_append (audiosinkcaps, temp);
          break;
        default:
          gst_caps_free (temp);
          break;
      }
    }

    /* construct the type */
    type_name = g_strdup_printf ("ffmux_%s", in_plugin->name);

    p = type_name;

    while (*p) {
      if (*p == '.')
        *p = '_';
      p++;
    }

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      goto next;
    }

    /* create a cache for these properties */
    params = g_new0 (GstFFMpegMuxClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->videosinkcaps = videosinkcaps;
    params->audiosinkcaps = audiosinkcaps;

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    if (!gst_element_register (plugin, type_name, GST_RANK_NONE, type)) {
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
