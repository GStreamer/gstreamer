/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

GST_START_TEST (test_overlay_basic)
{
  GESTextOverlayClip *source;

  ges_init ();

  source = ges_overlay_text_clip_new ();
  fail_unless (source != NULL);

  g_object_unref (source);
}

GST_END_TEST;

GST_START_TEST (test_overlay_properties)
{
  GESTrack *track;
  GESTrackElement *trackelement;
  GESClip *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_VIDEO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object = (GESClip *)
      ges_overlay_text_clip_new ();
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 51);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackelement = ges_clip_create_track_element (object, track->type);
  ges_clip_add_track_element (object, trackelement);
  fail_unless (trackelement != NULL);
  fail_unless (ges_track_element_set_track (trackelement, track));

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 51, 12,
      51, 0, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_DURATION (object), 510);
  assert_equals_uint64 (_INPOINT (object), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, 0, TRUE);

  ges_clip_release_track_element (object, trackelement);
  g_object_unref (object);
}

GST_END_TEST;

GST_START_TEST (test_overlay_in_layer)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *a, *v;
  GESTrackElement *trobj;
  GESTextOverlayClip *source;
  gchar *text;
  gint halign, valign;
  guint32 color;
  gdouble xpos;
  gdouble ypos;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  a = ges_track_audio_raw_new ();
  v = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_overlay_text_clip_new ();

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) layer,
      (GESClip *) source, 0);

  /* specifically test the text property */
  g_object_set (source, "text", (gchar *) "some text", NULL);
  g_object_get (source, "text", &text, NULL);
  assert_equals_string ("some text", text);
  g_free (text);

  trobj = ges_clip_find_track_element (GES_CLIP (source), v, G_TYPE_NONE);

  /* test the font-desc property */
  g_object_set (source, "font-desc", (gchar *) "sans 72", NULL);
  g_object_get (source, "font-desc", &text, NULL);
  assert_equals_string ("sans 72", text);
  g_free (text);

  assert_equals_string ("sans 72",
      ges_track_text_overlay_get_font_desc (GES_TRACK_TEXT_OVERLAY (trobj)));

  /* test halign and valign */
  g_object_set (source, "halignment", (gint)
      GES_TEXT_HALIGN_LEFT, "valignment", (gint) GES_TEXT_VALIGN_TOP, NULL);
  g_object_get (source, "halignment", &halign, "valignment", &valign, NULL);
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  halign =
      ges_track_text_overlay_get_halignment (GES_TRACK_TEXT_OVERLAY (trobj));
  valign =
      ges_track_text_overlay_get_valignment (GES_TRACK_TEXT_OVERLAY (trobj));
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  /* test color */
  g_object_set (source, "color", (gint) 2147483647, NULL);
  g_object_get (source, "color", &color, NULL);
  assert_equals_int (color, 2147483647);

  color = ges_track_text_overlay_get_color (GES_TRACK_TEXT_OVERLAY (trobj));
  assert_equals_int (color, 2147483647);

  /* test xpos */
  g_object_set (source, "xpos", (gdouble) 0.5, NULL);
  g_object_get (source, "xpos", &xpos, NULL);
  assert_equals_float (xpos, 0.5);

  xpos = ges_track_text_overlay_get_xpos (GES_TRACK_TEXT_OVERLAY (trobj));
  assert_equals_float (xpos, 0.5);

  /* test ypos */
  g_object_set (source, "ypos", (gdouble) 0.33, NULL);
  g_object_get (source, "ypos", &ypos, NULL);
  assert_equals_float (ypos, 0.33);

  ypos = ges_track_text_overlay_get_ypos (GES_TRACK_TEXT_OVERLAY (trobj));
  assert_equals_float (ypos, 0.33);

  GST_DEBUG ("removing the source");

  ges_timeline_layer_remove_object (layer, (GESClip *) source);

  GST_DEBUG ("removing the layer");

  g_object_unref (trobj);
  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-overlays");
  TCase *tc_chain = tcase_create ("overlays");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_overlay_basic);
  tcase_add_test (tc_chain, test_overlay_properties);
  tcase_add_test (tc_chain, test_overlay_in_layer);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
