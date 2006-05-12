/* GStreamer
 *
 * unit test for video
 *
 * Copyright (C) <2006> Jan Schmidt <thaytan@mad.scientist.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

#include <gst/video/video.h>
#include <string.h>

GST_START_TEST (test_dar_calc)
{
  guint display_ratio_n, display_ratio_d;

  /* Ensure that various Display Ratio calculations are correctly done */
  /* video 768x576, par 16/15, display par 16/15 = 4/3 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 768, 576, 16, 15, 16, 15));
  fail_unless (display_ratio_n == 4 && display_ratio_d == 3);

  /* video 720x480, par 32/27, display par 1/1 = 16/9 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 720, 480, 32, 27, 1, 1));
  fail_unless (display_ratio_n == 16 && display_ratio_d == 9);

  /* video 360x288, par 533333/500000, display par 16/15 = 
   * dar 1599999/1600000 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 360, 288, 533333, 500000, 16, 15));
  fail_unless (display_ratio_n == 1599999 && display_ratio_d == 1280000);
}

GST_END_TEST;

Suite *
video_suite (void)
{
  Suite *s = suite_create ("video support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_dar_calc);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = video_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
