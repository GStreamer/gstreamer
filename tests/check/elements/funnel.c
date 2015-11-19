/* GStreamer unit tests for the funnel
 *
 * Copyright (C) 2008 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>

struct TestData
{
  GstElement *funnel;
  GstPad *funnelsrc, *funnelsink11, *funnelsink22;
  GstPad *mysink, *mysrc1, *mysrc2;
  GstCaps *mycaps;
};

static void
setup_test_objects (struct TestData *td, GstPadChainFunction chain_func)
{
  td->mycaps = gst_caps_new_empty_simple ("test/test");

  td->funnel = gst_element_factory_make ("funnel", NULL);

  td->funnelsrc = gst_element_get_static_pad (td->funnel, "src");
  fail_unless (td->funnelsrc != NULL);

  td->funnelsink11 = gst_element_get_request_pad (td->funnel, "sink_11");
  fail_unless (td->funnelsink11 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td->funnelsink11), "sink_11"));

  td->funnelsink22 = gst_element_get_request_pad (td->funnel, "sink_22");
  fail_unless (td->funnelsink22 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td->funnelsink22), "sink_22"));

  fail_unless (gst_element_set_state (td->funnel, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  td->mysink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (td->mysink, chain_func);
  gst_pad_set_active (td->mysink, TRUE);

  td->mysrc1 = gst_pad_new ("src1", GST_PAD_SRC);
  gst_pad_set_active (td->mysrc1, TRUE);
  gst_check_setup_events_with_stream_id (td->mysrc1, td->funnel, td->mycaps,
      GST_FORMAT_BYTES, "test1");

  td->mysrc2 = gst_pad_new ("src2", GST_PAD_SRC);
  gst_pad_set_active (td->mysrc2, TRUE);
  gst_check_setup_events_with_stream_id (td->mysrc2, td->funnel, td->mycaps,
      GST_FORMAT_BYTES, "test2");

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->funnelsrc,
              td->mysink)));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->mysrc1,
              td->funnelsink11)));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->mysrc2,
              td->funnelsink22)));

}

static void
release_test_objects (struct TestData *td)
{
  gst_pad_set_active (td->mysink, FALSE);
  gst_pad_set_active (td->mysrc1, FALSE);
  gst_pad_set_active (td->mysrc1, FALSE);

  gst_object_unref (td->mysink);
  gst_object_unref (td->mysrc1);
  gst_object_unref (td->mysrc2);

  fail_unless (gst_element_set_state (td->funnel, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (td->funnelsrc);
  gst_element_release_request_pad (td->funnel, td->funnelsink11);
  gst_object_unref (td->funnelsink11);
  gst_element_release_request_pad (td->funnel, td->funnelsink22);
  gst_object_unref (td->funnelsink22);

  gst_caps_unref (td->mycaps);
  gst_object_unref (td->funnel);
}

static gint bufcount = 0;
static gint alloccount = 0;

static GstFlowReturn
chain_ok (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  bufcount++;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

GST_START_TEST (test_funnel_simple)
{
  struct TestData td;

  setup_test_objects (&td, chain_ok);

  bufcount = 0;
  alloccount = 0;

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (bufcount == 2);

  release_test_objects (&td);
}

GST_END_TEST;

guint num_eos = 0;

static gboolean
eos_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    ++num_eos;

  return gst_pad_event_default (pad, parent, event);
}

GST_START_TEST (test_funnel_eos)
{
  struct TestData td;
  GstSegment segment;

  setup_test_objects (&td, chain_ok);

  num_eos = 0;
  bufcount = 0;

  gst_pad_set_event_function (td.mysink, eos_event_func);

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (bufcount == 2);

  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_eos ()));
  fail_unless (num_eos == 0);

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (bufcount == 3);

  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_eos ()));
  fail_unless (num_eos == 1);

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_EOS);

  fail_unless (bufcount == 3);

  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_flush_start ()));
  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_flush_stop (TRUE)));

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (td.mysrc1, gst_event_new_segment (&segment));
  gst_pad_push_event (td.mysrc2, gst_event_new_segment (&segment));

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_EOS);

  fail_unless (bufcount == 4);

  fail_unless (gst_pad_unlink (td.mysrc1, td.funnelsink11));
  gst_element_release_request_pad (td.funnel, td.funnelsink11);
  gst_object_unref (td.funnelsink11);
  fail_unless (num_eos == 2);

  td.funnelsink11 = gst_element_get_request_pad (td.funnel, "sink_11");
  fail_unless (td.funnelsink11 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td.funnelsink11), "sink_11"));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td.mysrc1,
              td.funnelsink11)));

  /* This will fail because everything is EOS already */
  fail_if (gst_pad_push_event (td.mysrc1, gst_event_new_eos ()));
  fail_unless (num_eos == 2);

  fail_unless (gst_pad_unlink (td.mysrc1, td.funnelsink11));
  gst_element_release_request_pad (td.funnel, td.funnelsink11);
  gst_object_unref (td.funnelsink11);
  fail_unless (num_eos == 2);

  /* send only eos to check, it handles empty streams */
  td.funnelsink11 = gst_element_get_request_pad (td.funnel, "sink_11");
  fail_unless (td.funnelsink11 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td.funnelsink11), "sink_11"));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td.mysrc1,
              td.funnelsink11)));

  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_flush_start ()));
  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_flush_stop (TRUE)));
  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_flush_start ()));
  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_flush_stop (TRUE)));

  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_eos ()));
  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_eos ()));
  fail_unless (num_eos == 3);

  fail_unless (gst_pad_unlink (td.mysrc1, td.funnelsink11));
  gst_element_release_request_pad (td.funnel, td.funnelsink11);
  gst_object_unref (td.funnelsink11);
  fail_unless (num_eos == 3);

  td.funnelsink11 = gst_element_get_request_pad (td.funnel, "sink_11");
  fail_unless (td.funnelsink11 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td.funnelsink11), "sink_11"));

  release_test_objects (&td);
}

GST_END_TEST;

guint nb_stream_start_event = 0;
guint nb_caps_event = 0;
guint nb_segment_event = 0;
guint nb_gap_event = 0;

static GstPadProbeReturn
event_counter (GstObject * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  fail_unless (event != NULL);
  fail_unless (GST_IS_EVENT (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      ++nb_stream_start_event;
      break;
    case GST_EVENT_CAPS:
      ++nb_caps_event;
      break;
    case GST_EVENT_SEGMENT:
      ++nb_segment_event;
      break;
    case GST_EVENT_GAP:
      ++nb_gap_event;
      break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

/*
 * Push GAP events into funnel to forward sticky events.
 * Funnel element shoud also treat GAP events likes buffers.
 * For example, funnel can be used for internal subtitle with streamiddemux. 
 *  +--------------------------------------------------------------------------+
 *  | playbin                               +--------------------------------+ |
 *  | +--------------+  +----------------+  | +------------+     playsink    | |
 *  | | uridecodebin |  | input-selector |  | | video-sink |                 | |
 *  | |              |  +----------------+  | +------------+                 | |
 *  | |              |                      |                                | |
 *  | |              |  +----------------+  | +------------+                 | |
 *  | |              |  | input-selector |  | | audio-sink |                 | |
 *  | |              |  +----------------+  | +------------+                 | |
 *  | |              |                      |                                | |
 *  | |              |  +----------------+  | +---------------+ +----------+ | |
 *  | |              |  | funnel         |  | | streamiddemux | | appsink0 | | |
 *  | +--------------+  +----------------+  | +---------------+ +----------+ | |
 *  |                                       |                   +----------+ | |
 *  |                                       |                   | appsinkn | | |
 *  |                                       |                   +----------+ | |
 *  |                                       +--------------------------------+ |
 *  +--------------------------------------------------------------------------+
 * If no data was received in funnel and then sticky events can be pending continuously. 
 * And streamiddemux only receive gap events continuously. 
 * Thus, pipeline can not be constructed completely.
 * For support it, need to handle GAP events likes buffers.
 */
GST_START_TEST (test_funnel_gap_event)
{
  struct TestData td;
  guint probe = 0;

  setup_test_objects (&td, chain_ok);

  nb_stream_start_event = 0;
  nb_caps_event = 0;
  nb_segment_event = 0;
  nb_gap_event = 0;
  bufcount = 0;

  probe = gst_pad_add_probe (td.mysink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) event_counter, NULL, NULL);

  /* push a gap event to srcpad1 to push sticky events */
  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_gap (0,
              GST_SECOND)));

  fail_unless (nb_stream_start_event == 1);
  fail_unless (nb_caps_event == 1);
  fail_unless (nb_segment_event == 1);
  fail_unless (nb_gap_event == 1);

  /* push a gap event to srcpad2 to push sticky events */
  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_gap (0,
              GST_SECOND)));

  fail_unless (nb_stream_start_event == 2);
  fail_unless (nb_caps_event == 2);
  fail_unless (nb_segment_event == 2);
  fail_unless (nb_gap_event == 2);

  /* push a gap event to srcpad2 */
  fail_unless (gst_pad_push_event (td.mysrc2, gst_event_new_gap (0,
              GST_SECOND)));

  fail_unless (nb_stream_start_event == 2);
  fail_unless (nb_caps_event == 2);
  fail_unless (nb_segment_event == 2);
  fail_unless (nb_gap_event == 3);

  /* push a gap event to srcpad1 */
  fail_unless (gst_pad_push_event (td.mysrc1, gst_event_new_gap (0,
              GST_SECOND)));

  fail_unless (nb_stream_start_event == 3);
  fail_unless (nb_caps_event == 3);
  fail_unless (nb_segment_event == 3);
  fail_unless (nb_gap_event == 4);

  /* push buffer */
  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (nb_stream_start_event == 4);
  fail_unless (nb_caps_event == 4);
  fail_unless (nb_segment_event == 4);
  fail_unless (nb_gap_event == 4);
  fail_unless (bufcount == 2);

  gst_pad_remove_probe (td.mysink, probe);

  release_test_objects (&td);
}

GST_END_TEST;

GST_START_TEST (test_funnel_stress)
{
  GstHarness *h0 = gst_harness_new_with_padnames ("funnel", "sink_0", "src");
  GstHarness *h1 = gst_harness_new_with_element (h0->element, "sink_1", NULL);
  GstHarnessThread *req, *push0, *push1;
  GstPadTemplate *templ =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (h0->element),
      "sink_%u");
  GstCaps *caps = gst_caps_from_string ("testcaps");
  GstBuffer *buf = gst_buffer_new ();
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_TIME);

  req = gst_harness_stress_requestpad_start (h0, templ, NULL, NULL, TRUE);
  push0 = gst_harness_stress_push_buffer_start (h0, caps, &segment, buf);
  push1 = gst_harness_stress_push_buffer_start (h1, caps, &segment, buf);

  gst_caps_unref (caps);
  gst_buffer_unref (buf);

  /* test-length */
  g_usleep (G_USEC_PER_SEC * 1);

  gst_harness_stress_thread_stop (push1);
  gst_harness_stress_thread_stop (push0);
  gst_harness_stress_thread_stop (req);

  gst_harness_teardown (h1);
  gst_harness_teardown (h0);
}

GST_END_TEST;


static Suite *
funnel_suite (void)
{
  Suite *s = suite_create ("funnel");
  TCase *tc_chain;

  tc_chain = tcase_create ("funnel simple");
  tcase_add_test (tc_chain, test_funnel_simple);
  tcase_add_test (tc_chain, test_funnel_eos);
  tcase_add_test (tc_chain, test_funnel_gap_event);
  tcase_add_test (tc_chain, test_funnel_stress);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (funnel);
