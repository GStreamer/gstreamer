/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * dataprotocol.h: Functions implementing the GStreamer Data Protocol
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

#ifdef GST_ENABLE_NEW
#ifndef __GST_DATA_PROTOCOL_H__
#define __GST_DATA_PROTOCOL_H__

#include <gst/gstdata.h>
#include <gst/gstbuffer.h>
#include <gst/gstevent.h>
#include <gst/gstcaps.h>

G_BEGIN_DECLS

/* GStreamer Data Protocol Version */
#define GST_DP_VERSION_MAJOR 0
#define GST_DP_VERSION_MINOR 0

#define GST_DP_HEADER_LENGTH 60 /* header size in bytes */


/* header flags */
typedef enum {
  GST_DP_HEADER_FLAG_NONE        = 0,
  GST_DP_HEADER_FLAG_CRC_HEADER  = (1 << 0),
  GST_DP_HEADER_FLAG_CRC_PAYLOAD = (1 << 1),
  GST_DP_HEADER_FLAG_CRC         = (1 << 1) | (1 <<0),
} GstDPHeaderFlag;

/* payload types */
typedef enum {
  GST_DP_PAYLOAD_NONE            = 0,
  GST_DP_PAYLOAD_BUFFER,
  GST_DP_PAYLOAD_CAPS,
  GST_DP_PAYLOAD_EVENT_NONE      = 64,
} GstDPPayloadType;

/* payload information from header */
guint32		gst_dp_header_payload_length	(const guint8 * header);
GstDPPayloadType
		gst_dp_header_payload_type	(const guint8 * header);

/* converting from GstBuffer/GstEvent/GstCaps */
gboolean	gst_dp_header_from_buffer	(const GstBuffer * buffer,
						GstDPHeaderFlag flags,
						guint * length,
						guint8 ** header);
gboolean	gst_dp_packet_from_caps		(const GstCaps * caps,
						GstDPHeaderFlag flags,
						guint * length,
						guint8 ** header,
						guint8 ** payload);
gboolean	gst_dp_packet_from_event	(const GstEvent * event,
						GstDPHeaderFlag flags,
						guint * length,
						guint8 ** header,
						guint8 ** payload);


/* converting to GstBuffer/GstEvent/GstCaps */
GstBuffer *	gst_dp_buffer_from_header	(guint header_length,
						const guint8 * header);
GstCaps *	gst_dp_caps_from_packet		(guint header_length,
						const guint8 * header,
						const guint8 * payload);
GstEvent *	gst_dp_event_from_packet	(guint header_length,
						const guint8 * header,
						const guint8 * payload);

/* validation */
gboolean	gst_dp_validate_header		(guint header_length,
						const guint8 * header);
gboolean	gst_dp_validate_payload		(guint header_length,
						const guint8 * header,
						const guint8 * payload);
gboolean	gst_dp_validate_packet		(guint header_length,
						const guint8 * header,
						const guint8 * payload);

G_END_DECLS

#endif /* __GST_DATA_PROTOCOL_H__ */
#endif /* GST_ENABLE_NEW */

