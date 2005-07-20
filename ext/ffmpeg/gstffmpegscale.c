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

#define GST_TYPE_FFMPEGSCALE \
  (gst_ffmpegscale_get_type())
#define GST_FFMPEGSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGSCALE,GstFFMpegScale))
#define GST_FFMPEGSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGSCALE,GstFFMpegScale))
#define GST_IS_FFMPEGSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGSCALE))
#define GST_IS_FFMPEGSCALE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGSCALE))

typedef struct _GstFFMpegScale GstFFMpegScale;
typedef struct _GstFFMpegScaleClass GstFFMpegScaleClass;

struct _GstFFMpegScale
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint from_width, from_height;
  gint to_width, to_height;

  enum PixelFormat pixfmt;
  AVPicture from_frame, to_frame;
  GstCaps *sinkcaps;
  
  guint to_size;

  ImgReSampleContext *res;

  gboolean passthru;
};

struct _GstFFMpegScaleClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegscale_details = {
  "FFMPEG Scale element",
  "Filter/Converter/Video",
  "Converts video from one resolution to another",
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

static GstStaticPadTemplate gst_ffmpegscale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_ffmpegscale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


static GType gst_ffmpegscale_get_type (void);

static void gst_ffmpegscale_base_init (GstFFMpegScaleClass * klass);
static void gst_ffmpegscale_class_init (GstFFMpegScaleClass * klass);
static void gst_ffmpegscale_init (GstFFMpegScale * space);

static void gst_ffmpegscale_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegscale_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_ffmpegscale_pad_link (GstPad * pad, const GstCaps * caps);

static void gst_ffmpegscale_chain (GstPad * pad, GstData * data);
static GstElementStateReturn gst_ffmpegscale_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegscale_signals[LAST_SIGNAL] = { 0 }; */

static GstCaps *
gst_ffmpegscale_getcaps (GstPad * pad)
{
  GstFFMpegScale *filter;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;
  gint i;
  
  filter = GST_FFMPEGSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free (othercaps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  GST_DEBUG ("getcaps are: %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstPadLinkReturn
gst_ffmpegscale_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  AVCodecContext *ctx;
  GstFFMpegScale *scale;
  GstPad *otherpad;
  GstPadLinkReturn ret;
  int height, width;
  gchar *caps_string;

  caps_string = gst_caps_to_string (caps);
  GST_DEBUG ("ffmpegscale _link %s\n", caps_string);
  g_free (caps_string);

  scale = GST_FFMPEGSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == scale->srcpad) ? scale->sinkpad :
      scale->srcpad;

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

  scale->pixfmt = ctx->pix_fmt;
  av_free (ctx);

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (ret == GST_PAD_LINK_OK) {
    /* cool, we can use passthru */

    scale->to_width = width;
    scale->to_height = height;
    scale->from_width = width;
    scale->from_height = height;

    scale->passthru = TRUE;
    GST_FLAG_SET (scale, GST_ELEMENT_WORK_IN_PLACE);

    return GST_PAD_LINK_OK;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *newcaps = gst_caps_copy (caps);

    if (pad == scale->srcpad) {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, scale->from_width,
          "height", G_TYPE_INT, scale->from_height, NULL);
    } else {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, scale->to_width,
          "height", G_TYPE_INT, scale->to_height, NULL);
    }
    ret = gst_pad_try_set_caps (otherpad, newcaps);
    gst_caps_free (newcaps);

    if (GST_PAD_LINK_FAILED (ret)) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  scale->passthru = FALSE;
  GST_FLAG_UNSET (scale, GST_ELEMENT_WORK_IN_PLACE);

  if (pad == scale->srcpad) {
    scale->to_width = width;
    scale->to_height = height;
        
  } else {
    scale->from_width = width;
    scale->from_height = height;
  }  

  if (gst_pad_is_negotiated (otherpad)) {

    scale->to_size = avpicture_get_size (scale->pixfmt, scale->to_width, scale->to_height);

    if (scale->res != NULL) img_resample_close (scale->res);

    scale->res = img_resample_init (scale->to_width, scale->to_height,
				    scale->from_width, scale->from_height);

  }

  return GST_PAD_LINK_OK;
}

static GType
gst_ffmpegscale_get_type (void)
{
  static GType ffmpegscale_type = 0;

  if (!ffmpegscale_type) {
    static const GTypeInfo ffmpegscale_info = {
      sizeof (GstFFMpegScaleClass),
      (GBaseInitFunc) gst_ffmpegscale_base_init,
      NULL,
      (GClassInitFunc) gst_ffmpegscale_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegScale),
      0,
      (GInstanceInitFunc) gst_ffmpegscale_init,
    };

    ffmpegscale_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFFMpegScale", &ffmpegscale_info, 0);
  }

  return ffmpegscale_type;
}

static void
gst_ffmpegscale_base_init (GstFFMpegScaleClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &ffmpegscale_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegscale_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegscale_sink_template));
}

static void
gst_ffmpegscale_class_init (GstFFMpegScaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ffmpegscale_set_property;
  gobject_class->get_property = gst_ffmpegscale_get_property;

  gstelement_class->change_state = gst_ffmpegscale_change_state;
}

static void
gst_ffmpegscale_init (GstFFMpegScale * scale)
{
  scale->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get
					      (&gst_ffmpegscale_sink_template), 
					      "sink");
  gst_pad_set_link_function (scale->sinkpad, gst_ffmpegscale_pad_link);
  gst_pad_set_getcaps_function (scale->sinkpad, gst_ffmpegscale_getcaps);
  gst_pad_set_chain_function (scale->sinkpad, gst_ffmpegscale_chain);
  gst_element_add_pad (GST_ELEMENT (scale), scale->sinkpad);

  scale->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
					     (&gst_ffmpegscale_src_template), 
					     "src");
  gst_element_add_pad (GST_ELEMENT (scale), scale->srcpad);
  gst_pad_set_link_function (scale->srcpad, gst_ffmpegscale_pad_link);
  gst_pad_set_getcaps_function (scale->srcpad, gst_ffmpegscale_getcaps);

  scale->pixfmt = PIX_FMT_NB;
  scale->passthru = FALSE;
  scale->res = NULL;
}

static void
gst_ffmpegscale_chain (GstPad * pad, GstData * data)
{
  GstBuffer *inbuf = GST_BUFFER (data);
  GstFFMpegScale *scale;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  scale = GST_FFMPEGSCALE (gst_pad_get_parent (pad));

  g_return_if_fail (scale != NULL);
  g_return_if_fail (GST_IS_FFMPEGSCALE (scale));

  if (!GST_PAD_IS_USABLE (scale->srcpad)) {
    gst_buffer_unref (inbuf);
    return;
  }
  
  if (scale->passthru == TRUE) {
    gst_pad_push (scale->srcpad, GST_DATA (inbuf));
    
    return ;
  }
  
  outbuf = gst_pad_alloc_buffer (scale->srcpad, GST_BUFFER_OFFSET_NONE, scale->to_size);

  gst_ffmpeg_avpicture_fill (&scale->from_frame,
			     GST_BUFFER_DATA (inbuf),
			     scale->pixfmt, 
			     scale->from_width, scale->from_height);

  gst_ffmpeg_avpicture_fill (&scale->to_frame,
			     GST_BUFFER_DATA (outbuf),
			     scale->pixfmt, 
			     scale->to_width, scale->to_height);    
  
  img_resample (scale->res, &scale->to_frame, &scale->from_frame);    
  
  gst_buffer_stamp (outbuf, (const GstBuffer *) inbuf);

  gst_buffer_unref (inbuf);
  gst_pad_push (scale->srcpad, GST_DATA (outbuf));
  
}

static GstElementStateReturn
gst_ffmpegscale_change_state (GstElement * element)
{
  GstFFMpegScale *scale;

  scale = GST_FFMPEGSCALE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_NULL:
      if (scale->res != NULL)
	img_resample_close (scale->res);
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ffmpegscale_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegScale *scale;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGSCALE (object));
  scale = GST_FFMPEGSCALE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_ffmpegscale_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegScale *scale;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGSCALE (object));
  scale = GST_FFMPEGSCALE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegscale_register (GstPlugin * plugin)
{

  return gst_element_register (plugin, "ffvideoscale",
      GST_RANK_NONE, GST_TYPE_FFMPEGSCALE);
}
