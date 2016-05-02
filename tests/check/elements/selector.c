/* GStreamer
 *
 * Unit test for selector plugin
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
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

#include <gst/check/gstcheck.h>

#define NUM_SELECTOR_PADS 4
#define NUM_INPUT_BUFFERS 4     // buffers to send per each selector pad

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* Data probe cb to drop everything but count buffers and events */
static GstPadProbeReturn
probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gint count = 0;
  const gchar *count_type = NULL;
  GstMiniObject *obj = GST_PAD_PROBE_INFO_DATA (info);

  GST_LOG_OBJECT (pad, "got data");

  if (GST_IS_BUFFER (obj)) {
    count_type = "buffer_count";
  } else if (GST_IS_EVENT (obj)) {
    count_type = "event_count";
  } else {
    g_assert_not_reached ();
  }

  /* increment and store count */
  count = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pad), count_type));
  count++;
  g_object_set_data (G_OBJECT (pad), count_type, GINT_TO_POINTER (count));

  /* drop every buffer */
  return GST_IS_BUFFER (obj) ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_PASS;
}

/* Create and link output pad: selector:src%d ! output_pad */
static GstPad *
setup_output_pad (GstElement * element, GstStaticPadTemplate * tmpl)
{
  GstPad *srcpad = NULL, *output_pad = NULL;
  gulong probe_id = 0;

  if (tmpl == NULL)
    tmpl = &sinktemplate;

  /* create output_pad */
  output_pad = gst_pad_new_from_static_template (tmpl, "sink");
  fail_if (output_pad == NULL, "Could not create a output_pad");

  /* add probe */
  probe_id =
      gst_pad_add_probe (output_pad, GST_PAD_PROBE_TYPE_DATA_BOTH,
      (GstPadProbeCallback) probe_cb, NULL, NULL);
  g_object_set_data (G_OBJECT (output_pad), "probe_id",
      GINT_TO_POINTER (probe_id));

  /* request src pad */
  srcpad = gst_element_get_request_pad (element, "src_%u");
  fail_if (srcpad == NULL, "Could not get source pad from %s",
      GST_ELEMENT_NAME (element));

  /* link pads and activate */
  fail_unless (gst_pad_link (srcpad, output_pad) == GST_PAD_LINK_OK,
      "Could not link %s source and output pad", GST_ELEMENT_NAME (element));

  gst_pad_set_active (output_pad, TRUE);

  GST_DEBUG_OBJECT (output_pad, "set up %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT,
      srcpad, output_pad);

  gst_object_unref (srcpad);
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  return output_pad;
}

/* Clean up output/input pad and respective selector request pad */
static void
cleanup_pad (GstPad * pad, GstElement * element)
{
  GstPad *selpad = NULL;
  guint probe_id = 0;

  fail_if (pad == NULL, "pad doesn't exist");

  /* remove probe if necessary */
  probe_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pad), "probe_id"));
  if (probe_id)
    gst_pad_remove_probe (pad, probe_id);

  /* unlink */
  selpad = gst_pad_get_peer (pad);
  if (GST_PAD_DIRECTION (selpad) == GST_PAD_SRC) {
    gst_pad_unlink (selpad, pad);
  } else {
    gst_pad_unlink (pad, selpad);
  }

  GST_DEBUG_OBJECT (pad, "clean up %" GST_PTR_FORMAT " and  %" GST_PTR_FORMAT,
      selpad, pad);

  /* cleanup the pad */
  gst_pad_set_active (pad, FALSE);
  ASSERT_OBJECT_REFCOUNT (pad, "pad", 1);
  gst_object_unref (pad);

  /* cleanup selector pad, reffed by this function (_get_peer) and creator */
  gst_element_release_request_pad (element, selpad);
  gst_object_unref (selpad);
}

/* Duplicate and push given buffer many times to all input_pads */
static void
push_input_buffers (GList * input_pads, GstBuffer * buf, gint num_buffers)
{
  GstBuffer *buf_in = NULL;
  GList *l = input_pads;
  GstPad *input_pad;
  gint i = 0;

  while (l != NULL) {
    input_pad = l->data;
    GST_DEBUG_OBJECT (input_pad, "pushing %d buffers to %" GST_PTR_FORMAT,
        num_buffers, input_pad);
    for (i = 0; i < num_buffers; i++) {
      buf_in = gst_buffer_copy (buf);
      fail_unless (gst_pad_push (input_pad, buf_in) == GST_FLOW_OK,
          "pushing buffer failed");
    }
    l = g_list_next (l);
  }
}

/* Check that received buffers count match to expected buffers */
static void
count_output_buffers (GList * output_pads, gint expected_buffers)
{
  gint count = 0;
  GList *l = output_pads;
  GstPad *output_pad = NULL;

  while (l != NULL) {
    output_pad = l->data;
    count =
        GPOINTER_TO_INT (g_object_get_data (G_OBJECT (output_pad),
            "buffer_count"));
    GST_DEBUG_OBJECT (output_pad, "received %d buffers", count);
    fail_unless (count == expected_buffers,
        "received/expected buffer count doesn't match %d/%d", count,
        expected_buffers);
    count =
        GPOINTER_TO_INT (g_object_get_data (G_OBJECT (output_pad),
            "event_count"));
    GST_DEBUG_OBJECT (output_pad, "received %d events", count);
    l = g_list_next (l);
  }
}

/* Set selector active pad */
static void
selector_set_active_pad (GstElement * elem, GstPad * selpad)
{
  gchar *padname = NULL;

  if (selpad) {
    padname = gst_pad_get_name (selpad);
  }

  g_object_set (G_OBJECT (elem), "active-pad", selpad, NULL);
  GST_DEBUG_OBJECT (elem, "activated selector pad: %s", GST_STR_NULL (padname));
  g_free (padname);
}

static void
push_newsegment_events (GList * input_pads)
{
  GstSegment seg;
  GList *l;

  seg.flags = GST_SEGMENT_FLAG_NONE;
  seg.rate = seg.applied_rate = 1.0;
  seg.format = GST_FORMAT_BYTES;
  seg.base = 0;
  seg.start = 0;
  seg.stop = -1;
  seg.time = 0;
  seg.position = 0;
  seg.duration = -1;

  for (l = input_pads; l; l = l->next) {
    GstPad *pad = l->data;

    gst_pad_push_event (pad, gst_event_new_stream_start ("test"));
    gst_pad_push_event (pad, gst_event_new_segment (&seg));
  }
}

/* Push buffers and switch for each selector pad */
static void
push_switched_buffers (GList * input_pads,
    GstElement * elem, GList * peer_pads, gint num_buffers)
{
  GstBuffer *buf = NULL;
  GList *l = peer_pads;
  GstPad *selpad = NULL;

  /* setup dummy buffer */
  buf = gst_buffer_new_and_alloc (1);

  while (l != NULL) {
    /* set selector pad */
    selpad = gst_pad_get_peer (GST_PAD (l->data));
    selector_set_active_pad (elem, selpad);
    if (selpad) {
      gst_object_unref (selpad);
    }
    /* push buffers */
    push_input_buffers (input_pads, buf, num_buffers);
    /* switch to next selector pad */
    l = g_list_next (l);
  }

  /* cleanup buffer */
  gst_buffer_unref (buf);
}

/* Create output-selector with given number of src pads and switch
   given number of input buffers to each src pad.
 */
static void
run_output_selector_buffer_count (gint num_output_pads,
    gint num_buffers_per_output)
{
  /* setup input_pad ! selector ! output_pads */
  gint i = 0;
  GList *output_pads = NULL, *input_pads = NULL;
  GstElement *sel = gst_check_setup_element ("output-selector");
  GstPad *input_pad = gst_check_setup_src_pad (sel, &srctemplate);

  input_pads = g_list_append (input_pads, input_pad);
  gst_pad_set_active (input_pad, TRUE);
  for (i = 0; i < num_output_pads; i++) {
    output_pads = g_list_append (output_pads, setup_output_pad (sel, NULL));
  }

  /* run the test */
  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  push_newsegment_events (input_pads);
  push_switched_buffers (input_pads, sel, output_pads, num_buffers_per_output);
  count_output_buffers (output_pads, num_buffers_per_output);
  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup input_pad, selector and output_pads */
  gst_pad_set_active (input_pad, FALSE);
  gst_check_teardown_src_pad (sel);
  g_list_foreach (output_pads, (GFunc) cleanup_pad, sel);
  g_list_free (output_pads);
  g_list_free (input_pads);
  gst_check_teardown_element (sel);
}

/* Create and link input pad: input_pad ! selector:sink%d */
static GstPad *
setup_input_pad (GstElement * element)
{
  GstPad *sinkpad = NULL, *input_pad = NULL;

  /* create input_pad */
  input_pad = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_if (input_pad == NULL, "Could not create a input_pad");

  /* request sink pad */
  sinkpad = gst_element_get_request_pad (element, "sink_%u");
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));

  /* link pads and activate */
  fail_unless (gst_pad_link (input_pad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link input_pad and %s sink", GST_ELEMENT_NAME (element));

  gst_pad_set_active (input_pad, TRUE);

  GST_DEBUG_OBJECT (input_pad, "set up %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT,
      input_pad, sinkpad);

  gst_object_unref (sinkpad);
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  return input_pad;
}

/* Create input-selector with given number of sink pads and switch
   given number of input buffers to each sink pad.
 */
static void
run_input_selector_buffer_count (gint num_input_pads,
    gint num_buffers_per_input)
{
  /* set up input_pads ! selector ! output_pad */
  gint i = 0, probe_id = 0;
  GList *input_pads = NULL, *output_pads = NULL;
  GstElement *sel = gst_check_setup_element ("input-selector");
  GstPad *output_pad = gst_check_setup_sink_pad (sel, &sinktemplate);

  output_pads = g_list_append (output_pads, output_pad);
  gst_pad_set_active (output_pad, TRUE);
  for (i = 0; i < num_input_pads; i++) {
    input_pads = g_list_append (input_pads, setup_input_pad (sel));
  }
  /* add probe */
  probe_id =
      gst_pad_add_probe (output_pad, GST_PAD_PROBE_TYPE_DATA_BOTH,
      (GstPadProbeCallback) probe_cb, NULL, NULL);
  g_object_set_data (G_OBJECT (output_pad), "probe_id",
      GINT_TO_POINTER (probe_id));

  /* run the test */
  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  push_newsegment_events (input_pads);
  push_switched_buffers (input_pads, sel, input_pads, num_buffers_per_input);
  count_output_buffers (output_pads, (num_input_pads * num_buffers_per_input));
  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* clean up */
  gst_pad_remove_probe (output_pad, probe_id);
  gst_pad_set_active (output_pad, FALSE);
  gst_check_teardown_sink_pad (sel);
  GST_DEBUG ("setting selector pad to NULL");
  selector_set_active_pad (sel, NULL);  // unref input-selector active pad
  g_list_foreach (input_pads, (GFunc) cleanup_pad, sel);
  g_list_free (input_pads);
  g_list_free (output_pads);
  gst_check_teardown_element (sel);
}

/* Push buffers to input pad and check the 
   amount of buffers arrived to output pads */
GST_START_TEST (test_output_selector_buffer_count)
{
  gint i, j;

  for (i = 0; i < NUM_SELECTOR_PADS; i++) {
    for (j = 0; j < NUM_INPUT_BUFFERS; j++) {
      run_output_selector_buffer_count (i, j);
    }
  }
}

GST_END_TEST;

/* Push buffers to input pads and check the 
   amount of buffers arrived to output pad */
GST_START_TEST (test_input_selector_buffer_count)
{
  gint i, j;

  for (i = 0; i < NUM_SELECTOR_PADS; i++) {
    for (j = 0; j < NUM_INPUT_BUFFERS; j++) {
      run_input_selector_buffer_count (i, j);
    }
  }
}

GST_END_TEST;

static GstElement *selector;
static GstPad *output_pad;
static GstPad *stream1_pad;
static GstPad *stream2_pad;

static gboolean eos_received;
static gulong eos_probe;
static GMutex eos_probe_lock;
static GCond eos_probe_cond;

enum InputSelectorResult
{
  INPUT_SELECTOR_FORWARD,
  INPUT_SELECTOR_DROP
};

static GstPadProbeReturn
eos_pushed_probe (GstPad * pad, GstPadProbeInfo * info, gpointer udata)
{
  g_mutex_lock (&eos_probe_lock);
  if (GST_EVENT_TYPE (info->data) == GST_EVENT_EOS) {
    eos_received = TRUE;
    g_cond_broadcast (&eos_probe_cond);
  }
  g_mutex_unlock (&eos_probe_lock);

  return GST_PAD_PROBE_OK;
}

static void
setup_input_selector_with_2_streams (gint active_stream)
{
  eos_received = FALSE;
  g_mutex_init (&eos_probe_lock);
  g_cond_init (&eos_probe_cond);

  selector = gst_check_setup_element ("input-selector");
  output_pad = gst_check_setup_sink_pad (selector, &sinktemplate);

  gst_pad_set_active (output_pad, TRUE);
  stream1_pad = setup_input_pad (selector);
  stream2_pad = setup_input_pad (selector);

  if (active_stream == 1) {
    g_object_set (selector, "active-pad", GST_PAD_PEER (stream1_pad), NULL);
  } else {
    g_object_set (selector, "active-pad", GST_PAD_PEER (stream2_pad), NULL);
  }

  eos_probe =
      gst_pad_add_probe (output_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      eos_pushed_probe, NULL, NULL);

  fail_unless (gst_element_set_state (selector,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_check_setup_events_with_stream_id (stream1_pad, selector, NULL,
      GST_FORMAT_TIME, "stream-1-id");
  gst_check_setup_events_with_stream_id (stream2_pad, selector, NULL,
      GST_FORMAT_TIME, "stream-2-id");
}

static void
teardown_input_selector_with_2_streams (void)
{
  fail_unless (gst_element_set_state (selector,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  gst_pad_remove_probe (output_pad, eos_probe);

  gst_pad_set_active (output_pad, FALSE);
  gst_check_teardown_sink_pad (selector);
  gst_check_teardown_element (selector);
  gst_object_unref (stream1_pad);
  gst_object_unref (stream2_pad);

  g_mutex_clear (&eos_probe_lock);
  g_cond_clear (&eos_probe_cond);
}

static void
input_selector_push_buffer (gint stream, enum InputSelectorResult res)
{
  GstBuffer *buf;
  GstPad *pad = stream == 1 ? stream1_pad : stream2_pad;

  buf = gst_buffer_new ();
  fail_unless (buffers == NULL);
  fail_unless (gst_pad_push (pad, buf) == GST_FLOW_OK);

  if (res == INPUT_SELECTOR_DROP) {
    fail_unless (buffers == NULL);
  } else {
    fail_unless (buffers != NULL);
    fail_unless (buffers->data == buf);
    g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
    buffers = NULL;
  }
}

static gpointer
input_selector_do_push_eos (GstPad * pad)
{
  gst_pad_push_event (pad, gst_event_new_eos ());
  return NULL;
}

static void
input_selector_check_eos (gint present)
{
  GstEvent *eos;

  eos = gst_pad_get_sticky_event (output_pad, GST_EVENT_EOS, 0);
  if (present) {
    fail_unless (eos != NULL);
    gst_event_unref (eos);
  } else {
    fail_unless (eos == NULL);
  }
}

static void
input_selector_push_eos (gint stream, gboolean active)
{
  GstPad *pad = stream == 1 ? stream1_pad : stream2_pad;

  if (active) {
    fail_unless (gst_pad_push_event (pad, gst_event_new_eos ()));
  } else {
    /* The non-active pads will block when receving eos, so we need to do it
     * from a separate thread. This makes this test racy, but it should only
     * cause false positives, not false negatives */
    GThread *t = g_thread_new ("selector-test-push-eos",
        (GThreadFunc) input_selector_do_push_eos, pad);

    /* Sleep half a second to allow the other thread to execute, this is not
     * a definitive solution but there is no way to know when the
     * EOS has reached input-selector and blocked there, so this is just
     * to reduce the possibility of this test being racy (false positives)
     */
    g_usleep (0.5 * G_USEC_PER_SEC);
    g_thread_unref (t);
  }

  input_selector_check_eos (active);
}

GST_START_TEST (test_input_selector_empty_stream)
{
  setup_input_selector_with_2_streams (2);

  /* stream1 is the empty stream, stream2 has data */

  /* empty stream is just an EOS and it should not be forwarded */
  input_selector_push_eos (1, FALSE);

  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_eos (2, TRUE);

  teardown_input_selector_with_2_streams ();
}

GST_END_TEST;


GST_START_TEST (test_input_selector_shorter_stream)
{
  setup_input_selector_with_2_streams (2);

  /* stream1 is shorter than stream2 */

  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (1, INPUT_SELECTOR_DROP);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);

  /* EOS from inactive stream should not go through */
  input_selector_push_eos (1, FALSE);

  /* buffers from active stream can still flow */
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);

  /* EOS from active stream should go through */
  input_selector_push_eos (2, TRUE);

  teardown_input_selector_with_2_streams ();
}

GST_END_TEST;


GST_START_TEST (test_input_selector_switch_to_eos_stream)
{
  setup_input_selector_with_2_streams (2);

  /* stream1 receives eos before stream2 and then we switch to it */

  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (1, INPUT_SELECTOR_DROP);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (1, INPUT_SELECTOR_DROP);

  /* EOS from inactive stream should not go through */
  input_selector_push_eos (1, FALSE);

  /* buffers from active stream can still flow */
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);
  input_selector_push_buffer (2, INPUT_SELECTOR_FORWARD);

  /* now switch to stream1 */
  g_object_set (selector, "active-pad", GST_PAD_PEER (stream1_pad), NULL);

  /* wait for eos (it runs from a separate thread) */
  g_mutex_lock (&eos_probe_lock);
  while (!eos_received) {
    g_cond_wait (&eos_probe_cond, &eos_probe_lock);
  }
  g_mutex_unlock (&eos_probe_lock);

  teardown_input_selector_with_2_streams ();
}

GST_END_TEST;


GST_START_TEST (test_output_selector_no_srcpad_negotiation)
{
  GstElement *sel;
  GstCaps *caps;
  GstPad *pad;
  gint i;

  sel = gst_element_factory_make ("output-selector", NULL);
  fail_unless (sel != NULL);

  pad = gst_element_get_static_pad (sel, "sink");
  fail_unless (pad != NULL);

  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (i = 0; i <= 2; i++) {

    /* regardless of pad-negotiation-mode, getcaps should return ANY and
     * setcaps should accept any caps when there are no srcpads */
    g_object_set (sel, "pad-negotiation-mode", i, NULL);

    caps = gst_pad_query_caps (pad, NULL);
    fail_unless (gst_caps_is_any (caps));

    gst_caps_unref (caps);

    caps = gst_caps_new_empty_simple ("mymedia/mycaps");
    fail_unless (gst_pad_set_caps (pad, caps));
    gst_caps_unref (caps);
  }

  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  gst_object_unref (pad);
  gst_object_unref (sel);
}

GST_END_TEST;


GstElement *sel;
GstPad *input_pad;
GList *output_pads = NULL;      /* list of sinkpads linked to output-selector */
#define OUTPUT_SELECTOR_NUM_PADS 2

static GstStaticPadTemplate sinktmpl_nego_a = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("format/abc; format/xyz"));
static GstStaticPadTemplate sinktmpl_nego_b = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("format/abc"));

static void
setup_output_selector (void)
{
  sel = gst_check_setup_element ("output-selector");
  input_pad = gst_check_setup_src_pad (sel, &srctemplate);
  gst_pad_set_active (input_pad, TRUE);

  output_pads = g_list_append (output_pads, setup_output_pad (sel,
          &sinktmpl_nego_a));
  output_pads = g_list_append (output_pads, setup_output_pad (sel,
          &sinktmpl_nego_b));
}

static void
teardown_output_selector (void)
{
  gst_pad_set_active (input_pad, FALSE);
  gst_object_unref (input_pad);
  gst_check_teardown_src_pad (sel);
  g_list_foreach (output_pads, (GFunc) cleanup_pad, sel);
  g_list_free (output_pads);
  gst_check_teardown_element (sel);
  output_pads = NULL;
}

GST_START_TEST (test_output_selector_getcaps_none)
{
  GList *walker;

  /* set pad negotiation mode to none */
  g_object_set (sel, "pad-negotiation-mode", 0, NULL);

  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (walker = output_pads; walker; walker = g_list_next (walker)) {
    GstCaps *caps;
    GstPad *pad;

    pad = gst_pad_get_peer ((GstPad *) walker->data);

    g_object_set (sel, "active-pad", pad, NULL);

    caps = gst_pad_peer_query_caps (input_pad, NULL);

    /* in 'none' mode, the getcaps returns the template, which is ANY */
    g_assert (gst_caps_is_any (caps));
    gst_caps_unref (caps);
    gst_object_unref (pad);
  }

  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

}

GST_END_TEST;


GST_START_TEST (test_output_selector_getcaps_all)
{
  GList *walker;
  GstCaps *expected;

  /* set pad negotiation mode to 'all' */
  g_object_set (sel, "pad-negotiation-mode", 1, NULL);

  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* in 'all' mode, the intersection of the srcpad caps should be returned on
   * the sinkpad's getcaps */
  expected = gst_caps_new_empty_simple ("format/abc");

  for (walker = output_pads; walker; walker = g_list_next (walker)) {
    GstCaps *caps;
    GstPad *pad;

    pad = gst_pad_get_peer ((GstPad *) walker->data);

    g_object_set (sel, "active-pad", pad, NULL);

    caps = gst_pad_peer_query_caps (input_pad, NULL);

    g_assert (gst_caps_is_equal (caps, expected));
    gst_caps_unref (caps);
    gst_object_unref (pad);
  }
  gst_caps_unref (expected);

  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

}

GST_END_TEST;


GST_START_TEST (test_output_selector_getcaps_active)
{
  GList *walker;
  GstCaps *expected;

  /* set pad negotiation mode to 'active' */
  g_object_set (sel, "pad-negotiation-mode", 2, NULL);

  fail_unless (gst_element_set_state (sel,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (walker = output_pads; walker; walker = g_list_next (walker)) {
    GstCaps *caps;
    GstPad *pad;
    GstPadTemplate *templ;

    pad = gst_pad_get_peer ((GstPad *) walker->data);

    g_object_set (sel, "active-pad", pad, NULL);

    /* in 'active' mode, the active srcpad peer's caps should be returned on
     * the sinkpad's getcaps */

    templ = gst_pad_get_pad_template ((GstPad *) walker->data);
    expected = gst_pad_template_get_caps (templ);
    caps = gst_pad_peer_query_caps (input_pad, NULL);

    g_assert (gst_caps_is_equal (caps, expected));
    gst_caps_unref (caps);
    gst_caps_unref (expected);
    gst_object_unref (templ);
    gst_object_unref (pad);
  }

  fail_unless (gst_element_set_state (sel,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

}

GST_END_TEST;


static Suite *
selector_suite (void)
{
  Suite *s = suite_create ("selector");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_output_selector_buffer_count);
  tcase_add_test (tc_chain, test_input_selector_buffer_count);
  tcase_add_test (tc_chain, test_input_selector_empty_stream);
  tcase_add_test (tc_chain, test_input_selector_shorter_stream);
  tcase_add_test (tc_chain, test_input_selector_switch_to_eos_stream);
  tcase_add_test (tc_chain, test_output_selector_no_srcpad_negotiation);

  tc_chain = tcase_create ("output-selector-negotiation");
  tcase_add_checked_fixture (tc_chain, setup_output_selector,
      teardown_output_selector);
  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_output_selector_getcaps_none);
  tcase_add_test (tc_chain, test_output_selector_getcaps_all);
  tcase_add_test (tc_chain, test_output_selector_getcaps_active);

  return s;
}

GST_CHECK_MAIN (selector);
