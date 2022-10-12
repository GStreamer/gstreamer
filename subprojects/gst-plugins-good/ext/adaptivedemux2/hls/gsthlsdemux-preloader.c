/* GStreamer
 Copyright (C) 2022 Jan Schmidt <jan@centricular.com>
 *
 * gsthlsdemux-preloader.c:
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsthlsdemux-preloader.h"

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug

typedef struct _GstHLSDemuxPreloadRequest GstHLSDemuxPreloadRequest;
struct _GstHLSDemuxPreloadRequest
{
  GstHLSDemuxPreloader *preloader;      /* Parent preloader */
  GstM3U8PreloadHint *hint;
  DownloadRequest *download_request;
};

static GstHLSDemuxPreloadRequest *
gst_hls_demux_preload_request_new (GstHLSDemuxPreloader * preloader,
    GstM3U8PreloadHint * hint)
{
  GstHLSDemuxPreloadRequest *req = g_new0 (GstHLSDemuxPreloadRequest, 1);
  req->preloader = preloader;
  req->hint = gst_m3u8_preload_hint_ref (hint);

  return req;
};

static void
gst_hls_demux_preload_request_free (GstHLSDemuxPreloadRequest * req)
{
  gst_m3u8_preload_hint_unref (req->hint);

  /* The download request must have been cancelled and removed by the preload helper */
  g_assert (req->download_request == NULL);
  g_free (req);
};

static gboolean
gst_hls_demux_preloader_submit (GstHLSDemuxPreloader * preloader,
    GstHLSDemuxPreloadRequest * preload_req, const gchar * referrer_uri);
static void gst_hls_demux_preloader_cancel_request (GstHLSDemuxPreloader *
    preloader, GstHLSDemuxPreloadRequest * req);

GstHLSDemuxPreloader *
gst_hls_demux_preloader_new (DownloadHelper * download_helper)
{
  GstHLSDemuxPreloader *preloader = g_new0 (GstHLSDemuxPreloader, 1);

  preloader->download_helper = download_helper;
  preloader->active_preloads = g_ptr_array_new ();

  return preloader;
}

void
gst_hls_demux_preloader_free (GstHLSDemuxPreloader * preloader)
{
  gst_hls_demux_preloader_cancel (preloader, M3U8_PRELOAD_HINT_ALL);
  g_ptr_array_free (preloader->active_preloads, TRUE);
  g_free (preloader);
}

void
gst_hls_demux_preloader_load (GstHLSDemuxPreloader * preloader,
    GstM3U8PreloadHint * hint, const gchar * referrer_uri)
{
  /* Check if we have an active preload already for this hint */
  guint idx;
  for (idx = 0; idx < preloader->active_preloads->len; idx++) {
    GstHLSDemuxPreloadRequest *req =
        g_ptr_array_index (preloader->active_preloads, idx);
    if (hint->hint_type == req->hint->hint_type) {
      /* We already have an active hint of this type. If this new one is different, cancel
       * the active preload before starting this one */
      if (gst_m3u8_preload_hint_equal (hint, req->hint)) {
        GST_LOG ("Ignoring pre-existing preload of type %d uri: %s, range:%"
            G_GINT64_FORMAT " size %" G_GINT64_FORMAT, hint->hint_type,
            hint->uri, hint->offset, hint->size);
        return;                 /* Nothing to do */
      }

      gst_hls_demux_preloader_cancel_request (preloader, req);
      g_ptr_array_remove_index_fast (preloader->active_preloads, idx);
      break;
    }
  }

  /* If we get here, then there's no preload of this type. Create one */
  GstHLSDemuxPreloadRequest *req =
      gst_hls_demux_preload_request_new (preloader, hint);
  /* Submit the request */

  if (gst_hls_demux_preloader_submit (preloader, req, referrer_uri)) {
    g_ptr_array_add (preloader->active_preloads, req);
  } else {
    /* Discard failed request */
    gst_hls_demux_preloader_cancel_request (preloader, req);
  }
}

void
gst_hls_demux_preloader_cancel (GstHLSDemuxPreloader * preloader,
    GstM3U8PreloadHintType hint_types)
{
  /* Go through the active downloads and remove/cancel any with the matching type */
  guint idx;
  for (idx = 0; idx < preloader->active_preloads->len;) {
    GstHLSDemuxPreloadRequest *req =
        g_ptr_array_index (preloader->active_preloads, idx);
    if (hint_types & req->hint->hint_type) {
      gst_hls_demux_preloader_cancel_request (preloader, req);
      g_ptr_array_remove_index_fast (preloader->active_preloads, idx);
      continue;                 /* Don't increment idx++, as we just removed an item */
    }

    idx++;
  }
}

static void
on_download_cancellation (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
}

static void
on_download_error (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;
  GST_DEBUG ("preload type %d uri: %s download error", hint->hint_type,
      hint->uri);
  GST_FIXME ("How to handle failed preload request?");
}

static void
on_download_progress (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;
  GST_DEBUG ("preload type %d uri: %s download progress", hint->hint_type,
      hint->uri);
}

static void
on_download_complete (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;
  GST_DEBUG ("preload type %d uri: %s download complete", hint->hint_type,
      hint->uri);
}

static gboolean
gst_hls_demux_preloader_submit (GstHLSDemuxPreloader * preloader,
    GstHLSDemuxPreloadRequest * preload_req, const gchar * referrer_uri)
{
  g_assert (preload_req->download_request == NULL);

  DownloadRequest *download_req = download_request_new ();
  GstM3U8PreloadHint *hint = preload_req->hint;

  /* Configure our download request */
  gint64 end = RFC8673_LAST_BYTE_POS;
  if (hint->size > 0) {
    end = hint->offset + hint->size - 1;
  }

  download_request_set_uri (download_req, hint->uri, hint->offset, end);
  download_request_set_callbacks (download_req,
      (DownloadRequestEventCallback) on_download_complete,
      (DownloadRequestEventCallback) on_download_error,
      (DownloadRequestEventCallback) on_download_cancellation,
      (DownloadRequestEventCallback) on_download_progress, preload_req);

  GST_DEBUG ("Submitting preload type %d uri: %s, range:%" G_GINT64_FORMAT
      " - %" G_GINT64_FORMAT, hint->hint_type, hint->uri, hint->offset, end);

  if (!downloadhelper_submit_request (preloader->download_helper,
          referrer_uri, DOWNLOAD_FLAG_NONE, download_req, NULL)) {
    /* Abandon the request */
    download_request_unref (download_req);
    return FALSE;
  }

  preload_req->download_request = download_req;
  return TRUE;
}

static void
gst_hls_demux_preloader_cancel_request (GstHLSDemuxPreloader * preloader,
    GstHLSDemuxPreloadRequest * preload_req)
{
  if (preload_req->download_request) {
    GstM3U8PreloadHint *hint = preload_req->hint;
    GST_DEBUG ("Cancelling preload type %d uri: %s, range start:%"
        G_GINT64_FORMAT " size %" G_GINT64_FORMAT, hint->hint_type, hint->uri,
        hint->offset, hint->size);

    downloadhelper_cancel_request (preloader->download_helper,
        preload_req->download_request);
    download_request_unref (preload_req->download_request);
    preload_req->download_request = NULL;
  }
  gst_hls_demux_preload_request_free (preload_req);
}
