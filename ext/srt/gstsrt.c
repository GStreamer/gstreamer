/* GStreamer
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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

#include "gstsrt.h"

#include "gstsrtclientsrc.h"
#include "gstsrtserversrc.h"
#include "gstsrtclientsink.h"
#include "gstsrtserversink.h"

#define GST_CAT_DEFAULT gst_debug_srt
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

SRTSOCKET
gst_srt_client_connect_full (GstElement * elem, int sender,
    const gchar * host, guint16 port, int rendez_vous,
    const gchar * bind_address, guint16 bind_port, int latency,
    GSocketAddress ** socket_address, gint * poll_id, gchar * passphrase,
    int key_length)
{
  SRTSOCKET sock = SRT_INVALID_SOCK;
  GError *error = NULL;
  gpointer sa;
  size_t sa_len;

  if (host == NULL) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid host"),
        ("Unspecified NULL host"));
    goto failed;
  }

  *socket_address = g_inet_socket_address_new_from_string (host, port);

  if (*socket_address == NULL) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid host"),
        ("Failed to parse host"));
    goto failed;
  }

  sa_len = g_socket_address_get_native_size (*socket_address);
  sa = g_alloca (sa_len);
  if (!g_socket_address_to_native (*socket_address, sa, sa_len, &error)) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid address"),
        ("cannot resolve address (reason: %s)", error->message));
    goto failed;
  }

  sock = srt_socket (g_socket_address_get_family (*socket_address), SOCK_DGRAM,
      0);
  if (sock == SRT_ERROR) {
    GST_ELEMENT_ERROR (elem, LIBRARY, INIT, (NULL),
        ("failed to create SRT socket (reason: %s)", srt_getlasterror_str ()));
    goto failed;
  }

  /* Make sure TSBPD mode is enable (SRT mode) */
  srt_setsockopt (sock, 0, SRTO_TSBPDMODE, &(int) {
      1}, sizeof (int));

  /* This is a sink, we're always a receiver */
  srt_setsockopt (sock, 0, SRTO_SENDER, &sender, sizeof (int));

  srt_setsockopt (sock, 0, SRTO_TSBPDDELAY, &latency, sizeof (int));

  srt_setsockopt (sock, 0, SRTO_RENDEZVOUS, &rendez_vous, sizeof (int));

  if (passphrase != NULL && passphrase[0] != '\0') {
    srt_setsockopt (sock, 0, SRTO_PASSPHRASE, passphrase, strlen (passphrase));
    srt_setsockopt (sock, 0, SRTO_PBKEYLEN, &key_length, sizeof (int));
  }

  if (bind_address || bind_port || rendez_vous) {
    gpointer bsa;
    size_t bsa_len;
    GSocketAddress *b_socket_address = NULL;

    if (bind_address == NULL)
      bind_address = "0.0.0.0";

    if (rendez_vous)
      bind_port = port;

    b_socket_address = g_inet_socket_address_new_from_string (bind_address,
        bind_port);

    if (b_socket_address == NULL) {
      GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid bind address"),
          ("Failed to parse bind address: %s:%d", bind_address, bind_port));
      goto failed;
    }

    bsa_len = g_socket_address_get_native_size (b_socket_address);
    bsa = g_alloca (bsa_len);
    if (!g_socket_address_to_native (b_socket_address, bsa, bsa_len, &error)) {
      GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid bind address"),
          ("Can't parse bind address to sockaddr: %s", error->message));
      g_clear_object (&b_socket_address);
      goto failed;
    }
    g_clear_object (&b_socket_address);

    if (srt_bind (sock, bsa, bsa_len) == SRT_ERROR) {
      GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ,
          ("Can't bind to address"),
          ("Can't bind to %s:%d (reason: %s)", bind_address, bind_port,
              srt_getlasterror_str ()));
      goto failed;
    }
  }

  *poll_id = srt_epoll_create ();
  if (*poll_id == -1) {
    GST_ELEMENT_ERROR (elem, LIBRARY, INIT, (NULL),
        ("failed to create poll id for SRT socket (reason: %s)",
            srt_getlasterror_str ()));
    goto failed;
  }

  srt_epoll_add_usock (*poll_id, sock, &(int) {
      SRT_EPOLL_OUT});

  if (srt_connect (sock, sa, sa_len) == SRT_ERROR) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Connection error"),
        ("failed to connect to host (reason: %s)", srt_getlasterror_str ()));
    goto failed;
  }

  return sock;

failed:
  if (*poll_id != SRT_ERROR) {
    srt_epoll_release (*poll_id);
    *poll_id = SRT_ERROR;
  }

  if (sock != SRT_INVALID_SOCK) {
    srt_close (sock);
    sock = SRT_INVALID_SOCK;
  }

  g_clear_error (&error);
  g_clear_object (socket_address);

  return SRT_INVALID_SOCK;
}

SRTSOCKET
gst_srt_client_connect (GstElement * elem, int sender,
    const gchar * host, guint16 port, int rendez_vous,
    const gchar * bind_address, guint16 bind_port, int latency,
    GSocketAddress ** socket_address, gint * poll_id)
{
  return gst_srt_client_connect_full (elem, sender, host, port,
      rendez_vous, bind_address, bind_port, latency, socket_address, poll_id,
      NULL, 0);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srt", 0, "SRT Common code");

  if (!gst_element_register (plugin, "srtclientsrc", GST_RANK_PRIMARY,
          GST_TYPE_SRT_CLIENT_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "srtserversrc", GST_RANK_PRIMARY,
          GST_TYPE_SRT_SERVER_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "srtclientsink", GST_RANK_PRIMARY,
          GST_TYPE_SRT_CLIENT_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "srtserversink", GST_RANK_PRIMARY,
          GST_TYPE_SRT_SERVER_SINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    srt,
    "transfer data via SRT",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
