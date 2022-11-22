/* GStreamer UDP network utility functions
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Joni Valtanen <joni.valtanen@movial.fi>
 * Copyright (C) 2009 Jarkko Palviainen <jarkko.palviainen@sesca.com>
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

#include <gst/gst.h>
#include <string.h>

#include "gstudpnetutils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_udp_debug);
#define GST_CAT_DEFAULT gst_udp_debug

gboolean
gst_udp_parse_uri (const gchar * uristr, gchar ** host, guint16 * port,
    GPtrArray * source_list)
{
  GstUri *uri;
  const gchar *protocol;

  uri = gst_uri_from_string (uristr);
  if (!uri) {
    GST_ERROR ("Invalid URI string %s", uristr);
    return FALSE;
  }

  /* consider no protocol to be udp:// */
  protocol = gst_uri_get_scheme (uri);
  if (!protocol) {
    GST_ERROR ("error parsing uri %s: no protocol", uristr);
    goto error;
  } else if (g_strcmp0 (protocol, "udp")) {
    GST_ERROR ("error parsing uri %s: wrong protocol (%s != udp)", uristr,
        protocol);
    goto error;
  }

  *host = g_strdup (gst_uri_get_host (uri));
  if (*host == NULL) {
    GST_ERROR ("Unknown host");
    goto error;
  }

  GST_DEBUG ("host set to '%s'", *host);

  *port = gst_uri_get_port (uri);

  if (source_list) {
    const gchar *source = gst_uri_get_query_value (uri, "multicast-source");
    if (source)
      gst_udp_parse_multicast_source (source, source_list);
  }

  gst_uri_unref (uri);
  return TRUE;

error:
  gst_uri_unref (uri);
  return FALSE;
}

static gboolean
gst_udp_source_filter_equal_func (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 ((const gchar *) a, (const gchar *) b) == 0;
}

gboolean
gst_udp_parse_multicast_source (const gchar * multicast_source,
    GPtrArray * source_list)
{
  gchar **list;
  guint i;
  gboolean found = FALSE;

  if (!multicast_source || !source_list)
    return FALSE;

  GST_DEBUG ("Parsing multicast source \"%s\"", multicast_source);

  list = g_strsplit_set (multicast_source, "+-", 0);

  for (i = 0; list[i] != NULL; i++) {
    gchar *prefix;
    gboolean is_positive = FALSE;

    if (*list[i] == '\0')
      continue;

    prefix = g_strrstr (multicast_source, list[i]);
    g_assert (prefix);

    /* Begin without '+' or '-' prefix, assume it's positive filter */
    if (prefix == multicast_source) {
      GST_WARNING ("%s without prefix, assuming that it's positive filter",
          list[i]);
      is_positive = TRUE;
    } else if (*(prefix - 1) == '+') {
      is_positive = TRUE;
    }

    if (is_positive &&
        !g_ptr_array_find_with_equal_func (source_list, list[i],
            gst_udp_source_filter_equal_func, NULL)) {

      GST_DEBUG ("Add multicast-source %s", list[i]);
      /* Moves ownership to array */
      g_ptr_array_add (source_list, g_strdup (list[i]));
      found = TRUE;
    }
  }

  g_strfreev (list);

  return found;
}
