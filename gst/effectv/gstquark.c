/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
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
#include <math.h>
#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#define GST_TYPE_QUARKTV \
  (gst_quarktv_get_type())
#define GST_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QUARKTV,GstQuarkTV))
#define GST_QUARKTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QUARKTV,GstQuarkTVClass))
#define GST_IS_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QUARKTV))
#define GST_IS_QUARKTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QUARKTV))

/* number of frames of time-buffer. It should be as a configurable paramter */
/* This number also must be 2^n just for the speed. */
#define PLANES 16

typedef struct _GstQuarkTV GstQuarkTV;
typedef struct _GstQuarkTVClass GstQuarkTVClass;

struct _GstQuarkTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint area;
  gint planes;
  gint current_plane;
  GstBuffer **planetable;
};

struct _GstQuarkTVClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_quarktv_details = GST_ELEMENT_DETAILS ("QuarkTV",
    "Filter/Effect/Video",
    "Motion dissolver",
    "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PLANES,
};

static void gst_quarktv_base_init (gpointer g_class);
static void gst_quarktv_class_init (GstQuarkTVClass * klass);
static void gst_quarktv_init (GstQuarkTV * filter);

static GstElementStateReturn gst_quarktv_change_state (GstElement * element);

static void gst_quarktv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_quarktv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_quarktv_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

/* static guint gst_quarktv_signals[LAST_SIGNAL] = { 0 }; */

static inline guint32
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

GType
gst_quarktv_get_type (void)
{
  static GType quarktv_type = 0;

  if (!quarktv_type) {
    static const GTypeInfo quarktv_info = {
      sizeof (GstQuarkTVClass),
      gst_quarktv_base_init,
      NULL,
      (GClassInitFunc) gst_quarktv_class_init,
      NULL,
      NULL,
      sizeof (GstQuarkTV),
      0,
      (GInstanceInitFunc) gst_quarktv_init,
    };

    quarktv_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstQuarkTV", &quarktv_info,
        0);
  }
  return quarktv_type;
}

static void
gst_quarktv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_sink_template));

  gst_element_class_set_details (element_class, &gst_quarktv_details);
}

static void
gst_quarktv_class_init (GstQuarkTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PLANES,
      g_param_spec_int ("planes", "Planes", "Number of frames in the buffer",
          1, 32, PLANES, G_PARAM_READWRITE));

  gobject_class->set_property = gst_quarktv_set_property;
  gobject_class->get_property = gst_quarktv_get_property;

  gstelement_class->change_state = gst_quarktv_change_state;
}

static GstPadLinkReturn
gst_quarktv_link (GstPad * pad, const GstCaps * caps)
{
  GstQuarkTV *filter;
  GstPad *otherpad;
  gint i;
  GstStructure *structure;
  GstPadLinkReturn res;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_IS_QUARKTV (filter), GST_PAD_LINK_REFUSED);

  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

  res = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (res))
    return res;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &filter->width) ||
      !gst_structure_get_int (structure, "height", &filter->height))
    return GST_PAD_LINK_REFUSED;

  filter->area = filter->width * filter->height;

  for (i = 0; i < filter->planes; i++) {
    if (filter->planetable[i])
      gst_buffer_unref (filter->planetable[i]);
    filter->planetable[i] = NULL;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_quarktv_init (GstQuarkTV * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_effectv_sink_template), "sink");
  gst_pad_set_getcaps_function (filter->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_chain_function (filter->sinkpad, gst_quarktv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_quarktv_link);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_effectv_src_template), "src");
  gst_pad_set_getcaps_function (filter->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (filter->srcpad, gst_quarktv_link);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->planes = PLANES;
  filter->current_plane = filter->planes - 1;
  filter->planetable =
      (GstBuffer **) g_malloc (filter->planes * sizeof (GstBuffer *));
  memset (filter->planetable, 0, filter->planes * sizeof (GstBuffer *));
}

static void
gst_quarktv_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstQuarkTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;
  gint area;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  area = filter->area;

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = area * sizeof (guint32);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  dest = (guint32 *) GST_BUFFER_DATA (outbuf);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  if (filter->planetable[filter->current_plane])
    gst_buffer_unref (filter->planetable[filter->current_plane]);

  filter->planetable[filter->current_plane] = buf;

  while (--area) {
    GstBuffer *rand;

    /* pick a random buffer */
    rand =
        filter->planetable[(filter->current_plane +
            (fastrand () >> 24)) & (filter->planes - 1)];

    dest[area] = (rand ? ((guint32 *) GST_BUFFER_DATA (rand))[area] : 0);
  }

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));

  filter->current_plane--;

  if (filter->current_plane < 0)
    filter->current_plane = filter->planes - 1;
}

static GstElementStateReturn
gst_quarktv_change_state (GstElement * element)
{
  GstQuarkTV *filter = GST_QUARKTV (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
    {
      gint i;

      for (i = 0; i < filter->planes; i++) {
        if (filter->planetable[i])
          gst_buffer_unref (filter->planetable[i]);
        filter->planetable[i] = NULL;
      }
      g_free (filter->planetable);
      filter->planetable = NULL;
      break;
    }
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}


static void
gst_quarktv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstQuarkTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUARKTV (object));

  filter = GST_QUARKTV (object);

  switch (prop_id) {
    case ARG_PLANES:
    {
      gint new_n_planes = g_value_get_int (value);
      GstBuffer **new_planetable;
      gint i;

      /* If the number of planes changed, copy across any existing planes */
      if (new_n_planes != filter->planes) {
        new_planetable =
            (GstBuffer **) g_malloc (new_n_planes * sizeof (GstBuffer *));

        for (i = 0; (i < new_n_planes) && (i < filter->planes); i++) {
          new_planetable[i] = filter->planetable[i];
        }
        for (; i < filter->planes; i++) {
          if (filter->planetable[i])
            gst_buffer_unref (filter->planetable[i]);
        }
        g_free (filter->planetable);
        filter->planetable = new_planetable;
        filter->current_plane = filter->planes - 1;
        filter->planes = new_n_planes;
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_quarktv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstQuarkTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUARKTV (object));

  filter = GST_QUARKTV (object);

  switch (prop_id) {
    case ARG_PLANES:
      g_value_set_int (value, filter->planes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
