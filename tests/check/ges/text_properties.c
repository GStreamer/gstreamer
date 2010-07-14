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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>
#include <gst/check/gstcheck.h>

#define gnl_object_check(gnlobj, start, duration, mstart, mduration, priority, active) { \
  guint64 pstart, pdur, pmstart, pmdur, pprio; 			\
  g_object_get (gnlobj, "start", &pstart, "duration", &pdur,		\
		"media-start", &pmstart, "media-duration", &pmdur,	\
		"priority", &pprio, 			\
		NULL);							\
  assert_equals_uint64 (pstart, start);					\
  assert_equals_uint64 (pdur, duration);					\
  assert_equals_uint64 (pmstart, mstart);					\
  assert_equals_uint64 (pmdur, mduration);					\
  assert_equals_int (pprio, priority);					\
  }

GST_START_TEST (test_text_properties_in_layer)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *a, *v;
  GESTrackObject *trobj;
  GESTimelineTestSource *source;
  gchar *text;
  gint halign, valign;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  a = ges_track_audio_raw_new ();
  v = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_timeline_test_source_new ();

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) layer,
      (GESTimelineObject *) source, 0);

  trobj =
      ges_timeline_object_find_track_object (GES_TIMELINE_OBJECT (source), v,
      GES_TYPE_TRACK_TEXT_OVERLAY);

  assert_equals_int (trobj->active, FALSE);

  /* specifically test the text property */
  g_object_set (source, "text", (gchar *) "some text", NULL);
  g_object_get (source, "text", &text, NULL);
  assert_equals_string ("some text", text);
  g_free (text);

  assert_equals_int (trobj->active, TRUE);

  /* test the font-desc property */
  g_object_set (source, "font-desc", (gchar *) "sans 72", NULL);
  g_object_get (source, "font-desc", &text, NULL);
  assert_equals_string ("sans 72", text);
  g_free (text);

  text = ((GESTrackTextOverlay *) trobj)->font_desc;
  assert_equals_string ("sans 72", text);

  g_object_set (source, "text", (gchar *) NULL, NULL);
  assert_equals_int (trobj->active, FALSE);

  /* test halign and valign */
  g_object_set (source, "halignment", (gint)
      GES_TEXT_HALIGN_LEFT, "valignment", (gint) GES_TEXT_VALIGN_TOP, NULL);
  g_object_get (source, "halignment", &halign, "valignment", &valign, NULL);
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  halign = ((GESTrackTextOverlay *) trobj)->halign;
  valign = ((GESTrackTextOverlay *) trobj)->valign;
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  GST_DEBUG ("removing the source");

  ges_timeline_layer_remove_object (layer, (GESTimelineObject *) source);

  GST_DEBUG ("removing the layer");

  g_object_unref (trobj);
  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("filesource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_text_properties_in_layer);

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
