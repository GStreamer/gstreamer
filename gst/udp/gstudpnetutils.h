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
#include <winsock2.h>
#include <ws2tcpip.h>

/* Needed for GstObject and GST_WARNING_OBJECT */
#include <gst/gstobject.h>
#include <gst/gstinfo.h>

#else
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
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
#define inet_aton(c,addr) gst_udp_net_utils_win32_inet_aton ((c),(addr))
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

int      gst_udp_net_utils_win32_inet_aton (const char *c, struct in_addr * addr);
gboolean gst_udp_net_utils_win32_wsa_startup (GstObject * obj);

#endif

#endif /* __GST_UDP_NET_UTILS_H__*/

