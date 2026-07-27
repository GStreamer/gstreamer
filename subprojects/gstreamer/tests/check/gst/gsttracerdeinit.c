/* GStreamer
 * Copyright (C) 2026 Thibault Saunier <tsaunier@igalia.com>
 *
 * gsttracerdeinit.c: leaked objects must be reported into the debug log
 * when the leaks tracer shuts down at gst_deinit()
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

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

/* The child leaks an object on purpose and deinits through gstcheck's atexit
 * handler; the parent runs it and checks the leaks report made it into the
 * debug log through the log tracer, both explicitly enabled and
 * auto-enabled by GST_DEBUG=GST_TRACER:7. */

#define CHILD_ENV "GST_TRACER_DEINIT_TEST_CHILD"

static gboolean seen_leaks_warning = FALSE;

static gboolean
leaks_warning_cb (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  seen_leaks_warning = TRUE;
  /* discard: this warning is precisely what the child expects */
  return TRUE;
}

/* Registered before gst_check_init() so it runs after the gst_deinit() of
 * gstcheck's own atexit handler, where the leaks tracer warns. */
static void
verify_leaks_warning_seen (void)
{
  g_assert_true (seen_leaks_warning);
}

static void
run_child (const gchar * self, const gchar * tracers)
{
  gchar *log_file, *log = NULL;
  gchar *child_argv[] = { (gchar *) self, NULL };
  gchar **env;
  gint fd, wait_status = 0;
  GError *error = NULL;

  log_file = g_build_filename (g_get_tmp_dir (),
      "gsttracerdeinit-log-XXXXXX", NULL);
  fd = g_mkstemp (log_file);
  g_assert_cmpint (fd, >=, 0);
  g_close (fd, NULL);

  env = g_get_environ ();
  env = g_environ_setenv (env, CHILD_ENV, "1", TRUE);
  env = g_environ_setenv (env, "GST_TRACERS", tracers, TRUE);
  env = g_environ_setenv (env, "GST_DEBUG", "GST_TRACER:7", TRUE);
  env = g_environ_setenv (env, "GST_DEBUG_FILE", log_file, TRUE);
  env = g_environ_unsetenv (env, "G_DEBUG");

  g_spawn_sync (NULL, child_argv, env, G_SPAWN_DEFAULT, NULL, NULL,
      NULL, NULL, &wait_status, &error);
  g_assert_no_error (error);
  g_assert_cmpint (wait_status, ==, 0);

  g_assert_true (g_file_get_contents (log_file, &log, NULL, NULL));
  g_assert_nonnull (strstr (log, "object-alive"));
  g_assert_nonnull (strstr (log, "leaked-bin"));

  g_strfreev (env);
  g_free (log);
  g_unlink (log_file);
  g_free (log_file);
}

int
main (int argc, char **argv)
{
  if (g_getenv (CHILD_ENV)) {
    /* runs last at exit, after gstcheck's own atexit handler below */
    atexit (verify_leaks_warning_seen);
    /* registers gst_check_deinit() at exit, which calls gst_deinit();
     * unexpected criticals/warnings abort through the gstcheck log handler */
    gst_check_init (&argc, &argv);
    gst_check_add_log_filter (NULL, G_LOG_LEVEL_WARNING,
        g_regex_new ("^Leaks detected", 0, 0, NULL), leaks_warning_cb,
        NULL, NULL);
    /* leaked on purpose so the leaks tracer reports it at deinit */
    gst_object_ref_sink (gst_bin_new ("leaked-bin"));
    /* gst_deinit() runs after this return, in gstcheck's atexit handler:
     * that is where the leaks tracer logs the report and warns. Calling it
     * here instead would make the atexit one a fatal second deinit. */
    return 0;
  }

  /* explicitly enabled log tracer */
  run_child (argv[0], "leaks;log");
  /* log tracer auto-enabled by GST_DEBUG=GST_TRACER:7 */
  run_child (argv[0], "leaks");

  return 0;
}
