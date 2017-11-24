/* GStreamer
 * Copyright (C) <2011> Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/gstatomicqueue.h>
#include <gst/gst.h>

GST_START_TEST (test_create_free)
{
  GstAtomicQueue *aq;

  aq = gst_atomic_queue_new (20);
  gst_atomic_queue_unref (aq);
}

GST_END_TEST;

static Suite *
gst_atomic_queue_suite (void)
{
  Suite *s = suite_create ("GstAtomicQueue");
  TCase *tc_chain = tcase_create ("GstAtomicQueue tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_create_free);

  return s;
}

GST_CHECK_MAIN (gst_atomic_queue);
