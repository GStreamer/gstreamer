/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
#include <gst/codecs/gsth264picture.h>

static void
picture_freed (gpointer data, GstMiniObject * object)
{
  guint *count = data;
  (*count)++;
}

static GstH264Dpb *
create_test_dpb (void)
{
  GstH264Dpb *dpb = gst_h264_dpb_new ();

  gst_h264_dpb_set_interlaced (dpb, TRUE);
  gst_h264_dpb_set_max_num_frames (dpb, 1);

  return dpb;
}

static void
add_pair (GstH264Dpb * dpb, guint frame_num, gboolean gap, guint * freed)
{
  GstH264Picture *first = gst_h264_picture_new ();
  GstH264Picture *second = gst_h264_picture_new ();

  first->field = GST_H264_PICTURE_FIELD_TOP_FIELD;
  second->field = GST_H264_PICTURE_FIELD_BOTTOM_FIELD;
  first->frame_num = frame_num;
  second->frame_num = frame_num;
  first->pic_order_cnt = 2 * frame_num;
  second->pic_order_cnt = gap ? first->pic_order_cnt : first->pic_order_cnt + 1;
  first->ref = GST_H264_PICTURE_REF_SHORT_TERM;
  second->ref = GST_H264_PICTURE_REF_SHORT_TERM;
  first->ref_pic = TRUE;
  second->ref_pic = TRUE;
  second->second_field = TRUE;
  second->other_field = first;
  second->nonexisting = gap;

  gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (first), picture_freed, freed);
  gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (second),
      picture_freed, freed);

  gst_h264_dpb_add (dpb, first);
  gst_h264_dpb_add (dpb, second);
}

GST_START_TEST (test_emergency_bump_field_pair)
{
  gboolean gap = __i__;
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *output;
  GstH264Picture *bump_pic;
  GstH264Picture *current = gst_h264_picture_new ();
  guint freed = 0;

  /* Output the pair and release the caller's reference. The DPB still keeps
   * both fields as references */
  add_pair (dpb, 1, gap, &freed);
  output = gst_h264_dpb_bump (dpb, FALSE);
  fail_unless (output);
  gst_h264_picture_unref (output);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 2);
  fail_unless_equals_int (freed, 0);

  /* Max DPB size is 1, and the current pair still occupies the slot.
   * There should be no free space to store new picture now */
  fail_if (gst_h264_dpb_has_empty_frame_buffer (dpb));

  /* A new reference picture needs the space occupied by the old pair */
  current->ref_pic = TRUE;
  current->pic_order_cnt = 4;
  fail_unless (gst_h264_dpb_needs_bump (dpb, current,
          GST_H264_DPB_BUMP_NORMAL_LATENCY));

  /* Remove both fields together without outputting the pair again */
  bump_pic = gst_h264_dpb_bump (dpb, FALSE);
  fail_if (bump_pic);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 0);
  fail_unless (gst_h264_dpb_has_empty_frame_buffer (dpb));

  /* Above bump will release both fields of the old pair */
  fail_unless_equals_int (freed, 2);

  gst_h264_picture_unref (current);
  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

GST_START_TEST (test_emergency_bump_with_pending_output)
{
  gboolean gap = __i__;
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *output;
  GstH264Picture *bump_pic;
  GstH264Picture *current = gst_h264_picture_new ();
  guint freed = 0;

  /* The pair has been output, but the caller still holds the output picture */
  add_pair (dpb, 1, gap, &freed);
  output = gst_h264_dpb_bump (dpb, FALSE);
  fail_unless (output);

  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 2);
  fail_unless_equals_int (freed, 0);

  /* Max DPB size is 1, and the current pair still occupies the slot.
   * There should be no free space to store new picture now */
  fail_if (gst_h264_dpb_has_empty_frame_buffer (dpb));

  /* A new reference picture needs the space occupied by the old pair */
  current->ref_pic = TRUE;
  current->pic_order_cnt = 4;
  fail_unless (gst_h264_dpb_needs_bump (dpb, current,
          GST_H264_DPB_BUMP_NORMAL_LATENCY));

  /* Remove both fields from the DPB without returning the output picture again.
   * Only the second field is freed because the caller still holds the first */
  bump_pic = gst_h264_dpb_bump (dpb, FALSE);
  fail_if (bump_pic);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 0);
  fail_unless (gst_h264_dpb_has_empty_frame_buffer (dpb));
  fail_unless_equals_int (freed, 1);

  /* The first field is freed when the caller releases it */
  gst_h264_picture_unref (output);
  fail_unless_equals_int (freed, 2);

  gst_h264_picture_unref (current);
  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

GST_START_TEST (test_drain_reference_field_pair)
{
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *output;
  guint freed = 0;

  /* Draining outputs the pair and removes both fields from the DPB */
  add_pair (dpb, 1, __i__, &freed);
  output = gst_h264_dpb_bump (dpb, TRUE);
  fail_unless (output);

  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 0);
  fail_unless_equals_int (freed, 1);

  /* The caller holds the first field until it releases the output picture */
  gst_h264_picture_unref (output);
  fail_unless_equals_int (freed, 2);

  output = gst_h264_dpb_bump (dpb, TRUE);
  fail_if (output);

  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

GST_START_TEST (test_emergency_bump_frame)
{
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *picture = gst_h264_picture_new ();
  GstH264Picture *bump_pic;
  guint freed = 0;

  gst_h264_dpb_set_interlaced (dpb, FALSE);
  picture->ref = GST_H264_PICTURE_REF_SHORT_TERM;
  picture->ref_pic = TRUE;
  picture->pic_order_cnt = 2;

  gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (picture),
      picture_freed, &freed);
  gst_h264_dpb_add (dpb, picture);

  /* The frame stays in the DPB as a reference after output */
  bump_pic = gst_h264_dpb_bump (dpb, FALSE);
  fail_unless (bump_pic);
  gst_h264_picture_unref (bump_pic);

  fail_unless_equals_int (freed, 0);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 1);

  /* Remove the frame without outputting it again */
  bump_pic = gst_h264_dpb_bump (dpb, FALSE);
  fail_unless (bump_pic == NULL);

  /* Above bump should evict the frame from DPB, and it will release
   * the last reference to the frame */
  fail_unless_equals_int (freed, 1);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 0);
  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

GST_START_TEST (test_single_emergency_eviction)
{
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *current = gst_h264_picture_new ();
  GstH264Picture *bump_pic;
  guint freed = 0;
  guint i;

  /* Output three different pairs and keep them in the DPB as references */
  gst_h264_dpb_set_max_num_frames (dpb, 3);
  for (i = 0; i < 3; i++) {
    add_pair (dpb, i + 1, __i__, &freed);

    bump_pic = gst_h264_dpb_bump (dpb, FALSE);
    fail_unless (bump_pic);

    gst_h264_picture_unref (bump_pic);
  }

  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 6);
  fail_unless_equals_int (freed, 0);

  /* Reduce the limit so that one eviction is not enough to make room */
  gst_h264_dpb_set_max_num_frames (dpb, 1);
  current->ref_pic = TRUE;
  current->pic_order_cnt = 8;

  /* Each call removes one pair and does not output it again */
  for (i = 0; i < 3; i++) {
    guint prev_size = gst_h264_dpb_get_size (dpb);
    GstH264Picture *bump_pic;
    guint prev_freed = freed;

    fail_unless (gst_h264_dpb_needs_bump (dpb, current,
            GST_H264_DPB_BUMP_NORMAL_LATENCY));

    bump_pic = gst_h264_dpb_bump (dpb, FALSE);
    fail_unless (bump_pic == NULL);

    /* Above bump will evict one pair from DPB without outputting it,
     * since the pair has already been outputted.
     * Expected size reduction by 2 only */
    fail_unless_equals_int (gst_h264_dpb_get_size (dpb), prev_size - 2);

    /* The number of freed pictures should have increased by 2 due to the
     * above bump */
    fail_unless_equals_int (freed, prev_freed + 2);
  }

  /* All three pairs have been removed */
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), 0);
  fail_unless (gst_h264_dpb_has_empty_frame_buffer (dpb));
  fail_if (gst_h264_dpb_needs_bump (dpb, current,
          GST_H264_DPB_BUMP_NORMAL_LATENCY));

  gst_h264_picture_unref (current);
  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

GST_START_TEST (test_bump_without_progress)
{
  GstH264Dpb *dpb = create_test_dpb ();
  GstH264Picture *picture = gst_h264_picture_new ();
  guint prev_size;

  picture->field = GST_H264_PICTURE_FIELD_TOP_FIELD;
  picture->ref = GST_H264_PICTURE_REF_SHORT_TERM;
  gst_h264_dpb_add (dpb, picture);
  prev_size = gst_h264_dpb_get_size (dpb);

  /* A field without its pair cannot be output or removed by bumping */
  fail_unless (gst_h264_dpb_bump (dpb, FALSE) == NULL);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), prev_size);
  fail_unless (gst_h264_dpb_bump (dpb, TRUE) == NULL);
  fail_unless_equals_int (gst_h264_dpb_get_size (dpb), prev_size);

  gst_h264_dpb_free (dpb);
}

GST_END_TEST;

static Suite *
h264picture_suite (void)
{
  Suite *s = suite_create ("h264picture");
  TCase *tc = tcase_create ("dpb");

  suite_add_tcase (s, tc);
  /* Run each field-pair test with a real second field and a gap field */
  tcase_add_loop_test (tc, test_emergency_bump_field_pair, 0, 2);
  tcase_add_loop_test (tc, test_emergency_bump_with_pending_output, 0, 2);
  tcase_add_loop_test (tc, test_drain_reference_field_pair, 0, 2);
  tcase_add_test (tc, test_emergency_bump_frame);
  tcase_add_loop_test (tc, test_single_emergency_eviction, 0, 2);
  tcase_add_test (tc, test_bump_without_progress);
  return s;
}

GST_CHECK_MAIN (h264picture);
