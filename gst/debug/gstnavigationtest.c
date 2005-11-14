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

#include <gstnavigationtest.h>
#include <string.h>
#include <math.h>

typedef struct
{
  double x;
  double y;
  gint images_left;
  guint8 cy, cu, cv;
} ButtonClick;

static void gst_navigationtest_base_init (gpointer g_class);
static void gst_navigationtest_class_init (gpointer g_class,
    gpointer class_data);
static void gst_navigationtest_init (GTypeInstance * instance,
    gpointer g_class);

static gboolean gst_navigationtest_handle_src_event (GstPad * pad,
    GstEvent * event);

static GstStateChangeReturn
gst_navigationtest_change_state (GstElement * element,
    GstStateChange transition);

static void gst_navigationtest_planar411 (GstVideofilter * videofilter,
    void *dest, void *src);
static void gst_navigationtest_setup (GstVideofilter * videofilter);

static GstVideofilterClass *parent_class;       /* NULL */

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
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  element_class->change_state = gst_navigationtest_change_state;

  videofilter_class->setup = gst_navigationtest_setup;
}

static void
gst_navigationtest_init (GTypeInstance * instance, gpointer g_class)
{
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (instance);
  GstVideofilter *videofilter = GST_VIDEOFILTER (navtest);

  gst_pad_set_event_function (videofilter->srcpad,
      GST_DEBUG_FUNCPTR (gst_navigationtest_handle_src_event));

  navtest->x = -1;
  navtest->y = -1;
}

static gboolean
gst_navigationtest_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstNavigationtest *navtest;
  const gchar *type;

  navtest = GST_NAVIGATIONTEST (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
    {
      const GstStructure *s = gst_event_get_structure (event);

      type = gst_structure_get_string (s, "event");
      if (g_str_equal (type, "mouse-move")) {
        gst_structure_get_double (s, "pointer_x", &navtest->x);
        gst_structure_get_double (s, "pointer_y", &navtest->y);
      } else if (g_str_equal (type, "mouse-button-press")) {
        ButtonClick *click = g_new (ButtonClick, 1);

        gst_structure_get_double (s, "pointer_x", &click->x);
        gst_structure_get_double (s, "pointer_y", &click->y);
        click->images_left = ceil (GST_VIDEOFILTER (navtest)->framerate);
        /* green */
        click->cy = 150;
        click->cu = 46;
        click->cv = 21;
        navtest->clicks = g_slist_prepend (navtest->clicks, click);
      } else if (g_str_equal (type, "mouse-button-release")) {
        ButtonClick *click = g_new (ButtonClick, 1);

        gst_structure_get_double (s, "pointer_x", &click->x);
        gst_structure_get_double (s, "pointer_y", &click->y);
        click->images_left = ceil (GST_VIDEOFILTER (navtest)->framerate);
        /* red */
        click->cy = 76;
        click->cu = 85;
        click->cv = 255;
        navtest->clicks = g_slist_prepend (navtest->clicks, click);
      }
      break;
    }
    default:
      break;
  }
  return gst_pad_event_default (pad, event);
}

static void
gst_navigationtest_setup (GstVideofilter * videofilter)
{
  GstNavigationtest *navigationtest;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (videofilter));
  navigationtest = GST_NAVIGATIONTEST (videofilter);

  /* if any setup needs to be done, do it here */

}

static void
draw_box_planar411 (guint8 * dest, int width, int height, int x, int y,
    guint8 colory, guint8 coloru, guint8 colorv)
{
  int x1, x2, y1, y2;

  if (x < 0 || y < 0 || x >= width || y >= height)
    return;

  x1 = MAX (x - 5, 0);
  x2 = MIN (x + 5, width);
  y1 = MAX (y - 5, 0);
  y2 = MIN (y + 5, height);

  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) dest)[y * width + x] = colory;
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
      ((guint8 *) dest)[y * width + x] = coloru;
    }
  }

  dest += height * width;
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) dest)[y * width + x] = colorv;
    }
  }
}

static void
gst_navigationtest_planar411 (GstVideofilter * videofilter,
    void *dest, void *src)
{
  GstNavigationtest *navtest = (GstNavigationtest *) videofilter;
  gint width, height;
  GSList *walk;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (videofilter));

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  /* do something interesting here.  This simply copies the source
   * to the destination. */
  memcpy (dest, src, width * height + (width / 2) * (height / 2) * 2);

  walk = navtest->clicks;
  while (walk) {
    ButtonClick *click = walk->data;

    walk = g_slist_next (walk);
    draw_box_planar411 (dest, width, height, rint (click->x),
        rint (click->y), click->cy, click->cu, click->cv);
    if (--click->images_left < 1) {
      navtest->clicks = g_slist_remove (navtest->clicks, click);
      g_free (click);
    }
  }
  draw_box_planar411 (dest, width, height, rint (navtest->x),
      rint (navtest->y), 0, 128, 128);
}

static GstStateChangeReturn
gst_navigationtest_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (element);

  /* upwards state changes */
  switch (transition) {
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* downwards state changes */
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      g_slist_foreach (navtest->clicks, (GFunc) g_free, NULL);
      g_slist_free (navtest->clicks);
      navtest->clicks = NULL;
      break;
    }
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "navigationtest", GST_RANK_NONE,
      GST_TYPE_NAVIGATIONTEST);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "navigationtest",
    "Template for a video filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
