/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __RTSP_TRANSPORT_H__
#define __RTSP_TRANSPORT_H__

#include <rtspdefs.h>

G_BEGIN_DECLS

/**
 * RTSPTransMode:
 * @RTSP_TRANS_UNKNOWN: invalid tansport mode
 * @RTSP_TRANS_RTP: transfer RTP data
 * @RTSP_TRANS_RDT: transfer RDT (RealMedia) data
 *
 * The transfer mode to use.
 */
typedef enum {
  RTSP_TRANS_UNKNOWN =  0,
  RTSP_TRANS_RTP     = (1 << 0),
  RTSP_TRANS_RDT     = (1 << 1)
} RTSPTransMode;

/**
 * RTSPProfile:
 * @RTSP_PROFILE_UNKNOWN: invalid profile
 * @RTSP_PROFILE_AVP: the Audio/Visual profile
 * @RTSP_PROFILE_SAVP: the secure Audio/Visual profile
 *
 * The transfer profile to use.
 */
typedef enum {
  RTSP_PROFILE_UNKNOWN =  0,
  RTSP_PROFILE_AVP     = (1 << 0),
  RTSP_PROFILE_SAVP    = (1 << 1)
} RTSPProfile;

/**
 * RTSPLowerTrans:
 * @RTSP_LOWER_TRANS_UNKNOWN: invalid transport flag
 * @RTSP_LOWER_TRANS_UDP: stream data over UDP
 * @RTSP_LOWER_TRANS_UDP_MCAST: stream data over UDP multicast
 * @RTSP_LOWER_TRANS_TCP: stream data over TCP
 *
 * The different transport methods.
 */
typedef enum {
  RTSP_LOWER_TRANS_UNKNOWN   = 0,
  RTSP_LOWER_TRANS_UDP       = (1 << 0),
  RTSP_LOWER_TRANS_UDP_MCAST = (1 << 1),
  RTSP_LOWER_TRANS_TCP       = (1 << 2)
} RTSPLowerTrans;

/**
 * RTSPRange:
 * @min: minimum value of the range
 * @max: maximum value of the range
 *
 * A type to specify a range.
 */
typedef struct
{
  gint min;
  gint max;
} RTSPRange;

/**
 * RTSPTransport:
 *
 * A structure holding the RTSP transport values.
 */
typedef struct _RTSPTransport {
  /*< private >*/
  RTSPTransMode  trans;
  RTSPProfile    profile;
  RTSPLowerTrans lower_transport;

  gchar         *destination;
  gchar         *source;
  gint           layers;
  gboolean       mode_play;
  gboolean       mode_record;
  gboolean       append;
  RTSPRange      interleaved;

  /* multicast specific */
  gint  ttl;

  /* UDP specific */
  RTSPRange      port;
  RTSPRange      client_port;
  RTSPRange      server_port;
  /* RTP specific */
  gchar         *ssrc;
  
} RTSPTransport;

RTSPResult      rtsp_transport_new          (RTSPTransport **transport);
RTSPResult      rtsp_transport_init         (RTSPTransport *transport);

RTSPResult      rtsp_transport_parse        (const gchar *str, RTSPTransport *transport);

RTSPResult      rtsp_transport_get_mime     (RTSPTransMode trans, const gchar **mime);
RTSPResult      rtsp_transport_get_manager  (RTSPTransMode trans, const gchar **manager, guint option);

RTSPResult      rtsp_transport_free         (RTSPTransport *transport);

G_END_DECLS

#endif /* __RTSP_TRANSPORT_H__ */
