/* GStreamer
 *
 * unit test for rtphdrext-colorspace elements
 *
 * Copyright (C) 2020-2021 Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
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
#include <gst/video/video-color.h>
#include <gst/video/video-hdr.h>
#include <gst/rtp/gstrtphdrext-colorspace.h>

#define EXTMAP_ID 9

const GstVideoColorimetry expected_colorimetry = {
  GST_VIDEO_COLOR_RANGE_0_255,
  GST_VIDEO_COLOR_MATRIX_BT601,
  GST_VIDEO_TRANSFER_BT2020_10,
  GST_VIDEO_COLOR_PRIMARIES_BT2020
};

const GstVideoChromaSite expected_chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;

const GstVideoMasteringDisplayInfo expected_display_info = {
  {{1, 2}, {3, 4}, {5, 6}},
  {7, 8},
  10000,
  42
};

const GstVideoContentLightLevel expected_content_light_level = {
  35987, 28543
};

const guint8 vp8_payload[] = {
  0x30, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0xb0, 0x00, 0x90, 0x00, 0x06, 0x47,
  0x08, 0x85, 0x85, 0x88, 0x99, 0x84, 0x88, 0x21, 0x00
};

/* validate that upstream colorspace information get embedded into RTP packets
 * as Color Space header extension and correctly reconstructed in depayloader's
 * srccaps. This variant of the test case creates the one-byte form of the
 * header (without HDR metadata).
 */
GST_START_TEST (test_rtphdrext_colorspace_onebyte)
{
  GstHarness *h;
  GstElement *pay, *depay;
  GstVideoColorimetry colorimetry;
  GstVideoChromaSite chroma_site;
  gchar *colorimetry_str;
  const gchar *str;
  GstCaps *src_caps, *caps, *expected_caps;
  GstPad *pad;
  GstStructure *s;
  GstRTPHeaderExtension *pay_ext, *depay_ext;

  h = gst_harness_new_parse ("rtpvp8pay ! rtpvp8depay");

  pay = gst_harness_find_element (h, "rtpvp8pay");
  depay = gst_harness_find_element (h, "rtpvp8depay");

  pay_ext =
      GST_RTP_HEADER_EXTENSION (gst_element_factory_make ("rtphdrextcolorspace",
          NULL));
  depay_ext =
      GST_RTP_HEADER_EXTENSION (gst_element_factory_make ("rtphdrextcolorspace",
          NULL));

  gst_rtp_header_extension_set_id (pay_ext, EXTMAP_ID);
  gst_rtp_header_extension_set_id (depay_ext, EXTMAP_ID);

  g_signal_emit_by_name (pay, "add-extension", pay_ext);
  g_signal_emit_by_name (depay, "add-extension", depay_ext);

  colorimetry_str = gst_video_colorimetry_to_string (&expected_colorimetry);
  src_caps = gst_caps_new_simple ("video/x-vp8",
      "colorimetry", G_TYPE_STRING, colorimetry_str,
      "chroma-site", G_TYPE_STRING,
      gst_video_chroma_to_string (expected_chroma_site), NULL);

  gst_harness_set_src_caps (h, src_caps);

  gst_harness_push (h,
      gst_buffer_new_memdup (vp8_payload, sizeof (vp8_payload)));

  /* verify depayloader correctly reconstructs colorspace information in
   * its srccaps. */
  pad = gst_element_get_static_pad (depay, "src");
  caps = gst_pad_get_current_caps (pad);
  s = gst_caps_get_structure (caps, 0);
  gst_object_unref (pad);

  str = gst_structure_get_string (s, "colorimetry");
  fail_unless (str != NULL);
  gst_video_colorimetry_from_string (&colorimetry, str);
  fail_unless (gst_video_colorimetry_is_equal (&colorimetry,
          &expected_colorimetry));

  str = gst_structure_get_string (s, "chroma-site");
  fail_unless (str != NULL);
  chroma_site = gst_video_chroma_from_string (str);
  fail_unless_equals_int (chroma_site, expected_chroma_site);

  gst_caps_unref (caps);

  /* verify the presence of Color Space extmap in caps */
  pad = gst_element_get_static_pad (pay, "src");
  caps = gst_pad_get_current_caps (pad);
  expected_caps = gst_caps_from_string ("application/x-rtp, "
      "extmap-" G_STRINGIFY (EXTMAP_ID) "=" GST_RTP_HDREXT_COLORSPACE_URI);
  fail_unless (gst_caps_is_subset (caps, expected_caps));
  gst_object_unref (pad);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  g_free (colorimetry_str);

  gst_object_unref (pay_ext);
  gst_object_unref (depay_ext);
  gst_object_unref (pay);
  gst_object_unref (depay);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* validate that upstream colorspace information get embedded into RTP packets
 * as Color Space header extension and correctly reconstructed in depayloader's
 * srccaps. This variant of the test case creates the two-byte form of the
 * header (including HDR metadata).
 */
GST_START_TEST (test_rtphdrext_colorspace_twobyte)
{
  GstHarness *h;
  GstElement *pay, *depay;
  GstVideoColorimetry colorimetry;
  GstVideoChromaSite chroma_site;
  GstVideoMasteringDisplayInfo display_info;
  GstVideoContentLightLevel content_light_level;
  gchar *colorimetry_str;
  const gchar *str;
  GstCaps *src_caps, *caps, *expected_caps;
  GstPad *pad;
  GstStructure *s;
  GstRTPHeaderExtension *pay_ext, *depay_ext;

  h = gst_harness_new_parse ("rtpvp8pay ! rtpvp8depay");

  pay = gst_harness_find_element (h, "rtpvp8pay");
  depay = gst_harness_find_element (h, "rtpvp8depay");

  pay_ext =
      GST_RTP_HEADER_EXTENSION (gst_element_factory_make ("rtphdrextcolorspace",
          NULL));
  depay_ext =
      GST_RTP_HEADER_EXTENSION (gst_element_factory_make ("rtphdrextcolorspace",
          NULL));

  gst_rtp_header_extension_set_id (pay_ext, EXTMAP_ID);
  gst_rtp_header_extension_set_id (depay_ext, EXTMAP_ID);

  g_signal_emit_by_name (pay, "add-extension", pay_ext);
  g_signal_emit_by_name (depay, "add-extension", depay_ext);

  colorimetry_str = gst_video_colorimetry_to_string (&expected_colorimetry);
  src_caps = gst_caps_new_simple ("video/x-vp8",
      "colorimetry", G_TYPE_STRING, colorimetry_str,
      "chroma-site", G_TYPE_STRING,
      gst_video_chroma_to_string (expected_chroma_site), NULL);
  gst_video_mastering_display_info_add_to_caps (&expected_display_info,
      src_caps);
  gst_video_content_light_level_add_to_caps (&expected_content_light_level,
      src_caps);

  gst_harness_set_src_caps (h, src_caps);

  gst_harness_push (h,
      gst_buffer_new_memdup (vp8_payload, sizeof (vp8_payload)));

  /* verify depayloader correctly reconstructs colorspace information in
   * its srccaps. */
  pad = gst_element_get_static_pad (depay, "src");
  caps = gst_pad_get_current_caps (pad);
  s = gst_caps_get_structure (caps, 0);
  gst_object_unref (pad);

  str = gst_structure_get_string (s, "colorimetry");
  fail_unless (str != NULL);
  gst_video_colorimetry_from_string (&colorimetry, str);
  fail_unless (gst_video_colorimetry_is_equal (&colorimetry,
          &expected_colorimetry));

  str = gst_structure_get_string (s, "chroma-site");
  fail_unless (str != NULL);
  chroma_site = gst_video_chroma_from_string (str);
  fail_unless_equals_int (chroma_site, expected_chroma_site);

  gst_video_mastering_display_info_from_caps (&display_info, caps);
  fail_unless (gst_video_mastering_display_info_is_equal (&display_info,
          &expected_display_info));

  gst_video_content_light_level_from_caps (&content_light_level, caps);
  fail_unless (gst_video_content_light_level_is_equal (&content_light_level,
          &expected_content_light_level));

  gst_caps_unref (caps);

  /* verify the presence of Color Space extmap in caps */
  pad = gst_element_get_static_pad (pay, "src");
  caps = gst_pad_get_current_caps (pad);
  expected_caps = gst_caps_from_string ("application/x-rtp, "
      "extmap-" G_STRINGIFY (EXTMAP_ID) "=" GST_RTP_HDREXT_COLORSPACE_URI);
  fail_unless (gst_caps_is_subset (caps, expected_caps));
  gst_object_unref (pad);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  g_free (colorimetry_str);

  gst_object_unref (pay_ext);
  gst_object_unref (depay_ext);
  gst_object_unref (pay);
  gst_object_unref (depay);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtphdrext_colorspace_suite (void)
{
  Suite *s = suite_create ("rtphdrext_colorspace");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_rtphdrext_colorspace_onebyte);
  tcase_add_test (tc_chain, test_rtphdrext_colorspace_twobyte);

  return s;
}

GST_CHECK_MAIN (rtphdrext_colorspace)
