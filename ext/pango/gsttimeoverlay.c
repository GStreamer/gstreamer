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

/* portions derived from:
 * Pango
 * pangoft2topgm.c: Example program to view a UTF-8 encoding file
 *                  using Pango to render result.
 *
 * Copyright (C) 1999 Red Hat Software
 * Copyright (C) 2001 Sun Microsystems
 */

/*
 * This file was (probably) generated from gsttimeoverlay.c,
 * gsttimeoverlay.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gsttimeoverlay.h>
#include <string.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangoft2.h>


/* GstTimeoverlay signals and args */
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

static void gst_timeoverlay_base_init (gpointer g_class);
static void gst_timeoverlay_class_init (gpointer g_class, gpointer class_data);
static void gst_timeoverlay_init (GTypeInstance * instance, gpointer g_class);

static void gst_timeoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_timeoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_timeoverlay_planar411 (GstVideofilter * videofilter, void *dest,
    void *src);
static void gst_timeoverlay_setup (GstVideofilter * videofilter);

GType
gst_timeoverlay_get_type (void)
{
  static GType timeoverlay_type = 0;

  if (!timeoverlay_type) {
    static const GTypeInfo timeoverlay_info = {
      sizeof (GstTimeoverlayClass),
      gst_timeoverlay_base_init,
      NULL,
      gst_timeoverlay_class_init,
      NULL,
      NULL,
      sizeof (GstTimeoverlay),
      0,
      gst_timeoverlay_init,
    };

    timeoverlay_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
        "GstTimeoverlay", &timeoverlay_info, 0);
  }
  return timeoverlay_type;
}

static GstVideofilterFormat gst_timeoverlay_formats[] = {
  {"I420", 12, gst_timeoverlay_planar411,},
};


static void
gst_timeoverlay_base_init (gpointer g_class)
{
  static GstElementDetails timeoverlay_details =
      GST_ELEMENT_DETAILS ("Time Overlay",
      "Filter/Editor/Video",
      "Overlays the time on a video stream",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &timeoverlay_details);

  for (i = 0; i < G_N_ELEMENTS (gst_timeoverlay_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
        gst_timeoverlay_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_timeoverlay_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

#if 0
  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_TIMEOVERLAY_METHOD, GST_TIMEOVERLAY_METHOD_1,
          G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_timeoverlay_set_property;
  gobject_class->get_property = gst_timeoverlay_get_property;

  videofilter_class->setup = gst_timeoverlay_setup;
}

static void
gst_timeoverlay_init (GTypeInstance * instance, gpointer g_class)
{
  GstTimeoverlay *timeoverlay = GST_TIMEOVERLAY (instance);
  GstVideofilter *videofilter;

  GST_DEBUG ("gst_timeoverlay_init");

  videofilter = GST_VIDEOFILTER (timeoverlay);

  /* do stuff */
}

static void
gst_timeoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTimeoverlay *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TIMEOVERLAY (object));
  src = GST_TIMEOVERLAY (object);

  GST_DEBUG ("gst_timeoverlay_set_property");
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
gst_timeoverlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTimeoverlay *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TIMEOVERLAY (object));
  src = GST_TIMEOVERLAY (object);

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

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "timeoverlay", GST_RANK_NONE,
      GST_TYPE_TIMEOVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "timeoverlay",
    "Time overlay", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)

     static void gst_timeoverlay_setup (GstVideofilter * videofilter)
{
  GstTimeoverlay *timeoverlay;
  PangoFontDescription *font_description;
  PangoContext *context;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  /* if any setup needs to be done, do it here */

  /* what does this affect? */
  context = pango_ft2_get_context (100, 100);

  pango_context_set_language (context, pango_language_from_string ("en_US"));
  pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);

  font_description = pango_font_description_new ();
  pango_font_description_set_family (font_description, g_strdup ("Monospace"));
  pango_font_description_set_style (font_description, PANGO_STYLE_NORMAL);
  pango_font_description_set_variant (font_description, PANGO_VARIANT_NORMAL);
  pango_font_description_set_weight (font_description, PANGO_WEIGHT_NORMAL);
  pango_font_description_set_stretch (font_description, PANGO_STRETCH_NORMAL);
  pango_font_description_set_size (font_description, 12 * PANGO_SCALE);

  pango_context_set_font_description (context, font_description);

  timeoverlay->context = context;
  timeoverlay->font_description = font_description;

}

static char *
gst_timeoverlay_print_smpte_time (guint64 time)
{
  int hours;
  int minutes;
  int seconds;
  int ms;
  double x;

  x = rint ((time + 500000) * 1e-6);

  hours = floor (x / (60 * 60 * 1000));
  x -= hours * 60 * 60 * 1000;
  minutes = floor (x / (60 * 1000));
  x -= minutes * 60 * 1000;
  seconds = floor (x / (1000));
  x -= seconds * 1000;
  ms = rint (x);

  return g_strdup_printf ("%02d:%02d:%02d.%03d", hours, minutes, seconds, ms);
}

static void
gst_timeoverlay_planar411 (GstVideofilter * videofilter, void *dest, void *src)
{
  GstTimeoverlay *timeoverlay;
  int width;
  int height;
  PangoRectangle logical_rect;
  PangoLayout *layout;
  int b_height, b_width;
  FT_Bitmap bitmap;
  char *string;
  int i;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  layout = pango_layout_new (timeoverlay->context);
  string =
      gst_timeoverlay_print_smpte_time (GST_BUFFER_TIMESTAMP (videofilter->
          in_buf));
  pango_layout_set_text (layout, string, strlen (string));
  g_free (string);

  pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
  pango_layout_set_width (layout, -1);

  pango_layout_get_extents (layout, NULL, &logical_rect);
  b_height = PANGO_PIXELS (logical_rect.height);
  b_width = PANGO_PIXELS (logical_rect.width);

  //hheight = 20;

  memcpy (dest, src, videofilter->from_buf_size);

  for (i = 0; i < b_height; i++) {
    memset (dest + i * width, 0, b_width);
  }
  for (i = 0; i < b_height / 2; i++) {
    memset (dest + width * height + i * (width / 2), 128, b_width / 2);
    memset (dest + width * height + (width / 2) * (height / 2) +
        i * (width / 2), 128, b_width / 2);
  }
  bitmap.rows = b_height;
  bitmap.width = b_width;
  bitmap.pitch = width;
  bitmap.buffer = dest;
  bitmap.num_grays = 256;
  bitmap.pixel_mode = ft_pixel_mode_grays;

  pango_ft2_render_layout (&bitmap, layout, 0, 0);
}
