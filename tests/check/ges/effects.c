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

void
deep_prop_changed_cb (GESTrackElement * track_element, GstElement * element,
    GParamSpec * spec);

GST_START_TEST (test_effect_basic)
{
  GESEffect *effect;

  ges_init ();

  effect = ges_effect_new ("agingtv");
  fail_unless (effect != NULL);
  gst_object_unref (effect);
}

GST_END_TEST;

GST_START_TEST (test_add_effect_to_clip)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_audio, *track_video;
  GESEffect *effect;
  GESTestClip *source;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESLayer *) ges_simple_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  ges_simple_layer_add_object ((GESSimpleLayer *) (layer),
      (GESClip *) source, 0);


  GST_DEBUG ("Create effect");
  effect = ges_effect_new ("agingtv");

  fail_unless (GES_IS_EFFECT (effect));
  fail_unless (ges_container_add (GES_CONTAINER (source),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) !=
      NULL);

  assert_equals_int (GES_TRACK_ELEMENT (effect)->active, TRUE);

  ges_layer_remove_clip (layer, (GESClip *) source);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_get_effects_from_tl)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_video;
  GESTrackElement *video_source;
  GESEffect *effect, *effect1, *effect2;
  GESTestClip *source;
  GList *effects, *tmp = NULL;
  gint effect_prio = -1;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESLayer *) ges_layer_new ();
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  GST_DEBUG ("Adding source to layer");
  ges_layer_add_clip (layer, (GESClip *) source);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (source)), 1);
  video_source = GES_CONTAINER_CHILDREN (source)->data;
  fail_unless (GES_IS_VIDEO_TEST_SOURCE (video_source));
  assert_equals_int (_PRIORITY (video_source), MIN_GNL_PRIO);

  GST_DEBUG ("Create effect");
  effect = ges_effect_new ("agingtv");
  effect1 = ges_effect_new ("agingtv");
  effect2 = ges_effect_new ("agingtv");

  fail_unless (GES_IS_EFFECT (effect));
  fail_unless (GES_IS_EFFECT (effect1));
  fail_unless (GES_IS_EFFECT (effect2));

  GST_DEBUG ("Adding effect (0)");
  fail_unless (ges_container_add (GES_CONTAINER (source),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);
  assert_equals_int (_PRIORITY (effect), MIN_GNL_PRIO + 0);
  assert_equals_int (_PRIORITY (video_source), MIN_GNL_PRIO + 1);

  GST_DEBUG ("Adding effect 1");
  fail_unless (ges_container_add (GES_CONTAINER (source),
          GES_TIMELINE_ELEMENT (effect1)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect1)) ==
      track_video);
  assert_equals_int (_PRIORITY (effect), MIN_GNL_PRIO);
  assert_equals_int (_PRIORITY (effect1), MIN_GNL_PRIO + 1);
  assert_equals_int (_PRIORITY (video_source), MIN_GNL_PRIO + 2);

  GST_DEBUG ("Adding effect 2");
  fail_unless (ges_container_add (GES_CONTAINER (source),
          GES_TIMELINE_ELEMENT (effect2)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect2)) ==
      track_video);
  assert_equals_int (GES_CONTAINER_HEIGHT (source), 4);

  effects = ges_clip_get_top_effects (GES_CLIP (source));
  fail_unless (g_list_length (effects) == 3);
  for (tmp = effects; tmp; tmp = tmp->next) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (source),
        GES_BASE_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_EFFECT (tmp->data));
    effect_prio = priority;

    gst_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_layer_remove_clip (layer, (GESClip *) source);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_effect_clip)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_audio, *track_video;
  GESEffectClip *effect_clip;
  GESEffect *effect, *effect1;
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
  layer = (GESLayer *) ges_simple_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", "audiopanorama");

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_layer_add_object ((GESSimpleLayer *) (layer),
      (GESClip *) effect_clip, 0);

  effect = ges_effect_new ("agingtv");
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);

  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 3);

  effect1 = ges_effect_new ("audiopanorama");
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect1)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect1)) ==
      track_audio);

  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 4);

  effects = ges_clip_get_top_effects (GES_CLIP (effect_clip));
  for (tmp = effects, i = 0; tmp; tmp = tmp->next, i++) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (effect_clip),
        GES_BASE_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_EFFECT (tmp->data));
    fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (tmp->data))->
        type == track_type[i]);
    effect_prio = priority;

    gst_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_priorities_clip)
{
  gint i;
  GList *effects, *tmp;
  GESTimeline *timeline;
  GESLayer *layer;
  GESEffectClip *effect_clip;
  GESTrack *track_audio, *track_video;
  GESEffect *effect, *effect1, *audio_effect = NULL, *video_effect = NULL;

  gint effect_prio = -1;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESLayer *) ges_simple_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", "audiopanorama");

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_layer_add_object ((GESSimpleLayer *) (layer),
      (GESClip *) effect_clip, 0);

  for (tmp = GES_CONTAINER_CHILDREN (effect_clip); tmp; tmp = tmp->next) {
    if (ges_track_element_get_track_type (GES_TRACK_ELEMENT (tmp->data)) ==
        GES_TRACK_TYPE_AUDIO)
      audio_effect = tmp->data;
    else if (ges_track_element_get_track_type (GES_TRACK_ELEMENT (tmp->data)) ==
        GES_TRACK_TYPE_VIDEO)
      video_effect = tmp->data;
    else
      g_assert (0);
  }
  fail_unless (GES_IS_EFFECT (audio_effect));
  fail_unless (GES_IS_EFFECT (video_effect));

  /* FIXME This is ridiculus, both effects should have the same priority (0) */
  assert_equals_int (_PRIORITY (audio_effect), MIN_GNL_PRIO);
  assert_equals_int (_PRIORITY (video_effect), MIN_GNL_PRIO + 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 2);

  effect = ges_effect_new ("agingtv");
  GST_DEBUG ("Adding effect to the effect clip %" GST_PTR_FORMAT, effect);
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 3);

  effect1 = ges_effect_new ("audiopanorama");
  GST_DEBUG ("Adding effect1 to the effect clip %" GST_PTR_FORMAT, effect1);
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect1)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect1)) ==
      track_audio);

  fail_unless (ges_clip_set_top_effect_priority (GES_CLIP (effect_clip),
          GES_BASE_EFFECT (effect1), 0));
  assert_equals_int (_PRIORITY (effect), 3 + MIN_GNL_PRIO);
  assert_equals_int (_PRIORITY (effect1), 0 + MIN_GNL_PRIO);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 4);

  fail_unless (ges_clip_set_top_effect_priority (GES_CLIP (effect_clip),
          GES_BASE_EFFECT (effect1), 3));
  assert_equals_int (_PRIORITY (effect), 2 + MIN_GNL_PRIO);
  assert_equals_int (_PRIORITY (effect1), 3 + MIN_GNL_PRIO);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 4);

  effects = ges_clip_get_top_effects (GES_CLIP (effect_clip));
  for (tmp = effects, i = 0; tmp; tmp = tmp->next, i++) {
    gint priority = ges_clip_get_top_effect_position (GES_CLIP (effect_clip),
        GES_BASE_EFFECT (tmp->data));
    fail_unless (priority > effect_prio);
    fail_unless (GES_IS_EFFECT (tmp->data));
    effect_prio = priority;

    gst_object_unref (tmp->data);
  }
  g_list_free (effects);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_effect_set_properties)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_video;
  GESEffectClip *effect_clip;
  GESTrackElement *effect;
  guint scratch_line, n_props, i;
  gboolean color_aging;
  GParamSpec **pspecs, *spec;
  GValue val = { 0 };
  GValue nval = { 0 };

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESLayer *) ges_simple_layer_new ();
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", NULL);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_layer_add_object ((GESSimpleLayer *) (layer),
      (GESClip *) effect_clip, 0);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);

  ges_track_element_set_child_properties (effect,
      "GstAgingTV::scratch-lines", 17, "color-aging", FALSE, NULL);
  ges_track_element_get_child_properties (effect,
      "GstAgingTV::scratch-lines", &scratch_line,
      "color-aging", &color_aging, NULL);
  fail_unless (scratch_line == 17);
  fail_unless (color_aging == FALSE);

  pspecs = ges_track_element_list_children_properties (effect, &n_props);
  fail_unless (n_props == 7);

  spec = pspecs[0];
  i = 1;
  while (g_strcmp0 (spec->name, "scratch-lines")) {
    spec = pspecs[i++];
  }

  g_value_init (&val, G_TYPE_UINT);
  g_value_init (&nval, G_TYPE_UINT);
  g_value_set_uint (&val, 10);

  ges_track_element_set_child_property_by_pspec (effect, spec, &val);
  ges_track_element_get_child_property_by_pspec (effect, spec, &nval);
  fail_unless (g_value_get_uint (&nval) == 10);

  for (i = 0; i < n_props; i++) {
    g_param_spec_unref (pspecs[i]);
  }
  g_free (pspecs);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);
}

GST_END_TEST;

static void
effect_added_cb (GESClip * clip, GESBaseEffect * trop, gboolean * effect_added)
{
  GST_DEBUG ("Effect added");
  fail_unless (GES_IS_CLIP (clip));
  fail_unless (GES_IS_EFFECT (trop));
  *effect_added = TRUE;
}

void
deep_prop_changed_cb (GESTrackElement * track_element, GstElement * element,
    GParamSpec * spec)
{
  GST_DEBUG ("%s property changed", g_param_spec_get_name (spec));
  fail_unless (GES_IS_TRACK_ELEMENT (track_element));
  fail_unless (GST_IS_ELEMENT (element));
}

GST_START_TEST (test_clip_signals)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_video;
  GESEffectClip *effect_clip;
  GESEffect *effect;
  GValue val = { 0, };
  gboolean effect_added = FALSE;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESLayer *) ges_simple_layer_new ();
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", NULL);
  g_signal_connect (effect_clip, "child-added", (GCallback) effect_added_cb,
      &effect_added);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_simple_layer_add_object ((GESSimpleLayer *) (layer),
      (GESClip *) effect_clip, 0);

  effect = ges_effect_new ("agingtv");
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (effect_added);
  g_signal_handlers_disconnect_by_func (effect_clip, effect_added_cb,
      &effect_added);
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);
  g_signal_connect (effect, "deep-notify", (GCallback) deep_prop_changed_cb,
      effect);

  ges_track_element_set_child_properties (GES_TRACK_ELEMENT (effect),
      "GstAgingTV::scratch-lines", 17, NULL);

  g_value_init (&val, G_TYPE_UINT);
  ges_track_element_get_child_property (GES_TRACK_ELEMENT (effect),
      "GstAgingTV::scratch-lines", &val);
  fail_unless (G_VALUE_HOLDS_UINT (&val));
  g_value_unset (&val);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);
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
  tcase_add_test (tc_chain, test_effect_set_properties);
  tcase_add_test (tc_chain, test_clip_signals);

  return s;
}

GST_CHECK_MAIN (ges);
