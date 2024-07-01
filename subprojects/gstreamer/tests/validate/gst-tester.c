/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *   Author: Thibault Saunier <tsaunier@igalia.com>

 * gst-tester.c: tool to launch `.validatetest` files with
 * TAP compatible output and supporting missing `gst-validate`
 * application.
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

#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <sys/wait.h>
#elif defined (G_OS_WIN32)
#include <windows.h>
#include <io.h>
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#define isatty _isatty
#endif

#if defined (G_OS_WIN32)
#define VALIDATE_NAME "gst-validate-" GST_API_VERSION ".exe"
#else
#define VALIDATE_NAME "gst-validate-" GST_API_VERSION
#endif


typedef struct
{
  const gchar *testname;

  GSubprocess *subproc;
  GMainLoop *ml;
#if defined(G_OS_UNIX)
  guint signal_watch_intr_id;
#endif
  gint exitcode;
} Application;

#if defined(G_OS_UNIX)
/* As the interrupt handler is dispatched from GMainContext as a GSourceFunc
 * handler, we can react to this by posting a message. */
static gboolean
intr_handler (gpointer user_data)
{
  Application *app = user_data;

  g_print ("Bail out! Got interupted.\n");

  g_subprocess_force_exit (app->subproc);

  /* remove signal handler */
  app->signal_watch_intr_id = 0;
  return G_SOURCE_REMOVE;
}
#endif

static void
_run_app (Application * app)
{
  GError *err = NULL;
  gboolean bailed_out = FALSE, skipped = FALSE;
  gchar *_stdout = NULL;
  gboolean is_tty = isatty (STDOUT_FILENO);

  g_print ("1..1\n");
  g_subprocess_communicate_utf8 (app->subproc, NULL, NULL,
      is_tty ? NULL : &_stdout, NULL, &err);
  if (_stdout) {
    gchar *c;
    GString *output = g_string_new (NULL);

    for (c = _stdout; *c != '\0'; c++) {
      g_string_append_c (output, *c);
      if (!bailed_out && !skipped && *c == '\n' && *(c + 1) != '\0') {
        if (strstr ((c + 1), "Bail out!") == c + 1) {
          bailed_out = TRUE;
          continue;
        }

        if (strstr ((c + 1), "ok") == c + 1 && strstr ((c + 1), "# SKIP")) {
          skipped = TRUE;
          app->exitcode = 0;
          continue;
        }

        g_string_append (output, "# ");
      }
    }
    g_print ("# %s\n", output->str);
    g_string_free (output, TRUE);
    g_free (_stdout);
  }
#ifdef G_OS_UNIX
  if (app->signal_watch_intr_id > 0)
    g_source_remove (app->signal_watch_intr_id);
#endif

  if (skipped || bailed_out)
    goto done;

  if (g_subprocess_get_if_signaled (app->subproc))
    app->exitcode = g_subprocess_get_term_sig (app->subproc);
  else
    app->exitcode = g_subprocess_get_exit_status (app->subproc);

  if (app->exitcode == 0) {
    g_print ("ok 1 %s\n", app->testname);
  } else if (app->exitcode == 18) {
    g_print ("not ok 1 %s # Got a critical report\n", app->testname);
  } else {
    g_print ("not ok 1 %s # Unknown reason\n", app->testname);
  }

done:
  g_clear_object (&app->subproc);
  g_main_loop_quit (app->ml);
}

int
main (int argc, gchar ** argv)
{
  Application app = { 0, };
  gchar *dirname;
  GFile *f;
  gchar **args = g_new0 (gchar *, argc + 2);
  gint i;
  GError *err = NULL;
  gchar *filename;
  gboolean is_tty = isatty (STDOUT_FILENO);
  GThread *thread;

  if (argc < 2) {
    g_print ("1..0\nnot ok # Missing <testfile> argument\n");
    return 1;
  }

  app.testname = argv[1];

  dirname = g_path_get_dirname (argv[0]);
  filename = g_build_filename ("subprojects", "gst-devtools",
      "validate", "tools", VALIDATE_NAME, NULL);
  f = g_file_new_for_path (filename);
  g_free (filename);

  if (g_file_query_exists (f, NULL)) {
    /* Try to find `gst-validate` as a meson subproject */
    g_free (args[0]);
    g_clear_error (&err);
    args[0] = g_file_get_path (f);
    g_print ("# Running from meson subproject %s\n", args[0]);
  }
  g_free (dirname);
  g_object_unref (f);

  if (!args[0])
    args[0] = g_strdup (VALIDATE_NAME);
  args[1] = g_strdup ("--set-test-file");
  for (i = 1; i < argc; i++)
    args[i + 1] = g_strdup (argv[i]);

  app.subproc = g_subprocess_newv ((const char *const *) args,
      is_tty ? G_SUBPROCESS_FLAGS_STDIN_INHERIT :
      G_SUBPROCESS_FLAGS_STDOUT_PIPE, &err);

  if (!app.subproc) {
    g_printerr ("%s %s\n", args[0], err->message);
    if (g_error_matches (err, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT)) {
      g_print ("1..0 # Skipped: `" VALIDATE_NAME "` not available\n");
      return 0;
    }

    g_print ("1..0\nnot ok # %s\n", err->message);
    return -1;
  }

  app.ml = g_main_loop_new (NULL, TRUE);

#ifdef G_OS_UNIX
  app.signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, &app);
#endif

/* Running the subprocess in it own thread so that we can properly catch
 * interuptions in the main thread main loop */
  thread = g_thread_new ("gst-tester-thread", (GThreadFunc) _run_app, &app);
  g_main_loop_run (app.ml);
  g_main_loop_unref (app.ml);
  g_thread_join (thread);
  g_strfreev (args);

  return app.exitcode;
}
