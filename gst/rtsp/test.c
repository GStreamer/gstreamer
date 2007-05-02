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

#include <stdio.h>

#include "sdp.h"
#include "rtsp.h"

int
main (int argc, gchar * argv[])
{
  RTSPUrl *url;
  RTSPConnection *conn;
  RTSPResult res;
  RTSPMessage request = { 0 };
  gchar *urlstr;
  RTSPMessage response = { 0 };
  SDPMessage sdp = { 0 };

  urlstr = "rtsp://thread:5454/south-rtsp.mp3";

  /* create url */
  g_print ("parsing url \"%s\"...\n", urlstr);
  res = rtsp_url_parse (urlstr, &url);
  if (res != RTSP_OK) {
    g_print ("error parsing url \"%s\"\n", urlstr);
    return (-1);
  }

  g_print ("  url host: %s\n", url->host);
  g_print ("  url port: %d\n", url->port);
  g_print ("  url path: %s\n", url->abspath);

  /* create and open connection */
  g_print ("creating connection...\n");
  res = rtsp_connection_create (url, &conn);
  if (res != RTSP_OK) {
    g_print ("error creating connection to \"%s\"\n", urlstr);
    return (-1);
  }

  /* open connection */
  g_print ("opening connection...\n");
  res = rtsp_connection_connect (conn, NULL);
  if (res != RTSP_OK) {
    g_print ("error opening connection to \"%s\"\n", urlstr);
    return (-1);
  }

  /* do describe */
  {
    res = rtsp_message_init_request (&request, RTSP_DESCRIBE, urlstr);
    if (res != RTSP_OK) {
      g_print ("error creating request\n");
      return (-1);
    }
    rtsp_message_add_header (&request, RTSP_HDR_ACCEPT, "application/sdp");

    rtsp_message_dump (&request);

    res = rtsp_connection_send (conn, &request, NULL);
    if (res != RTSP_OK) {
      g_print ("error sending request\n");
      return (-1);
    }

    res = rtsp_connection_receive (conn, &response, NULL);
    if (res != RTSP_OK) {
      g_print ("error receiving response\n");
      return (-1);
    }
    rtsp_message_dump (&response);
  }

  /* parse SDP */
  {
    guint8 *data;
    guint size;

    rtsp_message_get_body (&response, &data, &size);

    sdp_message_init (&sdp);
    sdp_message_parse_buffer (data, size, &sdp);

    sdp_message_dump (&sdp);
  }

  /* do setup */
  {
    gint i;

    for (i = 0; i < sdp_message_medias_len (&sdp); i++) {
      SDPMedia *media;
      gchar *setup_url;
      gchar *control_url;

      media = sdp_message_get_media (&sdp, i);

      g_print ("setup media %d\n", i);
      control_url = sdp_media_get_attribute_val (media, "control");

      setup_url = g_strdup_printf ("%s/%s", urlstr, control_url);

      g_print ("setup %s\n", setup_url);
      res = rtsp_message_init_request (&request, RTSP_SETUP, setup_url);
      if (res != RTSP_OK) {
        g_print ("error creating request\n");
        return (-1);
      }

      rtsp_message_add_header (&request, RTSP_HDR_TRANSPORT,
          //"RTP/AVP/UDP;unicast;client_port=5000-5001,RTP/AVP/UDP;multicast,RTP/AVP/TCP");
          "RTP/AVP/TCP");
      rtsp_message_dump (&request);

      res = rtsp_connection_send (conn, &request, NULL);
      if (res != RTSP_OK) {
        g_print ("error sending request\n");
        return (-1);
      }

      res = rtsp_connection_receive (conn, &response, NULL);
      if (res != RTSP_OK) {
        g_print ("error receiving response\n");
        return (-1);
      }
      rtsp_message_dump (&response);
    }
  }
  /* do play */
  {
    res = rtsp_message_init_request (&request, RTSP_PLAY, urlstr);
    if (res != RTSP_OK) {
      g_print ("error creating request\n");
      return (-1);
    }
    rtsp_message_dump (&request);

    res = rtsp_connection_send (conn, &request, NULL);
    if (res != RTSP_OK) {
      g_print ("error sending request\n");
      return (-1);
    }

    res = rtsp_connection_receive (conn, &response, NULL);
    if (res != RTSP_OK) {
      g_print ("error receiving response\n");
      return (-1);
    }
    rtsp_message_dump (&response);
  }

  while (TRUE) {
    res = rtsp_connection_receive (conn, &response, NULL);
    if (res != RTSP_OK) {
      g_print ("error receiving response\n");
      return (-1);
    }
    rtsp_message_dump (&response);
  }

  /* close connection */
  g_print ("closing connection...\n");
  res = rtsp_connection_close (conn);
  if (res != RTSP_OK) {
    g_print ("error closing connection to \"%s\"\n", urlstr);
    return (-1);
  }

  return 0;
}
