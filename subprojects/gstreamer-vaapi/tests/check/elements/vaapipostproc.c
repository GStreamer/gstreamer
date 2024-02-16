/*
 *  vaapipostproc.c - GStreamer unit test for the vaapipostproc element
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <gst/video/navigation.h>

typedef struct
{
  GstElement *pipeline;
  GstElement *source;
  GstElement *filter;
  GstElement *vpp;
  GstElement *sink;
} VppTestContext;

typedef struct
{
  gdouble x;
  gdouble y;
} VppTestCoordinate;

typedef struct
{
  VppTestCoordinate send;
  VppTestCoordinate expect;
} VppTestCoordinateParams;

static void
vpp_test_init_context (VppTestContext * ctx)
{
  GST_INFO ("initing context");

  ctx->pipeline = gst_pipeline_new ("pipeline");
  fail_unless (ctx->pipeline != NULL);

  ctx->source = gst_element_factory_make ("videotestsrc", "src");
  fail_unless (ctx->source != NULL, "Failed to create videotestsrc element");

  ctx->filter = gst_element_factory_make ("capsfilter", "filter");
  fail_unless (ctx->filter != NULL, "Failed to create caps filter element");

  ctx->vpp = gst_element_factory_make ("vaapipostproc", "vpp");
  fail_unless (ctx->vpp != NULL, "Failed to create vaapipostproc element");

  ctx->sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (ctx->sink != NULL, "Failed to create fakesink element");

  gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->source, ctx->filter, ctx->vpp,
      ctx->sink, NULL);
  gst_element_link_many (ctx->source, ctx->filter, ctx->vpp, ctx->sink, NULL);
}

static void
vpp_test_deinit_context (VppTestContext * ctx)
{
  GST_INFO ("deiniting context");

  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
  gst_object_unref (ctx->pipeline);
  memset (ctx, 0x00, sizeof (VppTestContext));
}

static void
vpp_test_set_crop (VppTestContext * ctx, gint l, gint r, gint t, gint b)
{
  GST_LOG ("%d %d %d %0d", l, r, t, b);
  g_object_set (ctx->vpp, "crop-left", l, "crop-right", r,
      "crop-top", t, "crop-bottom", b, NULL);
}

static void
vpp_test_set_orientation (VppTestContext * ctx, GstVideoOrientationMethod m)
{
  GST_LOG ("%u", m);
  g_object_set (ctx->vpp, "video-direction", m, NULL);
}

static void
vpp_test_set_dimensions (VppTestContext * ctx, gint w, gint h)
{
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, NULL);
  GST_LOG ("%dx%d", w, h);
  g_object_set (ctx->filter, "caps", caps, NULL);
  gst_caps_unref (caps);
}

static GstPadProbeReturn
cb_mouse_event (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  VppTestCoordinate *coord = data;
  gdouble x = 0, y = 0;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    switch (gst_navigation_event_get_type (event)) {
      case GST_NAVIGATION_EVENT_MOUSE_MOVE:
        if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
          coord->x = x;
          coord->y = y;
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
        if (gst_navigation_event_parse_mouse_button_event (event, NULL, &x, &y)) {
          coord->x = x;
          coord->y = y;
        }
        break;
      default:
        break;
    }
  }

  return GST_PAD_PROBE_OK;
}

static void
vpp_test_mouse_events (VppTestContext * ctx,
    const VppTestCoordinateParams * const params, const size_t nparams)
{
  GstEvent *event = NULL;
  VppTestCoordinate probed = { 0, };
  guint i, j;

  /* probe mouse events propagated up from vaapipostproc */
  GstPad *pad = gst_element_get_static_pad (ctx->source, "src");
  gulong id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) cb_mouse_event, &probed, NULL);

  const char *mouse_events[] = {
    "mouse-move",
    "mouse-button-press",
    "mouse-button-release",
  };

  fail_unless (gst_element_set_state (ctx->pipeline, GST_STATE_PAUSED)
      != GST_STATE_CHANGE_FAILURE);
  fail_unless (gst_element_get_state (ctx->pipeline, NULL, NULL, -1)
      == GST_STATE_CHANGE_SUCCESS);

  for (i = 0; i < nparams; ++i) {
    for (j = 0; j < G_N_ELEMENTS (mouse_events); ++j) {
      probed.x = probed.y = -1;

      switch (j) {
        case 0:
          event = gst_navigation_event_new_mouse_move (params[i].send.x,
              params[i].send.y, GST_NAVIGATION_MODIFIER_NONE);
          break;
        case 1:
          event = gst_navigation_event_new_mouse_button_press (0,
              params[i].send.x, params[i].send.y, GST_NAVIGATION_MODIFIER_NONE);
          break;
        case 2:
          event = gst_navigation_event_new_mouse_button_release (0,
              params[i].send.x, params[i].send.y, GST_NAVIGATION_MODIFIER_NONE);
          break;
      }

      GST_LOG ("sending %s event %fx%f", mouse_events[j], params[i].send.x,
          params[i].send.y);
      gst_element_send_event (ctx->pipeline, event);

      GST_LOG ("probed %s event %fx%f", mouse_events[j], probed.x, probed.y);
      GST_LOG ("expect %s event %fx%f", mouse_events[j], params[i].expect.x,
          params[i].expect.y);

      fail_unless (params[i].expect.x == probed.x);
      fail_unless (params[i].expect.y == probed.y);
    }
  }

  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);

  gst_pad_remove_probe (pad, id);
  gst_object_unref (pad);
}

static void
vpp_test_crop_mouse_events (VppTestContext * ctx, gint w, gint h, gint l,
    gint r, gint t, gint b)
{
  const gdouble xmin = 0.0;
  const gdouble ymin = 0.0;
  const gdouble xmax = w - (l + r) - 1;
  const gdouble ymax = h - (t + b) - 1;
  const gdouble xctr = xmax / 2;
  const gdouble yctr = ymax / 2;
  const gdouble xrand = g_random_double_range (xmin, xmax);
  const gdouble yrand = g_random_double_range (ymin, ymax);

  const gdouble e_xmin = xmin + l;
  const gdouble e_ymin = ymin + t;
  const gdouble e_xmax = xmax + l;
  const gdouble e_ymax = ymax + t;
  const gdouble e_xctr = xctr + l;
  const gdouble e_yctr = yctr + t;
  const gdouble e_xrand = xrand + l;
  const gdouble e_yrand = yrand + t;

  const VppTestCoordinateParams params[] = {
    {{xmin, ymin}, {e_xmin, e_ymin}},   /* left-top */
    {{xmin, yctr}, {e_xmin, e_yctr}},   /* left-center */
    {{xmin, ymax}, {e_xmin, e_ymax}},   /* left-bottom */

    {{xmax, ymin}, {e_xmax, e_ymin}},   /* right-top */
    {{xmax, yctr}, {e_xmax, e_yctr}},   /* right-center */
    {{xmax, ymax}, {e_xmax, e_ymax}},   /* right-bottom */

    {{xctr, ymin}, {e_xctr, e_ymin}},   /* center-top */
    {{xctr, yctr}, {e_xctr, e_yctr}},   /* center */
    {{xctr, ymax}, {e_xctr, e_ymax}},   /* center-bottom */

    {{xrand, yrand}, {e_xrand, e_yrand}},       /* random */
  };

  vpp_test_set_dimensions (ctx, w, h);
  vpp_test_set_crop (ctx, l, r, t, b);
  vpp_test_mouse_events (ctx, params, G_N_ELEMENTS (params));
}

GST_START_TEST (test_crop_mouse_events)
{
  VppTestContext ctx;

  vpp_test_init_context (&ctx);

  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 1, 0, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 1, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 1, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 0, 1);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 63, 0, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 63, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 63, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 0, 63);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 63, 0, 0, 1);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 63, 1, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 1, 63, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 1, 0, 0, 63);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 0, 0, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 32, 0, 0, 128);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 32, 128, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 0, 128, 32, 0);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 128, 0, 0, 32);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 1, 1, 1, 1);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 63, 63, 63, 63);
  vpp_test_crop_mouse_events (&ctx, 160, 160, 64, 64, 64, 64);

  vpp_test_deinit_context (&ctx);
}

GST_END_TEST;

static void
vpp_test_orientation_mouse_events (VppTestContext * ctx, gint w, gint h)
{
  size_t i;
  const gdouble xmin = 0.0;
  const gdouble ymin = 0.0;
  const gdouble xmax = w - 1;
  const gdouble ymax = h - 1;
  const VppTestCoordinateParams params[8][4] = {
    /* (0) identity */
    {
          {{xmin, ymin}, {xmin, ymin}},
          {{xmax, ymin}, {xmax, ymin}},
          {{xmin, ymax}, {xmin, ymax}},
          {{xmax, ymax}, {xmax, ymax}},
        },
    /* (1) 90 Rotation */
    {
          {{ymin, xmin}, {xmin, ymax}},
          {{ymax, xmin}, {xmin, ymin}},
          {{ymin, xmax}, {xmax, ymax}},
          {{ymax, xmax}, {xmax, ymin}},
        },
    /* (2) 180 Rotation */
    {
          {{xmin, ymin}, {xmax, ymax}},
          {{xmax, ymin}, {xmin, ymax}},
          {{xmin, ymax}, {xmax, ymin}},
          {{xmax, ymax}, {xmin, ymin}},
        },
    /* (3) 270 Rotation */
    {
          {{ymin, xmin}, {xmax, ymin}},
          {{ymax, xmin}, {xmax, ymax}},
          {{ymin, xmax}, {xmin, ymin}},
          {{ymax, xmax}, {xmin, ymax}},
        },
    /* (4) Horizontal Flip */
    {
          {{xmin, ymin}, {xmax, ymin}},
          {{xmax, ymin}, {xmin, ymin}},
          {{xmin, ymax}, {xmax, ymax}},
          {{xmax, ymax}, {xmin, ymax}},
        },
    /* (5) Vertical Flip */
    {
          {{xmin, ymin}, {xmin, ymax}},
          {{xmax, ymin}, {xmax, ymax}},
          {{xmin, ymax}, {xmin, ymin}},
          {{xmax, ymax}, {xmax, ymin}},
        },
    /* (6) Vertical Flip + 90 Rotation */
    {
          {{ymin, xmin}, {xmin, ymin}},
          {{ymax, xmin}, {xmin, ymax}},
          {{ymin, xmax}, {xmax, ymin}},
          {{ymax, xmax}, {xmax, ymax}},
        },
    /* (7) Horizontal Flip + 90 Rotation */
    {
          {{ymin, xmin}, {xmax, ymax}},
          {{ymax, xmin}, {xmax, ymin}},
          {{ymin, xmax}, {xmin, ymax}},
          {{ymax, xmax}, {xmin, ymin}},
        },
  };

  vpp_test_set_dimensions (ctx, w, h);

  for (i = 0; i < 8; ++i) {
    vpp_test_set_orientation (ctx, i);
    vpp_test_mouse_events (ctx, params[i], 4);
  }
}

GST_START_TEST (test_orientation_mouse_events)
{
  VppTestContext ctx;

  vpp_test_init_context (&ctx);

  vpp_test_orientation_mouse_events (&ctx, 160, 320);
  vpp_test_orientation_mouse_events (&ctx, 161, 320);
  vpp_test_orientation_mouse_events (&ctx, 160, 321);
  vpp_test_orientation_mouse_events (&ctx, 161, 321);

  vpp_test_orientation_mouse_events (&ctx, 320, 160);
  vpp_test_orientation_mouse_events (&ctx, 320, 161);
  vpp_test_orientation_mouse_events (&ctx, 321, 160);
  vpp_test_orientation_mouse_events (&ctx, 321, 161);

  vpp_test_deinit_context (&ctx);
}

GST_END_TEST;

static Suite *
vaapipostproc_suite (void)
{
  Suite *s = suite_create ("vaapipostproc");
  TCase *tc_chain = tcase_create ("general");
  gboolean has_vaapipostproc = FALSE;

  {
    GstElement *vaapipostproc;

    vaapipostproc = gst_element_factory_make ("vaapipostproc", NULL);
    if (vaapipostproc) {
      has_vaapipostproc = TRUE;
      gst_object_unref (vaapipostproc);
    }
  }

  suite_add_tcase (s, tc_chain);

  if (has_vaapipostproc) {
    tcase_add_test (tc_chain, test_crop_mouse_events);
    tcase_add_test (tc_chain, test_orientation_mouse_events);
  }

  return s;
}

GST_CHECK_MAIN (vaapipostproc);
