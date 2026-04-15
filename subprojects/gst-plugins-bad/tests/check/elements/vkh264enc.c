/* GStreamer
 *
 * unit test for vkh264enc element
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include <gst/check/check.h>
#include <gst/check/gstharness.h>

static gboolean
check_enc_available (void)
{
  GstElement *encoder;

  encoder = gst_element_factory_make ("vulkanh264enc", NULL);
  if (!encoder) {
    GST_WARNING ("vulkanh264enc is not available");
    return FALSE;
  }

  gst_object_unref (encoder);
  return TRUE;
}

GST_START_TEST (test_encode_single_frame)
{
  GstHarness *h;

  h = gst_harness_new_parse ("videotestsrc num-buffers=1 pattern=blue ! "
      "vulkanupload ! vulkanh264enc ! h264parse");
  fail_unless (h, "No harness object");

  gst_harness_set_sink_caps_str (h,
      "video/x-h264, profile=main, width=(int)320, height=(int)240, "
      "framerate=(fraction)30/1");

  gst_harness_play (h);

  {
    GstBuffer *outbuf = gst_harness_pull (h);
    fail_unless (outbuf, "No buffer returned");

    {
      GstMapInfo map;
      gboolean ret = gst_buffer_map (outbuf, &map, GST_MAP_READ);
      fail_unless (ret, "Cannot map output buffer");
      GST_MEMDUMP ("encoded buffer", map.data, map.size);
      gst_buffer_unmap (outbuf, &map);
    }

    gst_buffer_unref (outbuf);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vkh264enc_suite (void)
{
  Suite *s = suite_create ("vkh264enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  if (check_enc_available ()) {
    tcase_add_test (tc_chain, test_encode_single_frame);
  }

  return s;
}

GST_CHECK_MAIN (vkh264enc);
