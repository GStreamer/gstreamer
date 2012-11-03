/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#ifndef __GST_DCCP_NET_H__
#define __GST_DCCP_NET_H__

#ifndef G_OS_WIN32
#  include <netdb.h>
#  include <sys/socket.h>
#  include <netinet/in.h>                   /* sockaddr_in */
#  include <arpa/inet.h>
#  include <sys/ioctl.h>
#else
/* ws2_32.dll has getaddrinfo and freeaddrinfo on Windows XP and later.
 * minwg32 headers check WINVER before allowing the use of these */
#ifndef WINVER
#  define WINVER 0x0501
#endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#ifndef socklen_t
#define socklen_t int
#endif
#endif
#include <sys/types.h>
#include <_stdint.h>
#include <unistd.h>
#include <string.h>

#endif /* __GST_DCCP_NET_H__ */
