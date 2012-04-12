/* GStreamer ASF/WMV/WMA demuxer
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

#ifndef __ASF_PACKET_H__
#define __ASF_PACKET_H__

#include <gst/gstbuffer.h>
#include <gst/gstclock.h>

#include "gstasfdemux.h"

G_BEGIN_DECLS

typedef struct {
  gboolean      keyframe;          /* buffer flags might not survive merge.. */
  guint         mo_number;         /* media object number (unused)           */
  guint         mo_offset;         /* offset (timestamp for compressed data) */
  guint         mo_size;           /* size of media-object-to-be, or 0       */
  guint         buf_filled;        /* how much of the mo data we got so far  */
  GstBuffer    *buf;               /* buffer to assemble media-object or NULL*/
  guint         rep_data_len;      /* should never be more than 256, since   */
  guint8        rep_data[256];     /* the length should be stored in a byte  */
  GstClockTime  ts;
  GstClockTime  duration;          /* is not always available                */
  guint8        par_x;             /* not always available (0:deactivated)   */
  guint8        par_y;             /* not always available (0:deactivated)   */
  gboolean      interlaced;        /* default: FALSE */
  gboolean      tff;
  gboolean      rff;
} AsfPayload;

typedef struct {
  GstBuffer    *buf;
  const guint8 *bdata;
  guint         length;            /* packet length (unused)               */
  guint         padding;           /* length of padding at end of packet   */
  guint         sequence;          /* sequence (unused)                    */
  GstClockTime  send_time;
  GstClockTime  duration;

  guint8        prop_flags;        /* payload length types                 */
} AsfPacket;

gboolean   gst_asf_demux_parse_packet (GstASFDemux * demux, GstBuffer * buf);

#define gst_asf_payload_is_complete(payload) \
    ((payload)->buf_filled >= (payload)->mo_size)

G_END_DECLS

#endif /* __ASF_PACKET_H__ */

