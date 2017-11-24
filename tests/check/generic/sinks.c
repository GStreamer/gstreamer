/* GStreamer
 *
 * unit test for sinks
 *
 * Copyright (C) <2005> Wim Taymans <wim at fluendo dot com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

static void
pop_state_change_message (GstBus * bus, GstElement * src, GstState old,
    GstState new, GstState pending)
{
  GstMessage *message = NULL;
  GstState _old, _new, _pending;

  message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, GST_SECOND);
  fail_unless (message != NULL,
      "Expected state change message, but got nothing");

  gst_message_parse_state_changed (message, &_old, &_new, &_pending);

  fail_unless (GST_MESSAGE_SRC (message) == (GstObject *) src,
      "Unexpected state change order");
  fail_unless (old == _old, "Unexpected old state");
  fail_unless (new == _new, "Unexpected new state");
  fail_unless (pending == _pending, "Unexpected pending state");

  gst_message_unref (message);
}

/* a sink should go ASYNC to PAUSE. forcing PLAYING is possible */
GST_START_TEST (test_sink)
{
  GstElement *sink;
  GstStateChangeReturn ret;
  GstState current, pending;

  sink = gst_element_factory_make ("fakesink", "sink");

  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");

  ret = gst_element_set_state (sink, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no forced async state change");

  ret = gst_element_get_state (sink, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not changing state async");
  fail_unless (current == GST_STATE_READY, "bad current state");
  fail_unless (pending == GST_STATE_PLAYING, "bad pending state");

  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async going back to paused");

  ret = gst_element_set_state (sink, GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to ready");

  ret = gst_element_set_state (sink, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to null");

  gst_object_unref (sink);
}

GST_END_TEST
/* a sink should go ASYNC to PAUSE and PLAYING, when linking a src, it
 * should complete the state change. */
GST_START_TEST (test_sink_completion)
{
  GstElement *sink, *src;
  GstStateChangeReturn ret;
  GstState current, pending;

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

  ret = gst_element_set_state (sink, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");

  ret = gst_element_get_state (sink, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not changing state async");
  fail_unless (current == GST_STATE_READY, "bad current state");
  fail_unless (pending == GST_STATE_PLAYING, "bad pending state");

  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "datarate", 200, "sizetype", 2, NULL);
  gst_element_link (src, sink);

  ret = gst_element_set_state (src, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");

  /* now wait for final state */
  ret = gst_element_get_state (sink, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to change state");
  fail_unless (current == GST_STATE_PLAYING, "bad current state");
  fail_unless (pending == GST_STATE_VOID_PENDING, "bad pending state");

  ret = gst_element_set_state (sink, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to null");

  ret = gst_element_set_state (src, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to null");

  gst_object_unref (sink);
  gst_object_unref (src);
}

GST_END_TEST
/* a sink should go ASYNC to PAUSE. PAUSE should complete when
 * prerolled. */
GST_START_TEST (test_src_sink)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "cannot start play");

  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "cannot null pipeline");

  gst_object_unref (pipeline);

}

GST_END_TEST
/* a pipeline with live source should return NO_PREROLL in
 * PAUSE. When removing the live source it should return ASYNC
 * from the sink */
GST_START_TEST (test_livesrc_remove)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_object_ref (src);
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no no_preroll state return");

  ret = gst_element_get_state (src, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not paused");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  gst_bin_remove (GST_BIN (pipeline), src);

  ret = gst_element_set_state (src, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "async going to null");
  gst_object_unref (src);

  ret = gst_element_get_state (pipeline, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not async");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_PAUSED, "not paused");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "async going to null");
  gst_object_unref (pipeline);
}

GST_END_TEST
/* the sink should go ASYNC to PAUSE. The live source should go
 * NO_PREROLL to PAUSE. the pipeline returns NO_PREROLL. An
 * attempt to go to PLAYING will return ASYNC. polling state
 * completion should return SUCCESS when the sink is gone to
 * PLAYING. */
GST_START_TEST (test_livesrc_sink)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;
  GstBus *bus;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  bus = gst_element_get_bus (pipeline);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no no_preroll state return");

  pop_state_change_message (bus, sink, GST_STATE_NULL, GST_STATE_READY,
      GST_STATE_VOID_PENDING);
  pop_state_change_message (bus, src, GST_STATE_NULL, GST_STATE_READY,
      GST_STATE_VOID_PENDING);
  pop_state_change_message (bus, pipeline, GST_STATE_NULL, GST_STATE_READY,
      GST_STATE_PAUSED);

  /* this order only holds true for live sources because they do not push
     buffers in PAUSED */
  pop_state_change_message (bus, src, GST_STATE_READY, GST_STATE_PAUSED,
      GST_STATE_VOID_PENDING);
  pop_state_change_message (bus, pipeline, GST_STATE_READY, GST_STATE_PAUSED,
      GST_STATE_VOID_PENDING);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no no_preroll state return the second time");

  ret = gst_element_get_state (src, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not paused");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  /* don't block here */
  ret = gst_element_get_state (sink, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not async");
  fail_unless (current == GST_STATE_READY, "not ready");
  fail_unless (pending == GST_STATE_PAUSED, "not paused");

  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not paused");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not async");
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  /* now we have four messages on the bus: src from paused to playing, sink from
     ready to paused and paused to playing, and pipeline from paused to playing.
     the pipeline message should be last, and the sink messages should go in
     order, but the src message can be interleaved with the sink one. */
  {
    GstMessage *m;
    GstState old, new, pending;
    gint n_src = 1, n_sink = 2;

    while (n_src + n_sink > 0) {
      m = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, GST_SECOND);
      fail_unless (m != NULL, "expected state change message");
      gst_message_parse_state_changed (m, &old, &new, &pending);
      if (GST_MESSAGE_SRC (m) == (GstObject *) src) {
        fail_unless (n_src == 1, "already got one message from the src");
        n_src--;
        fail_unless (old == GST_STATE_PAUSED, "unexpected old");
        fail_unless (new == GST_STATE_PLAYING, "unexpected new (got %d)", new);
        fail_unless (pending == GST_STATE_VOID_PENDING, "unexpected pending");
      } else if (GST_MESSAGE_SRC (m) == (GstObject *) sink) {
        if (n_sink == 2) {
          fail_unless (old == GST_STATE_READY, "unexpected old");
          fail_unless (new == GST_STATE_PAUSED, "unexpected new");
          fail_unless (pending == GST_STATE_VOID_PENDING, "unexpected pending");
        } else if (n_sink == 1) {
          fail_unless (old == GST_STATE_PAUSED, "unexpected old");
          fail_unless (new == GST_STATE_PLAYING, "unexpected new");
          fail_unless (pending == GST_STATE_VOID_PENDING, "unexpected pending");
        } else {
          g_assert_not_reached ();
        }
        n_sink--;
      } else {
        g_critical
            ("Unexpected state change message src %s (%d src %d sink pending)",
            GST_OBJECT_NAME (GST_MESSAGE_SRC (m)), n_src, n_sink);
      }
      gst_message_unref (m);
    }
  }

  pop_state_change_message (bus, pipeline, GST_STATE_PAUSED, GST_STATE_PLAYING,
      GST_STATE_VOID_PENDING);

  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* The sink should go ASYNC to PLAYING. The source should go
 * to PLAYING with SUCCESS. The pipeline returns ASYNC. */
GST_START_TEST (test_livesrc2_sink)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");

  ret = gst_element_get_state (src, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  /* and back down */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no no_preroll state return");

  ret = gst_element_get_state (src, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not no_preroll");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not paused");

  /* sink state is not known.. it might be prerolled or not */

  /* and to READY */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");

  ret = gst_element_get_state (src, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not success");
  fail_unless (current == GST_STATE_READY, "not ready");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not ready");

  ret = gst_element_get_state (sink, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not success");
  fail_unless (current == GST_STATE_READY, "not ready");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not ready");

  /* And destroy. Must be NULL */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_livesrc3_sink)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");

  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not playing");

  /* and back down */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_locked_sink)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  /* we don't link the elements */

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not no_preroll");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  /* the sink is now async going from ready to paused */
  ret = gst_element_get_state (sink, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not async");
  fail_unless (current == GST_STATE_READY, "not ready");
  fail_unless (pending == GST_STATE_PAUSED, "not paused");

  /* lock the sink */
  gst_element_set_locked_state (sink, TRUE);

  /* move to PlAYING, the sink should remain ASYNC. The pipeline
   * returns ASYNC */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* back to PAUSED, we should get NO_PREROLL again */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* unlock the sink */
  gst_element_set_locked_state (sink, FALSE);

  /* and now everything back down */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_unlinked_live)
{
  GstElement *sink, *src, *lsrc, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  lsrc = gst_element_factory_make ("fakesrc", "lsrc");
  g_object_set (G_OBJECT (lsrc), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), lsrc);
  gst_bin_add (GST_BIN (pipeline), sink);

  /* link non live source to sink */
  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* we don't link the srcpad of the live source, it will not contribute to the
   * NO_PREROLL. */

  /* set state to PAUSED, this should return NO_PREROLL because there is a live
   * source. since the only sink in this pipeline is linked to a non-live
   * source, it will preroll eventually. */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* wait till the sink is prerolled */
  ret = gst_element_get_state (sink, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not success");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have playing");

  /* the pipeline should still return NO_PREROLL */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not no_preroll");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have playing");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_delayed_async)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add source, don't add sink yet */
  gst_bin_add (GST_BIN (pipeline), src);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* add sink now and set to PAUSED */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* This will make the bin notice an ASYNC element. */
  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* we should still be NO_PREROLL now although there is an async element in the
   * pipeline. */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  /* link live source to sink */
  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* we should get SUCCESS now */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PLAYING, "not PLAYING");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_added_async)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add source, don't add sink yet */
  gst_bin_add (GST_BIN (pipeline), src);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* set sink to PAUSED without adding it to the pipeline */
  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* add sink now, pipeline should notice the async element */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* we should still be NO_PREROLL now although there is an async element in the
   * pipeline. */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  /* link live source to sink */
  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* we should get SUCCESS now */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PLAYING, "not PLAYING");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_added_async2)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add source, don't add sink yet */
  gst_bin_add (GST_BIN (pipeline), src);
  /* need to lock state here or the pipeline might go in error */
  gst_element_set_locked_state (src, TRUE);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no SUCCESS state return");

  /* set sink to PAUSED without adding it to the pipeline */
  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* add sink now, pipeline should notice the async element */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* we should be ASYNC now because there is an async element in the
   * pipeline. */
  ret = gst_element_get_state (pipeline, &current, &pending, 0);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not ASYNC");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_PAUSED, "not paused");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_add_live)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add sink, don't add sourc3 yet */
  gst_bin_add (GST_BIN (pipeline), sink);

  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* set source to PAUSED without adding it to the pipeline */
  ret = gst_element_set_state (src, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* add source now, pipeline should notice the NO_PREROLL element */
  gst_bin_add (GST_BIN (pipeline), src);

  /* we should be NO_PREROLL now because there is a NO_PREROLL element in the
   * pipeline. */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

static GMutex blocked_lock;
static GCond blocked_cond;

static GstPadProbeReturn
pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  g_mutex_lock (&blocked_lock);
  GST_DEBUG ("srcpad blocked: %d, sending signal", info->type);
  g_cond_signal (&blocked_cond);
  g_mutex_unlock (&blocked_lock);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_add_live2)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;
  gulong id;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add sink, don't add source yet */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* set the pipeline to PLAYING. This will return ASYNC on READY->PAUSED */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  g_mutex_lock (&blocked_lock);

  GST_DEBUG ("blocking srcpad");
  /* block source pad */
  srcpad = gst_element_get_static_pad (src, "src");
  id = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      pad_blocked_cb, NULL, NULL);

  /* set source to PAUSED without adding it to the pipeline */
  ret = gst_element_set_state (src, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");

  /* add source now, pipeline should notice the NO_PREROLL element. This
   * should trigger as commit of the ASYNC pipeline and make it continue
   * to PLAYING. We blocked the source pad so that we don't get an unlinked
   * error. */
  gst_bin_add (GST_BIN (pipeline), src);

  /* wait for pad blocked, this means the source is now PLAYING. */
  g_cond_wait (&blocked_cond, &blocked_lock);
  g_mutex_unlock (&blocked_lock);

  GST_DEBUG ("linking pads");

  /* link to sink */
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  GST_DEBUG ("unblocking srcpad");

  /* and unblock */
  gst_pad_remove_probe (srcpad, id);

  GST_DEBUG ("getting state");

  /* we should be SUCCESS now and PLAYING */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");
  fail_unless (current == GST_STATE_PLAYING, "not PLAYING");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_bin_live)
{
  GstElement *sink, *src, *pipeline, *bin;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new ("pipeline");
  bin = gst_bin_new ("bin");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), sink);
  gst_bin_add (GST_BIN (pipeline), bin);

  srcpad = gst_element_get_static_pad (src, "src");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* PAUSED returns NO_PREROLL because of the live source */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "no NO_PREROLL state return");
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL, "not NO_PREROLL");
  fail_unless (current == GST_STATE_PAUSED, "not paused");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not void pending");

  /* when going to PLAYING, the sink should go to PLAYING ASYNC */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not ASYNC");

  /* now wait for PLAYING to complete */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not playing");
  fail_unless (current == GST_STATE_PLAYING, "not playing");
  fail_unless (pending == GST_STATE_VOID_PENDING, "not void pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "cannot null pipeline");

  gst_object_unref (pipeline);
}

GST_END_TEST static gpointer
send_eos (GstPad * sinkpad)
{
  gboolean ret;

  ret = gst_pad_send_event (sinkpad, gst_event_new_eos ());

  return GINT_TO_POINTER (ret);
}

/* push a buffer with a very long duration in a fakesink, then push an EOS
 * event. fakesink should emit EOS after the duration of the buffer expired.
 * Going to PAUSED, however should not return ASYNC while processing the
 * buffer. */
GST_START_TEST (test_fake_eos)
{
  GstElement *sink, *pipeline;
  GstBuffer *buffer;
  GstStateChangeReturn ret;
  GstPad *sinkpad;
  GstFlowReturn res;
  GThread *thread;
  GstSegment segment;

  pipeline = gst_pipeline_new ("pipeline");

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

  sinkpad = gst_element_get_static_pad (sink, "sink");

  gst_bin_add (GST_BIN_CAST (pipeline), sink);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* push buffer of 100 seconds, since it has a timestamp of 0, it should be
   * rendered immediately and the chain function should return immediately */
  buffer = gst_buffer_new_and_alloc (10);
  GST_BUFFER_TIMESTAMP (buffer) = 0;
  GST_BUFFER_DURATION (buffer) = 100 * GST_SECOND;
  res = gst_pad_chain (sinkpad, buffer);
  fail_unless (res == GST_FLOW_OK, "no OK flow return");

  /* wait for preroll, this should happen really soon. */
  ret = gst_element_get_state (pipeline, NULL, NULL, -1);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no SUCCESS state return");

  /* push EOS, this will block for up to 100 seconds, until the previous
   * buffer has finished. We therefore push it in another thread so we can do
   * something else while it blocks. */
  thread =
      g_thread_try_new ("gst-check", (GThreadFunc) send_eos, sinkpad, NULL);
  fail_if (thread == NULL, "no thread");

  /* wait a while so that the thread manages to start and push the EOS */
  g_usleep (G_USEC_PER_SEC);

  /* this should cancel rendering of the EOS event and should return SUCCESS
   * because the sink is now prerolled on the EOS. */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no SUCCESS state return");

  /* we can unref the sinkpad now, we're done with it */
  gst_object_unref (sinkpad);

  /* wait for a second, use the debug log to see that basesink does not discard
   * the EOS */
  g_usleep (G_USEC_PER_SEC);

  /* go back to PLAYING, which means waiting some more in EOS, check debug log
   * to see this happen. */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no SUCCESS state return");
  g_usleep (G_USEC_PER_SEC);

  /* teardown and cleanup */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no SUCCESS state return");

  /* we can join now */
  g_thread_join (thread);

  gst_object_unref (pipeline);
}

GST_END_TEST;

/* this variable is updated in the same thread, first it is set by the
 * handoff-preroll signal, then it is checked when the ASYNC_DONE is posted on
 * the bus */
static gboolean have_preroll = FALSE;

static void
async_done_handoff (GstElement * element, GstBuffer * buf, GstPad * pad,
    GstElement * sink)
{
  GST_DEBUG ("we have the preroll buffer");
  have_preroll = TRUE;
}

/* when we get the ASYNC_DONE, query the position */
static GstBusSyncReply
async_done_func (GstBus * bus, GstMessage * msg, GstElement * sink)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
    gint64 position;

    GST_DEBUG ("we have ASYNC_DONE now");
    fail_unless (have_preroll == TRUE, "no preroll buffer received");

    /* get the position now */
    gst_element_query_position (sink, GST_FORMAT_TIME, &position);

    GST_DEBUG ("we have position %" GST_TIME_FORMAT, GST_TIME_ARGS (position));

    fail_unless (position == 10 * GST_SECOND, "position is wrong");
  }

  /* we must unref the message if we return DROP */
  gst_message_unref (msg);

  /* we can drop the message, nothing is listening for it. */
  return GST_BUS_DROP;
}

static void
send_buffer (GstPad * sinkpad)
{
  GstBuffer *buffer;
  GstFlowReturn ret;

  /* push a second buffer */
  GST_DEBUG ("pushing last buffer");
  buffer = gst_buffer_new_and_alloc (10);
  GST_BUFFER_TIMESTAMP (buffer) = 200 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 100 * GST_SECOND;

  /* this function will initially block */
  ret = gst_pad_chain (sinkpad, buffer);
  fail_unless (ret == GST_FLOW_OK, "no OK flow return");
}

/* when we get the ASYNC_DONE message from a sink, we want the sink to be able
 * to report the duration and position. The sink should also have called the
 * render method. */
GST_START_TEST (test_async_done)
{
  GstElement *sink;
  GstEvent *event;
  GstStateChangeReturn ret;
  GstPad *sinkpad;
  gboolean res;
  GstBus *bus;
  GThread *thread;
  gint64 position;
  gboolean qret;
  GstSegment segment;

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);

  g_signal_connect (sink, "preroll-handoff", (GCallback) async_done_handoff,
      sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");

  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* set bus on element synchronously listen for ASYNC_DONE */
  bus = gst_bus_new ();
  gst_element_set_bus (sink, bus);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) async_done_func, sink,
      NULL);

  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));

  /* make newsegment, this sets the position to 10sec when the buffer prerolls */
  GST_DEBUG ("sending segment");
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.time = 10 * GST_SECOND;

  event = gst_event_new_segment (&segment);
  res = gst_pad_send_event (sinkpad, event);
  fail_unless (res == TRUE);

  /* We have not yet received any buffers so we are still in the READY state,
   * the position is therefore still not queryable. */
  position = -1;
  qret = gst_element_query_position (sink, GST_FORMAT_TIME, &position);
  fail_unless (qret == TRUE, "position wrong");
  fail_unless (position == 10 * GST_SECOND, "position is wrong");

  /* last buffer, blocks because preroll queue is filled. Start the push in a
   * new thread so that we can check the position */
  GST_DEBUG ("starting thread");
  thread =
      g_thread_try_new ("gst-check", (GThreadFunc) send_buffer, sinkpad, NULL);
  fail_if (thread == NULL, "no thread");

  GST_DEBUG ("waiting 1 second");
  g_usleep (G_USEC_PER_SEC);
  GST_DEBUG ("waiting done");

  /* check if position is still 10 seconds. This is racy because  the above
   * thread might not yet have started the push, because of the above sleep,
   * this is very unlikely, though. */
  gst_element_query_position (sink, GST_FORMAT_TIME, &position);
  GST_DEBUG ("second buffer position %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));
  fail_unless (position == 10 * GST_SECOND, "position is wrong");

  /* now we go to playing. This should unlock and stop the above thread. */
  GST_DEBUG ("going to PLAYING");
  ret = gst_element_set_state (sink, GST_STATE_PLAYING);

  /* join the thread. At this point we know the sink processed the last buffer
   * and the position should now be 210 seconds; the time of the last buffer we
   * pushed. The element has no clock or base-time so it only reports the
   * last seen timestamp of the buffer, it does not know how much of the buffer
   * is consumed. */
  GST_DEBUG ("joining thread");
  g_thread_join (thread);

  gst_element_query_position (sink, GST_FORMAT_TIME, &position);
  GST_DEBUG ("last buffer position %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));
  fail_unless (position == 210 * GST_SECOND, "position is wrong");

  gst_object_unref (sinkpad);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_object_unref (sink);
  gst_object_unref (bus);
}

GST_END_TEST;

/* when we get the ASYNC_DONE, query the position */
static GstBusSyncReply
async_done_eos_func (GstBus * bus, GstMessage * msg, GstElement * sink)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
    gint64 position;

    GST_DEBUG ("we have ASYNC_DONE now");

    /* get the position now */
    gst_element_query_position (sink, GST_FORMAT_TIME, &position);

    GST_DEBUG ("we have position %" GST_TIME_FORMAT, GST_TIME_ARGS (position));

    fail_unless (position == 10 * GST_SECOND, "position is wrong");
  }
  /* we must unref the message if we return DROP */
  gst_message_unref (msg);
  /* we can drop the message, nothing is listening for it. */
  return GST_BUS_DROP;
}

/* when we get the ASYNC_DONE message from a sink, we want the sink to be able
 * to report the duration and position. The sink should also have called the
 * render method. */
GST_START_TEST (test_async_done_eos)
{
  GstElement *sink;
  GstEvent *event;
  GstStateChangeReturn ret;
  GstPad *sinkpad;
  gboolean res;
  GstBus *bus;
  gboolean qret;
  GstSegment segment;
  gint64 position;
  GThread *thread;

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

  sinkpad = gst_element_get_static_pad (sink, "sink");

  ret = gst_element_set_state (sink, GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  /* set bus on element synchronously listen for ASYNC_DONE */
  bus = gst_bus_new ();
  gst_element_set_bus (sink, bus);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) async_done_eos_func, sink,
      NULL);

  /* make newsegment, this sets the position to 10sec when the buffer prerolls */
  GST_DEBUG ("sending segment");
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.time = 10 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  res = gst_pad_send_event (sinkpad, event);
  fail_unless (res == TRUE);

  /* We have not yet received any buffers so we are still in the READY state,
   * the position is therefore still not queryable. */
  position = -1;
  qret = gst_element_query_position (sink, GST_FORMAT_TIME, &position);
  fail_unless (qret == TRUE, "position wrong");
  fail_unless (position == 10 * GST_SECOND, "position is wrong");

  /* Since we are paused and the preroll queue has a length of 1, this function
   * will return immediately. The EOS will complete the preroll and the
   * position should now be 10 seconds. */
  GST_DEBUG ("pushing EOS");
  GST_DEBUG ("starting thread");
  thread =
      g_thread_try_new ("gst-check", (GThreadFunc) send_eos, sinkpad, NULL);
  fail_if (thread == NULL, "no thread");

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, -1);

  /* check if position is still 10 seconds */
  gst_element_query_position (sink, GST_FORMAT_TIME, &position);
  GST_DEBUG ("EOS position %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  fail_unless (position == 10 * GST_SECOND, "position is wrong");

  gst_element_set_state (sink, GST_STATE_NULL);
  g_thread_join (thread);

  gst_object_unref (sinkpad);
  gst_object_unref (sink);
  gst_object_unref (bus);
}

GST_END_TEST;

static GMutex preroll_lock;
static GCond preroll_cond;

static void
test_async_false_seek_preroll (GstElement * elem, GstBuffer * buf,
    GstPad * pad, gpointer data)
{
  g_mutex_lock (&preroll_lock);
  GST_DEBUG ("Got preroll buffer %p", buf);
  g_cond_signal (&preroll_cond);
  g_mutex_unlock (&preroll_lock);
}

static void
test_async_false_seek_handoff (GstElement * elem, GstBuffer * buf,
    GstPad * pad, gpointer data)
{
  /* should never be reached, we never go to PLAYING */
  GST_DEBUG ("Got handoff buffer %p", buf);
  fail_unless (FALSE);
}

GST_START_TEST (test_async_false_seek)
{
  GstElement *pipeline, *source, *sink;

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("test-pipeline");
  source = gst_element_factory_make ("fakesrc", "file-source");
  sink = gst_element_factory_make ("fakesink", "audio-output");

  g_object_set (G_OBJECT (sink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (sink), "num-buffers", 10, NULL);
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);

  g_signal_connect (sink, "handoff", G_CALLBACK (test_async_false_seek_handoff),
      NULL);
  g_signal_connect (sink, "preroll-handoff",
      G_CALLBACK (test_async_false_seek_preroll), NULL);

  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);

  /* we link the elements together */
  gst_element_link (source, sink);

  GST_DEBUG ("Now pausing");
  g_mutex_lock (&preroll_lock);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  /* wait for preroll */
  GST_DEBUG ("wait for preroll");
  g_cond_wait (&preroll_cond, &preroll_lock);
  g_mutex_unlock (&preroll_lock);

  g_mutex_lock (&preroll_lock);
  GST_DEBUG ("Seeking");
  fail_unless (gst_element_seek (pipeline, 1.0, GST_FORMAT_BYTES,
          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1));

  GST_DEBUG ("wait for new preroll");
  /* this either prerolls or fails */
  g_cond_wait (&preroll_cond, &preroll_lock);
  g_mutex_unlock (&preroll_lock);

  GST_DEBUG ("bring pipe to state NULL");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_DEBUG ("Deleting pipeline");
  gst_object_unref (GST_OBJECT (pipeline));
}

GST_END_TEST;

static GMutex handoff_lock;
static GCond handoff_cond;

static void
test_async_false_seek_in_playing_handoff (GstElement * elem, GstBuffer * buf,
    GstPad * pad, gpointer data)
{
  g_mutex_lock (&handoff_lock);
  GST_DEBUG ("Got handoff buffer %p", buf);
  g_cond_signal (&handoff_cond);
  g_mutex_unlock (&handoff_lock);
}

GST_START_TEST (test_async_false_seek_in_playing)
{
  GstElement *pipeline, *source, *sink;

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("test-pipeline");
  source = gst_element_factory_make ("fakesrc", "fake-source");
  sink = gst_element_factory_make ("fakesink", "fake-output");

  g_object_set (G_OBJECT (sink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);

  g_signal_connect (sink, "handoff",
      G_CALLBACK (test_async_false_seek_in_playing_handoff), NULL);

  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);

  /* we link the elements together */
  gst_element_link (source, sink);

  GST_DEBUG ("Now playing");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_mutex_lock (&handoff_lock);
  GST_DEBUG ("wait for handoff buffer");
  g_cond_wait (&handoff_cond, &handoff_lock);
  g_mutex_unlock (&handoff_lock);

  GST_DEBUG ("Seeking");
  fail_unless (gst_element_seek (source, 1.0, GST_FORMAT_BYTES,
          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1));

  g_mutex_lock (&handoff_lock);
  GST_DEBUG ("wait for handoff buffer");
  g_cond_wait (&handoff_cond, &handoff_lock);
  g_mutex_unlock (&handoff_lock);

  GST_DEBUG ("bring pipe to state NULL");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_DEBUG ("Deleting pipeline");
  gst_object_unref (GST_OBJECT (pipeline));
}

GST_END_TEST;

/* test: try changing state of sinks */
static Suite *
gst_sinks_suite (void)
{
  Suite *s = suite_create ("Sinks");
  TCase *tc_chain = tcase_create ("general");
  guint timeout = 10;

  /* time out after 10s, not the default 3, we need this for the last test.
   * We need a longer timeout when running under valgrind though. */
  if (g_getenv ("CK_DEFAULT_TIMEOUT") != NULL)
    timeout = MAX (10, atoi (g_getenv ("CK_DEFAULT_TIMEOUT")));
  tcase_set_timeout (tc_chain, timeout);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_sink);
  tcase_add_test (tc_chain, test_sink_completion);
  tcase_add_test (tc_chain, test_src_sink);
  tcase_add_test (tc_chain, test_livesrc_remove);
  tcase_add_test (tc_chain, test_livesrc_sink);
  tcase_add_test (tc_chain, test_livesrc2_sink);
  tcase_add_test (tc_chain, test_livesrc3_sink);
  tcase_add_test (tc_chain, test_locked_sink);
  tcase_add_test (tc_chain, test_unlinked_live);
  tcase_add_test (tc_chain, test_delayed_async);
  tcase_add_test (tc_chain, test_added_async);
  tcase_add_test (tc_chain, test_added_async2);
  tcase_add_test (tc_chain, test_add_live);
  tcase_add_test (tc_chain, test_add_live2);
  tcase_add_test (tc_chain, test_bin_live);
  tcase_add_test (tc_chain, test_fake_eos);
  tcase_add_test (tc_chain, test_async_done);
  tcase_add_test (tc_chain, test_async_done_eos);
  tcase_add_test (tc_chain, test_async_false_seek);
  tcase_add_test (tc_chain, test_async_false_seek_in_playing);

  return s;
}

GST_CHECK_MAIN (gst_sinks);
