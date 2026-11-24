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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/check.h>
#include <gst/check/gstharness.h>

/* A fragmented packet (F != 0) carries a 4 byte header followed by a 2 byte
 * length. A packet that is too short for that must be dropped without
 * touching a NULL subbuffer. */
static void
check_short_fragment (const gchar * element, const gchar * caps,
    guint8 hdr_flags)
{
  GstHarness *h = gst_harness_new (element);
  guint8 pkt[12 + 5] = { 0x80, 0x60, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
    0x01, 0x02, 0x03, hdr_flags, 0x00
  };
  guint len;

  gst_harness_set_src_caps_str (h, caps);

  /* payload of 4 and 5 bytes: header only, header plus half the length */
  for (len = 16; len <= 17; len++) {
    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h, gst_buffer_new_memdup (pkt, len)));
  }
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  gst_harness_teardown (h);
}

GST_START_TEST (test_vorbis_depay_short_fragment)
{
  /* F=1 (start), VDT=1 (config), 1 packet */
  check_short_fragment ("rtpvorbisdepay",
      "application/x-rtp,media=audio,clock-rate=44100,"
      "encoding-name=VORBIS,payload=96", 0x51);
}

GST_END_TEST;

GST_START_TEST (test_theora_depay_short_fragment)
{
  /* F=1 (start), TDT=1 (config), 1 packet */
  check_short_fragment ("rtptheoradepay",
      "application/x-rtp,media=video,clock-rate=90000,"
      "encoding-name=THEORA,payload=96", 0x51);
}

GST_END_TEST;

static Suite *
rtpxiph_suite (void)
{
  Suite *s = suite_create ("rtpxiph");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_vorbis_depay_short_fragment);
  tcase_add_test (tc_chain, test_theora_depay_short_fragment);

  return s;
}

GST_CHECK_MAIN (rtpxiph);
