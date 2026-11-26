/* GStreamer
 *
 * unit test for vah265enc: the profile signalled for 10 bit input
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
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/video.h>

static gboolean
encoder_accepts (GstCaps * caps)
{
  GstElementFactory *factory = gst_element_factory_find ("vah265enc");
  gboolean ret = gst_element_factory_can_sink_any_caps (factory, caps);

  gst_object_unref (factory);
  return ret;
}

/* Encodes a few frames and parses the SPS of the stream. */
static void
encode_and_parse_sps (GstCaps * caps, GstH265SPS * sps)
{
  GstHarness *h;
  GstVideoInfo info;
  GstBuffer *buf;
  GstH265Parser *parser;
  gboolean found = FALSE;
  guint i;

  h = gst_harness_new ("vah265enc");
  gst_harness_set_src_caps (h, gst_caps_ref (caps));
  fail_unless (gst_video_info_from_caps (&info, caps));

  for (i = 0; i < 4; i++) {
    buf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&info), NULL);
    gst_buffer_memset (buf, 0, 0x80, GST_VIDEO_INFO_SIZE (&info));
    GST_BUFFER_PTS (buf) = gst_util_uint64_scale (i, GST_SECOND, 30);
    GST_BUFFER_DURATION (buf) = gst_util_uint64_scale (1, GST_SECOND, 30);
    fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  }
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));

  parser = gst_h265_parser_new ();
  while (!found && (buf = gst_harness_try_pull (h))) {
    GstMapInfo map;
    GstH265NalUnit nalu;
    GstH265ParserResult res;
    guint offset = 0;

    fail_unless (gst_buffer_map (buf, &map, GST_MAP_READ));
    while (!found && ((res = gst_h265_parser_identify_nalu (parser, map.data,
                    offset, map.size, &nalu)) == GST_H265_PARSER_OK
            || res == GST_H265_PARSER_NO_NAL_END)) {
      if (nalu.type == GST_H265_NAL_VPS) {
        GstH265VPS vps;

        fail_unless_equals_int (gst_h265_parser_parse_vps (parser, &nalu,
                &vps), GST_H265_PARSER_OK);
      } else if (nalu.type == GST_H265_NAL_SPS) {
        fail_unless_equals_int (gst_h265_parser_parse_sps (parser, &nalu, sps,
                TRUE), GST_H265_PARSER_OK);
        found = TRUE;
      }
      offset = nalu.offset + nalu.size;
    }
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
  }

  gst_h265_parser_free (parser);
  gst_harness_teardown (h);

  fail_unless (found, "no SPS in the encoded stream");
}

GST_START_TEST (main10_profile_idc)
{
  GstH265SPS sps;
  GstCaps *caps = gst_caps_from_string ("video/x-raw,format=P010_10LE,"
      "width=320,height=240,framerate=30/1");

  if (!encoder_accepts (caps)) {
    gst_caps_unref (caps);
    return;                     /* skipped: no 10 bits encoding */
  }

  encode_and_parse_sps (caps, &sps);
  gst_caps_unref (caps);

  assert_equals_int (sps.bit_depth_luma_minus8, 2);
  assert_equals_int (sps.profile_tier_level.profile_idc,
      GST_H265_PROFILE_IDC_MAIN_10);
  fail_unless (sps.profile_tier_level.profile_compatibility_flag[2]);
}

GST_END_TEST;

static Suite *
vah265enc_suite (void)
{
  Suite *s = suite_create ("vah265enc");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, main10_profile_idc);

  return s;
}

int
main (int argc, char **argv)
{
  GstElement *enc;

  gst_check_init (&argc, &argv);

  enc = gst_element_factory_make ("vah265enc", NULL);
  if (!enc)
    return EXIT_SUCCESS;        /* no VA H.265 encoder available */
  gst_object_unref (enc);

  return gst_check_run_suite (vah265enc_suite (), "vah265enc", __FILE__);
}
