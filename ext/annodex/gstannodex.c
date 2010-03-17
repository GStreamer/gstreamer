/*
 * gstannodex.c - GStreamer annodex plugin
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <gst/tag/tag.h>
#include "gstannodex.h"
#include "gstcmmlparser.h"
#include "gstcmmlenc.h"
#include "gstcmmldec.h"

GstClockTime
gst_annodex_granule_to_time (gint64 granulepos, gint64 granulerate_n,
    gint64 granulerate_d, guint8 granuleshift)
{
  gint64 keyindex, keyoffset;
  gint64 granulerate;
  GstClockTime res;

  g_return_val_if_fail (granuleshift <= 64, GST_CLOCK_TIME_NONE);

  if (granulepos == -1)
    return GST_CLOCK_TIME_NONE;

  if (granulepos == 0 || granulerate_n == 0 || granulerate_d == 0)
    return 0;

  if (granuleshift != 0 && granuleshift != 64) {
    keyindex = granulepos >> granuleshift;
    keyoffset = granulepos - (keyindex << granuleshift);
    granulepos = keyindex + keyoffset;
  }

  /* GST_SECOND / (granulerate_n / granulerate_d) */
  granulerate = gst_util_uint64_scale (GST_SECOND,
      granulerate_d, granulerate_n);

  /* granulepos * granulerate */
  res = gst_util_uint64_scale (granulepos, granulerate, 1);

  return res;
}

GValueArray *
gst_annodex_parse_headers (const gchar * headers)
{
  GValueArray *array;
  GValue val = { 0 };
  gchar *header_name = NULL;
  gchar *header_value = NULL;
  gchar *line, *column, *space, *tmp;
  gchar **lines;
  gint i = 0;

  array = g_value_array_new (0);
  g_value_init (&val, G_TYPE_STRING);

  lines = g_strsplit (headers, "\r\n", 0);
  line = lines[i];
  while (line != NULL && *line != '\0') {
    if (line[0] == '\t' || line[0] == ' ') {
      /* WSP: continuation line */
      if (header_value == NULL)
        /* continuation line without a previous value */
        goto fail;

      tmp = g_strjoin (" ", header_value, g_strstrip (line), NULL);
      g_free (header_value);
      header_value = tmp;
    } else {
      if (header_name) {
        g_value_take_string (&val, header_name);
        g_value_array_append (array, &val);
        g_value_take_string (&val, header_value);
        g_value_array_append (array, &val);
      }
      /* search the column starting from line[1] as an header name can't be
       * empty */
      column = g_strstr_len (line + 1, strlen (line) - 1, ":");
      if (column == NULL)
        /* bad syntax */
        goto fail;

      if (*(space = column + 1) != ' ')
        /* bad syntax */
        goto fail;

      header_name = g_strndup (line, column - line);
      header_value = g_strdup (space + 1);
    }

    line = lines[++i];
  }

  if (header_name) {
    g_value_take_string (&val, header_name);
    g_value_array_append (array, &val);
    g_value_take_string (&val, header_value);
    g_value_array_append (array, &val);
  }

  g_value_unset (&val);
  g_strfreev (lines);

  return array;

fail:
  GST_WARNING ("could not parse annodex headers");
  g_free (header_name);
  g_free (header_value);
  g_strfreev (lines);
  g_value_array_free (array);
  g_value_unset (&val);
  return NULL;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_tag_register (GST_TAG_CMML_STREAM, GST_TAG_FLAG_META,
      GST_TYPE_CMML_TAG_STREAM, "cmml-stream", "annodex CMML stream tag", NULL);

  gst_tag_register (GST_TAG_CMML_HEAD, GST_TAG_FLAG_META,
      GST_TYPE_CMML_TAG_HEAD, "cmml-head", "annodex CMML head tag", NULL);

  gst_tag_register (GST_TAG_CMML_CLIP, GST_TAG_FLAG_META,
      GST_TYPE_CMML_TAG_CLIP, "cmml-clip", "annodex CMML clip tag", NULL);

  gst_cmml_parser_init ();

  if (!gst_cmml_enc_plugin_init (plugin))
    return FALSE;

  if (!gst_cmml_dec_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "annodex",
    "annodex stream manipulation (info about annodex: http://www.annodex.net)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
