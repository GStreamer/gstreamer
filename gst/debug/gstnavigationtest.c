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
 * This file was (probably) generated from gstnavigationtest.c,
 * gstnavigationtest.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gstnavigationtest.h>
#include <string.h>
#include <math.h>

/* GstNavigationtest signals and args */
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

static void gst_navigationtest_base_init (gpointer g_class);
static void gst_navigationtest_class_init (gpointer g_class,
    gpointer class_data);
static void gst_navigationtest_init (GTypeInstance * instance,
    gpointer g_class);

static gboolean gst_navigationtest_handle_src_event (GstPad * pad,
    GstEvent * event);
static void gst_navigationtest_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_navigationtest_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_navigationtest_planar411 (GstVideofilter * videofilter,
    void *dest, void *src);
static void gst_navigationtest_setup (GstVideofilter * videofilter);

GType
gst_navigationtest_get_type (void)
{
  static GType navigationtest_type = 0;

  if (!navigationtest_type) {
    static const GTypeInfo navigationtest_info = {
      sizeof (GstNavigationtestClass),
      gst_navigationtest_base_init,
      NULL,
      gst_navigationtest_class_init,
      NULL,
      NULL,
      sizeof (GstNavigationtest),
      0,
      gst_navigationtest_init,
    };
    navigationtest_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
	"GstNavigationtest", &navigationtest_info, 0);
  }
  return navigationtest_type;
}

static GstVideofilterFormat gst_navigationtest_formats[] = {
  {"I420", 12, gst_navigationtest_planar411,},
};


static void
gst_navigationtest_base_init (gpointer g_class)
{
  static GstElementDetails navigationtest_details =
      GST_ELEMENT_DETAILS ("Video Filter Template",
      "Filter/Video",
      "Template for a video filter",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &navigationtest_details);

  for (i = 0; i < G_N_ELEMENTS (gst_navigationtest_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
	gst_navigationtest_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_navigationtest_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

#if 0
  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
	  GST_TYPE_NAVIGATIONTEST_METHOD, GST_NAVIGATIONTEST_METHOD_1,
	  G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_navigationtest_set_property;
  gobject_class->get_property = gst_navigationtest_get_property;

  videofilter_class->setup = gst_navigationtest_setup;
}

static void
gst_navigationtest_init (GTypeInstance * instance, gpointer g_class)
{
  GstNavigationtest *navigationtest = GST_NAVIGATIONTEST (instance);
  GstVideofilter *videofilter;

  GST_DEBUG ("gst_navigationtest_init");

  videofilter = GST_VIDEOFILTER (navigationtest);

  gst_pad_set_event_function (videofilter->srcpad,
      gst_navigationtest_handle_src_event);

  navigationtest->x = -1;
  navigationtest->y = -1;
}

static gboolean
gst_navigationtest_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstNavigationtest *navigationtest;

  navigationtest = GST_NAVIGATIONTEST (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      gst_structure_get_double (event->event_data.structure.structure,
	  "pointer_x", &navigationtest->x);
      gst_structure_get_double (event->event_data.structure.structure,
	  "pointer_y", &navigationtest->y);
      break;
    default:
      break;
  }
  return TRUE;
}

static void
gst_navigationtest_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNavigationtest *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_NAVIGATIONTEST (object));
  src = GST_NAVIGATIONTEST (object);

  GST_DEBUG ("gst_navigationtest_set_property");
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
gst_navigationtest_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNavigationtest *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_NAVIGATIONTEST (object));
  src = GST_NAVIGATIONTEST (object);

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

  return gst_element_register (plugin, "navigationtest", GST_RANK_NONE,
      GST_TYPE_NAVIGATIONTEST);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "navigationtest",
    "Template for a video filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)

     static void gst_navigationtest_setup (GstVideofilter * videofilter)
{
  GstNavigationtest *navigationtest;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (videofilter));
  navigationtest = GST_NAVIGATIONTEST (videofilter);

  /* if any setup needs to be done, do it here */

}

static void
gst_navigationtest_planar411 (GstVideofilter * videofilter,
    void *dest, void *src)
{
  GstNavigationtest *navigationtest;
  int width = gst_videofilter_get_input_width (videofilter);
  int height = gst_videofilter_get_input_height (videofilter);
  int x, y;
  int x1, x2, y1, y2;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (videofilter));
  navigationtest = GST_NAVIGATIONTEST (videofilter);

  /* do something interesting here.  This simply copies the source
   * to the destination. */
  memcpy (dest, src, width * height + (width / 2) * (height / 2) * 2);

  x = rint (navigationtest->x);
  y = rint (navigationtest->y);

  if (x < 0 || y < 0 || x >= width || y >= height)
    return;

  x1 = MAX (x - 5, 0);
  x2 = MIN (x + 5, width);
  y1 = MAX (y - 5, 0);
  y2 = MIN (y + 5, height);

  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) dest)[y * width + x] = 0;
    }
  }

  dest += height * width;
  width /= 2;
  height /= 2;
  x1 /= 2;
  x2 /= 2;
  y1 /= 2;
  y2 /= 2;
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) dest)[y * width + x] = 128;
    }
  }

  dest += height * width;
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) dest)[y * width + x] = 128;
    }
  }

}
