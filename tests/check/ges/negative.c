/* GStreamer Editing Services
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include <ges/ges.h>
#include <gst/check/gstcheck.h>
#include <signal.h>

static void
sigabrt_handler (int signum)
{
  /* expected abort */
  exit (0);
}

static gpointer
deinit_thread_func (gpointer user_data)
{
  signal (SIGABRT, sigabrt_handler);
  ges_deinit ();

  /* shouldn't be reached */
  exit (1);

  return NULL;
}

GST_START_TEST (test_inconsistent_init_deinit_thread)
{
  GThread *thread;

  fail_unless (ges_init ());

  /* test assertion, when trying to call ges_deinit() in a thread different
   * from that of ges_init() called.
   */
  thread = g_thread_new ("test-ges-deinit-thread",
      (GThreadFunc) deinit_thread_func, NULL);

  g_thread_join (thread);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-negative");
  TCase *tc_chain = tcase_create ("negative");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_inconsistent_init_deinit_thread);

  return s;
}

GST_CHECK_MAIN (ges);
