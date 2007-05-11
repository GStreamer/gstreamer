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

#include <string.h>
#include <stdlib.h>

#include "rtsptransport.h"

#define MAX_MANAGERS	2

typedef struct
{
  const gchar *name;
  const RTSPTransMode mode;
  const gchar *gst_mime;
  const gchar *manager[MAX_MANAGERS];
} RTSPTransMap;

static const RTSPTransMap transports[] = {
  {"rtp", RTSP_TRANS_RTP, "application/x-rtp", {"rtpbin", "rtpdec"}},
  {"x-real-rdt", RTSP_TRANS_RDT, "application/x-rdt", {NULL, NULL}},
  {"x-pn-tng", RTSP_TRANS_RDT, "application/x-rdt", {NULL, NULL}},
  {NULL, RTSP_TRANS_UNKNOWN, NULL, {NULL, NULL}}
};

typedef struct
{
  const gchar *name;
  const RTSPProfile profile;
} RTSPProfileMap;

static const RTSPProfileMap profiles[] = {
  {"avp", RTSP_PROFILE_AVP},
  {"savp", RTSP_PROFILE_SAVP},
  {NULL, RTSP_PROFILE_UNKNOWN}
};

typedef struct
{
  const gchar *name;
  const RTSPLowerTrans ltrans;
} RTSPLTransMap;

static const RTSPLTransMap ltrans[] = {
  {"udp", RTSP_LOWER_TRANS_UDP},
  {"mcast", RTSP_LOWER_TRANS_UDP_MCAST},
  {"tcp", RTSP_LOWER_TRANS_TCP},
  {NULL, RTSP_LOWER_TRANS_UDP}  /* UDP is default */
};

RTSPResult
rtsp_transport_new (RTSPTransport ** transport)
{
  RTSPTransport *trans;

  g_return_val_if_fail (transport != NULL, RTSP_EINVAL);

  trans = g_new0 (RTSPTransport, 1);

  *transport = trans;

  return rtsp_transport_init (trans);
}

RTSPResult
rtsp_transport_init (RTSPTransport * transport)
{
  g_return_val_if_fail (transport != NULL, RTSP_EINVAL);

  g_free (transport->destination);
  g_free (transport->source);
  g_free (transport->ssrc);

  memset (transport, 0, sizeof (RTSPTransport));

  transport->trans = RTSP_TRANS_RTP;
  transport->profile = RTSP_PROFILE_AVP;
  transport->lower_transport = RTSP_LOWER_TRANS_UDP;
  transport->mode_play = TRUE;
  transport->mode_record = FALSE;

  return RTSP_OK;
}

RTSPResult
rtsp_transport_get_mime (RTSPTransMode trans, const gchar ** mime)
{
  gint i;

  g_return_val_if_fail (mime != NULL, RTSP_EINVAL);

  for (i = 0; transports[i].name; i++)
    if (transports[i].mode == trans)
      break;
  *mime = transports[i].gst_mime;

  return RTSP_OK;
}

RTSPResult
rtsp_transport_get_manager (RTSPTransMode trans, const gchar ** manager,
    guint option)
{
  gint i;

  g_return_val_if_fail (manager != NULL, RTSP_EINVAL);

  for (i = 0; transports[i].name; i++)
    if (transports[i].mode == trans)
      break;

  if (option < MAX_MANAGERS)
    *manager = transports[i].manager[option];
  else
    *manager = NULL;

  return RTSP_OK;
}

static void
parse_mode (RTSPTransport * transport, gchar * str)
{
  transport->mode_play = (strstr (str, "\"play\"") != NULL);
  transport->mode_record = (strstr (str, "\"record\"") != NULL);
}

static void
parse_range (RTSPTransport * transport, gchar * str, RTSPRange * range)
{
  gchar *minus;

  minus = strstr (str, "-");
  if (minus) {
    range->min = atoi (str);
    range->max = atoi (minus + 1);
  } else {
    range->min = atoi (str);
    range->max = -1;
  }
}

RTSPResult
rtsp_transport_parse (const gchar * str, RTSPTransport * transport)
{
  gchar **split, *down;
  gint i;

  g_return_val_if_fail (transport != NULL, RTSP_EINVAL);
  g_return_val_if_fail (str != NULL, RTSP_EINVAL);

  rtsp_transport_init (transport);

  /* case insensitive */
  down = g_ascii_strdown (str, -1);

  split = g_strsplit (down, ";", 0);

  /* First field contains the transport/profile/lower_transport */
  i = 0;
  if (split[0]) {
    for (i = 0; transports[i].name; i++)
      if (strstr (split[0], transports[i].name))
        break;
    transport->trans = transports[i].mode;
    for (i = 0; profiles[i].name; i++)
      if (strstr (split[0], profiles[i].name))
        break;
    transport->profile = profiles[i].profile;
    for (i = 0; ltrans[i].name; i++)
      if (strstr (split[0], ltrans[i].name))
        break;
    transport->lower_transport = ltrans[i].ltrans;
    i = 1;
  }
  while (split[i]) {
    if (g_str_has_prefix (split[i], "multicast")) {
      transport->lower_transport = RTSP_LOWER_TRANS_UDP_MCAST;
    } else if (g_str_has_prefix (split[i], "unicast")) {
      if (transport->lower_transport == RTSP_LOWER_TRANS_UDP_MCAST)
        transport->lower_transport = RTSP_LOWER_TRANS_UDP;
    } else if (g_str_has_prefix (split[i], "destination=")) {
      transport->destination = g_strdup (split[i] + 12);
    } else if (g_str_has_prefix (split[i], "source=")) {
      transport->source = g_strdup (split[i] + 7);
    } else if (g_str_has_prefix (split[i], "layers=")) {
      transport->layers = atoi (split[i] + 7);
    } else if (g_str_has_prefix (split[i], "mode=")) {
      parse_mode (transport, split[i] + 5);
    } else if (g_str_has_prefix (split[i], "append")) {
      transport->append = TRUE;
    } else if (g_str_has_prefix (split[i], "interleaved=")) {
      parse_range (transport, split[i] + 12, &transport->interleaved);
    } else if (g_str_has_prefix (split[i], "ttl=")) {
      transport->ttl = atoi (split[i] + 4);
    } else if (g_str_has_prefix (split[i], "port=")) {
      parse_range (transport, split[i] + 5, &transport->port);
    } else if (g_str_has_prefix (split[i], "client_port=")) {
      parse_range (transport, split[i] + 12, &transport->client_port);
    } else if (g_str_has_prefix (split[i], "server_port=")) {
      parse_range (transport, split[i] + 12, &transport->server_port);
    } else if (g_str_has_prefix (split[i], "ssrc=")) {
      transport->ssrc = g_strdup (split[i] + 5);
    } else {
      /* unknown field... */
      g_warning ("unknown transport field \"%s\"", split[i]);
    }
    i++;
  }
  g_strfreev (split);
  g_free (down);

  return RTSP_OK;
}

RTSPResult
rtsp_transport_free (RTSPTransport * transport)
{
  g_return_val_if_fail (transport != NULL, RTSP_EINVAL);

  rtsp_transport_init (transport);
  g_free (transport);

  return RTSP_OK;
}
