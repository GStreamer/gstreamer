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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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
  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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
  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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
  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

static GMutex *blocked_lock;
static GCond *blocked_cond;

static void
pad_blocked_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  g_mutex_lock (blocked_lock);
  GST_DEBUG ("srcpad blocked: %d, sending signal", blocked);
  g_cond_signal (blocked_cond);
  g_mutex_unlock (blocked_lock);
}

GST_START_TEST (test_add_live2)
{
  GstElement *sink, *src, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstPad *srcpad, *sinkpad;

  blocked_lock = g_mutex_new ();
  blocked_cond = g_cond_new ();

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  /* add sink, don't add source yet */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* set the pipeline to PLAYING. This will return ASYNC on READY->PAUSED */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no ASYNC state return");

  g_mutex_lock (blocked_lock);

  GST_DEBUG ("blocking srcpad");
  /* block source pad */
  srcpad = gst_element_get_pad (src, "src");
  gst_pad_set_blocked_async (srcpad, TRUE, pad_blocked_cb, NULL);

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
  g_cond_wait (blocked_cond, blocked_lock);
  g_mutex_unlock (blocked_lock);

  GST_DEBUG ("linking pads");

  /* link to sink */
  sinkpad = gst_element_get_pad (sink, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  GST_DEBUG ("unblocking srcpad");

  /* and unblock */
  gst_pad_set_blocked_async (srcpad, FALSE, pad_blocked_cb, NULL);

  GST_DEBUG ("getting state");

  /* we should be SUCCESS now and PLAYING */
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");
  fail_unless (current == GST_STATE_PLAYING, "not PLAYING");
  fail_unless (pending == GST_STATE_VOID_PENDING, "have pending");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "not SUCCESS");

  g_cond_free (blocked_cond);
  g_mutex_free (blocked_lock);
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

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_element_get_pad (sink, "sink");
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

GST_END_TEST
/* test: try changing state of sinks */
    Suite * gst_sinks_suite (void)
{
  Suite *s = suite_create ("Sinks");
  TCase *tc_chain = tcase_create ("general");

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

  return s;
}

GST_CHECK_MAIN (gst_sinks);
