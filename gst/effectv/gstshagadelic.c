/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * Inspired by Adrian Likin's script for the GIMP.
 * EffecTV is free software.  This library is free software;
 * you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
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

#define GST_TYPE_SHAGADELICTV \
  (gst_shagadelictv_get_type())
#define GST_SHAGADELICTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHAGADELICTV,GstShagadelicTV))
#define GST_SHAGADELICTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHAGADELICTV,GstShagadelicTVClass))
#define GST_IS_SHAGADELICTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHAGADELICTV))
#define GST_IS_SHAGADELICTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHAGADELICTV))

typedef struct _GstShagadelicTV GstShagadelicTV;
typedef struct _GstShagadelicTVClass GstShagadelicTVClass;

struct _GstShagadelicTV
{
  GstVideofilter videofilter;

  gint width, height;
  gint stat;
  gchar *ripple;
  gchar *spiral;
  guchar phase;
  gint rx, ry;
  gint bx, by;
  gint rvx, rvy;
  gint bvx, bvy;
};

struct _GstShagadelicTVClass
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
  ARG_0
};

static void gst_shagadelictv_base_init (gpointer g_class);
static void gst_shagadelictv_class_init (gpointer g_class, gpointer class_data);
static void gst_shagadelictv_init (GTypeInstance * instance, gpointer g_class);

static void gst_shagadelic_initialize (GstShagadelicTV * filter);

static void gst_shagadelictv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shagadelictv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_shagadelictv_setup (GstVideofilter * videofilter);
static void gst_shagadelictv_rgb32 (GstVideofilter * videofilter, void *d,
    void *s);

/*static guint gst_shagadelictv_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_shagadelictv_get_type (void)
{
  static GType shagadelictv_type = 0;

  if (!shagadelictv_type) {
    static const GTypeInfo shagadelictv_info = {
      sizeof (GstShagadelicTVClass),
      gst_shagadelictv_base_init,
      NULL,
      (GClassInitFunc) gst_shagadelictv_class_init,
      NULL,
      NULL,
      sizeof (GstShagadelicTV),
      0,
      (GInstanceInitFunc) gst_shagadelictv_init,
    };

    shagadelictv_type =
        g_type_register_static (GST_TYPE_VIDEOFILTER, "GstShagadelicTV",
        &shagadelictv_info, 0);
  }
  return shagadelictv_type;
}

static GstVideofilterFormat gst_shagadelictv_formats[] = {
  {"RGB ", 32, gst_shagadelictv_rgb32, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000,
      0xff000000}
};

static void
gst_shagadelictv_base_init (gpointer g_class)
{
  /* elementfactory information */
  static GstElementDetails gst_shagadelictv_details =
      GST_ELEMENT_DETAILS ("ShagadelicTV",
      "Filter/Effect/Video",
      "Oh behave, ShagedelicTV makes images shagadelic!",
      "Wim Taymans <wim.taymans@chello.be>");

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &gst_shagadelictv_details);

  for (i = 0; i < G_N_ELEMENTS (gst_shagadelictv_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
        gst_shagadelictv_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_shagadelictv_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  gobject_class->set_property = gst_shagadelictv_set_property;
  gobject_class->get_property = gst_shagadelictv_get_property;

  videofilter_class->setup = gst_shagadelictv_setup;
}

static void
gst_shagadelictv_init (GTypeInstance * instance, gpointer g_class)
{
  GstShagadelicTV *filter = GST_SHAGADELICTV (instance);

  filter->ripple = NULL;
  filter->spiral = NULL;
}

static void
gst_shagadelictv_setup (GstVideofilter * videofilter)
{
  GstShagadelicTV *filter;
  int width = gst_videofilter_get_input_width (videofilter);
  int height = gst_videofilter_get_input_height (videofilter);
  int area;

  g_return_if_fail (GST_IS_SHAGADELICTV (videofilter));
  filter = GST_SHAGADELICTV (videofilter);

  filter->width = width;
  filter->height = height;

  area = filter->width * filter->height;

  g_free (filter->ripple);
  g_free (filter->spiral);

  filter->ripple = (gchar *) g_malloc (area * 4);
  filter->spiral = (gchar *) g_malloc (area);

  gst_shagadelic_initialize (filter);
}

static unsigned int
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

static void
gst_shagadelic_initialize (GstShagadelicTV * filter)
{
  int i, x, y;

#ifdef PS2
  float xx, yy;
#else
  double xx, yy;
#endif

  i = 0;
  for (y = 0; y < filter->height * 2; y++) {
    yy = y - filter->height;
    yy *= yy;

    for (x = 0; x < filter->width * 2; x++) {
      xx = x - filter->width;
#ifdef PS2
      filter->ripple[i++] = ((unsigned int) (sqrtf (xx * xx + yy) * 8)) & 255;
#else
      filter->ripple[i++] = ((unsigned int) (sqrt (xx * xx + yy) * 8)) & 255;
#endif
    }
  }

  i = 0;
  for (y = 0; y < filter->height; y++) {
    yy = y - filter->height / 2;

    for (x = 0; x < filter->width; x++) {
      xx = x - filter->width / 2;
#ifdef PS2
      filter->spiral[i++] = ((unsigned int)
          ((atan2f (xx,
                      yy) / ((float) M_PI) * 256 * 9) + (sqrtf (xx * xx +
                      yy * yy) * 5))) & 255;
#else
      filter->spiral[i++] = ((unsigned int)
          ((atan2 (xx, yy) / M_PI * 256 * 9) + (sqrt (xx * xx +
                      yy * yy) * 5))) & 255;
#endif
/* Here is another Swinger!
 * ((atan2(xx, yy)/M_PI*256) + (sqrt(xx*xx+yy*yy)*10))&255;
 */
    }
  }
  filter->rx = fastrand () % filter->width;
  filter->ry = fastrand () % filter->height;
  filter->bx = fastrand () % filter->width;
  filter->by = fastrand () % filter->height;
  filter->rvx = -2;
  filter->rvy = -2;
  filter->bvx = 2;
  filter->bvy = 2;
  filter->phase = 0;
}

static void
gst_shagadelictv_rgb32 (GstVideofilter * videofilter, void *d, void *s)
{
  GstShagadelicTV *filter;
  guint32 *src, *dest;
  gint x, y;
  guint32 v;
  guchar r, g, b;
  gint width, height;

  filter = GST_SHAGADELICTV (videofilter);

  src = (guint32 *) s;
  dest = (guint32 *) d;

  width = filter->width;
  height = filter->height;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      v = *src++ | 0x1010100;
      v = (v - 0x707060) & 0x1010100;
      v -= v >> 8;
/* Try another Babe! 
 * v = *src++;
 * *dest++ = v & ((r<<16)|(g<<8)|b);
 */
      r = (gchar) (filter->ripple[(filter->ry + y) * width * 2 + filter->rx +
              x] + filter->phase * 2) >> 7;
      g = (gchar) (filter->spiral[y * width + x] + filter->phase * 3) >> 7;
      b = (gchar) (filter->ripple[(filter->by + y) * width * 2 + filter->bx +
              x] - filter->phase) >> 7;
      *dest++ = v & ((r << 16) | (g << 8) | b);
    }
  }

  filter->phase -= 8;
  if ((filter->rx + filter->rvx) < 0 || (filter->rx + filter->rvx) >= width)
    filter->rvx = -filter->rvx;
  if ((filter->ry + filter->rvy) < 0 || (filter->ry + filter->rvy) >= height)
    filter->rvy = -filter->rvy;
  if ((filter->bx + filter->bvx) < 0 || (filter->bx + filter->bvx) >= width)
    filter->bvx = -filter->bvx;
  if ((filter->by + filter->bvy) < 0 || (filter->by + filter->bvy) >= height)
    filter->bvy = -filter->bvy;
  filter->rx += filter->rvx;
  filter->ry += filter->rvy;
  filter->bx += filter->bvx;
  filter->by += filter->bvy;
}

static void
gst_shagadelictv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShagadelicTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAGADELICTV (object));

  filter = GST_SHAGADELICTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_shagadelictv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstShagadelicTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAGADELICTV (object));

  filter = GST_SHAGADELICTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
