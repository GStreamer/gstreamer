/* GStreamer
 *
 * unit test for scaletempo
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
#include <gst/check/gstharness.h>
#include <gst/audio/audio.h>

#define RATE 44100
#define CHANNELS 2
#define BPF (CHANNELS * 2)
/* well below one 30 ms stride */
#define SMALL_FRAMES 64
#define N_BUFFERS 200

static GstHarness *
setup_scaletempo (gdouble rate, const gchar * mode)
{
  GstHarness *h;
  GstAudioInfo info;
  GstSegment segment;

  h = gst_harness_new ("scaletempo");
  if (mode)
    gst_util_set_object_arg (G_OBJECT (h->element), "mode", mode);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, RATE, CHANNELS, NULL);
  gst_harness_set_src_caps (h, gst_audio_info_to_caps (&info));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = rate;
  if (rate < 0)
    segment.stop = 10 * GST_SECOND;
  fail_unless (gst_harness_push_event (h, gst_event_new_segment (&segment)));

  return h;
}

static GstBuffer *
create_buffer (guint64 offset, guint frames)
{
  GstBuffer *buf = gst_buffer_new_and_alloc (frames * BPF);

  gst_buffer_memset (buf, 0, 0, frames * BPF);
  GST_BUFFER_PTS (buf) = gst_util_uint64_scale_int (offset, GST_SECOND, RATE);
  GST_BUFFER_DURATION (buf) =
      gst_util_uint64_scale_int (frames, GST_SECOND, RATE);
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + frames;

  return buf;
}

static void
check_small_buffers (gdouble rate, const gchar * mode)
{
  GstHarness *h = setup_scaletempo (rate, mode);
  GstBuffer *buf;
  guint i, received = 0;

  for (i = 0; i < N_BUFFERS; i++) {
    buf = create_buffer (i * SMALL_FRAMES, SMALL_FRAMES);
    if (i == 0)
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    if (mode) {
      GstCustomMeta *meta =
          gst_buffer_add_custom_meta (buf, "GstScaletempoTargetDurationMeta");
      gst_structure_set (gst_custom_meta_get_structure (meta), "duration",
          G_TYPE_UINT64, GST_BUFFER_DURATION (buf) / 2, NULL);
    }
    fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

    while ((buf = gst_harness_try_pull (h))) {
      fail_if (gst_buffer_get_size (buf) == 0);
      fail_unless (GST_BUFFER_DURATION_IS_VALID (buf));
      /* the initial DISCONT must survive withheld inputs, which themselves
       * must not mark the next output DISCONT */
      fail_unless_equals_int (GST_BUFFER_IS_DISCONT (buf), received == 0);
      received++;
      gst_buffer_unref (buf);
    }
  }

  /* scaletempo withheld some inputs, but not all of them */
  fail_unless (received > 0);
  fail_unless (received < N_BUFFERS);

  gst_harness_teardown (h);
}

GST_START_TEST (test_small_buffers_speedup)
{
  check_small_buffers (2.0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_small_buffers_slowdown)
{
  check_small_buffers (0.5, NULL);
}

GST_END_TEST;

GST_START_TEST (test_small_buffers_reverse)
{
  check_small_buffers (-2.0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_small_buffers_fit_down)
{
  check_small_buffers (1.0, "fit-down");
}

GST_END_TEST;

/* Pushes small buffers starting at *offset until one output comes out */
static GstBuffer *
push_until_output (GstHarness * h, guint64 * offset)
{
  GstBuffer *buf = NULL;
  guint i;

  for (i = 0; i < 100 && !buf; i++) {
    fail_unless_equals_int (gst_harness_push (h,
            create_buffer (*offset, SMALL_FRAMES)), GST_FLOW_OK);
    *offset += SMALL_FRAMES;
    buf = gst_harness_try_pull (h);
  }
  fail_unless (buf != NULL);

  return buf;
}

GST_START_TEST (test_discont_on_withheld_buffer)
{
  GstHarness *h = setup_scaletempo (2.0, NULL);
  GstBuffer *buf;
  guint64 offset = 0;

  gst_buffer_unref (push_until_output (h, &offset));

  buf = create_buffer (offset, SMALL_FRAMES);
  offset += SMALL_FRAMES;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  buf = push_until_output (h, &offset);
  fail_unless (GST_BUFFER_IS_DISCONT (buf));

  gst_buffer_unref (buf);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_passthrough_empty_gap_buffer)
{
  GstHarness *h = setup_scaletempo (1.0, NULL);
  GstBuffer *in, *out;

  in = create_buffer (0, 0);
  GST_BUFFER_FLAG_SET (in, GST_BUFFER_FLAG_GAP);
  gst_buffer_ref (in);
  fail_unless_equals_int (gst_harness_push (h, in), GST_FLOW_OK);

  out = gst_harness_try_pull (h);
  fail_unless (out == in);

  gst_clear_buffer (&out);
  gst_buffer_unref (in);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
scaletempo_suite (void)
{
  Suite *s = suite_create ("scaletempo");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_small_buffers_speedup);
  tcase_add_test (tc_chain, test_small_buffers_slowdown);
  tcase_add_test (tc_chain, test_small_buffers_reverse);
  tcase_add_test (tc_chain, test_small_buffers_fit_down);
  tcase_add_test (tc_chain, test_discont_on_withheld_buffer);
  tcase_add_test (tc_chain, test_passthrough_empty_gap_buffer);

  return s;
}

GST_CHECK_MAIN (scaletempo);
