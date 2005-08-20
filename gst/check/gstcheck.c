/* GStreamer
 *
 * Common code for GStreamer unittests
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include "gstcheck.h"

GST_DEBUG_CATEGORY (check_debug);

/* logging function for tests
 * a test uses g_message() to log a debug line
 * a gst unit test can be run with GST_TEST_DEBUG env var set to see the
 * messages
 */

gboolean _gst_check_threads_running = FALSE;
GList *thread_list = NULL;
GMutex *mutex;
GCond *start_cond;              /* used to notify main thread of thread startups */
GCond *sync_cond;               /* used to synchronize all threads and main thread */

gboolean _gst_check_debug = FALSE;
gboolean _gst_check_raised_critical = FALSE;
gboolean _gst_check_expecting_log = FALSE;

void gst_check_log_message_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (_gst_check_debug) {
    g_print (message);
  }
}

void gst_check_log_critical_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (!_gst_check_expecting_log) {
    g_print ("\n\nUnexpected critical/warning: %s\n", message);
    fail ("Unexpected critical/warning: %s", message);
  }

  if (_gst_check_debug) {
    g_print ("\nExpected critical/warning: %s\n", message);
  }

  if (log_level & G_LOG_LEVEL_CRITICAL)
    _gst_check_raised_critical = TRUE;
}

/* initialize GStreamer testing */
void
gst_check_init (int *argc, char **argv[])
{
  gst_init (argc, argv);

  GST_DEBUG_CATEGORY_INIT (check_debug, "check", 0, "check regression tests");

  if (g_getenv ("GST_TEST_DEBUG"))
    _gst_check_debug = TRUE;

  g_log_set_handler (NULL, G_LOG_LEVEL_MESSAGE, gst_check_log_message_func,
      NULL);
  g_log_set_handler (NULL, G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_log_set_handler ("GStreamer", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_log_set_handler ("GLib-GObject", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
}

/* message checking */
void
gst_check_message_error (GstMessage * message, GstMessageType type,
    GQuark domain, gint code)
{
  GError *error;
  gchar *debug;

  fail_unless_equals_int (GST_MESSAGE_TYPE (message), type);
  gst_message_parse_error (message, &error, &debug);
  fail_unless_equals_int (error->domain, domain);
  fail_unless_equals_int (error->code, code);
  g_error_free (error);
  g_free (debug);
}
