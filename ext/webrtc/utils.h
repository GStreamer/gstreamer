/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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

#ifndef __WEBRTC_UTILS_H__
#define __WEBRTC_UTILS_H__

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include "fwd.h"

G_BEGIN_DECLS

GstPadTemplate *        _find_pad_template          (GstElement * element,
                                                     GstPadDirection direction,
                                                     GstPadPresence presence,
                                                     const gchar * name);

GstSDPMessage *         _get_latest_sdp             (GstWebRTCBin * webrtc);

GstWebRTCICEStream *    _find_ice_stream_for_session            (GstWebRTCBin * webrtc,
                                                                 guint session_id);
void                    _add_ice_stream_item                    (GstWebRTCBin * webrtc,
                                                                 guint session_id,
                                                                 GstWebRTCICEStream * stream);

struct pad_block
{
  GstElement *element;
  GstPad *pad;
  gulong block_id;
  gpointer user_data;
  GDestroyNotify notify;
};

void                    _free_pad_block             (struct pad_block *block);
struct pad_block *      _create_pad_block           (GstElement * element,
                                                     GstPad * pad,
                                                     gulong block_id,
                                                     gpointer user_data,
                                                     GDestroyNotify notify);

G_GNUC_INTERNAL
gchar *                 _enum_value_to_string       (GType type, guint value);
G_GNUC_INTERNAL
const gchar *           _g_checksum_to_webrtc_string (GChecksumType type);

G_END_DECLS

#endif /* __WEBRTC_UTILS_H__ */
