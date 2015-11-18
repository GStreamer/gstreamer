/* GStreamer
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
 *
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

/* Helper process that runs setuid root or with appropriate privileges to
 * listen on ports < 1024, do multicast operations and get MAC addresses of
 * interfaces. Privileges are dropped after these operations are done.
 *
 * It listens on the PTP multicast group on port 319 and 320 and forwards
 * everything received there to stdout, while forwarding everything received
 * on stdout to those sockets.
 * Additionally it provides the MAC address of a network interface via stdout
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

#ifdef HAVE_GETIFADDRS_AF_LINK
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif

#ifdef HAVE_PTP_HELPER_SETUID
#include <grp.h>
#include <pwd.h>
#endif

#ifdef HAVE_PTP_HELPER_CAPABILITIES
#include <sys/capability.h>
#endif

#include <glib.h>
#include <gio/gio.h>

#include <gst/gst.h>
#include <gst/net/gstptp_private.h>

#define PTP_MULTICAST_GROUP "224.0.1.129"
#define PTP_EVENT_PORT   319
#define PTP_GENERAL_PORT 320

static gchar **ifaces = NULL;
static gboolean verbose = FALSE;
static guint64 clock_id = (guint64) - 1;
static guint8 clock_id_array[8];

static GOptionEntry opt_entries[] = {
  {"interface", 'i', 0, G_OPTION_ARG_STRING_ARRAY, &ifaces,
      "Interface to listen on", NULL},
  {"clock-id", 'c', 0, G_OPTION_ARG_INT64, &clock_id,
      "PTP clock id", NULL},
  {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
      "Be verbose", NULL},
  {NULL}
};

static GSocketAddress *event_saddr, *general_saddr;
static GSocket *socket_event, *socket_general;
static GIOChannel *stdin_channel, *stdout_channel;

static gboolean
have_socket_data_cb (GSocket * socket, GIOCondition condition,
    gpointer user_data)
{
  gchar buffer[8192];
  gssize read;
  gsize written;
  GError *err = NULL;
  GIOStatus status;
  StdIOHeader header = { 0, };

  read = g_socket_receive (socket, buffer, sizeof (buffer), NULL, &err);
  if (read == -1)
    g_error ("Failed to read from socket: %s", err->message);
  g_clear_error (&err);

  if (verbose)
    g_message ("Received %" G_GSSIZE_FORMAT " bytes from %s socket", read,
        (socket == socket_event ? "event" : "general"));

  header.size = read;
  header.type = (socket == socket_event) ? TYPE_EVENT : TYPE_GENERAL;

  status =
      g_io_channel_write_chars (stdout_channel, (gchar *) & header,
      sizeof (header), &written, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to write to stdout: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdout");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdout write status: %d", status);
  } else if (written != sizeof (header)) {
    g_error ("Unexpected write size: %" G_GSIZE_FORMAT, written);
  }

  status =
      g_io_channel_write_chars (stdout_channel, buffer, read, &written, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to write to stdout: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdout");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdout write status: %d", status);
  } else if (written != read) {
    g_error ("Unexpected write size: %" G_GSIZE_FORMAT, written);
  }

  return G_SOURCE_CONTINUE;
}

static gboolean
have_stdin_data_cb (GIOChannel * channel, GIOCondition condition,
    gpointer user_data)
{
  GIOStatus status;
  StdIOHeader header = { 0, };
  gchar buffer[8192];
  GError *err = NULL;
  gsize read;
  gssize written;

  if ((condition & G_IO_STATUS_EOF)) {
    g_message ("EOF on stdin");
    exit (0);
  }

  status =
      g_io_channel_read_chars (channel, (gchar *) & header, sizeof (header),
      &read, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to read from stdin: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdin");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdin read status: %d", status);
  } else if (read != sizeof (header)) {
    g_error ("Unexpected read size: %" G_GSIZE_FORMAT, read);
  } else if (header.size > 8192) {
    g_error ("Unexpected size: %u", header.size);
  }

  status = g_io_channel_read_chars (channel, buffer, header.size, &read, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to read from stdin: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdin");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdin read status: %d", status);
  } else if (read != header.size) {
    g_error ("Unexpected read size: %" G_GSIZE_FORMAT, read);
  }

  switch (header.type) {
    case TYPE_EVENT:
    case TYPE_GENERAL:
      written =
          g_socket_send_to (header.type ==
          TYPE_EVENT ? socket_event : socket_general,
          (header.type == TYPE_EVENT ? event_saddr : general_saddr), buffer,
          header.size, NULL, &err);
      if (written == -1)
        g_error ("Failed to write to socket: %s", err->message);
      else if (written != header.size)
        g_error ("Unexpected write size: %" G_GSSIZE_FORMAT, written);
      g_clear_error (&err);
      if (verbose)
        g_message ("Sent %" G_GSSIZE_FORMAT " bytes to %s socket", read,
            (header.type == TYPE_EVENT ? "event" : "general"));
      break;
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static void
setup_sockets (void)
{
  GInetAddress *bind_addr, *mcast_addr;
  GSocketAddress *bind_saddr;
  GSource *socket_event_source, *socket_general_source;
  gchar **probed_ifaces = NULL;
  GError *err = NULL;

  /* Create sockets */
  socket_event =
      g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, &err);
  if (!socket_event)
    g_error ("Couldn't create event socket: %s", err->message);
  g_clear_error (&err);
  g_socket_set_multicast_loopback (socket_event, FALSE);

  socket_general =
      g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, &err);
  if (!socket_general)
    g_error ("Couldn't create general socket: %s", err->message);
  g_clear_error (&err);
  g_socket_set_multicast_loopback (socket_general, FALSE);

  /* Bind sockets */
  bind_addr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  bind_saddr = g_inet_socket_address_new (bind_addr, PTP_EVENT_PORT);
  if (!g_socket_bind (socket_event, bind_saddr, TRUE, &err))
    g_error ("Couldn't bind event socket: %s", err->message);
  g_object_unref (bind_saddr);
  bind_saddr = g_inet_socket_address_new (bind_addr, PTP_GENERAL_PORT);
  if (!g_socket_bind (socket_general, bind_saddr, TRUE, &err))
    g_error ("Couldn't bind general socket: %s", err->message);
  g_object_unref (bind_saddr);
  g_object_unref (bind_addr);

  /* Probe all non-loopback interfaces */
  if (!ifaces) {
#if defined(HAVE_SIOCGIFCONF_SIOCGIFFLAGS_SIOCGIFHWADDR)
    struct ifreq ifr;
    struct ifconf ifc;
    gchar buf[8192];

    ifc.ifc_len = sizeof (buf);
    ifc.ifc_buf = buf;
    if (ioctl (g_socket_get_fd (socket_event), SIOCGIFCONF, &ifc) != -1) {
      guint i, idx = 0;

      probed_ifaces = g_new0 (gchar *, ifc.ifc_len + 1);

      for (i = 0; i < ifc.ifc_len / sizeof (struct ifreq); i++) {
        strncpy (ifr.ifr_name, ifc.ifc_req[i].ifr_name, IFNAMSIZ);
        if (ioctl (g_socket_get_fd (socket_event), SIOCGIFFLAGS, &ifr) == 0) {
          if ((ifr.ifr_flags & IFF_LOOPBACK))
            continue;
          probed_ifaces[idx] = g_strndup (ifc.ifc_req[i].ifr_name, IFNAMSIZ);
          idx++;
        } else {
          g_warning ("can't get flags of interface '%s'",
              ifc.ifc_req[i].ifr_name);
          probed_ifaces[idx] = g_strndup (ifc.ifc_req[i].ifr_name, IFNAMSIZ);
          idx++;
        }
        if (idx != 0)
          ifaces = probed_ifaces;
      }
    }
#elif defined(HAVE_GETIFADDRS_AF_LINK)
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs (&ifaddr) != -1) {
      GPtrArray *arr;

      arr = g_ptr_array_new ();

      for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if ((ifa->ifa_flags & IFF_LOOPBACK))
          continue;

        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK)
          continue;

        g_ptr_array_add (arr, g_strdup (ifa->ifa_name));
      }
      freeifaddrs (ifaddr);

      g_ptr_array_add (arr, NULL);
      ifaces = probed_ifaces = (gchar **) g_ptr_array_free (arr, FALSE);
    }
#else
#warning "Implement something to list all network interfaces"
#endif
  }

  /* Get a clock id from the MAC address if none was given */
  if (clock_id == (guint64) - 1) {
    gboolean success = FALSE;

#if defined(HAVE_SIOCGIFCONF_SIOCGIFFLAGS_SIOCGIFHWADDR)
    struct ifreq ifr;

    if (ifaces) {
      gchar **ptr = ifaces;

      while (*ptr) {
        strncpy (ifr.ifr_name, *ptr, IFNAMSIZ);
        if (ioctl (g_socket_get_fd (socket_event), SIOCGIFHWADDR, &ifr) == 0) {
          clock_id_array[0] = ifr.ifr_hwaddr.sa_data[0];
          clock_id_array[1] = ifr.ifr_hwaddr.sa_data[1];
          clock_id_array[2] = ifr.ifr_hwaddr.sa_data[2];
          clock_id_array[3] = 0xff;
          clock_id_array[4] = 0xfe;
          clock_id_array[5] = ifr.ifr_hwaddr.sa_data[3];
          clock_id_array[6] = ifr.ifr_hwaddr.sa_data[4];
          clock_id_array[7] = ifr.ifr_hwaddr.sa_data[5];
          success = TRUE;
          break;
        }
      }

      ptr++;
    } else {
      struct ifconf ifc;
      gchar buf[8192];

      ifc.ifc_len = sizeof (buf);
      ifc.ifc_buf = buf;
      if (ioctl (g_socket_get_fd (socket_event), SIOCGIFCONF, &ifc) != -1) {
        guint i;

        for (i = 0; i < ifc.ifc_len / sizeof (struct ifreq); i++) {
          strncpy (ifr.ifr_name, ifc.ifc_req[i].ifr_name, IFNAMSIZ);
          if (ioctl (g_socket_get_fd (socket_event), SIOCGIFFLAGS, &ifr) == 0) {
            if ((ifr.ifr_flags & IFF_LOOPBACK))
              continue;

            if (ioctl (g_socket_get_fd (socket_event), SIOCGIFHWADDR,
                    &ifr) == 0) {
              clock_id_array[0] = ifr.ifr_hwaddr.sa_data[0];
              clock_id_array[1] = ifr.ifr_hwaddr.sa_data[1];
              clock_id_array[2] = ifr.ifr_hwaddr.sa_data[2];
              clock_id_array[3] = 0xff;
              clock_id_array[4] = 0xfe;
              clock_id_array[5] = ifr.ifr_hwaddr.sa_data[3];
              clock_id_array[6] = ifr.ifr_hwaddr.sa_data[4];
              clock_id_array[7] = ifr.ifr_hwaddr.sa_data[5];
              success = TRUE;
              break;
            }
          } else {
            g_warning ("can't get flags of interface '%s'",
                ifc.ifc_req[i].ifr_name);
          }
        }
      }
    }
#elif defined(HAVE_GETIFADDRS_AF_LINK)
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs (&ifaddr) != -1) {
      for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        struct sockaddr_dl *sdl = (struct sockaddr_dl *) ifa->ifa_addr;
        guint8 mac_addr[6];

        if ((ifa->ifa_flags & IFF_LOOPBACK))
          continue;

        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK)
          continue;

        if (ifaces) {
          gchar **p = ifaces;
          gboolean found = FALSE;

          while (*p) {
            if (strcmp (*p, ifa->ifa_name) == 0) {
              found = TRUE;
              break;
            }
            p++;
          }

          if (!found)
            continue;
        }

        if (sdl->sdl_alen != 6)
          continue;

        memcpy (mac_addr, LLADDR (sdl), sdl->sdl_alen);

        clock_id_array[0] = mac_addr[0];
        clock_id_array[1] = mac_addr[1];
        clock_id_array[2] = mac_addr[2];
        clock_id_array[3] = 0xff;
        clock_id_array[4] = 0xfe;
        clock_id_array[5] = mac_addr[3];
        clock_id_array[6] = mac_addr[4];
        clock_id_array[7] = mac_addr[5];
        success = TRUE;
        break;
      }

      freeifaddrs (ifaddr);
    }
#else
#warning "Implement something to get MAC addresses of network interfaces"
#endif

    if (!success) {
      g_warning ("can't get any MAC address, using random clock id");
      clock_id = (((guint64) g_random_int ()) << 32) | (g_random_int ());
      GST_WRITE_UINT64_BE (clock_id_array, clock_id);
      clock_id_array[3] = 0xff;
      clock_id_array[4] = 0xfe;
    }
  } else {
    GST_WRITE_UINT64_BE (clock_id_array, clock_id);
  }

  /* Join multicast groups */
  mcast_addr = g_inet_address_new_from_string (PTP_MULTICAST_GROUP);
  if (ifaces) {
    gchar **ptr = ifaces;
    gboolean success = FALSE;

    while (*ptr) {
      gint c = 0;
      if (!g_socket_join_multicast_group (socket_event, mcast_addr, FALSE, *ptr,
              &err)
          && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE))
        g_warning ("Couldn't join multicast group on interface '%s': %s", *ptr,
            err->message);
      else
        c++;
      g_clear_error (&err);

      if (!g_socket_join_multicast_group (socket_general, mcast_addr, FALSE,
              *ptr, &err)
          && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE))
        g_warning ("Couldn't join multicast group on interface '%s': %s", *ptr,
            err->message);
      else
        c++;
      g_clear_error (&err);

      if (c == 2)
        success = TRUE;
      ptr++;
    }

    if (!success) {
      /* Join multicast group without any interface */
      if (!g_socket_join_multicast_group (socket_event, mcast_addr, FALSE, NULL,
              &err))
        g_error ("Couldn't join multicast group: %s", err->message);
      g_clear_error (&err);
      if (!g_socket_join_multicast_group (socket_general, mcast_addr, FALSE,
              NULL, &err))
        g_error ("Couldn't join multicast group: %s", err->message);
      g_clear_error (&err);
    }
  } else {
    /* Join multicast group without any interface */
    if (!g_socket_join_multicast_group (socket_event, mcast_addr, FALSE, NULL,
            &err))
      g_error ("Couldn't join multicast group: %s", err->message);
    g_clear_error (&err);
    if (!g_socket_join_multicast_group (socket_general, mcast_addr, FALSE, NULL,
            &err))
      g_error ("Couldn't join multicast group: %s", err->message);
    g_clear_error (&err);
  }

  event_saddr = g_inet_socket_address_new (mcast_addr, PTP_EVENT_PORT);
  general_saddr = g_inet_socket_address_new (mcast_addr, PTP_GENERAL_PORT);

  /* Create socket sources */
  socket_event_source =
      g_socket_create_source (socket_event, G_IO_IN | G_IO_PRI, NULL);
  g_source_set_priority (socket_event_source, G_PRIORITY_HIGH);
  g_source_set_callback (socket_event_source, (GSourceFunc) have_socket_data_cb,
      NULL, NULL);
  g_source_attach (socket_event_source, NULL);
  socket_general_source =
      g_socket_create_source (socket_general, G_IO_IN | G_IO_PRI, NULL);
  g_source_set_priority (socket_general_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (socket_general_source,
      (GSourceFunc) have_socket_data_cb, NULL, NULL);
  g_source_attach (socket_general_source, NULL);

  g_strfreev (probed_ifaces);
}

static void
drop_privileges (void)
{
#ifdef HAVE_PTP_HELPER_SETUID
  /* Switch to the given user/group */
#ifdef HAVE_PTP_HELPER_SETUID_GROUP
  {
    struct group *grp;

    grp = getgrnam (HAVE_PTP_HELPER_SETUID_GROUP);
    if (!grp)
      g_error ("Failed to get group information '%s': %s",
          HAVE_PTP_HELPER_SETUID_GROUP, g_strerror (errno));

    if (setgid (grp->gr_gid) != 0)
      g_error ("Failed to change to group '%s': %s",
          HAVE_PTP_HELPER_SETUID_GROUP, g_strerror (errno));
  }
#endif

#ifdef HAVE_PTP_HELPER_SETUID_USER
  {
    struct passwd *pwd;

    pwd = getpwnam (HAVE_PTP_HELPER_SETUID_USER);
    if (!pwd)
      g_error ("Failed to get user information '%s': %s",
          HAVE_PTP_HELPER_SETUID_USER, g_strerror (errno));

#ifndef HAVE_PTP_HELPER_SETUID_GROUP
    if (setgid (pwd->pw_gid) != 0)
      g_error ("Failed to change to user group '%s': %s",
          HAVE_PTP_HELPER_SETUID_USER, g_strerror (errno));
#endif

    if (setuid (pwd->pw_uid) != 0)
      g_error ("Failed to change to user '%s': %s", HAVE_PTP_HELPER_SETUID_USER,
          g_strerror (errno));
  }
#endif
#endif
#ifdef HAVE_PTP_HELPER_CAPABILITIES
  /* Drop all capabilities */
  {
    cap_t caps;

    caps = cap_get_proc ();
    if (caps == 0)
      g_error ("Failed to get process caps: %s", g_strerror (errno));
    if (cap_clear (caps) != 0)
      g_error ("Failed to clear caps: %s", g_strerror (errno));
    if (cap_set_proc (caps) != 0)
      g_error ("Failed to set process caps: %s", g_strerror (errno));
  }
#endif
}

static void
setup_stdio_channels (void)
{
  GSource *stdin_source;

  /* Create stdin source */
  stdin_channel = g_io_channel_unix_new (STDIN_FILENO);
  if (g_io_channel_set_encoding (stdin_channel, NULL,
          NULL) == G_IO_STATUS_ERROR)
    g_error ("Failed to set stdin to binary encoding");
  g_io_channel_set_buffered (stdin_channel, FALSE);
  stdin_source =
      g_io_create_watch (stdin_channel, G_IO_IN | G_IO_PRI | G_IO_HUP);
  g_source_set_priority (stdin_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (stdin_source, (GSourceFunc) have_stdin_data_cb, NULL,
      NULL);
  g_source_attach (stdin_source, NULL);

  /* Create stdout channel */
  stdout_channel = g_io_channel_unix_new (STDOUT_FILENO);
  if (g_io_channel_set_encoding (stdout_channel, NULL,
          NULL) == G_IO_STATUS_ERROR)
    g_error ("Failed to set stdout to binary encoding");
  g_io_channel_set_buffered (stdout_channel, FALSE);
}

static void
write_clock_id (void)
{
  GError *err = NULL;
  GIOStatus status;
  StdIOHeader header = { 0, };
  gsize written;

  /* Write clock id to stdout */

  header.type = TYPE_CLOCK_ID;
  header.size = 8;
  status =
      g_io_channel_write_chars (stdout_channel, (gchar *) & header,
      sizeof (header), &written, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to write to stdout: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdout");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdout write status: %d", status);
  } else if (written != sizeof (header)) {
    g_error ("Unexpected write size: %" G_GSIZE_FORMAT, written);
  }

  status =
      g_io_channel_write_chars (stdout_channel,
      (const gchar *) clock_id_array, sizeof (clock_id_array), &written, &err);
  if (status == G_IO_STATUS_ERROR) {
    g_error ("Failed to write to stdout: %s", err->message);
    g_clear_error (&err);
  } else if (status == G_IO_STATUS_EOF) {
    g_message ("EOF on stdout");
    exit (0);
  } else if (status != G_IO_STATUS_NORMAL) {
    g_error ("Unexpected stdout write status: %d", status);
  } else if (written != sizeof (clock_id_array)) {
    g_error ("Unexpected write size: %" G_GSIZE_FORMAT, written);
  }
}

#ifdef __APPLE__
static gint
dummy_poll (GPollFD * fds, guint nfds, gint timeout)
{
  return g_poll (fds, nfds, timeout);
}
#endif

gint
main (gint argc, gchar ** argv)
{
  GOptionContext *opt_ctx;
  GMainLoop *loop;
  GError *err = NULL;

  /* FIXME: Work around some side effects of the changes from
   * https://bugzilla.gnome.org/show_bug.cgi?id=741054
   *
   * The modified poll function somehow calls setugid(), which
   * then abort()s the application. Make sure that we use g_poll()
   * here!
   */
#ifdef __APPLE__
  {
    GMainContext *context = g_main_context_default ();
    g_main_context_set_poll_func (context, dummy_poll);
  }
#endif

#ifdef HAVE_PTP_HELPER_SETUID
  if (setuid (0) < 0)
    g_error ("not running with superuser privileges");
#endif

  opt_ctx = g_option_context_new ("- GStreamer PTP helper process");
  g_option_context_add_main_entries (opt_ctx, opt_entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &err))
    g_error ("Error parsing options: %s", err->message);
  g_clear_error (&err);
  g_option_context_free (opt_ctx);

  setup_sockets ();
  drop_privileges ();
  setup_stdio_channels ();
  write_clock_id ();

  /* Get running */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  /* We never exit cleanly, so don't do cleanup */
  g_assert_not_reached ();

  return 0;
}
