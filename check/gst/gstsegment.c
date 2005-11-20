/* GStreamer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * gstsegment.c: Unit test for segments
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

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_nosize)
{
  GstSegment segment;
  gboolean res;
  gint64 cstart, cstop;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  /* configure segment to start 100 */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET, 100, GST_SEEK_TYPE_NONE, -1);
  fail_unless (segment.start == 100);
  fail_unless (segment.stop == -1);

  /* configure segment to stop relative, should not do anything since 
   * size is unknown. */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_NONE, 200, GST_SEEK_TYPE_CUR, -100);
  fail_unless (segment.start == 100);
  fail_unless (segment.stop == -1);

  /* do some clipping on the open range */
  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 200, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 200);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == -1);

  /* add 100 to start, set stop to 300 */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_CUR, 100, GST_SEEK_TYPE_SET, 300);
  fail_unless (segment.start == 200);
  fail_unless (segment.stop == 300);

  /* add 100 to start (to 300), set stop to 200, this is not allowed. 
   * nothing should be updated in the segment. */
  ASSERT_CRITICAL (gst_segment_set_seek (&segment, 1.0,
          GST_FORMAT_BYTES,
          GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_CUR, 100, GST_SEEK_TYPE_SET, 200));
  fail_unless (segment.start == 200);
  fail_unless (segment.stop == 300);

  /* seek relative to end, should not do anything since size is
   * unknown. */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_END, -300, GST_SEEK_TYPE_END, -100);
  fail_unless (segment.start == 200);
  fail_unless (segment.stop == 300);

  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 200, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 250);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      200, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 250);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      250, 290, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 290);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      250, 350, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 300);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 300);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 200, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 300);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 250, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 300);

  /* start outside on boundary */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 300, -1, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 350, -1, &cstart, &cstop);
  fail_unless (res == FALSE);
}

GST_END_TEST;

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_size)
{
  GstSegment segment;
  gboolean res;
  gint64 cstart, cstop;

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_segment_set_duration (&segment, GST_FORMAT_BYTES, 200);

  /* configure segment to start 100 */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET, 100, GST_SEEK_TYPE_NONE, -1);
  fail_unless (segment.start == 100);
  fail_unless (segment.stop == -1);

  /* configure segment to stop relative, does not update stop
   * since we did not set it before. */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_NONE, 200, GST_SEEK_TYPE_CUR, -100);
  fail_unless (segment.start == 100);
  fail_unless (segment.stop == -1);

  /* do some clipping on the open range */
  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 200, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 200);

  /* partially inside, clip to size */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 300, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 200);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == -1);

  /* add 100 to start, set stop to 300, stop clips to 200 */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_CUR, 100, GST_SEEK_TYPE_SET, 300);
  fail_unless (segment.start == 200);
  fail_unless (segment.stop == 200);

  /* add 100 to start (to 300), set stop to 200, this clips start
   * to duration */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_CUR, 100, GST_SEEK_TYPE_SET, 200);
  fail_unless (segment.start == 200);
  fail_unless (segment.stop == 200);

  /* seek relative to end */
  gst_segment_set_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_END, -100, GST_SEEK_TYPE_END, -20);
  fail_unless (segment.start == 100);
  fail_unless (segment.stop == 180);

  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 170, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 170);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 180);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 180);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 180);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 180);

  /* start outside on boundary */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 180, -1, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 250, -1, &cstart, &cstop);
  fail_unless (res == FALSE);
}

GST_END_TEST;
Suite *
gstevents_suite (void)
{
  Suite *s = suite_create ("GstSegment");
  TCase *tc_chain = tcase_create ("segments");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, segment_seek_nosize);
  tcase_add_test (tc_chain, segment_seek_size);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstevents_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
