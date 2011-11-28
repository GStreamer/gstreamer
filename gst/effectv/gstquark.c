/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * EffecTV:
 * Copyright (C) 2001-2002 FUKUCHI Kentarou
 *
 * QuarkTV - motion disolver.
 *
 *  EffecTV is free software. This library is free software;
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
 * QuarkTV disolves moving objects. It picks up pixels from
 * the last frames randomly.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! quarktv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of quarktv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstquark.h"
#include "gsteffectv.h"

#include <gst/controller/gstcontroller.h>
#include <gst/video/video.h>

/* number of frames of time-buffer. It should be as a configurable paramater */
/* This number also must be 2^n just for the speed. */
#define PLANES 16

enum
{
  PROP_0,
  PROP_PLANES
};

GST_BOILERPLATE (GstQuarkTV, gst_quarktv, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void gst_quarktv_planetable_clear (GstQuarkTV * filter);

static GstStaticPadTemplate gst_quarktv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );

static GstStaticPadTemplate gst_quarktv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );

static gboolean
gst_quarktv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstQuarkTV *filter = GST_QUARKTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    gst_quarktv_planetable_clear (filter);
    filter->area = filter->width * filter->height;
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static GstFlowReturn
gst_quarktv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstQuarkTV *filter = GST_QUARKTV (trans);
  gint area;
  guint32 *src, *dest;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp;
  GstBuffer **planetable;
  gint planes, current_plane;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  timestamp =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (filter), timestamp);

  if (G_UNLIKELY (filter->planetable == NULL))
    return GST_FLOW_WRONG_STATE;

  GST_OBJECT_LOCK (filter);
  area = filter->area;
  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);
  planetable = filter->planetable;
  planes = filter->planes;
  current_plane = filter->current_plane;

  if (planetable[current_plane])
    gst_buffer_unref (planetable[current_plane]);
  planetable[current_plane] = gst_buffer_ref (in);

  /* For each pixel */
  while (--area) {
    GstBuffer *rand;

    /* pick a random buffer */
    rand = planetable[(current_plane + (fastrand () >> 24)) % planes];

    /* Copy the pixel from the random buffer to dest */
    dest[area] =
        (rand ? ((guint32 *) GST_BUFFER_DATA (rand))[area] : src[area]);
  }

  filter->current_plane--;
  if (filter->current_plane < 0)
    filter->current_plane = planes - 1;
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static void
gst_quarktv_planetable_clear (GstQuarkTV * filter)
{
  gint i;

  if (filter->planetable == NULL)
    return;

  for (i = 0; i < filter->planes; i++) {
    if (GST_IS_BUFFER (filter->planetable[i])) {
      gst_buffer_unref (filter->planetable[i]);
    }
    filter->planetable[i] = NULL;
  }
}

static gboolean
gst_quarktv_start (GstBaseTransform * trans)
{
  GstQuarkTV *filter = GST_QUARKTV (trans);

  if (filter->planetable) {
    gst_quarktv_planetable_clear (filter);
    g_free (filter->planetable);
  }
  filter->planetable =
      (GstBuffer **) g_malloc0 (filter->planes * sizeof (GstBuffer *));

  return TRUE;
}

static void
gst_quarktv_finalize (GObject * object)
{
  GstQuarkTV *filter = GST_QUARKTV (object);

  if (filter->planetable) {
    gst_quarktv_planetable_clear (filter);
    g_free (filter->planetable);
    filter->planetable = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_quarktv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstQuarkTV *filter = GST_QUARKTV (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_PLANES:
    {
      gint new_n_planes = g_value_get_int (value);
      GstBuffer **new_planetable;
      gint i;

      /* If the number of planes changed, copy across any existing planes */
      if (new_n_planes != filter->planes) {
        new_planetable =
            (GstBuffer **) g_malloc0 (new_n_planes * sizeof (GstBuffer *));

        if (filter->planetable) {
          for (i = 0; (i < new_n_planes) && (i < filter->planes); i++) {
            new_planetable[i] = filter->planetable[i];
          }
          for (; i < filter->planes; i++) {
            if (filter->planetable[i])
              gst_buffer_unref (filter->planetable[i]);
          }
          g_free (filter->planetable);
        }

        filter->planetable = new_planetable;
        filter->planes = new_n_planes;
        filter->current_plane = filter->planes - 1;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_quarktv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstQuarkTV *filter = GST_QUARKTV (object);

  switch (prop_id) {
    case PROP_PLANES:
      g_value_set_int (value, filter->planes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_quarktv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "QuarkTV effect",
      "Filter/Effect/Video",
      "Motion dissolver", "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_quarktv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_quarktv_src_template);
}

static void
gst_quarktv_class_init (GstQuarkTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_quarktv_set_property;
  gobject_class->get_property = gst_quarktv_get_property;

  gobject_class->finalize = gst_quarktv_finalize;

  g_object_class_install_property (gobject_class, PROP_PLANES,
      g_param_spec_int ("planes", "Planes",
          "Number of planes", 0, 64, PLANES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_quarktv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_quarktv_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_quarktv_start);
}

static void
gst_quarktv_init (GstQuarkTV * filter, GstQuarkTVClass * klass)
{
  filter->planes = PLANES;
  filter->current_plane = filter->planes - 1;
}
