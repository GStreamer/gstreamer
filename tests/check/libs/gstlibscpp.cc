/* GStreamer
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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

#include <gst/gst.h>
#include <gst/check/check.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <gst/net/net.h>

/* we mostly just want to make sure that our library headers don't
 * contain anything a C++ compiler might not like */
GST_START_TEST (test_nothing)
{
  gst_init (NULL, NULL);
}

GST_END_TEST;

GST_START_TEST (test_init_macros)
{
  GstBitReader bit_reader = GST_BIT_READER_INIT (NULL, 0);
  GstByteReader byte_reader = GST_BYTE_READER_INIT (NULL, 0);

  fail_unless (bit_reader.data == NULL);
  fail_unless (byte_reader.data == NULL);
}

GST_END_TEST;

static Suite *
libscpp_suite (void)
{
  Suite *s = suite_create ("GstLibsCpp");
  TCase *tc_chain = tcase_create ("C++ libs header tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_nothing);
  tcase_add_test (tc_chain, test_init_macros);

  return s;
}

GST_CHECK_MAIN (libscpp);
