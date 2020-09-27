/* GStreamer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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


#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

G_BEGIN_DECLS

#define GST_TYPE_REPLAY_BIN (gst_replay_bin_get_type ())
G_DECLARE_FINAL_TYPE (GstReplayBin, gst_replay_bin, GST, REPLAY_BIN, GstBin);

#define GST_TYPE_RTSP_MEDIA_FACTORY_REPLAY (gst_rtsp_media_factory_replay_get_type ())
G_DECLARE_FINAL_TYPE (GstRTSPMediaFactoryReplay,
    gst_rtsp_media_factory_replay, GST, RTSP_MEDIA_FACTORY_REPLAY,
    GstRTSPMediaFactory);

G_END_DECLS
