/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif
#include <gst/gst.h>

#include "gstffmpegcodecmap.h"

#define GST_TYPE_FFMPEGCSP \
  (gst_ffmpegcsp_get_type())
#define GST_FFMPEGCSP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGCSP,GstFFMpegCsp))
#define GST_FFMPEGCSP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGCSP,GstFFMpegCsp))
#define GST_IS_FFMPEGCSP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGCSP))
#define GST_IS_FFMPEGCSP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGCSP))

typedef struct _GstFFMpegCsp GstFFMpegCsp;
typedef struct _GstFFMpegCspClass GstFFMpegCspClass;

struct _GstFFMpegCsp {
  GstElement 	 element;

  GstPad 	*sinkpad, *srcpad;

  gint 		width, height;
  gfloat	fps;
  enum PixelFormat
		from_pixfmt,
		to_pixfmt;
  AVFrame	*from_frame,
		*to_frame;
  GstCaps	*sinkcaps;

  GstBufferPool *pool;
};

struct _GstFFMpegCspClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegcsp_details = {
  "FFMPEG Colorspace converter",
  "Filter/Effect",
  "LGPL",
  "Converts video from one colorspace to another",
  VERSION,
  "The FFMPEG crew, "
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2003",
};


/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
};

static GType	gst_ffmpegcsp_get_type 		(void);

static void	gst_ffmpegcsp_class_init	(GstFFMpegCspClass *klass);
static void	gst_ffmpegcsp_init		(GstFFMpegCsp *space);

static void	gst_ffmpegcsp_set_property	(GObject    *object,
						 guint       prop_id, 
						 const GValue *value,
						 GParamSpec *pspec);
static void	gst_ffmpegcsp_get_property	(GObject    *object,
						 guint       prop_id, 
						 GValue     *value,
						 GParamSpec *pspec);

static GstPadLinkReturn
		gst_ffmpegcsp_sinkconnect	(GstPad     *pad,
						 GstCaps    *caps);
static GstPadLinkReturn
		gst_ffmpegcsp_srcconnect 	(GstPad     *pad,
						 GstCaps    *caps);
static GstPadLinkReturn
		gst_ffmpegcsp_srcconnect_func 	(GstPad     *pad,
						 GstCaps    *caps,
						 gboolean    newcaps);

static void	gst_ffmpegcsp_chain		(GstPad     *pad,
						 GstData    *data);
static GstElementStateReturn
		gst_ffmpegcsp_change_state 	(GstElement *element);

static GstPadTemplate *srctempl, *sinktempl;
static GstElementClass *parent_class = NULL;
/*static guint gst_ffmpegcsp_signals[LAST_SIGNAL] = { 0 }; */

static GstBufferPool *
ffmpegcsp_get_bufferpool (GstPad *pad)
{
  GstFFMpegCsp *space;

  space = GST_FFMPEGCSP (gst_pad_get_parent (pad));

  if (space->from_pixfmt == space->to_pixfmt &&
      space->from_pixfmt != PIX_FMT_NB) {
    return gst_pad_get_bufferpool (space->srcpad);
  }

  return NULL;
}

static GstCaps *
gst_ffmpegcsp_getcaps (GstPad  *pad,
		       GstCaps *caps)
{
  GstFFMpegCsp *space;
  GstCaps *result;
  GstCaps *peercaps;
  GstCaps *ourcaps;
  
  space = GST_FFMPEGCSP (gst_pad_get_parent (pad));

  /* we can do everything our peer can... */
  peercaps = gst_caps_copy (gst_pad_get_allowed_caps (space->srcpad));

  /* and our own template of course */
  ourcaps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  /* merge them together, we prefer the peercaps first */
  result = gst_caps_prepend (ourcaps, peercaps);

  return result;
}

static GstPadLinkReturn
gst_ffmpegcsp_srcconnect_func (GstPad  *pad,
			       GstCaps *caps,
			       gboolean newcaps)
{
  AVCodecContext *ctx;
  GstFFMpegCsp *space;
  GstCaps *peercaps;
  GstCaps *ourcaps;
  
  space = GST_FFMPEGCSP (gst_pad_get_parent (pad));

  /* we cannot operate if we didn't get src caps */
  if (!(ourcaps = space->sinkcaps)) {
    if (newcaps) {
      gst_pad_recalc_allowed_caps (space->sinkpad);
    }

    return GST_PAD_LINK_DELAYED;
  }

  /* first see if we can do the format natively by filtering the peer caps 
   * with our incomming caps */
  if ((peercaps = gst_caps_intersect (caps, ourcaps)) != NULL) {
    /* see if the peer likes it too, it should as the caps say so.. */
    if (gst_pad_try_set_caps (space->srcpad, peercaps) > 0) {
      space->from_pixfmt = space->to_pixfmt = -1;
      return GST_PAD_LINK_DONE;
    }
  }

  /* then see what the peer has that matches the size */
  peercaps = gst_caps_intersect (caps,
		  gst_caps_append (
		  GST_CAPS_NEW (
		   "ffmpegcsp_filter",
		   "video/x-raw-yuv",
		     "width",     GST_PROPS_INT (space->width),
		     "height",    GST_PROPS_INT (space->height),
		     "framerate", GST_PROPS_FLOAT (space->fps)
		  ), GST_CAPS_NEW (
		   "ffmpegcsp_filter",
		   "video/x-raw-rgb",
		     "width",     GST_PROPS_INT (space->width),
		     "height",    GST_PROPS_INT (space->height),
		     "framerate", GST_PROPS_FLOAT (space->fps)
		  )));

  /* we are looping over the caps, so we have to get rid of the lists */
  peercaps = gst_caps_normalize (peercaps);

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  ctx = avcodec_alloc_context ();
  while (peercaps) {
    ctx->width = space->width;
    ctx->height = space->height;
    ctx->pix_fmt = PIX_FMT_NB;
    gst_ffmpeg_caps_to_codectype (CODEC_TYPE_VIDEO, peercaps, ctx);
    if (ctx->pix_fmt != PIX_FMT_NB) {
      GstCaps *one = gst_caps_copy_1 (peercaps);
      if (gst_pad_try_set_caps (space->srcpad, one) > 0) {
        space->to_pixfmt = ctx->pix_fmt;
        gst_caps_unref (one);
        av_free (ctx);
        if (space->from_frame)
          av_free (space->from_frame);
        if (space->to_frame)
          av_free (space->to_frame);
        space->from_frame = avcodec_alloc_frame ();
        space->to_frame = avcodec_alloc_frame ();
        return GST_PAD_LINK_DONE;
      }
      gst_caps_unref (one);
    }
    peercaps = peercaps->next;
  }
  av_free (ctx);
  
  /* we disable ourself here */
  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;

  return GST_PAD_LINK_REFUSED;
}

static GstPadLinkReturn
gst_ffmpegcsp_sinkconnect (GstPad  *pad,
			   GstCaps *caps)
{
  AVCodecContext *ctx;
  GstFFMpegCsp *space;
  GstPad *peer;

  space = GST_FFMPEGCSP (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  ctx = avcodec_alloc_context ();
  ctx->width = 0;
  ctx->height = 0;
  ctx->pix_fmt = PIX_FMT_NB;

  gst_ffmpeg_caps_to_codectype (CODEC_TYPE_VIDEO, caps, ctx);
  if (!ctx->width || !ctx->height || ctx->pix_fmt == PIX_FMT_NB) {
    return GST_PAD_LINK_REFUSED;
  }

  gst_caps_get_float (caps, "framerate", &space->fps);
  space->width = ctx->width;
  space->height = ctx->height;
  space->from_pixfmt = ctx->pix_fmt;
  av_free (ctx);

  GST_INFO ( "size: %dx%d", space->width, space->height);

  space->sinkcaps = caps;

  if ((peer = gst_pad_get_peer (pad)) != NULL) {
    GstPadLinkReturn ret;
    ret = gst_ffmpegcsp_srcconnect_func (pad,
					 gst_pad_get_allowed_caps (space->srcpad),
					 FALSE);
    if (ret <= 0) {
      space->sinkcaps = NULL;
      return ret;
    }

    return GST_PAD_LINK_DONE;
  }

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_ffmpegcsp_srcconnect (GstPad  *pad,
			  GstCaps *caps)
{
  return gst_ffmpegcsp_srcconnect_func (pad, caps, TRUE);
}

static GType
gst_ffmpegcsp_get_type (void)
{
  static GType ffmpegcsp_type = 0;

  if (!ffmpegcsp_type) {
    static const GTypeInfo ffmpegcsp_info = {
      sizeof (GstFFMpegCspClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ffmpegcsp_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegCsp),
      0,
      (GInstanceInitFunc) gst_ffmpegcsp_init,
    };

    ffmpegcsp_type = g_type_register_static (GST_TYPE_ELEMENT,
					     "GstFFMpegColorspace",
					     &ffmpegcsp_info, 0);
  }

  return ffmpegcsp_type;
}

static void
gst_ffmpegcsp_class_init (GstFFMpegCspClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ffmpegcsp_set_property;
  gobject_class->get_property = gst_ffmpegcsp_get_property;

  gstelement_class->change_state = gst_ffmpegcsp_change_state;
}

static void
gst_ffmpegcsp_init (GstFFMpegCsp *space)
{
  space->sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  gst_pad_set_link_function (space->sinkpad, gst_ffmpegcsp_sinkconnect);
  gst_pad_set_getcaps_function (space->sinkpad, gst_ffmpegcsp_getcaps);
  gst_pad_set_bufferpool_function (space->sinkpad, ffmpegcsp_get_bufferpool);
  gst_pad_set_chain_function (space->sinkpad,gst_ffmpegcsp_chain);
  gst_element_add_pad (GST_ELEMENT(space), space->sinkpad);

  space->srcpad = gst_pad_new_from_template (srctempl, "src");
  gst_element_add_pad (GST_ELEMENT (space), space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_ffmpegcsp_srcconnect);

  space->pool = NULL;
  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;
  space->from_frame = space->to_frame = NULL;
}

static void
gst_ffmpegcsp_chain (GstPad  *pad,
		     GstData *data)
{
  GstBuffer *inbuf = GST_BUFFER (data);
  GstFFMpegCsp *space;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  space = GST_FFMPEGCSP (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_FFMPEGCSP (space));

  if (space->from_pixfmt == PIX_FMT_NB ||
      space->to_pixfmt == PIX_FMT_NB) {
    gst_buffer_unref (inbuf);
    return;
  }

  if (space->from_pixfmt == space->to_pixfmt) {
    outbuf = inbuf;
  } else {
    if (space->pool) {
      outbuf = gst_buffer_new_from_pool (space->pool, 0, 0);
    }

    if (!outbuf) {
      guint size = avpicture_get_size (space->to_pixfmt,
				       space->width,
				       space->height);
      outbuf = gst_buffer_new_and_alloc (size);
    }

    /* convert */
    avpicture_fill ((AVPicture *) space->from_frame, GST_BUFFER_DATA (inbuf),
		    space->from_pixfmt, space->width, space->height);
    avpicture_fill ((AVPicture *) space->to_frame, GST_BUFFER_DATA (outbuf),
		    space->to_pixfmt, space->width, space->height);
    img_convert ((AVPicture *) space->to_frame, space->to_pixfmt,
		 (AVPicture *) space->from_frame, space->from_pixfmt,
		 space->width, space->height);

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    gst_buffer_unref (inbuf);
  }

  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_ffmpegcsp_change_state (GstElement *element)
{
  GstFFMpegCsp *space;

  space = GST_FFMPEGCSP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      space->pool = gst_pad_get_bufferpool (space->srcpad);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      space->pool = NULL;
    case GST_STATE_PAUSED_TO_READY:
      if (space->from_frame)
        av_free (space->from_frame);
      if (space->to_frame)
        av_free (space->to_frame);
      space->from_frame = NULL;
      space->to_frame = NULL;
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ffmpegcsp_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  GstFFMpegCsp *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCSP (object));
  space = GST_FFMPEGCSP (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_ffmpegcsp_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  GstFFMpegCsp *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCSP (object));
  space = GST_FFMPEGCSP (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegcsp_register (GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;

  factory = gst_element_factory_new ("ffcolorspace", GST_TYPE_FFMPEGCSP,
                                    &ffmpegcsp_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  /* template caps */
  caps = gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, NULL);

  /* build templates */
  srctempl  = gst_pad_template_new ("src",
				    GST_PAD_SRC,
				    GST_PAD_ALWAYS,
				    caps, NULL);
  gst_caps_ref (caps);
  sinktempl = gst_pad_template_new ("sink",
				    GST_PAD_SINK,
				    GST_PAD_ALWAYS,
				    caps, NULL);

  gst_element_factory_add_pad_template (factory, srctempl);
  gst_element_factory_add_pad_template (factory, sinktempl);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
