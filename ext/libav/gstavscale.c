/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
 * Copyright (C) 2006 Martin Zlomek <martin.zlomek@itonis.tv>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libavcodec/avcodec.h>

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegScale
{
  GstBaseTransform element;

  GstPad *sinkpad, *srcpad;

  gint in_width, in_height;
  gint out_width, out_height;

  enum PixelFormat pixfmt;

  ImgReSampleContext *res;
} GstFFMpegScale;

typedef struct _GstFFMpegScaleClass
{
  GstBaseTransformClass parent_class;
} GstFFMpegScaleClass;

#define GST_TYPE_FFMPEGSCALE \
	(gst_ffmpegscale_get_type())
#define GST_FFMPEGSCALE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGSCALE,GstFFMpegScale))
#define GST_FFMPEGSCALE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGSCALE,GstFFMpegScaleClass))
#define GST_IS_FFMPEGSCALE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGSCALE))
#define GST_IS_FFMPEGSCALE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGSCALE))

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

GST_BOILERPLATE (GstFFMpegScale, gst_ffmpegscale, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_ffmpegscale_finalize (GObject * object);

static GstCaps *gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static void gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_ffmpegscale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static gboolean gst_ffmpegscale_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_ffmpegscale_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static gboolean gst_ffmpegscale_handle_src_event (GstPad * pad,
    GstEvent * event);

static void
gst_ffmpegscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class, "libav Scale element",
      "Filter/Converter/Video/Scaler",
      "Converts video from one resolution to another",
      "Luca Ognibene <luogni@tin.it>");
}

static void
gst_ffmpegscale_class_init (GstFFMpegScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_ffmpegscale_finalize;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_fixate_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_get_unit_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform);

  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_ffmpegscale_init (GstFFMpegScale * scale, GstFFMpegScaleClass * klass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (scale);

  gst_pad_set_event_function (trans->srcpad, gst_ffmpegscale_handle_src_event);

  scale->pixfmt = AV_PIX_FMT_NB;
  scale->res = NULL;
}

static void
gst_ffmpegscale_finalize (GObject * object)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (object);

  if (scale->res != NULL)
    img_resample_close (scale->res);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *retcaps;
  int i;

  retcaps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (retcaps); i++) {
    GstStructure *structure = gst_caps_get_structure (retcaps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  return retcaps;
}

static void
gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *instructure = gst_caps_get_structure (caps, 0);
  GstStructure *outstructure = gst_caps_get_structure (othercaps, 0);
  const GValue *in_par, *out_par;

  in_par = gst_structure_get_value (instructure, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (outstructure, "pixel-aspect-ratio");

  if (in_par && out_par) {
    GValue out_ratio = { 0, };  /* w/h of output video */
    int in_w, in_h, in_par_n, in_par_d, out_par_n, out_par_d;
    int count = 0, w = 0, h = 0, num, den;

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (gst_structure_get_int (outstructure, "width", &w))
      ++count;
    if (gst_structure_get_int (outstructure, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (trans, "dimensions already set to %dx%d, not fixating",
          w, h);
      return;
    }

    gst_structure_get_int (instructure, "width", &in_w);
    gst_structure_get_int (instructure, "height", &in_h);
    in_par_n = gst_value_get_fraction_numerator (in_par);
    in_par_d = gst_value_get_fraction_denominator (in_par);
    out_par_n = gst_value_get_fraction_numerator (out_par);
    out_par_d = gst_value_get_fraction_denominator (out_par);

    g_value_init (&out_ratio, GST_TYPE_FRACTION);
    gst_value_set_fraction (&out_ratio, in_w * in_par_n * out_par_d,
        in_h * in_par_d * out_par_n);
    num = gst_value_get_fraction_numerator (&out_ratio);
    den = gst_value_get_fraction_denominator (&out_ratio);
    GST_DEBUG_OBJECT (trans,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        in_w, in_h, in_par_n, in_par_d, out_par_n, out_par_d);
    GST_DEBUG_OBJECT (trans, "resulting output should respect ratio of %d/%d",
        num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* start with same height, because of interlaced video */
    /* check hd / den is an integer scale factor, and scale wd with the PAR */
    if (in_h % den == 0) {
      GST_DEBUG_OBJECT (trans, "keeping video height");
      h = in_h;
      w = h * num / den;
    } else if (in_w % num == 0) {
      GST_DEBUG_OBJECT (trans, "keeping video width");
      w = in_w;
      h = w * den / num;
    } else {
      GST_DEBUG_OBJECT (trans, "approximating but keeping video height");
      h = in_h;
      w = h * num / den;
    }
    GST_DEBUG_OBJECT (trans, "scaling to %dx%d", w, h);

    /* now fixate */
    gst_structure_fixate_field_nearest_int (outstructure, "width", w);
    gst_structure_fixate_field_nearest_int (outstructure, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (instructure, "width", &width)) {
      if (gst_structure_has_field (outstructure, "width"))
        gst_structure_fixate_field_nearest_int (outstructure, "width", width);
    }
    if (gst_structure_get_int (instructure, "height", &height)) {
      if (gst_structure_has_field (outstructure, "height"))
        gst_structure_fixate_field_nearest_int (outstructure, "height", height);
    }
  }
}

static gboolean
gst_ffmpegscale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint width, height;
  AVCodecContext *ctx;

  if (!gst_structure_get_int (structure, "width", &width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &height))
    return FALSE;

  ctx = avcodec_alloc_context ();
  ctx->width = width;
  ctx->height = height;
  ctx->pix_fmt = AV_PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (CODEC_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == AV_PIX_FMT_NB) {
    av_free (ctx);
    return FALSE;
  }

  *size =
      (guint) av_image_get_buffer_size (pix->pix_fmt, ctx->width, ctx->height,
      1);

  av_free (ctx);

  return TRUE;
}

static gboolean
gst_ffmpegscale_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);
  GstStructure *instructure = gst_caps_get_structure (incaps, 0);
  GstStructure *outstructure = gst_caps_get_structure (outcaps, 0);
  gint par_num, par_den;
  AVCodecContext *ctx;

  if (!gst_structure_get_int (instructure, "width", &scale->in_width))
    return FALSE;
  if (!gst_structure_get_int (instructure, "height", &scale->in_height))
    return FALSE;

  if (!gst_structure_get_int (outstructure, "width", &scale->out_width))
    return FALSE;
  if (!gst_structure_get_int (outstructure, "height", &scale->out_height))
    return FALSE;

  if (gst_structure_get_fraction (instructure, "pixel-aspect-ratio",
          &par_num, &par_den)) {
    gst_structure_set (outstructure,
        "pixel-aspect-ratio", GST_TYPE_FRACTION,
        par_num * scale->in_width / scale->out_width,
        par_den * scale->in_height / scale->out_height, NULL);
  }

  ctx = avcodec_alloc_context ();
  ctx->width = scale->in_width;
  ctx->height = scale->in_height;
  ctx->pix_fmt = AV_PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (CODEC_TYPE_VIDEO, incaps, ctx);
  if (ctx->pix_fmt == AV_PIX_FMT_NB) {
    av_free (ctx);
    return FALSE;
  }

  scale->pixfmt = ctx->pix_fmt;

  av_free (ctx);

  scale->res = img_resample_init (scale->out_width, scale->out_height,
      scale->in_width, scale->in_height);

  return TRUE;
}

static GstFlowReturn
gst_ffmpegscale_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);
  AVPicture in_frame, out_frame;

  gst_buffer_copy_metadata (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);

  gst_ffmpeg_avpicture_fill (&in_frame,
      GST_BUFFER_DATA (inbuf),
      scale->pixfmt, scale->in_width, scale->in_height);

  gst_ffmpeg_avpicture_fill (&out_frame,
      GST_BUFFER_DATA (outbuf),
      scale->pixfmt, scale->out_width, scale->out_height);

  img_resample (scale->res, &out_frame, &in_frame);

  return GST_FLOW_OK;
}

static gboolean
gst_ffmpegscale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegScale *scale;
  GstStructure *structure;
  gdouble pointer;
  gboolean res;

  scale = GST_FFMPEGSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &pointer)) {
        gst_structure_set (structure,
            "pointer_x", G_TYPE_DOUBLE,
            pointer * scale->in_width / scale->out_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &pointer)) {
        gst_structure_set (structure,
            "pointer_y", G_TYPE_DOUBLE,
            pointer * scale->in_height / scale->out_height, NULL);
      }
      break;
    default:
      break;
  }

  res = gst_pad_event_default (pad, event);

  gst_object_unref (scale);

  return res;
}

gboolean
gst_ffmpegscale_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avvideoscale",
      GST_RANK_NONE, GST_TYPE_FFMPEGSCALE);
}
