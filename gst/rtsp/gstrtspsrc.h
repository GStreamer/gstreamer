/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_RTSPSRC_H__
#define __GST_RTSPSRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#include "gstrtsp.h"
#include "rtsp.h"

#define GST_TYPE_RTSPSRC \
  (gst_rtspsrc_get_type())
#define GST_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSPSRC,GstRTSPSrc))
#define GST_RTSPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSPSRC,GstRTSPSrcClass))
#define GST_IS_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSPSRC))
#define GST_IS_RTSPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSPSRC))

typedef struct _GstRTSPSrc GstRTSPSrc;
typedef struct _GstRTSPSrcClass GstRTSPSrcClass;

/**
 * GstRTSPProto:
 * @GST_RTSP_PROTO_UDP_UNICAST: Use unicast UDP transfer.
 * @GST_RTSP_PROTO_UDP_MULTICAST: Use multicast UDP transfer
 * @GST_RTSP_PROTO_TCP: Use TCP transfer.
 *
 * Flags with allowed protocols for the datatransfer.
 */
typedef enum
{
  GST_RTSP_PROTO_UDP_UNICAST    = (1 << 0),
  GST_RTSP_PROTO_UDP_MULTICAST  = (1 << 1),
  GST_RTSP_PROTO_TCP            = (1 << 2),
} GstRTSPProto;

typedef struct _GstRTSPStream GstRTSPStream;

struct _GstRTSPStream {
  gint          id;

  GstRTSPSrc   *parent;

  /* pad we expose or NULL when it does not have an actual pad */
  GstPad       *srcpad;
  GstFlowReturn last_ret;

  /* for interleaved mode */
  gint          rtpchannel;
  gint          rtcpchannel;
  GstCaps      *caps;

  /* our udp sources for RTP */
  GstElement   *rtpsrc;
  GstElement   *rtcpsrc;

  /* our udp sink back to the server */
  GstElement   *rtcpsink;

  /* the RTP decoder */
  GstElement   *rtpdec;
  GstPad       *rtpdecrtp;
  GstPad       *rtpdecrtcp;

  /* state */
  gint          pt;
  gboolean      container;
  gchar        *setup_url;
  guint32       ssrc; 
  guint32       seqbase;
};

struct _GstRTSPSrc {
  GstBin           parent;

  /* task and mutex for interleaved mode */
  gboolean         interleaved;
  GstTask         *task;
  GStaticRecMutex *stream_rec_lock;
  GstSegment       segment;
  gboolean         running;

  gint             numstreams;
  GList           *streams;

  gchar           *location;
  RTSPUrl         *url;
  gboolean         debug;
  guint   	   retry;

  GstRTSPProto     protocols;
  /* supported methods */
  gint             methods;

  RTSPConnection  *connection;
  RTSPMessage     *request;
  RTSPMessage     *response;
};

struct _GstRTSPSrcClass {
  GstBinClass parent_class;
};

GType gst_rtspsrc_get_type(void);

G_END_DECLS

#endif /* __GST_RTSPSRC_H__ */
