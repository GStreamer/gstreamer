/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * SECTION:gstrtspurl
 * @short_description: handling RTSP urls
 *  
 * <refsect2>
 * <para>
 * Provides helper functions to handle RTSP urls.
 * </para>
 * </refsect2>
 *  
 * Last reviewed on 2007-07-25 (0.10.14)
 */

#include <stdlib.h>
#include <string.h>

#include "gstrtspurl.h"

static void
register_rtsp_url_type (GType * id)
{
  *id = g_boxed_type_register_static ("GstRTSPUrl",
      (GBoxedCopyFunc) gst_rtsp_url_copy, (GBoxedFreeFunc) gst_rtsp_url_free);
}

GType
gst_rtsp_url_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_rtsp_url_type, &id);
  return id;
}


#define RTSP_PROTO      "rtsp://"
#define RTSP_PROTO_LEN  7
#define RTSPU_PROTO     "rtspu://"
#define RTSPU_PROTO_LEN 8
#define RTSPT_PROTO     "rtspt://"
#define RTSPT_PROTO_LEN 8
#define RTSPH_PROTO     "rtsph://"
#define RTSPH_PROTO_LEN 8

/* format is rtsp[u]://[user:passwd@]host[:port]/abspath[?query] */

/**
 * gst_rtsp_url_parse:
 * @urlstr: the url string to parse
 * @url: location to hold the result.
 *
 * Parse the RTSP @urlstr into a newly allocated #GstRTSPUrl. Free after usage
 * with gst_rtsp_url_free().
 *
 * Returns: a #GstRTSPResult.
 */
GstRTSPResult
gst_rtsp_url_parse (const gchar * urlstr, GstRTSPUrl ** url)
{
  GstRTSPUrl *res;
  gchar *p, *delim, *at, *col;

  g_return_val_if_fail (urlstr != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (url != NULL, GST_RTSP_EINVAL);

  res = g_new0 (GstRTSPUrl, 1);

  p = (gchar *) urlstr;
  if (g_str_has_prefix (p, RTSP_PROTO)) {
    res->transports =
        GST_RTSP_LOWER_TRANS_TCP | GST_RTSP_LOWER_TRANS_UDP |
        GST_RTSP_LOWER_TRANS_UDP_MCAST;
    p += RTSP_PROTO_LEN;
  } else if (g_str_has_prefix (p, RTSPU_PROTO)) {
    res->transports = GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST;
    p += RTSPU_PROTO_LEN;
  } else if (g_str_has_prefix (p, RTSPT_PROTO)) {
    res->transports = GST_RTSP_LOWER_TRANS_TCP;
    p += RTSPT_PROTO_LEN;
  } else if (g_str_has_prefix (p, RTSPH_PROTO)) {
    res->transports = GST_RTSP_LOWER_TRANS_HTTP | GST_RTSP_LOWER_TRANS_TCP;
    p += RTSPH_PROTO_LEN;
  } else
    goto invalid;

  delim = strpbrk (p, "/?");
  at = strchr (p, '@');

  if (at && delim && at > delim)
    at = NULL;

  if (at) {
    col = strchr (p, ':');

    /* must have a ':' and it must be before the '@' */
    if (col == NULL || col > at)
      goto invalid;

    res->user = g_strndup (p, col - p);
    col++;
    res->passwd = g_strndup (col, at - col);

    /* move to host */
    p = at + 1;
  }

  col = strchr (p, ':');
  /* we have a ':' and a delimiter but the ':' is after the delimiter, it's
   * not really part of the hostname */
  if (col && delim && col >= delim)
    col = NULL;

  if (col) {
    res->host = g_strndup (p, col - p);
    p = col + 1;
    res->port = strtoul (p, (char **) &p, 10);
    if (delim)
      p = delim;
  } else {
    /* no port specified, set to 0. _get_port() will return the default port. */
    res->port = 0;
    if (!delim) {
      res->host = g_strdup (p);
      p = NULL;
    } else {
      res->host = g_strndup (p, delim - p);
      p = delim;
    }
  }

  if (p && *p == '/') {
    delim = strchr (p, '?');
    if (!delim) {
      res->abspath = g_strdup (p);
      p = NULL;
    } else {
      res->abspath = g_strndup (p, delim - p);
      p = delim;
    }
  } else {
    res->abspath = g_strdup ("/");
  }

  if (p && *p == '?')
    res->query = g_strdup (p + 1);

  *url = res;

  return GST_RTSP_OK;

  /* ERRORS */
invalid:
  {
    gst_rtsp_url_free (res);
    return GST_RTSP_EINVAL;
  }
}

/**
 * gst_rtsp_url_copy:
 * @url: a #GstRTSPUrl
 *
 * Make a copy of @url.
 *
 * Returns: a copy of @url. Free with gst_rtsp_url_free () after usage.
 *
 * Since: 0.10.22
 */
GstRTSPUrl *
gst_rtsp_url_copy (const GstRTSPUrl * url)
{
  GstRTSPUrl *res;

  g_return_val_if_fail (url != NULL, NULL);

  res = g_new0 (GstRTSPUrl, 1);

  res->transports = url->transports;
  res->family = url->family;
  res->user = g_strdup (url->user);
  res->passwd = g_strdup (url->passwd);
  res->host = g_strdup (url->host);
  res->port = url->port;
  res->abspath = g_strdup (url->abspath);
  res->query = g_strdup (url->query);

  return res;
}

/**
 * gst_rtsp_url_free:
 * @url: a #GstRTSPUrl
 *
 * Free the memory used by @url.
 */
void
gst_rtsp_url_free (GstRTSPUrl * url)
{
  if (url == NULL)
    return;

  g_free (url->user);
  g_free (url->passwd);
  g_free (url->host);
  g_free (url->abspath);
  g_free (url->query);
  g_free (url);
}

/**
 * gst_rtsp_url_set_port:
 * @url: a #GstRTSPUrl
 * @port: the port
 *
 * Set the port number in @url to @port.
 *
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_url_set_port (GstRTSPUrl * url, guint16 port)
{
  g_return_val_if_fail (url != NULL, GST_RTSP_EINVAL);

  url->port = port;

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_url_get_port:
 * @url: a #GstRTSPUrl
 * @port: location to hold the port
 *
 * Get the port number of @url.
 *
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_url_get_port (const GstRTSPUrl * url, guint16 * port)
{
  g_return_val_if_fail (url != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (port != NULL, GST_RTSP_EINVAL);

  /* if a port was specified, use that else use the default port. */
  if (url->port != 0)
    *port = url->port;
  else
    *port = GST_RTSP_DEFAULT_PORT;

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_url_get_request_uri:
 * @url: a #GstRTSPUrl
 *
 * Get a newly allocated string describing the request URI for @url. 
 *
 * Returns: a string with the request URI. g_free() after usage.
 */
gchar *
gst_rtsp_url_get_request_uri (const GstRTSPUrl * url)
{
  gchar *uri;

  g_return_val_if_fail (url != NULL, NULL);

  if (url->port != 0) {
    uri = g_strdup_printf ("rtsp://%s:%u%s%s%s", url->host, url->port,
        url->abspath, url->query ? "?" : "", url->query ? url->query : "");
  } else {
    uri = g_strdup_printf ("rtsp://%s%s%s%s", url->host, url->abspath,
        url->query ? "?" : "", url->query ? url->query : "");
  }

  return uri;
}
