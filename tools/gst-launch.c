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
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef DISABLE_FAULT_HANDLER
#include <sys/wait.h>
#endif
#include <locale.h>             /* for LC_ALL */
#include "tools.h"

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
static gboolean is_live = FALSE;


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

  gst_object_ref (pipeline);
  gst_object_unref (xml);
  return pipeline;
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
    } else if (gst_tag_get_type (tag) == GST_TYPE_BUFFER) {
      GstBuffer *img;

      img = gst_value_get_buffer (gst_tag_list_get_value_index (list, tag, i));
      if (img) {
        gchar *caps_str;

        caps_str = GST_BUFFER_CAPS (img) ?
            gst_caps_to_string (GST_BUFFER_CAPS (img)) : g_strdup ("unknown");
        str = g_strdup_printf ("buffer of %u bytes, type: %s",
            GST_BUFFER_SIZE (img), caps_str);
        g_free (caps_str);
      } else {
        str = g_strdup ("NULL buffer");
      }
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("%16s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("%16s: %s\n", "", str);
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

  /* we set a flag that is checked by the mainloop, we cannot do much in the
   * interrupt handler (no mutex or other blocking stuff) */
  caught_intr = TRUE;
}

/* is called every 50 milliseconds (20 times a second), the interrupt handler
 * will set a flag for us. We react to this by posting a message. */
static gboolean
check_intr (GstElement * pipeline)
{
  if (!caught_intr) {
    return TRUE;
  } else {
    caught_intr = FALSE;
    g_print ("handling interrupt.\n");

    /* post an application specific message */
    gst_element_post_message (GST_ELEMENT (pipeline),
        gst_message_new_application (GST_OBJECT (pipeline),
            gst_structure_new ("GstLaunchInterrupt",
                "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));

    /* remove timeout handler */
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

/* returns TRUE if there was an error or we caught a keyboard interrupt. */
static gboolean
event_loop (GstElement * pipeline, gboolean blocking, GstState target_state)
{
  GstBus *bus;
  GstMessage *message = NULL;
  gboolean res = FALSE;
  gboolean buffering = FALSE;

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

#ifndef DISABLE_FAULT_HANDLER
  g_timeout_add (50, (GSourceFunc) check_intr, pipeline);
#endif

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, blocking ? -1 : 0);

    /* if the poll timed out, only when !blocking */
    if (message == NULL)
      goto exit;

    /* check if we need to dump messages to the console */
    if (messages) {
      const GstStructure *s;
      guint32 seqnum;

      seqnum = gst_message_get_seqnum (message);

      s = gst_message_get_structure (message);

      g_print (_("Got Message #%" G_GUINT32_FORMAT
              " from element \"%s\" (%s): "), seqnum,
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

        g_print ("New clock: %s\n", (clock ? GST_OBJECT_NAME (clock) : "NULL"));
        break;
      }
      case GST_MESSAGE_EOS:
        g_print (_
            ("Got EOS from element \"%s\".\n"),
            GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
        goto exit;
      case GST_MESSAGE_TAG:
        if (tags) {
          GstTagList *tags;

          gst_message_parse_tag (message, &tags);
          g_print (_("FOUND TAG      : found by element \"%s\".\n"),
              GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
          gst_tag_list_foreach (tags, print_tag, NULL);
          gst_tag_list_free (tags);
        }
        break;
      case GST_MESSAGE_INFO:{
        GError *gerror;
        gchar *debug;
        gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

        gst_message_parse_info (message, &gerror, &debug);
        if (debug) {
          g_print (_("INFO:\n%s\n"), debug);
        }
        g_error_free (gerror);
        g_free (debug);
        g_free (name);
        break;
      }
      case GST_MESSAGE_WARNING:{
        GError *gerror;
        gchar *debug;
        gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

        /* dump graph on warning */
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.warning");

        gst_message_parse_warning (message, &gerror, &debug);
        g_print (_("WARNING: from element %s: %s\n"), name, gerror->message);
        if (debug) {
          g_print (_("Additional debug info:\n%s\n"), debug);
        }
        g_error_free (gerror);
        g_free (debug);
        g_free (name);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        /* dump graph on error */
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.error");

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        /* we have an error */
        res = TRUE;
        goto exit;
      }
      case GST_MESSAGE_STATE_CHANGED:{
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        /* we only care about pipeline state change messages */
        if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
          break;

        /* dump graph for pipeline state changes */
        {
          gchar *dump_name = g_strdup_printf ("gst-launch.%s_%s",
              gst_element_state_get_name (old),
              gst_element_state_get_name (new));
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
          g_free (dump_name);
        }

        /* ignore when we are buffering since then we mess with the states
         * ourselves. */
        if (buffering) {
          fprintf (stderr,
              _("Prerolled, waiting for buffering to finish...\n"));
          break;
        }

        /* if we reached the final target state, exit */
        if (target_state == GST_STATE_PAUSED && new == target_state)
          goto exit;

        /* else not an interesting message */
        break;
      }
      case GST_MESSAGE_BUFFERING:{
        gint percent;

        gst_message_parse_buffering (message, &percent);
        fprintf (stderr, "%s %d%%  \r", _("buffering..."), percent);

        /* no state management needed for live pipelines */
        if (is_live)
          break;

        if (percent == 100) {
          /* a 100% message means buffering is done */
          buffering = FALSE;
          /* if the desired state is playing, go back */
          if (target_state == GST_STATE_PLAYING) {
            fprintf (stderr,
                _("Done buffering, setting pipeline to PLAYING ...\n"));
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
          } else
            goto exit;
        } else {
          /* buffering busy */
          if (buffering == FALSE && target_state == GST_STATE_PLAYING) {
            /* we were not buffering but PLAYING, PAUSE  the pipeline. */
            fprintf (stderr, _("Buffering, setting pipeline to PAUSED ...\n"));
            gst_element_set_state (pipeline, GST_STATE_PAUSED);
          }
          buffering = TRUE;
        }
        break;
      }
      case GST_MESSAGE_LATENCY:
      {
        fprintf (stderr, _("Redistribute latency...\n"));
        gst_bin_recalculate_latency (GST_BIN (pipeline));
        break;
      }
      case GST_MESSAGE_APPLICATION:{
        const GstStructure *s;

        s = gst_message_get_structure (message);

        if (gst_structure_has_name (s, "GstLaunchInterrupt")) {
          /* this application message is posted when we caught an interrupt and
           * we need to stop the pipeline. */
          fprintf (stderr, _("Interrupt: Stopping pipeline ...\n"));
          /* return TRUE when we caught an interrupt */
          res = TRUE;
          goto exit;
        }
      }
      default:
        /* just be quiet by default */
        break;
    }
    if (message)
      gst_message_unref (message);
  }
  g_assert_not_reached ();

exit:
  {
    if (message)
      gst_message_unref (message);
    gst_object_unref (bus);
    return res;
  }
}

int
main (int argc, char *argv[])
{
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
    GST_TOOLS_GOPTION_VERSION,
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

  if (!g_thread_supported ())
    g_thread_init (NULL);

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);

  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    if (err)
      g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    else
      g_print ("Error initializing: Unknown error!\n");
    exit (1);
  }
  g_option_context_free (ctx);

  gst_tools_print_version ("gst-launch");

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

    /* If the top-level object is not a pipeline, place it in a pipeline. */
    if (!GST_IS_PIPELINE (pipeline)) {
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
        event_loop (pipeline, FALSE, GST_STATE_VOID_PENDING);
        goto end;
      case GST_STATE_CHANGE_NO_PREROLL:
        fprintf (stderr, _("Pipeline is live and does not need PREROLL ...\n"));
        is_live = TRUE;
        break;
      case GST_STATE_CHANGE_ASYNC:
        fprintf (stderr, _("Pipeline is PREROLLING ...\n"));
        caught_error = event_loop (pipeline, TRUE, GST_STATE_PAUSED);
        if (caught_error) {
          fprintf (stderr, _("ERROR: pipeline doesn't want to preroll.\n"));
          goto end;
        }
        state = GST_STATE_PAUSED;
        /* fallthrough */
      case GST_STATE_CHANGE_SUCCESS:
        fprintf (stderr, _("Pipeline is PREROLLED ...\n"));
        break;
    }

    caught_error = event_loop (pipeline, FALSE, GST_STATE_PLAYING);

    if (caught_error) {
      fprintf (stderr, _("ERROR: pipeline doesn't want to preroll.\n"));
    } else {
      GstClockTime tfthen, tfnow;
      GstClockTimeDiff diff;

      fprintf (stderr, _("Setting pipeline to PLAYING ...\n"));
      if (gst_element_set_state (pipeline,
              GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        GstMessage *err_msg;
        GstBus *bus;

        fprintf (stderr, _("ERROR: pipeline doesn't want to play.\n"));
        bus = gst_element_get_bus (pipeline);
        if ((err_msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, 0))) {
          GError *gerror;
          gchar *debug;

          gst_message_parse_error (err_msg, &gerror, &debug);
          gst_object_default_error (GST_MESSAGE_SRC (err_msg), gerror, debug);
          gst_message_unref (err_msg);
          g_error_free (gerror);
          g_free (debug);
        }
        gst_object_unref (bus);
        res = -1;
        goto end;
      }

      tfthen = gst_util_get_timestamp ();
      caught_error = event_loop (pipeline, TRUE, GST_STATE_PLAYING);
      tfnow = gst_util_get_timestamp ();

      diff = GST_CLOCK_DIFF (tfthen, tfnow);

      g_print (_("Execution ended after %" G_GUINT64_FORMAT " ns.\n"), diff);
    }

    /* iterate mainloop to process pending stuff */
    while (g_main_context_iteration (NULL, FALSE));

    fprintf (stderr, _("Setting pipeline to PAUSED ...\n"));
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    if (!caught_error)
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
