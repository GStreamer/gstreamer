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

  ges_deinit ();
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
  layer = ges_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new ();

  g_object_set (source, "duration", 10 * GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) source);


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

  ges_deinit ();
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
  layer = ges_layer_new ();
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
  assert_equals_int (_PRIORITY (video_source),
      MIN_NLE_PRIO + TRANSITIONS_HEIGHT);

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
  assert_equals_int (_PRIORITY (effect), MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0);
  assert_equals_int (_PRIORITY (video_source),
      MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1);

  GST_DEBUG ("Adding effect 1");
  fail_unless (ges_container_add (GES_CONTAINER (source),
          GES_TIMELINE_ELEMENT (effect1)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect1)) ==
      track_video);
  assert_equals_int (_PRIORITY (effect), MIN_NLE_PRIO + TRANSITIONS_HEIGHT);
  assert_equals_int (_PRIORITY (effect1),
      MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1);
  assert_equals_int (_PRIORITY (video_source),
      MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2);

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

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_effect_clip)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_audio, *track_video;
  GESEffectClip *effect_clip;
  GESEffect *effect, *effect1, *core_effect, *core_effect1;
  GList *children, *top_effects, *tmp;
  gint clip_height;
  gint core_effect_prio;
  gint effect_index, effect1_index;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  /* these are the core video and audio effects for the clip */
  effect_clip = ges_effect_clip_new ("videobalance", "audioecho");

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) effect_clip);

  /* core elements should now be created */
  fail_unless (children = GES_CONTAINER_CHILDREN (effect_clip));
  core_effect = GES_EFFECT (children->data);
  fail_unless (children = children->next);
  core_effect1 = GES_EFFECT (children->data);
  fail_unless (children->next == NULL);

  /* both effects are placed at the same priority since they are core
   * children of the clip, destined for different tracks */
  core_effect_prio = _PRIORITY (core_effect);
  assert_equals_int (core_effect_prio, _PRIORITY (core_effect1));
  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 1);

  /* add additional non-core effects */
  effect = ges_effect_new ("agingtv");
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);

  /* placed at a higher priority than the effects */
  core_effect_prio = _PRIORITY (core_effect);
  assert_equals_int (core_effect_prio, _PRIORITY (core_effect1));
  fail_unless (_PRIORITY (effect) < core_effect_prio);
  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 2);

  effect_index =
      ges_clip_get_top_effect_index (GES_CLIP (effect_clip),
      GES_BASE_EFFECT (effect));
  assert_equals_int (effect_index, 0);

  /* 'effect1' is placed in between the core children and 'effect' */
  effect1 = ges_effect_new ("audiopanorama");
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect1)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect1)) ==
      track_audio);

  /* 'effect' is still the highest priority effect, and the core
   * elements are at the lowest priority */
  core_effect_prio = _PRIORITY (core_effect);
  assert_equals_int (core_effect_prio, _PRIORITY (core_effect1));
  fail_unless (_PRIORITY (effect1) < core_effect_prio);
  fail_unless (_PRIORITY (effect1) > _PRIORITY (effect));
  g_object_get (effect_clip, "height", &clip_height, NULL);
  assert_equals_int (clip_height, 3);

  effect_index =
      ges_clip_get_top_effect_index (GES_CLIP (effect_clip),
      GES_BASE_EFFECT (effect));
  effect1_index =
      ges_clip_get_top_effect_index (GES_CLIP (effect_clip),
      GES_BASE_EFFECT (effect1));
  assert_equals_int (effect_index, 0);
  assert_equals_int (effect1_index, 1);

  /* all effects are children of the effect_clip, ordered by priority */
  fail_unless (children = GES_CONTAINER_CHILDREN (effect_clip));
  fail_unless (children->data == effect);
  fail_unless (children = children->next);
  fail_unless (children->data == effect1);
  fail_unless (children = children->next);
  fail_unless (children->data == core_effect);
  fail_unless (children = children->next);
  fail_unless (children->data == core_effect1);
  fail_unless (children->next == NULL);

  /* but only the additional effects are part of the top effects */
  top_effects = ges_clip_get_top_effects (GES_CLIP (effect_clip));
  fail_unless (tmp = top_effects);
  fail_unless (tmp->data == effect);
  fail_unless (tmp = tmp->next);
  fail_unless (tmp->data == effect1);
  fail_unless (tmp->next == NULL);

  g_list_free_full (top_effects, gst_object_unref);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_priorities_clip)
{
  GList *top_effects, *tmp;
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *effect_clip;
  GESTrack *track_audio, *track_video, *track;
  GESBaseEffect *effects[6], *audio_effect = NULL, *video_effect = NULL;
  gint prev_index, i, num_effects = G_N_ELEMENTS (effects);
  guint32 base_prio = MIN_NLE_PRIO + TRANSITIONS_HEIGHT;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = GES_CLIP (ges_effect_clip_new ("videobalance", "audioecho"));

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_layer_add_clip ((layer), (GESClip *) effect_clip);
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
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (audio_effect)) ==
      track_audio);
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (video_effect)) ==
      track_video);

  /* both the core effects have the same priority */
  assert_equals_int (_PRIORITY (audio_effect), base_prio);
  assert_equals_int (_PRIORITY (video_effect), base_prio);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 1);

  /* can not change their priority using the top effect methods since
   * they are not top effects */
  fail_unless (ges_clip_set_top_effect_index (effect_clip, audio_effect, 1)
      == FALSE);
  fail_unless (ges_clip_set_top_effect_index (effect_clip, video_effect, 0)
      == FALSE);

  /* adding non-core effects */
  GST_DEBUG ("Adding effects to the effect clip ");
  for (i = 0; i < num_effects; i++) {
    if (i % 2)
      effects[i] = GES_BASE_EFFECT (ges_effect_new ("agingtv"));
    else
      effects[i] = GES_BASE_EFFECT (ges_effect_new ("audiopanorama"));
    fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
            GES_TIMELINE_ELEMENT (effects[i])));
    assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), 2 + i);
    track = ges_track_element_get_track (GES_TRACK_ELEMENT (effects[i]));
    if (i % 2)
      fail_unless (track == track_video);
    else
      fail_unless (track == track_audio);
  }

  /* change top effect index */
  for (i = 0; i < num_effects; i++) {
    assert_equals_int (ges_clip_get_top_effect_index (effect_clip, effects[i]),
        i);
    assert_equals_int (_PRIORITY (effects[i]), i + base_prio);
  }

  assert_equals_int (_PRIORITY (video_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (audio_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (effect_clip), 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), num_effects + 1);

  /* moving 4th effect to index 1 should only change the priority of
   * effects 1, 2, 3, and 4 because these lie between the new index (1)
   * and the old index (4). */
  fail_unless (ges_clip_set_top_effect_index (effect_clip, effects[4], 1));

  assert_equals_int (_PRIORITY (effects[0]), 0 + base_prio);
  assert_equals_int (_PRIORITY (effects[1]), 2 + base_prio);
  assert_equals_int (_PRIORITY (effects[2]), 3 + base_prio);
  assert_equals_int (_PRIORITY (effects[3]), 4 + base_prio);
  assert_equals_int (_PRIORITY (effects[4]), 1 + base_prio);
  assert_equals_int (_PRIORITY (effects[5]), 5 + base_prio);

  /* everything else stays the same */
  assert_equals_int (_PRIORITY (video_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (audio_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (effect_clip), 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), num_effects + 1);

  /* move back */
  fail_unless (ges_clip_set_top_effect_index (effect_clip, effects[4], 4));

  for (i = 0; i < num_effects; i++) {
    assert_equals_int (ges_clip_get_top_effect_index (effect_clip, effects[i]),
        i);
    assert_equals_int (_PRIORITY (effects[i]), i + base_prio);
  }

  assert_equals_int (_PRIORITY (video_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (audio_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (effect_clip), 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), num_effects + 1);

  /* moving 2nd effect to index 4 should only change the priority of
   * effects 2, 3 and 4 because these lie between the new index (4) and
   * the old index (2). */

  fail_unless (ges_clip_set_top_effect_index (effect_clip, effects[2], 4));

  assert_equals_int (_PRIORITY (effects[0]), 0 + base_prio);
  assert_equals_int (_PRIORITY (effects[1]), 1 + base_prio);
  assert_equals_int (_PRIORITY (effects[2]), 4 + base_prio);
  assert_equals_int (_PRIORITY (effects[3]), 2 + base_prio);
  assert_equals_int (_PRIORITY (effects[4]), 3 + base_prio);
  assert_equals_int (_PRIORITY (effects[5]), 5 + base_prio);

  /* everything else stays the same */
  assert_equals_int (_PRIORITY (video_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (audio_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (effect_clip), 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), num_effects + 1);

  /* move 4th effect to index 0 should only change the priority of
   * effects 0, 1, 3 and 4 because these lie between the new index (0) and
   * the old index (3) */

  fail_unless (ges_clip_set_top_effect_index (effect_clip, effects[4], 0));

  assert_equals_int (_PRIORITY (effects[0]), 1 + base_prio);
  assert_equals_int (_PRIORITY (effects[1]), 2 + base_prio);
  assert_equals_int (_PRIORITY (effects[2]), 4 + base_prio);
  assert_equals_int (_PRIORITY (effects[3]), 3 + base_prio);
  assert_equals_int (_PRIORITY (effects[4]), 0 + base_prio);
  assert_equals_int (_PRIORITY (effects[5]), 5 + base_prio);

  /* everything else stays the same */
  assert_equals_int (_PRIORITY (video_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (audio_effect), num_effects + base_prio);
  assert_equals_int (_PRIORITY (effect_clip), 1);
  assert_equals_int (GES_CONTAINER_HEIGHT (effect_clip), num_effects + 1);

  /* make sure top effects are ordered by index */
  top_effects = ges_clip_get_top_effects (effect_clip);
  prev_index = -1;
  for (tmp = top_effects; tmp; tmp = tmp->next) {
    gint index = ges_clip_get_top_effect_index (effect_clip,
        GES_BASE_EFFECT (tmp->data));
    fail_unless (index >= 0);
    fail_unless (index > prev_index);
    fail_unless (GES_IS_EFFECT (tmp->data));
    prev_index = index;
  }
  g_list_free_full (top_effects, gst_object_unref);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_effect_set_properties)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_video;
  GESEffectClip *effect_clip;
  GESTimelineElement *effect;
  guint scratch_line, n_props, i;
  gboolean color_aging;
  GParamSpec **pspecs, *spec;
  GValue val = { 0 };
  GValue nval = { 0 };

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", NULL);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) effect_clip);

  effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);

  ges_timeline_element_set_child_properties (effect,
      "GstAgingTV::scratch-lines", 17, "color-aging", FALSE, NULL);
  ges_timeline_element_get_child_properties (effect,
      "GstAgingTV::scratch-lines", &scratch_line,
      "color-aging", &color_aging, NULL);
  assert_equals_int (scratch_line, 17);
  assert_equals_int (color_aging, FALSE);

  pspecs = ges_timeline_element_list_children_properties (effect, &n_props);
  assert_equals_int (n_props, 7);

  spec = pspecs[0];
  i = 1;
  while (g_strcmp0 (spec->name, "scratch-lines")) {
    spec = pspecs[i++];
  }

  g_value_init (&val, G_TYPE_UINT);
  g_value_init (&nval, G_TYPE_UINT);
  g_value_set_uint (&val, 10);

  ges_timeline_element_set_child_property_by_pspec (effect, spec, &val);
  ges_timeline_element_get_child_property_by_pspec (effect, spec, &nval);
  assert_equals_int (g_value_get_uint (&nval), 10);

  for (i = 0; i < n_props; i++) {
    g_param_spec_unref (pspecs[i]);
  }
  g_free (pspecs);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);

  ges_deinit ();
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
  GESTimelineElement *effect;
  GValue val = { 0, };
  gboolean effect_added = FALSE;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  track_video = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect_clip = ges_effect_clip_new ("agingtv", NULL);
  g_signal_connect (effect_clip, "child-added", (GCallback) effect_added_cb,
      &effect_added);

  g_object_set (effect_clip, "duration", 25 * GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) effect_clip);

  effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (effect_clip), effect));
  fail_unless (effect_added);
  g_signal_handlers_disconnect_by_func (effect_clip, effect_added_cb,
      &effect_added);
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) ==
      track_video);
  g_signal_connect (effect, "deep-notify", (GCallback) deep_prop_changed_cb,
      effect);

  ges_timeline_element_set_child_properties (effect,
      "GstAgingTV::scratch-lines", 17, NULL);

  g_value_init (&val, G_TYPE_UINT);
  ges_timeline_element_get_child_property (effect,
      "GstAgingTV::scratch-lines", &val);
  fail_unless (G_VALUE_HOLDS_UINT (&val));
  g_value_unset (&val);

  ges_layer_remove_clip (layer, (GESClip *) effect_clip);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_split_clip_effect_priorities)
{
  GESLayer *layer;
  GESTimeline *timeline;
  GESTrack *track_video;
  GESClip *clip, *nclip;
  GESEffect *effect;
  GESTrackElement *source, *nsource, *neffect;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_timeline_append_layer (timeline);
  track_video = GES_TRACK (ges_video_track_new ());

  g_object_set (timeline, "auto-transition", TRUE, NULL);
  ges_timeline_add_track (timeline, track_video);
  ges_timeline_add_layer (timeline, layer);

  GST_DEBUG ("Create effect");
  effect = ges_effect_new ("agingtv");
  clip = GES_CLIP (ges_test_clip_new ());
  g_object_set (clip, "duration", GST_SECOND * 2, NULL);

  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  ges_layer_add_clip (layer, clip);

  source = ges_clip_find_track_element (clip, NULL, GES_TYPE_VIDEO_SOURCE);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (effect), 3);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (source), 4);

  nclip = ges_clip_split (clip, GST_SECOND);
  fail_unless (nclip);
  neffect = ges_clip_find_track_element (nclip, NULL, GES_TYPE_EFFECT);
  nsource = ges_clip_find_track_element (nclip, NULL, GES_TYPE_VIDEO_SOURCE);

  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (effect), 3);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (source), 4);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (neffect), 5);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (nsource), 6);

  /* Create a transition ... */
  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), GST_SECOND / 2);

  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (effect), 3);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (source), 4);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (neffect), 5);
  assert_equals_uint64 (GES_TIMELINE_ELEMENT_PRIORITY (nsource), 6);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _NO_ERROR -1
#define _set_rate(videorate, rate, error_code) \
{ \
  g_value_set_double (&val, rate); \
  if (error_code != _NO_ERROR) { \
    fail_if (ges_timeline_element_set_child_property_full ( \
          GES_TIMELINE_ELEMENT (videorate), "rate", &val, &error)); \
    assert_GESError (error, error_code); \
  } else { \
    fail_unless (ges_timeline_element_set_child_property_full ( \
          GES_TIMELINE_ELEMENT (videorate), "rate", &val, &error)); \
    fail_if (error); \
  } \
}

#define _add_effect(clip, effect, _index, error_code) \
{ \
  gint index = _index; \
  if (error_code != _NO_ERROR) { \
    fail_if (ges_clip_add_top_effect (clip, effect, index, &error)); \
    assert_GESError (error, error_code); \
  } else { \
    GList *effects; \
    gboolean res = ges_clip_add_top_effect (clip, effect, index, &error); \
    fail_unless (res, "Adding effect " #effect " failed: %s", \
        error ? error->message : "No error produced"); \
    fail_if (error); \
    effects = ges_clip_get_top_effects (clip); \
    fail_unless (g_list_find (effects, effect)); \
    if (index < 0 || index >= g_list_length (effects)) \
      index = g_list_length (effects) - 1; \
    assert_equals_int (ges_clip_get_top_effect_index (clip, effect), index); \
    fail_unless (g_list_nth_data (effects, index) == effect); \
    g_list_free_full (effects, gst_object_unref); \
  } \
}

#define _remove_effect(clip, effect, error_code) \
{ \
  if (error_code != _NO_ERROR) { \
    fail_if (ges_clip_remove_top_effect (clip, effect, &error)); \
    assert_GESError (error, error_code); \
  } else { \
    GList *effects; \
    gboolean res = ges_clip_remove_top_effect (clip, effect, &error); \
    fail_unless (res, "Removing effect " #effect " failed: %s", \
        error ? error->message : "No error produced"); \
    fail_if (error); \
    effects = ges_clip_get_top_effects (clip); \
    fail_if (g_list_find (effects, effect)); \
    g_list_free_full (effects, gst_object_unref); \
  } \
}

#define _move_effect(clip, effect, index, error_code) \
{ \
  if (error_code != _NO_ERROR) { \
    fail_if ( \
        ges_clip_set_top_effect_index_full (clip, effect, index, &error)); \
    assert_GESError (error, error_code); \
  } else { \
    GList *effects; \
    gboolean res = \
        ges_clip_set_top_effect_index_full (clip, effect, index, &error); \
    fail_unless (res, "Moving effect " #effect " failed: %s", \
        error ? error->message : "No error produced"); \
    fail_if (error); \
    effects = ges_clip_get_top_effects (clip); \
    fail_unless (g_list_find (effects, effect)); \
    assert_equals_int (ges_clip_get_top_effect_index (clip, effect), index); \
    fail_unless (g_list_nth_data (effects, index) == effect); \
    g_list_free_full (effects, gst_object_unref); \
  } \
}

GST_START_TEST (test_move_time_effect)
{
  GESTimeline *timeline;
  GESTrack *track;
  GESLayer *layer;
  GESAsset *asset;
  GESClip *clip;
  GESBaseEffect *rate0, *rate1, *overlay;
  GError *error = NULL;
  GValue val = G_VALUE_INIT;

  ges_init ();

  g_value_init (&val, G_TYPE_DOUBLE);


  timeline = ges_timeline_new ();
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  layer = ges_timeline_append_layer (timeline);

  /* add a dummy clip for overlap */
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, "max-duration=16", &error);
  fail_unless (asset);
  fail_if (error);

  fail_unless (ges_layer_add_asset_full (layer, asset, 0, 0, 16,
          GES_TRACK_TYPE_UNKNOWN, &error));
  fail_if (error);

  clip = GES_CLIP (ges_asset_extract (asset, &error));
  fail_unless (clip);
  fail_if (error);
  assert_set_start (clip, 8);
  assert_set_duration (clip, 16);

  rate0 = GES_BASE_EFFECT (ges_effect_new ("videorate"));
  rate1 = GES_BASE_EFFECT (ges_effect_new ("videorate"));
  overlay = GES_BASE_EFFECT (ges_effect_new ("textoverlay"));

  ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (overlay), TRUE);
  /* only has 8ns of content */
  assert_set_inpoint (overlay, 13);
  assert_set_max_duration (overlay, 21);

  _set_rate (rate0, 2.0, _NO_ERROR);
  _set_rate (rate1, 0.5, _NO_ERROR);

  /* keep alive */
  gst_object_ref (clip);
  gst_object_ref (rate0);
  gst_object_ref (rate1);
  gst_object_ref (overlay);

  /* cannot add to layer with rate effect because it would cause a full
   * overlap */
  _add_effect (clip, rate0, 0, _NO_ERROR);
  fail_if (ges_layer_add_clip_full (layer, clip, &error));
  assert_GESError (error, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _remove_effect (clip, rate0, _NO_ERROR);

  /* same with overlay */
  _add_effect (clip, overlay, 0, _NO_ERROR);
  fail_if (ges_layer_add_clip_full (layer, clip, &error));
  assert_GESError (error, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _remove_effect (clip, overlay, _NO_ERROR);

  CHECK_OBJECT_PROPS (clip, 8, 0, 16);

  fail_unless (ges_layer_add_clip_full (layer, clip, &error));
  fail_if (error);

  CHECK_OBJECT_PROPS_MAX (clip, 8, 0, 16, 16);

  /* can't add rate0 or overlay in the same way */
  _add_effect (clip, rate0, 0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _add_effect (clip, overlay, 0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* rate1 extends the duration-limit instead */
  _add_effect (clip, rate1, 0, _NO_ERROR);

  /* can't add overlay next to the timeline */
  _add_effect (clip, overlay, 0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  /* but next to source is ok */
  _add_effect (clip, overlay, 1, _NO_ERROR);

  /* can't add rate0 after overlay */
  _add_effect (clip, rate0, 1, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  /* but before is ok */
  _add_effect (clip, rate0, -1, _NO_ERROR);

  /* can't move rate0 to end */
  _move_effect (clip, rate0, 0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  /* can't move overlay to start or end */
  _move_effect (clip, overlay, 0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _move_effect (clip, overlay, 2, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* can now move: swap places with rate1 */
  _set_rate (rate0, 0.5, _NO_ERROR);
  _move_effect (clip, rate0, 0, _NO_ERROR);
  _move_effect (clip, rate1, 2, _NO_ERROR);
  _set_rate (rate1, 2.0, _NO_ERROR);

  /* cannot speed up either rate too much */
  _set_rate (rate0, 1.0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _set_rate (rate1, 4.0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* cannot remove rate0 which is slowing down */
  _remove_effect (clip, rate0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* removing the speed-up is fine */
  _remove_effect (clip, rate1, _NO_ERROR);

  /* removing the overlay is fine */
  _remove_effect (clip, overlay, _NO_ERROR);

  CHECK_OBJECT_PROPS_MAX (clip, 8, 0, 16, 16);
  assert_set_max_duration (clip, 8);
  CHECK_OBJECT_PROPS_MAX (clip, 8, 0, 16, 8);
  /* still can't remove the slow down since it is the only thing stopping
   * a full overlap */
  _remove_effect (clip, rate0, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  gst_object_unref (clip);
  /* shouldn't have any problems when removing from the layer */
  fail_unless (ges_layer_remove_clip (layer, clip));

  g_value_reset (&val);
  gst_object_unref (rate0);
  gst_object_unref (rate1);
  gst_object_unref (overlay);
  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
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
  tcase_add_test (tc_chain, test_split_clip_effect_priorities);
  tcase_add_test (tc_chain, test_move_time_effect);

  return s;
}

GST_CHECK_MAIN (ges);
