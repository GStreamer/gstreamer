/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gst-launch.c: tool to launch GStreamer pipelines from the command line
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
#  include "config.h"
#endif

/* FIXME: hack alert */
#ifdef _MSC_VER
#define DISABLE_FAULT_HANDLER
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef DISABLE_FAULT_HANDLER
#include <sys/wait.h>
#endif
#include <locale.h>             /* for LC_ALL */
#include "gst/gst-i18n-app.h"

#include <gst/gst.h>

/* FIXME: This is just a temporary hack.  We should have a better
 * check for siginfo handling. */
#ifdef SA_SIGINFO
#define USE_SIGINFO
#endif

extern volatile gboolean glib_on_error_halt;

#ifndef DISABLE_FAULT_HANDLER
static void fault_restore (void);
static void fault_spin (void);
static void sigint_restore (void);
#endif

static gint max_iterations = 0;
static GstElement *pipeline;
gboolean caught_intr = FALSE;
gboolean caught_quit = FALSE;

#ifndef DISABLE_FAULT_HANDLER
#ifndef USE_SIGINFO
static void
fault_handler_sighandler (int signum)
{
  fault_restore ();

  switch (signum) {
    case SIGSEGV:
      g_print ("Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      g_print ("Caught SIGQUIT\n");
      break;
    default:
      g_print ("signo:  %d\n", signum);
      break;
  }

  fault_spin ();
}

#else

static void
fault_handler_sigaction (int signum, siginfo_t * si, void *misc)
{
  fault_restore ();

  switch (si->si_signo) {
    case SIGSEGV:
      g_print ("Caught SIGSEGV accessing address %p\n", si->si_addr);
      break;
    case SIGQUIT:
      g_print ("Caught SIGQUIT\n");
      break;
    default:
      g_print ("signo:  %d\n", si->si_signo);
      g_print ("errno:  %d\n", si->si_errno);
      g_print ("code:   %d\n", si->si_code);
      break;
  }

  fault_spin ();
}
#endif

static void
fault_spin (void)
{
  int spinning = TRUE;

  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("gst-launch");

  wait (NULL);

  /* FIXME how do we know if we were run by libtool? */
  g_print ("Spinning.  Please run 'gdb gst-launch %d' to continue debugging, "
      "Ctrl-C to quit, or Ctrl-\\ to dump core.\n", (gint) getpid ());
  while (spinning)
    g_usleep (1000000);
}

static void
fault_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

static void
fault_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
#ifdef USE_SIGINFO
  action.sa_sigaction = fault_handler_sigaction;
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_handler = fault_handler_sighandler;
#endif

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}
#endif

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str))
        g_assert_not_reached ();
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("%15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("               : %s\n", str);
    }

    g_free (str);
  }
}
static void
found_tag (GObject * pipeline, GstElement * source, GstTagList * tags)
{
  g_print (_("FOUND TAG      : found by element \"%s\".\n"),
      GST_STR_NULL (GST_ELEMENT_NAME (source)));
  gst_tag_list_foreach (tags, print_tag, NULL);
}

#ifndef DISABLE_FAULT_HANDLER
/* we only use sighandler here because the registers are not important */
static void
sigint_handler_sighandler (int signum)
{
  sigint_restore ();

  caught_intr = TRUE;
}

static void
sigint_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = sigint_handler_sighandler;

  sigaction (SIGINT, &action, NULL);
}

static void
sigint_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);
}

static void
play_handler (int signum)
{
  switch (signum) {
    case SIGUSR1:
      g_print ("Caught SIGUSR1 - Play request.\n");
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
      break;
    case SIGUSR2:
      g_print ("Caught SIGUSR2 - Stop request.\n");
      gst_element_set_state (pipeline, GST_STATE_NULL);
      break;
  }
}

static void
play_signal_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = play_handler;
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGUSR2, &action, NULL);
}
#endif

static gboolean
should_quit (gpointer loop)
{
  if (!caught_intr && !caught_quit)
    return TRUE;

  g_main_loop_quit (loop);
  if (caught_intr)
    g_print ("Caught interrupt.\n");
  return FALSE;
}

static void
quit_cb (void)
{
  caught_quit = TRUE;
}

static GPollFunc gpoll;
static GTimer *poll_timer = NULL;

static gint
launch_poll (GPollFD * ufds, guint nfds, gint timeout_)
{
  gint ret;

  if (G_LIKELY (poll_timer)) {
    g_timer_continue (poll_timer);
  } else {
    poll_timer = g_timer_new ();
    g_timer_start (poll_timer);
  }
  ret = gpoll (ufds, nfds, timeout_);
  g_timer_stop (poll_timer);
  return ret;
}

int
main (int argc, char *argv[])
{
  gint i, j;

  /* options */
  gboolean verbose = FALSE;
  gboolean tags = FALSE;
  gboolean no_fault = FALSE;
  gboolean trace = FALSE;
  gchar *exclude_args = NULL;
  struct poptOption options[] = {
    {"tags", 't', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &tags, 0,
        N_("Output tags (also known as metadata)"), NULL},
    {"verbose", 'v', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &verbose, 0,
        N_("Output status information and property notifications"), NULL},
    {"exclude", 'X', POPT_ARG_STRING | POPT_ARGFLAG_STRIP, &exclude_args, 0,
        N_("Do not output status information of TYPE"), N_("TYPE1,TYPE2,...")},
    {"no-fault", 'f', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &no_fault, 0,
        N_("Do not install a fault handler"), NULL},
    {"trace", 'T', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &trace, 0,
        N_("Print alloc trace (if enabled at compile time)"), NULL},
    {"iterations", 'i', POPT_ARG_INT | POPT_ARGFLAG_STRIP, &max_iterations, 0,
        N_("Number of times to iterate pipeline"), NULL},
    POPT_TABLEEND
  };

  gchar **argvn;
  GError *error = NULL;
  gint res = 0;

  free (malloc (8));            /* -lefence */

#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);

  gst_init_with_popt_table (&argc, &argv, options);

  /* FIXpopt: strip short args, too. We do it ourselves for now */
  j = 1;
  for (i = 1; i < argc; i++) {
    if (*(argv[i]) == '-') {
      if (strlen (argv[i]) == 2) {
        gchar *c = argv[i];

        c++;
        if (*c == 'X' || *c == 'o') {
          i++;
        }
      }
    } else {
      argv[j] = argv[i];
      j++;
    }
  }
  argc = j;

#ifndef DISABLE_FAULT_HANDLER
  if (!no_fault)
    fault_setup ();

  sigint_setup ();
  play_signal_setup ();
#endif

  if (trace) {
    if (!gst_alloc_trace_available ()) {
      g_warning ("Trace not available (recompile with trace enabled).");
    }
    gst_alloc_trace_print_all ();
  }

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  {
    pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }
  g_free (argvn);

  if (!pipeline) {
    if (error) {
      g_printerr (_("ERROR: pipeline could not be constructed: %s.\n"),
          error->message);
      g_error_free (error);
    } else {
      g_printerr (_("ERROR: pipeline could not be constructed.\n"));
    }
    exit (1);
  } else if (error) {
    g_printerr (_("WARNING: erroneous pipeline: %s\n"), error->message);
    g_printerr (_("         Trying to run anyway.\n"));
    g_error_free (error);
  }

  if (verbose) {
    gchar **exclude_list =
        exclude_args ? g_strsplit (exclude_args, ",", 0) : NULL;
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_element_default_deep_notify), exclude_list);
  }
  if (tags) {
    g_signal_connect (pipeline, "found-tag", G_CALLBACK (found_tag), NULL);
  }
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error),
      NULL);

  if (!GST_IS_BIN (pipeline)) {
    GstElement *real_pipeline = gst_element_factory_make ("pipeline", NULL);

    if (real_pipeline == NULL) {
      g_printerr (_("ERROR: the 'pipeline' element wasn't found.\n"));
      return 1;
    }
    gst_bin_add (GST_BIN (real_pipeline), pipeline);
    pipeline = real_pipeline;
  }

  g_signal_connect_swapped (pipeline, "eos", G_CALLBACK (quit_cb), NULL);
  g_signal_connect_swapped (pipeline, "error", G_CALLBACK (quit_cb), NULL);
  g_print (_("RUNNING pipeline ...\n"));

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
    g_printerr (_("ERROR: pipeline doesn't want to play.\n"));
    res = -1;
  } else {
    GTimer *timer = g_timer_new ();
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    gdouble in_poll = 0, duration;

    g_timeout_add (1000, should_quit, loop);

    gpoll = g_main_context_get_poll_func (NULL);
    g_main_context_set_poll_func (NULL, launch_poll);
    g_timer_start (timer);
    g_main_loop_run (loop);
    g_timer_stop (timer);
    g_main_loop_unref (loop);
    if (poll_timer) {
      in_poll = g_timer_elapsed (poll_timer, NULL);
      g_timer_destroy (poll_timer);
    }
    duration = g_timer_elapsed (timer, NULL);
    g_timer_destroy (timer);

    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print (_("Execution ended after %.2fs (%.2fs or %.2f%% idling).\n"),
        duration, in_poll, in_poll * 100 / duration);
  }

  gst_object_unref (GST_OBJECT (pipeline));

  if (trace)
    gst_alloc_trace_print_all ();

  return res;
}
