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

typedef struct _GstFFMpegDecClassParams GstFFMpegDecClassParams;

struct _GstFFMpegDecClassParams {
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
static void	gst_ffmpegdec_base_init		(GstFFMpegDecClass *klass);
static void	gst_ffmpegdec_class_init	(GstFFMpegDecClass *klass);
static void	gst_ffmpegdec_init		(GstFFMpegDec *ffmpegdec);
static void	gst_ffmpegdec_dispose		(GObject      *object);

static GstPadLinkReturn	gst_ffmpegdec_connect	(GstPad    *pad,
						 const GstCaps  *caps);
static void	gst_ffmpegdec_chain		(GstPad    *pad,
						 GstData   *data);

static GstElementStateReturn
		gst_ffmpegdec_change_state	(GstElement *element);

#if 0
/* some sort of bufferpool handling, but different */
static int	gst_ffmpegdec_get_buffer	(AVCodecContext *context,
						 AVFrame        *picture);
static void	gst_ffmpegdec_release_buffer	(AVCodecContext *context,
						 AVFrame        *picture);
#endif

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdec_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdec_base_init (GstFFMpegDecClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDecClassParams *params;
  GstElementDetails *details;
  GstPadTemplate *sinktempl, *srctempl;

  params = g_hash_table_lookup (global_plugins,
		GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins,
		GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details = g_new0 (GstElementDetails, 1);
  details->longname = g_strdup_printf("FFMPEG %s decoder",
				      params->in_plugin->name);
  details->klass = g_strdup_printf("Codec/Decoder/%s",
				   (params->in_plugin->type == CODEC_TYPE_VIDEO) ?
				   "Video" : "Audio");
  details->description = g_strdup_printf("FFMPEG %s decoder",
					 params->in_plugin->name);
  details->author = g_strdup("Wim Taymans <wim.taymans@chello.be>\n"
			     "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
				    GST_PAD_ALWAYS, params->sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
				   GST_PAD_ALWAYS, params->srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);
  gst_element_class_set_details (element_class, details);

  klass->in_plugin = params->in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
}

static void
gst_ffmpegdec_class_init (GstFFMpegDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_peek_parent (klass);

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

  G_OBJECT_CLASS (parent_class)->dispose (object);
  /* old session should have been closed in element_class->dispose */
  g_assert (!ffmpegdec->opened);

  /* clean up remaining allocated data */
  av_free (ffmpegdec->context);
  av_free (ffmpegdec->picture);
}

static GstPadLinkReturn
gst_ffmpegdec_connect (GstPad  *pad,
		       const GstCaps *caps)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));

  /* close old session */
  if (ffmpegdec->opened) {
    avcodec_close (ffmpegdec->context);
    ffmpegdec->opened = FALSE;
  }

  /* set defaults */
  avcodec_get_context_defaults (ffmpegdec->context);

#if 0
  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;
#endif

  /* get size and so */
  gst_ffmpeg_caps_to_codectype (oclass->in_plugin->type,
				caps, ffmpegdec->context);

  /* we dont send complete frames */
  if (oclass->in_plugin->capabilities & CODEC_CAP_TRUNCATED)
    ffmpegdec->context->flags |= CODEC_FLAG_TRUNCATED;

  /* do *not* draw edges */
  ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0) {
    GST_DEBUG (
		"ffdec_%s: Failed to open FFMPEG codec",
		oclass->in_plugin->name);
    return GST_PAD_LINK_REFUSED;
  }

  /* done! */
  ffmpegdec->opened = TRUE;

  return GST_PAD_LINK_OK;
}

#if 0
static int
gst_ffmpegdec_get_buffer (AVCodecContext *context,
			  AVFrame        *picture)
{
  GstBuffer *buf = NULL;
  gulong bufsize = 0;

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      bufsize = avpicture_get_size (context->pix_fmt,
				    context->width,
				    context->height);
      buf = gst_buffer_new_and_alloc (bufsize);
      avpicture_fill ((AVPicture *) picture, GST_BUFFER_DATA (buf),
		      context->pix_fmt,
		      context->width, context->height);
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
}
#endif

static void
gst_ffmpegdec_chain (GstPad    *pad,
		     GstData *_data)
{
  GstBuffer *inbuf = GST_BUFFER (_data);
  GstBuffer *outbuf = NULL;
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *)(gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass = (GstFFMpegDecClass*)(G_OBJECT_GET_CLASS (ffmpegdec));
  guchar *data;
  gint size, len = 0;
  gint have_data;

  if (!ffmpegdec->opened) {
    gst_element_error (GST_ELEMENT (ffmpegdec),
		       "ffdec_%s: input format was not set before data-start",
		       oclass->in_plugin->name);
    return;
  }

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
	/* workarounds, functions write to buffers:
	   libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
           libavcodec/svq3.c:svq3_decode_slice_header too */
	if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
            oclass->in_plugin->id == CODEC_ID_SVQ3) {
	  inbuf = gst_buffer_copy_on_write(inbuf);
	  data = GST_BUFFER_DATA (inbuf);
	  size = GST_BUFFER_SIZE (inbuf);
	}
        len = avcodec_decode_video (ffmpegdec->context,
				    ffmpegdec->picture,
				    &have_data,
				    data, size);
        if (have_data) {
          /* libavcodec constantly crashes on stupid buffer allocation
           * errors inside. This drives me crazy, so we let it allocate
           * it's own buffers and copy to our own buffer afterwards... */
          AVPicture pic;
          gint size = avpicture_get_size (ffmpegdec->context->pix_fmt,
					  ffmpegdec->context->width,
					  ffmpegdec->context->height);
          outbuf = gst_buffer_new_and_alloc (size);
          avpicture_fill (&pic, GST_BUFFER_DATA (outbuf),
			  ffmpegdec->context->pix_fmt,
			  ffmpegdec->context->width,
			  ffmpegdec->context->height);
          img_convert (&pic, ffmpegdec->context->pix_fmt,
		       (AVPicture *) ffmpegdec->picture,
		       ffmpegdec->context->pix_fmt,
		       ffmpegdec->context->width,
		       ffmpegdec->context->height);

          /* this isn't necessarily true, but it's better than nothing */
          GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
        }
        break;

      case CODEC_TYPE_AUDIO:
        outbuf = gst_buffer_new_and_alloc (AVCODEC_MAX_AUDIO_FRAME_SIZE);
        len = avcodec_decode_audio (ffmpegdec->context,
				    (int16_t *) GST_BUFFER_DATA (outbuf),
				    &have_data,
				    data, size);
        if (have_data) {
          GST_BUFFER_SIZE (outbuf) = have_data;
          GST_BUFFER_DURATION (outbuf) = (have_data * GST_SECOND) /
					   (ffmpegdec->context->channels *
					    ffmpegdec->context->sample_rate);
        } else {
          gst_buffer_unref (outbuf);
        } 
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

      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);

      gst_pad_push (ffmpegdec->srcpad, GST_DATA (outbuf));
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
  GTypeInfo typeinfo = {
    sizeof(GstFFMpegDecClass),      
    (GBaseInitFunc)gst_ffmpegdec_base_init,
    NULL,
    (GClassInitFunc)gst_ffmpegdec_class_init,
    NULL,
    NULL,
    sizeof(GstFFMpegDec),
    0,
    (GInstanceInitFunc)gst_ffmpegdec_init,
  };
  GType type;
  AVCodec *in_plugin;
  
  in_plugin = first_avcodec;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    GstFFMpegDecClassParams *params;
    GstCaps *srccaps, *sinkcaps;
    gchar *type_name;

    /* no quasi-codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
	(in_plugin->id >= CODEC_ID_PCM_S16LE &&
	 in_plugin->id <= CODEC_ID_PCM_ALAW)) {
      goto next;
    }

    /* only decoders */
    if (!in_plugin->decode) {
      goto next;
    }

    /* first make sure we've got a supported type */
    sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL);
    srccaps  = gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL);
    if (!sinkcaps || !srccaps)
      goto next;

    /* construct the type */
    type_name = g_strdup_printf("ffdec_%s", in_plugin->name);

    /* if it's already registered, drop it */
    if (g_type_from_name(type_name)) {
      g_free(type_name);
      goto next;
    }

    params = g_new0 (GstFFMpegDecClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->sinkcaps = sinkcaps;
    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (0), 
			 (gpointer) params);
    
    /* create the gtype now */
    type = g_type_register_static(GST_TYPE_ELEMENT, type_name , &typeinfo, 0);
    if (!gst_element_register (plugin, type_name, GST_RANK_MARGINAL, type))
      return FALSE;

    g_hash_table_insert (global_plugins, 
		         GINT_TO_POINTER (type), 
			 (gpointer) params);

next:
    in_plugin = in_plugin->next;
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
