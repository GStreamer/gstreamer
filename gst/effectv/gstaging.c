/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * This library is free software; you can redistribute it and/or
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

/*
 * This file was (probably) generated from gstvideotemplate.c,
 * gstvideotemplate.c,v 1.11 2004/01/07 08:56:45 ds Exp 
 */

/* From main.c of warp-1.1:
 *
 *      Simple DirectMedia Layer demo
 *      Realtime picture 'gooing'
 *      by sam lantinga slouken@devolution.com
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gstvideofilter.h>
#include <string.h>
#include <math.h>

#define GST_TYPE_AGINGTV \
  (gst_agingtv_get_type())
#define GST_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGINGTV,GstAgingTV))
#define GST_AGINGTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AGINGTV,GstAgingTVClass))
#define GST_IS_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGINGTV))
#define GST_IS_AGINGTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGINGTV))

#define SCRATCH_MAX 20
typedef struct _scratch
{
  gint life;
  gint x;
  gint dx;
  gint init;
}
scratch;

static int dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static int dy[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };

typedef struct _GstAgingTV GstAgingTV;
typedef struct _GstAgingTVClass GstAgingTVClass;

struct _GstAgingTV
{
  GstVideofilter videofilter;

  gint width, height;
  gint aging_mode;

  scratch scratches[SCRATCH_MAX];
  gint scratch_lines;

  gint dust_interval;
  gint pits_interval;

};

struct _GstAgingTVClass
{
  GstVideofilterClass parent_class;
};

/* GstAgingTV signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static void gst_agingtv_base_init (gpointer g_class);
static void gst_agingtv_class_init (gpointer g_class, gpointer class_data);
static void gst_agingtv_init (GTypeInstance * instance, gpointer g_class);
static void gst_agingtv_setup (GstVideofilter * videofilter);

static void gst_agingtv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_agingtv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_agingtv_rgb32 (GstVideofilter * videofilter, void *d, void *s);

GType
gst_agingtv_get_type (void)
{
  static GType agingtv_type = 0;

  if (!agingtv_type) {
    static const GTypeInfo agingtv_info = {
      sizeof (GstAgingTVClass),
      gst_agingtv_base_init,
      NULL,
      gst_agingtv_class_init,
      NULL,
      NULL,
      sizeof (GstAgingTV),
      0,
      gst_agingtv_init,
    };

    agingtv_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
        "GstAgingTV", &agingtv_info, 0);
  }
  return agingtv_type;
}

static GstVideofilterFormat gst_agingtv_formats[] = {
  {"RGB ", 32, gst_agingtv_rgb32, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000,
      0xff000000}
};

static void
gst_agingtv_base_init (gpointer g_class)
{
  static GstElementDetails agingtv_details = GST_ELEMENT_DETAILS ("AgingTV",
      "Filter/Effect/Video",
      "AgingTV adds age to video input using scratches and dust",
      "Sam Lantinga <slouken@devolution.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &agingtv_details);

  for (i = 0; i < G_N_ELEMENTS (gst_agingtv_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
        gst_agingtv_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_agingtv_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

#if 0
  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AGINGTV_METHOD, GST_AGINGTV_METHOD_1, G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_agingtv_set_property;
  gobject_class->get_property = gst_agingtv_get_property;

  videofilter_class->setup = gst_agingtv_setup;
}

static void
gst_agingtv_init (GTypeInstance * instance, gpointer g_class)
{
  GstAgingTV *agingtv = GST_AGINGTV (instance);
  GstVideofilter *videofilter;

  GST_DEBUG ("gst_agingtv_init");

  videofilter = GST_VIDEOFILTER (agingtv);

  /* do stuff */
}

static void
gst_agingtv_setup (GstVideofilter * videofilter)
{
  GstAgingTV *agingtv;
  int width = gst_videofilter_get_input_width (videofilter);
  int height = gst_videofilter_get_input_height (videofilter);

  g_return_if_fail (GST_IS_AGINGTV (videofilter));
  agingtv = GST_AGINGTV (videofilter);

  /* if any setup needs to be done, do it here */

  agingtv->width = width;
  agingtv->height = height;
}

static unsigned int
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}


static void
coloraging (guint32 * src, guint32 * dest, gint video_area)
{
  guint32 a, b;
  gint i;

  for (i = video_area; i; i--) {
    a = *src++;
    b = (a & 0xfcfcfc) >> 2;
    *dest++ = a - b + 0x181818 + ((fastrand () >> 8) & 0x101010);
  }
}


static void
scratching (scratch * scratches, gint scratch_lines, guint32 * dest, gint width,
    gint height)
{
  gint i, y, y1, y2;
  guint32 *p, a, b;
  scratch *scratch;

  for (i = 0; i < scratch_lines; i++) {
    scratch = &scratches[i];

    if (scratch->life) {
      scratch->x = scratch->x + scratch->dx;

      if (scratch->x < 0 || scratch->x > width * 256) {
        scratch->life = 0;
        break;
      }
      p = dest + (scratch->x >> 8);
      if (scratch->init) {
        y1 = scratch->init;
        scratch->init = 0;
      } else {
        y1 = 0;
      }
      scratch->life--;
      if (scratch->life) {
        y2 = height;
      } else {
        y2 = fastrand () % height;
      }
      for (y = y1; y < y2; y++) {
        a = *p & 0xfefeff;
        a += 0x202020;
        b = a & 0x1010100;
        *p = a | (b - (b >> 8));
        p += width;
      }
    } else {
      if ((fastrand () & 0xf0000000) == 0) {
        scratch->life = 2 + (fastrand () >> 27);
        scratch->x = fastrand () % (width * 256);
        scratch->dx = ((int) fastrand ()) >> 23;
        scratch->init = (fastrand () % (height - 1)) + 1;
      }
    }
  }
}

static void
dusts (guint32 * dest, gint width, gint height, gint dust_interval,
    gint area_scale)
{
  int i, j;
  int dnum;
  int d, len;
  guint x, y;

  if (dust_interval == 0) {
    if ((fastrand () & 0xf0000000) == 0) {
      dust_interval = fastrand () >> 29;
    }
    return;
  }
  dnum = area_scale * 4 + (fastrand () >> 27);

  for (i = 0; i < dnum; i++) {
    x = fastrand () % width;
    y = fastrand () % height;
    d = fastrand () >> 29;
    len = fastrand () % area_scale + 5;
    for (j = 0; j < len; j++) {
      dest[y * width + x] = 0x101010;
      y += dy[d];
      x += dx[d];

      if (y >= height || x >= width)
        break;

      d = (d + fastrand () % 3 - 1) & 7;
    }
  }
  dust_interval--;
}

static void
pits (guint32 * dest, gint width, gint height, gint area_scale,
    gint pits_interval)
{
  int i, j;
  int pnum, size, pnumscale;
  guint x, y;

  pnumscale = area_scale * 2;
  if (pits_interval) {
    pnum = pnumscale + (fastrand () % pnumscale);

    pits_interval--;
  } else {
    pnum = fastrand () % pnumscale;

    if ((fastrand () & 0xf8000000) == 0) {
      pits_interval = (fastrand () >> 28) + 20;
    }
  }
  for (i = 0; i < pnum; i++) {
    x = fastrand () % (width - 1);
    y = fastrand () % (height - 1);

    size = fastrand () >> 28;

    for (j = 0; j < size; j++) {
      x = x + fastrand () % 3 - 1;
      y = y + fastrand () % 3 - 1;

      if (y >= height || x >= width)
        break;

      dest[y * width + x] = 0xc0c0c0;
    }
  }
}

static void
gst_agingtv_rgb32 (GstVideofilter * videofilter, void *d, void *s)
{
  GstAgingTV *agingtv;
  int width = gst_videofilter_get_input_width (videofilter);
  int height = gst_videofilter_get_input_height (videofilter);
  int video_size = width * height;
  guint32 *src = s;
  guint32 *dest = d;
  gint area_scale = width * height / 64 / 480;

  if (area_scale <= 0)
    area_scale = 1;

  g_return_if_fail (GST_IS_AGINGTV (videofilter));
  agingtv = GST_AGINGTV (videofilter);

  coloraging (src, dest, video_size);
  scratching (agingtv->scratches, agingtv->scratch_lines, dest, width, height);
  pits (dest, width, height, area_scale, agingtv->pits_interval);
  if (area_scale > 1)
    dusts (dest, width, height, agingtv->dust_interval, area_scale);

}

static void
gst_agingtv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstAgingTV *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));
  src = GST_AGINGTV (object);

  GST_DEBUG ("gst_agingtv_set_property");
  switch (prop_id) {
#if 0
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
#endif
    default:
      break;
  }
}

static void
gst_agingtv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAgingTV *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));
  src = GST_AGINGTV (object);

  switch (prop_id) {
#if 0
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
