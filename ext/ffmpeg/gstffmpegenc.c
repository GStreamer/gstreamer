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

#include "gstffmpegenc.h"

#include <string.h>

#define VIDEO_BUFFER_SIZE (1024*1024)

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_BIT_RATE,
  ARG_FRAME_RATE,
  ARG_SAMPLE_RATE,
  ARG_GOP_SIZE,
  ARG_HQ,
  ARG_ME_METHOD,
  /* FILL ME */
};

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegenc_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegenc_src",
    "unknown/unknown",
    NULL
  )
)

/* This factory is much simpler, and defines the source pad. */
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegenc_audio_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegenc_sink",
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
GST_PAD_TEMPLATE_FACTORY (gst_ffmpegenc_video_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "ffmpegenc_sink",
    "video/raw",
      "format",       GST_PROPS_LIST (
	                GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
	                GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2"))
		      ),
        "width",      GST_PROPS_INT_RANGE (16, 4096),
        "height",     GST_PROPS_INT_RANGE (16, 4096)
  )
)

#define GST_TYPE_ME_METHOD (gst_ffmpegenc_me_method_get_type())
static GType
gst_ffmpegenc_me_method_get_type (void)
{
  static GType ffmpegenc_me_method_type = 0;
  static GEnumValue ffmpegenc_me_methods[] = {
    { ME_ZERO,  "0", "zero" },
    { ME_FULL,  "1", "full" },
    { ME_LOG,   "2", "logarithmic" },
    { ME_PHODS, "3", "phods" },
    { ME_EPZS,  "4", "epzs" },
    { ME_X1   , "5", "x1" },
    { 0, NULL, NULL },
  };
  if (!ffmpegenc_me_method_type) {
    ffmpegenc_me_method_type = g_enum_register_static ("GstFFMpegEncMeMethod", ffmpegenc_me_methods);
  }
  return ffmpegenc_me_method_type;
}

static GHashTable *enc_global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_ffmpegenc_class_init	(GstFFMpegEncClass *klass);
static void	gst_ffmpegenc_init		(GstFFMpegEnc *ffmpegenc);

static void	gst_ffmpegenc_chain_audio	(GstPad *pad, GstBuffer *buffer);
static void	gst_ffmpegenc_chain_video	(GstPad *pad, GstBuffer *buffer);

static void	gst_ffmpegenc_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_ffmpegenc_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegenc_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegenc_class_init (GstFFMpegEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  klass->in_plugin = g_hash_table_lookup (enc_global_plugins,
		  GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  if (klass->in_plugin->type == CODEC_TYPE_VIDEO) {
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width","width","width",
                      0, G_MAXINT, 0, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_HEIGHT,
      g_param_spec_int ("height","height","height",
                      0, G_MAXINT, 0, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_int ("bit_rate","bit_rate","bit_rate",
                      0, G_MAXINT, 300000, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_FRAME_RATE,
      g_param_spec_int ("frame_rate","frame_rate","frame_rate",
                      0, G_MAXINT, 25, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_GOP_SIZE,
      g_param_spec_int ("gop_size","gop_size","gop_size",
                      0, G_MAXINT, 15, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_HQ,
      g_param_spec_boolean ("hq","hq","hq",
                      FALSE, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_ME_METHOD,
      g_param_spec_enum ("me_method","me_method","me_method",
                      GST_TYPE_ME_METHOD, ME_LOG, G_PARAM_READWRITE)); 
  }
  else if (klass->in_plugin->type == CODEC_TYPE_AUDIO) {
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_int ("bit_rate","bit_rate","bit_rate",
                      0, G_MAXINT, 128000, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_SAMPLE_RATE,
      g_param_spec_int ("sample_rate","rate","rate",
                      0, G_MAXINT, -1, G_PARAM_READWRITE)); 
    g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_HQ,
      g_param_spec_boolean ("hq","hq","hq",
                      FALSE, G_PARAM_READWRITE)); 
  }

  gobject_class->set_property = gst_ffmpegenc_set_property;
  gobject_class->get_property = gst_ffmpegenc_get_property;
}

static GstPadConnectReturn
gst_ffmpegenc_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) gst_pad_get_parent (pad);
  GstFFMpegEncClass *oclass = (GstFFMpegEncClass*)(G_OBJECT_GET_CLASS(ffmpegenc));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  if (strstr (gst_caps_get_mime (caps), "audio/raw")) {
    gst_caps_get_int (caps, "rate", &ffmpegenc->context->sample_rate);
    gst_caps_get_int (caps, "channels", &ffmpegenc->context->channels);
  }
  else if (strstr (gst_caps_get_mime (caps), "video/raw")) {
    guint32 fourcc;

    gst_caps_get_int (caps, "width", &ffmpegenc->in_width);
    gst_caps_get_int (caps, "height", &ffmpegenc->in_height);
    
    if (ffmpegenc->need_resample) {
      ffmpegenc->context->width = ffmpegenc->out_width;
      ffmpegenc->context->height = ffmpegenc->out_height;
    }
    else {
      ffmpegenc->context->width = ffmpegenc->in_width;
      ffmpegenc->context->height = ffmpegenc->in_height;
    }
    gst_caps_get_fourcc_int (caps, "format", &fourcc);
    if (fourcc == GST_STR_FOURCC ("I420")) {
      ffmpegenc->context->pix_fmt = PIX_FMT_YUV420P;
    }
    else {
      ffmpegenc->context->pix_fmt = PIX_FMT_YUV422;
    }

    ffmpegenc->resample = img_resample_init (ffmpegenc->context->width, ffmpegenc->context->height, 
		    	ffmpegenc->in_width, ffmpegenc->in_height);
  }
  else {
    g_warning ("ffmpegenc: invalid caps %s\n", gst_caps_get_mime (caps));
    return GST_PAD_CONNECT_REFUSED;
  }

  if (avcodec_open (ffmpegenc->context, oclass->in_plugin) < 0) {
    g_warning ("ffmpegenc: could not open codec\n");
    return GST_PAD_CONNECT_REFUSED;
  }

  if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    ffmpegenc->buffer = g_malloc (ffmpegenc->context->frame_size * 2 * 
		  ffmpegenc->context->channels);
    ffmpegenc->buffer_pos = 0;
  }
  return GST_PAD_CONNECT_OK;
}

static void
gst_ffmpegenc_init(GstFFMpegEnc *ffmpegenc)
{
  GstFFMpegEncClass *oclass = (GstFFMpegEncClass*)(G_OBJECT_GET_CLASS (ffmpegenc));

  ffmpegenc->context = g_malloc0 (sizeof (AVCodecContext));

  if (oclass->in_plugin->type == CODEC_TYPE_VIDEO) {
    ffmpegenc->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegenc_video_sink_factory), "sink");
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_video);
    ffmpegenc->context->bit_rate = 400000;
    ffmpegenc->context->bit_rate_tolerance = 400000;
    ffmpegenc->context->qmin = 3;
    ffmpegenc->context->qmax = 15;
    ffmpegenc->context->max_qdiff = 3;
    ffmpegenc->context->gop_size = 15;
    ffmpegenc->context->frame_rate = 25 * FRAME_RATE_BASE;
    ffmpegenc->out_width = -1;
    ffmpegenc->out_height = -1;
  }
  else if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    ffmpegenc->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegenc_audio_sink_factory), "sink");
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_audio);
    ffmpegenc->context->bit_rate = 128000;
    ffmpegenc->context->sample_rate = -1;
  }

  gst_pad_set_connect_function (ffmpegenc->sinkpad, gst_ffmpegenc_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->sinkpad);

  ffmpegenc->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_ffmpegenc_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->srcpad);

  /* Initialization of element's private variables. */
  ffmpegenc->need_resample = FALSE;
}

static void
gst_ffmpegenc_chain_audio (GstPad *pad, GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *)(gst_pad_get_parent (pad));
  gpointer data;
  gint size;
  gint frame_size = ffmpegenc->context->frame_size * ffmpegenc->context->channels * 2;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  GST_DEBUG (0, "got buffer %p %d", data, size);

  if (ffmpegenc->buffer_pos && (ffmpegenc->buffer_pos + size >= frame_size)) {

    memcpy (ffmpegenc->buffer + ffmpegenc->buffer_pos, data, frame_size - ffmpegenc->buffer_pos);

    outbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (outbuf) = frame_size;
    GST_BUFFER_DATA (outbuf) = g_malloc (frame_size);

    GST_BUFFER_SIZE (outbuf) = avcodec_encode_audio (ffmpegenc->context, GST_BUFFER_DATA (outbuf),
  				GST_BUFFER_SIZE (outbuf), (const short *)ffmpegenc->buffer);

    gst_pad_push (ffmpegenc->srcpad, outbuf);

    size -= (frame_size - ffmpegenc->buffer_pos);
    data += (frame_size - ffmpegenc->buffer_pos);

    ffmpegenc->buffer_pos = 0;
  }
  while (size >= frame_size) {

    outbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (outbuf) = frame_size;
    GST_BUFFER_DATA (outbuf) = g_malloc (frame_size);

    GST_BUFFER_SIZE (outbuf) = avcodec_encode_audio (ffmpegenc->context, GST_BUFFER_DATA (outbuf),
  				GST_BUFFER_SIZE (outbuf), (const short *)data);

    gst_pad_push (ffmpegenc->srcpad, outbuf);

    size -= frame_size;
    data += frame_size;
  }
    
  /* save leftover */
  if (size) {
     memcpy (ffmpegenc->buffer + ffmpegenc->buffer_pos, data, size);
     ffmpegenc->buffer_pos += size;
  }

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegenc_chain_video (GstPad *pad, GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *)(gst_pad_get_parent (pad));
  gpointer data;
  gint size, frame_size;
  AVPicture picture, rpicture, *toencode;
  gboolean free_data = FALSE, free_res = FALSE;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  frame_size = ffmpegenc->in_width * ffmpegenc->in_height;

  /*
  switch (ffmpegenc->context->pix_fmt) {
    case PIX_FMT_YUV422: 
    {
      guchar *temp;

      temp = g_malloc ((frame_size * 3) /2 );
      size = (frame_size * 3)/2;

      img_convert (temp, PIX_FMT_YUV422, data, PIX_FMT_YUV420P,
		      ffmpegenc->in_width, ffmpegenc->in_height);
      data = temp;
      free_data = TRUE;
      break;
    }
    default:
      break;
  }
  */

  avpicture_fill (&picture, data, PIX_FMT_YUV420P, ffmpegenc->in_width, ffmpegenc->in_height);
  toencode = &picture;

  if (ffmpegenc->need_resample) {
    gint rframe_size = ffmpegenc->context->width * ffmpegenc->context->height;
    guint8 *rdata;

    rdata = g_malloc ((rframe_size * 3)/2);
    avpicture_fill (&rpicture, rdata, PIX_FMT_YUV420P, ffmpegenc->context->width, ffmpegenc->context->height);

    free_res = TRUE;
    toencode = &rpicture;
    
    img_resample (ffmpegenc->resample, &rpicture, &picture);
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = VIDEO_BUFFER_SIZE;
  GST_BUFFER_DATA (outbuf) = g_malloc (VIDEO_BUFFER_SIZE);

  GST_BUFFER_SIZE (outbuf) = avcodec_encode_video (ffmpegenc->context, GST_BUFFER_DATA (outbuf),
  				GST_BUFFER_SIZE (outbuf), toencode);

  gst_pad_push (ffmpegenc->srcpad, outbuf);

  if (free_data)
    g_free (data);
  if (free_res)
    g_free (rpicture.data[0]);

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegenc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFFMpegEnc *ffmpegenc;

  /* Get a pointer of the right type. */
  ffmpegenc = (GstFFMpegEnc *)(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case ARG_WIDTH:
      ffmpegenc->out_width = g_value_get_int (value);
      ffmpegenc->need_resample = (ffmpegenc->out_width != -1);
      break;
    case ARG_HEIGHT:
      ffmpegenc->out_height = g_value_get_int (value);
      ffmpegenc->need_resample = (ffmpegenc->out_height != -1);
      break;
    case ARG_BIT_RATE:
      ffmpegenc->context->bit_rate = g_value_get_int (value);
      break;
    case ARG_FRAME_RATE:
      ffmpegenc->context->frame_rate = g_value_get_int (value);
      break;
    case ARG_SAMPLE_RATE:
      ffmpegenc->context->sample_rate = g_value_get_int (value);
      break;
    case ARG_GOP_SIZE:
      ffmpegenc->context->gop_size = g_value_get_int (value);
      break;
    case ARG_HQ:
      ffmpegenc->context->flags &= ~CODEC_FLAG_HQ;
      ffmpegenc->context->flags |= (g_value_get_boolean (value)?CODEC_FLAG_HQ:0);
      break;
    case ARG_ME_METHOD:
      motion_estimation_method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegenc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFFMpegEnc *ffmpegenc;

  /* It's not null if we got it, but it might not be ours */
  ffmpegenc = (GstFFMpegEnc *)(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, ffmpegenc->out_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, ffmpegenc->out_height);
      break;
    case ARG_BIT_RATE:
      g_value_set_int (value, ffmpegenc->context->bit_rate);
      break;
    case ARG_SAMPLE_RATE:
      g_value_set_int (value, ffmpegenc->context->sample_rate);
      break;
    case ARG_FRAME_RATE:
      g_value_set_int (value, ffmpegenc->context->frame_rate);
      break;
    case ARG_GOP_SIZE:
      g_value_set_int (value, ffmpegenc->context->gop_size);
      break;
    case ARG_HQ:
      g_value_set_boolean (value, ffmpegenc->context->flags & CODEC_FLAG_HQ);
      break;
    case ARG_ME_METHOD:
      g_value_set_enum (value, motion_estimation_method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegenc_register (GstPlugin *plugin)
{
  GstElementFactory *factory;
  GTypeInfo typeinfo = {
    sizeof(GstFFMpegEncClass),      
    NULL,
    NULL,
    (GClassInitFunc)gst_ffmpegenc_class_init,
    NULL,
    NULL,
    sizeof(GstFFMpegEnc),
    0,
    (GInstanceInitFunc)gst_ffmpegenc_init,
  };
  GType type;
  GstElementDetails *details;
  AVCodec *in_plugin;
  
  in_plugin = first_avcodec;

  enc_global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    gchar *codec_type;

    if (in_plugin->encode) {
      codec_type = "enc";
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

    g_hash_table_insert (enc_global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) in_plugin);

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_val_if_fail(factory != NULL, FALSE);

    gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegenc_src_factory));
    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegenc_video_sink_factory));
    }
    else if (in_plugin->type == CODEC_TYPE_AUDIO) {
      gst_element_factory_add_pad_template (factory, 
		    GST_PAD_TEMPLATE_GET (gst_ffmpegenc_audio_sink_factory));
    }

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  return TRUE;
}
