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

#include <gst/gst.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include "gstffmpegcodecmap.h"

GST_DEBUG_CATEGORY_STATIC (debug_ffmpeg_csp);
#define GST_CAT_DEFAULT debug_ffmpeg_csp

#define GST_TYPE_FFMPEG_CSP \
  (gst_ffmpegcsp_get_type())
#define GST_FFMPEG_CSP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEG_CSP,GstFFMpegCsp))
#define GST_FFMPEG_CSP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEG_CSP,GstFFMpegCsp))
#define GST_IS_FFMPEG_CSP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEG_CSP))
#define GST_IS_FFMPEG_CSP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEG_CSP))

typedef struct _GstFFMpegCsp GstFFMpegCsp;
typedef struct _GstFFMpegCspClass GstFFMpegCspClass;

struct _GstFFMpegCsp {
  GstElement		element;

  GstPad *		sinkpad;
  GstPad *		srcpad;
  gboolean		need_caps_nego;

  gint			width;
  gint			height;
  gdouble		fps;
  
  enum PixelFormat	from_pixfmt;
  enum PixelFormat	to_pixfmt;
  AVFrame *		from_frame;
  AVFrame *		to_frame;
};

struct _GstFFMpegCspClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegcsp_details = {
  "FFMPEG Colorspace converter",
  "Filter/Converter/Video",
  "Converts video from one colorspace to another",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
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

static void	gst_ffmpegcsp_base_init		(gpointer g_class);
static void	gst_ffmpegcsp_class_init	(gpointer g_class, gpointer class_data);
static void	gst_ffmpegcsp_init		(GTypeInstance *instance, gpointer g_class);

static GstPadLinkReturn
		gst_ffmpegcsp_connect		(GstPad		*pad,
						 const GstCaps *caps);
static GstPadLinkReturn
		gst_ffmpegcsp_try_connect 	(GstPad		*pad,
						 AVCodecContext	*ctx,
						 double		 fps);

static void	gst_ffmpegcsp_chain		(GstPad		*pad,
						 GstData	*data);
static GstElementStateReturn
		gst_ffmpegcsp_change_state 	(GstElement	*element);

static GstElementClass *parent_class = NULL;
/*static guint gst_ffmpegcsp_signals[LAST_SIGNAL] = { 0 }; */

/* does caps nego on a pad */
static GstPadLinkReturn
gst_ffmpegcsp_try_connect (GstPad *pad, AVCodecContext *ctx, double fps)
{
  gint i, ret;
  GstFFMpegCsp *space;
  gboolean try_all = (ctx->pix_fmt != PIX_FMT_NB);
  GstCaps *caps;
  
  space = GST_FFMPEG_CSP (gst_pad_get_parent (pad));

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  caps = gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, ctx);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    GstCaps *setcaps;

    if (fps > 0)
      gst_structure_set (structure, "framerate", G_TYPE_DOUBLE, fps, NULL);

    setcaps = gst_caps_new_full (gst_structure_copy (structure), NULL);
    
    ret = gst_pad_try_set_caps (pad, setcaps);
    gst_caps_free (setcaps);
    if (ret >= 0) {
      if (ctx->pix_fmt == PIX_FMT_NB)
	gst_ffmpeg_caps_to_codectype (CODEC_TYPE_VIDEO, caps, ctx);
      gst_caps_free (caps);

      return ret;
    }
  }

  if (try_all) {
    ctx->pix_fmt = PIX_FMT_NB;
    return gst_ffmpegcsp_try_connect (pad, ctx, fps);
  } else {
    return GST_PAD_LINK_REFUSED;
  }
}

static GstPadLinkReturn
gst_ffmpegcsp_connect (GstPad *pad, const GstCaps *caps)
{
  AVCodecContext *ctx;
  GstFFMpegCsp *space;
  gdouble fps;
  enum PixelFormat pixfmt;
  GstPad *other;
  enum PixelFormat *format, *other_format;

  space = GST_FFMPEG_CSP (gst_pad_get_parent (pad));

  if (space->sinkpad == pad) {
    other = space->srcpad;
    format = &space->from_pixfmt;
    other_format = &space->to_pixfmt;
  } else if (space->srcpad == pad) {
    other = space->sinkpad;
    format = &space->to_pixfmt;
    other_format = &space->from_pixfmt;
  } else {
    g_assert_not_reached ();
    return GST_PAD_LINK_REFUSED;
  }
  ctx = avcodec_alloc_context ();
  ctx->width = 0;
  ctx->height = 0;
  ctx->pix_fmt = PIX_FMT_NB;

  gst_ffmpeg_caps_to_codectype (CODEC_TYPE_VIDEO, caps, ctx);
  if (!ctx->width || !ctx->height || ctx->pix_fmt == PIX_FMT_NB) {
    av_free (ctx);
    return GST_PAD_LINK_REFUSED;
  }

  if (!gst_structure_get_double (gst_caps_get_structure (caps, 0), 
	"framerate", &fps))
    fps = 0;
  
  pixfmt = ctx->pix_fmt;
  if (*other_format == PIX_FMT_NB ||
      space->width != ctx->width ||
      space->height != ctx->height ||
      space->fps != fps) {
    GST_DEBUG_OBJECT (space, "Need caps nego on pad %s for size %dx%d", 
	GST_PAD_NAME (other), ctx->width, ctx->height);
    /* ctx->pix_fmt is set to preferred format */
    if (gst_ffmpegcsp_try_connect (space->sinkpad, ctx, fps) <= 0) {
      av_free (ctx);
      return GST_PAD_LINK_REFUSED;
    }
    *other_format = ctx->pix_fmt;
  }
  space->width = ctx->width;
  space->height = ctx->height;
  space->fps = fps;
  *format = pixfmt;
  av_free (ctx);

  GST_INFO ( "size: %dx%d", space->width, space->height);

  return GST_PAD_LINK_OK;
}

static GType
gst_ffmpegcsp_get_type (void)
{
  static GType ffmpegcsp_type = 0;

  if (!ffmpegcsp_type) {
    static const GTypeInfo ffmpegcsp_info = {
      sizeof (GstFFMpegCspClass),
      gst_ffmpegcsp_base_init,
      NULL,
      gst_ffmpegcsp_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegCsp),
      0,
      gst_ffmpegcsp_init,
    };

    ffmpegcsp_type = g_type_register_static (GST_TYPE_ELEMENT,
					     "GstFFMpegColorspace",
					     &ffmpegcsp_info, 0);

    GST_DEBUG_CATEGORY_INIT (debug_ffmpeg_csp, "ffcolorspace", 0, "FFMpeg colorspace converter");
  }

  return ffmpegcsp_type;
}

static void
gst_ffmpegcsp_base_init (gpointer g_class)
{
  GstCaps *caps, *capscopy;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  /* template caps */
  caps = gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, NULL);
  capscopy = gst_caps_copy (caps);
  
  /* build templates */
  gst_element_class_add_pad_template (element_class, 
      gst_pad_template_new ("src",
			    GST_PAD_SRC,
			    GST_PAD_ALWAYS,
			    caps));
  gst_element_class_add_pad_template (element_class, 
      gst_pad_template_new ("sink",
			    GST_PAD_SINK,
			    GST_PAD_ALWAYS,
			    capscopy));

  gst_element_class_set_details (element_class, &ffmpegcsp_details);
}

static void
gst_ffmpegcsp_class_init (gpointer g_class, gpointer class_data)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  gstelement_class->change_state = gst_ffmpegcsp_change_state;
}

static void
gst_ffmpegcsp_init (GTypeInstance *instance, gpointer g_class)
{
  GstFFMpegCsp *space = GST_FFMPEG_CSP (instance);
  
  space->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (space), "sink"), 
      "sink");
  gst_pad_set_link_function (space->sinkpad, gst_ffmpegcsp_connect);
  gst_pad_set_chain_function (space->sinkpad,gst_ffmpegcsp_chain);
  gst_element_add_pad (GST_ELEMENT(space), space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (space), "src"), 
      "src");
  gst_element_add_pad (GST_ELEMENT (space), space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_ffmpegcsp_connect);

  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;
  space->from_frame = space->to_frame = NULL;
}

static void
gst_ffmpegcsp_chain (GstPad  *pad,
		     GstData *data)
{
  GstFFMpegCsp *space;
  GstBuffer *inbuf = GST_BUFFER (data);
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  space = GST_FFMPEG_CSP (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_FFMPEG_CSP (space));

  if (space->from_pixfmt == PIX_FMT_NB ||
      space->to_pixfmt == PIX_FMT_NB) {
    gst_buffer_unref (inbuf);
    return;
  }

  if (space->from_pixfmt == space->to_pixfmt) {
    outbuf = inbuf;
  } else {
    guint size = avpicture_get_size (space->to_pixfmt,
				     space->width,
				     space->height);
    /* use bufferpools here */
    outbuf = gst_buffer_new_and_alloc (size);

    /* convert */
    avpicture_fill ((AVPicture *) space->from_frame, GST_BUFFER_DATA (inbuf),
		    space->from_pixfmt, space->width, space->height);
    avpicture_fill ((AVPicture *) space->to_frame, GST_BUFFER_DATA (outbuf),
		    space->to_pixfmt, space->width, space->height);
    img_convert ((AVPicture *) space->to_frame, space->to_pixfmt,
		 (AVPicture *) space->from_frame, space->from_pixfmt,
		 space->width, space->height);

    gst_buffer_stamp (outbuf, inbuf);

    gst_buffer_unref (inbuf);
  }

  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_ffmpegcsp_change_state (GstElement *element)
{
  GstFFMpegCsp *space;

  space = GST_FFMPEG_CSP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      space->need_caps_nego = TRUE;
      break;
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

gboolean
gst_ffmpegcsp_register (GstPlugin *plugin)
{
  return gst_element_register (plugin, "ffcolorspace",
			       GST_RANK_NONE, GST_TYPE_FFMPEG_CSP);
}
