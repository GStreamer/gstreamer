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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#define GST_TYPE_REVTV \
  (gst_revtv_get_type())
#define GST_REVTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REVTV,GstRevTV))
#define GST_REVTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstRevTV))
#define GST_IS_REVTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REVTV))
#define GST_IS_REVTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REVTV))

#define THE_COLOR 0xffffffff

typedef struct _GstRevTV GstRevTV;
typedef struct _GstRevTVClass GstRevTVClass;

struct _GstRevTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint vgrabtime;
  gint vgrab;
  gint linespace;
  gint vscale;
};

struct _GstRevTVClass
{
  GstElementClass parent_class;

  void (*reset) (GstElement *element);
};

/* elementfactory information */
static GstElementDetails gst_revtv_details = GST_ELEMENT_DETAILS (
  "RevTV",
  "Filter/Effect/Video",
  "A video waveform monitor for each line of video processed",
  "Wim Taymans <wim.taymans@chello.be>"
);


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DELAY,
  ARG_LINESPACE,
  ARG_GAIN,
};

static void 	gst_revtv_base_init 		(gpointer g_class);
static void 	gst_revtv_class_init 		(GstRevTVClass * klass);
static void 	gst_revtv_init 			(GstRevTV * filter);

static void 	gst_revtv_set_property 		(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_revtv_get_property 		(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_revtv_chain 		(GstPad * pad, GstData *_data);

static GstElementClass *parent_class = NULL;
/* static guint gst_revtv_signals[LAST_SIGNAL] = { 0 }; */

GType gst_revtv_get_type (void)
{
  static GType revtv_type = 0;

  if (!revtv_type) {
    static const GTypeInfo revtv_info = {
      sizeof (GstRevTVClass), 
      gst_revtv_base_init,
      NULL,
      (GClassInitFunc) gst_revtv_class_init,
      NULL,
      NULL,
      sizeof (GstRevTV),
      0,
      (GInstanceInitFunc) gst_revtv_init,
    };

    revtv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstRevTV", &revtv_info, 0);
  }
  return revtv_type;
}

static void
gst_revtv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_sink_template));
 
  gst_element_class_set_details (element_class, &gst_revtv_details);
}

static void
gst_revtv_class_init (GstRevTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELAY,
    g_param_spec_int ("delay","Delay","Delay in frames between updates",
                        1, 100, 1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LINESPACE,
    g_param_spec_int ("linespace","Linespace","Control line spacing",
                        1, 100, 6, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GAIN,
    g_param_spec_int ("gain","Gain","Control gain",
                        1, 200, 50, G_PARAM_READWRITE));

  gobject_class->set_property = gst_revtv_set_property;
  gobject_class->get_property = gst_revtv_get_property;
}

static GstPadLinkReturn
gst_revtv_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstRevTV *filter;
  GstStructure *structure;

  filter = GST_REVTV (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int  (structure, "width", &filter->width);
  gst_structure_get_int  (structure, "height", &filter->height);

  return gst_pad_try_set_caps (filter->srcpad, caps);
}

static void
gst_revtv_init (GstRevTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_effectv_sink_template), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_revtv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_revtv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_effectv_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->vgrabtime = 1;
  filter->vgrab = 0;
  filter->linespace = 6;
  filter->vscale = 50;
}


static void
gst_revtv_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRevTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;
  gint width, height, area;
  guint32 *nsrc;
  gint y, x, R, G, B, yval;

  filter = GST_REVTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  width = filter->width;
  height = filter->height;
  area = width * height;

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = area * sizeof(guint32);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  // draw the offset lines
  for (y = 0; y < height ; y += filter->linespace){
    for (x = 0; x <= width; x++) {
      nsrc = src + (y * width) + x;

      // Calc Y Value for curpix
      R = ((*nsrc) & 0xff0000) >> (16 - 1);
      G = ((*nsrc) & 0xff00) >> (8 - 2);
      B =  (*nsrc) & 0xff;

      yval = y - ((short) (R + G + B) / filter->vscale) ;

      if (yval > 0) {
      	dest[x + (yval * width)] = THE_COLOR;
      }
    }
  }
  
  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));
}

static void
gst_revtv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRevTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_REVTV (object));

  filter = GST_REVTV (object);

  switch (prop_id) {
    case ARG_DELAY:
      filter->vgrabtime = g_value_get_int (value);
      break;
    case ARG_LINESPACE:
      filter->linespace = g_value_get_int (value);
      break;
    case ARG_GAIN:
      filter->vscale = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_revtv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRevTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_REVTV (object));

  filter = GST_REVTV (object);

  switch (prop_id) {
    case ARG_DELAY:
      g_value_set_int (value, filter->vgrabtime);
      break;
    case ARG_LINESPACE:
      g_value_set_int (value, filter->linespace);
      break;
    case ARG_GAIN:
      g_value_set_int (value, filter->vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
