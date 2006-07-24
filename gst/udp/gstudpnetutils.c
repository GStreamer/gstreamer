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

#include "gstudpnetutils.h"

#ifdef G_OS_WIN32

int
gst_udp_net_utils_win32_inet_aton (const char *c, struct in_addr *paddr)
{
  paddr->s_addr = inet_addr (c);

  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}

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
