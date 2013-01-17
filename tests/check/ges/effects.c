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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

void effect_added_cb (GESClip * obj, GESTrackEffect * trop, gpointer data);
void
deep_prop_changed_cb (GESTrackObject * obj, GstElement * element,
    GParamSpec * spec);

GST_START_TEST (test_effect_basic)
{
  GESTrackParseLaunchEffect *effect;

  ges_init ();

  effect = ges_track_parse_launch_effect_new ("agingtv");
  fail_unless (effect != NULL);
  g_object_unref (effect);
}

GST_END_TEST;

GST_START_TEST (test_add_effect_to_clip)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_audio, *track_video;
  GESTrackParseLaunchEffect *track_effect;
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
      (GESClip *) source, 0);


  GST_DEBUG ("Create effect");
  track_effect = ges_track_parse_launch_effect_new ("agingtv");

  fail_unless (GES_IS_TRACK_EFFECT (track_effect));


  fail_unless (ges_clip_add_track_object (GES_CLIP
          (source), GES_TRACK_OBJECT (track_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect)));

  assert_equals_int (GES_TRACK_OBJECT (track_effect)->active, TRUE);

  ges_timeline_layer_remove_object (layer, (GESClip *) source);

  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_get_effects_from_tl)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_video;
  GESTrackParseLaunchEffect *track_effect, *track_effect1, *track_effect2;
  GESTimelineTestSource *source;
  GList *effects, *tmp = NULL;
  gint effect_prio = -1;
  guint clip_height = 0;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_timeline_test_source_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESClip *) source, 0);


  GST_DEBUG ("Create effect");
  track_effect = ges_track_parse_launch_effect_new ("agingtv");
  track_effect1 = ges_track_parse_launch_effect_new ("agingtv");
  track_effect2 = ges_track_parse_launch_effect_new ("agingtv");

  fail_unless (GES_IS_TRACK_EFFECT (track_effect));
  fail_unless (GES_IS_TRACK_EFFECT (track_effect1));
  fail_unless (GES_IS_TRACK_EFFECT (track_effect2));

  fail_unless (ges_clip_add_track_object (GES_CLIP
          (source), GES_TRACK_OBJECT (track_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect)));

  fail_unless (ges_clip_add_track_object (GES_CLIP
          (source), GES_TRACK_OBJECT (track_effect1)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect1)));

  fail_unless (ges_clip_add_track_object (GES_CLIP
          (source), GES_TRACK_OBJECT (track_effect2)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (track_effect2)));

  g_object_get (G_OBJECT (source), "height", &clip_height, NULL);
  fail_unless (clip_height == 4);

  effects = ges_clip_get_top_effects (GES_CLIP (source));
  fail_unless (g_list_length (effects) == 3);
  for (tmp = effects; tmp; tmp = tmp->next) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (source),
        GES_TRACK_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_TRACK_EFFECT (tmp->data));
    effect_prio = priority;

    g_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_timeline_layer_remove_object (layer, (GESClip *) source);

  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_effect_clip)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_audio, *track_video;
  GESTimelineParseLaunchEffect *effect_clip;
  GESTrackParseLaunchEffect *tck_effect, *tck_effect1;
  GList *effects, *tmp;
  gint i, clip_height;
  gint effect_prio = -1;
  /* FIXME the order of track type is not well defined */
  guint track_type[4] = { GES_TRACK_TYPE_AUDIO,
    GES_TRACK_TYPE_VIDEO, GES_TRACK_TYPE_VIDEO,
    GES_TRACK_TYPE_AUDIO
  };

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_audio = ges_track_audio_raw_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_timeline_parse_launch_effect_new ("identity", "identity");

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESClip *) effect_clip, 0);

  tck_effect = ges_track_parse_launch_effect_new ("identity");
  fail_unless (ges_clip_add_track_object (GES_CLIP
          (effect_clip), GES_TRACK_OBJECT (tck_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (tck_effect)));

  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 3);

  tck_effect1 = ges_track_parse_launch_effect_new ("identity");
  fail_unless (ges_clip_add_track_object (GES_CLIP
          (effect_clip), GES_TRACK_OBJECT (tck_effect1)));
  fail_unless (ges_track_add_object (track_audio,
          GES_TRACK_OBJECT (tck_effect1)));

  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 4);

  effects = ges_clip_get_top_effects (GES_CLIP (effect_clip));
  for (tmp = effects, i = 0; tmp; tmp = tmp->next, i++) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (effect_clip),
        GES_TRACK_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_TRACK_EFFECT (tmp->data));
    fail_unless (ges_track_object_get_track (GES_TRACK_OBJECT (tmp->
                data))->type == track_type[i]);
    effect_prio = priority;

    g_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_timeline_layer_remove_object (layer, (GESClip *) effect_clip);

  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_priorities_clip)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_audio, *track_video;
  GESTimelineParseLaunchEffect *effect_clip;
  GESTrackParseLaunchEffect *tck_effect, *tck_effect1;
  GList *effects, *tmp;
  gint i, clip_height;
  gint effect_prio = -1;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_audio = ges_track_audio_raw_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_timeline_parse_launch_effect_new ("identity", "identity");

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESClip *) effect_clip, 0);

  tck_effect = ges_track_parse_launch_effect_new ("identity");
  fail_unless (ges_clip_add_track_object (GES_CLIP
          (effect_clip), GES_TRACK_OBJECT (tck_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (tck_effect)));

  g_object_get (effect_clip, "height", &clip_height, NULL);
  fail_unless (clip_height == 3);

  tck_effect1 = ges_track_parse_launch_effect_new ("identity");
  fail_unless (ges_clip_add_track_object (GES_CLIP
          (effect_clip), GES_TRACK_OBJECT (tck_effect1)));
  fail_unless (ges_track_add_object (track_audio,
          GES_TRACK_OBJECT (tck_effect1)));

  fail_unless (ges_clip_set_top_effect_priority (GES_CLIP
          (effect_clip), GES_TRACK_EFFECT (tck_effect1), 0));

  fail_unless (_PRIORITY (tck_effect), 3);

  fail_unless (ges_clip_set_top_effect_priority (GES_CLIP
          (effect_clip), GES_TRACK_EFFECT (tck_effect1), 3));
  fail_unless (_PRIORITY (tck_effect) == 2);

  g_object_get (effect_clip, "height", &clip_height, NULL);
  fail_unless (clip_height == 4);

  effects = ges_clip_get_top_effects (GES_CLIP (effect_clip));
  for (tmp = effects, i = 0; tmp; tmp = tmp->next, i++) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (effect_clip),
        GES_TRACK_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_TRACK_EFFECT (tmp->data));
    effect_prio = priority;

    g_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_timeline_layer_remove_object (layer, (GESClip *) effect_clip);

  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_track_effect_set_properties)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_video;
  GESTimelineParseLaunchEffect *effect_clip;
  GESTrackObject *tck_effect;
  guint scratch_line, n_props, i;
  gboolean color_aging;
  GParamSpec **pspecs, *spec;
  GValue val = { 0 };
  GValue nval = { 0 };

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_timeline_parse_launch_effect_new ("agingtv", NULL);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESClip *) effect_clip, 0);

  tck_effect = GES_TRACK_OBJECT (ges_track_parse_launch_effect_new ("agingtv"));
  fail_unless (ges_clip_add_track_object (GES_CLIP (effect_clip), tck_effect));
  fail_unless (ges_track_add_object (track_video, tck_effect));

  ges_track_object_set_child_properties (tck_effect,
      "GstAgingTV::scratch-lines", 17, "color-aging", FALSE, NULL);
  ges_track_object_get_child_properties (tck_effect,
      "GstAgingTV::scratch-lines", &scratch_line,
      "color-aging", &color_aging, NULL);
  fail_unless (scratch_line == 17);
  fail_unless (color_aging == FALSE);

  pspecs = ges_track_object_list_children_properties (tck_effect, &n_props);
  fail_unless (n_props == 7);

  spec = pspecs[0];
  i = 1;
  while (g_strcmp0 (spec->name, "scratch-lines")) {
    spec = pspecs[i++];
  }

  g_value_init (&val, G_TYPE_UINT);
  g_value_init (&nval, G_TYPE_UINT);
  g_value_set_uint (&val, 10);

  ges_track_object_set_child_property_by_pspec (tck_effect, spec, &val);
  ges_track_object_get_child_property_by_pspec (tck_effect, spec, &nval);
  fail_unless (g_value_get_uint (&nval) == 10);

  for (i = 0; i < n_props; i++) {
    g_param_spec_unref (pspecs[i]);
  }
  g_free (pspecs);

  ges_timeline_layer_remove_object (layer, (GESClip *) effect_clip);

  g_object_unref (timeline);
}

GST_END_TEST;

void
effect_added_cb (GESClip * obj, GESTrackEffect * trop, gpointer data)
{
  GST_DEBUG ("Effect added");
  fail_unless (GES_IS_CLIP (obj));
  fail_unless (GES_IS_TRACK_EFFECT (trop));
}

void
deep_prop_changed_cb (GESTrackObject * obj, GstElement * element,
    GParamSpec * spec)
{
  GST_DEBUG ("%s property changed", g_param_spec_get_name (spec));
  fail_unless (GES_IS_TRACK_OBJECT (obj));
  fail_unless (GST_IS_ELEMENT (element));
}

GST_START_TEST (test_clip_signals)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track_video;
  GESTimelineParseLaunchEffect *effect_clip;
  GESTrackParseLaunchEffect *tck_effect;
  GValue val = { 0, };

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  track_video = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_timeline_parse_launch_effect_new ("agingtv", NULL);
  g_signal_connect (effect_clip, "effect-added", (GCallback) effect_added_cb,
      effect_clip);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) (layer),
      (GESClip *) effect_clip, 0);

  tck_effect = ges_track_parse_launch_effect_new ("agingtv");
  fail_unless (ges_clip_add_track_object (GES_CLIP
          (effect_clip), GES_TRACK_OBJECT (tck_effect)));
  fail_unless (ges_track_add_object (track_video,
          GES_TRACK_OBJECT (tck_effect)));
  g_signal_connect (tck_effect, "deep-notify", (GCallback) deep_prop_changed_cb,
      tck_effect);

  ges_track_object_set_child_properties (GES_TRACK_OBJECT (tck_effect),
      "GstAgingTV::scratch-lines", 17, NULL);

  g_value_init (&val, G_TYPE_UINT);
  ges_track_object_get_child_property (GES_TRACK_OBJECT (tck_effect),
      "GstAgingTV::scratch-lines", &val);
  fail_unless (G_VALUE_HOLDS_UINT (&val));
  g_value_unset (&val);

  ges_timeline_layer_remove_object (layer, (GESClip *) effect_clip);

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
  tcase_add_test (tc_chain, test_add_effect_to_clip);
  tcase_add_test (tc_chain, test_get_effects_from_tl);
  tcase_add_test (tc_chain, test_effect_clip);
  tcase_add_test (tc_chain, test_priorities_clip);
  tcase_add_test (tc_chain, test_track_effect_set_properties);
  tcase_add_test (tc_chain, test_clip_signals);

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
