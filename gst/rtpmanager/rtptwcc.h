/* GStreamer
 * Copyright (C) 2019 Pexip (http://pexip.com/)
 *   @author: Havard Graff <havard@pexip.com>
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

#ifndef __RTP_TWCC_H__
#define __RTP_TWCC_H__

#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "rtpstats.h"

typedef struct _RTPTWCCManager RTPTWCCManager;
typedef struct _RTPTWCCPacket RTPTWCCPacket;
typedef enum _RTPTWCCPacketStatus RTPTWCCPacketStatus;

G_DECLARE_FINAL_TYPE (RTPTWCCManager, rtp_twcc_manager, RTP, TWCC_MANAGER, GObject)
#define RTP_TYPE_TWCC_MANAGER (rtp_twcc_manager_get_type())
#define RTP_TWCC_MANAGER_CAST(obj) ((RTPTWCCManager *)(obj))

enum _RTPTWCCPacketStatus
{
  RTP_TWCC_PACKET_STATUS_NOT_RECV = 0,
  RTP_TWCC_PACKET_STATUS_SMALL_DELTA = 1,
  RTP_TWCC_PACKET_STATUS_LARGE_NEGATIVE_DELTA = 2,
};

struct _RTPTWCCPacket
{
  GstClockTime local_ts;
  GstClockTime remote_ts;
  GstClockTimeDiff local_delta;
  GstClockTimeDiff remote_delta;
  GstClockTimeDiff delta_delta;
  RTPTWCCPacketStatus status;
  guint16 seqnum;
  guint size;
};

RTPTWCCManager * rtp_twcc_manager_new (guint mtu);

void rtp_twcc_manager_set_mtu (RTPTWCCManager * twcc, guint mtu);

gboolean rtp_twcc_manager_recv_packet (RTPTWCCManager * twcc,
    guint16 seqnum, RTPPacketInfo * pinfo);

void rtp_twcc_manager_send_packet (RTPTWCCManager * twcc,
    guint16 seqnum, RTPPacketInfo * pinfo);
void rtp_twcc_manager_set_send_packet_ts (RTPTWCCManager * twcc,
    guint packet_id, GstClockTime ts);

GstBuffer * rtp_twcc_manager_get_feedback (RTPTWCCManager * twcc,
    guint32 sender_ssrc);

GArray * rtp_twcc_manager_parse_fci (RTPTWCCManager * twcc,
    guint8 * fci_data, guint fci_length);

#endif /* __RTP_TWCC_H__ */
