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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnavigationtest.h"
#include <string.h>
#include <math.h>

#include <gst/video/video.h>

#ifdef _MSC_VER
#define rint(x) (floor((x)+0.5))
#endif

GST_DEBUG_CATEGORY_STATIC (navigationtest_debug);
#define GST_CAT_DEFAULT navigationtest_debug

static GstStaticPadTemplate gst_navigationtest_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_navigationtest_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstVideoFilterClass *parent_class = NULL;

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
      gint fps_n, fps_d;

      fps_n = gst_value_get_fraction_numerator ((&navtest->framerate));
      fps_d = gst_value_get_fraction_denominator ((&navtest->framerate));

      type = gst_structure_get_string (s, "event");
      if (g_str_equal (type, "mouse-move")) {
        gst_structure_get_double (s, "pointer_x", &navtest->x);
        gst_structure_get_double (s, "pointer_y", &navtest->y);
      } else if (g_str_equal (type, "mouse-button-press")) {
        ButtonClick *click = g_new (ButtonClick, 1);

        gst_structure_get_double (s, "pointer_x", &click->x);
        gst_structure_get_double (s, "pointer_y", &click->y);
        click->images_left = (fps_n + fps_d - 1) / fps_d;
        /* green */
        click->cy = 150;
        click->cu = 46;
        click->cv = 21;
        navtest->clicks = g_slist_prepend (navtest->clicks, click);
      } else if (g_str_equal (type, "mouse-button-release")) {
        ButtonClick *click = g_new (ButtonClick, 1);

        gst_structure_get_double (s, "pointer_x", &click->x);
        gst_structure_get_double (s, "pointer_y", &click->y);
        click->images_left = (fps_n + fps_d - 1) / fps_d;
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

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static gboolean
gst_navigationtest_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstNavigationtest *navtest;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  navtest = GST_NAVIGATIONTEST (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = GST_VIDEO_I420_SIZE (width, height);
    ret = TRUE;
    GST_DEBUG_OBJECT (navtest, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static gboolean
gst_navigationtest_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (btrans);
  gboolean ret = FALSE;
  GstStructure *structure;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &navtest->width) &&
      gst_structure_get_int (structure, "height", &navtest->height)) {
    const GValue *framerate;

    framerate = gst_structure_get_value (structure, "framerate");
    if (framerate && GST_VALUE_HOLDS_FRACTION (framerate)) {
      g_value_copy (framerate, &navtest->framerate);
      ret = TRUE;
    }
  }

  return ret;
}

static void
draw_box_planar411 (guint8 * dest, int width, int height, int x, int y,
    guint8 colory, guint8 coloru, guint8 colorv)
{
  int x1, x2, y1, y2;
  guint8 *d = dest;

  if (x < 0 || y < 0 || x >= width || y >= height)
    return;

  x1 = MAX (x - 5, 0);
  x2 = MIN (x + 5, width);
  y1 = MAX (y - 5, 0);
  y2 = MIN (y + 5, height);

  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) d)[y * GST_VIDEO_I420_Y_ROWSTRIDE (width) + x] = colory;
    }
  }

  d = dest + GST_VIDEO_I420_U_OFFSET (width, height);
  x1 /= 2;
  x2 /= 2;
  y1 /= 2;
  y2 /= 2;
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) d)[y * GST_VIDEO_I420_U_ROWSTRIDE (width) + x] = coloru;
    }
  }

  d = dest + GST_VIDEO_I420_V_OFFSET (width, height);
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      ((guint8 *) d)[y * GST_VIDEO_I420_V_ROWSTRIDE (width) + x] = colorv;
    }
  }
}

static GstFlowReturn
gst_navigationtest_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (trans);
  GSList *walk;
  GstFlowReturn ret = GST_FLOW_OK;

  /* do something interesting here.  This simply copies the source
   * to the destination. */
  gst_buffer_copy_metadata (out, in, GST_BUFFER_COPY_TIMESTAMPS);

  memcpy (GST_BUFFER_DATA (out), GST_BUFFER_DATA (in),
      MIN (GST_BUFFER_SIZE (in), GST_BUFFER_SIZE (out)));

  walk = navtest->clicks;
  while (walk) {
    ButtonClick *click = walk->data;

    walk = g_slist_next (walk);
    draw_box_planar411 (GST_BUFFER_DATA (out), navtest->width, navtest->height,
        rint (click->x), rint (click->y), click->cy, click->cu, click->cv);
    if (--click->images_left < 1) {
      navtest->clicks = g_slist_remove (navtest->clicks, click);
      g_free (click);
    }
  }
  draw_box_planar411 (GST_BUFFER_DATA (out), navtest->width, navtest->height,
      rint (navtest->x), rint (navtest->y), 0, 128, 128);

  return ret;
}

static GstStateChangeReturn
gst_navigationtest_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (element);

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

static void
gst_navigationtest_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video navigation test",
      "Filter/Effect/Video",
      "Handle navigation events showing a black square following mouse pointer",
      "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_navigationtest_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_navigationtest_src_template);
}

static void
gst_navigationtest_class_init (gpointer klass, gpointer class_data)
{
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;

  element_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_navigationtest_change_state);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_navigationtest_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_navigationtest_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_navigationtest_transform);
}

static void
gst_navigationtest_init (GTypeInstance * instance, gpointer g_class)
{
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (instance);
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (instance);

  gst_pad_set_event_function (btrans->srcpad,
      GST_DEBUG_FUNCPTR (gst_navigationtest_handle_src_event));

  navtest->x = -1;
  navtest->y = -1;
  g_value_init (&navtest->framerate, GST_TYPE_FRACTION);
}

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

    navigationtest_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstNavigationtest", &navigationtest_info, 0);
  }
  return navigationtest_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (navigationtest_debug, "navigationtest", 0,
      "navigationtest");

  return gst_element_register (plugin, "navigationtest", GST_RANK_NONE,
      GST_TYPE_NAVIGATIONTEST);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "navigationtest",
    "Template for a video filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
