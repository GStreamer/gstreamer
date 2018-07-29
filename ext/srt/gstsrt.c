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

static GSocketAddress *
gst_srt_socket_address_new (GstElement * elem, const gchar * host, guint16 port)
{
  GInetAddress *iaddr = NULL;
  GSocketAddress *addr = NULL;
  GError *error = NULL;

  if (host == NULL) {
    iaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  } else {
    iaddr = g_inet_address_new_from_string (host);
  }

  if (!iaddr) {
    GList *results;
    GResolver *resolver = g_resolver_get_default ();

    results = g_resolver_lookup_by_name (resolver, host, NULL, &error);

    if (!results) {
      GST_ERROR_OBJECT (elem, "Failed to resolve %s: %s", host, error->message);
      g_object_unref (resolver);
      goto failed;
    }

    iaddr = G_INET_ADDRESS (g_object_ref (results->data));

    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *ip = g_inet_address_to_string (iaddr);

    GST_DEBUG_OBJECT (elem, "IP address for host %s is %s", host, ip);
    g_free (ip);
  }
#endif

  addr = g_inet_socket_address_new (iaddr, port);
  g_object_unref (iaddr);

  return addr;

failed:
  g_clear_error (&error);

  return NULL;
}

SRTSOCKET
gst_srt_client_connect (GstElement * elem, int sender,
    const gchar * host, guint16 port, int rendez_vous,
    const gchar * bind_address, guint16 bind_port, int latency,
    GSocketAddress ** socket_address, gint * poll_id, const gchar * passphrase,
    int key_length)
{
  SRTSOCKET sock = SRT_INVALID_SOCK;
  GError *error = NULL;
  gpointer sa;
  size_t sa_len;
  int poll_event = SRT_EPOLL_ERR;

  poll_event |= sender ? SRT_EPOLL_OUT : SRT_EPOLL_IN;

  if (host == NULL) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid host"),
        ("Unspecified NULL host"));
    goto failed;
  }

  *socket_address = gst_srt_socket_address_new (elem, host, port);

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

  srt_epoll_add_usock (*poll_id, sock, &poll_event);

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
gst_srt_server_listen (GstElement * elem, int sender, const gchar * host,
    guint16 port, int latency, gint * poll_id, const gchar * passphrase,
    int key_length)
{
  SRTSOCKET sock = SRT_INVALID_SOCK;
  GError *error = NULL;
  struct sockaddr sa;
  size_t sa_len;
  GSocketAddress *addr = NULL;

  addr = gst_srt_socket_address_new (elem, host, port);

  if (addr == NULL) {
    GST_WARNING_OBJECT (elem,
        "failed to extract host or port from the given URI");
    goto failed;
  }

  sa_len = g_socket_address_get_native_size (addr);
  if (!g_socket_address_to_native (addr, &sa, sa_len, &error)) {
    GST_ELEMENT_ERROR (elem, RESOURCE, OPEN_READ, ("Invalid address"),
        ("cannot resolve address (reason: %s)", error->message));
    goto failed;
  }

  sock = srt_socket (sa.sa_family, SOCK_DGRAM, 0);
  if (sock == SRT_INVALID_SOCK) {
    GST_WARNING_OBJECT (elem, "failed to create SRT socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  /* Make SRT server socket non-blocking */
  /* for non-blocking srt_close() */
  srt_setsockopt (sock, 0, SRTO_SNDSYN, &(int) {
      0}, sizeof (int));

  /* for non-blocking srt_accept() */
  srt_setsockopt (sock, 0, SRTO_RCVSYN, &(int) {
      0}, sizeof (int));

  /* Make sure TSBPD mode is enable (SRT mode) */
  srt_setsockopt (sock, 0, SRTO_TSBPDMODE, &(int) {
      1}, sizeof (int));

  srt_setsockopt (sock, 0, SRTO_SENDER, &sender, sizeof (int));
  srt_setsockopt (sock, 0, SRTO_TSBPDDELAY, &latency, sizeof (int));

  if (passphrase != NULL && passphrase[0] != '\0') {
    srt_setsockopt (sock, 0, SRTO_PASSPHRASE, passphrase, strlen (passphrase));
    srt_setsockopt (sock, 0, SRTO_PBKEYLEN, &key_length, sizeof (int));
  }

  *poll_id = srt_epoll_create ();
  if (*poll_id == -1) {
    GST_ELEMENT_ERROR (elem, LIBRARY, INIT, (NULL),
        ("failed to create poll id for SRT socket (reason: %s)",
            srt_getlasterror_str ()));
    goto failed;
  }

  srt_epoll_add_usock (*poll_id, sock, &(int) {
      SRT_EPOLL_IN | SRT_EPOLL_ERR});

  if (srt_bind (sock, &sa, sa_len) == SRT_ERROR) {
    GST_WARNING_OBJECT (elem, "failed to bind SRT server socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  if (srt_listen (sock, 1) == SRT_ERROR) {
    GST_WARNING_OBJECT (elem, "failed to listen SRT socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  g_clear_object (&addr);

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
  g_clear_object (&addr);

  return SRT_INVALID_SOCK;
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
