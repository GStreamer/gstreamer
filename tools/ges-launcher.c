/* GStreamer Editing Services
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
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

#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#include "ges-launcher.h"
#include "ges-validate.h"
#include "utils.h"

typedef struct
{
  gboolean mute;
  gboolean disable_mixing;
  gchar *save_path;
  gchar *load_path;
  GESTrackType track_types;
  gboolean needs_set_state;
  gboolean smartrender;
  gchar *scenario;
  gchar *format;
  gchar *outputuri;
  gchar *encoding_profile;
  gchar *videosink;
  gchar *audiosink;
  gboolean list_transitions;
  gchar *sanitized_timeline;
} ParsedOptions;

struct _GESLauncherPrivate
{
  GESTimeline *timeline;
  GESPipeline *pipeline;
  gboolean seenerrors;
#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif
  ParsedOptions parsed_options;
};

G_DEFINE_TYPE (GESLauncher, ges_launcher, G_TYPE_APPLICATION);

static const gchar *HELP_SUMMARY =
    "ges-launch renders a timeline, which can be specified on the commandline,\n"
    "or loaded from a xges file using the -l option.\n\n"
    "A timeline is a list of files, patterns, and transitions to be rendered\n"
    "one after the other. Files and Patterns provide video and audio as the\n"
    "primary input, and transitions animate between the end of one file/pattern\n"
    "and the beginning of a new one. Hence, transitions can only be listed\n"
    "in between patterns or files.\n\n"
    "A file is a triplet of filename, inpoint (in seconds) and\n"
    "duration (in seconds). If the duration is 0, the full file length is used.\n\n"
    "Patterns and transitions are triplets that begin with either \"+pattern\"\n"
    "or \"+transition\", followed by a <type> and duration (in seconds, must be\n"
    "greater than 0)\n\n"
    "Durations in all cases can be fractions of a second.\n\n"
    "========\nExamples\n========\n\n"
    "Play video1.ogv from inpoint 5 with duration 10 in seconds:\n"
    "$ ges-launch video1.ogv 5 10\n\n"
    "Crossfade:\n"
    "$ ges-launch video1.ogv 0 10 +transition crossfade 3.5 video2.ogv 0 10\n\n"
    "Render xges to ogv:\n"
    "$ ges-launch -l project.xges -o rendering.ogv\n\n"
    "Render xges to an XML encoding-profile called mymkv:\n"
    "$ ges-launch -l project.xges -o rendering.mkv -e mymkv\n\n"
    "Render to mp4:\n"
    "$ ges-launch -l project.xges -o out.mp4 \\\n"
    "             -f \"video/quicktime,variant=iso:video/x-h264:audio/mpeg,mpegversion=1,layer=3\"\n\n"
    "Render xges to WebM with 1920x1080 resolution:\n"
    "$ ges-launch -l project.xges -o out.webm \\\n"
    "             -f \"video/webm:video/x-raw,width=1920,height=1080->video/x-vp8:audio/x-vorbis\"\n\n"
    "A preset name can be used by adding +presetname:\n"
    "$ ges-launch -l project.xges -o out.webm \\\n"
    "             -f \"video/webm:video/x-vp8+presetname:x-vorbis\"\n\n"
    "The presence property of the profile can be specified with |<presence>:\n"
    "$ ges-launch -l project.xges -o out.ogv \\\n"
    "             -f \"application/ogg:video/x-theora|<presence>:audio/x-vorbis\"";

static gboolean
_parse_track_type (const gchar * option_name, const gchar * value,
    GESLauncher * self, GError ** error)
{
  ParsedOptions *opts = &self->priv->parsed_options;

  opts->track_types = get_flags_from_string (GES_TYPE_TRACK_TYPE, value);

  if (opts->track_types == 0)
    return FALSE;

  return TRUE;
}

static gboolean
_timeline_set_user_options (GESLauncher * self, GESTimeline * timeline,
    const gchar * load_path)
{
  GList *tmp;
  GESTrack *tracka, *trackv;
  gboolean has_audio = FALSE, has_video = FALSE;
  ParsedOptions *opts = &self->priv->parsed_options;

retry:
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {

    if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_VIDEO)
      has_video = TRUE;
    else if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_AUDIO)
      has_audio = TRUE;

    if (opts->disable_mixing)
      ges_track_set_mixing (tmp->data, FALSE);

    if (!(GES_TRACK (tmp->data)->type & opts->track_types)) {
      ges_timeline_remove_track (timeline, tmp->data);
      goto retry;
    }
  }

  if (opts->scenario && !load_path) {
    if (!has_video && opts->track_types & GES_TRACK_TYPE_VIDEO) {
      trackv = GES_TRACK (ges_video_track_new ());

      if (opts->disable_mixing)
        ges_track_set_mixing (trackv, FALSE);

      if (!(ges_timeline_add_track (timeline, trackv)))
        return FALSE;
    }

    if (!has_audio && opts->track_types & GES_TRACK_TYPE_AUDIO) {
      tracka = GES_TRACK (ges_audio_track_new ());
      if (opts->disable_mixing)
        ges_track_set_mixing (tracka, FALSE);

      if (!(ges_timeline_add_track (timeline, tracka)))
        return FALSE;
    }
  }

  return TRUE;
}

static void
_project_loaded_cb (GESProject * project, GESTimeline * timeline,
    GESLauncher * self)
{
  ParsedOptions *opts = &self->priv->parsed_options;
  GST_INFO ("Project loaded, playing it");

  if (opts->save_path) {
    gchar *uri;
    GError *error = NULL;

    if (g_strcmp0 (opts->save_path, "+r") == 0) {
      uri = ges_project_get_uri (project);
    } else if (!(uri = ensure_uri (opts->save_path))) {
      g_error ("couldn't create uri for '%s", opts->save_path);

      self->priv->seenerrors = TRUE;
      g_application_release (G_APPLICATION (self));
    }

    g_print ("\nSaving project to %s\n", uri);
    ges_project_save (project, timeline, uri, NULL, TRUE, &error);
    g_free (uri);

    g_assert_no_error (error);
    if (error) {
      self->priv->seenerrors = TRUE;
      g_application_release (G_APPLICATION (self));
    }
  }

  _timeline_set_user_options (self, timeline, ges_project_get_uri (project));

  if (ges_project_get_uri (project)
      && ges_validate_activate (GST_PIPELINE (self->priv->pipeline),
          opts->scenario, &opts->needs_set_state) == FALSE) {
    g_error ("Could not activate scenario %s", opts->scenario);
    self->priv->seenerrors = TRUE;
    g_application_release (G_APPLICATION (self));
  }

  if (opts->needs_set_state
      && gst_element_set_state (GST_ELEMENT (self->priv->pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to start the pipeline\n");
  }
}

static void
_error_loading_asset_cb (GESProject * project, GError * error,
    const gchar * failed_id, GType extractable_type, GESLauncher * self)
{
  g_printerr ("Error loading asset %s: %s\n", failed_id, error->message);
  self->priv->seenerrors = TRUE;

  g_application_release (G_APPLICATION (self));
}

static gboolean
_create_timeline (GESLauncher * self, const gchar * serialized_timeline,
    const gchar * proj_uri, const gchar * scenario)
{
  GESProject *project;

  GError *error = NULL;

  if (proj_uri != NULL) {
    project = ges_project_new (proj_uri);
  } else if (scenario == NULL) {
    GST_ERROR ("serialized timeline is %s", serialized_timeline);
    project = ges_project_new (serialized_timeline);
  } else {
    project = ges_project_new (NULL);
  }

  g_signal_connect (project, "error-loading-asset",
      G_CALLBACK (_error_loading_asset_cb), self);
  g_signal_connect (project, "loaded", G_CALLBACK (_project_loaded_cb), self);

  self->priv->timeline =
      GES_TIMELINE (ges_asset_extract (GES_ASSET (project), &error));

  if (error) {
    g_error ("Could not create timeline, error: %s", error->message);

    return FALSE;
  }

  return TRUE;
}

typedef void (*sinkSettingFunction) (GESPipeline * pipeline,
    GstElement * element);

static gboolean
_set_sink (GESLauncher * self, const gchar * sink_desc,
    sinkSettingFunction set_func)
{
  if (sink_desc != NULL) {
    GError *err = NULL;
    GstElement *sink = gst_parse_bin_from_description (sink_desc, TRUE, &err);
    if (sink == NULL) {
      GST_ERROR ("could not create the requested videosink %s (err: %s), "
          "exiting", err ? err->message : "", sink_desc);
      return FALSE;
    }
    set_func (self->priv->pipeline, sink);
  }
  return TRUE;
}

static gboolean
_set_playback_details (GESLauncher * self)
{
  ParsedOptions *opts = &self->priv->parsed_options;

  if (!_set_sink (self, opts->videosink, ges_pipeline_preview_set_video_sink) ||
      !_set_sink (self, opts->audiosink, ges_pipeline_preview_set_audio_sink))
    return FALSE;

  return TRUE;
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, GESLauncher * self)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_WARNING:{
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->priv->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch.warning");
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg_info = NULL;

      gst_message_parse_error (message, &err, &dbg_info);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->priv->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch-error");
      g_printerr ("ERROR from element %s: %s\n", GST_OBJECT_NAME (message->src),
          err->message);
      g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_error_free (err);
      g_free (dbg_info);
      self->priv->seenerrors = TRUE;
      g_application_release (G_APPLICATION (self));
      break;
    }
    case GST_MESSAGE_EOS:
      g_printerr ("\nDone\n");
      g_application_release (G_APPLICATION (self));
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (self->priv->pipeline)) {
        gchar *dump_name;
        GstState old, new, pending;
        gchar *state_transition_name;

        gst_message_parse_state_changed (message, &old, &new, &pending);
        state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));
        dump_name = g_strconcat ("ges-launch.", state_transition_name, NULL);


        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->priv->pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);

        g_free (dump_name);
        g_free (state_transition_name);
      }
      break;
    case GST_MESSAGE_REQUEST_STATE:
      ges_validate_handle_request_state_change (message, G_APPLICATION (self));
      break;
    default:
      break;
  }
}

#ifdef G_OS_UNIX
static gboolean
intr_handler (GESLauncher * self)
{
  g_print ("interrupt received.\n");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->priv->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch.interupted");

  g_application_release (G_APPLICATION (self));

  /* remove signal handler */
  return TRUE;
}
#endif /* G_OS_UNIX */

static gboolean
_save_timeline (GESLauncher * self)
{
  ParsedOptions *opts = &self->priv->parsed_options;

  if (opts->save_path && !opts->load_path) {
    gchar *uri;
    if (!(uri = ensure_uri (opts->save_path))) {
      g_error ("couldn't create uri for '%s", opts->save_path);
      return FALSE;
    }

    return ges_timeline_save_to_uri (self->priv->timeline, uri, NULL, TRUE,
        NULL);
  }

  return TRUE;
}

static gboolean
_run_pipeline (GESLauncher * self)
{
  GstBus *bus;
  ParsedOptions *opts = &self->priv->parsed_options;

  if (!opts->load_path) {
    if (ges_validate_activate (GST_PIPELINE (self->priv->pipeline),
            opts->scenario, &opts->needs_set_state) == FALSE) {
      g_error ("Could not activate scenario %s", opts->scenario);
      return FALSE;
    }

    if (!_timeline_set_user_options (self, self->priv->timeline, NULL)) {
      g_error ("Could not properly set tracks");
      return FALSE;
    }
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (self->priv->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), self);

  if (!opts->load_path) {
    if (opts->needs_set_state
        && gst_element_set_state (GST_ELEMENT (self->priv->pipeline),
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      g_error ("Failed to start the pipeline\n");
      return FALSE;
    }
  }
  g_application_hold (G_APPLICATION (self));

  return TRUE;
}

static gboolean
_set_rendering_details (GESLauncher * self)
{
  ParsedOptions *opts = &self->priv->parsed_options;

  /* Setup profile/encoding if needed */
  if (opts->smartrender || opts->outputuri) {
    GstEncodingProfile *prof = NULL;

    if (!opts->format) {
      GESProject *proj =
          GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (self->
                  priv->timeline)));
      const GList *profiles = ges_project_list_encoding_profiles (proj);

      if (profiles) {
        prof = profiles->data;
        if (opts->encoding_profile)
          for (; profiles; profiles = profiles->next)
            if (g_strcmp0 (opts->encoding_profile,
                    gst_encoding_profile_get_name (profiles->data)) == 0)
              prof = profiles->data;
      }
    }

    if (!prof) {
      if (opts->format == NULL)
        opts->format =
            g_strdup ("application/ogg:video/x-theora:audio/x-vorbis");

      prof = parse_encoding_profile (opts->format);
    }

    if (opts->outputuri)
      opts->outputuri = ensure_uri (opts->outputuri);

    if (!prof
        || !ges_pipeline_set_render_settings (self->priv->pipeline,
            opts->outputuri, prof)
        || !ges_pipeline_set_mode (self->priv->pipeline,
            opts->smartrender ? GES_PIPELINE_MODE_SMART_RENDER :
            GES_PIPELINE_MODE_RENDER)) {
      return FALSE;
    }

    gst_encoding_profile_unref (prof);
  } else {
    ges_pipeline_set_mode (self->priv->pipeline, GES_PIPELINE_MODE_PREVIEW);
  }
  return TRUE;
}

static gboolean
_create_pipeline (GESLauncher * self, const gchar * serialized_timeline)
{
  gchar *uri = NULL;
  gboolean res = TRUE;
  ParsedOptions *opts = &self->priv->parsed_options;

  /* Timeline creation */
  if (opts->load_path) {
    g_printf ("Loading project from : %s\n", opts->load_path);

    if (!(uri = ensure_uri (opts->load_path))) {
      g_error ("couldn't create uri for '%s'", opts->load_path);
      goto failure;
    }
  }

  self->priv->pipeline = ges_pipeline_new ();

  if (!_create_timeline (self, serialized_timeline, uri, opts->scenario)) {
    GST_ERROR ("Could not create the timeline");
    goto failure;
  }

  if (!opts->load_path)
    ges_timeline_commit (self->priv->timeline);

  /* save project if path is given. we do this now in case GES crashes or
   * hangs during playback. */
  if (!_save_timeline (self))
    goto failure;

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */

  if (opts->mute) {
    GstElement *sink = gst_element_factory_make ("fakesink", NULL);

    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_audio_sink (self->priv->pipeline, sink);

    sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_video_sink (self->priv->pipeline, sink);
  }

  /* Add the timeline to that pipeline */
  if (!ges_pipeline_set_timeline (self->priv->pipeline, self->priv->timeline))
    goto failure;

done:
  if (uri)
    g_free (uri);

  return res;

failure:
  {
    if (self->priv->timeline)
      gst_object_unref (self->priv->timeline);
    if (self->priv->pipeline)
      gst_object_unref (self->priv->pipeline);
    self->priv->pipeline = NULL;
    self->priv->timeline = NULL;

    res = FALSE;
    goto done;
  }
}

static gboolean
_add_media_path (const gchar * option_name, const gchar * value,
    gpointer udata, GError ** error)
{
  g_return_val_if_fail (gst_uri_is_valid (value), FALSE);

  if (g_strcmp0 (option_name, "--sample-path-recurse") == 0 ||
      g_strcmp0 (option_name, "-R") == 0) {
    ges_add_missing_uri_relocation_uri (value, TRUE);
  } else {
    GST_INFO ("Adding folder: %s", value);
    ges_add_missing_uri_relocation_uri (value, FALSE);
  }

  return TRUE;
}

static void
_print_transition_list (void)
{
  print_enum (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
}

static gboolean
_local_command_line (GApplication * application, gchar ** arguments[],
    gint * exit_status)
{
  GESLauncher *self = GES_LAUNCHER (application);
  GError *error = NULL;
  gchar **argv;
  gint argc;
  gboolean inspect_action_type = FALSE;
  GOptionContext *ctx;
  ParsedOptions *opts = &self->priv->parsed_options;
  GOptionGroup *main_group;
  GOptionEntry options[] = {
    {"outputuri", 'o', 0, G_OPTION_ARG_STRING, &opts->outputuri,
        "URI to encode to", "<protocol>://<location>"},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &opts->format,
          "Specify an encoding profile on the command line",
        "<profile>"},
    {"encoding-profile", 'e', 0, G_OPTION_ARG_STRING, &opts->encoding_profile,
        "Use a specific encoding profile from XML", "<profile-name>"},
    {"list-transitions", 0, 0, G_OPTION_ARG_NONE, &opts->list_transitions,
        "List valid transition types and exit", NULL},
    {"save", 's', 0, G_OPTION_ARG_STRING, &opts->save_path,
        "Save project to file before rendering", "<path>"},
    {"load", 'l', 0, G_OPTION_ARG_STRING, &opts->load_path,
        "Load project from file before rendering", "<path>"},
    {"track-types", 't', 0, G_OPTION_ARG_CALLBACK, &_parse_track_type,
        "Defines the track types to be created", "<track-types>"},
    {"mute", 'm', 0, G_OPTION_ARG_NONE, &opts->mute,
        "Mute playback output by using fakesinks", ""},
    {"disable-mixing", 0, 0, G_OPTION_ARG_NONE, &opts->disable_mixing,
        "Do not use mixing element in the tracks", ""},
    {"videosink", 'v', 0, G_OPTION_ARG_STRING, &opts->videosink,
        "The video sink used for playing back", "<videosink>"},
    {"audiosink", 'a', 0, G_OPTION_ARG_STRING, &opts->audiosink,
        "The audio sink used for playing back", "<audiosink>"},
    {"sample-paths", 'p', 0, G_OPTION_ARG_CALLBACK, &_add_media_path,
        "List of pathes to look assets in if they were moved", ""},
    {"sample-path-recurse", 'R', 0, G_OPTION_ARG_CALLBACK,
          &_add_media_path,
        "Same as above, but recursing into the folder", ""},
#ifdef HAVE_GST_VALIDATE
    {"inspect-action-type", 0, 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the avalaible action types with which to write scenarios"
          " if no parameter passed, it will list all avalaible action types"
          " otherwize will print the full description of the wanted types",
        NULL},
    {"set-scenario", 0, 0, G_OPTION_ARG_STRING, &opts->scenario,
        "Specify a GstValidate scenario to run, 'none' means load gst-validate"
          " but run no scenario on it", "<scenario_name>"},
#endif
    {NULL}
  };

  ctx = g_option_context_new ("- plays or renders a timeline.");
  g_option_context_set_summary (ctx, HELP_SUMMARY);

  main_group =
      g_option_group_new ("launcher", "launcher options",
      "Main launcher options", self, NULL);
  g_option_group_add_entries (main_group, options);
  g_option_context_set_main_group (ctx, main_group);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_group (ctx, ges_init_get_option_group ());
  g_option_context_set_ignore_unknown_options (ctx, TRUE);

  argv = *arguments;
  argc = g_strv_length (argv);
  *exit_status = 0;

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    g_option_context_free (ctx);
    *exit_status = 1;
  }

  if (inspect_action_type) {
    ges_validate_print_action_types ((const gchar **) argv + 1, argc - 1);
    return TRUE;
  }

  if (!opts->load_path && !opts->scenario && !opts->list_transitions
      && (argc <= 1)) {
    g_printf ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    *exit_status = 1;
    return TRUE;
  }

  g_option_context_free (ctx);

  opts->sanitized_timeline = sanitize_timeline_description (argc, argv);

  if (!g_application_register (application, NULL, &error)) {
    *exit_status = 1;
    g_error_free (error);
    return FALSE;
  }

  return TRUE;
}

static void
_startup (GApplication * application)
{
  GESLauncher *self = GES_LAUNCHER (application);
  ParsedOptions *opts = &self->priv->parsed_options;

#ifdef G_OS_UNIX
  self->priv->signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, self);
#endif

  /* Initialize the GStreamer Editing Services */
  if (!ges_init ()) {
    g_printerr ("Error initializing GES\n");
    goto done;
  }

  if (opts->list_transitions) {
    _print_transition_list ();
    goto done;
  }

  if (!_create_pipeline (self, opts->sanitized_timeline))
    goto failure;

  if (!_set_playback_details (self))
    goto failure;

  if (!_set_rendering_details (self))
    goto failure;

  if (!_run_pipeline (self))
    goto failure;

done:
  return G_APPLICATION_CLASS (ges_launcher_parent_class)->startup (application);

failure:
  self->priv->seenerrors = TRUE;
}

static void
_shutdown (GApplication * application)
{
  gint validate_res = 0;
  GESLauncher *self = GES_LAUNCHER (application);

  _save_timeline (self);

  if (self->priv->pipeline) {
    gst_element_set_state (GST_ELEMENT (self->priv->pipeline), GST_STATE_NULL);
    validate_res = ges_validate_clean (GST_PIPELINE (self->priv->pipeline));
  }

  if (self->priv->seenerrors == FALSE)
    self->priv->seenerrors = validate_res;

#ifdef G_OS_UNIX
  g_source_remove (self->priv->signal_watch_id);
#endif

  G_APPLICATION_CLASS (ges_launcher_parent_class)->shutdown (application);
}

static void
ges_launcher_class_init (GESLauncherClass * klass)
{
  G_APPLICATION_CLASS (klass)->local_command_line = _local_command_line;
  G_APPLICATION_CLASS (klass)->startup = _startup;
  G_APPLICATION_CLASS (klass)->shutdown = _shutdown;
  g_type_class_add_private (klass, sizeof (GESLauncherPrivate));
}

static void
ges_launcher_init (GESLauncher * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_LAUNCHER, GESLauncherPrivate);
  self->priv->parsed_options.track_types =
      GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO;
}

gint
ges_launcher_get_exit_status (GESLauncher * self)
{
  return self->priv->seenerrors;
}

GESLauncher *
ges_launcher_new (void)
{
  return GES_LAUNCHER (g_object_new (ges_launcher_get_type (), "application-id",
          "org.gstreamer.geslaunch", "flags",
          G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_COMMAND_LINE, NULL));
}
