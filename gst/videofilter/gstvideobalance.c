/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
 * This file was (probably) generated from gstvideobalance.c,
 * gstvideobalance.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gstvideobalance.h>
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#endif
#include <string.h>
#include <math.h>

#include <gst/colorbalance/colorbalance.h>

/* GstVideobalance signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_CONTRAST,
  ARG_BRIGHTNESS,
  ARG_HUE,
  ARG_SATURATION
      /* FILL ME */
};

static GstVideofilterClass *parent_class = NULL;

static void gst_videobalance_base_init (gpointer g_class);
static void gst_videobalance_class_init (gpointer g_class, gpointer class_data);
static void gst_videobalance_init (GTypeInstance * instance, gpointer g_class);

static void gst_videobalance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videobalance_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_videobalance_planar411 (GstVideofilter * videofilter,
    void *dest, void *src);
static void gst_videobalance_setup (GstVideofilter * videofilter);

static void gst_videobalance_interface_init (GstImplementsInterfaceClass *
    klass);
static void gst_videobalance_colorbalance_init (GstColorBalanceClass * iface);

static void gst_videobalance_dispose (GObject * object);
static void gst_videobalance_update_properties (GstVideobalance * videobalance);

GType
gst_videobalance_get_type (void)
{
  static GType videobalance_type = 0;

  if (!videobalance_type) {
    static const GTypeInfo videobalance_info = {
      sizeof (GstVideobalanceClass),
      gst_videobalance_base_init,
      NULL,
      gst_videobalance_class_init,
      NULL,
      NULL,
      sizeof (GstVideobalance),
      0,
      gst_videobalance_init,
    };

    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_videobalance_interface_init,
      NULL,
      NULL,
    };

    static const GInterfaceInfo colorbalance_info = {
      (GInterfaceInitFunc) gst_videobalance_colorbalance_init,
      NULL,
      NULL,
    };

    videobalance_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
	"GstVideobalance", &videobalance_info, 0);

    g_type_add_interface_static (videobalance_type,
	GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (videobalance_type, GST_TYPE_COLOR_BALANCE,
	&colorbalance_info);
  }
  return videobalance_type;
}

static GstVideofilterFormat gst_videobalance_formats[] = {
  {"I420", 12, gst_videobalance_planar411,},
};


static void
gst_videobalance_base_init (gpointer g_class)
{
  static GstElementDetails videobalance_details =
      GST_ELEMENT_DETAILS ("Video Balance Control",
      "Filter/Effect/Video",
      "Adjusts brightness, contrast, hue, saturation on a video stream",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &videobalance_details);

  for (i = 0; i < G_N_ELEMENTS (gst_videobalance_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
	gst_videobalance_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_videobalance_dispose (GObject * object)
{
  GList *channels = NULL;
  GstVideobalance *balance;
  gint i;

  balance = GST_VIDEOBALANCE (object);

  for (i = 0; i < 256; i++) {
    g_free (balance->tableu[i]);
    g_free (balance->tablev[i]);
  }
  g_free (balance->tabley);
  g_free (balance->tableu);
  g_free (balance->tablev);

  channels = balance->channels;

  while (channels) {
    GstColorBalanceChannel *channel = channels->data;

    g_object_unref (channel);
    channels = g_list_next (channels);
  }

  if (balance->channels)
    g_list_free (balance->channels);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_videobalance_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  parent_class = g_type_class_ref (GST_TYPE_VIDEOFILTER);

  g_object_class_install_property (gobject_class, ARG_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast",
	  0, 2, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness",
	  -1, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_HUE,
      g_param_spec_double ("hue", "Hue", "hue", -1, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation",
	  0, 2, 1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_videobalance_set_property;
  gobject_class->get_property = gst_videobalance_get_property;
  gobject_class->dispose = gst_videobalance_dispose;

  videofilter_class->setup = gst_videobalance_setup;

#ifdef HAVE_LIBOIL
  oil_init ();
#endif
}

static void
gst_videobalance_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideobalance *videobalance = GST_VIDEOBALANCE (instance);
  GstVideofilter *videofilter;
  char *channels[4] = { "HUE", "SATURATION",
    "BRIGHTNESS", "CONTRAST"
  };
  gint i;

  GST_DEBUG ("gst_videobalance_init");

  videofilter = GST_VIDEOFILTER (videobalance);

  /* do stuff */
  videobalance->contrast = 1.0;
  videobalance->brightness = 0.0;
  videobalance->saturation = 1.0;
  videobalance->hue = 0.0;

  videobalance->needupdate = FALSE;
  videofilter->passthru = TRUE;

  videobalance->tabley = g_new (guint8, 256);
  videobalance->tableu = g_new (guint8 *, 256);
  videobalance->tablev = g_new (guint8 *, 256);
  for (i = 0; i < 256; i++) {
    videobalance->tableu[i] = g_new (guint8, 256);
    videobalance->tablev[i] = g_new (guint8, 256);
  }

  /* Generate the channels list */
  for (i = 0; i < (sizeof (channels) / sizeof (char *)); i++) {
    GstColorBalanceChannel *channel;

    channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
    channel->label = g_strdup (channels[i]);
    channel->min_value = -1000;
    channel->max_value = 1000;

    videobalance->channels = g_list_append (videobalance->channels, channel);
  }

}

static gboolean
gst_videobalance_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  g_assert (type == GST_TYPE_COLOR_BALANCE);
  return TRUE;
}

static void
gst_videobalance_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_videobalance_interface_supported;
}

static const GList *
gst_videobalance_colorbalance_list_channels (GstColorBalance * balance)
{
  GstVideobalance *videobalance = GST_VIDEOBALANCE (balance);

  g_return_val_if_fail (videobalance != NULL, NULL);
  g_return_val_if_fail (GST_IS_VIDEOBALANCE (videobalance), NULL);

  return videobalance->channels;
}

static void
gst_videobalance_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstVideobalance *vb = GST_VIDEOBALANCE (balance);
  GstVideofilter *vf = GST_VIDEOFILTER (vb);

  g_return_if_fail (vb != NULL);
  g_return_if_fail (GST_IS_VIDEOBALANCE (vb));
  g_return_if_fail (GST_IS_VIDEOFILTER (vf));
  g_return_if_fail (channel->label != NULL);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    vb->hue = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    vb->saturation = (value + 1000.0) * 2.0 / 2000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    vb->brightness = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    vb->contrast = (value + 1000.0) * 2.0 / 2000.0;
  }

  gst_videobalance_update_properties (vb);
}

static gint
gst_videobalance_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstVideobalance *vb = GST_VIDEOBALANCE (balance);
  gint value = 0;

  g_return_val_if_fail (vb != NULL, 0);
  g_return_val_if_fail (GST_IS_VIDEOBALANCE (vb), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    value = (vb->hue + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    value = vb->saturation * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    value = (vb->brightness + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    value = vb->contrast * 2000.0 / 2.0 - 1000.0;
  }

  return value;
}

static void
gst_videobalance_colorbalance_init (GstColorBalanceClass * iface)
{
  GST_COLOR_BALANCE_TYPE (iface) = GST_COLOR_BALANCE_SOFTWARE;
  iface->list_channels = gst_videobalance_colorbalance_list_channels;
  iface->set_value = gst_videobalance_colorbalance_set_value;
  iface->get_value = gst_videobalance_colorbalance_get_value;
}

static void
gst_videobalance_update_properties (GstVideobalance * videobalance)
{
  GstVideofilter *vf = GST_VIDEOFILTER (videobalance);

  videobalance->needupdate = TRUE;

  if (videobalance->contrast == 1.0 &&
      videobalance->brightness == 0.0 &&
      videobalance->hue == 0.0 && videobalance->saturation == 1.0) {
    vf->passthru = TRUE;
  } else {
    vf->passthru = FALSE;
  }
}

static void
gst_videobalance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideobalance *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOBALANCE (object));
  src = GST_VIDEOBALANCE (object);

  GST_DEBUG ("gst_videobalance_set_property");
  switch (prop_id) {
    case ARG_CONTRAST:
      src->contrast = g_value_get_double (value);
      break;
    case ARG_BRIGHTNESS:
      src->brightness = g_value_get_double (value);
      break;
    case ARG_HUE:
      src->hue = g_value_get_double (value);
      break;
    case ARG_SATURATION:
      src->saturation = g_value_get_double (value);
      break;
    default:
      break;
  }

  gst_videobalance_update_properties (src);
}

static void
gst_videobalance_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideobalance *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOBALANCE (object));
  src = GST_VIDEOBALANCE (object);

  switch (prop_id) {
    case ARG_CONTRAST:
      g_value_set_double (value, src->contrast);
      break;
    case ARG_BRIGHTNESS:
      g_value_set_double (value, src->brightness);
      break;
    case ARG_HUE:
      g_value_set_double (value, src->hue);
      break;
    case ARG_SATURATION:
      g_value_set_double (value, src->saturation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "videobalance", GST_RANK_NONE,
      GST_TYPE_VIDEOBALANCE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videobalance",
    "Changes hue, saturation, brightness etc. on video images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)

     static void gst_videobalance_setup (GstVideofilter * videofilter)
{
  GstVideobalance *videobalance;

  g_return_if_fail (GST_IS_VIDEOBALANCE (videofilter));
  videobalance = GST_VIDEOBALANCE (videofilter);

  /* if any setup needs to be done, do it here */

}

/*
 * look-up tables (LUT).
 */

static void
gst_videobalance_update_tables_planar411 (GstVideobalance * vb)
{
  gint i, j;
  gdouble y, u, v, hue_cos, hue_sin;

  /* Y */
  for (i = 0; i < 256; i++) {
    y = 16 + ((i - 16) * vb->contrast + vb->brightness * 255);
    if (y < 0)
      y = 0;
    else if (y > 255)
      y = 255;
    vb->tabley[i] = rint (y);
  }

  /* FIXME this is a bogus transformation for hue, but you get
   * the idea */
  hue_cos = cos (M_PI * vb->hue);
  hue_sin = sin (M_PI * vb->hue);

  /* U/V lookup tables are 2D, since we need both U/V for each table
   * separately. */
  for (i = -128; i < 128; i++) {
    for (j = -128; j < 128; j++) {
      u = 128 + ((i * hue_cos + j * hue_sin) * vb->saturation);
      v = 128 + ((-i * hue_sin + j * hue_cos) * vb->saturation);
      if (u < 0)
	u = 0;
      else if (u > 255)
	u = 255;
      if (v < 0)
	v = 0;
      else if (v > 255)
	v = 255;
      vb->tableu[i + 128][j + 128] = rint (u);
      vb->tablev[i + 128][j + 128] = rint (v);
    }
  }
}

#ifndef HAVE_LIBOIL
void
tablelookup_u8 (guint8 * dest, int dstr, guint8 * src, int sstr,
    guint8 * table, int tstr, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = table[*src * tstr];
    dest += dstr;
    src += sstr;
  }
}
#endif

static void
gst_videobalance_planar411 (GstVideofilter * videofilter, void *dest, void *src)
{
  GstVideobalance *videobalance;
  int width;
  int height;
  int x, y;

  g_return_if_fail (GST_IS_VIDEOBALANCE (videofilter));
  videobalance = GST_VIDEOBALANCE (videofilter);

  if (videobalance->needupdate) {
    gst_videobalance_update_tables_planar411 (videobalance);
    videobalance->needupdate = FALSE;
  }

  width = videofilter->from_width;
  height = videofilter->from_height;

  {
    guint8 *cdest = dest;
    guint8 *csrc = src;

    for (y = 0; y < height; y++) {
      tablelookup_u8 (cdest + y * width, 1, csrc + y * width, 1,
	  videobalance->tabley, 1, width);
    }
  }

  {
    gint u1, v1;
    guint8 *usrc, *vsrc;
    guint8 *udest, *vdest;

    usrc = src + width * height;
    udest = dest + width * height;
    vsrc = src + width * height + (width / 2) * (height / 2);
    vdest = dest + width * height + (width / 2) * (height / 2);

    for (y = 0; y < height / 2; y++) {
      for (x = 0; x < width / 2; x++) {
	u1 = usrc[y * (width / 2) + x];
	v1 = vsrc[y * (width / 2) + x];
	udest[y * (width / 2) + x] = videobalance->tableu[u1][v1];
	vdest[y * (width / 2) + x] = videobalance->tablev[u1][v1];
      }
    }
  }

}
