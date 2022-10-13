/* GStreamer
 Copyright (C) 2022 Jan Schmidt <jan@centricular.com>
 *
 * gsthlsdemux-preloader.h:
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
#ifndef __GST_HLS_DEMUX_PRELOADER_H__
#define __GST_HLS_DEMUX_PRELOADER_H__

#include <glib.h>

#include "m3u8.h"

#include "downloadrequest.h"
#include "downloadhelper.h"

G_BEGIN_DECLS

typedef struct _GstHLSDemuxPreloader GstHLSDemuxPreloader;

struct _GstHLSDemuxPreloader {
  DownloadHelper *download_helper; /* Owned by the demuxer */
  GPtrArray *active_preloads;
};

GstHLSDemuxPreloader *gst_hls_demux_preloader_new (DownloadHelper *download_helper);
void gst_hls_demux_preloader_free (GstHLSDemuxPreloader *preloader);
void gst_hls_demux_preloader_load (GstHLSDemuxPreloader *preloader, GstM3U8PreloadHint *hint, const gchar *referrer_uri);
void gst_hls_demux_preloader_cancel (GstHLSDemuxPreloader *preloader, GstM3U8PreloadHintType hint_types);

gboolean gst_hls_demux_preloader_provide_request (GstHLSDemuxPreloader *preloader, DownloadRequest *target_req);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_PRELOADER_H__ */
