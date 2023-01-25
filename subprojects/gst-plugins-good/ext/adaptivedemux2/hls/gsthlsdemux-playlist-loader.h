/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
 *
 * Copyright (C) 2021-2022 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
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
#ifndef _GST_HLS_DEMUX_PLAYLIST_LOADER_H_
#define _GST_HLS_DEMUX_PLAYLIST_LOADER_H_

#include <gst/gst.h>
#include "gstadaptivedemux.h"
#include "gstadaptivedemuxutils.h"
#include "downloadhelper.h"
#include "downloadrequest.h"

G_BEGIN_DECLS

#define GST_TYPE_HLS_DEMUX_PLAYLIST_LOADER (gst_hls_demux_playlist_loader_get_type())
#define GST_HLS_DEMUX_PLAYLIST_LOADER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX_PLAYLIST_LOADER,GstHLSDemuxPlaylistLoader))
#define GST_HLS_DEMUX_PLAYLIST_LOADER_CAST(obj) ((GstHLSDemuxPlaylistLoader *)obj)

typedef struct _GstHLSDemuxPlaylistLoader GstHLSDemuxPlaylistLoader;
typedef struct _GstHLSDemuxPlaylistLoaderClass GstHLSDemuxPlaylistLoaderClass;
typedef struct _GstHLSDemuxPlaylistLoaderPrivate GstHLSDemuxPlaylistLoaderPrivate;

typedef void (*GstHLSDemuxPlaylistLoaderSuccessCallback) (GstHLSDemuxPlaylistLoader *pl,
    const gchar *playlist_uri, GstHLSMediaPlaylist *new_playlist, gpointer userdata);
typedef void (*GstHLSDemuxPlaylistLoaderErrorCallback) (GstHLSDemuxPlaylistLoader *pl,
    const gchar *playlist_uri, gpointer userdata);

struct _GstHLSDemuxPlaylistLoaderClass
{
  GstObjectClass parent_class;
};

struct _GstHLSDemuxPlaylistLoader
{
  GstObject object;
  GstHLSDemuxPlaylistLoaderPrivate *priv;
};

GType gst_hls_demux_playlist_loader_get_type(void);

GstHLSDemuxPlaylistLoader *gst_hls_demux_playlist_loader_new(GstAdaptiveDemux *demux,
    DownloadHelper *download_helper);

void gst_hls_demux_playlist_loader_set_callbacks (GstHLSDemuxPlaylistLoader *pl,
      GstHLSDemuxPlaylistLoaderSuccessCallback success_cb,
      GstHLSDemuxPlaylistLoaderErrorCallback error_cb,
      gpointer userdata);

void gst_hls_demux_playlist_loader_start (GstHLSDemuxPlaylistLoader *pl);
void gst_hls_demux_playlist_loader_stop (GstHLSDemuxPlaylistLoader *pl);

void gst_hls_demux_playlist_loader_set_playlist_uri (GstHLSDemuxPlaylistLoader *pl,
  const gchar *base_uri, const gchar *current_playlist_uri);
gboolean gst_hls_demux_playlist_loader_has_current_uri (GstHLSDemuxPlaylistLoader *pl,
  const gchar *target_playlist_uri);

G_END_DECLS
#endif
