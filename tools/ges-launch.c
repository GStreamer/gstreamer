/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <ges/ges.h>
#include "../ges/ges-structured-interface.h"
#include <gst/pbutils/encoding-profile.h>

#include <locale.h>             /* for LC_ALL */
#include "ges-validate.h"
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "ges-structure-parser.h"
#include "parse_lex.h"

/* GLOBAL VARIABLE */
static guint repeat = 0;
static gboolean mute = FALSE;
static gboolean disable_mixing = FALSE;
static GESPipeline *pipeline = NULL;
static gboolean seenerrors = FALSE;
static gchar *save_path = NULL;
static GMainLoop *mainloop;
static GESTrackType track_types = GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO;
static GESTimeline *timeline;
static gboolean needs_set_state;
static const gchar *scenario = NULL;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch.interupted");

  g_main_loop_quit (mainloop);

  /* remove signal handler */
  return TRUE;
}
#endif /* G_OS_UNIX */

static gchar *
ensure_uri (gchar * location)
{
  if (gst_uri_is_valid (location))
    return g_strdup (location);
  else
    return gst_filename_to_uri (location, NULL);
}

static guint
get_flags_from_string (GType type, const gchar * str_flags)
{
  guint i;
  gint flags = 0;
  GFlagsClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_flags, class->values[i].value_nick)) {
      flags |= class->values[i].value;
    }
  }
  g_type_class_unref (class);

  return flags;
}



static gboolean
parse_track_type (const gchar * option_name, const gchar * value,
    gpointer udata, GError ** error)
{
  track_types = get_flags_from_string (GES_TYPE_TRACK_TYPE, value);

  if (track_types == 0)
    return FALSE;

  return TRUE;
}

static gboolean
thumbnail_cb (gpointer pipeline)
{
  static int i = 0;
  GESPipeline *p = (GESPipeline *) pipeline;
  gchar *filename;
  gboolean res;

  filename = g_strdup_printf ("thumbnail%d.jpg", i++);

  res = ges_pipeline_save_thumbnail (p, -1, -1,
      (gchar *) "image/jpeg", filename, NULL);

  g_free (filename);

  return res;
}

static void
error_loading_asset_cb (GESProject * project, GError * error,
    const gchar * failed_id, GType extractable_type)
{
  g_printerr ("Error loading asset %s: %s\n", failed_id, error->message);
  seenerrors = TRUE;

  g_main_loop_quit (mainloop);
}

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline)
{
  GST_INFO ("Project loaded, playing it");

  if (save_path) {
    gchar *uri;
    GError *error = NULL;

    if (g_strcmp0 (save_path, "+r") == 0) {
      uri = ges_project_get_uri (project);
    } else if (!(uri = ensure_uri (save_path))) {
      g_error ("couldn't create uri for '%s", save_path);

      seenerrors = TRUE;
      g_main_loop_quit (mainloop);
    }

    g_print ("\nSaving project to %s\n", uri);
    ges_project_save (project, timeline, uri, NULL, TRUE, &error);
    g_free (uri);

    g_assert_no_error (error);
    if (error) {
      seenerrors = TRUE;
      g_main_loop_quit (mainloop);
    }
  }

  if (ges_validate_activate (GST_PIPELINE (pipeline), scenario,
          &needs_set_state) == FALSE) {
    g_error ("Could not activate scenario %s", scenario);
    seenerrors = TRUE;
    g_main_loop_quit (mainloop);
  }

  if (needs_set_state && gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to start the pipeline\n");
  }
}

static gint                     /*  -1: not present, 0: failure, 1: OK */
_convert_to_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime default_value)
{
  gint res = 1;
  gdouble val;
  GValue d_val = { 0 };
  GstClockTime timestamp;
  const GValue *gvalue = gst_structure_get_value (structure, name);

  if (gvalue == NULL) {
    timestamp = default_value;

    res = -1;

    goto done;
  }

  if (G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME)
    return 1;

  g_value_init (&d_val, G_TYPE_DOUBLE);
  if (!g_value_transform (gvalue, &d_val)) {
    GST_ERROR ("Could not get timestamp for %s", name);

    return 0;
  }
  val = g_value_get_double ((const GValue *) &d_val);

  if (val == -1.0)
    timestamp = GST_CLOCK_TIME_NONE;
  else
    timestamp = val * GST_SECOND;

done:
  gst_structure_set (structure, name, G_TYPE_UINT64, timestamp, NULL);

  return res;
}

typedef struct
{
  const gchar *long_name;
  const gchar *short_name;
  GType type;
  const gchar *new_name;
} Properties;

static gboolean
_cleanup_fields (const Properties * filed_names, GstStructure * structure,
    GError ** error)
{
  guint i;

  for (i = 0; filed_names[i].long_name; i++) {
    gboolean exists = FALSE;

    /* Move shortly named fields to longname variante */
    if (gst_structure_has_field (structure, filed_names[i].short_name)) {
      exists = TRUE;

      if (gst_structure_has_field (structure, filed_names[i].long_name)) {
        *error = g_error_new (GES_ERROR, 0, "Using short and long name"
            " at the same time for property: %s, which one should I use?!",
            filed_names[i].long_name);

        return FALSE;
      } else {
        const GValue *val =
            gst_structure_get_value (structure, filed_names[i].short_name);

        gst_structure_set_value (structure, filed_names[i].long_name, val);
        gst_structure_remove_field (structure, filed_names[i].short_name);
      }
    } else if (gst_structure_has_field (structure, filed_names[i].long_name)) {
      exists = TRUE;
    }

    if (exists) {
      if (filed_names[i].type == GST_TYPE_CLOCK_TIME) {
        if (_convert_to_clocktime (structure, filed_names[i].long_name, 0) == 0) {
          *error = g_error_new (GES_ERROR, 0, "Could not convert"
              " %s to GstClockTime", filed_names[i].long_name);

          return FALSE;
        }
      }
    }

    if (filed_names[i].new_name
        && gst_structure_has_field (structure, filed_names[i].long_name)) {
      const GValue *val =
          gst_structure_get_value (structure, filed_names[i].long_name);

      gst_structure_set_value (structure, filed_names[i].new_name, val);
      gst_structure_remove_field (structure, filed_names[i].long_name);
    }
  }

  return TRUE;
}

static gboolean
_add_clip (GESTimeline * timeline, GstStructure * structure, GError ** error)
{
  const Properties filed_names[] = {
    {"uri", "n", 0, "asset-id"},
    {"name", "n", 0, NULL},
    {"start", "s", GST_TYPE_CLOCK_TIME, NULL},
    {"duration", "d", GST_TYPE_CLOCK_TIME, NULL},
    {"inpoint", "i", GST_TYPE_CLOCK_TIME, NULL},
    {"track-types", "tt", 0, NULL},
    {NULL},
  };

  if (!_cleanup_fields (filed_names, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESUriClip", NULL);

  GST_ERROR ("Adding a clip %" GST_PTR_FORMAT, structure);

  return _ges_add_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_add_effect (GESTimeline * timeline, GstStructure * structure, GError ** error)
{
  const Properties filed_names[] = {
    {"element-name", "e", 0, NULL},
    {"bin-description", "d", 0, "asset-id"},
    {"name", "n", 0, "child-name"},
    {NULL, NULL, 0, NULL},
  };

  if (!_cleanup_fields (filed_names, structure, error))
    return FALSE;

  gst_structure_set (structure, "child-type", G_TYPE_STRING, "GESEffect", NULL);

  GST_ERROR ("Adding a clip %" GST_PTR_FORMAT, structure);

  return _ges_container_add_child_from_struct (timeline, structure, error);
}

static gboolean
_set_child_property (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  return _ges_set_child_property_from_struct (timeline, structure, error);
}

static GOptionEntry timeline_parsing_options[] = {
  {"clip", 'c', 0.0, G_OPTION_ARG_CALLBACK, &_add_clip,
        "Adds a clip in the timeline",
      " start - s: The start position of the element inside the layer.\n"
        " duration - d: The duration of the clip.\n"
        " inpoint - i: The inpoint of the clip\n."
        " track-types - tt: The type of the tracks where the clip should be used:\n"
        " Examples:\n"
        "  * audio  / a\n"
        "  * video / v\n"
        "  * audio+video / a+v\n"
        " Will default to all the media types in the clip that match the global track-types"},
  {"effect", 'e', 0.0, G_OPTION_ARG_CALLBACK, &_add_effect,
        "Adds an effect as decribed by 'bin-description'",
        " bin-description - d: The description of the effect bin with a gst-launch-style pipeline description."
        " element-name - d: The name of the element to apply the effect on."
        /* TODO: Implement that:
         * " start - s: The start position of the element inside the layer -- implies creation of effect *Clip*.\n"
         * " duration - d: The duration of the clip -- implies creation of effect *Clip*.\n"
         * " inpoint - i: The inpoint of the clip-- implies creation of effect *Clip*.\n" */
      },
};

#define EXEC(func,structure,error) G_STMT_START { \
  gboolean res = ((ActionFromStructureFunc)func)(timeline, structure, error); \
  if (!res) {\
    GST_ERROR ("Could not execute: %" GST_PTR_FORMAT ", error: %s", structure, (*error)->message); \
    goto build_failure; \
  } \
} G_STMT_END

static GESTimeline *
create_timeline (GList * structures, const gchar * proj_uri,
    const gchar * scenario)
{
  guint i;
  GList *tmp;
  GESTimeline *timeline;
  GESTrack *tracka = NULL, *trackv = NULL;
  GESProject *project = ges_project_new (proj_uri);

  g_signal_connect (project, "error-loading-asset",
      G_CALLBACK (error_loading_asset_cb), NULL);

  if (proj_uri != NULL) {
    g_signal_connect (project, "loaded", G_CALLBACK (project_loaded_cb), NULL);
  }

  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));

  if (proj_uri) {
    goto done;
  }

  g_object_set (timeline, "auto-transition", TRUE, NULL);
  if (track_types & GES_TRACK_TYPE_VIDEO) {
    trackv = GES_TRACK (ges_video_track_new ());

    if (disable_mixing)
      ges_track_set_mixing (trackv, FALSE);

    if (!(ges_timeline_add_track (timeline, trackv)))
      goto build_failure;
  }

  if (track_types & GES_TRACK_TYPE_AUDIO) {
    tracka = GES_TRACK (ges_audio_track_new ());
    if (disable_mixing)
      ges_track_set_mixing (tracka, FALSE);

    if (!(ges_timeline_add_track (timeline, tracka)))
      goto build_failure;
  }

  /* Here we've finished initializing our timeline, we're
   * ready to start using it... by solely working with the layer !*/
  for (tmp = structures; tmp; tmp = tmp->next) {
    const gchar *name = gst_structure_get_name (tmp->data);
    GError *error = NULL;

    if (g_str_has_prefix (name, "set-")) {
      EXEC (_set_child_property, tmp->data, &error);
      continue;
    }

    for (i = 0; i < G_N_ELEMENTS (timeline_parsing_options); i++) {
      if (gst_structure_has_name (tmp->data,
              timeline_parsing_options[i].long_name)
          || (strlen (name) == 1 &&
              *name == timeline_parsing_options[i].short_name)) {
        EXEC (((ActionFromStructureFunc) timeline_parsing_options[i].arg_data),
            tmp->data, &error);
      }
    }
  }

done:
  return timeline;

build_failure:
  {
    gst_object_unref (timeline);
    return NULL;
  }
}

static gboolean
_save_timeline (GESTimeline * timeline, const gchar * load_path)
{
  if (save_path && !load_path) {
    gchar *uri;
    if (!(uri = ensure_uri (save_path))) {
      g_error ("couldn't create uri for '%s", save_path);
      return FALSE;
    }

    return ges_timeline_save_to_uri (timeline, uri, NULL, TRUE, NULL);
  }

  return TRUE;
}

static GESPipeline *
create_pipeline (GESTimeline ** ret_timeline, gchar * load_path,
    GList * structures, const gchar * scenario)
{
  gchar *uri = NULL;
  GESTimeline *timeline = NULL;

  /* Timeline creation */
  if (load_path) {
    g_printf ("Loading project from : %s\n", load_path);

    if (!(uri = ensure_uri (load_path))) {
      g_error ("couldn't create uri for '%s'", load_path);
      goto failure;
    }
  }

  pipeline = ges_pipeline_new ();

  if (!(timeline = create_timeline (structures, uri, scenario)))
    goto failure;

  if (!load_path)
    ges_timeline_commit (timeline);

  /* save project if path is given. we do this now in case GES crashes or
   * hangs during playback. */
  if (!_save_timeline (timeline, load_path))
    goto failure;

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */

  if (mute) {
    GstElement *sink = gst_element_factory_make ("fakesink", NULL);

    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_audio_sink (pipeline, sink);

    sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_video_sink (pipeline, sink);
  }

  /* Add the timeline to that pipeline */
  if (!ges_pipeline_set_timeline (pipeline, timeline))
    goto failure;

  *ret_timeline = timeline;

done:
  if (uri)
    g_free (uri);

  return pipeline;

failure:
  {
    if (timeline)
      gst_object_unref (timeline);
    if (pipeline)
      gst_object_unref (pipeline);
    pipeline = NULL;
    timeline = NULL;

    goto done;
  }
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, GMainLoop * mainloop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_WARNING:{
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch.warning");
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg_info = NULL;

      gst_message_parse_error (message, &err, &dbg_info);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch-error");
      g_printerr ("ERROR from element %s: %s\n", GST_OBJECT_NAME (message->src),
          err->message);
      g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_error_free (err);
      g_free (dbg_info);
      seenerrors = TRUE;
      g_main_loop_quit (mainloop);
      break;
    }
    case GST_MESSAGE_EOS:
      if (repeat > 0) {
        g_printerr ("Looping again\n");
        if (!gst_element_seek_simple (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH, 0))
          g_printerr ("seeking failed\n");
        else
          g_printerr ("seeking succeeded\n");
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
        g_printerr ("Looping set\n");
        repeat -= 1;
      } else {
        g_printerr ("\nDone\n");
        g_main_loop_quit (mainloop);
      }
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        gchar *dump_name;
        GstState old, new, pending;
        gchar *state_transition_name;

        gst_message_parse_state_changed (message, &old, &new, &pending);
        state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));
        dump_name = g_strconcat ("ges-launch.", state_transition_name, NULL);


        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);

        g_free (dump_name);
        g_free (state_transition_name);
      }
      break;
    case GST_MESSAGE_REQUEST_STATE:
      ges_validate_handle_request_state_change (message, mainloop);
      break;
    default:
      break;
  }
}

static void
print_enum (GType enum_type)
{
  GEnumClass *enum_class = G_ENUM_CLASS (g_type_class_ref (enum_type));
  guint i;

  for (i = 0; i < enum_class->n_values; i++) {
    g_printf ("%s\n", enum_class->values[i].value_nick);
  }

  g_type_class_unref (enum_class);
}

static void
print_transition_list (void)
{
  print_enum (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
}

static void
print_pattern_list (void)
{
  print_enum (GES_VIDEO_TEST_PATTERN_TYPE);
}

static GstEncodingProfile *
_parse_encoding_profile (const gchar * format)
{
  GstCaps *caps;
  GstEncodingProfile *encoding_profile = NULL;
  gchar **restriction_format, **preset_v;

  guint i = 1, presence = 0;
  GstCaps *restrictioncaps = NULL;
  gchar **strpresence_v, **strcaps_v = g_strsplit (format, ":", 0);

  if (strcaps_v[0] && *strcaps_v[0]) {
    if (strcaps_v[1] == NULL) {
      /* Only 1 profile which means no container used */
      i = 0;
    } else {
      caps = gst_caps_from_string (strcaps_v[0]);
      if (caps == NULL) {
        g_printerr ("Could not parse caps %s", strcaps_v[0]);
        return FALSE;
      }
      encoding_profile =
          GST_ENCODING_PROFILE (gst_encoding_container_profile_new
          ("User profile", "User profile", caps, NULL));
      gst_caps_unref (caps);
    }
  }

  for (; strcaps_v[i]; i++) {
    gchar *strcaps, *strpresence;
    char *preset_name = NULL;
    GstEncodingProfile *profile = NULL;

    restriction_format = g_strsplit (strcaps_v[i], "->", 0);
    if (restriction_format[1]) {
      restrictioncaps = gst_caps_from_string (restriction_format[0]);
      strcaps = g_strdup (restriction_format[1]);
    } else {
      restrictioncaps = NULL;
      strcaps = g_strdup (restriction_format[0]);
    }
    g_strfreev (restriction_format);

    preset_v = g_strsplit (strcaps, "+", 0);
    if (preset_v[1]) {
      strpresence = preset_v[1];
      g_free (strcaps);
      strcaps = g_strdup (preset_v[0]);
    } else {
      strpresence = preset_v[0];
    }

    strpresence_v = g_strsplit (strpresence, "|", 0);
    if (strpresence_v[1]) {     /* We have a presence */
      gchar *endptr;

      if (preset_v[1]) {        /* We have preset and presence */
        preset_name = g_strdup (strpresence_v[0]);
      } else {                  /* We have a presence but no preset */
        g_free (strcaps);
        strcaps = g_strdup (strpresence_v[0]);
      }

      presence = strtoll (strpresence_v[1], &endptr, 10);
      if (endptr == strpresence_v[1]) {
        g_printerr ("Wrong presence %s\n", strpresence_v[1]);

        return FALSE;
      }
    } else {                    /* We have no presence */
      if (preset_v[1]) {        /* Not presence but preset */
        preset_name = g_strdup (preset_v[1]);
        g_free (strcaps);
        strcaps = g_strdup (preset_v[0]);
      }                         /* Else we have no presence nor preset */
    }
    g_strfreev (strpresence_v);
    g_strfreev (preset_v);

    GST_DEBUG ("Creating preset with restrictions: %" GST_PTR_FORMAT
        ", caps: %s, preset %s, presence %d", restrictioncaps, strcaps,
        preset_name ? preset_name : "none", presence);

    caps = gst_caps_from_string (strcaps);
    g_free (strcaps);
    if (caps == NULL) {
      g_warning ("Could not create caps for %s", strcaps_v[i]);

      return FALSE;
    }

    if (g_str_has_prefix (strcaps_v[i], "audio/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_audio_profile_new (caps,
              preset_name, restrictioncaps, presence));
    } else if (g_str_has_prefix (strcaps_v[i], "video/") ||
        g_str_has_prefix (strcaps_v[i], "image/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_video_profile_new (caps,
              preset_name, restrictioncaps, presence));
    }

    g_free (preset_name);
    gst_caps_unref (caps);
    if (restrictioncaps)
      gst_caps_unref (restrictioncaps);

    if (profile == NULL) {
      g_warning ("No way to create a preset for caps: %s", strcaps_v[i]);

      return NULL;
    }

    if (encoding_profile) {
      if (gst_encoding_container_profile_add_profile
          (GST_ENCODING_CONTAINER_PROFILE (encoding_profile),
              profile) == FALSE) {
        g_warning ("Can not create a preset for caps: %s", strcaps_v[i]);

        return NULL;
      }
    } else {
      encoding_profile = profile;
    }
  }
  g_strfreev (strcaps_v);

  return encoding_profile;
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

/* g_free after usage */
static gchar *
sanitize_argument (gchar * arg)
{
  char *equal_index = strstr (arg, "=");
  char *space_index = strstr (arg, " ");
  gchar *new_string = g_malloc (sizeof (gchar) * (strlen (arg) + 3));
  gchar *tmp_string = new_string;

  if (!space_index)
    return g_strdup (arg);

  if (!equal_index || equal_index > space_index)
    return g_strdup_printf ("\"%s\"", arg);

  for (arg = arg; *arg != '\0'; arg++) {
    *tmp_string = *arg;
    tmp_string += 1;
    if (*arg == '=') {
      *tmp_string = '"';
      tmp_string += 1;
    }
  }
  *tmp_string = '"';
  tmp_string += 1;
  *tmp_string = '\0';

  return new_string;
}

static GESStructureParser *
_parse_timeline (int argc, char **argv)
{
  gint i;
  yyscan_t scanner;
  gchar *string = g_strdup (" ");
  GESStructureParser *parser = ges_structure_parser_new ();

  priv_ges_parse_yylex_init_extra (parser, &scanner);
  for (i = 1; i < argc; i++) {
    gchar *new_string;
    gchar *sanitized = sanitize_argument (argv[i]);

    new_string = g_strconcat (string, " ", sanitized, NULL);

    g_free (sanitized);
    g_free (string);
    string = new_string;
  }

  priv_ges_parse_yy_scan_string (string, scanner);
  priv_ges_parse_yylex (scanner);
  g_free (string);
  priv_ges_parse_yylex_destroy (scanner);

  ges_structure_parser_end_of_file (parser);
  return parser;
}

int
main (int argc, gchar ** argv)
{
  gint validate_res;
  GError *err = NULL;
  gchar *outputuri = NULL;
  const gchar *format = NULL;
  gchar *exclude_args = NULL;
  static gboolean smartrender = FALSE;
  static gboolean list_transitions = FALSE;
  static gboolean list_patterns = FALSE;
  static gdouble thumbinterval = 0;
  static gboolean verbose = FALSE;
  gchar *load_path = NULL;
  gchar *videosink = NULL, *audiosink = NULL;
  gboolean inspect_action_type = FALSE;
  gchar *encoding_profile = NULL;
  GESStructureParser *parser;

  GOptionEntry options[] = {
    {"thumbnail", 'm', 0.0, G_OPTION_ARG_DOUBLE, &thumbinterval,
        "Save thumbnail every <n> seconds to current directory", "<n>"},
    {"smartrender", 's', 0, G_OPTION_ARG_NONE, &smartrender,
        "Render to outputuri and avoid decoding/reencoding", NULL},
    {"outputuri", 'o', 0, G_OPTION_ARG_STRING, &outputuri,
        "URI to encode to", "<protocol>://<location>"},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &format,
          "Specify an encoding profile on the command line",
        "<profile>"},
    {"encoding-profile", 'e', 0, G_OPTION_ARG_STRING, &encoding_profile,
        "Use a specific encoding profile from XML", "<profile-name>"},
    {"repeat", 'r', 0, G_OPTION_ARG_INT, &repeat,
        "Number of times to repeat timeline", "<times>"},
    {"list-transitions", 't', 0, G_OPTION_ARG_NONE, &list_transitions,
        "List valid transition types and exit", NULL},
    {"list-patterns", 'p', 0, G_OPTION_ARG_NONE, &list_patterns,
        "List patterns and exit", NULL},
    {"save", 'z', 0, G_OPTION_ARG_STRING, &save_path,
        "Save project to file before rendering", "<path>"},
    {"load", 'l', 0, G_OPTION_ARG_STRING, &load_path,
        "Load project from file before rendering", "<path>"},
    {"verbose", 0, 0, G_OPTION_ARG_NONE, &verbose,
        "Output status information and property notifications", NULL},
    {"exclude", 'X', 0, G_OPTION_ARG_NONE, &exclude_args,
        "Do not output status information of <type>", "<type1>,<type2>,..."},
    {"track-types", 'p', 0, G_OPTION_ARG_CALLBACK, &parse_track_type,
        "Defines the track types to be created"},
    {"mute", 0, 0, G_OPTION_ARG_NONE, &mute,
        "Mute playback output by using fakesinks"},
    {"disable-mixing", 0, 0, G_OPTION_ARG_NONE, &disable_mixing,
        "Do not use mixing element in the tracks"},
    {"videosink", 'v', 0, G_OPTION_ARG_STRING, &videosink,
        "The video sink used for playing back", "<videosink>"},
    {"audiosink", 'a', 0, G_OPTION_ARG_STRING, &audiosink,
        "The audio sink used for playing back", "<audiosink>"},
    {"sample-paths", 'P', 0, G_OPTION_ARG_CALLBACK, &_add_media_path,
        "List of pathes to look assets in if they were moved"},
    {"sample-path-recurse", 'R', 0, G_OPTION_ARG_CALLBACK,
          &_add_media_path,
        "Same as above, but recursing into the folder"},
#ifdef HAVE_GST_VALIDATE
    {"inspect-action-type", 'y', 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the avalaible action types with which to write scenarios"
          " if no parameter passed, it will list all avalaible action types"
          " otherwize will print the full description of the wanted types",
        NULL},
    {"set-scenario", 0, 0, G_OPTION_ARG_STRING, &scenario,
        "Specify a GstValidate scenario to run, 'none' means load gst-validate"
          " but run no scenario on it", "<scenario_name>"},
#endif
    {NULL}
  };

  GOptionContext *ctx;
  GstBus *bus;

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  setlocale (LC_ALL, "");

  ctx = g_option_context_new ("- plays or renders a timeline.");
  g_option_context_set_summary (ctx,
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
      "             -f \"application/ogg:video/x-theora|<presence>:audio/x-vorbis\"");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_group (ctx, ges_init_get_option_group ());

  g_option_context_set_ignore_unknown_options (ctx, TRUE);

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  /* Initialize the GStreamer Editing Services */
  if (!ges_init ()) {
    g_printerr ("Error initializing GES\n");

    exit (1);
  }

  if (list_transitions) {
    print_transition_list ();
    exit (0);
  }

  if (list_patterns) {
    print_pattern_list ();
    exit (0);
  }

  if (inspect_action_type)
    return ges_validate_print_action_types ((const gchar **) argv + 1,
        argc - 1);

  if (((!load_path && !scenario && (argc < 1)))) {
    g_printf ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  /* Create the pipeline */
  parser = _parse_timeline (argc, argv);
  create_pipeline (&timeline, load_path, parser->structures, scenario);
  if (!pipeline)
    exit (1);

  if (videosink != NULL) {
    GError *err = NULL;
    GstElement *sink = gst_parse_bin_from_description (videosink, TRUE, &err);
    if (sink == NULL) {
      GST_ERROR ("could not create the requested videosink %s (err: %s), "
          "exiting", err ? err->message : "", videosink);
      exit (1);
    }
    ges_pipeline_preview_set_video_sink (pipeline, sink);
  }

  if (audiosink != NULL) {
    GError *err = NULL;
    GstElement *sink = gst_parse_bin_from_description (audiosink, TRUE, &err);
    if (sink == NULL) {
      GST_ERROR ("could not create the requested audiosink %s (err: %s), "
          "exiting", err ? err->message : "", audiosink);
      exit (1);
    }
    ges_pipeline_preview_set_audio_sink (pipeline, sink);
  }

  /* Setup profile/encoding if needed */
  if (smartrender || outputuri) {
    GstEncodingProfile *prof = NULL;

    if (!format) {
      GESProject *proj =
          GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)));
      const GList *profiles = ges_project_list_encoding_profiles (proj);

      if (profiles) {
        prof = profiles->data;
        if (encoding_profile)
          for (; profiles; profiles = profiles->next)
            if (strcmp (encoding_profile,
                    gst_encoding_profile_get_name (profiles->data)) == 0)
              prof = profiles->data;
      }
    }

    if (!prof) {
      if (format == NULL)
        format = "application/ogg:video/x-theora:audio/x-vorbis";

      prof = _parse_encoding_profile (format);
    }

    if (outputuri)
      outputuri = ensure_uri (outputuri);

    if (!prof || !ges_pipeline_set_render_settings (pipeline, outputuri, prof)
        || !ges_pipeline_set_mode (pipeline,
            smartrender ? GES_PIPELINE_MODE_SMART_RENDER :
            GES_PIPELINE_MODE_RENDER)) {
      g_free (outputuri);
      exit (1);
    }
    g_free (outputuri);

    gst_encoding_profile_unref (prof);
  } else {
    ges_pipeline_set_mode (pipeline, GES_PIPELINE_MODE_PREVIEW);
  }

  if (verbose) {
    gchar **exclude_list =
        exclude_args ? g_strsplit (exclude_args, ",", 0) : NULL;
    g_signal_connect (pipeline, "deep-notify",
        G_CALLBACK (gst_object_default_deep_notify), exclude_list);
  }

  /* Play the pipeline */
  mainloop = g_main_loop_new (NULL, FALSE);

  if (thumbinterval != 0.0) {
    g_printf ("thumbnailing every %f seconds\n", thumbinterval);
    g_timeout_add (1000 * thumbinterval, thumbnail_cb, pipeline);
  }

  if (!load_path) {
    if (ges_validate_activate (GST_PIPELINE (pipeline), scenario,
            &needs_set_state) == FALSE) {
      g_error ("Could not activate scenario %s", scenario);
      return 29;
    }
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), mainloop);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  if (!load_path) {
    if (needs_set_state && gst_element_set_state (GST_ELEMENT (pipeline),
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      g_error ("Failed to start the pipeline\n");
      return 1;
    }
  }
  g_main_loop_run (mainloop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  /*  Re save the timeline in case the scenario changed it! */
  _save_timeline (timeline, load_path);

  validate_res = ges_validate_clean (GST_PIPELINE (pipeline));
  if (seenerrors == FALSE)
    seenerrors = validate_res;

#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  return (int) seenerrors;
}
