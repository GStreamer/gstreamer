/* GStreamer
 * Copyright (C) 2021 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_RTPHDREXT_REPAIRED_STREAM_ID_H__
#define __GST_RTPHDREXT_REPAIRED_STREAM_ID_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (gst_rtp_header_extension_repaired_stream_id_get_type())

G_DECLARE_FINAL_TYPE (GstRTPHeaderExtensionRepairedStreamId, gst_rtp_header_extension_repaired_stream_id, GST, RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID, GstRTPHeaderExtension);

GST_ELEMENT_REGISTER_DECLARE (rtphdrextrepairedstreamid);

G_END_DECLS

#endif /* __GST_RTPHDREXT_REPAIRED_STREAM_ID_H__ */
