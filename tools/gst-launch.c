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
#ifdef WIN32
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
static gboolean caught_intr = FALSE;
#endif

static GstElement *pipeline;
static gboolean caught_error = FALSE;
static gboolean tags = FALSE;
static gboolean messages = FALSE;


#ifndef GST_DISABLE_LOADSAVE
static GstElement *
xmllaunch_parse_cmdline (const gchar ** argv)
{
  GstElement *pipeline = NULL, *e;
  GstXML *xml;
  gboolean err;
  const gchar *arg;
  gchar *element, *property, *value;
  GList *l;
  gint i = 0;

  if (!(arg = argv[0])) {
    g_print (_
        ("Usage: gst-xmllaunch <file.xml> [ element.property=value ... ]\n"));
    exit (1);
  }

  xml = gst_xml_new ();
  /* FIXME guchar from gstxml.c */
  err = gst_xml_parse_file (xml, (guchar *) arg, NULL);

  if (err != TRUE) {
    fprintf (stderr, _("ERROR: parse of xml file '%s' failed.\n"), arg);
    exit (1);
  }

  l = gst_xml_get_topelements (xml);
  if (!l) {
    fprintf (stderr, _("ERROR: no toplevel pipeline element in file '%s'.\n"),
        arg);
    exit (1);
  }

  if (l->next)
    fprintf (stderr,
        _("WARNING: only one toplevel element is supported at this time."));

  pipeline = GST_ELEMENT (l->data);

  while ((arg = argv[++i])) {
    element = g_strdup (arg);
    property = strchr (element, '.');
    value = strchr (element, '=');

    if (!(element < property && property < value)) {
      fprintf (stderr,
          _("ERROR: could not parse command line argument %d: %s.\n"), i,
          element);
      g_free (element);
      exit (1);
    }

    *property++ = '\0';
    *value++ = '\0';

    e = gst_bin_get_by_name (GST_BIN (pipeline), element);
    if (!e) {
      fprintf (stderr, _("WARNING: element named '%s' not found.\n"), element);
    } else {
      gst_util_set_object_arg (G_OBJECT (e), property, value);
    }
    g_free (element);
  }

  if (!l)
    return NULL;
  else
    return l->data;
}
#endif

#ifndef DISABLE_FAULT_HANDLER
#ifndef USE_SIGINFO
static void
fault_handler_sighandler (int signum)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      printf ("Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      printf ("Caught SIGQUIT\n");
      break;
    default:
      printf ("signo:  %d\n", signum);
      break;
  }

  fault_spin ();
}

#else /* USE_SIGINFO */

static void
fault_handler_sigaction (int signum, siginfo_t * si, void *misc)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (si->si_signo) {
    case SIGSEGV:
      printf ("Caught SIGSEGV accessing address %p\n", si->si_addr);
      break;
    case SIGQUIT:
      printf ("Caught SIGQUIT\n");
      break;
    default:
      printf ("signo:  %d\n", si->si_signo);
      printf ("errno:  %d\n", si->si_errno);
      printf ("code:   %d\n", si->si_code);
      break;
  }

  fault_spin ();
}
#endif /* USE_SIGINFO */

static void
fault_spin (void)
{
  int spinning = TRUE;

  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("gst-launch");

  wait (NULL);

  /* FIXME how do we know if we were run by libtool? */
  printf ("Spinning.  Please run 'gdb gst-launch %d' to continue debugging, "
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
#endif /* DISABLE_FAULT_HANDLER */

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

#ifndef DISABLE_FAULT_HANDLER
/* we only use sighandler here because the registers are not important */
static void
sigint_handler_sighandler (int signum)
{
  g_print ("Caught interrupt -- ");

  sigint_restore ();

  caught_intr = TRUE;
}

static gboolean
check_intr (GstElement * pipeline)
{
  if (!caught_intr) {
    return TRUE;
  } else {
    GstBus *bus;
    GstMessage *message;

    caught_intr = FALSE;
    g_print ("Pausing pipeline.\n");

    bus = gst_element_get_bus (GST_ELEMENT (pipeline));
    message = gst_message_new_warning (GST_OBJECT (pipeline),
        NULL, "pipeline interrupted");
    gst_bus_post (bus, message);

    /* pipeline will wait for element to go to PAUSED */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    g_print ("Pipeline paused.\n");

    gst_object_unref (bus);

    return FALSE;
  }
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
#endif /* DISABLE_FAULT_HANDLER */

static gboolean
event_loop (GstElement * pipeline, gboolean blocking)
{
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

#ifndef DISABLE_FAULT_HANDLER
  g_timeout_add (50, (GSourceFunc) check_intr, pipeline);
#endif

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, blocking ? -1 : 0);

    /* if the poll timed out, only when !blocking */
    if (message == NULL) {
      gst_object_unref (bus);
      return FALSE;
    }

    if (messages) {
      const GstStructure *s;

      s = gst_message_get_structure (message);

      g_print (_("Got Message from element \"%s\" (%s): "),
          GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))),
          gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
      if (s) {
        gchar *sstr;

        sstr = gst_structure_to_string (s);
        g_print ("%s\n", sstr);
        g_free (sstr);
      } else {
        g_print ("no message details\n");
      }
    }

    switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_NEW_CLOCK:
      {
        GstClock *clock;

        gst_message_parse_new_clock (message, &clock);

        g_print ("New clock: %s\n", GST_OBJECT_NAME (clock));
        gst_message_unref (message);
        break;
      }
      case GST_MESSAGE_EOS:
        g_print (_
            ("Got EOS from element \"%s\".\n"),
            GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
        gst_message_unref (message);
        gst_object_unref (bus);
        return FALSE;
      case GST_MESSAGE_TAG:
        if (tags) {
          GstTagList *tags;

          gst_message_parse_tag (message, &tags);
          g_print (_("FOUND TAG      : found by element \"%s\".\n"),
              GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
          gst_tag_list_foreach (tags, print_tag, NULL);
          gst_tag_list_free (tags);
        }
        gst_message_unref (message);
        break;
      case GST_MESSAGE_WARNING:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        if (debug) {
          g_print ("WARNING: Element \"%s\" warns: %s\n",
              GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))),
              debug);
        }
        gst_message_unref (message);
        if (gerror)
          g_error_free (gerror);
        g_free (debug);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        if (gerror)
          g_error_free (gerror);
        g_free (debug);
        gst_object_unref (bus);
        return TRUE;
      }
      case GST_MESSAGE_STATE_CHANGED:{
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);
        if (!(old == GST_STATE_PLAYING && new == GST_STATE_PAUSED &&
                GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline))) {
          gst_message_unref (message);
          break;
        }
        g_print (_
            ("Element \"%s\" has gone from PLAYING to PAUSED, quitting.\n"),
            GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
        /* cut out of the event loop if check_intr set us to PAUSED */
        gst_message_unref (message);
        gst_object_unref (bus);
        return FALSE;
      }
      default:
        /* just be quiet by default */
        gst_message_unref (message);
        break;
    }
  }

  g_assert_not_reached ();
  return TRUE;
}

int
main (int argc, char *argv[])
{
  gint i, j;

  /* options */
  gboolean verbose = FALSE;
  gboolean no_fault = FALSE;
  gboolean trace = FALSE;
  gchar *savefile = NULL;
  gchar *exclude_args = NULL;
  GOptionEntry options[] = {
    {"tags", 't', 0, G_OPTION_ARG_NONE, &tags,
        N_("Output tags (also known as metadata)"), NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        N_("Output status information and property notifications"), NULL},
    {"messages", 'm', 0, G_OPTION_ARG_NONE, &messages,
        N_("Output messages"), NULL},
    {"exclude", 'X', 0, G_OPTION_ARG_NONE, &exclude_args,
        N_("Do not output status information of TYPE"), N_("TYPE1,TYPE2,...")},
#ifndef GST_DISABLE_LOADSAVE
    {"output", 'o', 0, G_OPTION_ARG_STRING, &savefile,
        N_("Save xml representation of pipeline to FILE and exit"), N_("FILE")},
#endif
    {"no-fault", 'f', 0, G_OPTION_ARG_NONE, &no_fault,
        N_("Do not install a fault handler"), NULL},
    {"trace", 'T', 0, G_OPTION_ARG_NONE, &trace,
        N_("Print alloc trace (if enabled at compile time)"), NULL},
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;
  gchar **argvn;
  GError *error = NULL;
  gint res = 0;

  free (malloc (8));            /* -lefence */

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);

  ctx = g_option_context_new ("gst-launch");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    exit (1);
  }
  g_option_context_free (ctx);

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
    gst_alloc_trace_print_live ();
  }

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
#ifndef GST_DISABLE_LOADSAVE
  if (strstr (argv[0], "gst-xmllaunch")) {
    pipeline = xmllaunch_parse_cmdline ((const gchar **) argvn);
  } else
#endif
  {
    pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }
  g_free (argvn);

  if (!pipeline) {
    if (error) {
      fprintf (stderr, _("ERROR: pipeline could not be constructed: %s.\n"),
          GST_STR_NULL (error->message));
      g_error_free (error);
    } else {
      fprintf (stderr, _("ERROR: pipeline could not be constructed.\n"));
    }
    return 1;
  } else if (error) {
    fprintf (stderr, _("WARNING: erroneous pipeline: %s\n"),
        GST_STR_NULL (error->message));
    g_error_free (error);
    return 1;
  }

  if (verbose) {
    gchar **exclude_list =
        exclude_args ? g_strsplit (exclude_args, ",", 0) : NULL;
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_object_default_deep_notify), exclude_list);
  }
#ifndef GST_DISABLE_LOADSAVE
  if (savefile) {
    gst_xml_write_file (GST_ELEMENT (pipeline), fopen (savefile, "w"));
  }
#endif

  if (!savefile) {
    GstState state, pending;
    GstStateChangeReturn ret;

    if (!GST_IS_BIN (pipeline)) {
      GstElement *real_pipeline = gst_element_factory_make ("pipeline", NULL);

      if (real_pipeline == NULL) {
        fprintf (stderr, _("ERROR: the 'pipeline' element wasn't found.\n"));
        return 1;
      }
      gst_bin_add (GST_BIN (real_pipeline), pipeline);
      pipeline = real_pipeline;
    }
    fprintf (stderr, _("Setting pipeline to PAUSED ...\n"));
    ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);

    switch (ret) {
      case GST_STATE_CHANGE_FAILURE:
        fprintf (stderr, _("ERROR: Pipeline doesn't want to pause.\n"));
        res = -1;
        event_loop (pipeline, FALSE);
        goto end;
      case GST_STATE_CHANGE_NO_PREROLL:
        fprintf (stderr, _("ERROR: Pipeline can't PREROLL ...\n"));
        break;
      case GST_STATE_CHANGE_ASYNC:
        fprintf (stderr, _("Pipeline is PREROLLING ...\n"));
        gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
        /* fallthrough */
      case GST_STATE_CHANGE_SUCCESS:
        fprintf (stderr, _("Pipeline is PREROLLED ...\n"));
        break;
    }

    caught_error = event_loop (pipeline, FALSE);

    if (caught_error) {
      fprintf (stderr, _("ERROR: pipeline doesn't want to preroll.\n"));
    } else {
      GTimeVal tfthen, tfnow;
      GstClockTimeDiff diff;

      fprintf (stderr, _("Setting pipeline to PLAYING ...\n"));
      if (gst_element_set_state (pipeline,
              GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        fprintf (stderr, _("ERROR: pipeline doesn't want to play.\n"));
        res = -1;
        goto end;
      }

      g_get_current_time (&tfthen);
      caught_error = event_loop (pipeline, TRUE);
      g_get_current_time (&tfnow);

      diff = GST_TIMEVAL_TO_TIME (tfnow) - GST_TIMEVAL_TO_TIME (tfthen);

      g_print (_("Execution ended after %" G_GUINT64_FORMAT " ns.\n"), diff);
    }
    while (g_main_context_iteration (NULL, FALSE));

    fprintf (stderr, _("Setting pipeline to PAUSED ...\n"));
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
    fprintf (stderr, _("Setting pipeline to READY ...\n"));
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);

  end:
    fprintf (stderr, _("Setting pipeline to NULL ...\n"));
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
  }

  fprintf (stderr, _("FREEING pipeline ...\n"));
  gst_object_unref (pipeline);

  gst_deinit ();
  if (trace)
    gst_alloc_trace_print_live ();

  return res;
}
