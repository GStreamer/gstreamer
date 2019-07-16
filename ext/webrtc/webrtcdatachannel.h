/* GStreamer
 * Copyright (C) 2018 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_WEBRTC_DATA_CHANNEL_H__
#define __GST_WEBRTC_DATA_CHANNEL_H__

#include <gst/gst.h>
#include <gst/webrtc/webrtc_fwd.h>
#include <gst/webrtc/dtlstransport.h>
#include "sctptransport.h"

G_BEGIN_DECLS

GST_WEBRTC_API
GType gst_webrtc_data_channel_get_type(void);
#define GST_TYPE_WEBRTC_DATA_CHANNEL            (gst_webrtc_data_channel_get_type())
#define GST_WEBRTC_DATA_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_DATA_CHANNEL,GstWebRTCDataChannel))
#define GST_IS_WEBRTC_DATA_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_DATA_CHANNEL))
#define GST_WEBRTC_DATA_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_DATA_CHANNEL,GstWebRTCDataChannelClass))
#define GST_IS_WEBRTC_DATA_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_DATA_CHANNEL))
#define GST_WEBRTC_DATA_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_DATA_CHANNEL,GstWebRTCDataChannelClass))

typedef struct _GstWebRTCDataChannel GstWebRTCDataChannel;
typedef struct _GstWebRTCDataChannelClass GstWebRTCDataChannelClass;

struct _GstWebRTCDataChannel
{
  GObject                         parent;

  GstWebRTCSCTPTransport           *sctp_transport;
  GstElement                       *appsrc;
  GstElement                       *appsink;

  gchar                            *label;
  gboolean                          ordered;
  guint                             max_packet_lifetime;
  guint                             max_retransmits;
  gchar                            *protocol;
  gboolean                          negotiated;
  gint                              id;
  GstWebRTCPriorityType             priority;
  GstWebRTCDataChannelState         ready_state;
  guint64                           buffered_amount;
  guint64                           buffered_amount_low_threshold;

  GstWebRTCBin                     *webrtcbin;
  gboolean                          opened;
  gulong                            src_probe;
  GError                           *stored_error;
  GMutex                            lock;

  gpointer                          _padding[GST_PADDING];
};

struct _GstWebRTCDataChannelClass
{
  GObjectClass            parent_class;

  gpointer                  _padding[GST_PADDING];
};

void    gst_webrtc_data_channel_start_negotiation   (GstWebRTCDataChannel       *channel);
G_GNUC_INTERNAL
void    gst_webrtc_data_channel_link_to_sctp (GstWebRTCDataChannel              *channel,
                                              GstWebRTCSCTPTransport            *sctp_transport);

G_END_DECLS

#endif /* __GST_WEBRTC_DATA_CHANNEL_H__ */
