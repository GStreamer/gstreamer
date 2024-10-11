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
#include <gst/transcoder/gsttranscoder.h>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-bin-monitor.h>

static gint ret = 0;
static GstValidateMonitor *monitor = NULL;
static GstValidateRunner *runner = NULL;
static GstTranscoder *transcoder = NULL;
static gboolean eos_on_shutdown = FALSE;

static gint
finish_transcoding (GstElement * pipeline, gint ret)
{
  int rep_err;

  if (!runner) {
    ret = 1;
    goto done;
  }

  rep_err = gst_validate_runner_exit (runner, TRUE);
  if (ret == 0)
    ret = rep_err;

  gst_clear_object (&transcoder);
  gst_clear_object (&pipeline);
  gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (monitor));
  g_object_unref (monitor);
  g_object_unref (runner);

  gst_validate_deinit ();
  gst_deinit ();

done:
  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);

  exit (ret);
  return ret;
}

#ifdef G_OS_UNIX
static gboolean
intr_handler (GstElement * pipeline)
{
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.interrupted");

  if (eos_on_shutdown) {
    eos_on_shutdown = FALSE;
    gst_element_send_event (pipeline, gst_event_new_eos ());
    return TRUE;

  }

  finish_transcoding (pipeline, 1);
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
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);
  GstEncodingProfile *encoding_profile;

  g_object_get (pipeline, "profile", &encoding_profile, NULL);
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

static int
real_main (int argc, gchar ** argv)
{
  guint i;
  GOptionContext *ctx;
  gchar *output_file = NULL;

  const gchar *profile_str;

  GError *err = NULL;
  gchar *scenario = NULL, *configs = NULL;
  gboolean want_help = FALSE;
  gboolean list_scenarios = FALSE, inspect_action_type = FALSE;
  GstElement *pipeline = NULL;
  gboolean force_reencoding = TRUE;

  GOptionEntry options[] = {
    {"output-format", 'o', 0, G_OPTION_ARG_STRING, &profile_str,
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
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "If an EOS event should be sent to the pipeline if an interrupt is "
          "received, instead of forcing the pipeline to stop. Sending an EOS "
          "will allow the transcoding to finish the files properly before "
          "exiting.", NULL},
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

  if (profile_str == NULL) {
    GST_INFO ("Creating default encoding profile");

    profile_str = "application/ogg:video/x-theora:audio/x-vorbis";
  }

  transcoder = gst_transcoder_new (argv[1], argv[2], profile_str);
  gst_transcoder_set_avoid_reencoding (transcoder, !force_reencoding);

  /* Create the pipeline */
  runner = gst_validate_runner_new ();

  pipeline = gst_transcoder_get_pipeline (transcoder);
#ifdef G_OS_UNIX
  g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  gst_validate_spin_on_fault_signals ();

  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  if (!runner) {
    gst_object_unref (pipeline);
    gst_object_unref (transcoder);
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  if (!gst_transcoder_run (transcoder, &err)) {
    ret = -1;
    GST_ERROR ("\nFAILURE: %s", err->message);
  }

  return finish_transcoding (pipeline, ret);
}

int
main (int argc, char *argv[])
{
  int ret;

#ifdef G_OS_WIN32
  argv = g_win32_get_command_line ();
#endif

#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  ret = gst_macos_main ((GstMainFunc) real_main, argc, argv, NULL);
#else
  ret = real_main (argc, argv);
#endif

#ifdef G_OS_WIN32
  g_strfreev (argv);
#endif

  return ret;
}
