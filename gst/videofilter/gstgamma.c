/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) 2003 Arwed v. Merkatz <v.merkatz@gmx.net>
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
 * This file was (probably) generated from
 * gstvideotemplate.c,v 1.12 2004/01/07 21:07:12 ds Exp 
 * and
 * make_filter,v 1.6 2004/01/07 21:33:01 ds Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gstvideofilter.h>
#include <string.h>
#include <math.h>

#define GST_TYPE_GAMMA \
  (gst_gamma_get_type())
#define GST_GAMMA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GAMMA,GstGamma))
#define GST_GAMMA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GAMMA,GstGammaClass))
#define GST_IS_GAMMA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GAMMA))
#define GST_IS_GAMMA_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GAMMA))

typedef struct _GstGamma GstGamma;
typedef struct _GstGammaClass GstGammaClass;

struct _GstGamma {
  GstVideofilter videofilter;

  double gamma;
  double gamma_r, gamma_g, gamma_b;
  guint8 gamma_table[256];
  guint8 gamma_table_r[256];
  guint8 gamma_table_g[256];
  guint8 gamma_table_b[256];
};

struct _GstGammaClass {
  GstVideofilterClass parent_class;
};


/* GstGamma signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_GAMMA,
  ARG_GAMMA_R,
  ARG_GAMMA_G,
  ARG_GAMMA_B,
  /* FILL ME */
};

static void	gst_gamma_base_init	(gpointer g_class);
static void	gst_gamma_class_init	(gpointer g_class, gpointer class_data);
static void	gst_gamma_init		(GTypeInstance *instance, gpointer g_class);

static void	gst_gamma_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_gamma_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_gamma_planar411(GstVideofilter *videofilter, void *dest, void *src);
static void gst_gamma_rgb24(GstVideofilter *videofilter, void *dest, void *src);
static void gst_gamma_rgb32(GstVideofilter *videofilter, void *dest, void *src);
static void gst_gamma_setup(GstVideofilter *videofilter);
static void gst_gamma_calculate_tables (GstGamma *gamma);

GType
gst_gamma_get_type (void)
{
  static GType gamma_type = 0;

  if (!gamma_type) {
    static const GTypeInfo gamma_info = {
      sizeof(GstGammaClass),
      gst_gamma_base_init,
      NULL,
      gst_gamma_class_init,
      NULL,
      NULL,
      sizeof(GstGamma),
      0,
      gst_gamma_init,
    };
    gamma_type = g_type_register_static(GST_TYPE_VIDEOFILTER,
        "GstGamma", &gamma_info, 0);
  }
  return gamma_type;
}

static GstVideofilterFormat gst_gamma_formats[] = {
  { "I420", 12, gst_gamma_planar411, },
  { "RGB ", 24, gst_gamma_rgb24, 24, G_BIG_ENDIAN, 0xff0000, 0xff00, 0xff },
  { "RGB ", 32, gst_gamma_rgb32, 24, G_BIG_ENDIAN, 0x00ff00, 0xff0000, 0xff000000 },
};

  
static void
gst_gamma_base_init (gpointer g_class)
{
  static GstElementDetails gamma_details = GST_ELEMENT_DETAILS (
    "Video Gamma Correction",
    "Filter/Effect/Video",
    "Adjusts gamma on a video stream",
    "Arwed v. Merkatz <v.merkatz@gmx.net"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &gamma_details);

  for(i=0;i<G_N_ELEMENTS(gst_gamma_formats);i++){
    gst_videofilter_class_add_format(videofilter_class,
	gst_gamma_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_gamma_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  g_object_class_install_property(gobject_class, ARG_GAMMA,
      g_param_spec_double("gamma", "Gamma", "gamma",
        0.01, 10, 1, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_GAMMA_R,
      g_param_spec_double("redgamma", "Gamma_r", "gamma value for the red channel",
        0.01, 10, 1, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_GAMMA_G,
      g_param_spec_double("greengamma", "Gamma_g", "gamma value for the green channel",
        0.01, 10, 1, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_GAMMA_B,
      g_param_spec_double("bluegamma", "Gamma_b", "gamma value for the blue channel",
        0.01, 10, 1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_gamma_set_property;
  gobject_class->get_property = gst_gamma_get_property;

  videofilter_class->setup = gst_gamma_setup;
}

static void
gst_gamma_init (GTypeInstance *instance, gpointer g_class)
{
  GstGamma *gamma = GST_GAMMA (instance);
  GstVideofilter *videofilter;

  GST_DEBUG("gst_gamma_init");

  videofilter = GST_VIDEOFILTER(gamma);

  /* do stuff */
  gamma->gamma = 1;
  gamma->gamma_r = 1;
  gamma->gamma_g = 1;
  gamma->gamma_b = 1;
  gst_gamma_calculate_tables (gamma);
}

static void
gst_gamma_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstGamma *gamma;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_GAMMA(object));
  gamma = GST_GAMMA(object);

  GST_DEBUG("gst_gamma_set_property");
  switch (prop_id) {
    case ARG_GAMMA:
      gamma->gamma = g_value_get_double (value);
      gst_gamma_calculate_tables (gamma);
      break;
    case ARG_GAMMA_R:
      gamma->gamma_r = g_value_get_double (value);
      gst_gamma_calculate_tables (gamma);
      break;
    case ARG_GAMMA_G:
      gamma->gamma_g = g_value_get_double (value);
      gst_gamma_calculate_tables (gamma);
      break;
    case ARG_GAMMA_B:
      gamma->gamma_b = g_value_get_double (value);
      gst_gamma_calculate_tables (gamma);
      break;
    default:
      break;
  }
}

static void
gst_gamma_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstGamma *gamma;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_GAMMA(object));
  gamma = GST_GAMMA(object);

  switch (prop_id) {
    case ARG_GAMMA:
      g_value_set_double (value, gamma->gamma);
      break;
    case ARG_GAMMA_R:
      g_value_set_double (value, gamma->gamma_r);
      break;
    case ARG_GAMMA_G:
      g_value_set_double (value, gamma->gamma_g);
      break;
    case ARG_GAMMA_B:
      g_value_set_double (value, gamma->gamma_b);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean plugin_init (GstPlugin *plugin)
{
  if(!gst_library_load("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "gamma", GST_RANK_NONE,
      GST_TYPE_GAMMA);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gamma",
  "Changes gamma on video images",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)


static void gst_gamma_setup(GstVideofilter *videofilter)
{
  GstGamma *gamma;

  g_return_if_fail(GST_IS_GAMMA(videofilter));
  gamma = GST_GAMMA(videofilter);

  /* if any setup needs to be done, do it here */

}

static void
gst_gamma_calculate_tables (GstGamma *gamma)
{
  int n;
  double val;
  double exp;

  if (gamma->gamma == 1.0  &&
      gamma->gamma_r == 1.0 &&
      gamma->gamma_g == 1.0 &&
      gamma->gamma_b == 1.0) {
    GST_VIDEOFILTER (gamma)->passthru = TRUE;
    return;
  }
  GST_VIDEOFILTER (gamma)->passthru = FALSE;

  exp = 1.0 / gamma->gamma;
  for (n = 0; n < 256; n++) {
    val = n/255.0;
    val = pow(val, exp);
    val = 255.0 * val;
    gamma->gamma_table[n] = (unsigned char) floor(val + 0.5);
  }
  exp = 1.0 / gamma->gamma_r;
  for (n = 0; n < 256; n++) {
    val = n/255.0;
    val = pow(val, exp);
    val = 255.0 * val;
    gamma->gamma_table_r[n] = (unsigned char) floor(val + 0.5);
  }
  exp = 1.0 / gamma->gamma_g;
  for (n = 0; n < 256; n++) {
    val = n/255.0;
    val = pow(val, exp);
    val = 255.0 * val;
    gamma->gamma_table_g[n] = (unsigned char) floor(val + 0.5);
  }
  exp = 1.0 / gamma->gamma_b;
  for (n = 0; n < 256; n++) {
    val = n/255.0;
    val = pow(val, exp);
    val = 255.0 * val;
    gamma->gamma_table_b[n] = (unsigned char) floor(val + 0.5);
  }

}

static void gst_gamma_planar411(GstVideofilter *videofilter,
    void *dest, void *src)
{
  GstGamma *gamma;
  int width = gst_videofilter_get_input_width(videofilter);
  int height = gst_videofilter_get_input_height(videofilter);

  g_return_if_fail(GST_IS_GAMMA(videofilter));
  gamma = GST_GAMMA(videofilter);

  memcpy(dest,src,width * height + (width/2) * (height/2) * 2);

  if (gamma->gamma != 1.0) {
    {
      guint8 *cdest = dest;
      guint8 *csrc = src;
      int x,y;
      for (y=0; y < height; y++) {
        for (x=0; x < width; x++) {
          cdest[y*width + x] = gamma->gamma_table[(unsigned char)csrc[y*width + x]];
        }
      }
    }
  }
}

static void gst_gamma_rgb24(GstVideofilter *videofilter, void *dest, void *src)
{
  GstGamma *gamma;
  int i;
  int width, height;
  guint8 *csrc = src;
  guint8 *cdest = dest;
  
  g_return_if_fail(GST_IS_GAMMA(videofilter));
  gamma = GST_GAMMA(videofilter);

  width = gst_videofilter_get_input_width(videofilter);
  height = gst_videofilter_get_input_height(videofilter);
  if (gamma->gamma == 1.0) {
    i = 0;
    while ( i < width * height * 3) {
      *cdest++ = gamma->gamma_table_r[*csrc++];
      *cdest++ = gamma->gamma_table_g[*csrc++];
      *cdest++ = gamma->gamma_table_b[*csrc++];
      i = i + 3;
    }
  } else {
    i = 0;
    while (i < width * height * 3) {
      *cdest++ = gamma->gamma_table[*csrc++];
      i++;
    }
  }
}

static void gst_gamma_rgb32(GstVideofilter *videofilter, void *dest, void *src)
{
  GstGamma *gamma;
  int i;
  int width, height;
  guint8 *csrc = src;
  guint8 *cdest = dest;
  
  g_return_if_fail(GST_IS_GAMMA(videofilter));
  gamma = GST_GAMMA(videofilter);

  width = gst_videofilter_get_input_width(videofilter);
  height = gst_videofilter_get_input_height(videofilter);
  if (gamma->gamma == 1.0) {
    i = 0;
    while ( i < width * height * 4) {
      *cdest++ = gamma->gamma_table_b[*csrc++];
      *cdest++ = gamma->gamma_table_g[*csrc++];
      *cdest++ = gamma->gamma_table_r[*csrc++];
      cdest++;
      csrc++;
      i = i + 4;
    }
  } else {
    i = 0;
    while (i < width * height * 4) {
      if ((i % 4) != 3)
        *cdest++ = gamma->gamma_table[*csrc++];
      else {
        cdest++;
        csrc++;
      }
      i++;
    }
  }
}
