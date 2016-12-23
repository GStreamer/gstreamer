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
