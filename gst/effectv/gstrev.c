/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * revTV based on Rutt-Etra Video Synthesizer 1974?

 * (c)2002 Ed Tannenbaum
 *
 * This effect acts like a waveform monitor on each line.
 * It was originally done by deflecting the electron beam on a monitor using
 * additional electromagnets on the yoke of a b/w CRT. 
 * Here it is emulated digitally.

 * Experimaental tapes were made with this system by Bill and 
 * Louise Etra and Woody and Steina Vasulka

 * The line spacing can be controlled using the 1 and 2 Keys.
 * The gain is controlled using the 3 and 4 keys.
 * The update rate is controlled using the 0 and - keys.
 
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

/**
 * SECTION:element-quarktv
 *
 * RevTV acts like a video waveform monitor for each line of video
 * processed. This creates a pseudo 3D effect based on the brightness
 * of the video along each line.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! revtv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of revtv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstrev.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

#define THE_COLOR 0xffffffff

enum
{
  PROP_0,
  PROP_DELAY,
  PROP_LINESPACE,
  PROP_GAIN
};

GST_BOILERPLATE (GstRevTV, gst_revtv, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_RGBx
#else
#define CAPS_STR GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_xRGB
#endif

static GstStaticPadTemplate gst_revtv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate gst_revtv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static gboolean
gst_revtv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRevTV *filter = GST_REVTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static GstFlowReturn
gst_revtv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstRevTV *filter = GST_REVTV (trans);
  guint32 *src, *dest;
  gint width, height;
  guint32 *nsrc;
  gint y, x, R, G, B, yval;
  GstFlowReturn ret = GST_FLOW_OK;
  gint linespace, vscale;
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (filter), stream_time);

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  GST_OBJECT_LOCK (filter);
  width = filter->width;
  height = filter->height;

  /* Clear everything to black */
  memset (dest, 0, width * height * sizeof (guint32));

  linespace = filter->linespace;
  vscale = filter->vscale;

  /* draw the offset lines */
  for (y = 0; y < height; y += linespace) {
    for (x = 0; x <= width; x++) {
      nsrc = src + (y * width) + x;

      /* Calc Y Value for curpix */
      R = ((*nsrc) & 0xff0000) >> (16 - 1);
      G = ((*nsrc) & 0xff00) >> (8 - 2);
      B = (*nsrc) & 0xff;

      yval = y - ((short) (R + G + B) / vscale);

      if (yval > 0) {
        dest[x + (yval * width)] = THE_COLOR;
      }
    }
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static void
gst_revtv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRevTV *filter = GST_REVTV (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_DELAY:
      filter->vgrabtime = g_value_get_int (value);
      break;
    case PROP_LINESPACE:
      filter->linespace = g_value_get_int (value);
      break;
    case PROP_GAIN:
      filter->vscale = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_revtv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRevTV *filter = GST_REVTV (object);

  switch (prop_id) {
    case PROP_DELAY:
      g_value_set_int (value, filter->vgrabtime);
      break;
    case PROP_LINESPACE:
      g_value_set_int (value, filter->linespace);
      break;
    case PROP_GAIN:
      g_value_set_int (value, filter->vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_revtv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "RevTV effect",
      "Filter/Effect/Video",
      "A video waveform monitor for each line of video processed",
      "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_revtv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_revtv_src_template);
}

static void
gst_revtv_class_init (GstRevTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_revtv_set_property;
  gobject_class->get_property = gst_revtv_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELAY,
      g_param_spec_int ("delay", "Delay", "Delay in frames between updates",
          1, 100, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LINESPACE,
      g_param_spec_int ("linespace", "Linespace", "Control line spacing", 1,
          100, 6,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_GAIN,
      g_param_spec_int ("gain", "Gain", "Control gain", 1, 200, 50,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_revtv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_revtv_transform);
}

static void
gst_revtv_init (GstRevTV * restv, GstRevTVClass * klass)
{
  restv->vgrabtime = 1;
  restv->vgrab = 0;
  restv->linespace = 6;
  restv->vscale = 50;
}
