/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <tsaunier@gnome.org>
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
 * You should have received track_audio copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>
#include <ges/ges-track-operation.h>
#include <gst/check/gstcheck.h>

GST_START_TEST (test_effect_basic)
{
  GESTrackEffect *effect;

  ges_init ();

  effect = ges_track_effect_new_from_bin_desc ("identity");
  fail_unless (effect != NULL);
  g_object_unref (effect);
}

GST_END_TEST;

GST_START_TEST (test_add_effect_to_tl_object)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_audio, *track_video;
  GESTrackEffect *track_effect;
  GESTimelineTestSource *source;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_audio = ges_track_audio_raw_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_timeline_test_source_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESTimelineObject *) source, 0);


  GST_DEBUG ("Create effect");
  track_effect = ges_track_effect_new_from_bin_desc ("identity");

  fail_unless (GES_IS_TRACK_EFFECT (track_effect));


  fail_unless (ges_timeline_object_add_track_object (GES_TIMELINE_OBJECT
          (source), GES_TRACK_OBJECT (track_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect)));

  assert_equals_int (GES_TRACK_OBJECT (track_effect)->active, TRUE);

  ges_timeline_layer_remove_object (layer, (GESTimelineObject *) source);

  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_get_effects_from_tl)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_video;
  GESTrackEffect *track_effect, *track_effect1, *track_effect2;
  GESTimelineTestSource *source;
  GList *effects, *tmp = NULL;
  gint effect_prio = -1;
  guint tl_object_height = 0;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_timeline_test_source_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESTimelineObject *) source, 0);


  GST_DEBUG ("Create effect");
  track_effect = ges_track_effect_new_from_bin_desc ("identity");
  track_effect1 = ges_track_effect_new_from_bin_desc ("identity");
  track_effect2 = ges_track_effect_new_from_bin_desc ("identity");

  fail_unless (GES_IS_TRACK_EFFECT (track_effect));
  fail_unless (GES_IS_TRACK_EFFECT (track_effect1));
  fail_unless (GES_IS_TRACK_EFFECT (track_effect2));

  fail_unless (ges_timeline_object_add_track_object (GES_TIMELINE_OBJECT
          (source), GES_TRACK_OBJECT (track_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect)));

  fail_unless (ges_timeline_object_add_track_object (GES_TIMELINE_OBJECT
          (source), GES_TRACK_OBJECT (track_effect1)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect1)));

  fail_unless (ges_timeline_object_add_track_object (GES_TIMELINE_OBJECT
          (source), GES_TRACK_OBJECT (track_effect2)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect2)));

  g_object_get (G_OBJECT (source), "height", &tl_object_height, NULL);
  fail_unless (tl_object_height == 4);

  effects = ges_timeline_object_get_effects (GES_TIMELINE_OBJECT (source));
  for (tmp = effects; tmp; tmp = tmp->next) {
    gint priority =
        ges_timeline_object_get_top_effect_position (GES_TIMELINE_OBJECT
        (source),
        GES_TRACK_OPERATION (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_TRACK_EFFECT (tmp->data));
    effect_prio = priority;

    g_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_timeline_layer_remove_object (layer, (GESTimelineObject *) source);

  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("effect");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_effect_basic);
  tcase_add_test (tc_chain, test_add_effect_to_tl_object);
  tcase_add_test (tc_chain, test_get_effects_from_tl);

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
