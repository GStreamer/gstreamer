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

#include <string.h>
#include <stdlib.h>

#include "rtsptransport.h"

RTSPResult
rtsp_transport_new (RTSPTransport ** transport)
{
  RTSPTransport *trans;

  if (transport == NULL)
    return RTSP_EINVAL;

  trans = g_new0 (RTSPTransport, 1);

  *transport = trans;

  return rtsp_transport_init (trans);
}

RTSPResult
rtsp_transport_init (RTSPTransport * transport)
{
  g_free (transport->destination);
  g_free (transport->source);
  g_free (transport->ssrc);

  memset (transport, 0, sizeof (RTSPTransport));

  transport->trans = RTSP_TRANS_RTP;
  transport->profile = RTSP_PROFILE_AVP;
  transport->lower_transport = RTSP_LOWER_TRANS_UNKNOWN;
  transport->mode_play = TRUE;
  transport->mode_record = FALSE;

  return RTSP_OK;
}

static void
parse_mode (RTSPTransport * transport, gchar * str)
{
  transport->mode_play = (strstr (str, "\"PLAY\"") != NULL);
  transport->mode_record = (strstr (str, "\"RECORD\"") != NULL);
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
rtsp_transport_parse (gchar * str, RTSPTransport * transport)
{
  gchar **split, *down;
  gint i;

  if (str == NULL || transport == NULL)
    return RTSP_EINVAL;

  rtsp_transport_init (transport);

  /* case insensitive */
  down = g_ascii_strdown (str, -1);

  split = g_strsplit (down, ";", 0);
  i = 0;
  while (split[i]) {
    if (g_str_has_prefix (split[i], "rtp/avp/udp")) {
      transport->lower_transport = RTSP_LOWER_TRANS_UDP;
    } else if (g_str_has_prefix (split[i], "rtp/avp/tcp")) {
      transport->lower_transport = RTSP_LOWER_TRANS_TCP;
    } else if (g_str_has_prefix (split[i], "rtp/avp")) {
      transport->lower_transport = RTSP_LOWER_TRANS_UDP;
    } else if (g_str_has_prefix (split[i], "multicast")) {
      transport->multicast = TRUE;
    } else if (g_str_has_prefix (split[i], "unicast")) {
      transport->multicast = FALSE;
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
  rtsp_transport_init (transport);
  g_free (transport);
  return RTSP_OK;
}
