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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideofilter.h>

#include <math.h>
#include <string.h>

#include <gst/video/video.h>

#define GST_TYPE_QUARKTV \
  (gst_quarktv_get_type())
#define GST_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QUARKTV,GstQuarkTV))
#define GST_QUARKTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QUARKTV,GstQuarkTVClass))
#define GST_IS_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QUARKTV))
#define GST_IS_QUARKTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QUARKTV))

/* number of frames of time-buffer. It should be as a configurable paramater */
/* This number also must be 2^n just for the speed. */
#define PLANES 16

typedef struct _GstQuarkTV GstQuarkTV;
typedef struct _GstQuarkTVClass GstQuarkTVClass;

struct _GstQuarkTV
{
  GstVideoFilter element;

  gint width, height;
  gint area;
  gint planes;
  gint current_plane;
  GstBuffer **planetable;
};

struct _GstQuarkTVClass
{
  GstVideoFilterClass parent_class;
};

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );

static GstStaticPadTemplate gst_quarktv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );

static gboolean
gst_quarktv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstQuarkTV *filter = GST_QUARKTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    gst_quarktv_planetable_clear (filter);
    filter->area = filter->width * filter->height;
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_quarktv_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstQuarkTV *filter = GST_QUARKTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = width * height * 32 / 8;
    ret = TRUE;
    GST_DEBUG_OBJECT (filter, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static inline guint
fastrand (void)
{
  static guint fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

static GstFlowReturn
gst_quarktv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstQuarkTV *filter = GST_QUARKTV (trans);
  gint area;
  guint32 *dest;
  GstFlowReturn ret = GST_FLOW_OK;

  area = filter->area;
  dest = (guint32 *) GST_BUFFER_DATA (out);

  if (G_UNLIKELY (filter->planetable == NULL))
    return GST_FLOW_WRONG_STATE;

  if (filter->planetable[filter->current_plane])
    gst_buffer_unref (filter->planetable[filter->current_plane]);

  filter->planetable[filter->current_plane] = gst_buffer_ref (in);

  /* For each pixel */
  while (--area) {
    GstBuffer *rand;

    /* pick a random buffer */
    rand =
        filter->planetable[(filter->current_plane +
            (fastrand () >> 24)) % filter->planes];

    /* Copy the pixel from the random buffer to dest */
    dest[area] = (rand ? ((guint32 *) GST_BUFFER_DATA (rand))[area] : 0);
  }

  filter->current_plane--;
  if (filter->current_plane < 0)
    filter->current_plane = filter->planes - 1;

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

static GstStateChangeReturn
gst_quarktv_change_state (GstElement * element, GstStateChange transition)
{
  GstQuarkTV *filter = GST_QUARKTV (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      filter->planetable =
          (GstBuffer **) g_malloc0 (filter->planes * sizeof (GstBuffer *));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_quarktv_planetable_clear (filter);
      g_free (filter->planetable);
      filter->planetable = NULL;
      break;
    default:
      break;
  }

  return ret;
}


static void
gst_quarktv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstQuarkTV *filter = GST_QUARKTV (object);

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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_quarktv_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_quarktv_src_template));
}

static void
gst_quarktv_class_init (GstQuarkTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_quarktv_set_property;
  gobject_class->get_property = gst_quarktv_get_property;

  g_object_class_install_property (gobject_class, PROP_PLANES,
      g_param_spec_int ("planes", "Planes",
          "Number of planes", 0, 64, PLANES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_quarktv_change_state);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_quarktv_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_quarktv_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_quarktv_transform);
}

static void
gst_quarktv_init (GstQuarkTV * filter, GstQuarkTVClass * klass)
{
  filter->planes = PLANES;
  filter->current_plane = filter->planes - 1;
}
