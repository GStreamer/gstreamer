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

#define IS_ALPHANUM(c) (g_ascii_isalnum((c)) || ((c) == '-') || ((c) == '+'))

/* g_free after usage */
static gchar *
_sanitize_argument (gchar * arg)
{
  gboolean has_non_alphanum = FALSE;
  char *equal_index = strstr (arg, "=");
  gchar *new_string, *tmp_string;

  for (tmp_string = arg; *tmp_string != '\0'; tmp_string++) {
    if (!IS_ALPHANUM (*tmp_string)) {
      has_non_alphanum = TRUE;

      break;
    }
  }

  if (!has_non_alphanum)
    return g_strdup (arg);

  if (!equal_index)
    return g_strdup_printf ("\"%s\"", arg);

  tmp_string = new_string = g_malloc (sizeof (gchar) * (strlen (arg) + 3));
  for (; *arg != '\0'; arg++) {
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

gchar *
sanitize_timeline_description (gchar ** args)
{
  gint i;

  gchar *string = g_strdup (" ");

  for (i = 1; args[i]; i++) {
    gchar *new_string;
    gchar *sanitized = _sanitize_argument (args[i]);

    new_string = g_strconcat (string, " ", sanitized, NULL);

    g_free (sanitized);
    g_free (string);
    string = new_string;
  }

  return string;
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
    g_printf ("%s\n", enum_class->values[i].value_nick);
  }

  g_type_class_unref (enum_class);
}

void
print (GstDebugColorFlags c, gboolean err, gboolean nline, const gchar * format,
    va_list var_args)
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
    g_printerr ("%s", str->str);
  else
    g_print ("%s", str->str);

  g_string_free (str, TRUE);
}

void
ok (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  print (GST_DEBUG_FG_GREEN, FALSE, TRUE, format, var_args);
  va_end (var_args);
}

void
warn (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  print (GST_DEBUG_FG_YELLOW, TRUE, TRUE, format, var_args);
  va_end (var_args);
}

void
printerr (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  print (GST_DEBUG_FG_RED, TRUE, TRUE, format, var_args);
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
get_profile_type (GstEncodingProfile * profile)
{
  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile))
    return "Container";
  else if (GST_IS_ENCODING_AUDIO_PROFILE (profile))
    return "Audio";
  else if (GST_IS_ENCODING_VIDEO_PROFILE (profile))
    return "Video";
  else
    return "Unkonwn";
}

static void
print_profile (GstEncodingProfile * profile, const gchar * prefix)
{
  const gchar *name = gst_encoding_profile_get_name (profile);
  const gchar *desc = gst_encoding_profile_get_description (profile);
  GstCaps *format = gst_encoding_profile_get_format (profile);
  gchar *capsdesc;

  if (gst_caps_is_fixed (format))
    capsdesc = gst_pb_utils_get_codec_description (format);
  else
    capsdesc = gst_caps_to_string (format);

  g_print ("%s%s: %s%s%s%s%s%s\n", prefix, get_profile_type (profile),
      name ? name : capsdesc, desc ? ": " : "", desc ? desc : "",
      name ? " (" : "", name ? capsdesc : "", name ? ")" : "");

  gst_caps_unref (format);

  g_free (capsdesc);
}

void
describe_encoding_profile (GstEncodingProfile * profile)
{
  g_return_if_fail (GST_IS_ENCODING_PROFILE (profile));

  print_profile (profile, "  ");
  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
    const GList *tmp;

    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (profile)); tmp; tmp = tmp->next)
      print_profile (tmp->data, "    - ");
  }

}
