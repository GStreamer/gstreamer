/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstformat.c: GstFormat registration
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gst_private.h"
#include "gstformat.h"

static GList *_gst_formats = NULL;
static GHashTable *_nick_to_format = NULL;
static GHashTable *_format_to_nick = NULL;
static gint _n_values = 1;      /* we start from 1 because 0 reserved for UNDEFINED */

static GstFormatDefinition standard_definitions[] = {
  {GST_FORMAT_DEFAULT, "default", "Default format for the media type"},
  {GST_FORMAT_BYTES, "bytes", "Bytes"},
  {GST_FORMAT_TIME, "time", "Time"},
  {GST_FORMAT_BUFFERS, "buffers", "Buffers"},
  {GST_FORMAT_PERCENT, "percent", "Percent"},
  {0, NULL, NULL}
};

void
_gst_format_initialize (void)
{
  GstFormatDefinition *standards = standard_definitions;

  if (_nick_to_format == NULL) {
    _nick_to_format = g_hash_table_new (g_str_hash, g_str_equal);
    _format_to_nick = g_hash_table_new (NULL, NULL);
  }

  while (standards->nick) {
    g_hash_table_insert (_nick_to_format, standards->nick, standards);
    g_hash_table_insert (_format_to_nick, GINT_TO_POINTER (standards->value),
        standards);

    _gst_formats = g_list_append (_gst_formats, standards);
    standards++;
    _n_values++;
  }
}

/**
 * gst_format_register:
 * @nick: The nick of the new format
 * @description: The description of the new format
 *
 * Create a new GstFormat based on the nick or return an
 * allrady registered format with that nick
 *
 * Returns: A new GstFormat or an already registered format
 * with the same nick.
 */
GstFormat
gst_format_register (const gchar * nick, const gchar * description)
{
  GstFormatDefinition *format;
  GstFormat query;

  g_return_val_if_fail (nick != NULL, 0);
  g_return_val_if_fail (description != NULL, 0);

  query = gst_format_get_by_nick (nick);
  if (query != GST_FORMAT_UNDEFINED)
    return query;

  format = g_new0 (GstFormatDefinition, 1);
  format->value = _n_values;
  format->nick = g_strdup (nick);
  format->description = g_strdup (description);

  g_hash_table_insert (_nick_to_format, format->nick, format);
  g_hash_table_insert (_format_to_nick, GINT_TO_POINTER (format->value),
      format);
  _gst_formats = g_list_append (_gst_formats, format);
  _n_values++;

  return format->value;
}

/**
 * gst_format_get_by_nick:
 * @nick: The nick of the format
 *
 * Return the format registered with the given nick. 
 *
 * Returns: The format with @nick or GST_FORMAT_UNDEFINED
 * if the format was not registered.
 */
GstFormat
gst_format_get_by_nick (const gchar * nick)
{
  GstFormatDefinition *format;

  g_return_val_if_fail (nick != NULL, 0);

  format = g_hash_table_lookup (_nick_to_format, nick);

  if (format != NULL)
    return format->value;
  else
    return GST_FORMAT_UNDEFINED;
}

/**
 * gst_formats_contains:
 * @formats: The format array to search
 * @format: the format to find
 *
 * See if the given format is inside the format array.
 *
 * Returns: TRUE if the format is found inside the array
 */
gboolean
gst_formats_contains (const GstFormat * formats, GstFormat format)
{
  if (!formats)
    return FALSE;

  while (*formats) {
    if (*formats == format)
      return TRUE;

    formats++;
  }
  return FALSE;
}


/**
 * gst_format_get_details:
 * @format: The format to get details of
 *
 * Get details about the given format.
 *
 * Returns: The #GstFormatDefinition for @format or NULL on failure.
 */
const GstFormatDefinition *
gst_format_get_details (GstFormat format)
{
  return g_hash_table_lookup (_format_to_nick, GINT_TO_POINTER (format));
}

/**
 * gst_format_get_definitions:
 *
 * Get a list of all the registered formats.
 *
 * Returns: A GList of #GstFormatDefinition.
 */
const GList *
gst_format_get_definitions (void)
{
  return _gst_formats;
}
