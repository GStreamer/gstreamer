/* GStreamer unit test for gstprofile
 *
 * Copyright (C) <2009> Edward Hervey <edward.hervey@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/pbutils/encoding-profile.h>
#include <gst/check/gstcheck.h>

/* Helper functions to create profiles */

static GstEncodingProfile *
create_ogg_vorbis_profile (guint presence, gchar * preset)
{
  GstEncodingContainerProfile *cprof;
  GstCaps *ogg, *vorbis;

  ogg = gst_caps_new_empty_simple ("application/ogg");
  cprof =
      gst_encoding_container_profile_new ((gchar *) "myprofile", NULL, ogg,
      NULL);
  gst_caps_unref (ogg);

  vorbis = gst_caps_new_empty_simple ("audio/x-vorbis");
  fail_unless (gst_encoding_container_profile_add_profile (cprof,
          (GstEncodingProfile *) gst_encoding_audio_profile_new (vorbis, preset,
              NULL, presence)));
  gst_caps_unref (vorbis);

  return (GstEncodingProfile *) cprof;
}

static GstEncodingProfile *
create_ogg_theora_vorbis_profile (guint theorapresence, guint vorbispresence)
{
  GstEncodingContainerProfile *prof;
  GstCaps *ogg, *vorbis, *theora;

  ogg = gst_caps_new_empty_simple ("application/ogg");
  prof =
      gst_encoding_container_profile_new ((gchar *) "myprofile", NULL, ogg,
      NULL);
  gst_caps_unref (ogg);

  vorbis = gst_caps_new_empty_simple ("audio/x-vorbis");
  fail_unless (gst_encoding_container_profile_add_profile (prof,
          (GstEncodingProfile *) gst_encoding_audio_profile_new (vorbis, NULL,
              NULL, vorbispresence)));
  gst_caps_unref (vorbis);

  theora = gst_caps_new_empty_simple ("video/x-theora");
  fail_unless (gst_encoding_container_profile_add_profile (prof,
          (GstEncodingProfile *) gst_encoding_video_profile_new (theora, NULL,
              NULL, theorapresence)));
  gst_caps_unref (theora);

  return (GstEncodingProfile *) prof;
}

static GstEncodingProfile *
create_vorbis_only_profile (void)
{
  GstEncodingProfile *prof;
  GstCaps *vorbis;

  vorbis = gst_caps_new_empty_simple ("audio/x-vorbis");
  prof =
      (GstEncodingProfile *) gst_encoding_audio_profile_new (vorbis, NULL, NULL,
      0);
  gst_caps_unref (vorbis);

  return prof;
}

GST_START_TEST (test_encodebin_states)
{
  GstElement *ebin;
  GstEncodingProfile *prof, *prof2;
  GstCaps *ogg;
  GstPad *srcpad;
  GstPad *target;

  /* Create an encodebin and check that it correctly changes states
   * according to whether a profile is set or not */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);

  /* At this point, the ghostpad has *NO* target */
  target = gst_ghost_pad_get_target (GST_GHOST_PAD (srcpad));
  fail_unless (target == NULL);
  gst_object_unref (srcpad);

  /* No profile,
   * switching to READY should succeed,
   * but switching to PAUSED should fail
   */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_FAILURE);

  /* Set a profile on encodebin... */
  ogg = gst_caps_new_empty_simple ("application/ogg");
  prof = (GstEncodingProfile *) gst_encoding_container_profile_new ((gchar *)
      "myprofile", NULL, ogg, NULL);
  gst_caps_unref (ogg);

  g_object_set (ebin, "profile", prof, NULL);

  /* ... and check the profile has been properly set */
  g_object_get (ebin, "profile", &prof2, NULL);

  fail_unless (gst_encoding_profile_is_equal (prof, prof2));

  gst_encoding_profile_unref (prof);
  gst_encoding_profile_unref (prof2);

  /* Make sure we can go to PAUSED */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* At this point, the source pad *HAS* a target */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  target = gst_ghost_pad_get_target (GST_GHOST_PAD (srcpad));
  fail_unless (target != NULL);
  gst_object_unref (target);
  gst_object_unref (srcpad);


  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_static)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstPad *srcpad, *sinkpad;

  /* Create an encodebin and check that it properly creates the sink pads
   * for a single-stream profile with fixed presence */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* streamprofile that has a forced presence of 1 */
  prof = create_ogg_vorbis_profile (1, NULL);

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  gst_object_unref (srcpad);

  /* Check if the audio sink pad was properly created */
  sinkpad = gst_element_get_static_pad (ebin, "audio_0");
  fail_unless (sinkpad != NULL);
  gst_object_unref (sinkpad);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_nopreset_static)
{
  GstElement *ebin;
  GstEncodingProfile *prof;

  /* Create an encodebin with a bogus preset and check it fails switching states */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* streamprofile that has a forced presence of 1 */
  prof = create_ogg_vorbis_profile (1, (gchar *) "nowaythispresetexists");

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  /* It will go to READY... */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* ... but to not PAUSED */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (ebin, GST_STATE_NULL);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_dynamic)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstPad *srcpad, *sinkpad;
  GstCaps *sinkcaps;

  /* Create an encodebin and check that it properly creates the sink pads
   * for a single-stream profile with a unfixed presence */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* streamprofile that has non-forced presence */
  prof = create_ogg_vorbis_profile (0, NULL);

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  gst_object_unref (srcpad);

  /* Check if the audio sink pad can be requested */
  sinkpad = gst_element_get_request_pad (ebin, "audio_0");
  fail_unless (sinkpad != NULL);
  gst_element_release_request_pad (ebin, sinkpad);
  gst_object_unref (sinkpad);
  sinkpad = NULL;

  /* Check again with the 'request-pad' signal */
  sinkcaps = gst_caps_new_empty_simple ("audio/x-raw");
  g_signal_emit_by_name (ebin, "request-pad", sinkcaps, &sinkpad);
  gst_caps_unref (sinkcaps);
  fail_unless (sinkpad != NULL);
  gst_element_release_request_pad (ebin, sinkpad);
  gst_object_unref (sinkpad);
  sinkpad = NULL;

  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_multiple_static)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstPad *srcpad, *sinkpadvorbis, *sinkpadtheora;

  /* Create an encodebin and check that it properly creates the sink pads */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* First try is with a streamprofile that has a forced presence of 1 */
  prof = create_ogg_theora_vorbis_profile (1, 1);

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  gst_object_unref (srcpad);

  /* Check if the audio sink pad was properly created */
  sinkpadvorbis = gst_element_get_static_pad (ebin, "audio_0");
  fail_unless (sinkpadvorbis != NULL);
  gst_object_unref (sinkpadvorbis);

  /* Check if the video sink pad was properly created */
  sinkpadtheora = gst_element_get_static_pad (ebin, "video_1");
  fail_unless (sinkpadtheora != NULL);
  gst_object_unref (sinkpadtheora);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_multiple_dynamic)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstPad *srcpad, *sinkpadvorbis, *sinkpadtheora;

  /* Create an encodebin and check that it properly creates the sink pads
   * for a multiple-stream with unfixed presence */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* multi-stream profile that has non-forced presence */
  prof = create_ogg_theora_vorbis_profile (0, 0);

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  gst_object_unref (srcpad);

  /* Check if the audio sink pad was properly created */
  sinkpadvorbis = gst_element_get_request_pad (ebin, "audio_0");
  fail_unless (sinkpadvorbis != NULL);

  /* Check if the video sink pad was properly created */
  sinkpadtheora = gst_element_get_request_pad (ebin, "video_1");
  fail_unless (sinkpadtheora != NULL);

  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_element_release_request_pad (GST_ELEMENT (ebin), sinkpadvorbis);
  gst_object_unref (sinkpadvorbis);
  gst_element_release_request_pad (GST_ELEMENT (ebin), sinkpadtheora);
  gst_object_unref (sinkpadtheora);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_sink_pads_dynamic_encoder)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstPad *srcpad, *sinkpad = NULL;
  GstCaps *vorbiscaps;

  /* Create an encodebin and check that it properly creates the sink pads
   * for a single-stream profile with a unfixed presence */

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* streamprofile that has non-forced presence */
  prof = create_ogg_vorbis_profile (0, NULL);

  g_object_set (ebin, "profile", prof, NULL);

  gst_encoding_profile_unref (prof);

  /* Check if the source pad was properly created */
  srcpad = gst_element_get_static_pad (ebin, "src");
  fail_unless (srcpad != NULL);
  gst_object_unref (srcpad);

  /* Check if the audio sink pad was properly created */
  vorbiscaps = gst_caps_from_string ("audio/x-vorbis,channels=2,rate=44100");
  g_signal_emit_by_name (ebin, "request-pad", vorbiscaps, &sinkpad);
  gst_caps_unref (vorbiscaps);
  fail_unless (sinkpad != NULL);
  gst_element_release_request_pad (ebin, sinkpad);
  gst_object_unref (sinkpad);

  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
};

GST_END_TEST;

GST_START_TEST (test_encodebin_render_audio_static)
{
  GstElement *ebin, *pipeline, *audiotestsrc, *fakesink;
  GstEncodingProfile *prof;
  GstBus *bus;
  gboolean done = FALSE;

  /* Create an encodebin and render 5s of vorbis/ogg */

  pipeline = gst_pipeline_new ("encodebin-pipeline");
  bus = gst_pipeline_get_bus ((GstPipeline *) pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiotestsrc, "num-buffers", 10, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);

  prof = create_ogg_vorbis_profile (1, NULL);
  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  gst_bin_add_many ((GstBin *) pipeline, audiotestsrc, ebin, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, ebin, fakesink, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *msg;

    /* poll the bus until we get EOS without any errors */
    msg = gst_bus_timed_pop (bus, GST_SECOND / 10);
    if (msg) {
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          fail ("GST_MESSAGE_ERROR");
          break;
        case GST_MESSAGE_EOS:
          done = TRUE;
          break;
        default:
          break;
      }
      gst_message_unref (msg);
    }
  }

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_encodebin_render_audio_only_static)
{
  GstElement *ebin, *pipeline, *audiotestsrc, *fakesink;
  GstEncodingProfile *prof;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *sinkpad;
  GstCaps *sinkcaps;

  /* Create an encodebin and render 5s of vorbis only */
  pipeline = gst_pipeline_new ("encodebin-pipeline");
  bus = gst_pipeline_get_bus ((GstPipeline *) pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiotestsrc, "num-buffers", 10, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);

  prof = create_vorbis_only_profile ();
  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, ebin, fakesink, NULL);

  GST_DEBUG ("linking encodebin");
  fail_unless (gst_element_link_many (audiotestsrc, ebin, fakesink, NULL));

  /* Requesting a new pad should fail */
  ASSERT_CRITICAL (gst_element_get_request_pad (ebin, "audio_0"));

  sinkcaps = gst_caps_new_empty_simple ("audio/x-raw");
  g_signal_emit_by_name (ebin, "request-pad", sinkcaps, &sinkpad);
  gst_caps_unref (sinkcaps);
  fail_if (sinkpad != NULL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *msg;

    /* poll the bus until we get EOS without any errors */
    msg = gst_bus_timed_pop (bus, GST_SECOND / 10);
    if (msg) {
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          fail ("GST_MESSAGE_ERROR");
          break;
        case GST_MESSAGE_EOS:
          done = TRUE;
          break;
        default:
          break;
      }
      gst_message_unref (msg);
    }
  }

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_encodebin_render_audio_dynamic)
{
  GstElement *ebin, *pipeline, *audiotestsrc, *fakesink;
  GstEncodingProfile *prof;
  GstBus *bus;
  GstPad *sinkpad, *srcpad;
  gboolean done = FALSE;

  /* Create an encodebin and render 5s of vorbis/ogg */

  pipeline = gst_pipeline_new ("encodebin-pipeline");
  bus = gst_pipeline_get_bus ((GstPipeline *) pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiotestsrc, "num-buffers", 10, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);

  prof = create_ogg_vorbis_profile (0, NULL);
  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  gst_bin_add_many ((GstBin *) pipeline, audiotestsrc, ebin, fakesink, NULL);

  srcpad = gst_element_get_static_pad (audiotestsrc, "src");
  fail_unless (srcpad != NULL);

  sinkpad = gst_element_get_request_pad (ebin, "audio_0");
  fail_unless (sinkpad != NULL);

  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad), GST_PAD_LINK_OK);

  gst_object_unref (srcpad);

  fail_unless (gst_element_link (ebin, fakesink));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *msg;

    /* poll the bus until we get EOS without any errors */
    msg = gst_bus_timed_pop (bus, GST_SECOND / 10);
    if (msg) {
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          fail ("GST_MESSAGE_ERROR");
          break;
        case GST_MESSAGE_EOS:
          done = TRUE;
          break;
        default:
          break;
      }
      gst_message_unref (msg);
    }
  }

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_element_release_request_pad (GST_ELEMENT (ebin), sinkpad);
  gst_object_unref (sinkpad);

  gst_object_unref (bus);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_encodebin_render_audio_video_static)
{
  GstElement *ebin, *pipeline, *audiotestsrc, *videotestsrc, *fakesink;
  GstEncodingProfile *prof;
  GstBus *bus;
  gboolean done = FALSE;

  /* Create an encodebin and render 5s of vorbis/ogg */

  pipeline = gst_pipeline_new ("encodebin-pipeline");
  bus = gst_pipeline_get_bus ((GstPipeline *) pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiotestsrc, "num-buffers", 10, NULL);
  videotestsrc = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (videotestsrc, "num-buffers", 5, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);

  prof = create_ogg_theora_vorbis_profile (1, 1);
  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  gst_bin_add_many ((GstBin *) pipeline, audiotestsrc, videotestsrc, ebin,
      fakesink, NULL);

  fail_unless (gst_element_link (videotestsrc, ebin));
  fail_unless (gst_element_link_many (audiotestsrc, ebin, fakesink, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *msg;

    /* poll the bus until we get EOS without any errors */
    msg = gst_bus_timed_pop (bus, GST_SECOND / 10);
    if (msg) {
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          fail ("GST_MESSAGE_ERROR");
          break;
        case GST_MESSAGE_EOS:
          done = TRUE;
          break;
        default:
          break;
      }
      gst_message_unref (msg);
    }
  }

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_encodebin_render_audio_video_dynamic)
{
  GstElement *ebin, *pipeline, *audiotestsrc, *videotestsrc, *fakesink;
  GstEncodingProfile *prof;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *sinkpad1, *sinkpad2, *srcpad;

  /* Create an encodebin and render 5s of vorbis/ogg */

  pipeline = gst_pipeline_new ("encodebin-pipeline");
  bus = gst_pipeline_get_bus ((GstPipeline *) pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiotestsrc, "num-buffers", 10, NULL);
  videotestsrc = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (videotestsrc, "num-buffers", 5, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);

  prof = create_ogg_theora_vorbis_profile (0, 0);
  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  gst_bin_add_many ((GstBin *) pipeline, audiotestsrc, videotestsrc, ebin,
      fakesink, NULL);

  fail_unless (gst_element_link (ebin, fakesink));

  srcpad = gst_element_get_static_pad (audiotestsrc, "src");
  sinkpad1 = gst_element_get_request_pad (ebin, "audio_0");
  fail_unless (srcpad != NULL);
  fail_unless (sinkpad1 != NULL);
  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad1), GST_PAD_LINK_OK);
  gst_object_unref (srcpad);

  srcpad = gst_element_get_static_pad (videotestsrc, "src");
  sinkpad2 = gst_element_get_request_pad (ebin, "video_1");
  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad2), GST_PAD_LINK_OK);
  gst_object_unref (srcpad);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *msg;

    /* poll the bus until we get EOS without any errors */
    msg = gst_bus_timed_pop (bus, GST_SECOND / 10);
    if (msg) {
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          fail ("GST_MESSAGE_ERROR");
          break;
        case GST_MESSAGE_EOS:
          done = TRUE;
          break;
        default:
          break;
      }
      gst_message_unref (msg);
    }
  }

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_element_release_request_pad (GST_ELEMENT (ebin), sinkpad1);
  gst_object_unref (sinkpad1);
  gst_element_release_request_pad (GST_ELEMENT (ebin), sinkpad2);
  gst_object_unref (sinkpad2);

  gst_object_unref (bus);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_encodebin_impossible_element_combination)
{
  GstElement *ebin;
  GstEncodingProfile *prof;
  GstCaps *ogg, *x264;

  ebin = gst_element_factory_make ("x264enc", NULL);
  if (ebin == NULL) {
    GST_DEBUG ("No available h264 encoder, skipping test");
    return;
  }
  gst_object_unref (ebin);

  /* Make sure that impossible combinations of encoders and muxer
   * properly fail. In this case we try putting h264 in ogg.
   *
   * To properly test we abort early, we use a presence of zero for the
   * h264 stream profile. */

  ebin = gst_element_factory_make ("encodebin", NULL);

  ogg = gst_caps_new_empty_simple ("application/ogg");
  prof = (GstEncodingProfile *) gst_encoding_container_profile_new ((gchar *)
      "myprofile", NULL, ogg, NULL);
  gst_caps_unref (ogg);

  x264 = gst_caps_new_empty_simple ("video/x-h264");
  fail_unless (gst_encoding_container_profile_add_profile
      (GST_ENCODING_CONTAINER_PROFILE (prof),
          (GstEncodingProfile *) gst_encoding_video_profile_new (x264, NULL,
              NULL, 0)));
  gst_caps_unref (x264);

  g_object_set (ebin, "profile", prof, NULL);
  gst_encoding_profile_unref (prof);

  /* It will go to READY... */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* ... but to not PAUSED */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (ebin, GST_STATE_NULL);

  gst_object_unref (ebin);
};

GST_END_TEST;

static void
_test_encodebin_reuse (GstEncodingProfile * prof1, GstEncodingProfile * prof2)
{
  GstElement *ebin;

  ebin = gst_element_factory_make ("encodebin", NULL);

  /* Set a profile on encodebin... */
  if (prof1)
    g_object_set (ebin, "profile", prof1, NULL);

  /* Make sure we can go to PAUSED */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  if (prof2)
    g_object_set (ebin, "profile", prof2, NULL);

  /* Make sure we can go to PLAYING */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  /* Set back to NULL */
  fail_unless_equals_int (gst_element_set_state (ebin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (ebin);
}

GST_START_TEST (test_encodebin_reuse)
{
  GstEncodingProfile *prof1;
  GstEncodingProfile *prof2;
  GstEncodingProfile *prof3;
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("application/ogg");
  prof1 = (GstEncodingProfile *) gst_encoding_container_profile_new ((gchar *)
      "myprofile", NULL, caps, NULL);
  gst_caps_unref (caps);

  prof2 = create_ogg_theora_vorbis_profile (1, 1);
  prof3 = create_vorbis_only_profile ();

  _test_encodebin_reuse (prof1, NULL);
  _test_encodebin_reuse (prof1, prof1);

  _test_encodebin_reuse (prof1, prof2);

  _test_encodebin_reuse (prof2, prof3);

  gst_encoding_profile_unref (prof1);
  gst_encoding_profile_unref (prof2);
  gst_encoding_profile_unref (prof3);
};

GST_END_TEST;


static Suite *
encodebin_suite (void)
{
  Suite *s = suite_create ("encodebin element");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_encodebin_states);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_static);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_nopreset_static);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_dynamic);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_multiple_static);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_multiple_dynamic);
  tcase_add_test (tc_chain, test_encodebin_sink_pads_dynamic_encoder);
  tcase_add_test (tc_chain, test_encodebin_render_audio_static);
  tcase_add_test (tc_chain, test_encodebin_render_audio_only_static);
  tcase_add_test (tc_chain, test_encodebin_render_audio_dynamic);
  tcase_add_test (tc_chain, test_encodebin_render_audio_video_static);
  tcase_add_test (tc_chain, test_encodebin_render_audio_video_dynamic);
  tcase_add_test (tc_chain, test_encodebin_impossible_element_combination);
  tcase_add_test (tc_chain, test_encodebin_reuse);

  return s;
}

GST_CHECK_MAIN (encodebin);
