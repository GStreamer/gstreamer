/* GStreamer
 * Copyright (C) 2013 Sebastian Rasmussen <sebras@hotmail.com>
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

#include <rtsp-token.h>

GST_START_TEST (test_token)
{
  GstRTSPToken *token;
  GstRTSPToken *token2;
  GstRTSPToken *copy;
  GstStructure *str;

  token = gst_rtsp_token_new_empty ();
  fail_if (gst_rtsp_token_is_allowed (token, "missing"));
  gst_rtsp_token_unref (token);

  token = gst_rtsp_token_new ("role", G_TYPE_STRING, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");
  fail_unless (gst_rtsp_token_is_allowed (token, "permission1"));
  fail_if (gst_rtsp_token_is_allowed (token, "permission2"));
  fail_if (gst_rtsp_token_is_allowed (token, "missing"));
  copy = GST_RTSP_TOKEN (gst_mini_object_copy (GST_MINI_OBJECT (token)));
  gst_rtsp_token_unref (token);
  fail_unless_equals_string (gst_rtsp_token_get_string (copy, "role"), "user");
  fail_unless (gst_rtsp_token_is_allowed (copy, "permission1"));
  fail_if (gst_rtsp_token_is_allowed (copy, "permission2"));
  fail_if (gst_rtsp_token_is_allowed (copy, "missing"));
  gst_rtsp_token_unref (copy);

  token = gst_rtsp_token_new ("role", G_TYPE_STRING, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");
  fail_unless (gst_rtsp_token_is_allowed (token, "permission1"));
  fail_if (gst_rtsp_token_is_allowed (token, "permission2"));
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");

  fail_unless (gst_mini_object_is_writable (GST_MINI_OBJECT (token)));
  fail_unless (gst_rtsp_token_writable_structure (token) != NULL);
  fail_unless (gst_rtsp_token_get_structure (token) != NULL);

  token2 = gst_rtsp_token_ref (token);

  fail_if (gst_mini_object_is_writable (GST_MINI_OBJECT (token)));
  ASSERT_CRITICAL (fail_unless (gst_rtsp_token_writable_structure (token) ==
          NULL));
  fail_unless (gst_rtsp_token_get_structure (token) != NULL);

  gst_rtsp_token_unref (token2);

  fail_unless (gst_mini_object_is_writable (GST_MINI_OBJECT (token)));
  fail_unless (gst_rtsp_token_writable_structure (token) != NULL);
  fail_unless (gst_rtsp_token_get_structure (token) != NULL);

  str = gst_rtsp_token_writable_structure (token);
  gst_structure_set (str, "permission2", G_TYPE_BOOLEAN, TRUE, NULL);
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");
  fail_unless (gst_rtsp_token_is_allowed (token, "permission1"));
  fail_unless (gst_rtsp_token_is_allowed (token, "permission2"));
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");

  gst_rtsp_token_set_bool (token, "permission3", FALSE);
  fail_unless (!gst_rtsp_token_is_allowed (token, "permission3"));
  gst_rtsp_token_set_bool (token, "permission4", TRUE);
  fail_unless (gst_rtsp_token_is_allowed (token, "permission4"));

  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"), "user");
  gst_rtsp_token_set_string (token, "role", "admin");
  fail_unless_equals_string (gst_rtsp_token_get_string (token, "role"),
      "admin");

  gst_rtsp_token_unref (token);
}

GST_END_TEST;

static Suite *
rtsptoken_suite (void)
{
  Suite *s = suite_create ("rtsptoken");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_token);

  return s;
}

GST_CHECK_MAIN (rtsptoken);
