/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
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
#include <gstvideofilter.h>

#define GST_TYPE_VERTIGOTV \
  (gst_vertigotv_get_type())
#define GST_VERTIGOTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VERTIGOTV,GstVertigoTV))
#define GST_VERTIGOTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VERTIGOTV,GstVertigoTVClass))
#define GST_IS_VERTIGOTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VERTIGOTV))
#define GST_IS_VERTIGOTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VERTIGOTV))

typedef struct _GstVertigoTV GstVertigoTV;
typedef struct _GstVertigoTVClass GstVertigoTVClass;

struct _GstVertigoTV
{
  GstVideofilter videofilter;

  gint width, height;
  guint32 *buffer;
  guint32 *current_buffer, *alt_buffer;
  gint dx, dy;
  gint sx, sy;
  gdouble phase;
  gdouble phase_increment;
  gdouble zoomrate;
};

struct _GstVertigoTVClass
{
  GstVideofilterClass parent_class;

  void (*reset) (GstElement *element);
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  RESET_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SPEED,
  ARG_ZOOM_SPEED,
};

static void	gst_vertigotv_base_init		(gpointer g_class);
static void	gst_vertigotv_class_init 	(GstVertigoTVClass * klass, gpointer class_data);
static void 	gst_vertigotv_init 		(GTypeInstance *instance, gpointer g_class);
static void     gst_vertigotv_setup		(GstVideofilter *videofilter);

static void 	gst_vertigotv_reset_handler 	(GstElement *element);

static void 	gst_vertigotv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_vertigotv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);
static void     gst_vertigotv_rgb32 		(GstVideofilter *videofilter, void *d, void *s);

static guint gst_vertigotv_signals[LAST_SIGNAL] = { 0 };

GType gst_vertigotv_get_type (void)
{
  static GType vertigotv_type = 0;

  if (!vertigotv_type) {
    static const GTypeInfo vertigotv_info = {
      sizeof (GstVertigoTVClass), 
      gst_vertigotv_base_init,
      NULL,
      (GClassInitFunc) gst_vertigotv_class_init,
      NULL,
      NULL,
      sizeof (GstVertigoTV),
      0,
      (GInstanceInitFunc) gst_vertigotv_init,
    };

    vertigotv_type = g_type_register_static (GST_TYPE_VIDEOFILTER, "GstVertigoTV", &vertigotv_info, 0);
  }
  return vertigotv_type;
}

static GstVideofilterFormat gst_vertigotv_formats[] = {
  { "RGB ", 32, gst_vertigotv_rgb32, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000, 0xff000000 }
};

static void
gst_vertigotv_base_init (gpointer g_class)
{
  /* elementfactory information */
  static GstElementDetails vertigotv_details = GST_ELEMENT_DETAILS (
    "VertigoTV",
    "Filter/Effect/Video",
    "A loopback alpha blending effector with rotating and scaling",
    "Wim Taymans <wim.taymans@chello.be>"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &vertigotv_details);

  for(i=0;i<G_N_ELEMENTS(gst_vertigotv_formats);i++){
    gst_videofilter_class_add_format(videofilter_class,
	gst_vertigotv_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_vertigotv_class_init (GstVertigoTVClass * klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  videofilter_class = GST_VIDEOFILTER_CLASS (klass);

  gst_vertigotv_signals[RESET_SIGNAL] =
    g_signal_new ("reset-parms",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GstVertigoTVClass, reset),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  klass->reset = gst_vertigotv_reset_handler;
       
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SPEED,
    g_param_spec_float ("speed","Speed","Control the speed of movement",
                        0.01, 100.0, 0.02, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ZOOM_SPEED,
    g_param_spec_float ("zoom_speed","Zoom Speed","Control the rate of zooming",
                        1.01, 1.1, 1.01, G_PARAM_READWRITE));

  gobject_class->set_property = gst_vertigotv_set_property;
  gobject_class->get_property = gst_vertigotv_get_property;

  videofilter_class->setup = gst_vertigotv_setup;
}

static void
gst_vertigotv_reset_handler (GstElement *element)
{
  GstVertigoTV *filter = GST_VERTIGOTV (element);

  filter->phase = 0.0;
  filter->phase_increment = 0.02;
  filter->zoomrate = 1.01;
}

static void 
gst_vertigotv_setup(GstVideofilter *videofilter)
{
  GstVertigoTV *filter;
  gint area;
  int width = gst_videofilter_get_input_width(videofilter);
  int height = gst_videofilter_get_input_height(videofilter);

  g_return_if_fail(GST_IS_VERTIGOTV(videofilter));
  filter = GST_VERTIGOTV(videofilter);

  filter->width = width;
  filter->height = height;

  area = width * height;

  g_free (filter->buffer);
  filter->buffer = (guint32 *) g_malloc (area * 2 * sizeof(guint32));

  memset (filter->buffer, 0, area * 2 * sizeof(guint32));
  filter->current_buffer = filter->buffer;
  filter->alt_buffer = filter->buffer + area;
  filter->phase = 0;
}

static void
gst_vertigotv_init (GTypeInstance *instance, gpointer g_class)
{
  GstVertigoTV *filter = GST_VERTIGOTV (instance);

  filter->buffer = NULL;
  filter->phase = 0.0;
  filter->phase_increment = 0.02;
  filter->zoomrate = 1.01;
}

static void 
gst_vertigotv_set_parms (GstVertigoTV *filter)
{
  double vx, vy;
  double t;
  double x, y;
  double dizz;

  dizz = sin (filter->phase) * 10 + sin (filter->phase*1.9+5) * 5;

  x = filter->width / 2;
  y = filter->height / 2;

  t = (x*x + y*y) * filter->zoomrate;

  if (filter->width > filter->height) {
    if (dizz >= 0) {
      if (dizz > x) dizz = x;
    	vx = (x * (x - dizz) + y * y) / t;
    } else {
      if (dizz < -x) dizz = -x;
      vx = (x * (x + dizz) + y * y) / t;
    }
    vy = (dizz * y) / t;
  } else {
    if (dizz >= 0) {
      if (dizz > y) dizz = y;
      vx = (x * x + y * (y - dizz)) / t;
    } else {
      if (dizz < -y) dizz = -y;
      vx = (x * x + y * (y + dizz)) / t;
    }
    vy = (dizz * x) / t;
  }
  filter->dx = vx * 65536;
  filter->dy = vy * 65536;
  filter->sx = (-vx * x + vy * y + x + cos (filter->phase * 5) * 2) * 65536;
  filter->sy = (-vx * y - vy * x + y + sin (filter->phase * 6) * 2) * 65536;

  filter->phase += filter->phase_increment;
  if (filter->phase > 5700000) filter->phase = 0;
}
	
static void     
gst_vertigotv_rgb32 (GstVideofilter *videofilter, void *d, void *s)
{
  GstVertigoTV *filter;
  guint32 *src, *dest;
  guint32 *p;
  guint32 v;
  gint x, y;
  gint ox, oy;
  gint i;
  gint width, height, area;

  filter = GST_VERTIGOTV (videofilter);

  src = (guint32 *) s;
  dest = (guint32 *) d;

  width = filter->width;
  height = filter->height;
  area = width * height;

  gst_vertigotv_set_parms (filter);
  p = filter->alt_buffer;

  for (y = height; y > 0; y--) {
    ox = filter->sx;
    oy = filter->sy;

    for (x = width; x > 0; x--) {
      i = (oy >> 16) * width + (ox >> 16);
      if (i < 0) i = 0;
      if (i >= area) i = area;
      
      v = filter->current_buffer[i] & 0xfcfcff;
      v = (v * 3) + ((*src++) & 0xfcfcff);
      
      *p++ = (v >> 2);
      ox += filter->dx;
      oy += filter->dy;
    }
    filter->sx -= filter->dy;
    filter->sy += filter->dx;
  }

  memcpy(dest, filter->alt_buffer, area * sizeof(guint32));

  p = filter->current_buffer;
  filter->current_buffer = filter->alt_buffer;
  filter->alt_buffer = p;
}

static void
gst_vertigotv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVertigoTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VERTIGOTV (object));

  filter = GST_VERTIGOTV (object);

  switch (prop_id) {
    case ARG_SPEED:
      filter->phase_increment = g_value_get_float (value);
      break;
    case ARG_ZOOM_SPEED:
      filter->zoomrate = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void
gst_vertigotv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVertigoTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VERTIGOTV (object));

  filter = GST_VERTIGOTV (object);

  switch (prop_id) {
    case ARG_SPEED:
      g_value_set_float (value, filter->phase_increment);
      break;
    case ARG_ZOOM_SPEED:
      g_value_set_float (value, filter->zoomrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
