/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gsttcp.h: helper functions
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

#ifndef __GST_TCP_HELP_H__
#define __GST_TCP_HELP_H__

#include "gsttcp-enumtypes.h"
#include <gst/gst.h>
#include <gst/dataprotocol/dataprotocol.h>

G_BEGIN_DECLS

typedef enum
{
  GST_TCP_PROTOCOL_TYPE_NONE,
  GST_TCP_PROTOCOL_TYPE_GDP
} GstTCPProtocolType;

gchar * gst_tcp_host_to_ip (GstElement *element, const gchar *host);

gint gst_tcp_socket_write (int socket, const void *buf, size_t count);
gint gst_tcp_socket_read (int socket, void *buf, size_t count);

GstData * gst_tcp_gdp_read_header (GstElement *this, int socket);
GstCaps * gst_tcp_gdp_read_caps (GstElement *this, int socket);

gboolean gst_tcp_gdp_write_header (GstElement *this, int socket, GstBuffer *buffer, gboolean fatal, const gchar *host, int port);
gboolean gst_tcp_gdp_write_caps (GstElement *this, int socket, const GstCaps *caps, gboolean fatal, const gchar *host, int port);

G_END_DECLS

#endif /* __GST_TCP_HELP_H__ */
