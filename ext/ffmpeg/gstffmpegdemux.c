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

#include <string.h>
#include <libav/avformat.h>
#include <libav/avi.h>

#include <gst/gst.h>

extern URLProtocol gstreamer_protocol;

typedef enum {
  STATE_OPEN,
  STATE_STREAM_INFO,
  STATE_DEMUX,
  STATE_END,
} DemuxState;

typedef struct _GstFFMpegDemux GstFFMpegDemux;

struct _GstFFMpegDemux {
  GstElement 		element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad 		*sinkpad;

  AVFormatContext 	*context;
  DemuxState 		 state;

  GstPad		*srcpads[MAX_STREAMS];
};

typedef struct _GstFFMpegDemuxClass GstFFMpegDemuxClass;

struct _GstFFMpegDemuxClass {
  GstElementClass	 parent_class;

  AVInputFormat 	*in_plugin;
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

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdemux_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  NULL
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdemux_audio_src_factory,
  "audio_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  NULL
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdemux_video_src_factory,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  NULL
)

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_ffmpegdemux_class_init	(GstFFMpegDemuxClass *klass);
static void	gst_ffmpegdemux_init		(GstFFMpegDemux *ffmpegdemux);

static void	gst_ffmpegdemux_loop		(GstElement *element);

static void	gst_ffmpegdemux_set_property	(GObject *object, guint prop_id, const GValue *value, 
						 GParamSpec *pspec);
static void	gst_ffmpegdemux_get_property	(GObject *object, guint prop_id, GValue *value, 
						 GParamSpec *pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdemux_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdemux_class_init (GstFFMpegDemuxClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  klass->in_plugin = g_hash_table_lookup (global_plugins,
		  GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  gobject_class->set_property = gst_ffmpegdemux_set_property;
  gobject_class->get_property = gst_ffmpegdemux_get_property;
}

static void
gst_ffmpegdemux_init(GstFFMpegDemux *ffmpegdemux)
{
  //GstFFMpegDemuxClass *oclass = (GstFFMpegDemuxClass*)(G_OBJECT_GET_CLASS (ffmpegdemux));

  ffmpegdemux->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_sink_factory), "sink");

  gst_element_add_pad (GST_ELEMENT (ffmpegdemux), ffmpegdemux->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (ffmpegdemux), gst_ffmpegdemux_loop);

  ffmpegdemux->state = STATE_OPEN;
}

static void
gst_ffmpegdemux_loop (GstElement *element)
{
  GstFFMpegDemux *ffmpegdemux = (GstFFMpegDemux *)(element);
  GstFFMpegDemuxClass *oclass = (GstFFMpegDemuxClass*)(G_OBJECT_GET_CLASS (ffmpegdemux));
  gint res = 0;

  switch (ffmpegdemux->state) {
    case STATE_OPEN:
    {
       res = av_open_input_file (&ffmpegdemux->context, 
		            g_strdup_printf ("gstreamer://%p", ffmpegdemux->sinkpad),
		            oclass->in_plugin,
		            0,
		            NULL);

      /* this doesn't work */
      av_set_pts_info (ffmpegdemux->context, 33, 1, 100000);

      ffmpegdemux->state = STATE_DEMUX;
      break;
    }
    case STATE_DEMUX:
    {
      gint res;
      AVPacket pkt;
      AVFormatContext *ct = ffmpegdemux->context;
      AVStream *st;
      GstPad *pad;
      
      res = av_read_packet(ct, &pkt);
      if (res < 0) {
	gint i;

	for (i = 0; i < ct->nb_streams; i++) {
          GstPad *pad;

	  pad = ffmpegdemux->srcpads[i];

	  if (GST_PAD_IS_USABLE (pad)) {
	    gst_pad_push (pad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
	  }
	}
	gst_element_set_eos (element);
	return;
      }

      st = ct->streams[pkt.stream_index];

      if (st->codec_info_state == 0) {
	gchar *padname = NULL;
	GstPadTemplate *templ = NULL;
	
        st->codec_info_state = 1;

	if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
	  padname = g_strdup_printf ("video_%02d", pkt.stream_index);
	  templ = GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_video_src_factory);
	}
	else if (st->codec.codec_type == CODEC_TYPE_AUDIO) {
	  padname = g_strdup_printf ("audio_%02d", pkt.stream_index);
	  templ = GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_audio_src_factory);
	}

	if (padname != NULL) {
          pad = gst_pad_new_from_template (templ, padname);

	  ffmpegdemux->srcpads[pkt.stream_index] = pad;
          gst_element_add_pad (GST_ELEMENT (ffmpegdemux), pad);
	  g_print ("new pad\n");
	}
	else {
          g_warning ("unkown pad type %d", st->codec.codec_type);
	  return;
	}
      }
      else {
	pad = ffmpegdemux->srcpads[pkt.stream_index];
      }

      if (GST_PAD_IS_USABLE (pad)) {
        GstBuffer *outbuf;

        outbuf = gst_buffer_new ();
        GST_BUFFER_DATA (outbuf) = pkt.data;
        GST_BUFFER_SIZE (outbuf) = pkt.size;
	if (pkt.pts != 0) {
          GST_BUFFER_TIMESTAMP (outbuf) = pkt.pts * GST_SECOND / 90000LL;
	}
	else {
          GST_BUFFER_TIMESTAMP (outbuf) = -1;
	}

        gst_pad_push (pad, outbuf);
      }
      break;
    }
    default:
      gst_element_set_eos (element);
      break;
  }
}

static void
gst_ffmpegdemux_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFFMpegDemux *ffmpegdemux;

  /* Get a pointer of the right type. */
  ffmpegdemux = (GstFFMpegDemux *)(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegdemux_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFFMpegDemux *ffmpegdemux;

  /* It's not null if we got it, but it might not be ours */
  ffmpegdemux = (GstFFMpegDemux *)(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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
  
  in_plugin = first_iformat;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    gchar *p;

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

    /* create the gtk type now */
    type = g_type_register_static(GST_TYPE_ELEMENT, type_name , &typeinfo, 0);

    /* construct the element details struct */
    details = g_new0 (GstElementDetails,1);
    details->longname = g_strdup (in_plugin->name);
    details->klass = "Codec/Demuxer/FFMpeg";
    details->license = "LGPL";
    details->description = g_strdup (in_plugin->name);
    details->version = g_strdup("1.0.0");
    details->author = g_strdup("The FFMPEG crew, GStreamer plugin by Wim Taymans <wim.taymans@chello.be>");
    details->copyright = g_strdup("(c) 2002");

    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) in_plugin);

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_val_if_fail(factory != NULL, FALSE);

    gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_NONE);

    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_sink_factory));

    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_video_src_factory));
    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdemux_audio_src_factory));

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  register_protocol (&gstreamer_protocol);

  return TRUE;
}
