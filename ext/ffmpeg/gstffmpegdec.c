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

#include "config.h"

#include <assert.h>
#include <string.h>

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegDec GstFFMpegDec;

struct _GstFFMpegDec {
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
};

typedef struct _GstFFMpegDecClass GstFFMpegDecClass;

struct _GstFFMpegDecClass {
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

typedef struct {
  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
} GstFFMpegDecClassParams;

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
static void	gst_ffmpegdec_class_init	(GstFFMpegDecClass *klass);
static void	gst_ffmpegdec_init		(GstFFMpegDec *ffmpegdec);
static void	gst_ffmpegdec_dispose		(GObject      *object);

static GstPadLinkReturn	gst_ffmpegdec_connect	(GstPad    *pad,
						 GstCaps   *caps);
static void	gst_ffmpegdec_chain		(GstPad    *pad,
						 GstBuffer *buffer);

static GstElementStateReturn
		gst_ffmpegdec_change_state	(GstElement *element);

/* some sort of bufferpool handling, but different */
static int	gst_ffmpegdec_get_buffer	(AVCodecContext *context,
						 AVFrame        *picture);
static void	gst_ffmpegdec_release_buffer	(AVCodecContext *context,
						 AVFrame        *picture);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdec_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdec_class_init (GstFFMpegDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstFFMpegDecClassParams *params;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  params = g_hash_table_lookup (global_plugins,
		  GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));

  klass->in_plugin = params->in_plugin;
  klass->srctempl = params->srctempl;
  klass->sinktempl = params->sinktempl;

  gobject_class->dispose = gst_ffmpegdec_dispose;
  gstelement_class->change_state = gst_ffmpegdec_change_state;
}

static void
gst_ffmpegdec_init (GstFFMpegDec *ffmpegdec)
{
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_link_function (ffmpegdec->sinkpad, gst_ffmpegdec_connect);
  gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain);
  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");

  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context();
  ffmpegdec->picture = avcodec_alloc_frame();

  ffmpegdec->opened = FALSE;
}

static void
gst_ffmpegdec_dispose (GObject *object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;
  /* close old session */
  if (ffmpegdec->opened) {
    avcodec_close (ffmpegdec->context);
    ffmpegdec->opened = FALSE;
  }

  /* clean up remaining allocated data */
  av_free (ffmpegdec->context);
  av_free (ffmpegdec->picture);
}

static GstPadLinkReturn
gst_ffmpegdec_connect (GstPad  *pad,
		       GstCaps *caps)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));

  /* we want fixed caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  /* close old session */
  if (ffmpegdec->opened) {
    avcodec_close (ffmpegdec->context);
    ffmpegdec->opened = FALSE;
  }

  /* set defaults */
  avcodec_get_context_defaults (ffmpegdec->context);

  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      /* get size */
      if (gst_caps_has_property_typed (caps, "width", GST_PROPS_INT_TYPE))
        gst_caps_get_int (caps, "width", &ffmpegdec->context->width);
      if (gst_caps_has_property_typed (caps, "height", GST_PROPS_INT_TYPE))
        gst_caps_get_int (caps, "height", &ffmpegdec->context->height);
      break;

    case CODEC_TYPE_AUDIO:
      /* FIXME: does ffmpeg want us to set the sample format
       * and the rate+channels here?  Or does it provide them
       * itself? */
      break;

    default:
      /* Unsupported */
      return GST_PAD_LINK_REFUSED;
  }

  /* we dont send complete frames */
  if (oclass->in_plugin->capabilities & CODEC_CAP_TRUNCATED)
    ffmpegdec->context->flags |= CODEC_FLAG_TRUNCATED;

  /* do *not* draw edges */
  ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0) {
    GST_DEBUG (GST_CAT_PLUGIN_INFO,
		"ffdec_%s: Failed to open FFMPEG codec",
		oclass->in_plugin->name);
    return GST_PAD_LINK_REFUSED;
  }

  /* done! */
  ffmpegdec->opened = TRUE;

  return GST_PAD_LINK_OK;
}

/* innocent hacks */
#define ALIGN(x) (((x)+alignment)&~alignment)

static int
gst_ffmpegdec_get_buffer (AVCodecContext *context,
			  AVFrame        *picture)
{
  GstBuffer *buf = NULL;
  gint hor_chr_dec = -1, ver_chr_dec = -1;
  gint width, height;
  gint alignment;
  gulong bufsize = 0;

  /* set alignment */
  if (context->codec_id == CODEC_ID_SVQ1) {
    alignment = 63;
  } else {
    alignment = 15;
  }

  /* set start size */
  width = ALIGN (context->width);
  height = ALIGN (context->height);

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      bufsize = avpicture_get_size (context->pix_fmt,
				    width, height);

      /* find out whether we are planar or packed */
      switch (context->pix_fmt) {
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUV410P:
        case PIX_FMT_YUV411P:
          avcodec_get_chroma_sub_sample (context->pix_fmt,
					 &hor_chr_dec, &ver_chr_dec);
          break;
        case PIX_FMT_YUV422:
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
        case PIX_FMT_RGBA32:
        case PIX_FMT_RGB565:
        case PIX_FMT_RGB555:
          /* not planar */
          break;
        default:
          g_assert (0);
          break;
      }
      break;

    case CODEC_TYPE_AUDIO:
      bufsize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
      break;

    default:
      g_assert (0);
      break;
  }

  /* create buffer */
  buf = gst_buffer_new_and_alloc (bufsize);

  /* set up planes */
  picture->data[0] = GST_BUFFER_DATA (buf);
  if (hor_chr_dec >= 0 && ver_chr_dec >= 0) {
    picture->linesize[0] = width;
    picture->linesize[1] = width >> hor_chr_dec;
    picture->linesize[2] = width >> hor_chr_dec;

    picture->data[1] = picture->data[0] + (width * height);
    picture->data[2] = picture->data[1] +
			 ((width * height) >> (ver_chr_dec + hor_chr_dec));
  } else {
    picture->linesize[0] = GST_BUFFER_MAXSIZE (buf) / height;
    picture->linesize[1] = picture->linesize[2] = 0;

    picture->data[1] = picture->data[2] = NULL;
  }
  picture->linesize[3] = 0;
  picture->data[3] = NULL;

  /* tell ffmpeg we own this buffer
   *
   * we also use an evil hack (keep buffer in base[0])
   * to keep a reference to the buffer in release_buffer(),
   * so that we can ref() it here and unref() it there
   * so that we don't need to copy data */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = G_MAXINT;
  picture->base[0] = (int8_t *) buf;
  gst_buffer_ref (buf);

  return 0;
}

static void
gst_ffmpegdec_release_buffer (AVCodecContext *context,
			      AVFrame        *picture)
{
  gint i;
  GstBuffer *buf = GST_BUFFER (picture->base[0]);
  gst_buffer_unref (buf);

  /* zero out the reference in ffmpeg */
  for (i=0;i<4;i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
  picture->base[0] = NULL;
}

static void
gst_ffmpegdec_chain (GstPad    *pad,
		     GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));
  guchar *data;
  gint size, len = 0;
  gint have_data;

  /* FIXME: implement event awareness (especially EOS
   * (av_close_codec ()) and FLUSH/DISCONT
   * (avcodec_flush_buffers ()))
   */

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  do {
    ffmpegdec->context->frame_number++;

    switch (oclass->in_plugin->type) {
      case CODEC_TYPE_VIDEO:
        len = avcodec_decode_video (ffmpegdec->context,
				    ffmpegdec->picture,
				    &have_data,
				    data, size);
        break;
      case CODEC_TYPE_AUDIO:
        len = avcodec_decode_audio (ffmpegdec->context,
				    (int16_t *) ffmpegdec->picture->data[0],
				    &have_data,
				    data, size);
        break;
      default:
	g_assert(0);
        break;
    }

    if (len < 0) {
      g_warning ("ffdec_%s: decoding error",
		 oclass->in_plugin->name);
      break;
    }

    if (have_data) {
      if (!GST_PAD_CAPS (ffmpegdec->srcpad)) {
        GstCaps *caps;
        caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
					     ffmpegdec->context);
        if (caps == NULL ||
            gst_pad_try_set_caps (ffmpegdec->srcpad, caps) <= 0) {
          gst_element_error (GST_ELEMENT (ffmpegdec),
			     "Failed to link ffmpeg decoder (%s) to next element",
			     oclass->in_plugin->name);
          return;
        }
      }

      outbuf = GST_BUFFER (ffmpegdec->picture->base[0]);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
      if (oclass->in_plugin->type == CODEC_TYPE_AUDIO)
        GST_BUFFER_SIZE (outbuf) = have_data;
      else
        GST_BUFFER_SIZE (outbuf) = GST_BUFFER_MAXSIZE (outbuf);
      gst_pad_push (ffmpegdec->srcpad, outbuf);
    } 

    size -= len;
    data += len;
  } while (size > 0);

  gst_buffer_unref (inbuf);
}

static GstElementStateReturn
gst_ffmpegdec_change_state (GstElement *element)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) element;
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      if (ffmpegdec->opened) {
        avcodec_close (ffmpegdec->context);
        ffmpegdec->opened = FALSE;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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
    GstPadTemplate *sinktempl, *srctempl;
    GstCaps *sinkcaps, *srccaps;
    GstFFMpegDecClassParams *params;

    if (in_plugin->decode) {
      codec_type = "dec";
    }
    else {
      goto next;
    }

    /* first make sure we've got a supported type */
    sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL);
    srccaps  = gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL);
    if (!sinkcaps || !srccaps)
      goto next;

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
    details = g_new0 (GstElementDetails, 1);
    details->longname = g_strdup(in_plugin->name);
    details->klass = g_strdup_printf("Codec/%s/Decoder",
				     (in_plugin->type == CODEC_TYPE_VIDEO) ?
				     "Video" : "Audio");
    details->license = g_strdup("LGPL");
    details->description = g_strdup_printf("FFMPEG %s decoder",
					   in_plugin->name);
    details->version = g_strdup(VERSION);
    details->author = g_strdup("The FFMPEG crew\n"
				"Wim Taymans <wim.taymans@chello.be>\n"
				"Ronald Bultje <rbultje@ronald.bitfreak.net>");
    details->copyright = g_strdup("(c) 2001-2003");

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_val_if_fail(factory != NULL, FALSE);

    gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_MARGINAL);

    sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
				      GST_PAD_ALWAYS, sinkcaps, NULL);
    gst_element_factory_add_pad_template (factory, sinktempl);

    srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
				     GST_PAD_ALWAYS, srccaps, NULL);
    gst_element_factory_add_pad_template (factory, srctempl);

    params = g_new0 (GstFFMpegDecClassParams, 1);
    params->in_plugin = in_plugin;
    params->sinktempl = sinktempl;
    params->srctempl = srctempl;

    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) params);

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

next:
    in_plugin = in_plugin->next;
  }

  return TRUE;
}
