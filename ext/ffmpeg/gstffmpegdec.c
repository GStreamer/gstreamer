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

#include "gstffmpegdec.h"

#include <string.h>

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdec_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegdec_sink",
    "video/avi",
      "format",		GST_PROPS_STRING ("strf_vids")
  ),
  GST_CAPS_NEW (
    "ffmpegdec_sink",
    "video/mpeg",
    NULL
  )
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdec_audio_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegdec_src",
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
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegdec_video_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegdec_src",
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
static void	gst_ffmpegdec_class_init	(GstFFMpegDecClass *klass);
static void	gst_ffmpegdec_init		(GstFFMpegDec *ffmpegdec);

static void	gst_ffmpegdec_chain_audio	(GstPad *pad, GstBuffer *buffer);
static void	gst_ffmpegdec_chain_video	(GstPad *pad, GstBuffer *buffer);

static void	gst_ffmpegdec_set_property	(GObject *object, guint prop_id, const GValue *value, 
						 GParamSpec *pspec);
static void	gst_ffmpegdec_get_property	(GObject *object, guint prop_id, GValue *value, 
						 GParamSpec *pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdec_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdec_class_init (GstFFMpegDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  klass->in_plugin = g_hash_table_lookup (global_plugins,
		  GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  gobject_class->set_property = gst_ffmpegdec_set_property;
  gobject_class->get_property = gst_ffmpegdec_get_property;
}

static GstPadConnectReturn
gst_ffmpegdec_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  if (gst_caps_has_property_typed (caps, "width", GST_PROPS_INT_TYPE))
    gst_caps_get_int (caps, "width", &ffmpegdec->context->width);
  if (gst_caps_has_property_typed (caps, "height", GST_PROPS_INT_TYPE))
    gst_caps_get_int (caps, "height", &ffmpegdec->context->height);

  ffmpegdec->context->pix_fmt = PIX_FMT_YUV420P;
  ffmpegdec->context->frame_rate = 23 * FRAME_RATE_BASE;
  ffmpegdec->context->bit_rate = 0;

  /* FIXME bug in ffmpeg */
  if (avcodec_open (ffmpegdec->context, avcodec_find_encoder(CODEC_ID_MPEG1VIDEO)) <0 ) {
    g_warning ("ffmpegdec: could not open codec");
    return GST_PAD_CONNECT_REFUSED;
  }

  if (avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0) {
    g_warning ("ffmpegdec: could not open codec");
    return GST_PAD_CONNECT_REFUSED;
  }
  return GST_PAD_CONNECT_OK;
}

static void
gst_ffmpegdec_init(GstFFMpegDec *ffmpegdec)
{
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));

  ffmpegdec->context = g_malloc0 (sizeof (AVCodecContext));

  ffmpegdec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegdec_sink_factory), "sink");
  gst_pad_set_connect_function (ffmpegdec->sinkpad, gst_ffmpegdec_sinkconnect);

  if (oclass->in_plugin->type == CODEC_TYPE_VIDEO) {
    ffmpegdec->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegdec_video_src_factory), "src");
    gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain_video);
  }
  else if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    ffmpegdec->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegdec_audio_src_factory), "src");
    gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain_audio);
  }

  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  ffmpegdec->picture = g_malloc0 (sizeof (AVPicture));
}

static void
gst_ffmpegdec_chain_audio (GstPad *pad, GstBuffer *inbuf)
{
  /*GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad)); */
  gpointer data;
  gint size;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  GST_DEBUG (0, "got buffer %p %d", data, size);

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegdec_chain_video (GstPad *pad, GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  guchar *data;
  gint size, frame_size, len;
  gint have_picture;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  do {
    ffmpegdec->context->frame_number++;

    len = avcodec_decode_video (ffmpegdec->context, ffmpegdec->picture,
		  &have_picture, data, size);

    if (len < 0) {
      g_warning ("ffmpegdec: decoding error");
      break;
    }

    if (have_picture) {
      guchar *picdata, *picdata2, *outdata, *outdata2;
      gint xsize, i, width, height;

      width = ffmpegdec->context->width;
      height = ffmpegdec->context->height;

      if (!GST_PAD_CAPS (ffmpegdec->srcpad)) {
        gst_pad_try_set_caps (ffmpegdec->srcpad, 
		      GST_CAPS_NEW (
			"ffmpegdec_src",
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
 
      picdata = ffmpegdec->picture->data[0];
      xsize = ffmpegdec->picture->linesize[0];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        outdata += width;
        picdata += xsize;
      }

      frame_size >>= 2;
      width >>= 1;
      height >>= 1;
      outdata2 = outdata + frame_size;

      picdata = ffmpegdec->picture->data[1];
      picdata2 = ffmpegdec->picture->data[2];
      xsize = ffmpegdec->picture->linesize[1];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        memcpy (outdata2, picdata2, width);
        outdata += width; outdata2 += width;
        picdata += xsize; picdata2 += xsize;
      }

      gst_pad_push (ffmpegdec->srcpad, outbuf);
    } 

    size -= len;
    data += len;
  }
  while (size > 0);

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFFMpegDec *ffmpegdec;

  /* Get a pointer of the right type. */
  ffmpegdec = (GstFFMpegDec *)(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFFMpegDec *ffmpegdec;

  /* It's not null if we got it, but it might not be ours */
  ffmpegdec = (GstFFMpegDec *)(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegdec_register (GstPlugin *plugin)
{
  GstElementFactory *factory;
  GTypeInfo typeinfo = {
    sizeof(GstFFMpegDecClass),      
    NULL,
    NULL,
    (GClassInitFunc)gst_ffmpegdec_class_init,
    NULL,
    NULL,
    sizeof(GstFFMpegDec),
    0,
    (GInstanceInitFunc)gst_ffmpegdec_init,
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
    type_name = g_strdup_printf("ffmpeg%s_%s", codec_type, in_plugin->name);

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

    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdec_sink_factory));

    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdec_video_src_factory));
    }
    else if (in_plugin->type == CODEC_TYPE_AUDIO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegdec_audio_src_factory));
    }

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  return TRUE;
}
