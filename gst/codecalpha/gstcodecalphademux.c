/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

/**
 * SECTION:element-codecalphademux
 * @title: CODEC Alpha Demuxer
 *
 * Extract the CODEC (typically VP8/VP9) alpha stream stored at meta and
 * expose it as a stream. This element allow using single stream VP9 decoders
 * in order to decode both streams.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=transparency.webm ! matroskademux ! 
 *     codecalphademux name=d
 *     d.video ! queue ! vp9dec ! autovideosink
 *     d.alpha ! queue ! vp9dec ! autovideosink
 * ]| This pipeline splits and decode the video and the alpha stream, showing
 *    the result on seperate window.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include "gstcodecalphademux.h"

GST_DEBUG_CATEGORY_STATIC (codecalphademux_debug);
#define GST_CAT_DEFAULT (codecalphademux_debug)

struct _GstCodecAlphaDemux
{
  GstElement parent;
};

#define gst_codec_alpha_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCodecAlphaDemux, gst_codec_alpha_demux, 
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (codecalphademux_debug, "codecalphademux", 0,
        "codecalphademux"));

GST_ELEMENT_REGISTER_DEFINE (codec_alpha_demux, "codecalphademux",
    GST_RANK_NONE, GST_TYPE_CODEC_ALPHA_DEMUX);

static GstStaticPadTemplate gst_codec_alpha_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_codec_alpha_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_codec_alpha_demux_alpha_template =
GST_STATIC_PAD_TEMPLATE ("alpha",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static void
gst_codec_alpha_demux_class_init (GstCodecAlphaDemuxClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (element_class,
      "CODEC Alpha Demuxer", "Codec/Demuxer",
      "Extract and expose as a stream the CODEC alpha.",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_codec_alpha_demux_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_codec_alpha_demux_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_codec_alpha_demux_alpha_template);
}

static void
gst_codec_alpha_demux_init (GstCodecAlphaDemux * demux)
{
}
