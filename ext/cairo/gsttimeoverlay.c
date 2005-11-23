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

#include <gsttimeoverlay.h>

#include <string.h>
#include <math.h>

#include <cairo.h>

#include <gst/video/video.h>

static GstElementDetails timeoverlay_details =
GST_ELEMENT_DETAILS ("Time Overlay",
    "Filter/Editor/Video",
    "Overlays the time on a video stream",
    "David Schleef <ds@schleef.org>");

static GstStaticPadTemplate gst_timeoverlay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_timeoverlay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstVideofilterClass *parent_class = NULL;

static void
gst_timeoverlay_update_font_height (GstTimeoverlay * timeoverlay)
{
  gint width, height;
  cairo_surface_t *font_surface;
  cairo_t *font_cairo;
  cairo_font_extents_t font_extents;

  width = timeoverlay->width;
  height = timeoverlay->height;

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

static gboolean
gst_timeoverlay_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstTimeoverlay *filter = GST_TIMEOVERLAY (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    gst_timeoverlay_update_font_height (filter);
    ret = TRUE;
  }

  return ret;
}

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static gboolean
gst_timeoverlay_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstTimeoverlay *filter;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  filter = GST_TIMEOVERLAY (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = GST_VIDEO_I420_SIZE (width, height);
    ret = TRUE;
    GST_DEBUG_OBJECT (filter, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
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


static GstFlowReturn
gst_timeoverlay_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstTimeoverlay *timeoverlay;
  int width;
  int height;
  int b_width;
  char *string;
  int i, j;
  unsigned char *image;
  cairo_text_extents_t extents;
  gpointer dest, src;
  cairo_surface_t *font_surface;
  cairo_t *text_cairo;
  GstFlowReturn ret = GST_FLOW_OK;

  timeoverlay = GST_TIMEOVERLAY (trans);

  gst_buffer_stamp (out, in);

  src = GST_BUFFER_DATA (in);
  dest = GST_BUFFER_DATA (out);

  width = timeoverlay->width;
  height = timeoverlay->height;

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

  string = gst_timeoverlay_print_smpte_time (GST_BUFFER_TIMESTAMP (in));
  cairo_save (text_cairo);
  cairo_select_font_face (text_cairo, "monospace", 0, 0);
  cairo_set_font_size (text_cairo, 20);
  cairo_text_extents (text_cairo, string, &extents);
  cairo_set_source_rgb (text_cairo, 1, 1, 1);
  cairo_move_to (text_cairo, 0, timeoverlay->text_height - 2);
  cairo_show_text (text_cairo, string);
  g_free (string);

  cairo_restore (text_cairo);

  /* blend width; should retain a max text width so it doesn't jitter */
  b_width = extents.width;
  if (b_width > width)
    b_width = width;

  memcpy (dest, src, GST_BUFFER_SIZE (in));
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

  return ret;
}

static void
gst_timeoverlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &timeoverlay_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timeoverlay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timeoverlay_src_template));
}

static void
gst_timeoverlay_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_timeoverlay_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_timeoverlay_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_timeoverlay_transform);
}

static void
gst_timeoverlay_init (GTypeInstance * instance, gpointer g_class)
{
}

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
