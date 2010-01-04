/* GStreamer unit tests for the deinterlace element
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
# include "config.h"
#endif

#include <stdio.h>
#include <gst/check/gstcheck.h>

GST_START_TEST (test_create_and_unref)
{
  GstElement *deinterlace;

  deinterlace = gst_element_factory_make ("deinterlace", NULL);
  fail_unless (deinterlace != NULL);

  gst_element_set_state (deinterlace, GST_STATE_NULL);
  gst_object_unref (deinterlace);
}

GST_END_TEST;

static Suite *
deinterlace_suite (void)
{
  Suite *s = suite_create ("deinterlace");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_create_and_unref);

  return s;
}

GST_CHECK_MAIN (deinterlace);
