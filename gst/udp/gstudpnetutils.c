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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <memory.h>

#include "gstudpnetutils.h"

#ifdef G_OS_WIN32

gboolean
gst_udp_net_utils_win32_wsa_startup (GstObject * obj)
{
  WSADATA w;
  int error;

  error = WSAStartup (0x0202, &w);

  if (error) {
    GST_WARNING_OBJECT (obj, "WSAStartup error: %d", error);
    return FALSE;
  }

  if (w.wVersion != 0x0202) {
    WSACleanup ();
    GST_WARNING_OBJECT (obj, "Winsock version wrong : 0x%x", w.wVersion);
    return FALSE;
  }

  return TRUE;
}

#endif

int
gst_udp_get_addr (const char *hostname, int port, struct sockaddr_storage *addr)
{
  struct addrinfo hints, *res, *nres;
  char service[NI_MAXSERV];
  int ret;

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  snprintf (service, sizeof (service) - 1, "%d", port);
  service[sizeof (service) - 1] = '\0';

  if ((ret = getaddrinfo (hostname, (port == -1) ? NULL : service, &hints,
              &res)) < 0) {
    return ret;
  }

  nres = res;
  while (nres) {
    if (nres->ai_family == AF_INET || nres->ai_family == AF_INET6)
      break;
    nres = nres->ai_next;
  }

  if (nres) {
    memcpy (addr, nres->ai_addr, nres->ai_addrlen);
  } else {
    errno = EAI_ADDRFAMILY;
    ret = -1;
  }
  freeaddrinfo (res);

  return ret;
}

int
gst_udp_set_loop_ttl (int sockfd, gboolean loop, int ttl)
{
  int ret = -1;

#if 0
  int l = (loop == FALSE) ? 0 : 1;

  switch (addr->ss_family) {
    case AF_INET:
    {
      if ((ret =
              setsockopt (sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, &l,
                  sizeof (l))) < 0)
        return ret;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                  sizeof (ttl))) < 0)
        return ret;
      break;
    }
    case AF_INET6:
    {
      if ((ret =
              setsockopt (sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &l,
                  sizeof (l))) < 0)
        return ret;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl,
                  sizeof (ttl))) < 0)
        return ret;

      break;
    }
    default:
      errno = EAFNOSUPPORT;
  }
#endif
  return ret;
}

int
gst_udp_join_group (int sockfd, struct sockaddr_storage *addr)
{
  int ret = -1;

  switch (addr->ss_family) {
    case AF_INET:
    {
      struct ip_mreq mreq4;

      mreq4.imr_multiaddr.s_addr =
          ((struct sockaddr_in *) addr)->sin_addr.s_addr;
      mreq4.imr_interface.s_addr = INADDR_ANY;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                  (const void *) &mreq4, sizeof (mreq4))) < 0)
        return ret;

      break;
    }
    case AF_INET6:
    {
      struct ipv6_mreq mreq6;

      memcpy (&mreq6.ipv6mr_multiaddr,
          &(((struct sockaddr_in6 *) addr)->sin6_addr),
          sizeof (struct in6_addr));
      mreq6.ipv6mr_interface = 0;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                  (const void *) &mreq6, sizeof (mreq6))) < 0)
        return ret;

      break;
    }
    default:
      errno = EAFNOSUPPORT;
  }
  return ret;
}

int
gst_udp_leave_group (int sockfd, struct sockaddr_storage *addr)
{
  int ret = -1;

  switch (addr->ss_family) {
    case AF_INET:
    {
      struct ip_mreq mreq4;

      mreq4.imr_multiaddr.s_addr =
          ((struct sockaddr_in *) addr)->sin_addr.s_addr;
      mreq4.imr_interface.s_addr = INADDR_ANY;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                  (const void *) &mreq4, sizeof (mreq4))) < 0)
        return ret;
    }
      break;

    case AF_INET6:
    {
      struct ipv6_mreq mreq6;

      memcpy (&mreq6.ipv6mr_multiaddr,
          &(((struct sockaddr_in6 *) addr)->sin6_addr),
          sizeof (struct in6_addr));
      mreq6.ipv6mr_interface = 0;

      if ((ret =
              setsockopt (sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
                  (const void *) &mreq6, sizeof (mreq6))) < 0)
        return ret;
    }
      break;

    default:
      errno = EAFNOSUPPORT;
  }

  return ret;
}

int
gst_udp_is_multicast (struct sockaddr_storage *addr)
{
  int ret = -1;

  switch (addr->ss_family) {
    case AF_INET:
    {
      struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;

      ret = IN_MULTICAST (ntohl (addr4->sin_addr.s_addr));
    }
      break;

    case AF_INET6:
    {
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) addr;

      ret = IN6_IS_ADDR_MULTICAST (&addr6->sin6_addr);
    }
      break;

    default:
      errno = EAFNOSUPPORT;
  }

  return ret;
}
