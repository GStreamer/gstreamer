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
#include "config.h"
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>

typedef struct _GstFFMpegMux GstFFMpegMux;

struct _GstFFMpegMux {
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  AVCodecContext *context;
  AVFrame *picture;
};

typedef struct _GstFFMpegMuxClass GstFFMpegMuxClass;

struct _GstFFMpegMuxClass {
  GstElementClass parent_class;

  AVCodec *in_plugin;
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

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegmux_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegmux_sink",
    "video/avi",
      "format",		GST_PROPS_STRING ("strf_vids")
  ),
  GST_CAPS_NEW (
    "ffmpegmux_sink",
    "video/mpeg",
    NULL
  )
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegmux_audio_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegmux_src",
    "audio/raw",
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "signed",     GST_PROPS_BOOLEAN (TRUE),
        "width",      GST_PROPS_INT (16),
	"depth",      GST_PROPS_INT (16),
        "rate",       GST_PROPS_INT_RANGE (8000, 96000),
        "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegmux_video_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegmux_src",
    "video/raw",
      "format",       GST_PROPS_LIST (
	                GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
		      ),
        "width",      GST_PROPS_INT_RANGE (16, 4096),
        "height",     GST_PROPS_INT_RANGE (16, 4096)
  )
)

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_ffmpegmux_class_init	(GstFFMpegMuxClass *klass);
static void	gst_ffmpegmux_init		(GstFFMpegMux *ffmpegmux);

static void	gst_ffmpegmux_chain_audio	(GstPad *pad, GstBuffer *buffer);
static void	gst_ffmpegmux_chain_video	(GstPad *pad, GstBuffer *buffer);

static void	gst_ffmpegmux_set_property	(GObject *object, guint prop_id, const GValue *value, 
						 GParamSpec *pspec);
static void	gst_ffmpegmux_get_property	(GObject *object, guint prop_id, GValue *value, 
						 GParamSpec *pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegmux_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegmux_class_init (GstFFMpegMuxClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  klass->in_plugin = g_hash_table_lookup (global_plugins,
		  GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  gobject_class->set_property = gst_ffmpegmux_set_property;
  gobject_class->get_property = gst_ffmpegmux_get_property;
}

static GstPadConnectReturn
gst_ffmpegmux_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *)(gst_pad_get_parent (pad));
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass*)(G_OBJECT_GET_CLASS (ffmpegmux));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  if (gst_caps_has_property_typed (caps, "width", GST_PROPS_INT_TYPE))
    gst_caps_get_int (caps, "width", &ffmpegmux->context->width);
  if (gst_caps_has_property_typed (caps, "height", GST_PROPS_INT_TYPE))
    gst_caps_get_int (caps, "height", &ffmpegmux->context->height);

  ffmpegmux->context->pix_fmt = PIX_FMT_YUV420P;
  ffmpegmux->context->frame_rate = 23 * FRAME_RATE_BASE;
  ffmpegmux->context->bit_rate = 0;

  /* FIXME bug in ffmpeg */
  if (avcodec_open (ffmpegmux->context, avcodec_find_encoder(CODEC_ID_MPEG1VIDEO)) <0 ) {
    g_warning ("ffmpegmux: could not open codec");
    return GST_PAD_CONNECT_REFUSED;
  }

  if (avcodec_open (ffmpegmux->context, oclass->in_plugin) < 0) {
    g_warning ("ffmpegmux: could not open codec");
    return GST_PAD_CONNECT_REFUSED;
  }
  return GST_PAD_CONNECT_OK;
}

static void
gst_ffmpegmux_init(GstFFMpegMux *ffmpegmux)
{
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass*)(G_OBJECT_GET_CLASS (ffmpegmux));

  ffmpegmux->context = g_malloc0 (sizeof (AVCodecContext));

  ffmpegmux->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegmux_sink_factory), "sink");
  gst_pad_set_connect_function (ffmpegmux->sinkpad, gst_ffmpegmux_sinkconnect);

  if (oclass->in_plugin->type == CODEC_TYPE_VIDEO) {
    ffmpegmux->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegmux_video_src_factory), "src");
    gst_pad_set_chain_function (ffmpegmux->sinkpad, gst_ffmpegmux_chain_video);
  }
  else if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    ffmpegmux->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegmux_audio_src_factory), "src");
    gst_pad_set_chain_function (ffmpegmux->sinkpad, gst_ffmpegmux_chain_audio);
  }

  gst_element_add_pad (GST_ELEMENT (ffmpegmux), ffmpegmux->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegmux), ffmpegmux->srcpad);

  ffmpegmux->picture = g_malloc0 (sizeof (AVFrame));
}

static void
gst_ffmpegmux_chain_audio (GstPad *pad, GstBuffer *inbuf)
{
  /*GstFFMpegMux *ffmpegmux = (GstFFMpegMux *)(gst_pad_get_parent (pad)); */
  gpointer data;
  gint size;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  GST_DEBUG (0, "got buffer %p %d", data, size);

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegmux_chain_video (GstPad *pad, GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *)(gst_pad_get_parent (pad));
  guchar *data;
  gint size, frame_size, len;
  gint have_picture;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  do {
    ffmpegmux->context->frame_number++;

    len = avcodec_decode_video (ffmpegmux->context, ffmpegmux->picture,
		  &have_picture, data, size);

    if (len < 0) {
      g_warning ("ffmpegmux: decoding error");
      break;
    }

    if (have_picture) {
      guchar *picdata, *picdata2, *outdata, *outdata2;
      gint xsize, i, width, height;

      width = ffmpegmux->context->width;
      height = ffmpegmux->context->height;

      if (!GST_PAD_CAPS (ffmpegmux->srcpad)) {
        gst_pad_try_set_caps (ffmpegmux->srcpad, 
		      GST_CAPS_NEW (
			"ffmpegmux_src",
			"video/raw",
			  "format",	GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
			    "width",	GST_PROPS_INT (width),
			    "height",	GST_PROPS_INT (height)
		      ));
      }

      frame_size = width * height;

      outbuf = gst_buffer_new ();
      GST_BUFFER_SIZE (outbuf) = (frame_size*3)>>1;
      outdata = GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
 
      picdata = ffmpegmux->picture->data[0];
      xsize = ffmpegmux->picture->linesize[0];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        outdata += width;
        picdata += xsize;
      }

      frame_size >>= 2;
      width >>= 1;
      height >>= 1;
      outdata2 = outdata + frame_size;

      picdata = ffmpegmux->picture->data[1];
      picdata2 = ffmpegmux->picture->data[2];
      xsize = ffmpegmux->picture->linesize[1];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        memcpy (outdata2, picdata2, width);
        outdata += width; outdata2 += width;
        picdata += xsize; picdata2 += xsize;
      }

      gst_pad_push (ffmpegmux->srcpad, outbuf);
    } 

    size -= len;
    data += len;
  }
  while (size > 0);

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegmux_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFFMpegMux *ffmpegmux;

  /* Get a pointer of the right type. */
  ffmpegmux = (GstFFMpegMux *)(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegmux_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFFMpegMux *ffmpegmux;

  /* It's not null if we got it, but it might not be ours */
  ffmpegmux = (GstFFMpegMux *)(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegmux_register (GstPlugin *plugin)
{
  GstElementFactory *factory;
  GTypeInfo typeinfo = {
    sizeof(GstFFMpegMuxClass),      
    NULL,
    NULL,
    (GClassInitFunc)gst_ffmpegmux_class_init,
    NULL,
    NULL,
    sizeof(GstFFMpegMux),
    0,
    (GInstanceInitFunc)gst_ffmpegmux_init,
  };
  GType type;
  GstElementDetails *details;
  AVCodec *in_plugin;
  
  in_plugin = first_avcodec;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    gchar *codec_type;

    if (in_plugin->decode) {
      codec_type = "dec";
    }
    else {
      goto next;
    }
    /* construct the type */
    type_name = g_strdup_printf("ff%s_%s", codec_type, in_plugin->name);

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
    details->klass = "Codec/FFMpeg";
    details->license = "LGPL";
    details->description = g_strdup (in_plugin->name);
    details->version = g_strdup("1.0.0");
    details->author = g_strdup("The FFMPEG crew, GStreamer plugin by Wim Taymans <wim.taymans@chello.be>");
    details->copyright = g_strdup("(c) 2001");

    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) in_plugin);

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_val_if_fail(factory != NULL, FALSE);

    gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_NONE);

    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegmux_sink_factory));

    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegmux_video_src_factory));
    }
    else if (in_plugin->type == CODEC_TYPE_AUDIO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegmux_audio_src_factory));
    }

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  return TRUE;
}
