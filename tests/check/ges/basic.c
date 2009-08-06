/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

GST_START_TEST (test_ges_init)
{
  /* Yes, I know.. minimalistic... */
  ges_init ();
}

GST_END_TEST;

GST_START_TEST (test_ges_scenario)
{
  GESTimelinePipeline *pipeline;
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESTimelineSource *source;

  /* This is the simplest scenario ever */

  pipeline = ges_timeline_pipeline_new ();
  fail_unless (pipeline != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  layer = ges_timeline_layer_new ();
  fail_unless (layer != NULL);

  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (layer->timeline == timeline);

  track = ges_track_new ();
  fail_unless (track != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));
  fail_unless (track->timeline == timeline);

  source = ges_timeline_source_new ();
  fail_unless (source != NULL);

  fail_unless (ges_timeline_layer_add_object (layer,
          GES_TIMELINE_OBJECT (source)));
  fail_unless (GES_TIMELINE_OBJECT (source)->layer == layer);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_ges_init);
  tcase_add_test (tc_chain, test_ges_scenario);

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
