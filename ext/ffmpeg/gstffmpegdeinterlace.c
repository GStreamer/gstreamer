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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_FFMPEG_UNINSTALLED
#  include <avcodec.h>
#else
#  include <libavcodec/avcodec.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"

typedef struct _GstFFMpegDeinterlace
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint to_size;

  enum PixelFormat pixfmt;
  AVPicture from_frame, to_frame;
} GstFFMpegDeinterlace;

typedef struct _GstFFMpegDeinterlaceClass
{
  GstElementClass parent_class;
} GstFFMpegDeinterlaceClass;

#define GST_TYPE_FFMPEGDEINTERLACE \
  (gst_ffmpegdeinterlace_get_type())
#define GST_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_FFMPEGDEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_IS_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEINTERLACE))
#define GST_IS_FFMPEGDEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEINTERLACE))

GType gst_ffmpegdeinterlace_get_type (void);

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

GST_BOILERPLATE (GstFFMpegDeinterlace, gst_ffmpegdeinterlace, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn gst_ffmpegdeinterlace_chain (GstPad * pad,
    GstBuffer * inbuf);

static void
gst_ffmpegdeinterlace_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class,
      "FFMPEG Deinterlace element", "Filter/Effect/Video/Deinterlace",
      "Deinterlace video", "Luca Ognibene <luogni@tin.it>");
}

static void
gst_ffmpegdeinterlace_class_init (GstFFMpegDeinterlaceClass * klass)
{
}

static gboolean
gst_ffmpegdeinterlace_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegDeinterlace *deinterlace =
      GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  AVCodecContext *ctx;
  GstCaps *src_caps;
  gboolean ret;

  if (!gst_structure_get_int (structure, "width", &deinterlace->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &deinterlace->height))
    return FALSE;

  ctx = avcodec_alloc_context ();
  ctx->width = deinterlace->width;
  ctx->height = deinterlace->height;
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (AVMEDIA_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB) {
    av_free (ctx);
    return FALSE;
  }

  deinterlace->pixfmt = ctx->pix_fmt;

  av_free (ctx);

  deinterlace->to_size =
      avpicture_get_size (deinterlace->pixfmt, deinterlace->width,
      deinterlace->height);

  src_caps = gst_caps_copy (caps);
  gst_caps_set_simple (src_caps, "interlaced", G_TYPE_BOOLEAN, FALSE, NULL);
  ret = gst_pad_set_caps (deinterlace->srcpad, src_caps);
  gst_caps_unref (src_caps);

  return ret;
}

static void
gst_ffmpegdeinterlace_init (GstFFMpegDeinterlace * deinterlace,
    GstFFMpegDeinterlaceClass * klass)
{
  deinterlace->sinkpad =
      gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (deinterlace->sinkpad,
      gst_ffmpegdeinterlace_sink_setcaps);
  gst_pad_set_chain_function (deinterlace->sinkpad,
      gst_ffmpegdeinterlace_chain);
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->sinkpad);

  deinterlace->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->srcpad);

  deinterlace->pixfmt = PIX_FMT_NB;
}

static GstFlowReturn
gst_ffmpegdeinterlace_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFFMpegDeinterlace *deinterlace =
      GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));
  GstBuffer *outbuf = NULL;
  GstFlowReturn result;

  result =
      gst_pad_alloc_buffer (deinterlace->srcpad, GST_BUFFER_OFFSET_NONE,
      deinterlace->to_size, GST_PAD_CAPS (deinterlace->srcpad), &outbuf);
  if (result == GST_FLOW_OK) {
    gst_ffmpeg_avpicture_fill (&deinterlace->from_frame,
        GST_BUFFER_DATA (inbuf), deinterlace->pixfmt, deinterlace->width,
        deinterlace->height);

    gst_ffmpeg_avpicture_fill (&deinterlace->to_frame, GST_BUFFER_DATA (outbuf),
        deinterlace->pixfmt, deinterlace->width, deinterlace->height);

    avpicture_deinterlace (&deinterlace->to_frame, &deinterlace->from_frame,
        deinterlace->pixfmt, deinterlace->width, deinterlace->height);

    gst_buffer_copy_metadata (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);

    result = gst_pad_push (deinterlace->srcpad, outbuf);
  }

  gst_buffer_unref (inbuf);

  return result;
}

gboolean
gst_ffmpegdeinterlace_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "ffdeinterlace",
      GST_RANK_NONE, GST_TYPE_FFMPEGDEINTERLACE);
}
