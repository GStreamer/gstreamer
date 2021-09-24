/* GStreamer Editing Services
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
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

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static gboolean
compare_caps_from_string (GstCaps * caps, const gchar * desc)
{
  GstCaps *new_caps = gst_caps_from_string (desc);
  gboolean ret = TRUE;

  if (!gst_caps_is_strictly_equal (caps, new_caps))
    ret = FALSE;

  gst_caps_unref (new_caps);
  return ret;
}

GST_START_TEST (test_update_restriction_caps)
{
  GESTrack *track;
  GstCaps *original;
  GstCaps *new;
  GstCaps *current;

  ges_init ();

  track = GES_TRACK (ges_audio_track_new ());

  original = gst_caps_from_string ("audio/x-raw, format=S32LE");
  ges_track_set_restriction_caps (track, original);

  new = gst_caps_from_string ("audio/x-raw, format=S16LE, width=720");
  ges_track_update_restriction_caps (track, new);
  g_object_get (track, "restriction-caps", &current, NULL);

  /* Assuming the format for to_string doesn't change */
  fail_unless (compare_caps_from_string (current,
          "audio/x-raw, format=(string)S16LE, width=(int)720"));

  gst_caps_unref (new);
  new = gst_caps_from_string ("audio/x-raw, width=360");
  ges_track_update_restriction_caps (track, new);
  gst_caps_unref (current);
  g_object_get (track, "restriction-caps", &current, NULL);
  fail_unless (compare_caps_from_string (current,
          "audio/x-raw, format=(string)S16LE, width=(int)360"));

  gst_caps_append_structure (new,
      gst_structure_new_from_string ("audio/x-raw, format=S16LE"));
  ges_track_update_restriction_caps (track, new);
  gst_caps_unref (current);
  g_object_get (track, "restriction-caps", &current, NULL);
  fail_unless (compare_caps_from_string (current,
          "audio/x-raw, format=(string)S16LE, width=(int)360; audio/x-raw, format=S16LE"));

  gst_caps_unref (new);
  new =
      gst_caps_from_string
      ("audio/x-raw, width=240; audio/x-raw, format=S32LE");
  ges_track_update_restriction_caps (track, new);
  gst_caps_unref (current);
  g_object_get (track, "restriction-caps", &current, NULL);
  fail_unless (compare_caps_from_string (current,
          "audio/x-raw, format=(string)S16LE, width=(int)240; audio/x-raw, format=S32LE"));

  gst_caps_unref (new);
  gst_caps_unref (original);
  gst_caps_unref (current);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-track");
  TCase *tc_chain = tcase_create ("track");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_update_restriction_caps);

  return s;
}

GST_CHECK_MAIN (ges);
