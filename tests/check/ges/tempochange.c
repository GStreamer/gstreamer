/* GStreamer Editing Services
 * Copyright (C) 2016 Sjors Gielen <mixml-ges@sjorsgielen.nl>
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
#include <plugins/nle/nleobject.h>

GST_START_TEST (test_tempochange)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track_audio;
  GESEffect *effect;
  GESTestClip *clip;
  GESClip *clip2, *clip3;
  GList *tmp;
  int found;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  track_audio = GES_TRACK (ges_audio_track_new ());

  ges_timeline_add_track (timeline, track_audio);
  ges_timeline_add_layer (timeline, layer);

  /* Add a 9-second clip */
  clip = ges_test_clip_new ();
  g_object_set (clip, "duration", 9 * GST_SECOND, NULL);
  ges_layer_add_clip (layer, (GESClip *) clip);

  /* Split it after 3 seconds */
  clip2 = ges_clip_split ((GESClip *) clip, 3 * GST_SECOND);

  /* Add pitch effect to play 1.5 times faster */
  effect = ges_effect_new ("pitch tempo=1.5");

  fail_unless (GES_IS_EFFECT (effect));
  fail_unless (ges_container_add (GES_CONTAINER (clip2),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (ges_track_element_get_track (GES_TRACK_ELEMENT (effect)) !=
      NULL);

  assert_equals_int (GES_TRACK_ELEMENT (effect)->active, TRUE);

  /* Split clip again after 6 seconds (note: this is timeline time) */
  clip3 = ges_clip_split (clip2, 6 * GST_SECOND);

  // note: start and duration are counted in timeline time, while
  // inpoint is counted in media time.
  fail_unless_equals_int64 (_START (clip), 0 * GST_SECOND);
  fail_unless_equals_int64 (_INPOINT (clip), 0 * GST_SECOND);
  fail_unless_equals_int64 (_DURATION (clip), 3 * GST_SECOND);

  fail_unless_equals_int64 (_START (clip2), 3 * GST_SECOND);
  fail_unless_equals_int64 (_INPOINT (clip2), 3 * GST_SECOND);
  fail_unless_equals_int64 (_DURATION (clip2), 3 * GST_SECOND);

  fail_unless_equals_int64 (_START (clip3), 6 * GST_SECOND);
  fail_unless_equals_int64 (_INPOINT (clip3), 7.5 * GST_SECOND);
  fail_unless_equals_int64 (_DURATION (clip3), 3 * GST_SECOND);

#define MDF_OF_CLIP(x) \
  ((NleObject *) ges_track_element_get_nleobject \
    (ges_clip_find_track_element (GES_CLIP (x), track_audio, \
    G_TYPE_NONE)))->media_duration_factor

  fail_unless_equals_float (MDF_OF_CLIP (clip), 1.0);
  fail_unless_equals_float (MDF_OF_CLIP (clip2), 1.5);
  fail_unless_equals_float (MDF_OF_CLIP (clip3), 1.5);

  found = 0;

  for (tmp = GES_CONTAINER_CHILDREN (clip2); tmp; tmp = tmp->next) {
    // A clip may have children other than the effect we added. As such, we
    // need to find the child which is the pitch effect in order to check its
    // value.
    GstElement *nle = ges_track_element_get_nleobject (tmp->data);
    if (strncmp (GST_OBJECT_NAME (nle), "GESEffect:", 10) == 0) {
      fail_if (found);
      found = 1;
      fail_unless_equals_float (((NleObject *) nle)->media_duration_factor,
          1.5);
    }
  }

  fail_unless (found);

  found = 0;
  for (tmp = GES_CONTAINER_CHILDREN (clip3); tmp; tmp = tmp->next) {
    GstElement *nle = ges_track_element_get_nleobject (tmp->data);
    if (strncmp (GST_OBJECT_NAME (nle), "GESEffect:", 10) == 0) {
      fail_if (found);
      found = 1;
      fail_unless_equals_float (((NleObject *) nle)->media_duration_factor,
          1.5);
    }
  }

  fail_unless (found);

  ges_layer_remove_clip (layer, (GESClip *) clip);
  ges_layer_remove_clip (layer, clip2);
  ges_layer_remove_clip (layer, clip3);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("tempochange");
  GstPluginFeature *pitch = gst_registry_find_feature (gst_registry_get (),
      "pitch", GST_TYPE_ELEMENT_FACTORY);

  if (pitch) {
    gst_object_unref (pitch);

    suite_add_tcase (s, tc_chain);
    tcase_add_test (tc_chain, test_tempochange);
  }

  return s;
}

GST_CHECK_MAIN (ges);
