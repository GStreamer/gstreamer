/*
 * GStreamer
 *
 * unit test for amrparse
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
#include "amrparse_data.h"

#define SRC_CAPS_NB  "audio/x-amr-nb-sh"
#define SRC_CAPS_WB  "audio/x-amr-wb-sh"
#define SRC_CAPS_ANY "ANY"

#define SINK_CAPS_NB  "audio/AMR, rate=8000 , channels=1"
#define SINK_CAPS_WB  "audio/AMR-WB, rate=16000 , channels=1"
#define SINK_CAPS_ANY "ANY"

#define AMR_FRAME_DURATION (GST_SECOND/50)

GList *buffers;
GList *current_buf = NULL;

GstPad *srcpad, *sinkpad;
guint dataoffset = 0;
GstClockTime ts_counter = 0;
gint64 offset_counter = 0;
guint buffer_counter = 0;

static GstStaticPadTemplate sinktemplate_nb = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_NB)
    );

static GstStaticPadTemplate sinktemplate_wb = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_WB)
    );

static GstStaticPadTemplate sinktemplate_any = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_ANY)
    );

static GstStaticPadTemplate srctemplate_nb = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_NB)
    );

static GstStaticPadTemplate srctemplate_wb = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_WB)
    );

static GstStaticPadTemplate srctemplate_any = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_ANY)
    );

typedef struct
{
  guint buffers_before_offset_skip;
  guint offset_skip_amount;
} buffer_verify_data_s;

/*
 * Create a GstBuffer of the given data and set the caps, if not NULL.
 */
static GstBuffer *
buffer_new (const unsigned char *buffer_data, guint size,
    const gchar * caps_str)
{
  GstBuffer *buffer;
  GstCaps *caps;

  buffer = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buffer), buffer_data, size);
  if (caps_str) {
    caps = gst_caps_from_string (caps_str);
    gst_buffer_set_caps (buffer, caps);
    gst_caps_unref (caps);
  }
  GST_BUFFER_OFFSET (buffer) = dataoffset;
  dataoffset += size;
  return buffer;
}


/*
 * Unrefs given buffer.
 */
static void
buffer_unref (void *buffer, void *user_data)
{
  gst_buffer_unref (GST_BUFFER (buffer));
}


/*
 * Verify that given buffer contains predefined AMR-NB frame.
 */
static void
buffer_verify_nb (void *buffer, void *user_data)
{
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), frame_data_nb,
          FRAME_DATA_NB_LEN) == 0);
  fail_unless (GST_BUFFER_TIMESTAMP (buffer) == ts_counter);
  fail_unless (GST_BUFFER_DURATION (buffer) == AMR_FRAME_DURATION);
  ts_counter += AMR_FRAME_DURATION;

  if (user_data) {
    buffer_verify_data_s *vdata = (buffer_verify_data_s *) user_data;

    /* This is for skipping the garbage in some test cases */
    if (buffer_counter == vdata->buffers_before_offset_skip) {
      offset_counter += vdata->offset_skip_amount;
    }
  }
  fail_unless (GST_BUFFER_OFFSET (buffer) == offset_counter);
  offset_counter += FRAME_DATA_NB_LEN;
  buffer_counter++;
}


/*
 * Verify that given buffer contains predefined AMR-WB frame.
 */
static void
buffer_verify_wb (void *buffer, void *user_data)
{
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), frame_data_wb,
          FRAME_DATA_WB_LEN) == 0);
  fail_unless (GST_BUFFER_TIMESTAMP (buffer) == ts_counter);
  fail_unless (GST_BUFFER_DURATION (buffer) == AMR_FRAME_DURATION);

  if (user_data) {
    buffer_verify_data_s *vdata = (buffer_verify_data_s *) user_data;

    /* This is for skipping the garbage in some test cases */
    if (buffer_counter == vdata->buffers_before_offset_skip) {
      offset_counter += vdata->offset_skip_amount;
    }
  }
  fail_unless (GST_BUFFER_OFFSET (buffer) == offset_counter);
  offset_counter += FRAME_DATA_WB_LEN;
  ts_counter += AMR_FRAME_DURATION;
  buffer_counter++;
}

/*
 * Create a parser and pads according to given templates.
 */
static GstElement *
setup_amrparse (GstStaticPadTemplate * srctemplate,
    GstStaticPadTemplate * sinktemplate)
{
  GstElement *amrparse;
  GstBus *bus;

  GST_DEBUG ("setup_amrparse");
  amrparse = gst_check_setup_element ("amrparse");
  srcpad = gst_check_setup_src_pad (amrparse, srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (amrparse, sinktemplate, NULL);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (amrparse, bus);

  fail_unless (gst_element_set_state (amrparse,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  ts_counter = offset_counter = buffer_counter = 0;
  buffers = NULL;
  return amrparse;
}


/*
 * Delete parser and all related resources.
 */
static void
cleanup_amrparse (GstElement * amrparse)
{
  GstBus *bus;

  /* free parsed buffers */
  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  bus = GST_ELEMENT_BUS (amrparse);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("cleanup_amrparse");
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (amrparse);
  gst_check_teardown_sink_pad (amrparse);
  gst_check_teardown_element (amrparse);
  srcpad = NULL;
  sinkpad = NULL;
}


/*
 * Test if NB parser manages to find all frames and pushes them forward.
 */
GST_START_TEST (test_parse_nb_normal)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_nb, &sinktemplate_nb);

  /* Push the header */
  buffer = buffer_new (frame_hdr_nb, FRAME_HDR_NB_LEN, SRC_CAPS_NB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  offset_counter = FRAME_HDR_NB_LEN;

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, SRC_CAPS_NB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_nb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if NB parser drains its buffers properly. Even one single buffer
 * should be drained and pushed forward when EOS occurs. This single buffer
 * case is special, since normally the parser needs more data to be sure
 * about stream format. But it should still push the frame forward in EOS.
 */
GST_START_TEST (test_parse_nb_drain_single)
{
  GstElement *amrparse;
  GstBuffer *buffer;

  amrparse = setup_amrparse (&srctemplate_nb, &sinktemplate_nb);

  buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, SRC_CAPS_NB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_foreach (buffers, buffer_verify_nb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Make sure that parser does not drain garbage when EOS occurs.
 */
GST_START_TEST (test_parse_nb_drain_garbage)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_nb, &sinktemplate_nb);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, SRC_CAPS_NB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* Now push one garbage frame and then EOS */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN, SRC_CAPS_NB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* parser should have pushed only the valid frames */
  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_nb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if NB parser splits a buffer that contains two frames into two
 * separate buffers properly.
 */
GST_START_TEST (test_parse_nb_split)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_nb, &sinktemplate_nb);

  for (i = 0; i < 10; i++) {
    /* Put two frames in one buffer */
    buffer = buffer_new (frame_data_nb, 2 * FRAME_DATA_NB_LEN, SRC_CAPS_NB);
    memcpy (GST_BUFFER_DATA (buffer) + FRAME_DATA_NB_LEN,
        frame_data_nb, FRAME_DATA_NB_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 20);

  /* Does output buffers contain correct frame data? */
  g_list_foreach (buffers, buffer_verify_nb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if NB parser detects the format correctly.
 */
GST_START_TEST (test_parse_nb_detect_stream)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  GstCaps *caps, *mycaps;
  guint i;

  amrparse = setup_amrparse (&srctemplate_any, &sinktemplate_any);

  /* Push the header */
  buffer = buffer_new (frame_hdr_nb, FRAME_HDR_NB_LEN, NULL);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, NULL);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  caps = GST_PAD_CAPS (sinkpad);
  mycaps = gst_caps_from_string (SINK_CAPS_NB);
  fail_unless (gst_caps_is_equal (caps, mycaps));
  gst_caps_unref (mycaps);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if NB parser skips garbage in the datastream correctly and still
 * finds all correct frames.
 */
GST_START_TEST (test_parse_nb_skip_garbage)
{
  buffer_verify_data_s vdata = { 5, GARBAGE_FRAME_LEN };
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_nb, &sinktemplate_nb);

  /* First push 5 healthy frames */
  for (i = 0; i < 5; i++) {
    buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, SRC_CAPS_NB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* Then push some garbage */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN, SRC_CAPS_NB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  /* Again, healthy frames */
  for (i = 0; i < 5; i++) {
    buffer = buffer_new (frame_data_nb, FRAME_DATA_NB_LEN, SRC_CAPS_NB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* Did it find all 10 healthy frames? */
  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_nb, &vdata);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if WB parser manages to find all frames and pushes them forward.
 */
GST_START_TEST (test_parse_wb_normal)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_wb, &sinktemplate_wb);

  /* Push the header */
  buffer = buffer_new (frame_hdr_wb, FRAME_HDR_WB_LEN, SRC_CAPS_WB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  offset_counter = FRAME_HDR_WB_LEN;

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, SRC_CAPS_WB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_wb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if WB parser drains its buffers properly. Even one single buffer
 * should be drained and pushed forward when EOS occurs. This single buffer
 * case is special, since normally the parser needs more data to be sure
 * about stream format. But it should still push the frame forward in EOS.
 */
GST_START_TEST (test_parse_wb_drain_single)
{
  GstElement *amrparse;
  GstBuffer *buffer;

  amrparse = setup_amrparse (&srctemplate_wb, &sinktemplate_wb);

  buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, SRC_CAPS_WB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_foreach (buffers, buffer_verify_wb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Make sure that parser does not drain garbage when EOS occurs.
 */
GST_START_TEST (test_parse_wb_drain_garbage)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_wb, &sinktemplate_wb);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, SRC_CAPS_WB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* Now push one garbage frame and then EOS */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN, SRC_CAPS_WB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* parser should have pushed only the valid frames */
  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_wb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if WB parser splits a buffer that contains two frames into two
 * separate buffers properly.
 */
GST_START_TEST (test_parse_wb_split)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_wb, &sinktemplate_wb);

  for (i = 0; i < 10; i++) {
    /* Put two frames in one buffer */
    buffer = buffer_new (frame_data_wb, 2 * FRAME_DATA_WB_LEN, SRC_CAPS_WB);
    memcpy (GST_BUFFER_DATA (buffer) + FRAME_DATA_WB_LEN,
        frame_data_wb, FRAME_DATA_WB_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 20);

  /* Does output buffers contain correct frame data? */
  g_list_foreach (buffers, buffer_verify_wb, NULL);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if WB parser detects the format correctly.
 */
GST_START_TEST (test_parse_wb_detect_stream)
{
  GstElement *amrparse;
  GstBuffer *buffer;
  GstCaps *caps, *mycaps;
  guint i;

  amrparse = setup_amrparse (&srctemplate_any, &sinktemplate_any);

  /* Push the header */
  buffer = buffer_new (frame_hdr_wb, FRAME_HDR_WB_LEN, NULL);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, NULL);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  caps = GST_PAD_CAPS (sinkpad);
  mycaps = gst_caps_from_string (SINK_CAPS_WB);
  fail_unless (gst_caps_is_equal (caps, mycaps));
  gst_caps_unref (mycaps);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Test if WB parser skips garbage in the datastream correctly and still
 * finds all correct frames.
 */
GST_START_TEST (test_parse_wb_skip_garbage)
{
  buffer_verify_data_s vdata = { 5, GARBAGE_FRAME_LEN };
  GstElement *amrparse;
  GstBuffer *buffer;
  guint i;

  amrparse = setup_amrparse (&srctemplate_wb, &sinktemplate_wb);

  /* First push 5 healthy frames */
  for (i = 0; i < 5; i++) {
    buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, SRC_CAPS_WB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* Then push some garbage */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN, SRC_CAPS_WB);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  /* Again, healthy frames */
  for (i = 0; i < 5; i++) {
    buffer = buffer_new (frame_data_wb, FRAME_DATA_WB_LEN, SRC_CAPS_WB);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* Did it find all 10 healthy frames? */
  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_wb, &vdata);

  cleanup_amrparse (amrparse);
}

GST_END_TEST;


/*
 * Create test suite.
 */
static Suite *
amrparse_suite ()
{
  Suite *s = suite_create ("amrparse");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  /* AMR-NB tests */
  tcase_add_test (tc_chain, test_parse_nb_normal);
  tcase_add_test (tc_chain, test_parse_nb_drain_single);
  tcase_add_test (tc_chain, test_parse_nb_drain_garbage);
  tcase_add_test (tc_chain, test_parse_nb_split);
  tcase_add_test (tc_chain, test_parse_nb_detect_stream);
  tcase_add_test (tc_chain, test_parse_nb_skip_garbage);

  /* AMR-WB tests */
  tcase_add_test (tc_chain, test_parse_wb_normal);
  tcase_add_test (tc_chain, test_parse_wb_drain_single);
  tcase_add_test (tc_chain, test_parse_wb_drain_garbage);
  tcase_add_test (tc_chain, test_parse_wb_split);
  tcase_add_test (tc_chain, test_parse_wb_detect_stream);
  tcase_add_test (tc_chain, test_parse_wb_skip_garbage);
  return s;
}

/*
 * TODO:
 *   - Both push- and pull-modes need to be tested
 *      * Pull-mode & EOS
 */

GST_CHECK_MAIN (amrparse);
