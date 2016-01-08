/* GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/gl/gl.h>

#include <stdio.h>

static GstGLDisplay *display;
static GstGLContext *context;

GST_DEBUG_CATEGORY_STATIC (gst_test_debug_cat);

static void
setup (void)
{
  GError *error = NULL;

  display = gst_gl_display_new ();
  context = gst_gl_context_new (display);

  gst_gl_context_create (context, NULL, &error);

  fail_if (error != NULL, "Error creating context: %s\n",
      error ? error->message : "Unknown Error");
}

static void
teardown (void)
{
  gst_object_unref (display);
  gst_object_unref (context);
}

static void
_test_query_gl (GstGLContext * context, gpointer data)
{
  GstGLQuery *q1, q2;

  q1 = gst_gl_query_new (context, GST_GL_QUERY_TIME_ELAPSED);
  fail_if (q1 == NULL);

  gst_gl_query_start_log (q1, NULL, GST_LEVEL_ERROR, NULL, "%s",
      "1. testing query proxy-logging");
  gst_gl_query_end (q1);
  /* GST_GL_QUERY_TIME_ELAPSED doesn't supported counter() */
  ASSERT_CRITICAL (gst_gl_query_counter (q1));
  gst_gl_query_result (q1);

  gst_gl_query_free (q1);

  gst_gl_query_init (&q2, context, GST_GL_QUERY_TIMESTAMP);

  /* GST_GL_QUERY_TIMESTAMP doesn't supported start()/end() */
  ASSERT_CRITICAL (gst_gl_query_start (&q2));
  ASSERT_CRITICAL (gst_gl_query_end (&q2));

  gst_gl_query_counter_log (&q2, gst_test_debug_cat, GST_LEVEL_ERROR, NULL,
      "%s", "2. testing query proxy-logging works from _unset()");
  gst_gl_query_result (&q2);

  gst_gl_query_unset (&q2);

  /* no usage */
  gst_gl_query_init (&q2, context, GST_GL_QUERY_TIMESTAMP);
  gst_gl_query_unset (&q2);

  /* test mismatched start()/free() */
  q1 = gst_gl_query_new (context, GST_GL_QUERY_TIME_ELAPSED);
  fail_if (q1 == NULL);

  gst_gl_query_start (q1);

  ASSERT_CRITICAL (gst_gl_query_free (q1));

  /* test mismatched start()/result() */
  q1 = gst_gl_query_new (context, GST_GL_QUERY_TIME_ELAPSED);
  fail_if (q1 == NULL);

  gst_gl_query_start (q1);
  ASSERT_CRITICAL (gst_gl_query_result (q1));
  gst_gl_query_end (q1);

  gst_gl_query_free (q1);

  /* test mismatched end() */
  q1 = gst_gl_query_new (context, GST_GL_QUERY_TIME_ELAPSED);
  fail_if (q1 == NULL);
  ASSERT_CRITICAL (gst_gl_query_end (q1));
  gst_gl_query_free (q1);

  /* test double end() */
  q1 = gst_gl_query_new (context, GST_GL_QUERY_TIME_ELAPSED);
  fail_if (q1 == NULL);

  gst_gl_query_start (q1);
  gst_gl_query_end (q1);
  ASSERT_CRITICAL (gst_gl_query_end (q1));

  gst_gl_query_free (q1);

  /* double start is allowed */
}

GST_START_TEST (test_query)
{
  gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _test_query_gl,
      NULL);
}

GST_END_TEST;

static Suite *
gst_gl_upload_suite (void)
{
  Suite *s = suite_create ("GstGLQuery");
  TCase *tc_chain = tcase_create ("glquery");

  GST_DEBUG_CATEGORY_INIT (gst_test_debug_cat, "test-debug", 0,
      "proxy-logging test debug");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_query);

  return s;
}

GST_CHECK_MAIN (gst_gl_upload);
