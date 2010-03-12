/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

static gboolean
my_fill_track_func (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackobjec:%p, gnlobj:%p",
      object, trobject, gnlobj);

  /* Let's just put a fakesource in for the time being */
  src = gst_element_factory_make ("fakesrc", NULL);
  return gst_bin_add (GST_BIN (gnlobj), src);
}

#define gnl_object_check(gnlobj, start, duration, mstart, mduration, priority, active) { \
  guint64 pstart, pdur, pmstart, pmdur;					\
  guint32 pprio;							\
  gboolean pact;							\
  g_object_get (gnlobj, "start", &pstart, "duration", &pdur,		\
		"media-start", &pmstart, "media-duration", &pmdur,	\
		"priority", &pprio, "active", &pact,			\
		NULL);							\
  assert_equals_uint64 (pstart, start);					\
  assert_equals_uint64 (pdur, duration);					\
  assert_equals_uint64 (pmstart, mstart);					\
  assert_equals_uint64 (pmdur, mduration);					\
  assert_equals_int (pprio, priority);					\
  assert_equals_int (pact, active);					\
  }


GST_START_TEST (test_layer_properties)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineObject *object;

  ges_init ();

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_timeline_layer_new ();

  /* The default priority is 0 */
  fail_unless_equals_int (layer->priority, 0);

  fail_unless (ges_timeline_add_layer (timeline, layer));

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (track != NULL);
  fail_unless (ges_timeline_add_track (timeline, track));

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 42);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 51);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 12);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_PRIORITY (object), 0);

  /* Add the object to the timeline */
  fail_unless (ges_timeline_layer_add_object (layer,
          GES_TIMELINE_OBJECT (object)));
  trackobject = ges_timeline_object_find_track_object (object, track);
  fail_unless (trackobject != NULL);

  /* This is not a SimpleLayer, therefore the properties shouldn't have changed */
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 42);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 51);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 12);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_PRIORITY (object), 0);
  gnl_object_check (trackobject->gnlobject, 42, 51, 12, 51, 0, TRUE);

  /* Change the priority of the layer */
  g_object_set (layer, "priority", 1, NULL);
  assert_equals_int (layer->priority, 1);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_PRIORITY (object), 10);
  gnl_object_check (trackobject->gnlobject, 42, 51, 12, 51, 10, TRUE);

  /* Change it to an insanely high value */
  g_object_set (layer, "priority", 1000000, NULL);
  assert_equals_int (layer->priority, 1000000);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_PRIORITY (object), 10000000);
  gnl_object_check (trackobject->gnlobject, 42, 51, 12, 51, 10000000, TRUE);

  /* and back to 0 */
  g_object_set (layer, "priority", 0, NULL);
  assert_equals_int (layer->priority, 0);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_PRIORITY (object), 0);
  gnl_object_check (trackobject->gnlobject, 42, 51, 12, 51, 0, TRUE);

  g_object_unref (trackobject);
  fail_unless (ges_timeline_layer_remove_object (layer, object));
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("timeline-layer");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_layer_properties);

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
