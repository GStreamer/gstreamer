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

#include <stdlib.h>
#include <glib/gprintf.h>
#include <string.h>
#include <gst/gst.h>
#include "utils.h"
#include "../ges/ges-internal.h"

#undef GST_CAT_DEFAULT

/* Copy of GST_ASCII_IS_STRING */
#define ASCII_IS_STRING(c) (g_ascii_isalnum((c)) || ((c) == '_') || \
    ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == ':') || \
    ((c) == '.'))

/* g_free after usage */
static gchar *
_sanitize_argument (gchar * arg, const gchar * prev_arg)
{
  gboolean expect_equal = !(arg[0] == '+' || g_str_has_prefix (arg, "set-")
      || prev_arg == NULL || prev_arg[0] == '+'
      || g_str_has_prefix (prev_arg, "set-"));
  gboolean need_wrap = FALSE;
  gchar *first_equal = NULL;
  gchar *wrap_start;
  gchar *new_string, *tmp_string;
  gsize num_escape;

  for (tmp_string = arg; *tmp_string != '\0'; tmp_string++) {
    if (expect_equal && first_equal == NULL && *tmp_string == '=') {
      first_equal = tmp_string;
      /* if this is the first equal, then don't count it as necessarily
       * needing a wrap */
    } else if (!ASCII_IS_STRING (*tmp_string)) {
      need_wrap = TRUE;
      break;
    }
  }

  if (!need_wrap)
    return g_strdup (arg);

  if (first_equal)
    wrap_start = first_equal + 1;
  else
    wrap_start = arg;

  /* need to escape any '"' or '\\' to correctly parse in as a structure */
  num_escape = 0;
  for (tmp_string = wrap_start; *tmp_string != '\0'; tmp_string++) {
    if (*tmp_string == '"' || *tmp_string == '\\')
      num_escape++;
  }

  tmp_string = new_string =
      g_malloc (sizeof (gchar) * (strlen (arg) + num_escape + 3));

  while (arg != wrap_start)
    *(tmp_string++) = *(arg++);
  (*tmp_string++) = '"';

  while (*arg != '\0') {
    if (*arg == '"' || *arg == '\\')
      (*tmp_string++) = '\\';
    *(tmp_string++) = *(arg++);
  }
  *(tmp_string++) = '"';
  *tmp_string = '\0';

  return new_string;
}

gchar *
sanitize_timeline_description (gchar ** args, GESLauncherParsedOptions * opts)
{
  gint i;
  gchar *prev_arg = NULL;
  GString *track_def;
  GString *timeline_str;
  gboolean adds_tracks = FALSE;

  gchar *string = g_strdup (" ");

  for (i = 1; args[i]; i++) {
    gchar *new_string;
    gchar *sanitized = _sanitize_argument (args[i], prev_arg);

    new_string = g_strconcat (string, " ", sanitized, NULL);

    adds_tracks |= (g_strcmp0 (args[i], "+track") == 0);

    g_free (sanitized);
    g_free (string);
    string = new_string;
    prev_arg = args[i];
  }

  if (i == 1) {
    g_free (string);

    return NULL;
  }

  if (adds_tracks) {
    gchar *res = g_strconcat ("ges:", string, NULL);
    g_free (string);

    return res;
  }

  timeline_str = g_string_new (string);
  g_free (string);

  if (opts->track_types & GES_TRACK_TYPE_VIDEO) {
    track_def = g_string_new (" +track video ");

    if (opts->video_track_caps)
      g_string_append_printf (track_def, " restrictions=[%s] ",
          opts->video_track_caps);

    g_string_prepend (timeline_str, track_def->str);
    g_string_free (track_def, TRUE);
  }

  if (opts->track_types & GES_TRACK_TYPE_AUDIO) {
    track_def = g_string_new (" +track audio ");

    if (opts->audio_track_caps)
      g_string_append_printf (track_def, " restrictions=[%s] ",
          opts->audio_track_caps);

    g_string_prepend (timeline_str, track_def->str);
    g_string_free (track_def, TRUE);
  }

  g_string_prepend (timeline_str, "ges:");

  return g_string_free (timeline_str, FALSE);
}

gboolean
get_flags_from_string (GType type, const gchar * str_flags, guint * flags)
{
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_flags)) {
    g_value_unset (&value);

    return FALSE;
  }

  *flags = g_value_get_flags (&value);
  g_value_unset (&value);

  return TRUE;
}

gchar *
ensure_uri (const gchar * location)
{
  if (gst_uri_is_valid (location))
    return g_strdup (location);
  else
    return gst_filename_to_uri (location, NULL);
}

GstEncodingProfile *
parse_encoding_profile (const gchar * format)
{
  GstEncodingProfile *profile;
  GValue value = G_VALUE_INIT;

  g_value_init (&value, GST_TYPE_ENCODING_PROFILE);

  if (!gst_value_deserialize (&value, format)) {
    g_value_reset (&value);

    return NULL;
  }

  profile = g_value_dup_object (&value);
  g_value_reset (&value);

  return profile;
}

void
print_enum (GType enum_type)
{
  GEnumClass *enum_class = G_ENUM_CLASS (g_type_class_ref (enum_type));
  guint i;

  for (i = 0; i < enum_class->n_values; i++) {
    gst_print ("%s\n", enum_class->values[i].value_nick);
  }

  g_type_class_unref (enum_class);
}

void
ges_print (GstDebugColorFlags c, gboolean err, gboolean nline,
    const gchar * format, va_list var_args)
{
  GString *str = g_string_new (NULL);
  GstDebugColorMode color_mode;
  gchar *color = NULL;
  const gchar *clear = NULL;

  color_mode = gst_debug_get_color_mode ();
#ifdef G_OS_WIN32
  if (color_mode == GST_DEBUG_COLOR_MODE_UNIX) {
#else
  if (color_mode != GST_DEBUG_COLOR_MODE_OFF) {
#endif
    clear = "\033[00m";
    color = gst_debug_construct_term_color (c);
  }

  if (color) {
    g_string_append (str, color);
    g_free (color);
  }

  g_string_append_vprintf (str, format, var_args);

  if (nline)
    g_string_append_c (str, '\n');

  if (clear)
    g_string_append (str, clear);

  if (err)
    gst_printerr ("%s", str->str);
  else
    gst_print ("%s", str->str);

  g_string_free (str, TRUE);
}

void
ges_ok (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  ges_print (GST_DEBUG_FG_GREEN, FALSE, TRUE, format, var_args);
  va_end (var_args);
}

void
ges_warn (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  ges_print (GST_DEBUG_FG_YELLOW, TRUE, TRUE, format, var_args);
  va_end (var_args);
}

void
ges_printerr (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  ges_print (GST_DEBUG_FG_RED, TRUE, TRUE, format, var_args);
  va_end (var_args);
}

gchar *
get_file_extension (gchar * uri)
{
  size_t len;
  gint find;

  len = strlen (uri);
  find = len - 1;

  while (find >= 0) {
    if (uri[find] == '.')
      break;
    find--;
  }

  if (find <= 0)
    return NULL;

  return g_strdup (&uri[find + 1]);
}

static const gchar *
get_type_icon (gpointer obj)
{
  if (GST_IS_ENCODING_AUDIO_PROFILE (obj) || GST_IS_DISCOVERER_AUDIO_INFO (obj))
    return "♫";
  else if (GST_IS_ENCODING_VIDEO_PROFILE (obj)
      || GST_IS_DISCOVERER_VIDEO_INFO (obj))
    return "▶";
  else if (GST_IS_ENCODING_CONTAINER_PROFILE (obj)
      || GST_IS_DISCOVERER_CONTAINER_INFO (obj))
    return "∋";
  else
    return "";
}

static void
print_profile (GstEncodingProfile * profile, const gchar * prefix)
{
  const gchar *name = gst_encoding_profile_get_name (profile);
  const gchar *desc = gst_encoding_profile_get_description (profile);
  GstCaps *format = gst_encoding_profile_get_format (profile);
  gchar *capsdesc = NULL;

  if (gst_caps_is_fixed (format))
    capsdesc = gst_pb_utils_get_codec_description (format);
  if (!capsdesc)
    capsdesc = gst_caps_to_string (format);

  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
    gst_print ("%s> %s %s: %s%s%s%s\n", prefix,
        get_type_icon (profile),
        capsdesc, name ? name : "",
        desc ? " (" : "", desc ? desc : "", desc ? ")" : "");

  } else {
    gst_print ("%s%s %s%s%s%s%s%s", prefix, get_type_icon (profile),
        name ? name : capsdesc, desc ? ": " : "", desc ? desc : "",
        name ? " (" : "", name ? capsdesc : "", name ? ")" : "");

    if (GST_IS_ENCODING_VIDEO_PROFILE (profile)) {
      GstCaps *caps = gst_encoding_profile_get_restriction (profile);

      if (!caps && gst_caps_is_fixed (format))
        caps = gst_caps_ref (format);

      if (caps) {
        GstVideoInfo info;

        if (gst_video_info_from_caps (&info, caps)) {
          gst_print (" (%dx%d", info.width, info.height);
          if (info.fps_n)
            gst_print ("@%d/%dfps", info.fps_n, info.fps_d);
          gst_print (")");
        }
        gst_caps_unref (caps);
      }
    } else if (GST_IS_ENCODING_AUDIO_PROFILE (profile)) {
      GstCaps *caps = gst_encoding_profile_get_restriction (profile);

      if (!caps && gst_caps_is_fixed (format))
        caps = gst_caps_ref (format);

      if (caps) {
        GstAudioInfo info;

        if (gst_caps_is_fixed (caps) && gst_audio_info_from_caps (&info, caps))
          gst_print (" (%d channels @ %dhz)", info.channels, info.rate);
        gst_caps_unref (caps);
      }
    }


    gst_print ("\n");
  }

  gst_caps_unref (format);

  g_free (capsdesc);
}

void
describe_encoding_profile (GstEncodingProfile * profile)
{
  g_return_if_fail (GST_IS_ENCODING_PROFILE (profile));

  print_profile (profile, "     ");
  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
    const GList *tmp;

    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (profile)); tmp; tmp = tmp->next)
      print_profile (tmp->data, "       - ");
  }
}

static void
describe_stream_info (GstDiscovererStreamInfo * sinfo, GString * desc)
{
  gchar *capsdesc;
  GstCaps *caps;

  caps = gst_discoverer_stream_info_get_caps (sinfo);
  capsdesc = gst_pb_utils_get_codec_description (caps);
  if (!capsdesc)
    capsdesc = gst_caps_to_string (caps);
  gst_caps_unref (caps);

  g_string_append_printf (desc, "%s%s%s", desc->len ? ", " : "",
      get_type_icon (sinfo), capsdesc);

  g_free (capsdesc);

  if (GST_IS_DISCOVERER_CONTAINER_INFO (sinfo)) {
    GList *tmp, *streams;

    streams =
        gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO
        (sinfo));
    for (tmp = streams; tmp; tmp = tmp->next)
      describe_stream_info (tmp->data, desc);
    gst_discoverer_stream_info_list_free (streams);
  }
}

static gchar *
describe_discoverer (GstDiscovererInfo * info)
{
  GString *desc = g_string_new (NULL);
  GstDiscovererStreamInfo *sinfo = gst_discoverer_info_get_stream_info (info);

  describe_stream_info (sinfo, desc);
  gst_discoverer_stream_info_unref (sinfo);

  return g_string_free (desc, FALSE);
}

void
print_timeline (GESTimeline * timeline)
{
  gchar *uri;
  GList *layer, *clip, *clips;

  if (!timeline->layers)
    return;

  uri = ges_command_line_formatter_get_timeline_uri (timeline);
  gst_print ("\nTimeline description: `%s`\n", &uri[5]);
  g_free (uri);
  gst_print ("====================\n\n");
  for (layer = timeline->layers; layer; layer = layer->next) {
    clips = ges_layer_get_clips (layer->data);

    if (!clips)
      continue;

    gst_printerr ("  layer %d: \n", ges_layer_get_priority (layer->data));
    gst_printerr ("  --------\n");
    for (clip = clips; clip; clip = clip->next) {
      gchar *name;

      if (GES_IS_URI_CLIP (clip->data)) {
        GESUriClipAsset *asset =
            GES_URI_CLIP_ASSET (ges_extractable_get_asset (clip->data));
        gchar *asset_desc =
            describe_discoverer (ges_uri_clip_asset_get_info (asset));

        name = g_strdup_printf ("Clip from: '%s' [%s]",
            ges_asset_get_id (GES_ASSET (asset)), asset_desc);
        g_free (asset_desc);
      } else {
        name = g_strdup (GES_TIMELINE_ELEMENT_NAME (clip->data));
      }
      gst_print ("    - %s\n        start=%" GST_TIME_FORMAT,
          name, GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (clip->data)));
      g_free (name);
      if (GES_TIMELINE_ELEMENT_INPOINT (clip->data))
        gst_print (" inpoint=%" GST_TIME_FORMAT,
            GST_TIME_ARGS (GES_TIMELINE_ELEMENT_INPOINT (clip->data)));
      gst_print (" duration=%" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (GES_TIMELINE_ELEMENT_END (clip->data)));
    }
    if (layer->next)
      gst_printerr ("\n");

    g_list_free_full (clips, gst_object_unref);
  }

  gst_print ("\n");
}
