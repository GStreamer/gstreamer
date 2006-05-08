/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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
#include <stdlib.h>
#include <string.h>

#include "rtspurl.h"

#define RTSP_PROTO      "rtsp://"
#define RTSP_PROTO_LEN  7
#define RTSPU_PROTO     "rtspu://"
#define RTSPU_PROTO_LEN 8

/* format is rtsp[u]://[user:passwd@]host[:port]/abspath */

RTSPResult
rtsp_url_parse (const gchar * urlstr, RTSPUrl ** url)
{
  RTSPUrl *res;
  gchar *p, *slash, *at, *col;

  res = g_new0 (RTSPUrl, 1);

  if (urlstr == NULL)
    goto invalid;

  p = (gchar *) urlstr;
  if (g_str_has_prefix (p, RTSP_PROTO)) {
    res->protocol = RTSP_PROTO_TCP;
    p += RTSP_PROTO_LEN;
  } else if (g_str_has_prefix (p, RTSPU_PROTO)) {
    res->protocol = RTSP_PROTO_UDP;
    p += RTSPU_PROTO_LEN;
  } else
    goto invalid;

  slash = strstr (p, "/");
  at = strstr (p, "@");

  if (at && slash && at > slash)
    at = NULL;

  if (at) {
    col = strstr (p, ":");

    /* must have a ':' and it must be before the '@' */
    if (col == NULL || col > at)
      goto invalid;

    res->user = g_strndup (p, col - p);
    col++;
    res->passwd = g_strndup (col, col - at);

    /* move to host */
    p = at + 1;
  }

  col = strstr (p, ":");
  if (col) {
    res->host = g_strndup (p, col - p);
    p = col + 1;
    res->port = strtoul (p, (char **) &p, 10);
    if (slash)
      p = slash + 1;
  } else {
    res->port = RTSP_DEFAULT_PORT;
    if (!slash) {
      res->host = g_strdup (p);
      p = NULL;
    } else {
      res->host = g_strndup (p, slash - p);
      p = slash + 1;
    }
  }
  if (p)
    res->abspath = g_strdup (p);

  *url = res;

  return RTSP_OK;

invalid:
  {
    rtsp_url_free (res);
    return RTSP_EINVAL;
  }
}

void
rtsp_url_free (RTSPUrl * url)
{
  g_free (url->user);
  g_free (url->passwd);
  g_free (url->host);
  g_free (url->abspath);
  g_free (url);
}
