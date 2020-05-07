/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-transcoding.c - CLI tool to validate transcoding operations
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include <stdlib.h>
#include <string.h>
#include <locale.h>             /* for LC_ALL */

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/validate.h>
#include <gst/pbutils/encoding-profile.h>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-bin-monitor.h>

static gint ret = 0;
static GMainLoop *mainloop;
static GstElement *pipeline, *encodebin, *sink;
static GstEncodingProfile *encoding_profile = NULL;
static gboolean eos_on_shutdown = FALSE;
static gboolean force_reencoding = FALSE;

static gboolean buffering = FALSE;
static gboolean is_live = FALSE;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  gst_validate_printf (NULL, "interrupt received.\n");

  if (eos_on_shutdown) {
    gst_validate_printf (NULL, "Sending EOS to the pipeline\n");
    eos_on_shutdown = FALSE;
    gst_element_send_event (GST_ELEMENT_CAST (user_data), gst_event_new_eos ());
    return TRUE;
  }
  g_main_loop_quit (mainloop);

  /* remove signal handler */
  return FALSE;
}
#endif /* G_OS_UNIX */

static gboolean
_execute_set_restriction (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstCaps *caps;
  GType profile_type = G_TYPE_NONE;
  const gchar *restriction_caps, *profile_type_name, *profile_name;

  restriction_caps =
      gst_structure_get_string (action->structure, "restriction-caps");
  profile_type_name =
      gst_structure_get_string (action->structure, "profile-type");
  profile_name = gst_structure_get_string (action->structure, "profile-name");

  if (profile_type_name) {
    profile_type = g_type_from_name (profile_type_name);

    if (profile_type == G_TYPE_NONE) {
      gst_validate_abort ("Profile name %s not known", profile_name);

      return FALSE;
    } else if (profile_type == GST_TYPE_ENCODING_CONTAINER_PROFILE) {
      gst_validate_abort ("Can not set restrictions on container profiles");

      return FALSE;
    }
  } else if (profile_name == NULL) {
    if (g_strrstr (restriction_caps, "audio/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_AUDIO_PROFILE;
    else if (g_strrstr (restriction_caps, "video/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_VIDEO_PROFILE;
    else {
      g_error
          ("No information on what profiles to apply action, you should set either "
          "profile_name or profile_type_name and the caps %s give us no hint",
          restriction_caps);

      return FALSE;
    }
  }

  caps = gst_caps_from_string (restriction_caps);
  if (caps == NULL) {
    gst_validate_abort ("Could not parse caps: %s", restriction_caps);

    return FALSE;
  }

  if (GST_IS_ENCODING_CONTAINER_PROFILE (encoding_profile)) {
    gboolean found = FALSE;
    const GList *tmp;

    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (encoding_profile)); tmp;
        tmp = tmp->next) {
      GstEncodingProfile *profile = tmp->data;

      if (profile_type != G_TYPE_NONE
          && G_OBJECT_TYPE (profile) == profile_type) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      } else if (profile_name
          && g_strcmp0 (gst_encoding_profile_get_name (profile),
              profile_name) == 0) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      }
    }

    if (!found) {
      gst_validate_abort ("Could not find profile for %s%s",
          profile_type_name ? profile_type_name : "",
          profile_name ? profile_name : "");

      gst_caps_unref (caps);
      return FALSE;

    }
  }

  if (profile_type != G_TYPE_NONE) {
    gst_validate_printf (action,
        "setting caps to %s on profiles of type %s\n",
        restriction_caps, g_type_name (profile_type));
  } else {
    gst_validate_printf (action, "setting caps to %s on profile %s\n",
        restriction_caps, profile_name);

  }

  gst_caps_unref (caps);
  return TRUE;
}

typedef struct
{
  GMainLoop *mainloop;
  GstValidateMonitor *monitor;
} BusCallbackData;

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  BusCallbackData *bus_callback_data = data;
  GMainLoop *loop = bus_callback_data->mainloop;
  GstValidateMonitor *monitor = bus_callback_data->monitor;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        gchar *dotname;
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        if (new == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate-transcode.playing");
        }

        dotname = g_strdup_printf ("gst-validate-transcoding.%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dotname);
        g_free (dotname);
      }
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      if (!g_getenv ("GST_VALIDATE_SCENARIO"))
        g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_BUFFERING:{
      gint percent;
      GstState target_state = GST_STATE_PLAYING;
      gboolean monitor_handles_state;

      GParamSpec *spec =
          g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "sync");

      if (spec) {
        gboolean sync;

        /* Never do buffering if the sink is not synchronizing on the clock */
        g_object_get (sink, "sync", &sync, NULL);
        if (!sync)
          return TRUE;
      } else {
        return TRUE;
      }

      g_object_get (monitor, "handles-states", &monitor_handles_state, NULL);
      if (monitor_handles_state && GST_IS_VALIDATE_BIN_MONITOR (monitor)) {
        target_state =
            gst_validate_scenario_get_target_state (GST_VALIDATE_BIN_MONITOR
            (monitor)->scenario);
      }

      if (!buffering) {
        gst_validate_printf (NULL, "\n");
      }

      gst_message_parse_buffering (message, &percent);

      /* no state management needed for live pipelines */
      if (is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;

          if (target_state == GST_STATE_PLAYING) {
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
          }
        }
      } else {
        /* buffering... */
        if (!buffering) {
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
          buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      GstState state;

      gst_message_parse_request_state (message, &state);

      if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
          && state == GST_STATE_NULL) {
        GST_VALIDATE_REPORT (GST_MESSAGE_SRC (message),
            SCENARIO_ACTION_EXECUTION_ISSUE,
            "Force stopping a transcoding pipeline is not recommended"
            " you should make sure to finalize it using a EOS event");

        gst_validate_printf (pipeline, "State change request NULL, "
            "quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement * uridecodebin, GstPad * pad, GstElement * encodebin)
{
  GstCaps *caps;
  GstPad *sinkpad = NULL;

  caps = gst_pad_query_caps (pad, NULL);

  /* Ask encodebin for a compatible pad */
  GST_DEBUG_OBJECT (uridecodebin, "Pad added, caps: %" GST_PTR_FORMAT, caps);

  g_signal_emit_by_name (encodebin, "request-pad", caps, &sinkpad);
  if (caps)
    gst_caps_unref (caps);

  if (sinkpad == NULL) {
    GST_WARNING ("Couldn't get an encoding pad for pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return;
  }

  if (G_UNLIKELY (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GstCaps *othercaps = gst_pad_get_current_caps (sinkpad);
    caps = gst_pad_get_current_caps (pad);

    GST_ERROR ("Couldn't link pads \n\n%" GST_PTR_FORMAT "\n\n  and \n\n %"
        GST_PTR_FORMAT "\n\n", caps, othercaps);

    gst_caps_unref (caps);
    gst_caps_unref (othercaps);
  }

  gst_object_unref (sinkpad);
  return;
}

static void
create_transcoding_pipeline (gchar * uri, gchar * outuri)
{
  GstElement *src;

  pipeline = gst_pipeline_new ("encoding-pipeline");
  src = gst_element_factory_make ("uridecodebin", NULL);

  encodebin = gst_element_factory_make ("encodebin", NULL);
  g_object_set (encodebin, "avoid-reencoding", !force_reencoding, NULL);
  sink = gst_element_make_from_uri (GST_URI_SINK, outuri, "sink", NULL);
  g_assert (sink);

  g_object_set (src, "uri", uri, NULL);
  g_object_set (encodebin, "profile", encoding_profile, NULL);

  g_signal_connect (src, "pad-added", G_CALLBACK (pad_added_cb), encodebin);

  gst_bin_add_many (GST_BIN (pipeline), src, encodebin, sink, NULL);
  gst_element_link (encodebin, sink);
}

static gboolean
_parse_encoding_profile (const gchar * option_name, const gchar * profile_desc,
    gpointer udata, GError ** error)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, GST_TYPE_ENCODING_PROFILE);

  if (!gst_value_deserialize (&value, profile_desc)) {
    g_value_reset (&value);

    return FALSE;
  }

  encoding_profile = g_value_dup_object (&value);
  g_value_reset (&value);

  return TRUE;
}

static void
_register_actions (void)
{
/* *INDENT-OFF* */
  gst_validate_register_action_type ("set-restriction", "validate-transcoding", _execute_set_restriction,
      (GstValidateActionParameter []) {
        {
          .name = "restriction-caps",
          .description = "The restriction caps to set on the encodebin "
                         "encoding profile.\nSee gst_encoding_profile_set_restriction()",
          .mandatory = TRUE,
          .types = "GstCaps serialized as a string"
        },
        {NULL}
      },
      "Change the restriction caps on the fly",
      FALSE);

/* *INDENT-ON* */
}

int
main (int argc, gchar ** argv)
{
  guint i;
  GstBus *bus;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GOptionContext *ctx;
  int rep_err;
  GstStateChangeReturn sret;
  gchar *output_file = NULL;
  BusCallbackData bus_callback_data = { 0, };

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  GError *err = NULL;
  gchar *scenario = NULL, *configs = NULL;
  gboolean want_help = FALSE;
  gboolean list_scenarios = FALSE, inspect_action_type = FALSE;

  GOptionEntry options[] = {
    {"output-format", 'o', 0, G_OPTION_ARG_CALLBACK, &_parse_encoding_profile,
          "Set the properties to use for the encoding profile "
          "(in case of transcoding.) For example:\n"
          "video/mpegts:video/x-raw-yuv,width=1920,height=1080->video/x-h264:audio/x-ac3\n"
          "A preset name can be used by adding +presetname, eg:\n"
          "video/webm:video/x-vp8+mypreset:audio/x-vorbis\n"
          "The presence property of the profile can be specified with |<presence>, eg:\n"
          "video/webm:video/x-vp8|<presence>:audio/x-vorbis\n",
        "properties-values"},
    {"set-scenario", '\0', 0, G_OPTION_ARG_FILENAME, &scenario,
        "Let you set a scenario, it can be a full path to a scenario file"
          " or the name of the scenario (name of the file without the"
          " '.scenario' extension).", NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Select a config scenario (one including 'is-config=true' in its"
          " description). Specify multiple ones using ':' as separator."
          " This option overrides the GST_VALIDATE_SCENARIO environment variable.",
        NULL},
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "If an EOS event should be sent to the pipeline if an interrupt is "
          "received, instead of forcing the pipeline to stop. Sending an EOS "
          "will allow the transcoding to finish the files properly before "
          "exiting.", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the available scenarios that can be run", NULL},
    {"inspect-action-type", 't', 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the available action types with which to write scenarios"
          " if no parameter passed, it will list all available action types"
          " otherwise will print the full description of the wanted types",
        NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenarios",
        NULL},
    {"force-reencoding", 'r', 0, G_OPTION_ARG_NONE, &force_reencoding,
        "Whether to try to force reencoding, meaning trying to only remux "
          "if possible(default: TRUE)", NULL},
    {NULL}
  };

  setlocale (LC_ALL, "");
  /* There is a bug that make gst_init remove the help param when initializing,
   * it is FIXED in 1.0 */
  for (i = 1; i < argc; i++) {
    if (!g_strcmp0 (argv[i], "--help") || !g_strcmp0 (argv[i], "-h"))
      want_help = TRUE;
  }

  if (!want_help)
    gst_init (&argc, &argv);

  g_set_prgname ("gst-validate-transcoding-" GST_API_VERSION);
  ctx = g_option_context_new ("[input-uri] [output-uri]");
  g_option_context_set_summary (ctx, "Transcodes input-uri to output-uri, "
      "using the given encoding profile. The pipeline will be monitored for "
      "possible issues detection using the gst-validate lib."
      "\nCan also perform file conformance "
      "tests after transcoding to make sure the result is correct");
  g_option_context_add_main_entries (ctx, options, NULL);
  if (want_help) {
    g_option_context_add_group (ctx, gst_init_get_option_group ());
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  g_option_context_free (ctx);

  if (want_help)
    exit (0);

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
  }

  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      return 1;
    return 0;
  }


  _register_actions ();

  if (inspect_action_type) {
    if (gst_validate_print_action_types ((const gchar **) argv + 1, argc - 1))
      return 0;

    return -1;
  }

  if (argc != 3) {
    g_printerr ("%i arguments received, 2 expected.\n"
        "You should run the test using:\n"
        "    ./gst-validate-transcoding-1.0 <input-uri> <output-uri> [options]\n",
        argc - 1);
    return 1;
  }

  if (encoding_profile == NULL) {
    GST_INFO ("Creating default encoding profile");

    _parse_encoding_profile ("encoding-profile",
        "application/ogg:video/x-theora:audio/x-vorbis", NULL, NULL);
  }

  /* Create the pipeline */
  runner = gst_validate_runner_new ();
  create_transcoding_pipeline (argv[1], argv[2]);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  gst_validate_spin_on_fault_signals ();

  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  bus_callback_data.mainloop = mainloop;
  bus_callback_data.monitor = monitor;
  g_signal_connect (bus, "message", (GCallback) bus_callback,
      &bus_callback_data);

  gst_validate_printf (NULL, "Starting pipeline\n");
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      gst_validate_printf (NULL, "Pipeline failed to go to PLAYING state\n");
      ret = -1;
      goto exit;
    case GST_STATE_CHANGE_NO_PREROLL:
      gst_validate_printf (NULL, "Pipeline is live.\n");
      is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      gst_validate_printf (NULL, "Prerolling...\r");
      break;
    default:
      break;
  }

  g_main_loop_run (mainloop);

  rep_err = gst_validate_runner_exit (runner, TRUE);
  if (ret == 0)
    ret = rep_err;

exit:
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_clear_object (&encoding_profile);
  g_object_unref (pipeline);
  gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (monitor));
  g_object_unref (monitor);
  g_object_unref (runner);

#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif
  gst_validate_deinit ();
  gst_deinit ();

  gst_validate_printf (NULL, "\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
