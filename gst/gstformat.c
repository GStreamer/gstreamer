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

#include "gstlog.h"

#include "gstformat.h"

static GList *_gst_formats = NULL;
static gint  _n_values = 1; /* we start from 1 because 0 reserved for UNDEFINED */

typedef struct _GstFormatDefinition GstFormatDefinition;

struct _GstFormatDefinition 
{
  GstFormat  value;
  gchar     *nick;
  gchar     *description;
};

static GstFormatDefinition standard_definitions[] = {
  { GST_FORMAT_DEFAULT, "default", "Default" },
  { GST_FORMAT_BYTES,   "bytes",   "Bytes" },
  { GST_FORMAT_TIME, 	"time",    "Time" }, 
  { GST_FORMAT_BUFFERS, "buffers", "Buffers" },
  { GST_FORMAT_PERCENT, "percent", "Percent" },
  { GST_FORMAT_UNITS,   "units",   "Units as defined by the media type" },
  { 0, NULL, NULL }
};

void		
_gst_format_initialize (void)
{
  GstFormatDefinition *standards = standard_definitions;

  while (standards->nick) {
    _gst_formats = g_list_prepend (_gst_formats, standards);
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
gst_format_register (const gchar *nick, const gchar *description)
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

  _gst_formats = g_list_prepend (_gst_formats, format);

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
gst_format_get_by_nick (const gchar *nick)
{
  GList *walk;
  GstFormatDefinition *format;
  
  g_return_val_if_fail (nick != NULL, 0);

  walk = _gst_formats;

  while (walk) {
    format = (GstFormatDefinition *) walk->data;
    
    if (!strcmp (format->nick, nick))
      return format->value;

    walk = g_list_next (walk);
  }

  return GST_FORMAT_UNDEFINED;
}

/**
 * gst_format_get_details:
 * @format: The format to get details of
 * @nick: The nick of the format
 * @description: The description of the format
 *
 * Get details about the given format.
 *
 * Returns: TRUE if the format was registered, FALSE otherwise
 */
gboolean
gst_format_get_details (GstFormat format, const gchar **nick, const gchar **description)
{
  GList *walk;
  GstFormatDefinition *walk_format;

  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (description != NULL, FALSE);

  walk = _gst_formats;

  while (walk) {
    walk_format = (GstFormatDefinition *) walk->data;
    
    if (walk_format->value == format) {
      *nick = walk_format->nick;
      *description = walk_format->description;

      return TRUE;
    }

    walk = g_list_next (walk);
  }
  return FALSE;
}
