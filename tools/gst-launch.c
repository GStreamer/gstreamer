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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <sys/wait.h>
#elif defined (G_OS_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <locale.h>             /* for LC_ALL */
#include "tools.h"

extern volatile gboolean glib_on_error_halt;

#ifdef G_OS_UNIX
static void fault_restore (void);
static void fault_spin (void);
#endif

/* event_loop return codes */
typedef enum _EventLoopResult
{
  ELR_NO_ERROR = 0,
  ELR_ERROR,
  ELR_INTERRUPT
} EventLoopResult;

static GstElement *pipeline;
static EventLoopResult caught_error = ELR_NO_ERROR;
static gboolean quiet = FALSE;
static gboolean tags = FALSE;
static gboolean toc = FALSE;
static gboolean messages = FALSE;
static gboolean is_live = FALSE;
static gboolean waiting_eos = FALSE;
static gchar **exclude_args = NULL;

/* convenience macro so we don't have to litter the code with if(!quiet) */
#define PRINT if(!quiet)g_print

#ifdef G_OS_UNIX
static void
fault_handler_sighandler (int signum)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      fprintf (stderr, "Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      if (!quiet)
        printf ("Caught SIGQUIT\n");
      break;
    default:
      fprintf (stderr, "signo:  %d\n", signum);
      break;
  }

  fault_spin ();
}

static void
fault_spin (void)
{
  int spinning = TRUE;

  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("gst-launch-" GST_API_VERSION);

  wait (NULL);

  /* FIXME how do we know if we were run by libtool? */
  fprintf (stderr,
      "Spinning.  Please run 'gdb gst-launch-" GST_API_VERSION " %d' to "
      "continue debugging, Ctrl-C to quit, or Ctrl-\\ to dump core.\n",
      (gint) getpid ());
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
  action.sa_handler = fault_handler_sighandler;

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}
#endif /* G_OS_UNIX */

#if 0
typedef struct _GstIndexStats
{
  gint id;
  gchar *desc;

  guint num_frames;
  guint num_keyframes;
  guint num_dltframes;
  GstClockTime last_keyframe;
  GstClockTime last_dltframe;
  GstClockTime min_keyframe_gap;
  GstClockTime max_keyframe_gap;
  GstClockTime avg_keyframe_gap;
} GstIndexStats;

static void
entry_added (GstIndex * index, GstIndexEntry * entry, gpointer user_data)
{
  GPtrArray *index_stats = (GPtrArray *) user_data;
  GstIndexStats *s;

  switch (entry->type) {
    case GST_INDEX_ENTRY_ID:
      /* we have a new writer */
      GST_DEBUG_OBJECT (index, "id %d: describes writer %s", entry->id,
          GST_INDEX_ID_DESCRIPTION (entry));
      if (entry->id >= index_stats->len) {
        g_ptr_array_set_size (index_stats, entry->id + 1);
      }
      s = g_new (GstIndexStats, 1);
      s->id = entry->id;
      s->desc = g_strdup (GST_INDEX_ID_DESCRIPTION (entry));
      s->num_frames = s->num_keyframes = s->num_dltframes = 0;
      s->last_keyframe = s->last_dltframe = GST_CLOCK_TIME_NONE;
      s->min_keyframe_gap = s->max_keyframe_gap = s->avg_keyframe_gap =
          GST_CLOCK_TIME_NONE;
      g_ptr_array_index (index_stats, entry->id) = s;
      break;
    case GST_INDEX_ENTRY_FORMAT:
      /* have not found any code calling this */
      GST_DEBUG_OBJECT (index, "id %d: registered format %d for %s\n",
          entry->id, GST_INDEX_FORMAT_FORMAT (entry),
          GST_INDEX_FORMAT_KEY (entry));
      break;
    case GST_INDEX_ENTRY_ASSOCIATION:
    {
      gint64 ts;
      GstAssocFlags flags = GST_INDEX_ASSOC_FLAGS (entry);

      s = g_ptr_array_index (index_stats, entry->id);
      gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &ts);

      if (flags & GST_ASSOCIATION_FLAG_KEY_UNIT) {
        s->num_keyframes++;

        if (GST_CLOCK_TIME_IS_VALID (ts)) {
          if (GST_CLOCK_TIME_IS_VALID (s->last_keyframe)) {
            GstClockTimeDiff d = GST_CLOCK_DIFF (s->last_keyframe, ts);

            if (G_UNLIKELY (d < 0)) {
              GST_WARNING ("received out-of-order keyframe at %"
                  GST_TIME_FORMAT, GST_TIME_ARGS (ts));
              /* FIXME: does it still make sense to use that for the statistics */
              d = GST_CLOCK_DIFF (ts, s->last_keyframe);
            }

            if (GST_CLOCK_TIME_IS_VALID (s->min_keyframe_gap)) {
              if (d < s->min_keyframe_gap)
                s->min_keyframe_gap = d;
            } else {
              s->min_keyframe_gap = d;
            }
            if (GST_CLOCK_TIME_IS_VALID (s->max_keyframe_gap)) {
              if (d > s->max_keyframe_gap)
                s->max_keyframe_gap = d;
            } else {
              s->max_keyframe_gap = d;
            }
            if (GST_CLOCK_TIME_IS_VALID (s->avg_keyframe_gap)) {
              s->avg_keyframe_gap = (d + s->num_frames * s->avg_keyframe_gap) /
                  (s->num_frames + 1);
            } else {
              s->avg_keyframe_gap = d;
            }
          }
          s->last_keyframe = ts;
        }
      }
      if (flags & GST_ASSOCIATION_FLAG_DELTA_UNIT) {
        s->num_dltframes++;
        if (GST_CLOCK_TIME_IS_VALID (ts)) {
          s->last_dltframe = ts;
        }
      }
      s->num_frames++;

      break;
    }
    default:
      break;
  }
}

/* print statistics from the entry_added callback, free the entries */
static void
print_index_stats (GPtrArray * index_stats)
{
  gint i;

  if (index_stats->len) {
    g_print ("%s:\n", _("Index statistics"));
  }

  for (i = 0; i < index_stats->len; i++) {
    GstIndexStats *s = g_ptr_array_index (index_stats, i);
    if (s) {
      g_print ("id %d, %s\n", s->id, s->desc);
      if (s->num_frames) {
        GstClockTime last_frame = s->last_keyframe;

        if (GST_CLOCK_TIME_IS_VALID (s->last_dltframe)) {
          if (!GST_CLOCK_TIME_IS_VALID (last_frame) ||
              (s->last_dltframe > last_frame))
            last_frame = s->last_dltframe;
        }

        if (GST_CLOCK_TIME_IS_VALID (last_frame)) {
          g_print ("  total time               = %" GST_TIME_FORMAT "\n",
              GST_TIME_ARGS (last_frame));
        }
        g_print ("  frame/keyframe rate      = %u / %u = ", s->num_frames,
            s->num_keyframes);
        if (s->num_keyframes)
          g_print ("%lf\n", s->num_frames / (gdouble) s->num_keyframes);
        else
          g_print ("-\n");
        if (s->num_keyframes) {
          g_print ("  min/avg/max keyframe gap = %" GST_TIME_FORMAT ", %"
              GST_TIME_FORMAT ", %" GST_TIME_FORMAT "\n",
              GST_TIME_ARGS (s->min_keyframe_gap),
              GST_TIME_ARGS (s->avg_keyframe_gap),
              GST_TIME_ARGS (s->max_keyframe_gap));
        }
      } else {
        g_print ("  no stats\n");
      }

      g_free (s->desc);
      g_free (s);
    }
  }
}
#endif

/* Kids, use the functions from libgstpbutils in gst-plugins-base in your
 * own code (we can't do that here because it would introduce a circular
 * dependency) */
static gboolean
gst_is_missing_plugin_message (GstMessage * msg)
{
  if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ELEMENT
      || gst_message_get_structure (msg) == NULL)
    return FALSE;

  return gst_structure_has_name (gst_message_get_structure (msg),
      "missing-plugin");
}

static const gchar *
gst_missing_plugin_message_get_description (GstMessage * msg)
{
  return gst_structure_get_string (gst_message_get_structure (msg), "name");
}

static void
print_error_message (GstMessage * msg)
{
  GError *err = NULL;
  gchar *name, *debug = NULL;

  name = gst_object_get_path_string (msg->src);
  gst_message_parse_error (msg, &err, &debug);

  g_printerr (_("ERROR: from element %s: %s\n"), name, err->message);
  if (debug != NULL)
    g_printerr (_("Additional debug info:\n%s\n"), debug);

  g_clear_error (&err);
  g_free (debug);
  g_free (name);
}

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str = NULL;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str)) {
        g_warning ("Couldn't fetch string for %s tag", tag);
        g_assert_not_reached ();
      }
    } else if (gst_tag_get_type (tag) == GST_TYPE_SAMPLE) {
      GstSample *sample = NULL;

      if (gst_tag_list_get_sample_index (list, tag, i, &sample)) {
        GstBuffer *img = gst_sample_get_buffer (sample);
        GstCaps *caps = gst_sample_get_caps (sample);

        if (img) {
          if (caps) {
            gchar *caps_str;

            caps_str = gst_caps_to_string (caps);
            str = g_strdup_printf ("buffer of %" G_GSIZE_FORMAT " bytes, "
                "type: %s", gst_buffer_get_size (img), caps_str);
            g_free (caps_str);
          } else {
            str = g_strdup_printf ("buffer of %" G_GSIZE_FORMAT " bytes",
                gst_buffer_get_size (img));
          }
        } else {
          str = g_strdup ("NULL buffer");
        }
      } else {
        g_warning ("Couldn't fetch sample for %s tag", tag);
        g_assert_not_reached ();
      }
      gst_sample_unref (sample);
    } else if (gst_tag_get_type (tag) == GST_TYPE_DATE_TIME) {
      GstDateTime *dt = NULL;

      gst_tag_list_get_date_time_index (list, tag, i, &dt);
      if (!gst_date_time_has_time (dt)) {
        str = gst_date_time_to_iso8601_string (dt);
      } else {
        gdouble tz_offset = gst_date_time_get_time_zone_offset (dt);
        gchar tz_str[32];

        if (tz_offset != 0.0) {
          g_snprintf (tz_str, sizeof (tz_str), "(UTC %s%gh)",
              (tz_offset > 0.0) ? "+" : "", tz_offset);
        } else {
          g_snprintf (tz_str, sizeof (tz_str), "(UTC)");
        }

        str = g_strdup_printf ("%04u-%02u-%02u %02u:%02u:%02u %s",
            gst_date_time_get_year (dt), gst_date_time_get_month (dt),
            gst_date_time_get_day (dt), gst_date_time_get_hour (dt),
            gst_date_time_get_minute (dt), gst_date_time_get_second (dt),
            tz_str);
      }
      gst_date_time_unref (dt);
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (str) {
      PRINT ("%16s: %s\n", i == 0 ? gst_tag_get_nick (tag) : "", str);
      g_free (str);
    }
  }
}

static void
print_tag_foreach (const GstTagList * tags, const gchar * tag,
    gpointer user_data)
{
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);

  if (!gst_tag_list_copy_value (&val, tags, tag))
    return;

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);

  g_value_unset (&val);
}

#define MAX_INDENT 40

static void
print_toc_entry (gpointer data, gpointer user_data)
{
  GstTocEntry *entry = (GstTocEntry *) data;
  const gchar spc[MAX_INDENT + 1] = "                                        ";
  guint indent = MIN (GPOINTER_TO_UINT (user_data), MAX_INDENT);
  const GstTagList *tags;
  GList *subentries;
  gint64 start, stop;

  gst_toc_entry_get_start_stop_times (entry, &start, &stop);

  PRINT ("%s%s:", &spc[MAX_INDENT - indent],
      gst_toc_entry_type_get_nick (gst_toc_entry_get_entry_type (entry)));
  if (GST_CLOCK_TIME_IS_VALID (start)) {
    PRINT (" start: %" GST_TIME_FORMAT, GST_TIME_ARGS (start));
  }
  if (GST_CLOCK_TIME_IS_VALID (stop)) {
    PRINT (" stop: %" GST_TIME_FORMAT, GST_TIME_ARGS (stop));
  }
  PRINT ("\n");
  indent += 2;

  /* print tags */
  tags = gst_toc_entry_get_tags (entry);
  if (tags)
    gst_tag_list_foreach (tags, print_tag_foreach, GUINT_TO_POINTER (indent));

  /* loop over sub-toc entries */
  subentries = gst_toc_entry_get_sub_entries (entry);
  g_list_foreach (subentries, print_toc_entry, GUINT_TO_POINTER (indent));
}

#if defined(G_OS_UNIX) || defined(G_OS_WIN32)
static guint signal_watch_id;
#if defined(G_OS_WIN32)
static GstElement *intr_pipeline;
#endif
#endif

#if defined(G_OS_UNIX) || defined(G_OS_WIN32)
/* As the interrupt handler is dispatched from GMainContext as a GSourceFunc
 * handler, we can react to this by posting a message. */
static gboolean
intr_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;

  PRINT ("handling interrupt.\n");

  /* post an application specific message */
  gst_element_post_message (GST_ELEMENT (pipeline),
      gst_message_new_application (GST_OBJECT (pipeline),
          gst_structure_new ("GstLaunchInterrupt",
              "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));

  /* remove signal handler */
  signal_watch_id = 0;
  return FALSE;
}

#if defined(G_OS_WIN32)         /* G_OS_UNIX */
static BOOL WINAPI
w32_intr_handler (DWORD dwCtrlType)
{
  intr_handler ((gpointer) intr_pipeline);
  intr_pipeline = NULL;
  return TRUE;
}
#endif /* G_OS_WIN32 */
#endif /* G_OS_UNIX */

/* returns ELR_ERROR if there was an error
 * or ELR_INTERRUPT if we caught a keyboard interrupt
 * or ELR_NO_ERROR otherwise. */
static EventLoopResult
event_loop (GstElement * pipeline, gboolean blocking, gboolean do_progress,
    GstState target_state)
{
  GstBus *bus;
  GstMessage *message = NULL;
  EventLoopResult res = ELR_NO_ERROR;
  gboolean buffering = FALSE, in_progress = FALSE;
  gboolean prerolled = target_state != GST_STATE_PAUSED;

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#elif defined(G_OS_WIN32)
  intr_pipeline = NULL;
  if (SetConsoleCtrlHandler (w32_intr_handler, TRUE))
    intr_pipeline = pipeline;
#endif

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, blocking ? -1 : 0);

    /* if the poll timed out, only when !blocking */
    if (message == NULL)
      goto exit;

    /* check if we need to dump messages to the console */
    if (messages) {
      GstObject *src_obj;
      const GstStructure *s;
      guint32 seqnum;

      seqnum = gst_message_get_seqnum (message);

      s = gst_message_get_structure (message);

      src_obj = GST_MESSAGE_SRC (message);

      if (GST_IS_ELEMENT (src_obj)) {
        PRINT (_("Got message #%u from element \"%s\" (%s): "),
            (guint) seqnum, GST_ELEMENT_NAME (src_obj),
            GST_MESSAGE_TYPE_NAME (message));
      } else if (GST_IS_PAD (src_obj)) {
        PRINT (_("Got message #%u from pad \"%s:%s\" (%s): "),
            (guint) seqnum, GST_DEBUG_PAD_NAME (src_obj),
            GST_MESSAGE_TYPE_NAME (message));
      } else if (GST_IS_OBJECT (src_obj)) {
        PRINT (_("Got message #%u from object \"%s\" (%s): "),
            (guint) seqnum, GST_OBJECT_NAME (src_obj),
            GST_MESSAGE_TYPE_NAME (message));
      } else {
        PRINT (_("Got message #%u (%s): "), (guint) seqnum,
            GST_MESSAGE_TYPE_NAME (message));
      }

      if (s) {
        gchar *sstr;

        sstr = gst_structure_to_string (s);
        PRINT ("%s\n", sstr);
        g_free (sstr);
      } else {
        PRINT ("no message details\n");
      }
    }

    switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_NEW_CLOCK:
      {
        GstClock *clock;

        gst_message_parse_new_clock (message, &clock);

        PRINT ("New clock: %s\n", (clock ? GST_OBJECT_NAME (clock) : "NULL"));
        break;
      }
      case GST_MESSAGE_CLOCK_LOST:
        PRINT ("Clock lost, selecting a new one\n");
        gst_element_set_state (pipeline, GST_STATE_PAUSED);
        gst_element_set_state (pipeline, GST_STATE_PLAYING);
        break;
      case GST_MESSAGE_EOS:{
        waiting_eos = FALSE;
        PRINT (_("Got EOS from element \"%s\".\n"),
            GST_MESSAGE_SRC_NAME (message));
        goto exit;
      }
      case GST_MESSAGE_TAG:
        if (tags) {
          GstTagList *tag_list;

          if (GST_IS_ELEMENT (GST_MESSAGE_SRC (message))) {
            PRINT (_("FOUND TAG      : found by element \"%s\".\n"),
                GST_MESSAGE_SRC_NAME (message));
          } else if (GST_IS_PAD (GST_MESSAGE_SRC (message))) {
            PRINT (_("FOUND TAG      : found by pad \"%s:%s\".\n"),
                GST_DEBUG_PAD_NAME (GST_MESSAGE_SRC (message)));
          } else if (GST_IS_OBJECT (GST_MESSAGE_SRC (message))) {
            PRINT (_("FOUND TAG      : found by object \"%s\".\n"),
                GST_MESSAGE_SRC_NAME (message));
          } else {
            PRINT (_("FOUND TAG\n"));
          }

          gst_message_parse_tag (message, &tag_list);
          gst_tag_list_foreach (tag_list, print_tag, NULL);
          gst_tag_list_unref (tag_list);
        }
        break;
      case GST_MESSAGE_TOC:
        if (toc) {
          GstToc *toc;
          GList *entries;
          gboolean updated;

          if (GST_IS_ELEMENT (GST_MESSAGE_SRC (message))) {
            PRINT (_("FOUND TOC      : found by element \"%s\".\n"),
                GST_MESSAGE_SRC_NAME (message));
          } else if (GST_IS_OBJECT (GST_MESSAGE_SRC (message))) {
            PRINT (_("FOUND TOC      : found by object \"%s\".\n"),
                GST_MESSAGE_SRC_NAME (message));
          } else {
            PRINT (_("FOUND TOC\n"));
          }

          gst_message_parse_toc (message, &toc, &updated);
          /* recursively loop over toc entries */
          entries = gst_toc_get_entries (toc);
          g_list_foreach (entries, print_toc_entry, GUINT_TO_POINTER (0));
          gst_toc_unref (toc);
        }
        break;
      case GST_MESSAGE_INFO:{
        GError *gerror;
        gchar *debug;
        gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

        gst_message_parse_info (message, &gerror, &debug);
        if (debug) {
          PRINT (_("INFO:\n%s\n"), debug);
        }
        g_clear_error (&gerror);
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
        PRINT (_("WARNING: from element %s: %s\n"), name, gerror->message);
        if (debug) {
          PRINT (_("Additional debug info:\n%s\n"), debug);
        }
        g_clear_error (&gerror);
        g_free (debug);
        g_free (name);
        break;
      }
      case GST_MESSAGE_ERROR:{
        /* dump graph on error */
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.error");

        print_error_message (message);

        /* we have an error */
        res = ELR_ERROR;
        goto exit;
      }
      case GST_MESSAGE_STATE_CHANGED:{
        GstState old, new, pending;

        /* we only care about pipeline state change messages */
        if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
          break;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        /* if we reached the final target state, exit */
        if (target_state == GST_STATE_PAUSED && new == target_state) {
          prerolled = TRUE;
          /* ignore when we are buffering since then we mess with the states
           * ourselves. */
          if (buffering) {
            PRINT (_("Prerolled, waiting for buffering to finish...\n"));
            break;
          }
          if (in_progress) {
            PRINT (_("Prerolled, waiting for progress to finish...\n"));
            break;
          }
          goto exit;
        }
        /* else not an interesting message */
        break;
      }
      case GST_MESSAGE_BUFFERING:{
        gint percent;

        gst_message_parse_buffering (message, &percent);
        PRINT ("%s %d%%  \r", _("buffering..."), percent);

        /* no state management needed for live pipelines */
        if (is_live)
          break;

        if (percent == 100) {
          /* a 100% message means buffering is done */
          buffering = FALSE;
          /* if the desired state is playing, go back */
          if (target_state == GST_STATE_PLAYING) {
            PRINT (_("Done buffering, setting pipeline to PLAYING ...\n"));
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
          } else if (prerolled && !in_progress)
            goto exit;
        } else {
          /* buffering busy */
          if (!buffering && target_state == GST_STATE_PLAYING) {
            /* we were not buffering but PLAYING, PAUSE  the pipeline. */
            PRINT (_("Buffering, setting pipeline to PAUSED ...\n"));
            gst_element_set_state (pipeline, GST_STATE_PAUSED);
          }
          buffering = TRUE;
        }
        break;
      }
      case GST_MESSAGE_LATENCY:
      {
        PRINT (_("Redistribute latency...\n"));
        gst_bin_recalculate_latency (GST_BIN (pipeline));
        break;
      }
      case GST_MESSAGE_REQUEST_STATE:
      {
        GstState state;
        gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

        gst_message_parse_request_state (message, &state);

        PRINT (_("Setting state to %s as requested by %s...\n"),
            gst_element_state_get_name (state), name);

        gst_element_set_state (pipeline, state);

        g_free (name);
        break;
      }
      case GST_MESSAGE_APPLICATION:{
        const GstStructure *s;

        s = gst_message_get_structure (message);

        if (gst_structure_has_name (s, "GstLaunchInterrupt")) {
          /* this application message is posted when we caught an interrupt and
           * we need to stop the pipeline. */
          PRINT (_("Interrupt: Stopping pipeline ...\n"));
          res = ELR_INTERRUPT;
          goto exit;
        }
        break;
      }
      case GST_MESSAGE_PROGRESS:
      {
        GstProgressType type;
        gchar *code, *text;

        gst_message_parse_progress (message, &type, &code, &text);

        switch (type) {
          case GST_PROGRESS_TYPE_START:
          case GST_PROGRESS_TYPE_CONTINUE:
            if (do_progress) {
              in_progress = TRUE;
              blocking = TRUE;
            }
            break;
          case GST_PROGRESS_TYPE_COMPLETE:
          case GST_PROGRESS_TYPE_CANCELED:
          case GST_PROGRESS_TYPE_ERROR:
            in_progress = FALSE;
            break;
          default:
            break;
        }
        PRINT (_("Progress: (%s) %s\n"), code, text);
        g_free (code);
        g_free (text);

        if (do_progress && !in_progress && !buffering && prerolled)
          goto exit;
        break;
      }
      case GST_MESSAGE_ELEMENT:{
        if (gst_is_missing_plugin_message (message)) {
          const gchar *desc;

          desc = gst_missing_plugin_message_get_description (message);
          PRINT (_("Missing element: %s\n"), desc ? desc : "(no description)");
        }
        break;
      }
      case GST_MESSAGE_HAVE_CONTEXT:{
        GstContext *context;
        const gchar *context_type;
        gchar *context_str;

        gst_message_parse_have_context (message, &context);

        context_type = gst_context_get_context_type (context);
        context_str =
            gst_structure_to_string (gst_context_get_structure (context));
        PRINT (_("Got context from element '%s': %s=%s\n"),
            GST_ELEMENT_NAME (GST_MESSAGE_SRC (message)), context_type,
            context_str);
        g_free (context_str);
        gst_context_unref (context);
        break;
      }
      case GST_MESSAGE_PROPERTY_NOTIFY:{
        const GValue *val;
        const gchar *name;
        GstObject *obj;
        gchar *val_str = NULL;
        gchar **ex_prop, *obj_name;

        if (quiet)
          break;

        gst_message_parse_property_notify (message, &obj, &name, &val);

        /* Let's not print anything for excluded properties... */
        ex_prop = exclude_args;
        while (ex_prop != NULL && *ex_prop != NULL) {
          if (strcmp (name, *ex_prop) == 0)
            break;
          ex_prop++;
        }
        if (ex_prop != NULL && *ex_prop != NULL)
          break;

        obj_name = gst_object_get_path_string (GST_OBJECT (obj));
        if (val != NULL) {
          if (G_VALUE_HOLDS_STRING (val))
            val_str = g_value_dup_string (val);
          else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
            val_str = gst_caps_to_string (g_value_get_boxed (val));
          else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
            val_str = gst_tag_list_to_string (g_value_get_boxed (val));
          else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE)
            val_str = gst_structure_to_string (g_value_get_boxed (val));
          else
            val_str = gst_value_serialize (val);
        } else {
          val_str = g_strdup ("(no value)");
        }

        g_print ("%s: %s = %s\n", obj_name, name, val_str);
        g_free (obj_name);
        g_free (val_str);
        break;
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
#ifdef G_OS_UNIX
    if (signal_watch_id > 0)
      g_source_remove (signal_watch_id);
#elif defined(G_OS_WIN32)
    intr_pipeline = NULL;
    SetConsoleCtrlHandler (w32_intr_handler, FALSE);
#endif
    return res;
  }
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GstElement *pipeline = (GstElement *) data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:
      /* we only care about pipeline state change messages */
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        GstState old, new, pending;
        gchar *state_transition_name;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));

        /* dump graph for (some) pipeline state changes */
        {
          gchar *dump_name = g_strconcat ("gst-launch.", state_transition_name,
              NULL);
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
          g_free (dump_name);
        }

        /* place a marker into e.g. strace logs */
        {
          gchar *access_name = g_strconcat (g_get_tmp_dir (), G_DIR_SEPARATOR_S,
              "gst-launch", G_DIR_SEPARATOR_S, state_transition_name, NULL);
          g_file_test (access_name, G_FILE_TEST_EXISTS);
          g_free (access_name);
        }

        g_free (state_transition_name);
      }
      break;
    default:
      break;
  }
  return GST_BUS_PASS;
}

int
main (int argc, char *argv[])
{
  /* options */
  gboolean verbose = FALSE;
  gboolean no_fault = FALSE;
  gboolean eos_on_shutdown = FALSE;
#if 0
  gboolean check_index = FALSE;
#endif
  gchar *savefile = NULL;
#ifndef GST_DISABLE_OPTION_PARSING
  GOptionEntry options[] = {
    {"tags", 't', 0, G_OPTION_ARG_NONE, &tags,
        N_("Output tags (also known as metadata)"), NULL},
    {"toc", 'c', 0, G_OPTION_ARG_NONE, &toc,
        N_("Output TOC (chapters and editions)"), NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        N_("Output status information and property notifications"), NULL},
    {"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
        N_("Do not print any progress information"), NULL},
    {"messages", 'm', 0, G_OPTION_ARG_NONE, &messages,
        N_("Output messages"), NULL},
    {"exclude", 'X', 0, G_OPTION_ARG_STRING_ARRAY, &exclude_args,
          N_("Do not output status information for the specified property "
              "if verbose output is enabled (can be used multiple times)"),
        N_("PROPERTY-NAME")},
    {"no-fault", 'f', 0, G_OPTION_ARG_NONE, &no_fault,
        N_("Do not install a fault handler"), NULL},
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        N_("Force EOS on sources before shutting the pipeline down"), NULL},
#if 0
    {"index", 'i', 0, G_OPTION_ARG_NONE, &check_index,
        N_("Gather and print index statistics"), NULL},
#endif
    GST_TOOLS_GOPTION_VERSION,
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;
#endif
#if 0
  GstIndex *index;
  GPtrArray *index_stats = NULL;
#endif
  gchar **argvn;
  GError *error = NULL;
  gulong deep_notify_id = 0;
  gint res = 0;

  free (malloc (8));            /* -lefence */

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-launch-" GST_API_VERSION);
  /* Ensure XInitThreads() is called if/when needed */
  g_setenv ("GST_GL_XINITTHREADS", "1", TRUE);

#ifndef GST_DISABLE_OPTION_PARSING
  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    if (err)
      g_printerr ("Error initializing: %s\n", GST_STR_NULL (err->message));
    else
      g_printerr ("Error initializing: Unknown error!\n");
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (1);
  }
  g_option_context_free (ctx);
#else
  gst_init (&argc, &argv);
#endif

  gst_tools_print_version ();

#ifdef G_OS_UNIX
  if (!no_fault)
    fault_setup ();
#endif

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
          GST_STR_NULL (error->message));
      g_clear_error (&error);
    } else {
      g_printerr (_("ERROR: pipeline could not be constructed.\n"));
    }
    return 1;
  } else if (error) {
    g_printerr (_("WARNING: erroneous pipeline: %s\n"),
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return 1;
  }

  if (!savefile) {
    GstState state, pending;
    GstStateChangeReturn ret;
    GstBus *bus;

    /* If the top-level object is not a pipeline, place it in a pipeline. */
    if (!GST_IS_PIPELINE (pipeline)) {
      GstElement *real_pipeline = gst_element_factory_make ("pipeline", NULL);

      if (real_pipeline == NULL) {
        g_printerr (_("ERROR: the 'pipeline' element wasn't found.\n"));
        return 1;
      }
      gst_bin_add (GST_BIN (real_pipeline), pipeline);
      pipeline = real_pipeline;
    }
    if (verbose) {
      deep_notify_id =
          gst_element_add_property_deep_notify_watch (pipeline, NULL, TRUE);
    }
#if 0
    if (check_index) {
      /* gst_index_new() creates a null-index, it does not store anything, but
       * the entry-added signal works and this is what we use to build the
       * statistics */
      index = gst_index_new ();
      if (index) {
        index_stats = g_ptr_array_new ();
        g_signal_connect (G_OBJECT (index), "entry-added",
            G_CALLBACK (entry_added), index_stats);
        g_object_set (G_OBJECT (index), "resolver", GST_INDEX_RESOLVER_GTYPE,
            NULL);
        gst_element_set_index (pipeline, index);
      }
    }
#endif

    bus = gst_element_get_bus (pipeline);
    gst_bus_set_sync_handler (bus, bus_sync_handler, (gpointer) pipeline, NULL);
    gst_object_unref (bus);

    PRINT (_("Setting pipeline to PAUSED ...\n"));
    ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);

    switch (ret) {
      case GST_STATE_CHANGE_FAILURE:
        g_printerr (_("ERROR: Pipeline doesn't want to pause.\n"));
        res = -1;
        event_loop (pipeline, FALSE, FALSE, GST_STATE_VOID_PENDING);
        goto end;
      case GST_STATE_CHANGE_NO_PREROLL:
        PRINT (_("Pipeline is live and does not need PREROLL ...\n"));
        is_live = TRUE;
        break;
      case GST_STATE_CHANGE_ASYNC:
        PRINT (_("Pipeline is PREROLLING ...\n"));
        caught_error = event_loop (pipeline, TRUE, TRUE, GST_STATE_PAUSED);
        if (caught_error) {
          g_printerr (_("ERROR: pipeline doesn't want to preroll.\n"));
          res = caught_error;
          goto end;
        }
        state = GST_STATE_PAUSED;
        /* fallthrough */
      case GST_STATE_CHANGE_SUCCESS:
        PRINT (_("Pipeline is PREROLLED ...\n"));
        break;
    }

    caught_error = event_loop (pipeline, FALSE, TRUE, GST_STATE_PLAYING);

    if (caught_error) {
      g_printerr (_("ERROR: pipeline doesn't want to preroll.\n"));
      res = caught_error;
    } else {
      GstClockTime tfthen, tfnow;
      GstClockTimeDiff diff;

      PRINT (_("Setting pipeline to PLAYING ...\n"));

      if (gst_element_set_state (pipeline,
              GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        GstMessage *err_msg;
        GstBus *bus;

        g_printerr (_("ERROR: pipeline doesn't want to play.\n"));
        bus = gst_element_get_bus (pipeline);
        if ((err_msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, 0))) {
          print_error_message (err_msg);
          gst_message_unref (err_msg);
        }
        gst_object_unref (bus);
        res = -1;
        goto end;
      }

      tfthen = gst_util_get_timestamp ();
      caught_error = event_loop (pipeline, TRUE, FALSE, GST_STATE_PLAYING);
      res = caught_error;
      if (eos_on_shutdown && caught_error != ELR_NO_ERROR) {
        gboolean ignore_errors;

        waiting_eos = TRUE;
        if (caught_error == ELR_INTERRUPT) {
          PRINT (_("EOS on shutdown enabled -- Forcing EOS on the pipeline\n"));
          gst_element_send_event (pipeline, gst_event_new_eos ());
          ignore_errors = FALSE;
        } else {
          PRINT (_("EOS on shutdown enabled -- waiting for EOS after Error\n"));
          ignore_errors = TRUE;
        }
        PRINT (_("Waiting for EOS...\n"));

        while (TRUE) {
          caught_error = event_loop (pipeline, TRUE, FALSE, GST_STATE_PLAYING);

          if (caught_error == ELR_NO_ERROR) {
            /* we got EOS */
            PRINT (_("EOS received - stopping pipeline...\n"));
            break;
          } else if (caught_error == ELR_INTERRUPT) {
            PRINT (_
                ("Interrupt while waiting for EOS - stopping pipeline...\n"));
            res = caught_error;
            break;
          } else if (caught_error == ELR_ERROR) {
            if (!ignore_errors) {
              PRINT (_("An error happened while waiting for EOS\n"));
              res = caught_error;
              break;
            }
          }
        }
      }
      tfnow = gst_util_get_timestamp ();

      diff = GST_CLOCK_DIFF (tfthen, tfnow);

      PRINT (_("Execution ended after %" GST_TIME_FORMAT "\n"),
          GST_TIME_ARGS (diff));
    }

    PRINT (_("Setting pipeline to PAUSED ...\n"));
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    if (caught_error == ELR_NO_ERROR)
      gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);

    /* iterate mainloop to process pending stuff */
    while (g_main_context_iteration (NULL, FALSE));

    /* No need to see all those pad caps going to NULL etc., it's just noise */
    if (deep_notify_id != 0)
      g_signal_handler_disconnect (pipeline, deep_notify_id);

    PRINT (_("Setting pipeline to READY ...\n"));
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);

#if 0
    if (check_index) {
      print_index_stats (index_stats);
      g_ptr_array_free (index_stats, TRUE);
    }
#endif

  end:
    PRINT (_("Setting pipeline to NULL ...\n"));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  PRINT (_("Freeing pipeline ...\n"));
  gst_object_unref (pipeline);

  gst_deinit ();

  return res;
}
