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

#include <cairo.h>


/* GstTimeoverlay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
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

static void
gst_timeoverlay_update_font_height (GstVideofilter * videofilter)
{
  GstTimeoverlay *timeoverlay = GST_TIMEOVERLAY (videofilter);
  gint width, height;
  cairo_surface_t *font_surface;
  cairo_t *font_cairo;
  cairo_font_extents_t font_extents;

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  font_surface =
      cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  font_cairo = cairo_create (font_surface);
  cairo_surface_destroy (font_surface);
  font_surface = NULL;

  cairo_select_font_face (font_cairo, "monospace", 0, 0);
  cairo_set_font_size (font_cairo, 20);
  cairo_font_extents (font_cairo, &font_extents);
  timeoverlay->text_height = font_extents.height;
  GST_DEBUG_OBJECT (timeoverlay, "font height is %d", font_extents.height);
  cairo_destroy (font_cairo);
  font_cairo = NULL;
}

static void
gst_timeoverlay_setup (GstVideofilter * videofilter)
{
  GstTimeoverlay *timeoverlay;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  gst_timeoverlay_update_font_height (videofilter);
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
  int b_width;
  char *string;
  int i, j;
  unsigned char *image;
  cairo_text_extents_t extents;

  cairo_surface_t *font_surface;
  cairo_t *text_cairo;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  /* create surface for font rendering */
  /* FIXME: preparation of the surface could also be done once when settings
   * change */
  image = g_malloc (4 * width * timeoverlay->text_height);

  font_surface =
      cairo_image_surface_create_for_data (image, CAIRO_FORMAT_ARGB32, width,
      timeoverlay->text_height, width * 4);
  text_cairo = cairo_create (font_surface);
  cairo_surface_destroy (font_surface);
  font_surface = NULL;

  /* we draw a rectangle because the compositing on the buffer below
   * doesn't do alpha */
  cairo_save (text_cairo);
  cairo_rectangle (text_cairo, 0, 0, width, timeoverlay->text_height);
  cairo_set_source_rgba (text_cairo, 0, 0, 0, 1);
  cairo_set_operator (text_cairo, CAIRO_OPERATOR_SOURCE);
  cairo_fill (text_cairo);
  cairo_restore (text_cairo);

  string =
      gst_timeoverlay_print_smpte_time (GST_BUFFER_TIMESTAMP (videofilter->
          in_buf));
  cairo_save (text_cairo);
  cairo_select_font_face (text_cairo, "monospace", 0, 0);
  cairo_set_font_size (text_cairo, 20);
  cairo_text_extents (text_cairo, string, &extents);
  cairo_set_source_rgb (text_cairo, 1, 1, 1);
  cairo_move_to (text_cairo, 0, timeoverlay->text_height - 2);
  cairo_show_text (text_cairo, string);
  g_free (string);
#if 0
  cairo_text_path (timeoverlay->cr, string);
  cairo_set_rgb_color (timeoverlay->cr, 1, 1, 1);
  cairo_set_line_width (timeoverlay->cr, 1.0);
  cairo_stroke (timeoverlay->cr);
#endif

  cairo_restore (text_cairo);

  /* blend width; should retain a max text width so it doesn't jitter */
  b_width = extents.width;
  if (b_width > width)
    b_width = width;

  memcpy (dest, src, videofilter->from_buf_size);
  for (i = 0; i < timeoverlay->text_height; i++) {
    for (j = 0; j < b_width; j++) {
      ((unsigned char *) dest)[i * width + j] = image[(i * width + j) * 4 + 0];
    }
  }
  for (i = 0; i < timeoverlay->text_height / 2; i++) {
    memset (dest + width * height + i * (width / 2), 128, b_width / 2);
    memset (dest + width * height + (width / 2) * (height / 2) +
        i * (width / 2), 128, b_width / 2);
  }

  cairo_destroy (text_cairo);
  text_cairo = NULL;
  g_free (image);
}
