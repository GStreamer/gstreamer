/* GStreamer
 * Copyright (C) 2022 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_RTP_UTILS_H__
#define __GST_RTP_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_RTP_NTP_UNIX_OFFSET (2208988800LL)

G_GNUC_INTERNAL guint8
gst_rtp_get_extmap_id_for_attribute (const GstStructure * s, const gchar * ext_name);

G_END_DECLS

#endif /* __GST_RTP_UTILS_H__ */
