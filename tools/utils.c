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

/* g_free after usage */
static gchar *
_sanitize_argument (gchar * arg)
{
  char *equal_index = strstr (arg, "=");
  char *space_index = strstr (arg, " ");
  gchar *new_string, *tmp_string;

  if (!space_index)
    return g_strdup (arg);

  if (!equal_index || equal_index > space_index)
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
sanitize_timeline_description (int argc, char **argv)
{
  gint i;

  gchar *string = g_strdup (" ");

  for (i = 1; i < argc; i++) {
    gchar *new_string;
    gchar *sanitized = _sanitize_argument (argv[i]);

    new_string = g_strconcat (string, " ", sanitized, NULL);

    g_free (sanitized);
    g_free (string);
    string = new_string;
  }

  return string;
}

guint
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
