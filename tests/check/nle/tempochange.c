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

#include "common.h"
#include "plugins/nle/nleobject.h"

GST_START_TEST (test_tempochange)
{
  GstElement *pipeline;
  GstElement *comp, *source1, *def, *sink, *oper;
  GList *segments = NULL;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on, ret = FALSE;
  CollectStructure *collect;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");

  gst_element_set_state (comp, GST_STATE_READY);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  gst_element_link (comp, sink);

  /*
     source1
     Start : 0s
     Duration : 2s
     Priority : 2
   */

  source1 = audiotest_bin_src ("source1", 0, 2 * GST_SECOND, 2, 2);

  /*
     def (default source)
     Priority = G_MAXUINT32
   */
  def =
      audiotest_bin_src ("default", 0 * GST_SECOND, 0 * GST_SECOND, G_MAXUINT32,
      1);
  g_object_set (def, "expandable", TRUE, NULL);

  /* Operation */
  oper = new_operation ("oper", "identity", 0, 2 * GST_SECOND, 1);
  fail_if (oper == NULL);
  ((NleObject *) oper)->media_duration_factor = 2.0;

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (def, "default", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Add source 1 */

  nle_composition_add (GST_BIN (comp), source1);
  nle_composition_add (GST_BIN (comp), def);
  nle_composition_add (GST_BIN (comp), oper);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (oper, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Define expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0 * GST_SECOND, 4.0 * GST_SECOND, 0));
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  collect->expected_segments = segments;
  collect->keep_expected_segments = FALSE;

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PAUSED");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  carry_on = TRUE;
  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ASYNC_DONE:
        {
          carry_on = FALSE;
          GST_DEBUG ("Pipeline reached PAUSED, stopping polling");
          break;
        }
        case GST_MESSAGE_EOS:
        {
          GST_WARNING ("Saw EOS");

          fail_if (TRUE);
        }
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  fail_if (((NleObject *) source1)->media_duration_factor != 1.0f);
  fail_if (((NleObject *) source1)->recursive_media_duration_factor != 2.0f);
  fail_if (((NleObject *) oper)->media_duration_factor != 2.0f);
  fail_if (((NleObject *) oper)->recursive_media_duration_factor != 2.0f);

  GST_DEBUG ("Setting pipeline to READY");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  g_free (collect);
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("nle");
  TCase *tc_chain = tcase_create ("tempochange");

  ges_init ();
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_tempochange);

  return s;
}

GST_CHECK_MAIN (gnonlin)
