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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gstrtsp.h"
#include "rtsp.h"

#define GST_TYPE_RTSPSRC \
  (gst_rtspsrc_get_type())
#define GST_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSPSRC,GstRTSPSrc))
#define GST_RTSPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSPSRC,GstRTSPSrc))
#define GST_IS_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSPSRC))
#define GST_IS_RTSPSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSPSRC))

typedef struct _GstRTSPSrc GstRTSPSrc;
typedef struct _GstRTSPSrcClass GstRTSPSrcClass;

/* flags with allowed protocols */
typedef enum
{
  GST_RTSP_PROTO_UDP_UNICAST    = (1 << 0),
  GST_RTSP_PROTO_UDP_MULTICAST  = (1 << 1),
  GST_RTSP_PROTO_TCP            = (1 << 2),
} GstRTSPProto;

typedef struct _GstRTSPStream GstRTSPStream;

struct _GstRTSPStream {
  gint        id;

  gint        rtpchannel;
  gint        rtcpchannel;

  GstRTSPSrc *parent;

  /* our udp sources */
  GstElement *rtpsrc;
  GstElement *rtcpsrc;

  /* our udp sink back to the server */
  GstElement *rtcpsink;

  /* the rtp decoder */
  GstElement *rtpdec;
  GstPad     *rtpdecrtp;
  GstPad     *rtpdecrtcp;
};

struct _GstRTSPSrc {
  GstElement element;

  gboolean       interleaved;
  GstTask       *task;

  gint           numstreams;
  GList         *streams;

  gchar         *location;
  gboolean       debug;

  GstRTSPProto   protocols;
  /* supported options */
  gint           options;

  RTSPConnection *connection;
  RTSPMessage   *request;
  RTSPMessage   *response;
};

struct _GstRTSPSrcClass {
  GstElementClass parent_class;
};

GType gst_rtspsrc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_RTSPSRC_H__ */
