/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifndef __RTSP_TRANSPORT_H__
#define __RTSP_TRANSPORT_H__

#include <rtspdefs.h>

G_BEGIN_DECLS

typedef enum {
  RTSP_TRANS_RTP,
} RTSPTransMode;

typedef enum {
  RTSP_PROFILE_AVP,
} RTSPProfile;

typedef enum {
  RTSP_LOWER_TRANS_UNKNOWN,
  RTSP_LOWER_TRANS_UDP,
  RTSP_LOWER_TRANS_TCP,
} RTSPLowerTrans;

typedef struct
{
  gint min;
  gint max;
} RTSPRange;

typedef struct _RTSPTransport {
  RTSPTransMode  trans;
  RTSPProfile    profile;
  RTSPLowerTrans lower_transport;

  gboolean       multicast;
  gchar         *destination;
  gchar         *source;
  gint           layers;
  gboolean       mode_play;
  gboolean       mode_record;
  gboolean       append;
  RTSPRange      interleaved;

  /* mulitcast specific */
  gint  ttl;

  /* RTP specific */
  RTSPRange      port;
  RTSPRange      client_port;
  RTSPRange      server_port;
  gchar         *ssrc;
  
} RTSPTransport;

RTSPResult      rtsp_transport_new      (RTSPTransport **transport);
RTSPResult      rtsp_transport_init     (RTSPTransport *transport);

RTSPResult      rtsp_transport_parse    (gchar *str, RTSPTransport *transport);

RTSPResult      rtsp_transport_free     (RTSPTransport *transport);

G_END_DECLS

#endif /* __RTSP_TRANSPORT_H__ */
