/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. We release this product under the terms of the
 * GNU General Public License version 2. The license is included in the file
 * COPYING.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 */

#include <math.h>
#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#define GST_TYPE_QUARKTV \
  (gst_quarktv_get_type())
#define GST_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QUARKTV,GstQuarkTV))
#define GST_QUARKTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstQuarkTV))
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
  gint planes;
  gint current_plane;
  guint32 *buffer;
  guint32 **planetable;
};

struct _GstQuarkTVClass
{
  GstElementClass parent_class;
};

GstElementDetails gst_quarktv_details = {
  "QuarkTV",
  "Filter/Effect",
  "Motion disolver",
  VERSION,
  "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>",
  "(C) 2001 FUKUCHI Kentarou",
};


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

static void 	gst_quarktv_class_init 	(GstQuarkTVClass * klass);
static void 	gst_quarktv_init 		(GstQuarkTV * filter);
static void 	gst_quarktv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_quarktv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

/* static void 	gst_quarktv_reset_handler 	(GstElement *element); */

static void 	gst_quarktv_chain 		(GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;
/* static guint gst_quarktv_signals[LAST_SIGNAL] = { 0 }; */

static unsigned int 
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

GType gst_quarktv_get_type (void)
{
  static GType quarktv_type = 0;

  if (!quarktv_type) {
    static const GTypeInfo quarktv_info = {
      sizeof (GstQuarkTVClass), 
      NULL,
      NULL,
      (GClassInitFunc) gst_quarktv_class_init,
      NULL,
      NULL,
      sizeof (GstQuarkTV),
      0,
      (GInstanceInitFunc) gst_quarktv_init,
    };

    quarktv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstQuarkTV", &quarktv_info, 0);
  }
  return quarktv_type;
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
    g_param_spec_int ("planes","Planes","Number of frames in the buffer",
                      1, 32, PLANES, G_PARAM_READWRITE));
       
  gobject_class->set_property = gst_quarktv_set_property;
  gobject_class->get_property = gst_quarktv_get_property;
}

static GstPadConnectReturn
gst_quarktv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstQuarkTV *filter;
  gint area;
  gint i;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  area = filter->width * filter->height;

  g_free (filter->buffer);
  filter->buffer = (guint32 *) g_malloc (area * filter->planes * sizeof(guint32));
  g_free (filter->planetable);
  filter->planetable = (guint32 **) g_malloc(filter->planes * sizeof(guint32 *));

  bzero (filter->buffer, area * filter->planes * sizeof(guint32));
  for(i = 0; i < filter->planes; i++) {
    filter->planetable[i] = &(filter->buffer[area * i]);
  }

  if (gst_pad_try_set_caps (filter->srcpad, caps)) {
    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_quarktv_init (GstQuarkTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_quarktv_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_quarktv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->planes = PLANES;
  filter->current_plane = filter->planes - 1;
  filter->buffer = NULL;
  filter->planetable = NULL;
}

static void
gst_quarktv_chain (GstPad * pad, GstBuffer * buf)
{
  GstQuarkTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;
  gint area;
  gint i;
  gint cf;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  area = filter->width * filter->height;

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = area * sizeof(guint32);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  
  memcpy (filter->planetable[filter->current_plane], src, area * sizeof(guint32));

  for (i = area - 1; i >= 0; i--) {
    cf = (filter->current_plane + (fastrand () >> 24)) & (filter->planes - 1);
    dest[i] = (filter->planetable[cf])[i];
  }

  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, outbuf);

  filter->current_plane--;
  
  if (filter->current_plane < 0) 
    filter->current_plane = filter->planes - 1;
}

static void
gst_quarktv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstQuarkTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUARKTV (object));

  filter = GST_QUARKTV (object);

  switch (prop_id) {
    case ARG_PLANES:
      filter->planes = g_value_get_int (value);
      filter->current_plane = filter->planes - 1;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_quarktv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
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
