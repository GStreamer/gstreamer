/* GStreamer
 * Copyright (C) 2020-2021 Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
 *
 * gstrtphdrext-colorspace.h: Color Space RTP header extension
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

#ifndef __GST_RTPHDREXT_COLORSPACE_H__
#define __GST_RTPHDREXT_COLORSPACE_H__

#include <gst/rtp/gstrtphdrext.h>

G_BEGIN_DECLS

#define GST_RTP_HDREXT_COLORSPACE_SIZE 4
#define GST_RTP_HDREXT_COLORSPACE_WITH_HDR_META_SIZE 28
#define GST_RTP_HDREXT_COLORSPACE_URI "http://www.webrtc.org/experiments/rtp-hdrext/color-space"

#define GST_TYPE_RTP_HEADER_EXTENSION_COLORSPACE (gst_rtp_header_extension_colorspace_get_type())

G_DECLARE_FINAL_TYPE (GstRTPHeaderExtensionColorspace, gst_rtp_header_extension_colorspace,
  GST, RTP_HEADER_EXTENSION_COLORSPACE, GstRTPHeaderExtension)

G_END_DECLS

#endif /* __GST_RTPHDREXT_COLORSPACE_H__ */
