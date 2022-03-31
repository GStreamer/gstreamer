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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

static GstStaticPadTemplate gst_navigationtest_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

#define gst_navigationtest_parent_class parent_class
G_DEFINE_TYPE (GstNavigationtest, gst_navigationtest, GST_TYPE_VIDEO_FILTER);
GST_ELEMENT_REGISTER_DEFINE (navigationtest, "navigationtest", GST_RANK_NONE,
    GST_TYPE_NAVIGATIONTEST);

enum
{
  PROP_DISPLAY_MOUSE = 1,
  PROP_DISPLAY_TOUCH,
};

static void
gst_navigationtest_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNavigationtest *navtest;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (object));
  navtest = GST_NAVIGATIONTEST (object);

  switch (prop_id) {
    case PROP_DISPLAY_MOUSE:
      navtest->display_mouse = g_value_get_boolean (value);
      break;
    case PROP_DISPLAY_TOUCH:
      navtest->display_touch = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_navigationtest_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNavigationtest *navtest;

  g_return_if_fail (GST_IS_NAVIGATIONTEST (object));
  navtest = GST_NAVIGATIONTEST (object);

  switch (prop_id) {
    case PROP_DISPLAY_MOUSE:
      g_value_set_boolean (value, navtest->display_mouse);
      break;
    case PROP_DISPLAY_TOUCH:
      g_value_set_boolean (value, navtest->display_mouse);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_navigationtest_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoInfo *info;
  GstNavigationtest *navtest;
  GstNavigationEventType type;

  navtest = GST_NAVIGATIONTEST (trans);

  info = &GST_VIDEO_FILTER (trans)->in_info;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
    {
      gint fps_n, fps_d;

      fps_n = GST_VIDEO_INFO_FPS_N (info);
      fps_d = GST_VIDEO_INFO_FPS_D (info);

      type = gst_navigation_event_get_type (event);
      switch (type) {
        case GST_NAVIGATION_EVENT_MOUSE_MOVE:{
          gst_navigation_event_get_coordinates (event, &navtest->mousex,
              &navtest->mousey);
          GST_DEBUG ("received mouse-move event at %f,%f", navtest->mousex,
              navtest->mousey);
          gst_navigation_event_parse_modifier_state (event,
              &navtest->modifiers);
          break;
        }
        case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:{
          ButtonClick *click = g_new (ButtonClick, 1);

          gst_navigation_event_parse_mouse_button_event (event, &click->button,
              &click->x, &click->y);
          GST_DEBUG ("received mouse-button-press for button %d at %f,%f",
              click->button, click->x, click->y);
          click->images_left = (fps_n + fps_d - 1) / fps_d;
          /* green */
          click->cy = 150;
          click->cu = 46;
          click->cv = 21;
          navtest->clicks = g_slist_prepend (navtest->clicks, click);
          break;
        }
        case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:{
          ButtonClick *click = g_new (ButtonClick, 1);

          gst_navigation_event_parse_mouse_button_event (event, &click->button,
              &click->x, &click->y);
          GST_DEBUG ("received mouse-button-release for button %d at %f,%f",
              click->button, click->x, click->y);
          click->images_left = (fps_n + fps_d - 1) / fps_d;
          /* red */
          click->cy = 76;
          click->cu = 85;
          click->cv = 255;
          navtest->clicks = g_slist_prepend (navtest->clicks, click);
          break;
        }
        case GST_NAVIGATION_EVENT_MOUSE_SCROLL:{
          gdouble x, y, sx, sy;

          gst_navigation_event_parse_mouse_scroll_event (event, &x, &y, &sx,
              &sy);
          GST_DEBUG ("received mouse-scroll event at %f,%f with axes %f,%f", x,
              y, sx, sy);
          break;
        }
        case GST_NAVIGATION_EVENT_KEY_PRESS:
        case GST_NAVIGATION_EVENT_KEY_RELEASE:{
          const char *name;

          gst_navigation_event_parse_modifier_state (event,
              &navtest->modifiers);
          gst_navigation_event_parse_key_event (event, &name);
          GST_DEBUG ("received %s event for key \"%s\"",
              type == GST_NAVIGATION_EVENT_KEY_PRESS ? "key-press" :
              "key-release", name);
          break;
        }
        case GST_NAVIGATION_EVENT_COMMAND:{
          GstNavigationCommand command;
          const char *name;

          gst_navigation_event_parse_command (event, &command);
          switch (command) {
            case GST_NAVIGATION_COMMAND_INVALID:
              name = "invalid";
              break;
            case GST_NAVIGATION_COMMAND_MENU1:
              name = "menu1";
              break;
            case GST_NAVIGATION_COMMAND_MENU2:
              name = "menu2";
              break;
            case GST_NAVIGATION_COMMAND_MENU3:
              name = "menu3";
              break;
            case GST_NAVIGATION_COMMAND_MENU4:
              name = "menu4";
              break;
            case GST_NAVIGATION_COMMAND_MENU5:
              name = "menu5";
              break;
            case GST_NAVIGATION_COMMAND_MENU6:
              name = "menu6";
              break;
            case GST_NAVIGATION_COMMAND_MENU7:
              name = "menu7";
              break;
            case GST_NAVIGATION_COMMAND_LEFT:
              name = "left";
              break;
            case GST_NAVIGATION_COMMAND_RIGHT:
              name = "right";
              break;
            case GST_NAVIGATION_COMMAND_UP:
              name = "up";
              break;
            case GST_NAVIGATION_COMMAND_DOWN:
              name = "down";
              break;
            case GST_NAVIGATION_COMMAND_ACTIVATE:
              name = "activate";
              break;
            case GST_NAVIGATION_COMMAND_PREV_ANGLE:
              name = "prev_angle";
              break;
            case GST_NAVIGATION_COMMAND_NEXT_ANGLE:
              name = "next_angle";
              break;
            default:
              name = "unknown";
              break;
          }
          GST_DEBUG ("received \"%s\" command event", name);
          break;
        }
        case GST_NAVIGATION_EVENT_TOUCH_DOWN:
        case GST_NAVIGATION_EVENT_TOUCH_MOTION:{
          TouchPoint *point;

          point = g_new0 (TouchPoint, 1);
          gst_navigation_event_parse_touch_event (event,
              &(point->id), &(point->x), &(point->y), &(point->pressure));
          GST_DEBUG ("received %s event with id %u at %f, %f",
              (type == GST_NAVIGATION_EVENT_TOUCH_DOWN) ? "touch-down" :
              "touch-motion", point->id, point->x, point->y);
          point->images_left = (fps_n + fps_d - 1) / fps_d;
          /* black */
          point->cy = 0;
          point->cu = 0;
          point->cv = 0;

          g_mutex_lock (&navtest->touch_lock);
          navtest->touches = g_slist_prepend (navtest->touches, point);
          g_mutex_unlock (&navtest->touch_lock);
          break;
        }
        case GST_NAVIGATION_EVENT_TOUCH_UP:{
          TouchPoint *point;

          point = g_new0 (TouchPoint, 1);
          gst_navigation_event_parse_touch_up_event (event,
              &(point->id), &(point->x), &(point->y));
          GST_DEBUG ("received touch-up event with id %u at %f, %f",
              point->id, point->x, point->y);
          point->images_left = (fps_n + fps_d - 1) / fps_d;
          /* black */
          point->cy = 0;
          point->cu = 0;
          point->cv = 0;

          g_mutex_lock (&navtest->touch_lock);
          navtest->touches = g_slist_prepend (navtest->touches, point);
          g_mutex_unlock (&navtest->touch_lock);
          break;
        }
        case GST_NAVIGATION_EVENT_TOUCH_FRAME:{
          GST_DEBUG ("received touch-frame event");
          break;
        }
        case GST_NAVIGATION_EVENT_TOUCH_CANCEL:{
          GST_DEBUG ("received touch-cancel event");
          g_slist_foreach (navtest->touches, (GFunc) g_free, NULL);
          g_slist_free (navtest->touches);
          navtest->touches = NULL;
          break;
        }
        case GST_NAVIGATION_EVENT_INVALID:{
          GST_WARNING ("received invalid event");
          break;
        }
        default:{
          GST_WARNING ("received unknown event");
          break;
        }
      }

      break;
    }
    default:
      break;
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static void
draw_box_planar411 (GstVideoFrame * frame, int x, int y, int radius,
    guint8 colory, guint8 coloru, guint8 colorv)
{
  gint width, height;
  int x1, x2, y1, y2;
  guint8 *d;
  gint stride;

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  if (x < 0 || y < 0 || x >= width || y >= height)
    return;

  x1 = MAX (x - radius, 0);
  x2 = MIN (x + radius, width);
  y1 = MAX (y - radius, 0);
  y2 = MIN (y + radius, height);

  d = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      d[y * stride + x] = colory;
    }
  }

  d = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);

  x1 /= 2;
  x2 /= 2;
  y1 /= 2;
  y2 /= 2;
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      d[y * stride + x] = coloru;
    }
  }

  d = GST_VIDEO_FRAME_PLANE_DATA (frame, 2);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2);

  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x++) {
      d[y * stride + x] = colorv;
    }
  }
}

static GstFlowReturn
gst_navigationtest_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (filter);
  GSList *walk;
  guint8 u = 255, v = 255;

  gst_video_frame_copy (out_frame, in_frame);

  /* Draw mouse events */
  if (navtest->display_mouse) {
    walk = navtest->clicks;
    while (walk) {
      ButtonClick *click = walk->data;

      walk = g_slist_next (walk);
      draw_box_planar411 (out_frame,
          rint (click->x), rint (click->y), 5, click->cy, click->cu, click->cv);
      if (--click->images_left < 1) {
        navtest->clicks = g_slist_remove (navtest->clicks, click);
        g_free (click);
      }
    }

    for (guint i = 0; i <= 28; i++) {
      if (navtest->modifiers & (1 << i)) {
        u /= 2;
        v /= 2;
      }
    }
    draw_box_planar411 (out_frame,
        rint (navtest->mousex), rint (navtest->mousey), 5, 128, u, v);
  }

  /* Draw touch events */
  if (navtest->display_touch) {
    g_mutex_lock (&navtest->touch_lock);
    walk = navtest->touches;
    while (walk) {
      TouchPoint *point = walk->data;

      walk = g_slist_next (walk);
      draw_box_planar411 (out_frame,
          rint (point->x), rint (point->y), 2, point->cy, point->cu, point->cv);
      if (--point->images_left < 1) {
        navtest->touches = g_slist_remove (navtest->touches, point);
        g_free (point);
      }
    }
    g_mutex_unlock (&navtest->touch_lock);
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_navigationtest_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstNavigationtest *navtest = GST_NAVIGATIONTEST (element);

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      g_slist_foreach (navtest->clicks, (GFunc) g_free, NULL);
      g_slist_free (navtest->clicks);
      navtest->clicks = NULL;
      g_slist_foreach (navtest->touches, (GFunc) g_free, NULL);
      g_slist_free (navtest->touches);
      navtest->touches = NULL;
      g_mutex_clear (&navtest->touch_lock);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      g_mutex_init (&navtest->touch_lock);
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
gst_navigationtest_class_init (GstNavigationtestClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;
  GstVideoFilterClass *vfilter_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;
  vfilter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_navigationtest_set_property;
  gobject_class->get_property = gst_navigationtest_get_property;

  /**
   * navigationtest:display-mouse:
   *
   * Toggles display of mouse events.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_DISPLAY_MOUSE,
      g_param_spec_boolean ("display-mouse", "Display mouse",
          "Toggles display of mouse events", TRUE, G_PARAM_READWRITE));

  /**
   * navigationtest:display-touch:
   *
   * Toggles display of touch events.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_DISPLAY_TOUCH,
      g_param_spec_boolean ("display-touch", "Display touch",
          "Toggles display of touchscreen events", TRUE, G_PARAM_READWRITE));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_navigationtest_change_state);

  gst_element_class_set_static_metadata (element_class, "Video navigation test",
      "Filter/Effect/Video",
      "Handle navigation events showing black squares following "
      "mouse pointer and touch points", "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_navigationtest_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_navigationtest_src_template);

  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_navigationtest_src_event);

  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_navigationtest_transform_frame);
}

static void
gst_navigationtest_init (GstNavigationtest * navtest)
{
  navtest->mousex = -1;
  navtest->mousey = -1;
  navtest->display_mouse = TRUE;
  navtest->display_touch = TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (navigationtest_debug, "navigationtest", 0,
      "navigationtest");

  return GST_ELEMENT_REGISTER (navigationtest, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    navigationtest,
    "Template for a video filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
