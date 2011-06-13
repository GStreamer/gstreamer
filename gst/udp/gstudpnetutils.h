/* GStreamer UDP network utility functions
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Joni Valtanen <joni.valtanen@movial.fi>
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

#ifndef __GST_UDP_NET_UTILS_H__
#define __GST_UDP_NET_UTILS_H__

#include <sys/types.h>

/* Needed for G_OS_XXXX */
#include <glib.h>

#ifdef G_OS_WIN32
/* ws2_32.dll has getaddrinfo and freeaddrinfo on Windows XP and later.
 * minwg32 headers check WINVER before allowing the use of these */
#ifndef WINVER
#define WINVER 0x0501
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef socklen_t
#define socklen_t int
#endif

/* Needed for GstObject and GST_WARNING_OBJECT */
#include <gst/gstobject.h>
#include <gst/gstinfo.h>

#else
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include <fcntl.h>

#ifdef G_OS_WIN32

#define IOCTL_SOCKET ioctlsocket
#define CLOSE_SOCKET(sock) closesocket(sock)
#define setsockopt(sock,l,opt,val,len) setsockopt(sock,l,opt,(char *)(val),len)
#define WSA_STARTUP(obj) gst_udp_net_utils_win32_wsa_startup(GST_OBJECT(obj))
#define WSA_CLEANUP(obj) WSACleanup ()

#else

#define IOCTL_SOCKET ioctl
#define CLOSE_SOCKET(sock) close(sock)
#define setsockopt(sock,l,opt,val,len) setsockopt(sock,l,opt,(void *)(val),len)
#define WSA_STARTUP(obj)
#define WSA_CLEANUP(obj)

#endif

#ifdef G_OS_WIN32

gboolean gst_udp_net_utils_win32_wsa_startup (GstObject * obj);

#endif

typedef struct {
  gchar    *host;
  gint      port;
  gboolean  is_ipv6;
} GstUDPUri;

int     gst_udp_get_sockaddr_length  (struct sockaddr_storage *addr);

int     gst_udp_get_addr             (const char *hostname, int port, struct sockaddr_storage *addr);
int     gst_udp_is_multicast         (struct sockaddr_storage *addr);

int     gst_udp_set_loop             (int sockfd, guint16 ss_family, gboolean loop);
int     gst_udp_set_ttl              (int sockfd, guint16 ss_family, int ttl, gboolean is_multicast);

/* multicast groups */
int     gst_udp_join_group           (int sockfd, struct sockaddr_storage *addr,
                                      gchar *iface);
int     gst_udp_leave_group          (int sockfd, struct sockaddr_storage *addr);

/* uri handling */
void    gst_udp_uri_init             (GstUDPUri *uri, const gchar *host, gint port);
int     gst_udp_uri_update           (GstUDPUri *uri, const gchar *host, gint port);
int     gst_udp_parse_uri            (const gchar *uristr, GstUDPUri *uri);
gchar * gst_udp_uri_string           (GstUDPUri *uri);
void    gst_udp_uri_free             (GstUDPUri *uri);

#endif /* __GST_UDP_NET_UTILS_H__*/

