/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxmpeg4videoenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mpeg4_video_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_mpeg4_video_enc_debug_category

/* prototypes */
static void gst_omx_mpeg4_video_enc_finalize (GObject * object);
static gboolean gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);
static GstCaps *gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mpeg4_video_enc_debug_category, "omxmpeg4videoenc", 0, \
      "debug category for gst-omx video encoder base class");

GST_BOILERPLATE_FULL (GstOMXMPEG4VideoEnc, gst_omx_mpeg4_video_enc,
    GstOMXVideoEnc, GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_mpeg4_video_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX MPEG4 Video Encoder",
      "Codec/Encoder/Video",
      "Encode MPEG4 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_omx_mpeg4_video_enc_class_init (GstOMXMPEG4VideoEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  gobject_class->finalize = gst_omx_mpeg4_video_enc_finalize;

  videoenc_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_set_format);
  videoenc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_get_caps);

  videoenc_class->default_src_template_caps = "video/mpeg, "
      "mpegversion=(int) 4, "
      "systemstream=(boolean) false, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->default_sink_template_caps = GST_VIDEO_CAPS_YUV ("I420");
}

static void
gst_omx_mpeg4_video_enc_init (GstOMXMPEG4VideoEnc * self,
    GstOMXMPEG4VideoEncClass * klass)
{
}

static void
gst_omx_mpeg4_video_enc_finalize (GObject * object)
{
  /* GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  return TRUE;
}

static GstCaps *
gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstCaps *caps;

  caps =
      gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE, "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height, NULL);

  if (state->fps_n != 0)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, state->fps_n,
        state->fps_d, NULL);
  if (state->par_n != 1 || state->par_d != 1)
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        state->par_n, state->par_d, NULL);

  return caps;
}
