/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
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

#include <gst/video/video.h>
#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

#define GST_TYPE_FFMPEGDEINTERLACE \
  (gst_ffmpegdeinterlace_get_type())
#define GST_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_FFMPEGDEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_IS_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEINTERLACE))
#define GST_IS_FFMPEGDEINTERLACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEINTERLACE))

typedef struct _GstFFMpegDeinterlace GstFFMpegDeinterlace;
typedef struct _GstFFMpegDeinterlaceClass GstFFMpegDeinterlaceClass;

struct _GstFFMpegDeinterlace
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;

  enum PixelFormat pixfmt;
  AVPicture from_frame, to_frame;
  GstCaps *sinkcaps;
  
  guint to_size;

};

struct _GstFFMpegDeinterlaceClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegdeinterlace_details = {
  "FFMPEG Deinterlace element",
  "Filter/Converter/Video",
  "Deinterlace video",
  "Luca Ognibene <luogni@tin.it>",
};

/* Signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate gst_ffmpegdeinterlace_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_ffmpegdeinterlace_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


static GType gst_ffmpegdeinterlace_get_type (void);

static void gst_ffmpegdeinterlace_base_init (GstFFMpegDeinterlaceClass * klass);
static void gst_ffmpegdeinterlace_class_init (GstFFMpegDeinterlaceClass * klass);
static void gst_ffmpegdeinterlace_init (GstFFMpegDeinterlace * space);

static void gst_ffmpegdeinterlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegdeinterlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_ffmpegdeinterlace_pad_link (GstPad * pad, const GstCaps * caps);

static void gst_ffmpegdeinterlace_chain (GstPad * pad, GstData * data);
static GstElementStateReturn gst_ffmpegdeinterlace_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdeinterlace_signals[LAST_SIGNAL] = { 0 }; */

static GstCaps *
gst_ffmpegdeinterlace_getcaps (GstPad * pad)
{
  GstFFMpegDeinterlace *filter;
  GstPad *otherpad;
  
  filter = GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  return gst_pad_get_allowed_caps (otherpad);
}

static GstPadLinkReturn
gst_ffmpegdeinterlace_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  AVCodecContext *ctx;
  GstFFMpegDeinterlace *deinterlace;
  GstPad *otherpad;
  GstPadLinkReturn ret;
  int height, width;

  deinterlace = GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));

  otherpad = (pad == deinterlace->srcpad) ? deinterlace->sinkpad :
      deinterlace->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);

  ctx = avcodec_alloc_context ();
  ctx->width = width;
  ctx->height = height;
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (CODEC_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB) {
    av_free (ctx);

    /* we disable ourself here */
    return GST_PAD_LINK_REFUSED;
  }

  deinterlace->pixfmt = ctx->pix_fmt;
  av_free (ctx);

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (ret)) {
    return ret;
  }

  deinterlace->width = width;
  deinterlace->height = height;
  deinterlace->to_size = avpicture_get_size (deinterlace->pixfmt, 
					     deinterlace->width, deinterlace->height);
  return GST_PAD_LINK_OK;
}

static GType
gst_ffmpegdeinterlace_get_type (void)
{
  static GType ffmpegdeinterlace_type = 0;

  if (!ffmpegdeinterlace_type) {
    static const GTypeInfo ffmpegdeinterlace_info = {
      sizeof (GstFFMpegDeinterlaceClass),
      (GBaseInitFunc) gst_ffmpegdeinterlace_base_init,
      NULL,
      (GClassInitFunc) gst_ffmpegdeinterlace_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegDeinterlace),
      0,
      (GInstanceInitFunc) gst_ffmpegdeinterlace_init,
    };

    ffmpegdeinterlace_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFFMpegDeinterlace", &ffmpegdeinterlace_info, 0);
  }

  return ffmpegdeinterlace_type;
}

static void
gst_ffmpegdeinterlace_base_init (GstFFMpegDeinterlaceClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &ffmpegdeinterlace_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegdeinterlace_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegdeinterlace_sink_template));
}

static void
gst_ffmpegdeinterlace_class_init (GstFFMpegDeinterlaceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ffmpegdeinterlace_set_property;
  gobject_class->get_property = gst_ffmpegdeinterlace_get_property;

  gstelement_class->change_state = gst_ffmpegdeinterlace_change_state;
}

static void
gst_ffmpegdeinterlace_init (GstFFMpegDeinterlace * deinterlace)
{
  deinterlace->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get
					      (&gst_ffmpegdeinterlace_sink_template), 
					      "sink");
  gst_pad_set_link_function (deinterlace->sinkpad, gst_ffmpegdeinterlace_pad_link);
  gst_pad_set_getcaps_function (deinterlace->sinkpad, gst_ffmpegdeinterlace_getcaps);
  gst_pad_set_chain_function (deinterlace->sinkpad, gst_ffmpegdeinterlace_chain);
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->sinkpad);

  deinterlace->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
					     (&gst_ffmpegdeinterlace_src_template), 
					     "src");
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->srcpad);
  gst_pad_set_link_function (deinterlace->srcpad, gst_ffmpegdeinterlace_pad_link);
  gst_pad_set_getcaps_function (deinterlace->srcpad, gst_ffmpegdeinterlace_getcaps);

  deinterlace->pixfmt = PIX_FMT_NB;
}

static void
gst_ffmpegdeinterlace_chain (GstPad * pad, GstData * data)
{
  GstBuffer *inbuf = GST_BUFFER (data);
  GstFFMpegDeinterlace *deinterlace;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  deinterlace = GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));

  g_return_if_fail (deinterlace != NULL);
  g_return_if_fail (GST_IS_FFMPEGDEINTERLACE (deinterlace));

  if (!GST_PAD_IS_USABLE (deinterlace->srcpad)) {
    gst_buffer_unref (inbuf);
    return;
  }

  outbuf = gst_pad_alloc_buffer (deinterlace->srcpad, 
				 GST_BUFFER_OFFSET_NONE, deinterlace->to_size);

  gst_ffmpeg_avpicture_fill (&deinterlace->from_frame,
			     GST_BUFFER_DATA (inbuf),
			     deinterlace->pixfmt, 
			     deinterlace->width, deinterlace->height);

  gst_ffmpeg_avpicture_fill (&deinterlace->to_frame,
			     GST_BUFFER_DATA (outbuf),
			     deinterlace->pixfmt, 
			     deinterlace->width, deinterlace->height);    

  avpicture_deinterlace (&deinterlace->to_frame, &deinterlace->from_frame,
			 deinterlace->pixfmt, deinterlace->width, deinterlace->height);

  gst_buffer_stamp (outbuf, (const GstBuffer *) inbuf);
  
  gst_buffer_unref (inbuf);
  gst_pad_push (deinterlace->srcpad, GST_DATA (outbuf));
  
}

static GstElementStateReturn
gst_ffmpegdeinterlace_change_state (GstElement * element)
{
  GstFFMpegDeinterlace *deinterlace;

  deinterlace = GST_FFMPEGDEINTERLACE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ffmpegdeinterlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegDeinterlace *deinterlace;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGDEINTERLACE (object));
  deinterlace = GST_FFMPEGDEINTERLACE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_ffmpegdeinterlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegDeinterlace *deinterlace;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGDEINTERLACE (object));
  deinterlace = GST_FFMPEGDEINTERLACE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegdeinterlace_register (GstPlugin * plugin)
{

  return gst_element_register (plugin, "ffdeinterlace",
      GST_RANK_NONE, GST_TYPE_FFMPEGDEINTERLACE);
}
