/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2017 Robert Rosengren <robertr@axis.com>
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
/**
 * SECTION:gstnetutils
 * @title: GstNetUtils
 * @short_description: Network utility functions.
 *
 * GstNetUtils gathers network utility functions, enabling use for all
 * gstreamer plugins.
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnetutils.h"
#include <gst/gstinfo.h>
#include <errno.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef G_OS_WIN32
#include <netinet/in.h>
#endif

/**
 * gst_net_utils_set_socket_tos:
 * @socket: Socket to configure
 * @qos_dscp: QoS DSCP value
 *
 * Configures IP_TOS value of socket, i.e. sets QoS DSCP.
 *
 * Returns: TRUE if successful, FALSE in case an error occurred.
 *
 * Since: 1.18
 */
gboolean
gst_net_utils_set_socket_tos (GSocket * socket, gint qos_dscp)
{
  gboolean ret = FALSE;

#ifdef IP_TOS
  gint tos, fd;
  fd = g_socket_get_fd (socket);

  /* Extract and shift 6 bits of DSFIELD */
  tos = (qos_dscp & 0x3f) << 2;

  if (setsockopt (fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0) {
    GST_ERROR ("could not set TOS: %s", g_strerror (errno));
  } else {
    ret = TRUE;
  }
#ifdef IPV6_TCLASS
  if (g_socket_get_family (socket) == G_SOCKET_FAMILY_IPV6) {
    if (setsockopt (fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof (tos)) < 0) {
      GST_ERROR ("could not set TCLASS: %s", g_strerror (errno));
    } else {
      ret = TRUE;
    }
  }
#endif
#endif

  return ret;
}
