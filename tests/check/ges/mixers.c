/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * gst-editing-services
 *
 * Copyright (C) 2013 Thibault Saunier <tsaunier@gnome.org>
 *
 * gst-editing-services is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gst-editing-services is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 *
 */
#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

#include <ges/ges-smart-adder.h>

static GMainLoop *main_loop;

GST_START_TEST (simple_smart_adder_test)
{
  GstPad *requested_pad;
  GstPadTemplate *template = NULL;
  GESTrack *track = GES_TRACK (ges_audio_track_new ());
  GstElement *smart_adder = ges_smart_adder_new (track);

  fail_unless (GES_IS_SMART_ADDER (smart_adder));
  fail_unless (GST_IS_ELEMENT (smart_adder));
  fail_unless (GST_IS_ELEMENT (GES_SMART_ADDER (smart_adder)->adder));
  fail_unless (GST_IS_PAD (GES_SMART_ADDER (smart_adder)->srcpad));

  template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (smart_adder),
      "sink_%u");
  fail_unless (template != NULL);
  requested_pad = gst_element_request_pad (GST_ELEMENT (smart_adder),
      template, NULL, NULL);
  fail_unless (GST_IS_PAD (requested_pad));

  gst_object_unref (smart_adder);
  gst_object_unref (track);
}

GST_END_TEST;

static void
message_received_cb (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      /* we should check if we really finished here */
      GST_WARNING ("Got an EOS");
      g_main_loop_quit (main_loop);
      break;
    case GST_MESSAGE_SEGMENT_START:
    case GST_MESSAGE_SEGMENT_DONE:
      /* We shouldn't see any segement messages, since we didn't do a segment seek */
      GST_WARNING ("Saw a Segment start/stop");
      fail_if (TRUE);
      g_main_loop_quit (main_loop);
      break;
    case GST_MESSAGE_ERROR:
      fail_error_message (message);
      g_main_loop_quit (main_loop);
    default:
      break;
  }
}

GST_START_TEST (simple_audio_mixed_with_pipeline)
{
  GstBus *bus;
  GESAsset *asset;
  GESClip *tmpclip;
  GstMessage *message;
  GESLayer *layer, *layer1;
  GESTrack *track = GES_TRACK (ges_audio_track_new ());
  GESTimeline *timeline = ges_timeline_new ();
  GESTimelinePipeline *pipeline = ges_test_create_pipeline (timeline);

  ges_timeline_add_track (timeline, track);
  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);

  asset = GES_ASSET (ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL));

  GST_DEBUG ("Setting volume on the layer");
  ges_meta_container_set_float (GES_META_CONTAINER (layer), GES_META_VOLUME,
      1.5);

  tmpclip = ges_layer_add_asset (layer, asset, 0, 0, 1 * GST_SECOND,
      GES_TRACK_TYPE_AUDIO);
  ges_audio_test_source_set_volume (GES_CONTAINER_CHILDREN (tmpclip)->data,
      1.0);
  ges_audio_test_source_set_freq (GES_CONTAINER_CHILDREN (tmpclip)->data, 550);

  tmpclip = ges_layer_add_asset (layer1, asset, 0, 0, 2 * GST_SECOND,
      GES_TRACK_TYPE_AUDIO);

  ges_audio_test_source_set_volume (GES_CONTAINER_CHILDREN (tmpclip)->data, 1);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  main_loop = g_main_loop_new (NULL, FALSE);

  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message", (GCallback) message_received_cb, pipeline);
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING)
      == GST_STATE_CHANGE_FAILURE);
  message = gst_bus_timed_pop_filtered (bus, 5 * GST_SECOND,
      GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);

  if (message == NULL) {
    fail_unless ("No message after 5 seconds" == NULL);
    goto done;
  } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    fail_error_message (message);

  GST_INFO ("running main loop");
  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

done:
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (audio_video_mixed_with_pipeline)
{
  GstBus *bus;
  GESAsset *asset;
  GESClip *tmpclip;
  GstMessage *message;
  GESLayer *layer, *layer1;
  GESTrack *track = GES_TRACK (ges_video_track_new ());
  GESTrack *track_audio = GES_TRACK (ges_audio_track_new ());
  GESTimeline *timeline = ges_timeline_new ();
  GESTimelinePipeline *pipeline = ges_test_create_pipeline (timeline);

  ges_timeline_add_track (timeline, track);
  ges_timeline_add_track (timeline, track_audio);
  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);

  asset = GES_ASSET (ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL));

  tmpclip =
      ges_layer_add_asset (layer, asset, 0 * GST_SECOND, 0, 2 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);

  ges_test_clip_set_vpattern (GES_TEST_CLIP (tmpclip), 18);

  tmpclip =
      ges_layer_add_asset (layer1, asset, 1 * GST_SECOND, 0, 5 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  main_loop = g_main_loop_new (NULL, FALSE);

  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message", (GCallback) message_received_cb, pipeline);
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING)
      == GST_STATE_CHANGE_FAILURE);

  message = gst_bus_timed_pop_filtered (bus, 5 * GST_SECOND,
      GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);

  if (message == NULL) {
    fail_unless ("No message after 5 seconds" == NULL);
    goto done;
  } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    fail_error_message (message);

  GST_INFO ("running main loop");
  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

done:
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("Smart mixers");
  TCase *tc_chain = tcase_create ("smart-mixers");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, simple_smart_adder_test);
  tcase_add_test (tc_chain, simple_audio_mixed_with_pipeline);
  tcase_add_test (tc_chain, audio_video_mixed_with_pipeline);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);
  ges_init ();

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
