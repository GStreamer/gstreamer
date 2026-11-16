/* GStreamer
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

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#define CAPS "video/x-raw,format=I420,width=320,height=240,framerate=30/1"

GST_START_TEST (test_qroverlay_changing_data)
{
  GstHarness *h;
  gint i;

  h = gst_harness_new ("qroverlay");
  gst_harness_set_caps_str (h, CAPS, CAPS);

  for (i = 0; i < 5; i++) {
    gchar *data = g_strdup_printf ("frame %d", i);
    GstBuffer *buf;

    g_object_set (h->element, "data", data, NULL);
    g_free (data);

    buf = gst_harness_create_buffer (h, 320 * 240 * 3 / 2);
    GST_BUFFER_PTS (buf) = i * GST_SECOND / 30;
    GST_BUFFER_DURATION (buf) = GST_SECOND / 30;
    fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

    buf = gst_harness_pull (h);
    fail_unless (buf != NULL);
    gst_buffer_unref (buf);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
qroverlay_suite (void)
{
  Suite *s = suite_create ("qroverlay");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_qroverlay_changing_data);

  return s;
}

GST_CHECK_MAIN (qroverlay);
