/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * dp-private.h: private defines/macros for dataprotocol implementation
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

#ifndef __GST_DP_PRIVATE_H__
#define __GST_DP_PRIVATE_H__

#include <gst/gstdata.h>
#include <gst/gstbuffer.h>
#include <gst/gstevent.h>
#include <gst/gstcaps.h>

G_BEGIN_DECLS

/* accessor defines */
#define GST_DP_HEADER_MAJOR_VERSION(x)	((x)[0])
#define GST_DP_HEADER_MINOR_VERSION(x)  ((x)[1])
#define GST_DP_HEADER_FLAGS(x)          ((x)[2])
#define GST_DP_HEADER_PAYLOAD_TYPE(x)   ((x)[3])
#define GST_DP_HEADER_PAYLOAD_LENGTH(x) GST_READ_UINT32_BE (x + 4)
#define GST_DP_HEADER_TIMESTAMP(x)      GST_READ_UINT64_BE (x + 8)
#define GST_DP_HEADER_DURATION(x)       GST_READ_UINT64_BE (x + 16)
#define GST_DP_HEADER_OFFSET(x)         GST_READ_UINT64_BE (x + 24)
#define GST_DP_HEADER_OFFSET_END(x)     GST_READ_UINT64_BE (x + 32)
#define GST_DP_HEADER_BUFFER_FLAGS(x)   GST_READ_UINT16_BE (x + 40)
#define GST_DP_HEADER_CRC_HEADER(x)     GST_READ_UINT16_BE (x + 56)
#define GST_DP_HEADER_CRC_PAYLOAD(x)    GST_READ_UINT16_BE (x + 58)

void gst_dp_init (void);
void gst_dp_dump_byte_array (guint8 *array, guint length);

G_END_DECLS

#endif /* __GST_DP_PRIVATE_H__ */

