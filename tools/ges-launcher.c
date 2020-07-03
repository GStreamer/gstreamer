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

struct _GESLauncherPrivate
{
  GESTimeline *timeline;
  GESPipeline *pipeline;
  gboolean seenerrors;
#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif
  GESLauncherParsedOptions parsed_options;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESLauncher, ges_launcher, G_TYPE_APPLICATION);

static const gchar *HELP_SUMMARY =
    "  `ges-launch-1.0` creates a multimedia timeline and plays it back,\n"
    "  or renders it to the specified format.\n\n"
    "  It can load a timeline from an existing project, or create one\n"
    "  using the 'Timeline description format', specified in the section\n"
    "  of the same name.\n\n"
    "  Updating an existing project can be done through `--set-scenario`\n"
    "  if ges-launch-1.0 has been compiled with gst-validate, see\n"
    "  `ges-launch-1.0 --inspect-action-type` for the available commands.\n\n"
    "  By default, ges-launch-1.0 is in \"playback-mode\".";

static gboolean
_parse_track_type (const gchar * option_name, const gchar * value,
    GESLauncherParsedOptions * opts, GError ** error)
{
  if (!get_flags_from_string (GES_TYPE_TRACK_TYPE, value, &opts->track_types))
    return FALSE;

  return TRUE;
}

static gboolean
_set_track_restriction_caps (GESTrack * track, const gchar * caps_str)
{
  GstCaps *caps;

  if (!caps_str)
    return TRUE;

  caps = gst_caps_from_string (caps_str);

  if (!caps) {
    g_error ("Could not create caps for %s from: %s",
        G_OBJECT_TYPE_NAME (track), caps_str);

    return FALSE;
  }

  ges_track_set_restriction_caps (track, caps);

  gst_caps_unref (caps);
  return TRUE;
}

static void
_set_restriction_caps (GESTimeline * timeline, GESLauncherParsedOptions * opts)
{
  GList *tmp, *tracks = ges_timeline_get_tracks (timeline);

  for (tmp = tracks; tmp; tmp = tmp->next) {
    if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_VIDEO)
      _set_track_restriction_caps (tmp->data, opts->video_track_caps);
    else if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_AUDIO)
      _set_track_restriction_caps (tmp->data, opts->audio_track_caps);
  }

  g_list_free_full (tracks, gst_object_unref);

}

static void
_check_has_audio_video (GESLauncher * self, gint * n_audio, gint * n_video)
{
  GList *tmp, *tracks = ges_timeline_get_tracks (self->priv->timeline);

  *n_video = *n_audio = 0;
  for (tmp = tracks; tmp; tmp = tmp->next) {
    if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_VIDEO)
      *n_video = *n_video + 1;
    else if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_AUDIO)
      *n_audio = *n_audio + 1;
  }
}

#define N_INSTANCES "__n_instances"
static gint
sort_encoding_profiles (gconstpointer a, gconstpointer b)
{
  const gint acount =
      GPOINTER_TO_INT (g_object_get_data ((GObject *) a, N_INSTANCES));
  const gint bcount =
      GPOINTER_TO_INT (g_object_get_data ((GObject *) b, N_INSTANCES));

  if (acount < bcount)
    return -1;

  if (acount == bcount)
    return 0;

  return 1;
}

static GstEncodingProfile *
get_smart_profile (GESLauncher * self)
{
  gint n_audio, n_video;
  GList *tmp, *assets, *possible_profiles = NULL;
  GstEncodingProfile *res = NULL;
  GESProject *proj =
      GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (self->
              priv->timeline)));

  _check_has_audio_video (self, &n_audio, &n_video);
  assets = ges_project_list_assets (proj, GES_TYPE_URI_CLIP);
  for (tmp = assets; tmp; tmp = tmp->next) {
    GESAsset *asset = tmp->data;
    GList *audio_streams, *video_streams;
    GstDiscovererInfo *info;

    if (!GES_IS_URI_CLIP_ASSET (asset))
      continue;

    info = ges_uri_clip_asset_get_info (GES_URI_CLIP_ASSET (asset));
    audio_streams = gst_discoverer_info_get_audio_streams (info);
    video_streams = gst_discoverer_info_get_video_streams (info);
    if (g_list_length (audio_streams) >= n_audio
        && g_list_length (video_streams) >= n_video) {
      GstEncodingProfile *prof = gst_encoding_profile_from_discoverer (info);
      GList *prevprof;

      prevprof =
          g_list_find_custom (possible_profiles, prof,
          (GCompareFunc) gst_encoding_profile_is_equal);
      if (prevprof) {
        g_object_unref (prof);
        prof = prevprof->data;
      } else {
        possible_profiles = g_list_prepend (possible_profiles, prof);
      }

      g_object_set_data ((GObject *) prof, N_INSTANCES,
          GINT_TO_POINTER (GPOINTER_TO_INT (g_object_get_data ((GObject *) prof,
                      N_INSTANCES)) + 1));
    }
    gst_discoverer_stream_info_list_free (audio_streams);
    gst_discoverer_stream_info_list_free (video_streams);
  }
  g_list_free_full (assets, gst_object_unref);

  if (possible_profiles) {
    possible_profiles = g_list_sort (possible_profiles, sort_encoding_profiles);
    res = gst_object_ref (possible_profiles->data);
    g_list_free_full (possible_profiles, gst_object_unref);
  }

  return res;
}

static void
disable_bframe_for_smart_rendering_cb (GstBin * bin, GstBin * sub_bin,
    GstElement * child)
{
  GstElementFactory *factory = gst_element_get_factory (child);

  if (factory && !g_strcmp0 (GST_OBJECT_NAME (factory), "x264enc")) {
    g_object_set (child, "b-adapt", FALSE, "b-pyramid", FALSE, "bframes", 0,
        NULL);
  }
}

static gboolean
_set_rendering_details (GESLauncher * self)
{
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;
  gboolean smart_profile = FALSE;
  GESPipelineFlags cmode = ges_pipeline_get_mode (self->priv->pipeline);

  if (cmode & GES_PIPELINE_MODE_RENDER
      || cmode & GES_PIPELINE_MODE_SMART_RENDER) {
    GST_INFO_OBJECT (self, "Rendering settings already set");
    return TRUE;
  }

  /* Setup profile/encoding if needed */
  if (opts->outputuri) {
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
      if (opts->format == NULL) {
        if (opts->smartrender)
          prof = get_smart_profile (self);
        if (prof)
          smart_profile = TRUE;
        else {
          opts->format = get_file_extension (opts->outputuri);
          prof = parse_encoding_profile (opts->format);
        }
      } else {
        prof = parse_encoding_profile (opts->format);
        if (!prof)
          g_error ("Invalid format specified: %s", opts->format);
      }

      if (!prof) {
        warn ("No format specified and couldn't find one from output file extension, " "falling back to theora+vorbis in ogg.");
        g_free (opts->format);

        opts->format =
            g_strdup ("application/ogg:video/x-theora:audio/x-vorbis");
        prof = parse_encoding_profile (opts->format);
      }

      if (!prof) {
        printerr ("Could not find any encoding format for %s\n", opts->format);
        return FALSE;
      }

      g_print ("Output: %s\n", opts->outputuri);
      g_print ("Encoding to:%s\n", smart_profile ?
          " (Selected from input files format for efficient smart rendering)" :
          "");
      describe_encoding_profile (prof);
    }

    opts->outputuri = ensure_uri (opts->outputuri);
    if (opts->smartrender) {
      g_signal_connect (self->priv->pipeline, "deep-element-added",
          G_CALLBACK (disable_bframe_for_smart_rendering_cb), NULL);
    }
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

static void
_track_set_mixing (GESTrack * track, GESLauncherParsedOptions * opts)
{
  static gboolean printed_mixing_disabled = FALSE;

  if (opts->disable_mixing || opts->smartrender)
    ges_track_set_mixing (track, FALSE);
  if (!opts->disable_mixing && opts->smartrender && !printed_mixing_disabled) {
    g_print ("**Mixing is disabled for smart rendering to work**\n");
    printed_mixing_disabled = TRUE;
  }
}

static gboolean
_timeline_set_user_options (GESLauncher * self, GESTimeline * timeline,
    const gchar * load_path)
{
  GList *tmp;
  GESTrack *tracka, *trackv;
  gboolean has_audio = FALSE, has_video = FALSE;
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

retry:
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {

    if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_VIDEO)
      has_video = TRUE;
    else if (GES_TRACK (tmp->data)->type == GES_TRACK_TYPE_AUDIO)
      has_audio = TRUE;

    _track_set_mixing (tmp->data, opts);
    if (!(GES_TRACK (tmp->data)->type & opts->track_types)) {
      ges_timeline_remove_track (timeline, tmp->data);
      goto retry;
    }
  }

  if ((opts->scenario || opts->testfile) && !load_path) {
    if (!has_video && opts->track_types & GES_TRACK_TYPE_VIDEO) {
      trackv = GES_TRACK (ges_video_track_new ());

      if (!_set_track_restriction_caps (trackv, opts->video_track_caps))
        return FALSE;

      _track_set_mixing (trackv, opts);

      if (!(ges_timeline_add_track (timeline, trackv)))
        return FALSE;
    }

    if (!has_audio && opts->track_types & GES_TRACK_TYPE_AUDIO) {
      tracka = GES_TRACK (ges_audio_track_new ());

      if (!_set_track_restriction_caps (tracka, opts->audio_track_caps))
        return FALSE;

      _track_set_mixing (tracka, opts);

      if (!(ges_timeline_add_track (timeline, tracka)))
        return FALSE;
    }
  } else {
    _set_restriction_caps (timeline, opts);
  }

  return TRUE;
}

static void
_project_loading_error_cb (GESProject * project, GESTimeline * timeline,
    GError * error, GESLauncher * self)
{
  printerr ("Error loading timeline: '%s'\n", error->message);
  self->priv->seenerrors = TRUE;

  g_application_quit (G_APPLICATION (self));
}

static void
_project_loaded_cb (GESProject * project, GESTimeline * timeline,
    GESLauncher * self)
{
  gchar *project_uri = NULL;
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;
  GST_INFO ("Project loaded, playing it");

  if (opts->save_path) {
    gchar *uri;
    GError *error = NULL;

    if (g_strcmp0 (opts->save_path, "+r") == 0) {
      uri = ges_project_get_uri (project);
    } else if (!(uri = ensure_uri (opts->save_path))) {
      g_error ("couldn't create uri for '%s", opts->save_path);

      self->priv->seenerrors = TRUE;
      g_application_quit (G_APPLICATION (self));
    }

    g_print ("\nSaving project to %s\n", uri);
    ges_project_save (project, timeline, uri, NULL, TRUE, &error);
    g_free (uri);

    g_assert_no_error (error);
    if (error) {
      self->priv->seenerrors = TRUE;
      g_error_free (error);
      g_application_quit (G_APPLICATION (self));
    }
  }

  project_uri = ges_project_get_uri (project);

  if (self->priv->parsed_options.load_path && project_uri
      && ges_validate_activate (GST_PIPELINE (self->priv->pipeline),
          self, opts) == FALSE) {
    if (opts->scenario)
      g_error ("Could not activate scenario %s", opts->scenario);
    else
      g_error ("Could not activate testfile %s", opts->testfile);
    self->priv->seenerrors = TRUE;
    g_application_quit (G_APPLICATION (self));
  }
  _timeline_set_user_options (self, timeline, project_uri);
  if (project_uri) {
    if (!_set_rendering_details (self))
      g_error ("Failed to setup rendering details\n");
  }

  g_free (project_uri);

  if (!self->priv->seenerrors && opts->needs_set_state &&
      gst_element_set_state (GST_ELEMENT (self->priv->pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to start the pipeline\n");
  }
}

static void
_error_loading_asset_cb (GESProject * project, GError * error,
    const gchar * failed_id, GType extractable_type, GESLauncher * self)
{
  printerr ("Error loading asset %s: %s\n", failed_id, error->message);
  self->priv->seenerrors = TRUE;

  g_application_quit (G_APPLICATION (self));
}

static gboolean
_create_timeline (GESLauncher * self, const gchar * serialized_timeline,
    const gchar * proj_uri, gboolean validate)
{
  GESProject *project;

  GError *error = NULL;

  if (proj_uri != NULL) {
    project = ges_project_new (proj_uri);
  } else if (!validate) {
    GST_INFO ("serialized timeline is %s", serialized_timeline);
    project = ges_project_new (serialized_timeline);
  } else {
    project = ges_project_new (NULL);
  }

  g_signal_connect (project, "error-loading-asset",
      G_CALLBACK (_error_loading_asset_cb), self);
  g_signal_connect (project, "loaded", G_CALLBACK (_project_loaded_cb), self);
  g_signal_connect (project, "error-loading",
      G_CALLBACK (_project_loading_error_cb), self);

  self->priv->timeline =
      GES_TIMELINE (ges_asset_extract (GES_ASSET (project), &error));
  gst_object_unref (project);

  if (error) {
    printerr ("\nERROR: Could not create timeline because: %s\n\n",
        error->message);
    g_error_free (error);
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
    GstElement *sink = gst_parse_bin_from_description_full (sink_desc, TRUE,
        NULL, GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS, &err);
    if (sink == NULL) {
      GST_ERROR ("could not create the requested videosink %s (err: %s), "
          "exiting", err ? err->message : "", sink_desc);
      if (err)
        g_error_free (err);
      return FALSE;
    }
    set_func (self->priv->pipeline, sink);
  }
  return TRUE;
}

static gboolean
_set_playback_details (GESLauncher * self)
{
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

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
      printerr ("ERROR from element %s: %s\n", GST_OBJECT_NAME (message->src),
          err->message);
      printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_clear_error (&err);
      g_free (dbg_info);
      self->priv->seenerrors = TRUE;
      g_application_quit (G_APPLICATION (self));
      break;
    }
    case GST_MESSAGE_EOS:
      if (!self->priv->parsed_options.ignore_eos) {
        ok ("\nDone\n");
        g_application_quit (G_APPLICATION (self));
      }
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
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch.interrupted");

  g_application_quit (G_APPLICATION (self));

  /* remove signal handler */
  return TRUE;
}
#endif /* G_OS_UNIX */

static gboolean
_save_timeline (GESLauncher * self)
{
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;


  if (opts->embed_nesteds) {
    GList *tmp, *assets;
    GESProject *proj =
        GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (self->
                priv->timeline)));

    assets = ges_project_list_assets (proj, GES_TYPE_URI_CLIP);
    for (tmp = assets; tmp; tmp = tmp->next) {
      gboolean is_nested;

      g_object_get (tmp->data, "is-nested-timeline", &is_nested, NULL);
      if (is_nested) {
        GESAsset *subproj =
            ges_asset_request (GES_TYPE_TIMELINE, ges_asset_get_id (tmp->data),
            NULL);

        ges_project_add_asset (proj, subproj);
      }
    }
    g_list_free_full (assets, gst_object_unref);
  }

  if (opts->save_only_path) {
    gchar *uri;

    if (!(uri = ensure_uri (opts->save_only_path))) {
      g_error ("couldn't create uri for '%s", opts->save_only_path);
      return FALSE;
    }

    return ges_timeline_save_to_uri (self->priv->timeline, uri, NULL, TRUE,
        NULL);
  }

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
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

  if (!opts->load_path) {
    if (ges_validate_activate (GST_PIPELINE (self->priv->pipeline),
            self, opts) == FALSE) {
      g_error ("Could not activate scenario %s", opts->scenario);
      return FALSE;
    }

    if (!_timeline_set_user_options (self, self->priv->timeline, NULL)) {
      printerr ("Could not properly set tracks\n");
      return FALSE;
    }

    if (!_set_rendering_details (self)) {
      g_error ("Failed to setup rendering details\n");
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
_create_pipeline (GESLauncher * self, const gchar * serialized_timeline)
{
  gchar *uri = NULL;
  gboolean res = TRUE;
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

  /* Timeline creation */
  if (opts->load_path) {
    g_printf ("Loading project from : %s\n", opts->load_path);

    if (!(uri = ensure_uri (opts->load_path))) {
      g_error ("couldn't create uri for '%s'", opts->load_path);
      goto failure;
    }
  }

  self->priv->pipeline = ges_pipeline_new ();

  if (!_create_timeline (self, serialized_timeline, uri, opts->scenario
          || opts->testfile)) {
    GST_ERROR ("Could not create the timeline");
    goto failure;
  }

  if (!opts->load_path)
    ges_timeline_commit (self->priv->timeline);

  /* save project if path is given. we do this now in case GES crashes or
   * hangs during playback. */
  if (!_save_timeline (self))
    goto failure;

  if (opts->save_only_path)
    goto done;

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

static void
_print_transition_list (void)
{
  print_enum (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
}

static GOptionGroup *
ges_launcher_get_project_option_group (GESLauncherParsedOptions * opts)
{
  GOptionGroup *group;

  GOptionEntry options[] = {
    {"load", 'l', 0, G_OPTION_ARG_STRING, &opts->load_path,
          "Load project from file. The project can be saved "
          "again with the --save option.",
        "<path>"},
    {"save", 's', 0, G_OPTION_ARG_STRING, &opts->save_path,
          "Save project to file before rendering. "
          "It can then be loaded with the --load option",
        "<path>"},
    {"save-only", 0, 0, G_OPTION_ARG_STRING, &opts->save_only_path,
          "Same as save project, except exit as soon as the timeline "
          "is saved instead of playing it back",
        "<path>"},
    {NULL}
  };
  group = g_option_group_new ("project", "Project Options",
      "Show project-related options", NULL, NULL);

  g_option_group_add_entries (group, options);

  return group;
}

static GOptionGroup *
ges_launcher_get_info_option_group (GESLauncherParsedOptions * opts)
{
  GOptionGroup *group;

  GOptionEntry options[] = {
#ifdef HAVE_GST_VALIDATE
    {"inspect-action-type", 0, 0, G_OPTION_ARG_NONE, &opts->inspect_action_type,
          "Inspect the available action types that can be defined in a scenario "
          "set with --set-scenario. "
          "Will list all action-types if action-type is empty.",
        "<[action-type]>"},
#endif
    {"list-transitions", 0, 0, G_OPTION_ARG_NONE, &opts->list_transitions,
          "List all valid transition types and exit. "
          "See ges-launch-1.0 help transition for more information.",
        NULL},
    {NULL}
  };

  group = g_option_group_new ("informative", "Informative Options",
      "Show informative options", NULL, NULL);

  g_option_group_add_entries (group, options);

  return group;
}

static GOptionGroup *
ges_launcher_get_rendering_option_group (GESLauncherParsedOptions * opts)
{
  GOptionGroup *group;

  GOptionEntry options[] = {
    {"outputuri", 'o', 0, G_OPTION_ARG_STRING, &opts->outputuri,
          "If set, ges-launch-1.0 will render the timeline instead of playing "
          "it back. If no format `--format` is specified, the outputuri extension"
          " will be used to determine an encoding format, or default to theora+vorbis"
          " in ogg if that doesn't work out.",
        "<URI>"},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &opts->format,
          "Set an encoding profile on the command line. "
          "See ges-launch-1.0 help profile for more information. "
          "This will have no effect if no outputuri has been specified.",
        "<profile>"},
    {"encoding-profile", 'e', 0, G_OPTION_ARG_STRING, &opts->encoding_profile,
          "Set an encoding profile from a preset file. "
          "See ges-launch-1.0 help profile for more information. "
          "This will have no effect if no outputuri has been specified.",
        "<profile-name>"},
    {"smart-rendering", 0, 0, G_OPTION_ARG_NONE, &opts->smartrender,
          "Avoid reencoding when rendering. This option implies --disable-mixing.",
        NULL},
    {NULL}
  };

  group = g_option_group_new ("rendering", "Rendering Options",
      "Show rendering options", NULL, NULL);

  g_option_group_add_entries (group, options);

  return group;
}

static GOptionGroup *
ges_launcher_get_playback_option_group (GESLauncherParsedOptions * opts)
{
  GOptionGroup *group;

  GOptionEntry options[] = {
    {"videosink", 'v', 0, G_OPTION_ARG_STRING, &opts->videosink,
        "Set the videosink used for playback.", "<videosink>"},
    {"audiosink", 'a', 0, G_OPTION_ARG_STRING, &opts->audiosink,
        "Set the audiosink used for playback.", "<audiosink>"},
    {"mute", 'm', 0, G_OPTION_ARG_NONE, &opts->mute,
        "Mute playback output. This has no effect when rendering.", NULL},
    {NULL}
  };

  group = g_option_group_new ("playback", "Playback Options",
      "Show playback options", NULL, NULL);

  g_option_group_add_entries (group, options);

  return group;
}

gboolean
ges_launcher_parse_options (GESLauncher * self,
    gchar ** arguments[], gint * argc, GOptionContext * ctx, GError ** error)
{
  gboolean res;
  GOptionGroup *main_group;
  gint nargs = 0, tmpargc;
  gchar **commands = NULL, *help, *tmp;
  GError *err = NULL;
  gboolean owns_ctx = ctx == NULL;
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;
  gchar *prev_videosink = opts->videosink, *prev_audiosink = opts->audiosink;
  GOptionEntry options[] = {
    {"disable-mixing", 0, 0, G_OPTION_ARG_NONE, &opts->disable_mixing,
        "Do not use mixing elements to mix layers together.", NULL}
    ,
    {"track-types", 't', 0, G_OPTION_ARG_CALLBACK, &_parse_track_type,
          "Specify the track types to be created. "
          "When loading a project, only relevant tracks will be added to the "
          "timeline.",
        "<track-types>"}
    ,
    {
          "video-caps",
          0,
          0,
          G_OPTION_ARG_STRING,
          &opts->video_track_caps,
          "Specify the track restriction caps of the video track.",
        }
    ,
    {
          "audio-caps",
          0,
          0,
          G_OPTION_ARG_STRING,
          &opts->audio_track_caps,
          "Specify the track restriction caps of the audio track.",
        }
    ,
#ifdef HAVE_GST_VALIDATE
    {"set-test-file", 0, 0, G_OPTION_ARG_STRING, &opts->testfile,
          "ges-launch-1.0 exposes gst-validate functionalities, such as test files and scenarios."
          " Scenarios describe actions to execute, such as seeks or setting of "
          "properties. "
          "GES implements editing-specific actions such as adding or removing "
          "clips. "
          "See gst-validate-1.0 --help for more info about validate and "
          "scenarios, " "and --inspect-action-type.",
        "</test/file/path>"}
    ,
    {"set-scenario", 0, 0, G_OPTION_ARG_STRING, &opts->scenario,
          "ges-launch-1.0 exposes gst-validate functionalities, such as scenarios."
          " Scenarios describe actions to execute, such as seeks or setting of "
          "properties. "
          "GES implements editing-specific actions such as adding or removing "
          "clips. "
          "See gst-validate-1.0 --help for more info about validate and "
          "scenarios, " "and --inspect-action-type.",
        "<scenario_name>"}
    ,
    {"disable-validate", 'n', 0, G_OPTION_ARG_NONE, &opts->disable_validate,
          "Do not run inside GstValidate.",
        "<scenario_name>"}
    ,
#endif
    {
          "embed-nesteds",
          0,
          0,
          G_OPTION_ARG_NONE,
          &opts->embed_nesteds,
          "Embed nested timelines when saving.",
        }
    ,
    {NULL}
  };

  if (owns_ctx) {
    opts->videosink = opts->audiosink = NULL;
    ctx = g_option_context_new ("- plays or renders a timeline.");
  }
  tmpargc = argc ? *argc : g_strv_length (*arguments);

  if (tmpargc > 2) {
    nargs = tmpargc - 2;
    commands = &(*arguments)[2];
  }

  tmp = ges_command_line_formatter_get_help (nargs, commands);
  help =
      g_strdup_printf ("%s\n\nTimeline description format:\n\n%s", HELP_SUMMARY,
      tmp);
  g_free (tmp);
  g_option_context_set_summary (ctx, help);
  g_free (help);

  main_group =
      g_option_group_new ("launcher", "launcher options",
      "Main launcher options", opts, NULL);
  g_option_group_add_entries (main_group, options);
  g_option_context_set_main_group (ctx, main_group);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_group (ctx, ges_init_get_option_group ());
  g_option_context_add_group (ctx,
      ges_launcher_get_project_option_group (opts));
  g_option_context_add_group (ctx,
      ges_launcher_get_rendering_option_group (opts));
  g_option_context_add_group (ctx,
      ges_launcher_get_playback_option_group (opts));
  g_option_context_add_group (ctx, ges_launcher_get_info_option_group (opts));
  g_option_context_set_ignore_unknown_options (ctx, TRUE);

  res = g_option_context_parse_strv (ctx, arguments, &err);
  if (argc)
    *argc = tmpargc;

  if (err)
    g_propagate_error (error, err);

  if (owns_ctx) {
    g_option_context_free (ctx);
    /* sinks passed in the command line are preferred. */
    if (prev_videosink) {
      g_free (opts->videosink);
      opts->videosink = prev_videosink;
    }

    if (prev_audiosink) {
      g_free (opts->audiosink);
      opts->audiosink = prev_audiosink;
    }
    _set_playback_details (self);
  }

  return res;
}

static gboolean
_local_command_line (GApplication * application, gchar ** arguments[],
    gint * exit_status)
{
  gboolean res = TRUE;
  gint argc;
  GError *error = NULL;
  GESLauncher *self = GES_LAUNCHER (application);
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;
  GOptionContext *ctx = g_option_context_new ("- plays or renders a timeline.");

  *exit_status = 0;
  argc = g_strv_length (*arguments);

  gst_init (&argc, arguments);
  if (!ges_launcher_parse_options (self, arguments, &argc, ctx, &error)) {
    gst_init (NULL, NULL);
    printerr ("Error initializing: %s\n", error->message);
    g_option_context_free (ctx);
    g_error_free (error);
    *exit_status = 1;
    goto done;
  }

  if (opts->inspect_action_type) {
    ges_validate_print_action_types ((const gchar **) &((*arguments)[1]),
        argc - 1);
    goto done;
  }

  if (!opts->load_path && !opts->scenario && !opts->testfile
      && !opts->list_transitions && (argc <= 1)) {
    g_printf ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    *exit_status = 1;
    goto done;
  }

  g_option_context_free (ctx);

  opts->sanitized_timeline = sanitize_timeline_description (*arguments);

  if (!g_application_register (application, NULL, &error)) {
    *exit_status = 1;
    g_clear_error (&error);
    res = FALSE;
  }

done:
  return res;
}

static void
_startup (GApplication * application)
{
  GESLauncher *self = GES_LAUNCHER (application);
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

#ifdef G_OS_UNIX
  self->priv->signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, self);
#endif

  /* Initialize the GStreamer Editing Services */
  if (!ges_init ()) {
    printerr ("Error initializing GES\n");
    goto done;
  }

  if (opts->list_transitions) {
    _print_transition_list ();
    goto done;
  }

  if (!_create_pipeline (self, opts->sanitized_timeline))
    goto failure;

  if (opts->save_only_path)
    goto done;

  if (!_set_playback_details (self))
    goto failure;

  if (!_set_rendering_details (self))
    goto failure;

  if (!_run_pipeline (self))
    goto failure;

done:
  G_APPLICATION_CLASS (ges_launcher_parent_class)->startup (application);

  return;

failure:
  self->priv->seenerrors = TRUE;

  goto done;
}

static void
_shutdown (GApplication * application)
{
  gint validate_res = 0;
  GESLauncher *self = GES_LAUNCHER (application);
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

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

  g_free (opts->sanitized_timeline);

  G_APPLICATION_CLASS (ges_launcher_parent_class)->shutdown (application);
}

static void
_finalize (GObject * object)
{
  GESLauncher *self = GES_LAUNCHER (object);
  GESLauncherParsedOptions *opts = &self->priv->parsed_options;

  g_free (opts->load_path);
  g_free (opts->save_path);
  g_free (opts->save_only_path);
  g_free (opts->outputuri);
  g_free (opts->format);
  g_free (opts->encoding_profile);
  g_free (opts->videosink);
  g_free (opts->audiosink);
  g_free (opts->video_track_caps);
  g_free (opts->audio_track_caps);
  g_free (opts->scenario);
  g_free (opts->testfile);

  G_OBJECT_CLASS (ges_launcher_parent_class)->finalize (object);
}

static void
ges_launcher_class_init (GESLauncherClass * klass)
{
  G_APPLICATION_CLASS (klass)->local_command_line = _local_command_line;
  G_APPLICATION_CLASS (klass)->startup = _startup;
  G_APPLICATION_CLASS (klass)->shutdown = _shutdown;

  G_OBJECT_CLASS (klass)->finalize = _finalize;
}

static void
ges_launcher_init (GESLauncher * self)
{
  self->priv = ges_launcher_get_instance_private (self);
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
