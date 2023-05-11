/* GStreamer
 *
 * unit test for qtdemux
 *
 * Copyright (C) <2016> Edward Hervey <edward@centricular.com>
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
#include <config.h>
#endif

#include <glib/gstdio.h>
#include <glib/gprintf.h>

#include <gio/gio.h>

#include <gst/check/check.h>
#include <gst/app/app.h>
#include <gst/audio/audio.h>

#define TEST_FILE_PREFIX GST_TEST_FILES_PATH G_DIR_SEPARATOR_S

/* Fragments taken from http://dash.akamaized.net/dash264/TestCases/5c/nomor/4_1a.mpd
 *
 * Audio stream (aac)
 * Header + first Fragments
 */
/* http://dash.akamaized.net/dash264/TestCases/5c/nomor/BBB_32k_init.mp4 */
#define BBB_FILE_I TEST_FILE_PREFIX "qtdemux-test-BBB_32k_init.mp4"
static guint8 *BBB_32k_init_mp4;
static const guint BBB_32k_init_mp4_len = 776;

/* http://dash.akamaized.net/dash264/TestCases/5c/nomor/BBB_32k_1.mp4 */
#define BBB_FILE_1 TEST_FILE_PREFIX "qtdemux-test-BBB_32k_1.mp4"
static guint8 *BBB_32k_1_mp4;
static const guint BBB_32k_1_mp4_len = 8423;

/* Fragments taken from http://www.bok.net/dash/tears_of_steel/cleartext/stream.mpd
 *
 * Audio stream (aac)
 * Header + first fragment
 */
/* http://www.bok.net/dash/tears_of_steel/cleartext/audio/en/init.mp4 */
#define INIT_FILE TEST_FILE_PREFIX "qtdemux-test-audio-init.mp4"
static guint8 *init_mp4;
const guint init_mp4_len = 624;

/* http://www.bok.net/dash/tears_of_steel/cleartext/audio/en/seg-1.m4f */
#define SEG1_FILE TEST_FILE_PREFIX "qtdemux-test-audio-seg1.m4f"
static guint8 *seg_1_m4f;
const guint seg_1_m4f_len = 49554;
const guint seg_1_moof_size = 1120;
const guint seg_1_sample_0_offset = 1128;

static const guint seg_1_sample_sizes[] = {
  371, 372, 477, 530, 489, 462, 441, 421, 420, 410, 402, 398, 381, 381, 386,
  386, 369, 370, 362, 346, 357, 355, 376, 336, 341, 358, 350, 362, 333, 415,
  386, 364, 344, 386, 358, 365, 404, 342, 361, 366, 361, 350, 390, 348, 366,
  359, 357, 360, 349, 356, 365, 393, 353, 385, 381, 348, 345, 414, 372, 369,
  401, 391, 333, 339, 423, 343, 445, 425, 422, 415, 406, 389, 395, 375, 356,
  442, 432, 391, 385, 339, 277, 293, 316, 327, 309, 389, 359, 427, 326, 420,
  407, 316, 362, 419, 349, 387, 326, 328, 367, 344, 425, 329, 379, 403, 314,
  397, 368, 389, 380, 373, 342, 343, 368, 436, 359, 352, 361, 366, 350, 419,
  331, 426, 401, 382, 326, 411, 364, 338, 345
};

/* in timescale */
static const GstClockTime seg_1_sample_duration = 1024;
static const guint32 seg_1_timescale = 44100;

static gboolean
load_file (const gchar * fn, guint8 ** p_data, guint expected_len)
{
  gsize read_len = 0;

  if (!g_file_get_contents (fn, (gchar **) p_data, &read_len, NULL))
    return FALSE;

  g_assert_cmpuint (read_len, ==, expected_len);
  return TRUE;
}

static void
load_files (void)
{
  g_assert (load_file (INIT_FILE, &init_mp4, init_mp4_len));
  g_assert (load_file (SEG1_FILE, &seg_1_m4f, seg_1_m4f_len));
  g_assert (load_file (BBB_FILE_I, &BBB_32k_init_mp4, BBB_32k_init_mp4_len));
  g_assert (load_file (BBB_FILE_1, &BBB_32k_1_mp4, BBB_32k_1_mp4_len));
}

static void
unload_files (void)
{
  g_clear_pointer (&init_mp4, (GDestroyNotify) g_free);
  g_clear_pointer (&seg_1_m4f, (GDestroyNotify) g_free);
  g_clear_pointer (&BBB_32k_init_mp4, (GDestroyNotify) g_free);
  g_clear_pointer (&BBB_32k_1_mp4, (GDestroyNotify) g_free);
}

typedef struct
{
  GstPad *srcpad;
  guint expected_size;
  GstClockTime expected_time;
} CommonTestData;

static GstPadProbeReturn
qtdemux_probe (GstPad * pad, GstPadProbeInfo * info, CommonTestData * data)
{
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);

    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_SEGMENT:
      {
        const GstSegment *segment;
        gst_event_parse_segment (ev, &segment);
        fail_unless (GST_CLOCK_TIME_IS_VALID (segment->format));
        fail_unless (GST_CLOCK_TIME_IS_VALID (segment->start));
        fail_unless (GST_CLOCK_TIME_IS_VALID (segment->base));
        fail_unless (GST_CLOCK_TIME_IS_VALID (segment->time));
        fail_unless (GST_CLOCK_TIME_IS_VALID (segment->position));
        break;
      }
        break;
      default:
        break;
    }

    return GST_PAD_PROBE_OK;
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);

    fail_unless_equals_int (gst_buffer_get_size (buf), data->expected_size);
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), data->expected_time);
  }

  return GST_PAD_PROBE_DROP;
}

static void
qtdemux_pad_added_cb (GstElement * element, GstPad * pad, CommonTestData * data)
{
  data->srcpad = pad;
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) qtdemux_probe, data, NULL);
}

GST_START_TEST (test_qtdemux_fuzzed0)
{
  GstHarness *h;
  GstBuffer *buf;
  guchar *fuzzed_qtdemux;
  gsize fuzzed_qtdemux_len;

  /* The goal of this test is to check that qtdemux can properly handle
   * a stream that does not contain any stsd entries, by correctly identifying
   * the case and erroring out appropriately.
   */

  h = gst_harness_new_parse ("qtdemux");
  gst_harness_set_src_caps_str (h, "video/quicktime");

  fuzzed_qtdemux =
      g_base64_decode
      ("AAAAIGZ0eXBtcDQyAAAAAG1wNDJtcDQxaXNvbWlzbzIAAAAIZnJlZQAAAMltZGF0AAAADGdCwAyV"
      "oQkgHhEI1AAAAARozjyAAAAAIWW4AA5///wRRQAfHAxwABAJkxWTk6xWuuuupaupa6668AAAABJB"
      "4CBX8Zd3d3d3d3d3eJ7E8ZAAAABWQeBAO/wpFAYoDFAYoDFAYkeKAzx4+gAA+kcPHBQGePPHF6jj"
      "HP0Qdj/og7H/SHY/6jsf9R2P+o7H/Udj/qOx/1HY/6jsf9R2P+o7H/Udj/qOx/1HY/AAAAAGQeBg"
      "O8IwAAAABkHggDvCMAAAA1dtb292AAAAbG12aGQAAAAA1lbpxdZW6cYAAAfQAAAH0AABAAABAAAA"
      "AAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAAAAAAAAACAAACpnRyYWsAAABcdGtoZAAAAAfWVunF1lbpxgAAAAEAAAAAAAAH0AAA"
      "AAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAEAAAAAAAQAAAAEA"
      "AAAAACRlZHRzAAAAHGVsc3QAAIAAAAAAAQAAB9AAAAAAAAEAAAAAAeFtZGlhAAAAIG1kaGQAAAAA"
      "1lbpxdZW6cYAAAH0AAAB9FXEAAAAAAAtaGRscgAAAAAAAAAAdmlkZUAAAAAAAAAAAAAAAFZpZGVv"
      "SGFuZGxlcgAAAAGMbWluZgAAABR2bWhkAAAAAQAAAAAAAAAAAAAAJGRpbmYAAAAcZHJlZgAAAAAA"
      "AAABAAAADHVybCAAAAABAAABTHN0YmwAAADAc3RzZAAAAAAAAAAAAAAAsGF2YzEAAAAAAAAAAQAA"
      "AAAAAAAZAAAAAAAAAAAAQABAAEgAAABIAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAY//8AAAAjYXZjQwFCwAz/4QAMZ0LADJWhCSAeEQjUAQAEaM48gAAAABRidHJ0AAAA"
      "AAAAAAAAAAYIAAAAE2NvbHJuY2x4AAYAAQAGAAAAABBwYXNwAAAAAQAAAAEAAAAYc3R0cwAAAAAA"
      "AAABAAAABQAAAAAAAAAUc3RzcwAAAAAAAAABAAAAAQAAABxzdHNjAAAAAAAAAAEAAAABAAAABQAA"
      "AAEAAAAoc3RzegAAAAAAAAAAAAAAAQAAAAAAAAAWAAAAWgAAAAoAAAAKAAAAFHN0Y28AAAAAAAAA"
      "AQAAADAAAAA9dWR0YQAAADVtZXRhAAAAAAAAACFoZGxyAAAAAG1obJJtZGlyAAAAAAAAAAAAAAAA"
      "AAAAAAhpbHN0AAAAPXVkdGEAAAA1bWV0YQAAAAAAAAAhaGRscgAAAABtaGxybWRpcgAAAAAAAAAA"
      "AAAAAAAAAAAIaWxzdA==", &fuzzed_qtdemux_len);

  buf = gst_buffer_new_and_alloc (fuzzed_qtdemux_len);
  gst_buffer_fill (buf, 0, fuzzed_qtdemux, fuzzed_qtdemux_len);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  fail_unless (gst_harness_buffers_received (h) == 0);

  g_free (fuzzed_qtdemux);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_fuzzed1)
{
  GstHarness *h;
  GstBuffer *buf;
  guchar *fuzzed_qtdemux;
  gsize fuzzed_qtdemux_len;

  /* The goal of this test is to check that qtdemux can properly handle
   * a stream that claims it contains more stsd entries than it can possibly have,
   * by correctly identifying the case and erroring out appropriately.
   */

  h = gst_harness_new_parse ("qtdemux");
  gst_harness_set_src_caps_str (h, "video/quicktime");

  fuzzed_qtdemux =
      g_base64_decode
      ("AAAAIGZ0eXBtcDQyAAAAAG1wNDJtcDQxaXNvbWlzbzIAAAAIZnJlZQAAAMltZGF0AAAADGdCwAyV"
      "oQkgHhEI1AAAAARozjyAAAAAIWW4BA5///wRRQAfHAxwABAJkxWTk6xWuuuupaupa6668AAAABJB"
      "4CBX8Zd3d3d3d3d3eJ7E8ZAAAABWQeBAO+opFAYoDFAYoDFAYkeKAzx4oDFAYkcPHBQGePPHF6jj"
      "HP0Qdj/og7H/SHY/6jsf9R2P+o7H/Udj/qOx/1HY/6jsf9R2P+o7H/Udj/qOx/1HY/AAAAAGQeBg"
      "O8IwAAAABkHggDvCMAAAA1dtb292AAAAbG12aGQAAAAA1lbpxdZW6cYAAAfQAAAH0AABAAABAAAA"
      "AAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAAAAAAAAACAAACpnRyYWsAAABcdGtoZAAAAAfWVunF1lbpxgAAAAEAAAAAAAAH0AAA"
      "AAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAEAAAAAAAQAAAAEA"
      "AAAAACRlZHRzAAAAHGVsc3QAAAAAAAAAAQAAB9AAAAAAAAEAAAAAAeFtZGlhAAAAIG1kaGQAAAAA"
      "1lbpxdZW6cYAAAH0AAAB9FXEAAAAAAAtaGRscgAAAAAAAAAAdmlkZQAAAAAAAAAAAAAAAFZpZGVv"
      "SGFuZGxlcgAAAAGMbWluZgAAABR2bWhkAAAAAQAAAAAAAAAAAAAAJGRpbmYAAAAcZHJlZgAAAAAA"
      "AAABAAAADHVybCAAAAABAAABTHN0YmwAAADAc3RzZAAAAADv/wABAAAAsGF2YzEAAAAAAAAAAQAA"
      "AAAAAAAAAAAAAAAAAAAAQABAAEgAAABIAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAY//8AAAAjYXZjQwFCwAz/4QAMZ0LADJWhCSAeEQjUAQAEaM48gAAAABRidHJ0AAAA"
      "AAAAAAAAAAYIAAAAE2NvbHJuY2x4AAYAAQAGAAAAABBwYXNwAAAAAQAAAAEAAAAYc3R0cwAAAAAA"
      "AAABAAAABQAAAGQAAAAUc3RzcwAAAAAAAAABAAAAAQAAABxzdHNjAAAAAAAAAAEAAAABAAAABQAA"
      "AAEAAAAoc3RzegAAAAAAAAAAAAAABQAAAD0AAAAWAAAAWgAAAAoAAAAKAAAAFHN0Y28AAAAAAAAA"
      "AQAAADAAAAA9dWR0YQAAADVtZXRhAAAAAAAAACFoZGxyAAAAAG1obHJtZGlyAAAAAAAAAAAAAAAA"
      "AAAAAAhpbHN0AAAAPXVkdGEAAAA1bWV0YQAAAAAAAAAhaGRscgAAAABtaGxybWRpcgAAAAAAAAAA"
      "AAAAAAAAAAAIaWxzdA==", &fuzzed_qtdemux_len);

  buf = gst_buffer_new_and_alloc (fuzzed_qtdemux_len);
  gst_buffer_fill (buf, 0, fuzzed_qtdemux, fuzzed_qtdemux_len);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  fail_unless (gst_harness_buffers_received (h) == 0);

  g_free (fuzzed_qtdemux);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_input_gap)
{
  GstElement *qtdemux;
  GstPad *sinkpad;
  CommonTestData data = { 0, };
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  guint i, offset;
  GstClockTime pts;

  /* The goal of this test is to check that qtdemux can properly handle
   * fragmented input from dashdemux, with gaps in it.
   *
   * Input segment :
   *   - TIME
   * Input buffers :
   *   - The offset is set on buffers, it corresponds to the offset
   *     within the current fragment.
   *   - Buffer of the beginning of a fragment has the PTS set, others
   *     don't.
   *   - By extension, the beginning of a fragment also has an offset
   *     of 0.
   */

  load_files ();

  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux, "pad-added", (GCallback) qtdemux_pad_added_cb,
      &data);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Feed the init buffer, should create the source pad */
  inbuf = gst_buffer_new_and_alloc (init_mp4_len);
  gst_buffer_fill (inbuf, 0, init_mp4, init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_DEBUG ("Pushing header buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  /* Now send the trun of the first fragment */
  inbuf = gst_buffer_new_and_alloc (seg_1_moof_size);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_moof_size);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  /* We are simulating that this fragment can happen at any point */
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing trun buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.srcpad == NULL);

  /* We are now ready to send some buffers with gaps */
  offset = seg_1_sample_0_offset;
  pts = 0;

  GST_DEBUG ("Pushing gap'ed buffers");
  for (i = 0; i < 129; i++) {
    /* Let's send one every 3 */
    if ((i % 3) == 0) {
      GST_DEBUG ("Pushing buffer #%d offset:%" G_GUINT32_FORMAT, i, offset);
      inbuf = gst_buffer_new_and_alloc (seg_1_sample_sizes[i]);
      gst_buffer_fill (inbuf, 0, seg_1_m4f + offset, seg_1_sample_sizes[i]);
      GST_BUFFER_OFFSET (inbuf) = offset;
      GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
      data.expected_time =
          gst_util_uint64_scale (pts, GST_SECOND, seg_1_timescale);
      data.expected_size = seg_1_sample_sizes[i];
      fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
    }
    /* Finally move offset forward */
    offset += seg_1_sample_sizes[i];
    pts += seg_1_sample_duration;
  }

  gst_object_unref (sinkpad);
  gst_element_set_state (qtdemux, GST_STATE_NULL);
  gst_object_unref (qtdemux);

  unload_files ();
}

GST_END_TEST;

typedef struct
{
  GstPad *sinkpad;
  GstPad *pending_pad;
  GstEventType *expected_events;
  guint step;
  guint total_step;
  guint expected_num_srcpad;
  guint num_srcpad;
} ReconfigTestData;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static gboolean
_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gst_event_unref (event);
  return TRUE;
}

static GstFlowReturn
_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static GstPadProbeReturn
qtdemux_block_for_reconfig (GstPad * pad, GstPadProbeInfo * info,
    ReconfigTestData * data)
{
  fail_unless (data->pending_pad);
  fail_unless (data->pending_pad == pad);

  GST_DEBUG_OBJECT (pad, "Unblock pad");

  if (gst_pad_is_linked (data->sinkpad)) {
    GstPad *peer = gst_pad_get_peer (data->sinkpad);
    fail_unless (peer);
    gst_pad_unlink (peer, data->sinkpad);
  }

  fail_unless (gst_pad_link (data->pending_pad, data->sinkpad) ==
      GST_PAD_LINK_OK);
  data->pending_pad = NULL;

  return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn
qtdemux_probe_for_reconfig (GstPad * pad, GstPadProbeInfo * info,
    ReconfigTestData * data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstEventType expected;

  if (data->step < data->total_step) {
    expected = data->expected_events[data->step];
  } else {
    expected = GST_EVENT_UNKNOWN;
  }

  GST_DEBUG ("Got event %p %s", event, GST_EVENT_TYPE_NAME (event));

  fail_unless (GST_EVENT_TYPE (event) == expected,
      "Received unexpected event: %s (expected: %s)",
      GST_EVENT_TYPE_NAME (event), gst_event_type_get_name (expected));
  data->step++;

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS && data->step < data->total_step) {
    /* If current EOS is for draining, there should be pending srcpad */
    fail_unless (data->pending_pad != NULL);
  }

  return GST_PAD_PROBE_OK;
}

static void
qtdemux_pad_added_cb_for_reconfig (GstElement * element, GstPad * pad,
    ReconfigTestData * data)
{
  data->num_srcpad++;

  fail_unless (data->num_srcpad <= data->expected_num_srcpad);
  fail_unless (data->pending_pad == NULL);

  GST_DEBUG_OBJECT (pad, "New pad added");

  data->pending_pad = pad;
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) qtdemux_block_for_reconfig, data, NULL);

  if (!data->sinkpad) {
    GstPad *sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");

    gst_pad_set_event_function (sinkpad, _sink_event);
    gst_pad_set_chain_function (sinkpad, _sink_chain);

    gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        (GstPadProbeCallback) qtdemux_probe_for_reconfig, data, NULL);
    gst_pad_set_active (sinkpad, TRUE);
    data->sinkpad = sinkpad;
  }
}

GST_START_TEST (test_qtdemux_duplicated_moov)
{
  GstElement *qtdemux;
  GstPad *sinkpad;
  ReconfigTestData data = { 0, };
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  GstEventType expected[] = {
    GST_EVENT_STREAM_START,
    GST_EVENT_CAPS,
    GST_EVENT_SEGMENT,
    GST_EVENT_TAG,
    GST_EVENT_TAG,
    GST_EVENT_EOS
  };

  data.expected_events = expected;
  data.expected_num_srcpad = 1;
  data.total_step = G_N_ELEMENTS (expected);

  load_files ();

  /* The goal of this test is to check that qtdemux can properly handle
   * duplicated moov without redundant events and pad exposing
   *
   * Testing step
   *  - Push events stream-start and segment to qtdemux
   *  - Push init and media data
   *  - Push the same init and media data again
   *
   * Expected behaviour
   *  - Expose srcpad only once
   *  - No additional downstream events when the second init and media data is
   *    pushed to qtdemux
   */

  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux, "pad-added", (GCallback)
      qtdemux_pad_added_cb_for_reconfig, &data);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Feed the init buffer, should create the source pad */
  inbuf = gst_buffer_new_and_alloc (init_mp4_len);
  gst_buffer_fill (inbuf, 0, init_mp4, init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_DEBUG ("Pushing moov buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  fail_unless_equals_int (data.num_srcpad, 1);

  /* Now send the moof and mdat of the first fragment */
  inbuf = gst_buffer_new_and_alloc (seg_1_m4f_len);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_m4f_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_DEBUG ("Pushing moof and mdat buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  /* Resend the init, moof and mdat, no additional event and pad are expected */
  inbuf = gst_buffer_new_and_alloc (init_mp4_len);
  gst_buffer_fill (inbuf, 0, init_mp4, init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing moov buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  fail_unless_equals_int (data.num_srcpad, 1);

  inbuf = gst_buffer_new_and_alloc (seg_1_m4f_len);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_m4f_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = init_mp4_len;
  GST_DEBUG ("Pushing moof and mdat buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_unless (gst_pad_send_event (sinkpad, gst_event_new_eos ()) == TRUE);
  fail_unless_equals_int (data.step, data.total_step);
  fail_unless (data.pending_pad == NULL);

  gst_object_unref (sinkpad);
  gst_pad_set_active (data.sinkpad, FALSE);
  gst_object_unref (data.sinkpad);
  gst_element_set_state (qtdemux, GST_STATE_NULL);
  gst_object_unref (qtdemux);

  unload_files ();
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_stream_change)
{
  GstElement *qtdemux;
  GstPad *sinkpad;
  ReconfigTestData data = { 0, };
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  const gchar *upstream_id;
  const gchar *stream_id = NULL;
  gchar *expected_stream_id = NULL;
  guint track_id;
  GstEventType expected[] = {
    /* 1st group */
    GST_EVENT_STREAM_START,
    GST_EVENT_CAPS,
    GST_EVENT_SEGMENT,
    GST_EVENT_TAG,
    GST_EVENT_TAG,
    /* 2nd group (track-id change without upstream stream-start) */
    GST_EVENT_EOS,
    GST_EVENT_STREAM_START,
    GST_EVENT_CAPS,
    GST_EVENT_SEGMENT,
    GST_EVENT_TAG,
    GST_EVENT_TAG,
    /* 3rd group (no track-id change with upstream stream-start) */
    GST_EVENT_EOS,
    GST_EVENT_STREAM_START,
    GST_EVENT_CAPS,
    GST_EVENT_SEGMENT,
    GST_EVENT_TAG,
    GST_EVENT_TAG,
    /* last group (track-id change with upstream stream-start) */
    GST_EVENT_EOS,
    GST_EVENT_STREAM_START,
    GST_EVENT_CAPS,
    GST_EVENT_SEGMENT,
    GST_EVENT_TAG,
    GST_EVENT_TAG,
    GST_EVENT_EOS
  };

  data.expected_events = expected;
  data.expected_num_srcpad = 4;
  data.total_step = G_N_ELEMENTS (expected);

  load_files ();

  /* The goal of this test is to check that qtdemux can properly handle
   * stream change regardless of track-id change.
   * This test is simulating DASH bitrate switching (for both playbin and plabyin3)
   * and period-change for playbin3
   *
   * NOTE: During bitrate switching in DASH, track-id might be changed
   * NOTE: stream change with new stream-start to qtdemux is playbin3 specific behaviour,
   * because playbin configures new demux per period and existing demux never ever get
   * new stream-start again.
   *
   * Testing step
   *  [GROUP 1]
   *  - Push events stream-start and segment to qtdemux
   *  - Push init and media data to qtdemux
   *  [GROUP 2]
   *  - Push different (track-id change) init and media data to qtdemux
   *    without new downstream sticky events to qtdemux
   *  [GROUP 3]
   *  - Push events stream-start and segment to qtdemux again
   *  - Push the init and media data which are the same as GROUP 2
   *  [GROUP 4]
   *  - Push events stream-start and segment to qtdemux again
   *  - Push different (track-id change) init and media data to qtdemux
   *
   * Expected behaviour
   *  - Demux exposes srcpad four times, per test GROUP, regardless of track-id change
   *  - Whenever exposing new pads, downstream sticky events should be detected
   *    at demux srcpad
   */

  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux, "pad-added", (GCallback)
      qtdemux_pad_added_cb_for_reconfig, &data);

  /***************
   * TEST GROUP 1
   * (track-id: 2)
   **************/
  /* Send the initial STREAM_START and segment (TIME) event */
  upstream_id = "TEST-GROUP-1";
  track_id = 2;
  expected_stream_id = g_strdup_printf ("%s/%03u", upstream_id, track_id);
  event = gst_event_new_stream_start (upstream_id);
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Feed the init buffer, should create the source pad */
  inbuf = gst_buffer_new_and_alloc (init_mp4_len);
  gst_buffer_fill (inbuf, 0, init_mp4, init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_DEBUG ("Pushing moov buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  fail_unless_equals_int (data.num_srcpad, 1);

  /* Check stream-id */
  event = gst_pad_get_sticky_event (data.sinkpad, GST_EVENT_STREAM_START, 0);
  fail_unless (event != NULL);
  gst_event_parse_stream_start (event, &stream_id);
  fail_unless_equals_string (stream_id, expected_stream_id);
  g_free (expected_stream_id);
  gst_event_unref (event);

  /* Now send the moof and mdat of the first fragment */
  inbuf = gst_buffer_new_and_alloc (seg_1_m4f_len);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_m4f_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = init_mp4_len;
  GST_DEBUG ("Pushing moof and mdat buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);


  /***************
   * TEST GROUP 2
   * (track-id: 1)
   * - track-id change without new upstream stream-start event
   **************/
  /* Resend the init */
  inbuf = gst_buffer_new_and_alloc (BBB_32k_init_mp4_len);
  gst_buffer_fill (inbuf, 0, BBB_32k_init_mp4, BBB_32k_init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing moov buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  /* new srcpad should be exposed */
  fail_unless_equals_int (data.num_srcpad, 2);

  /* Check stream-id */
  upstream_id = "TEST-GROUP-1"; /* upstream-id does not changed from GROUP 1 */
  track_id = 1;                 /* track-id is changed from 2 to 1 */
  expected_stream_id = g_strdup_printf ("%s/%03u", upstream_id, track_id);
  event = gst_pad_get_sticky_event (data.sinkpad, GST_EVENT_STREAM_START, 0);
  fail_unless (event != NULL);
  gst_event_parse_stream_start (event, &stream_id);
  fail_unless_equals_string (stream_id, expected_stream_id);
  g_free (expected_stream_id);
  gst_event_unref (event);

  /* push the moof and mdat again */
  inbuf = gst_buffer_new_and_alloc (BBB_32k_1_mp4_len);
  gst_buffer_fill (inbuf, 0, BBB_32k_1_mp4, BBB_32k_1_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = BBB_32k_init_mp4_len;
  GST_DEBUG ("Pushing moof and mdat buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  /***************
   * TEST GROUP 3
   * (track-id: 1)
   * - Push new stream-start and segment to qtdemux
   * - Reuse init and media data of GROUP 2 (no track-id change)
   **************/
  /* Send STREAM_START and segment (TIME) event */
  upstream_id = "TEST-GROUP-3";
  track_id = 1;
  expected_stream_id = g_strdup_printf ("%s/%03u", upstream_id, track_id);
  event = gst_event_new_stream_start (upstream_id);
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Resend the init */
  inbuf = gst_buffer_new_and_alloc (BBB_32k_init_mp4_len);
  gst_buffer_fill (inbuf, 0, BBB_32k_init_mp4, BBB_32k_init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing moov buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  /* new srcpad should be exposed */
  fail_unless_equals_int (data.num_srcpad, 3);

  /* Check stream-id */
  event = gst_pad_get_sticky_event (data.sinkpad, GST_EVENT_STREAM_START, 0);
  fail_unless (event != NULL);
  gst_event_parse_stream_start (event, &stream_id);
  fail_unless_equals_string (stream_id, expected_stream_id);
  g_free (expected_stream_id);
  gst_event_unref (event);

  /* push the moof and mdat again */
  inbuf = gst_buffer_new_and_alloc (BBB_32k_1_mp4_len);
  gst_buffer_fill (inbuf, 0, BBB_32k_1_mp4, BBB_32k_1_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = BBB_32k_init_mp4_len;
  GST_DEBUG ("Pushing moof and mdat buffer");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  /***************
   * TEST GROUP 4
   * (track-id: 2)
   * - Push new stream-start and segment to qtdemux
   * - track-id change from 1 to 2
   **************/
  /* Send STREAM_START and segment (TIME) event */
  upstream_id = "TEST-GROUP-4";
  track_id = 2;
  expected_stream_id = g_strdup_printf ("%s/%03u", upstream_id, track_id);
  event = gst_event_new_stream_start (upstream_id);
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Resend the init */
  inbuf = gst_buffer_new_and_alloc (init_mp4_len);
  gst_buffer_fill (inbuf, 0, init_mp4, init_mp4_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing moov buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);
  /* new srcpad should be exposed */
  fail_unless_equals_int (data.num_srcpad, 4);

  /* Check stream-id */
  event = gst_pad_get_sticky_event (data.sinkpad, GST_EVENT_STREAM_START, 0);
  fail_unless (event != NULL);
  gst_event_parse_stream_start (event, &stream_id);
  fail_unless_equals_string (stream_id, expected_stream_id);
  g_free (expected_stream_id);
  gst_event_unref (event);

  /* push the moof and mdat again */
  inbuf = gst_buffer_new_and_alloc (seg_1_m4f_len);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_m4f_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = init_mp4_len;
  GST_DEBUG ("Pushing moof and mdat buffer again");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_unless (gst_pad_send_event (sinkpad, gst_event_new_eos ()) == TRUE);
  fail_unless_equals_int (data.step, data.total_step);
  fail_unless (data.pending_pad == NULL);

  gst_object_unref (sinkpad);
  gst_pad_set_active (data.sinkpad, FALSE);
  gst_object_unref (data.sinkpad);
  gst_element_set_state (qtdemux, GST_STATE_NULL);
  gst_object_unref (qtdemux);

  unload_files ();
}

GST_END_TEST;

static void
qtdemux_pad_added_cb_check_name (GstElement * element, GstPad * pad,
    gchar * data)
{
  gchar *pad_name = gst_pad_get_name (pad);

  GST_DEBUG_OBJECT (pad, "New pad added");
  fail_unless (!g_strcmp0 (pad_name, data));
  g_free (pad_name);
}

GST_START_TEST (test_qtdemux_pad_names)
{
  GstElement *qtdemux_v;
  GstElement *qtdemux_a;
  GstPad *sinkpad;
  gchar *expected_video_pad_name;
  gchar *expected_audio_pad_name;
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  GstCaps *caps;
  GstCaps *mediacaps;

  load_files ();

  /* The goal of this test is to check that qtdemux can create proper
   * pad names with encrypted stream caps in mss mode.
   *
   * Input Caps:
   *   - media-caps with cenc
   *
   * Expected behaviour
   *  - Demux exposes src pad with names in accordance to their media types
   */
  expected_video_pad_name = g_strdup ("video_0");
  qtdemux_v = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux_v, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux_v, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux_v, "pad-added", (GCallback)
      qtdemux_pad_added_cb_check_name, expected_video_pad_name);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send CAPS event* */
  mediacaps = gst_caps_new_simple ("application/x-cenc",
      "stream-format", G_TYPE_STRING, "avc",
      "format", G_TYPE_STRING, "H264",
      "width", G_TYPE_INT, 512,
      "height", G_TYPE_INT, 288,
      "original-media-type", G_TYPE_STRING, "video/x-h264",
      "protection-system", G_TYPE_STRING,
      "9a04f079-9840-4286-ab92-e65be0885f95", NULL);
  caps =
      gst_caps_new_simple ("video/quicktime",
      "variant", G_TYPE_STRING, "mss-fragmented",
      "timescale", G_TYPE_UINT64, G_GUINT64_CONSTANT (10000000),
      "media-caps", GST_TYPE_CAPS, mediacaps, NULL);

  /* Send segment event* */
  event = gst_event_new_caps (caps);
  GST_DEBUG ("Pushing caps event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_caps_unref (mediacaps);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send the first fragment */
  /* NOTE: mss streams don't have moov */
  inbuf = gst_buffer_new_and_alloc (seg_1_moof_size);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_moof_size);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing video fragment");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  gst_object_unref (sinkpad);
  gst_element_set_state (qtdemux_v, GST_STATE_NULL);
  gst_object_unref (qtdemux_v);
  g_free (expected_video_pad_name);

  /* Repeat test for audio media type */
  expected_audio_pad_name = g_strdup ("audio_0");
  qtdemux_a = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux_a, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux_a, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux_a, "pad-added", (GCallback)
      qtdemux_pad_added_cb_check_name, expected_audio_pad_name);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send CAPS event* */
  mediacaps = gst_caps_new_simple ("application/x-cenc",
      "mpegversion", G_TYPE_INT, 4,
      "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 48000,
      "original-media-type", G_TYPE_STRING, "audio/mpeg",
      "protection-system", G_TYPE_STRING,
      "9a04f079-9840-4286-ab92-e65be0885f95", NULL);
  caps =
      gst_caps_new_simple ("video/quicktime",
      "variant", G_TYPE_STRING, "mss-fragmented",
      "timescale", G_TYPE_UINT64, G_GUINT64_CONSTANT (10000000),
      "media-caps", GST_TYPE_CAPS, mediacaps, NULL);

  /* Send segment event* */
  event = gst_event_new_caps (caps);
  GST_DEBUG ("Pushing caps event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_caps_unref (mediacaps);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send the first fragment */
  /* NOTE: mss streams don't have moov */
  inbuf = gst_buffer_new_and_alloc (seg_1_moof_size);
  gst_buffer_fill (inbuf, 0, seg_1_m4f, seg_1_moof_size);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing audio fragment");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);

  gst_object_unref (sinkpad);
  gst_element_set_state (qtdemux_a, GST_STATE_NULL);
  gst_object_unref (qtdemux_a);
  g_free (expected_audio_pad_name);

  unload_files ();
}

GST_END_TEST;

typedef struct
{
  GstPad *sinkpad;
  guint sample_cnt;
  guint expected_sample_cnt;
} MssModeTestData;

static GstPadProbeReturn
qtdemux_probe_for_mss_mode (GstPad * pad, GstPadProbeInfo * info,
    MssModeTestData * data)
{
  data->sample_cnt++;
  GST_LOG ("samples received: %u", data->sample_cnt);
  return GST_PAD_PROBE_OK;
}

static void
qtdemux_pad_added_cb_in_mss_mode (GstElement * element, GstPad * pad,
    MssModeTestData * data)
{
  GST_DEBUG_OBJECT (pad, "New pad added");

  if (!data->sinkpad) {
    GstPad *sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");

    gst_pad_set_event_function (sinkpad, _sink_event);
    gst_pad_set_chain_function (sinkpad, _sink_chain);

    gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) qtdemux_probe_for_mss_mode, data, NULL);
    gst_pad_set_active (sinkpad, TRUE);
    data->sinkpad = sinkpad;
    gst_pad_link (pad, sinkpad);
  }
}

/* Fragment taken from
 * http://amssamples.streaming.mediaservices.windows.net/b6822ec8-5c2b-4ae0-a851-fd46a78294e9/ElephantsDream.ism/QualityLevels(53644)/Fragments(AAC_und_ch2_56kbps=0)
 */
#define MSS_FRAGMENT TEST_FILE_PREFIX "mss-fragment.m4f"
static guint8 *mss_fragment;
static const guint mss_fragment_len = 14400;

GST_START_TEST (test_qtdemux_compensate_data_offset)
{
  /* Same fragment as above, but with modified trun box data offset field
   * from 871 to 791 to mimic an mss fragment with data offset smaller
   * than the moof size. */
  const guint mss_fragment_wrong_data_offset_len = 14400;
  guint8 *mss_fragment_wrong_data_offset;
  GstElement *qtdemux;
  GstPad *sinkpad;
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  GstCaps *caps;
  GstCaps *mediacaps;
  MssModeTestData data = { 0, };

  data.expected_sample_cnt = 87;

  g_assert (load_file (MSS_FRAGMENT, &mss_fragment, mss_fragment_len));

  /* Change trun box data offset field from 871 to 791 to mimic an MSS fragment
   * with data offset smaller than the moof size. */
  mss_fragment_wrong_data_offset = g_memdup2 (mss_fragment, mss_fragment_len);
  g_assert (GST_READ_UINT32_BE (&mss_fragment_wrong_data_offset[64]) == 871);
  GST_WRITE_UINT32_BE (&mss_fragment_wrong_data_offset[64], 791);

  /* The goal of this test is to check that qtdemux can compensate
   * wrong data offset in trun boxes and allow proper parsing of samples
   * in mss mode.
   */

  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux, "pad-added", (GCallback)
      qtdemux_pad_added_cb_in_mss_mode, &data);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send CAPS event* */
  mediacaps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "channels", G_TYPE_INT, 2, "rate", G_TYPE_INT, 48000, NULL);
  caps =
      gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING,
      "mss-fragmented", "timescale", G_TYPE_UINT64, 10000000, "media-caps",
      GST_TYPE_CAPS, mediacaps, NULL);

  /* Send segment event* */
  event = gst_event_new_caps (caps);
  GST_DEBUG ("Pushing caps event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_caps_unref (mediacaps);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send the first fragment */
  /* NOTE: mss streams don't have moov */
  inbuf = gst_buffer_new_and_alloc (mss_fragment_wrong_data_offset_len);
  gst_buffer_fill (inbuf, 0, mss_fragment_wrong_data_offset,
      mss_fragment_wrong_data_offset_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing fragment");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);

  /* If data offset has been compensated samples will be pushed as normal */
  fail_unless (data.sample_cnt == data.expected_sample_cnt);

  gst_object_unref (sinkpad);
  gst_pad_set_active (data.sinkpad, FALSE);
  gst_object_unref (data.sinkpad);
  gst_element_set_state (qtdemux, GST_STATE_NULL);
  gst_object_unref (qtdemux);

  g_free (mss_fragment_wrong_data_offset);
  g_clear_pointer (&mss_fragment, (GDestroyNotify) g_free);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_mss_fragment)
{
  GstElement *qtdemux;
  GstPad *sinkpad;
  GstBuffer *inbuf;
  GstSegment segment;
  GstEvent *event;
  GstCaps *caps;
  GstCaps *mediacaps;
  MssModeTestData data = { 0, };

  data.expected_sample_cnt = 87;

  g_assert (load_file (MSS_FRAGMENT, &mss_fragment, mss_fragment_len));

  /* The goal of this test is to check that qtdemux can handle a normal
   * mss fragment.
   */

  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  gst_element_set_state (qtdemux, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (qtdemux, "sink");

  /* We'll want to know when the source pad is added */
  g_signal_connect (qtdemux, "pad-added", (GCallback)
      qtdemux_pad_added_cb_in_mss_mode, &data);

  /* Send the initial STREAM_START and segment (TIME) event */
  event = gst_event_new_stream_start ("TEST");
  GST_DEBUG ("Pushing stream-start event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send CAPS event* */
  mediacaps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "channels", G_TYPE_INT, 2, "rate", G_TYPE_INT, 48000, NULL);
  caps =
      gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING,
      "mss-fragmented", "timescale", G_TYPE_UINT64, 10000000, "media-caps",
      GST_TYPE_CAPS, mediacaps, NULL);

  /* Send segment event* */
  event = gst_event_new_caps (caps);
  GST_DEBUG ("Pushing caps event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);
  gst_caps_unref (mediacaps);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  GST_DEBUG ("Pushing segment event");
  fail_unless (gst_pad_send_event (sinkpad, event) == TRUE);

  /* Send the first fragment */
  /* NOTE: mss streams don't have moov */
  inbuf = gst_buffer_new_and_alloc (mss_fragment_len);
  gst_buffer_fill (inbuf, 0, mss_fragment, mss_fragment_len);
  GST_BUFFER_PTS (inbuf) = 0;
  GST_BUFFER_OFFSET (inbuf) = 0;
  GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  GST_DEBUG ("Pushing fragment");
  fail_unless (gst_pad_chain (sinkpad, inbuf) == GST_FLOW_OK);
  fail_if (data.sinkpad == NULL);

  fail_unless (data.sample_cnt == data.expected_sample_cnt);

  gst_object_unref (sinkpad);
  gst_pad_set_active (data.sinkpad, FALSE);
  gst_object_unref (data.sinkpad);
  gst_element_set_state (qtdemux, GST_STATE_NULL);
  gst_object_unref (qtdemux);

  g_clear_pointer (&mss_fragment, (GDestroyNotify) g_free);
}

GST_END_TEST;

typedef struct
{
  const gchar *filename;
  /* Total number of AAC frames, including any and all dummy/empty/padding frames. */
  guint num_aac_frames;
  /* In AAC, this is 1024 in the vast majority of the cases.
   * AAC can also use 960 samples per frame, but this is rare. */
  guint num_samples_per_frame;
  /* How many padding samples to expect at the beginning and the end.
   * The amount of padding samples can exceed the size of a frame.
   * This means that the first and last N frame(s) can actually be
   * fully made of padding samples and thus need to be thrown away. */
  guint num_start_padding_samples;
  guint num_end_padding_samples;
  guint sample_rate;
  /* Some encoders produce data whose last frame uses a different
   * (smaller) stts value to handle the padding at the end. Data
   * produced by such encoders will not get a clipmeta added at the
   * end. When using test data produced by such an encoder, this
   * must be set to FALSE, otherwise it must be set to TRUE.
   * Notably, anything that produces an iTunSMPB tag (iTunes itself
   * as well as newer Nero encoders for example) will cause such
   * a clipmeta to be added. */
  gboolean expect_clipmeta_at_end;

  /* Total number of samples available, with / without padding
   * samples factored in. */
  guint64 num_samples_with_padding;
  guint64 num_samples_without_padding;

  /* The index of the first / last frame that contains valid samples.
   * Indices start with 0. Valid range is [0 , (num_aac_frames-1)].
   * In virtually all cases, when the AAC data was encoded with iTunes,
   * the first and last valid frames will be partially clipped. */
  guint first_frame_with_valid_samples;
  guint last_frame_with_valid_samples;

  guint64 num_samples_in_first_valid_frame;
  guint64 num_samples_in_last_valid_frame;

  GstClockTime total_duration_without_padding;

  GstElement *appsink;
} GaplessTestInfo;

static void
precalculate_gapless_test_factors (GaplessTestInfo * info)
{
  info->num_samples_with_padding = info->num_aac_frames *
      info->num_samples_per_frame;
  info->num_samples_without_padding = info->num_samples_with_padding -
      info->num_start_padding_samples - info->num_end_padding_samples;

  info->first_frame_with_valid_samples = info->num_start_padding_samples /
      info->num_samples_per_frame;
  info->last_frame_with_valid_samples = (info->num_samples_with_padding -
      info->num_end_padding_samples) / info->num_samples_per_frame;

  info->num_samples_in_first_valid_frame =
      (info->first_frame_with_valid_samples + 1) * info->num_samples_per_frame -
      info->num_start_padding_samples;
  info->num_samples_in_last_valid_frame =
      (info->num_samples_with_padding - info->num_end_padding_samples) -
      info->last_frame_with_valid_samples * info->num_samples_per_frame;

  /* The total actual playtime duration. */
  info->total_duration_without_padding =
      gst_util_uint64_scale_int (info->num_samples_without_padding, GST_SECOND,
      info->sample_rate);

  GST_DEBUG ("num_samples_with_padding %" G_GUINT64_FORMAT
      " num_samples_without_padding %" G_GUINT64_FORMAT
      " first_frame_with_valid_samples %u"
      " last_frame_with_valid_samples %u"
      " num_samples_in_first_valid_frame %" G_GUINT64_FORMAT
      " num_samples_in_last_valid_frame %" G_GUINT64_FORMAT
      " total_duration_without_padding %" G_GUINT64_FORMAT,
      info->num_samples_with_padding, info->num_samples_without_padding,
      info->first_frame_with_valid_samples, info->last_frame_with_valid_samples,
      info->num_samples_in_first_valid_frame,
      info->num_samples_in_last_valid_frame,
      info->total_duration_without_padding);
}

static void
setup_gapless_itunes_test_info (GaplessTestInfo * info)
{
  info->filename =
      "sine-1kHztone-48kHzrate-mono-s32le-200000samples-itunes.m4a";
  info->num_aac_frames = 198;
  info->num_samples_per_frame = 1024;
  info->sample_rate = 48000;
  info->expect_clipmeta_at_end = TRUE;

  info->num_start_padding_samples = 2112;
  info->num_end_padding_samples = 640;

  precalculate_gapless_test_factors (info);
}

static void
setup_gapless_nero_with_itunsmpb_test_info (GaplessTestInfo * info)
{
  info->filename =
      "sine-1kHztone-48kHzrate-mono-s32le-200000samples-nero-with-itunsmpb.m4a";
  info->num_aac_frames = 198;
  info->num_samples_per_frame = 1024;
  info->sample_rate = 48000;
  info->expect_clipmeta_at_end = TRUE;

  info->num_start_padding_samples = 2624;
  info->num_end_padding_samples = 128;

  precalculate_gapless_test_factors (info);
}

static void
setup_gapless_nero_without_itunsmpb_test_info (GaplessTestInfo * info)
{
  info->filename =
      "sine-1kHztone-48kHzrate-mono-s32le-200000samples-nero-without-itunsmpb.m4a";
  info->num_aac_frames = 198;
  info->num_samples_per_frame = 1024;
  info->sample_rate = 48000;
  /* Older Nero AAC encoders produce a different stts value for the
   * last frame to skip padding data. In this file, all frames except
   * the last one use an stts value of 1024, while the last value
   * uses an stts value of 896. Consequently, the logic inside qtdemux
   * won't deem it necessary to add an audioclipmeta - there are no
   * padding samples to clip. */
  info->expect_clipmeta_at_end = FALSE;

  info->num_start_padding_samples = 2624;
  info->num_end_padding_samples = 128;

  precalculate_gapless_test_factors (info);
}

static void
check_parsed_aac_frame (GaplessTestInfo * info, guint frame_num)
{
  GstClockTime expected_pts = GST_CLOCK_TIME_NONE;
  GstClockTime expected_duration = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff ts_delta;
  guint64 expected_sample_offset;
  guint64 expected_num_samples;
  gboolean expect_audioclipmeta = FALSE;
  guint64 expected_audioclipmeta_start = 0;
  guint64 expected_audioclipmeta_end = 0;
  GstSample *sample;
  GstBuffer *buffer;
  GstAudioClippingMeta *audioclip_meta;

  if (frame_num < info->first_frame_with_valid_samples) {
    /* Frame is at the beginning and is fully clipped. */
    expected_sample_offset = 0;
    expected_num_samples = 0;

    expected_audioclipmeta_start = info->num_samples_per_frame;
    expected_audioclipmeta_end = 0;
  } else if (frame_num == info->first_frame_with_valid_samples) {
    /* Frame is at the beginning and is partially clipped. */

    expected_sample_offset = 0;
    expected_num_samples = info->num_samples_in_first_valid_frame;

    expected_audioclipmeta_start = info->num_samples_per_frame -
        info->num_samples_in_first_valid_frame;
    expected_audioclipmeta_end = 0;
  } else if (frame_num < info->last_frame_with_valid_samples) {
    /* Regular, unclipped frame. */

    expected_sample_offset = info->num_samples_in_first_valid_frame +
        info->num_samples_per_frame * (frame_num -
        info->first_frame_with_valid_samples - 1);
    expected_num_samples = info->num_samples_per_frame;
  } else if (frame_num == info->last_frame_with_valid_samples) {
    /* The first frame at the end with padding samples. This one will have
     * the last few valid samples, followed by the first padding samples. */

    expected_sample_offset = info->num_samples_in_first_valid_frame +
        info->num_samples_per_frame * (frame_num -
        info->first_frame_with_valid_samples - 1);
    expected_num_samples = info->num_samples_in_last_valid_frame;

    if (info->expect_clipmeta_at_end) {
      expect_audioclipmeta = TRUE;
      expected_audioclipmeta_start = 0;
      expected_audioclipmeta_end =
          info->num_samples_per_frame - expected_num_samples;
    }
  } else {
    /* A fully clipped frame at the end of the stream. */

    expected_sample_offset = info->num_samples_in_first_valid_frame +
        info->num_samples_without_padding;
    expected_num_samples = 0;

    if (info->expect_clipmeta_at_end) {
      expect_audioclipmeta = TRUE;
      expected_audioclipmeta_start = 0;
      expected_audioclipmeta_end = info->num_samples_per_frame;
    }
  }

  /* Pull the frame from appsink so we can check it. */

  sample = gst_app_sink_pull_sample (GST_APP_SINK (info->appsink));
  fail_if (sample == NULL);
  fail_unless (GST_IS_SAMPLE (sample));

  expected_pts = gst_util_uint64_scale_int (expected_sample_offset,
      GST_SECOND, info->sample_rate);
  expected_duration = gst_util_uint64_scale_int (expected_num_samples,
      GST_SECOND, info->sample_rate);

  buffer = gst_sample_get_buffer (sample);
  fail_if (buffer == NULL);

  /* Verify the sample's PTS and duration. Allow for 1 nanosecond difference
   * to account for rounding errors in sample <-> timestamp conversions. */
  ts_delta = GST_CLOCK_DIFF (GST_BUFFER_PTS (buffer), expected_pts);
  fail_unless (ABS (ts_delta) <= 1);
  ts_delta = GST_CLOCK_DIFF (GST_BUFFER_DURATION (buffer), expected_duration);
  fail_unless (ABS (ts_delta) <= 1);
  /* Check if there's audio clip metadata, and verify it if it exists. */
  if (expect_audioclipmeta) {
    audioclip_meta = gst_buffer_get_audio_clipping_meta (buffer);
    fail_if (audioclip_meta == NULL);
    fail_unless_equals_uint64 (audioclip_meta->start,
        expected_audioclipmeta_start);
    fail_unless_equals_uint64 (audioclip_meta->end, expected_audioclipmeta_end);
  }

  gst_sample_unref (sample);
}

static void
qtdemux_pad_added_cb_for_gapless (GstElement * demux, GstPad * pad,
    GaplessTestInfo * info)
{
  GstPad *appsink_pad;
  GstPadLinkReturn ret;

  appsink_pad = gst_element_get_static_pad (info->appsink, "sink");

  if (gst_pad_is_linked (appsink_pad))
    goto finish;

  ret = gst_pad_link (pad, appsink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    GST_ERROR ("Could not link qtdemux and appsink: %s",
        gst_pad_link_get_name (ret));
  }

finish:
  gst_object_unref (GST_OBJECT (appsink_pad));
}

static void
switch_state_with_async_wait (GstElement * pipeline, GstState state)
{
  GstStateChangeReturn state_ret;

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  if (state_ret == GST_STATE_CHANGE_ASYNC) {
    GST_LOG ("waiting for pipeline to reach %s state",
        gst_element_state_get_name (state));
    state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
    fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
  }
}

static void
perform_gapless_test (GaplessTestInfo * info)
{
  GstElement *source, *demux, *appsink, *pipeline;
  guint frame_num;

  pipeline = gst_pipeline_new (NULL);
  source = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  appsink = gst_element_factory_make ("appsink", NULL);

  info->appsink = appsink;

  g_signal_connect (demux, "pad-added", (GCallback)
      qtdemux_pad_added_cb_for_gapless, info);

  gst_bin_add_many (GST_BIN (pipeline), source, demux, appsink, NULL);
  gst_element_link (source, demux);

  {
    char *full_filename =
        g_build_filename (GST_TEST_FILES_PATH, info->filename, NULL);
    g_object_set (G_OBJECT (source), "location", full_filename, NULL);
    g_free (full_filename);
  }

  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);

  switch_state_with_async_wait (pipeline, GST_STATE_PLAYING);

  /* Verify all frames from the test signal. */
  for (frame_num = 0; frame_num < info->num_aac_frames; ++frame_num)
    check_parsed_aac_frame (info, frame_num);

  /* Check what duration is returned by a query. This duration must exclude
   * the padding samples. */
  {
    GstQuery *query;
    gint64 duration;
    GstFormat format;

    query = gst_query_new_duration (GST_FORMAT_TIME);
    fail_unless (gst_element_query (pipeline, query));

    gst_query_parse_duration (query, &format, &duration);
    fail_unless_equals_int (format, GST_FORMAT_TIME);
    fail_unless_equals_uint64 ((guint64) duration,
        info->total_duration_without_padding);

    gst_query_unref (query);
  }

  /* Seek tests: Here we seek to a certain position that corresponds to a
   * certain frame. Then we check if we indeed got that frame. */

  /* Seek back to the first frame. This will _not_ be the first valid frame.
   * Instead, it will be a frame that gets only decoded and has duration
   * zero. Other zero-duration frames may follow, until the first frame
   * with valid data is encountered. This means that when the user seeks
   * to position 0, downstream will subsequently get a number of buffers
   * with PTS 0, and all of those buffers except the last will have a
   * duration of 0. */
  {
    switch_state_with_async_wait (pipeline, GST_STATE_PAUSED);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0);
    switch_state_with_async_wait (pipeline, GST_STATE_PLAYING);

    check_parsed_aac_frame (info, 0);
  }

  /* Now move to the frame past the very first one that contained valid samples.
   * This very first frame will usually be clipped, and be output as the last
   * buffer at PTS 0 (see above). */
  {
    GstClockTime position;

    position =
        gst_util_uint64_scale_int (info->num_samples_in_first_valid_frame,
        GST_SECOND, info->sample_rate);

    switch_state_with_async_wait (pipeline, GST_STATE_PAUSED);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
        position);
    switch_state_with_async_wait (pipeline, GST_STATE_PLAYING);

    check_parsed_aac_frame (info, info->first_frame_with_valid_samples + 1);
  }

  /* Seek to the last frame with valid samples (= the first frame with padding
   * samples at the end of the stream). */
  {
    GstClockTime position;

    position =
        gst_util_uint64_scale_int (info->num_samples_in_first_valid_frame +
        info->num_samples_without_padding - info->num_samples_per_frame,
        GST_SECOND, info->sample_rate);

    switch_state_with_async_wait (pipeline, GST_STATE_PAUSED);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
        position);
    switch_state_with_async_wait (pipeline, GST_STATE_PLAYING);

    check_parsed_aac_frame (info, info->last_frame_with_valid_samples);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_qtdemux_gapless_itunes_data)
{
  GaplessTestInfo info;
  setup_gapless_itunes_test_info (&info);
  perform_gapless_test (&info);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_gapless_nero_data_with_itunsmpb)
{
  GaplessTestInfo info;
  setup_gapless_nero_with_itunsmpb_test_info (&info);
  perform_gapless_test (&info);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_gapless_nero_data_without_itunsmpb)
{
  GaplessTestInfo info;
  setup_gapless_nero_without_itunsmpb_test_info (&info);
  perform_gapless_test (&info);
}

GST_END_TEST;

GST_START_TEST (test_qtdemux_editlist)
{
  const gsize editlist_mp4_size = 5322593;
  guint8 *editlist_mp4 = NULL;
  GstElement *src, *sink, *pipe;
  GstSample *sample;
  guint frame_count = 0;

  {
    GZlibDecompressor *decompress;
    GConverterResult decomp_res;
    gsize bytes_read, gz_size, mp4_size;
    guint8 *gz_gz = NULL;
    guint8 gz[8705];

    /* read .mp4.gz.gz */
    g_assert (load_file (TEST_FILE_PREFIX "editlists.mp4.gz.gz", &gz_gz, 3597));

    /* mp4.gz.gz -> mp4.gz */
    decompress = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    decomp_res = g_converter_convert (G_CONVERTER (decompress), gz_gz, 3597,
        gz, 8705, G_CONVERTER_INPUT_AT_END, &bytes_read, &gz_size, NULL);
    fail_unless_equals_int (decomp_res, G_CONVERTER_FINISHED);
    fail_unless_equals_int (bytes_read, 3597);
    fail_unless_equals_int (gz_size, 8705);
    g_object_unref (decompress);
    g_clear_pointer (&gz_gz, (GDestroyNotify) g_free);

    editlist_mp4 = g_malloc0 (editlist_mp4_size);

    /* mp4.gz -> mp4 */
    decompress = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    decomp_res = g_converter_convert (G_CONVERTER (decompress), gz, 8705,
        editlist_mp4, editlist_mp4_size, G_CONVERTER_INPUT_AT_END, &bytes_read,
        &mp4_size, NULL);
    fail_unless_equals_int (decomp_res, G_CONVERTER_FINISHED);
    fail_unless_equals_int (bytes_read, 8705);
    fail_unless_equals_int (mp4_size, editlist_mp4_size);
    g_object_unref (decompress);
  }

  fail_unless_equals_int (editlist_mp4[28 + 4], 'm');
  fail_unless_equals_int (editlist_mp4[28 + 5], 'd');
  fail_unless_equals_int (editlist_mp4[28 + 6], 'a');
  fail_unless_equals_int (editlist_mp4[28 + 7], 't');

  pipe = gst_parse_launch ("dataurisrc name=src ! qtdemux name=d "
      "d.video_0 ! appsink name=sink", NULL);

  fail_unless (pipe != NULL);

  src = gst_bin_get_by_name (GST_BIN (pipe), "src");
  fail_unless (src != NULL);

  sink = gst_bin_get_by_name (GST_BIN (pipe), "sink");
  fail_unless (sink != NULL);

  /* Convert to data: URI so we can use dataurisrc. Bit silly of course,
   * should have a memsrc or somesuch, but does the job for now */
  {
    gsize s_alloc_len = 32 + (editlist_mp4_size / 3 + 1) * 4 + 4;
    gchar *s = g_malloc0 (s_alloc_len);
    gsize s_len = 0;
    gsize base64_size;
    gint state = 0;
    gint save = 0;

    s_len = g_strlcat (s, "data:video/quicktime;base64,", s_alloc_len);

    base64_size =
        g_base64_encode_step (editlist_mp4, editlist_mp4_size, FALSE, s + s_len,
        &state, &save);
    s_len += base64_size;
    base64_size = g_base64_encode_close (FALSE, s + s_len, &state, &save);
    s_len += base64_size;
    g_clear_pointer (&editlist_mp4, (GDestroyNotify) g_free);

    {
      GValue v = G_VALUE_INIT;

      /* Avoids at least one of the two string copies */
      g_value_init (&v, G_TYPE_STRING);
      g_value_take_string (&v, s);
      g_object_set_property (G_OBJECT (src), "uri", &v);
      g_value_reset (&v);
    }
  }

  g_object_set (sink, "sync", FALSE, NULL);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  /* wait for preroll */
  {
    GstMessage *msg;

    GST_LOG ("waiting for preroll");
    msg =
        gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1,
        GST_MESSAGE_ASYNC_DONE);

    gst_message_unref (msg);
  }

  /* pull video frames out of qtdemux */
  while ((sample = gst_app_sink_pull_sample (GST_APP_SINK (sink)))) {
    ++frame_count;
    gst_sample_unref (sample);
  }

  fail_unless_equals_int (frame_count, 361);

  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_clear_object (&src);
  gst_clear_object (&sink);
  gst_clear_object (&pipe);
}

GST_END_TEST;

static Suite *
qtdemux_suite (void)
{
  Suite *s = suite_create ("qtdemux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_qtdemux_fuzzed0);
  tcase_add_test (tc_chain, test_qtdemux_fuzzed1);
  tcase_add_test (tc_chain, test_qtdemux_input_gap);
  tcase_add_test (tc_chain, test_qtdemux_duplicated_moov);
  tcase_add_test (tc_chain, test_qtdemux_stream_change);
  tcase_add_test (tc_chain, test_qtdemux_pad_names);
  tcase_add_test (tc_chain, test_qtdemux_compensate_data_offset);
  tcase_add_test (tc_chain, test_qtdemux_mss_fragment);
  tcase_add_test (tc_chain, test_qtdemux_gapless_itunes_data);
  tcase_add_test (tc_chain, test_qtdemux_gapless_nero_data_with_itunsmpb);
  tcase_add_test (tc_chain, test_qtdemux_gapless_nero_data_without_itunsmpb);
  tcase_add_test (tc_chain, test_qtdemux_editlist);

  return s;
}

GST_CHECK_MAIN (qtdemux)
