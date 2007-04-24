/* GStreamer RTP DTMF source
 *
 * gstrtpdtmfsrc.h:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
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

#ifndef __GST_RTP_DTMF_SRC_H__
#define __GST_RTP_DTMF_SRC_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_DTMF_SRC		(gst_rtp_dtmf_src_get_type())
#define GST_RTP_DTMF_SRC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_DTMF_SRC,GstRTPDTMFSrc))
#define GST_RTP_DTMF_SRC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_DTMF_SRC,GstRTPDTMFSrcClass))
#define GST_RTP_DTMF_SRC_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTP_DTMF_SRC, GstRTPDTMFSrcClass))
#define GST_IS_RTP_DTMF_SRC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_DTMF_SRC))
#define GST_IS_RTP_DTMF_SRC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_DTMF_SRC))
#define GST_RTP_DTMF_SRC_CAST(obj)		((GstRTPDTMFSrc *)(obj))

typedef struct {
  unsigned  event:8;         /* Current DTMF event */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned  volume:6;        /* power level of the tone, in dBm0 */
  unsigned  r:1;             /* Reserved-bit */
  unsigned  e:1;             /* End-bit */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned  e:1;             /* End-bit */
  unsigned  r:1;             /* Reserved-bit */
  unsigned  volume:6;        /* power level of the tone, in dBm0 */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  unsigned  duration:16;     /* Duration of digit, in timestamp units */
} GstRTPDTMFPayload;

typedef struct _GstRTPDTMFSrc GstRTPDTMFSrc;
typedef struct _GstRTPDTMFSrcClass GstRTPDTMFSrcClass;

/**
 * GstRTPDTMFSrc:
 * @element: the parent element.
 *
 * The opaque #GstRTPDTMFSrc data structure.
 */
struct _GstRTPDTMFSrc {
  GstElement        element;

  GstPad	    *srcpad;
  GstRTPDTMFPayload *payload;

  guint32           ts_base;
  guint16           seqnum_base;

  gint16            seqnum_offset;
  guint16           seqnum;
  gint32            ts_offset;
  guint32           rtp_timestamp;
  guint32           clock_rate;
  guint             pt;
  guint             ssrc;
  guint             current_ssrc;
  gboolean          first_packet;
  
  GstClockTime      timestamp;
  GstSegment        segment;

  guint16	    interval;
  guint16	    packet_redundancy;
};

struct _GstRTPDTMFSrcClass {
  GstElementClass parent_class;
};

GType gst_rtp_dtmf_src_get_type (void);

G_END_DECLS

#endif /* __GST_RTP_DTMF_SRC_H__ */
