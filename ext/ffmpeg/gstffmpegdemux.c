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

#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegDemux GstFFMpegDemux;

struct _GstFFMpegDemux {
  GstElement 		element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad 		*sinkpad;

  AVFormatContext 	*context;
  gboolean		opened;

  GstPad		*srcpads[MAX_STREAMS];
  gint			videopads, audiopads;
};

typedef struct _GstFFMpegDemuxClassParams {
  AVInputFormat 	*in_plugin;
  GstPadTemplate	*sinktempl;
  GstPadTemplate	*videosrctempl;
  GstPadTemplate	*audiosrctempl;
} GstFFMpegDemuxClassParams;

typedef struct _GstFFMpegDemuxClass GstFFMpegDemuxClass;

struct _GstFFMpegDemuxClass {
  GstElementClass	 parent_class;

  AVInputFormat 	*in_plugin;
  GstPadTemplate	*sinktempl;
  GstPadTemplate	*videosrctempl;
  GstPadTemplate	*audiosrctempl;
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

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_ffmpegdemux_class_init	(GstFFMpegDemuxClass *klass);
static void	gst_ffmpegdemux_init		(GstFFMpegDemux *ffmpegdemux);
static void	gst_ffmpegdemux_dispose		(GObject *object);

static void	gst_ffmpegdemux_loop		(GstElement *element);

static GstElementStateReturn
		gst_ffmpegdemux_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdemux_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdemux_class_init (GstFFMpegDemuxClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstFFMpegDemuxClassParams *params;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  params = g_hash_table_lookup (global_plugins,
		GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  klass->in_plugin = params->in_plugin;
  klass->videosrctempl = params->videosrctempl;
  klass->audiosrctempl = params->audiosrctempl;
  klass->sinktempl = params->sinktempl;

  gstelement_class->change_state = gst_ffmpegdemux_change_state;
  gobject_class->dispose = gst_ffmpegdemux_dispose;
}

static void
gst_ffmpegdemux_init(GstFFMpegDemux *ffmpegdemux)
{
  GstFFMpegDemuxClass *oclass = (GstFFMpegDemuxClass*)(G_OBJECT_GET_CLASS (ffmpegdemux));

  ffmpegdemux->sinkpad = gst_pad_new_from_template (oclass->sinktempl,
						    "sink");
  gst_element_add_pad (GST_ELEMENT (ffmpegdemux),
		       ffmpegdemux->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (ffmpegdemux),
				 gst_ffmpegdemux_loop);

  ffmpegdemux->opened = FALSE;

  ffmpegdemux->videopads = 0;
  ffmpegdemux->audiopads = 0;
}

static void
gst_ffmpegdemux_dispose (GObject *object)
{
  GstFFMpegDemux *ffmpegdemux = (GstFFMpegDemux *) object;

  if (ffmpegdemux->opened) {
    av_close_input_file (ffmpegdemux->context);
    ffmpegdemux->opened = FALSE;
  }
}

#define GST_FFMPEG_TYPE_FIND_SIZE 4096
static void
gst_ffmpegdemux_type_find (GstTypeFind *tf, gpointer priv)
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
    res = res * GST_TYPE_FIND_MAXIMUM / AVPROBE_SCORE_MAX;
    if (res > 0) {
      GstCaps *caps = GST_PAD_TEMPLATE_CAPS (params->sinktempl);
      /* make sure we still hold a refcount to this caps */
      gst_caps_ref (caps);
      gst_type_find_suggest (tf, res, caps);
    }
  }
}

static void
gst_ffmpegdemux_loop (GstElement *element)
{
  GstFFMpegDemux *ffmpegdemux = (GstFFMpegDemux *)(element);
  GstFFMpegDemuxClass *oclass = (GstFFMpegDemuxClass*)(G_OBJECT_GET_CLASS (ffmpegdemux));

  gint res;
  AVPacket pkt;
  AVFormatContext *ct;
  AVStream *st;
  GstPad *pad;

  /* open file if we didn't so already */
  if (!ffmpegdemux->opened) {
    res = av_open_input_file (&ffmpegdemux->context, 
			      g_strdup_printf ("gstreamer://%p",
					       ffmpegdemux->sinkpad),
			      oclass->in_plugin, 0, NULL);
    if (res < 0) {
      gst_element_error (GST_ELEMENT (ffmpegdemux),
			 "Failed to open demuxer/file context");
      return;
    }

    ffmpegdemux->opened = TRUE;
  }

  /* shortcut to context */
  ct = ffmpegdemux->context;

  /* read a package */
  res = av_read_packet (ct, &pkt);
  if (res < 0) {
    if (url_feof (&ct->pb)) {
      int i;

      /* we're at the end of file - send an EOS to
       * each stream that we opened so far */
      for (i = 0; i < ct->nb_streams; i++) {
        GstPad *pad;
        GstEvent *event = gst_event_new (GST_EVENT_EOS);

        pad = ffmpegdemux->srcpads[i];
        if (GST_PAD_IS_USABLE (pad)) {
          gst_data_ref (GST_DATA (event));
          gst_pad_push (pad, GST_DATA (event));
        }
        gst_data_unref (GST_DATA (event));
      }
      gst_element_set_eos (element);

      /* FIXME: should we go into
       * should we close the context here?
       * either way, a new media stream needs an
       * event too */
    }
    return;
  }

  /* shortcut to stream */
  st = ct->streams[pkt.stream_index];

  /* create the pad/stream if we didn't do so already */
  if (st->codec_info_state == 0) {
    GstPadTemplate *templ = NULL;
    GstCaps *caps;
    gchar *padname;
    gint num;

    /* mark as handled */	
    st->codec_info_state = 1;

    /* find template */
    switch (st->codec.codec_type) {
      case CODEC_TYPE_VIDEO:
        templ = oclass->videosrctempl;
        num = ffmpegdemux->videopads++;
        break;
      case CODEC_TYPE_AUDIO:
        templ = oclass->audiosrctempl;
        num = ffmpegdemux->audiopads++;
        break;
      default:
        g_warning ("Unknown pad type %d",
		   st->codec.codec_type);
        return;
    }

    /* create new pad for this stream */
    padname = g_strdup_printf (GST_PAD_TEMPLATE_NAME_TEMPLATE(templ),
			       num);
    pad = gst_pad_new_from_template (templ, padname);
    g_free (padname);

    /* FIXME: convert() and query() functions for pad */

    /* store pad internally */
    ffmpegdemux->srcpads[pkt.stream_index] = pad;
    gst_element_add_pad (GST_ELEMENT (ffmpegdemux), pad);

    /* get caps that belongs to this stream */
    caps = gst_ffmpeg_codecid_to_caps (st->codec.codec_id,
				       &st->codec);
    if (gst_pad_try_set_caps (pad, caps) <= 0) {
      GST_DEBUG (
		 "Failed to set caps from ffdemuxer on next element");
      /* we continue here, in the next pad-is-usable check,
       * we'll return nonetheless */
    }
  }

  /* shortcut to pad belonging to this stream */
  pad = ffmpegdemux->srcpads[pkt.stream_index];

  /* and handle the data by pushing it forward... */
  if (GST_PAD_IS_USABLE (pad)) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (pkt.size);
    memcpy (GST_BUFFER_DATA (outbuf), pkt.data, pkt.size);
    GST_BUFFER_SIZE (outbuf) = pkt.size;

    if (pkt.pts != AV_NOPTS_VALUE && ct->pts_den) {
      GST_BUFFER_TIMESTAMP (outbuf) = pkt.pts * GST_SECOND *
					ct->pts_num / ct->pts_den;
    }

    if (pkt.flags & PKT_FLAG_KEY) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
    }

    gst_pad_push (pad, GST_DATA (outbuf));
    pkt.destruct (&pkt);
  }
}

static GstElementStateReturn
gst_ffmpegdemux_change_state (GstElement *element)
{
  GstFFMpegDemux *ffmpegdemux = (GstFFMpegDemux *)(element);
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      if (ffmpegdemux->opened) {
        av_close_input_file (ffmpegdemux->context);
        ffmpegdemux->opened = FALSE;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_ffmpegdemux_register (GstPlugin *plugin)
{
  GstElementFactory *factory;
  GTypeInfo typeinfo = {
    sizeof(GstFFMpegDemuxClass),      
    NULL,
    NULL,
    (GClassInitFunc)gst_ffmpegdemux_class_init,
    NULL,
    NULL,
    sizeof(GstFFMpegDemux),
    0,
    (GInstanceInitFunc)gst_ffmpegdemux_init,
  };
  GType type;
  GstElementDetails *details;
  AVInputFormat *in_plugin;
  GstFFMpegDemuxClassParams *params;
  AVCodec *in_codec;
  gchar **extensions;
  
  in_plugin = first_iformat;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    gchar *p;
    GstCaps *sinkcaps, *audiosrccaps, *videosrccaps;

    /* Try to find the caps that belongs here */
    sinkcaps = gst_ffmpeg_formatid_to_caps (in_plugin->name);
    if (!sinkcaps) {
      goto next;
    }
    /* This is a bit ugly, but we just take all formats
     * for the pad template. We'll get an exact match
     * when we open the stream */
    audiosrccaps = NULL;
    videosrccaps = NULL;
    for (in_codec = first_avcodec; in_codec != NULL;
	 in_codec = in_codec->next) {
      GstCaps *temp = gst_ffmpeg_codecid_to_caps (in_codec->id, NULL);
      if (!temp) {
        continue;
      }
      switch (in_codec->type) {
        case CODEC_TYPE_VIDEO:
          videosrccaps = gst_caps_append (videosrccaps, temp);
          break;
        case CODEC_TYPE_AUDIO:
          audiosrccaps = gst_caps_append (audiosrccaps, temp);
          break;
        default:
          gst_caps_unref (temp);
          break;
      }
    }

    /* construct the type */
    type_name = g_strdup_printf("ffdemux_%s", in_plugin->name);

    p = type_name;

    while (*p) {
      if (*p == '.') *p = '_';
      p++;
    }

    /* if it's already registered, drop it */
    if (g_type_from_name(type_name)) {
      g_free(type_name);
      goto next;
    }

    /* create the type now */
    type = g_type_register_static(GST_TYPE_ELEMENT, type_name , &typeinfo, 0);

    /* construct the element details struct */
    details = g_new0 (GstElementDetails, 1);
    details->longname = g_strdup (in_plugin->long_name);
    details->klass = g_strdup ("Codec/Demuxer");
    details->license = g_strdup ("LGPL");
    details->description = g_strdup_printf ("FFMPEG %s demuxer",
					    in_plugin->name);
    details->version = g_strdup (VERSION);
    details->author = g_strdup ("The FFMPEG crew, "
				"Wim Taymans <wim.taymans@chello.be>, "
				"Ronald Bultje <rbultje@ronald.bitfreak.net>");
    details->copyright = g_strdup ("(c) 2002-2003");

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_val_if_fail(factory != NULL, FALSE);

    /* create a cache for these properties */
    params = g_new0 (GstFFMpegDemuxClassParams, 1);
    params->in_plugin = in_plugin;
    params->sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
					      GST_PAD_ALWAYS,
					      sinkcaps, NULL);
    gst_element_factory_add_pad_template (factory,
					  params->sinktempl);
    params->audiosrctempl = gst_pad_template_new ("audio_%02d",
						  GST_PAD_SRC,
						  GST_PAD_SOMETIMES,
						  audiosrccaps, NULL);
    gst_element_factory_add_pad_template (factory,
					  params->audiosrctempl);
    params->videosrctempl = gst_pad_template_new ("video_%02d",
						  GST_PAD_SRC,
						  GST_PAD_SOMETIMES,
						  videosrccaps, NULL);
    gst_element_factory_add_pad_template (factory,
					  params->videosrctempl);

    /* typefind registering */
    extensions = g_strsplit (in_plugin->extensions, " ", 0);
    gst_type_find_factory_register (plugin, g_strdup_printf ("fftype_%s", in_plugin->name),
	    GST_ELEMENT_RANK_MARGINAL, gst_ffmpegdemux_type_find, 
	    extensions, GST_CAPS_ANY, params);
    g_strfreev (extensions);

    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) params);

    gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_MARGINAL);

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  return TRUE;
}
