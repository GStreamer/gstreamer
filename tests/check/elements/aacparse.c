/*
 * GStreamer
 *
 * unit test for aacparse
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
#include "aacparse_data.h"

#define SRC_CAPS_CDATA "audio/mpeg, framed=(boolean)false, codec_data=(buffer)1190"
#define SRC_CAPS_TMPL  "audio/mpeg, framed=(boolean)false, mpegversion=(int){2,4}"

#define SINK_CAPS \
    "audio/mpeg, framed=(boolean)true"
#define SINK_CAPS_MPEG2 \
    "audio/mpeg, framed=(boolean)true, mpegversion=2, rate=48000, channels=2"
#define SINK_CAPS_MPEG4 \
    "audio/mpeg, framed=(boolean)true, mpegversion=4, rate=96000, channels=2"
#define SINK_CAPS_TMPL  "audio/mpeg, framed=(boolean)true, mpegversion=(int){2,4}"

GList *buffers;
GList *current_buf = NULL;

GstPad *srcpad, *sinkpad;
guint dataoffset = 0;
GstClockTime ts_counter = 0;
gint64 offset_counter = 0;
guint buffer_counter = 0;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL)
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_TMPL)
    );

typedef struct
{
  guint buffers_before_offset_skip;
  guint offset_skip_amount;
  const unsigned char *data_to_verify;
  GstCaps *caps;
} buffer_verify_data_s;

/* takes a copy of the passed buffer data */
GstBuffer *
buffer_new (const unsigned char *buffer_data, guint size)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (size);
  if (buffer_data) {
    memcpy (GST_BUFFER_DATA (buffer), buffer_data, size);
  } else {
    guint i;
    /* Create a recognizable pattern (loop 0x00 -> 0xff) in the data block */
    for (i = 0; i < size; i++) {
      GST_BUFFER_DATA (buffer)[i] = i % 0x100;
    }
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (srcpad));
  GST_BUFFER_OFFSET (buffer) = dataoffset;
  dataoffset += size;
  return buffer;
}


/*
 * Count buffer sizes together.
 */
static void
buffer_count_size (void *buffer, void *user_data)
{
  guint *sum = (guint *) user_data;
  *sum += GST_BUFFER_SIZE (buffer);
}


/*
 * Verify that given buffer contains predefined ADTS frame.
 */
static void
buffer_verify_adts (void *buffer, void *user_data)
{
  buffer_verify_data_s *vdata;

  if (!user_data) {
    return;
  }

  vdata = (buffer_verify_data_s *) user_data;

  fail_unless (memcmp (GST_BUFFER_DATA (buffer), vdata->data_to_verify,
          ADTS_FRAME_LEN) == 0);

  fail_unless (GST_BUFFER_TIMESTAMP (buffer) == ts_counter);
  fail_unless (GST_BUFFER_DURATION (buffer) != 0);

  if (vdata->buffers_before_offset_skip) {
    /* This is for skipping the garbage in some test cases */
    if (buffer_counter == vdata->buffers_before_offset_skip) {
      offset_counter += vdata->offset_skip_amount;
    }
  }
  fail_unless (GST_BUFFER_OFFSET (buffer) == offset_counter);

  if (vdata->caps) {
    gchar *bcaps = gst_caps_to_string (GST_BUFFER_CAPS (buffer));
    g_free (bcaps);

    GST_LOG ("%" GST_PTR_FORMAT " = %" GST_PTR_FORMAT " ?",
        GST_BUFFER_CAPS (buffer), vdata->caps);
    fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), vdata->caps));
  }

  ts_counter += GST_BUFFER_DURATION (buffer);
  offset_counter += ADTS_FRAME_LEN;
  buffer_counter++;
}

GstElement *
setup_aacparse (const gchar * src_caps_str)
{
  GstElement *aacparse;
  GstCaps *srccaps = NULL;
  GstBus *bus;

  if (src_caps_str) {
    srccaps = gst_caps_from_string (src_caps_str);
    fail_unless (srccaps != NULL);
  }

  aacparse = gst_check_setup_element ("aacparse");
  srcpad = gst_check_setup_src_pad (aacparse, &srctemplate, srccaps);
  sinkpad = gst_check_setup_sink_pad (aacparse, &sinktemplate, NULL);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (aacparse, bus);

  fail_unless (gst_element_set_state (aacparse,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  if (srccaps) {
    gst_caps_unref (srccaps);
  }
  ts_counter = offset_counter = buffer_counter = 0;
  buffers = NULL;
  return aacparse;
}

static void
cleanup_aacparse (GstElement * aacparse)
{
  GstBus *bus;

  /* Free parsed buffers */
  gst_check_drop_buffers ();

  bus = GST_ELEMENT_BUS (aacparse);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (aacparse);
  gst_check_teardown_sink_pad (aacparse);
  gst_check_teardown_element (aacparse);
}


/*
 * Test if the parser pushes data with ADIF header properly and detects the
 * stream to MPEG4 properly.
 */
GST_START_TEST (test_parse_adif_normal)
{
  GstElement *aacparse;
  GstBuffer *buffer;
  GstCaps *scaps, *sinkcaps;
  guint datasum = 0;
  guint i;

  aacparse = setup_aacparse (NULL);

  buffer = buffer_new (adif_header, ADIF_HEADER_LEN);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  for (i = 0; i < 3; i++) {
    buffer = buffer_new (NULL, 100);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* Calculate the outputted buffer sizes */
  g_list_foreach (buffers, buffer_count_size, &datasum);

  /* ADIF is not a framed format, and therefore we cannot expect the
     same amount of output buffers as we pushed. However, all data should
     still come through, including the header bytes */
  fail_unless_equals_int (datasum, 3 * 100 + ADIF_HEADER_LEN);

  /* Check that the negotiated caps are as expected */
  /* For ADIF parser assumes that data is always version 4 */
  scaps = gst_caps_from_string (SINK_CAPS_MPEG4);
  sinkcaps = gst_pad_get_negotiated_caps (sinkpad);
  GST_LOG ("%" GST_PTR_FORMAT " = %" GST_PTR_FORMAT " ?", sinkcaps, scaps);
  fail_unless (gst_caps_is_equal (sinkcaps, scaps));
  gst_caps_unref (sinkcaps);
  gst_caps_unref (scaps);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Test if the parser pushes data with ADTS frames properly.
 */
GST_START_TEST (test_parse_adts_normal)
{
  buffer_verify_data_s vdata = { 0, 0, adts_frame_mpeg4, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;
  guint i;

  aacparse = setup_aacparse (NULL);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Test if ADTS parser drains its buffers properly. Even one single frame
 * should be drained and pushed forward when EOS occurs. This single frame
 * case is special, since normally the parser needs more data to be sure
 * about stream format. But it should still push the frame forward in EOS.
 */
GST_START_TEST (test_parse_adts_drain_single)
{
  buffer_verify_data_s vdata = { 0, 0, adts_frame_mpeg4, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;

  aacparse = setup_aacparse (NULL);

  buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Make sure that parser does not drain garbage when EOS occurs.
 */
GST_START_TEST (test_parse_adts_drain_garbage)
{
  buffer_verify_data_s vdata = { 0, 0, adts_frame_mpeg4, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;
  guint i;

  aacparse = setup_aacparse (NULL);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* Push one garbage frame and then EOS */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Test if ADTS parser splits a buffer that contains two frames into two
 * separate buffers properly.
 */
GST_START_TEST (test_parse_adts_split)
{
  buffer_verify_data_s vdata = { 0, 0, adts_frame_mpeg4, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;
  guint i;

  aacparse = setup_aacparse (NULL);

  for (i = 0; i < 5; i++) {
    buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN * 2);
    memcpy (GST_BUFFER_DATA (buffer) + ADTS_FRAME_LEN,
        adts_frame_mpeg4, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Test if the ADTS parser skips garbage between frames properly.
 */
GST_START_TEST (test_parse_adts_skip_garbage)
{
  buffer_verify_data_s vdata =
      { 10, GARBAGE_FRAME_LEN, adts_frame_mpeg4, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;
  guint i;

  aacparse = setup_aacparse (NULL);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }

  /* push garbage */
  buffer = buffer_new (garbage_frame, GARBAGE_FRAME_LEN);
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  for (i = 0; i < 10; i++) {
    buffer = buffer_new (adts_frame_mpeg4, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  fail_unless_equals_int (g_list_length (buffers), 20);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


/*
 * Test if the src caps are set according to stream format (MPEG version).
 */
GST_START_TEST (test_parse_adts_detect_mpeg_version)
{
  buffer_verify_data_s vdata = { 0, 0, adts_frame_mpeg2, NULL };
  GstElement *aacparse;
  GstBuffer *buffer;
  GstCaps *sinkcaps;
  guint i;

  aacparse = setup_aacparse (NULL);

  /* buffer_verify_adts will check if the caps are equal */
  vdata.caps = gst_caps_from_string (SINK_CAPS_MPEG2);

  for (i = 0; i < 10; i++) {
    /* Push MPEG version 2 frames. */
    buffer = buffer_new (adts_frame_mpeg2, ADTS_FRAME_LEN);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* Check that the negotiated caps are as expected */
  sinkcaps = gst_pad_get_negotiated_caps (sinkpad);
  GST_LOG ("%" GST_PTR_FORMAT " = %" GST_PTR_FORMAT "?", sinkcaps, vdata.caps);
  fail_unless (gst_caps_is_equal (sinkcaps, vdata.caps));
  gst_caps_unref (sinkcaps);

  fail_unless_equals_int (g_list_length (buffers), 10);
  g_list_foreach (buffers, buffer_verify_adts, &vdata);

  gst_caps_unref (vdata.caps);
  cleanup_aacparse (aacparse);
}

GST_END_TEST;

#define structure_get_int(s,f) \
    (g_value_get_int(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_int_equals(s,field,num) \
    fail_unless_equals_int (structure_get_int(s,field), num)
/*
 * Test if the parser handles raw stream and codec_data info properly.
 */
GST_START_TEST (test_parse_handle_codec_data)
{
  GstElement *aacparse;
  GstBuffer *buffer;
  GstCaps *sinkcaps;
  GstStructure *s;
  guint datasum = 0;
  guint i;

  aacparse = setup_aacparse (SRC_CAPS_CDATA);

  for (i = 0; i < 10; i++) {
    /* Push random data. It should get through since the parser should be
       initialized because it got codec_data in the caps */
    buffer = buffer_new (NULL, 100);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);
  }
  gst_pad_push_event (srcpad, gst_event_new_eos ());

  /* Check that the negotiated caps are as expected */
  /* When codec_data is present, parser assumes that data is version 4 */
  sinkcaps = gst_pad_get_negotiated_caps (sinkpad);
  GST_LOG ("aac output caps: %" GST_PTR_FORMAT, sinkcaps);
  s = gst_caps_get_structure (sinkcaps, 0);
  fail_unless (gst_structure_has_name (s, "audio/mpeg"));
  fail_unless_structure_field_int_equals (s, "mpegversion", 4);
  fail_unless_structure_field_int_equals (s, "channels", 2);
  fail_unless_structure_field_int_equals (s, "rate", 48000);
  fail_unless (gst_structure_has_field (s, "codec_data"));

  gst_caps_unref (sinkcaps);

  g_list_foreach (buffers, buffer_count_size, &datasum);
  fail_unless_equals_int (datasum, 10 * 100);

  cleanup_aacparse (aacparse);
}

GST_END_TEST;


static Suite *
aacparse_suite ()
{
  Suite *s = suite_create ("aacparse");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  /* ADIF tests */
  tcase_add_test (tc_chain, test_parse_adif_normal);

  /* ADTS tests */
  tcase_add_test (tc_chain, test_parse_adts_normal);
  tcase_add_test (tc_chain, test_parse_adts_drain_single);
  tcase_add_test (tc_chain, test_parse_adts_drain_garbage);
  tcase_add_test (tc_chain, test_parse_adts_split);
  tcase_add_test (tc_chain, test_parse_adts_skip_garbage);
  tcase_add_test (tc_chain, test_parse_adts_detect_mpeg_version);

  /* Other tests */
  tcase_add_test (tc_chain, test_parse_handle_codec_data);

  return s;
}


/*
 * TODO:
 *   - Both push- and pull-modes need to be tested
 *      * Pull-mode & EOS
 */

GST_CHECK_MAIN (aacparse);
