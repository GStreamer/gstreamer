/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. * This library is free software;
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
#include <string.h>
#include <gst/gst.h>
#include <gstvideofilter.h>

#define GST_TYPE_EDGETV \
  (gst_edgetv_get_type())
#define GST_EDGETV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EDGETV,GstEdgeTV))
#define GST_EDGETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EDGETV,GstEdgeTVClass))
#define GST_IS_EDGETV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EDGETV))
#define GST_IS_EDGETV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EDGETV))

typedef struct _GstEdgeTV GstEdgeTV;
typedef struct _GstEdgeTVClass GstEdgeTVClass;

struct _GstEdgeTV
{
  GstVideofilter videofilter;

  gint width, height;
  gint map_width, map_height;
  guint32 *map;
  gint video_width_margin;
};

struct _GstEdgeTVClass
{
  GstVideofilterClass parent_class;
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
};

static void 	gst_edgetv_base_init 		(gpointer g_class);
static void 	gst_edgetv_class_init 		(gpointer g_class, gpointer class_data);
static void	gst_edgetv_init			(GTypeInstance *instance, gpointer g_class);

static void 	gst_edgetv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_edgetv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void     gst_edgetv_setup		(GstVideofilter *videofilter);
static void     gst_edgetv_rgb32 		(GstVideofilter *videofilter, void *d, void *s);

/*static guint gst_edgetv_signals[LAST_SIGNAL] = { 0 }; */

GType gst_edgetv_get_type (void)
{
  static GType edgetv_type = 0;

  if (!edgetv_type) {
    static const GTypeInfo edgetv_info = {
      sizeof (GstEdgeTVClass), 
      gst_edgetv_base_init,
      NULL,
      (GClassInitFunc) gst_edgetv_class_init,
      NULL,
      NULL,
      sizeof (GstEdgeTV),
      0,
      (GInstanceInitFunc) gst_edgetv_init,
    };

    edgetv_type = g_type_register_static (GST_TYPE_VIDEOFILTER, "GstEdgeTV", &edgetv_info, 0);
  }
  return edgetv_type;
}

static GstVideofilterFormat gst_edgetv_formats[] = {
  { "RGB ", 32, gst_edgetv_rgb32, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000, 0xff000000 }
};

static void
gst_edgetv_base_init (gpointer g_class)
{
  /* elementfactory information */
  static GstElementDetails gst_edgetv_details = GST_ELEMENT_DETAILS (
    "EdgeTV",
    "Filter/Effect/Video",
    "Apply edge detect on video",
    "Wim Taymans <wim.taymans@chello.be>"
  );

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &gst_edgetv_details);

  for(i=0; i < G_N_ELEMENTS(gst_edgetv_formats); i++) {
    gst_videofilter_class_add_format(videofilter_class,
	gst_edgetv_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_edgetv_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  gobject_class->set_property = gst_edgetv_set_property;
  gobject_class->get_property = gst_edgetv_get_property;

  videofilter_class->setup = gst_edgetv_setup;
}

static void
gst_edgetv_init (GTypeInstance *instance, gpointer g_class)
{
  GstEdgeTV *edgetv = GST_EDGETV (instance);

  edgetv->map = NULL;
}

static void gst_edgetv_setup(GstVideofilter *videofilter)
{
  GstEdgeTV *edgetv;
  int width = gst_videofilter_get_input_width (videofilter);
  int height = gst_videofilter_get_input_height (videofilter);

  g_return_if_fail (GST_IS_EDGETV (videofilter));
  edgetv = GST_EDGETV (videofilter);

  edgetv->width = width;
  edgetv->height = height;
  edgetv->map_width = width / 4;
  edgetv->map_height = height / 4;
  edgetv->video_width_margin = width % 4;

  g_free (edgetv->map);
  edgetv->map = (guint32 *)g_malloc (edgetv->map_width * edgetv->map_height * sizeof (guint32) * 2);
  memset(edgetv->map, 0, edgetv->map_width * edgetv->map_height * sizeof (guint32) * 2);
}

static void     
gst_edgetv_rgb32 (GstVideofilter *videofilter, void *d, void *s)
{
  GstEdgeTV *filter;
  int x, y;
  int r, g, b;
  guint32 *src, *dest;
  guint32 p, q;
  guint32 v0, v1, v2, v3;

  filter = GST_EDGETV (videofilter);

  src = (guint32 *) s;
  dest = (guint32 *) d;
  
  src += filter->width * 4 + 4;
  dest += filter->width * 4 + 4;
  
  for (y = 1; y < filter->map_height - 1; y++) {
    for (x = 1; x < filter->map_width - 1; x++) {

      p = *src;
      q = *(src - 4);

/* difference between the current pixel and right neighbor. */
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;		/* To lack the lower bit for saturated addition,  */
      g = g >> 5;		/* devide the value with 32, instead of 16. It is */
      b = b >> 4;		/* same as `v2 &= 0xfefeff' */
      if (r > 127)
	r = 127;
      if (g > 127)
	g = 127;
      if (b > 255)
	b = 255;
      v2 = (r << 17) | (g << 9) | b;

/* difference between the current pixel and upper neighbor. */
      q = *(src - filter->width * 4);
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;
      g = g >> 5;
      b = b >> 4;
      if (r > 127)
	r = 127;
      if (g > 127)
	g = 127;
      if (b > 255)
	b = 255;
      v3 = (r << 17) | (g << 9) | b;

      v0 = filter->map[(y - 1) * filter->map_width * 2 + x * 2];
      v1 = filter->map[y * filter->map_width * 2 + (x - 1) * 2 + 1];
      filter->map[y * filter->map_width * 2 + x * 2] = v2;
      filter->map[y * filter->map_width * 2 + x * 2 + 1] = v3;
      r = v0 + v1;
      g = r & 0x01010100;
      dest[0] = r | (g - (g >> 8));
      r = v0 + v3;
      g = r & 0x01010100;
      dest[1] = r | (g - (g >> 8));
      dest[2] = v3;
      dest[3] = v3;
      r = v2 + v1;
      g = r & 0x01010100;
      dest[filter->width] = r | (g - (g >> 8));
      r = v2 + v3;
      g = r & 0x01010100;
      dest[filter->width + 1] = r | (g - (g >> 8));
      dest[filter->width + 2] = v3;
      dest[filter->width + 3] = v3;
      dest[filter->width * 2] = v2;
      dest[filter->width * 2 + 1] = v2;
      dest[filter->width * 3] = v2;
      dest[filter->width * 3 + 1] = v2;

      src += 4;
      dest += 4;
    }
    src += filter->width * 3 + 8 + filter->video_width_margin;
    dest += filter->width * 3 + 8 + filter->video_width_margin;
  }
}

static void
gst_edgetv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstEdgeTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_EDGETV (object));

  filter = GST_EDGETV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_edgetv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstEdgeTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_EDGETV (object));

  filter = GST_EDGETV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
