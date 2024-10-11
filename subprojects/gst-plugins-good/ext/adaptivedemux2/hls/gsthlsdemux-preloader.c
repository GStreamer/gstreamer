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

/* Everything is called from the scheduler thread, including
 * download handling callbacks */

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug

typedef struct _GstHLSDemuxPreloadRequest GstHLSDemuxPreloadRequest;
struct _GstHLSDemuxPreloadRequest
{
  GstHLSDemuxPreloader *preloader;      /* Parent preloader */
  GstM3U8PreloadHint *hint;

  /* Incoming download tracking for the resource */
  DownloadRequest *download_request;
  gboolean download_is_finished;        /* TRUE if the input download request completed / failed */
  guint64 download_cur_offset;  /* offset of the next expected received data */
  guint64 download_content_length;      /* Content length (filled in when response headers arrive) */
  GstClockTime request_latency; /* original HTTP request to data latency */
  GstClockTime download_first_data_time;        /* Arrival timestamp of the first data in the download chunk */
  guint64 download_first_data_offset;   /* First data byte offset of the download chunk */

  /* Target tracking for the stream download to deliver data blocks to */
  /* Each active preload only needs one target to output to at a time,
   * since we only download one segment at a time, and MAP requests are distinct from PART requests,
   * so 1 preload = 1 download request by the stream */
  guint64 target_cur_offset;    /* offset of the next delivered target data */
  DownloadRequest *target_request;
};

static GstHLSDemuxPreloadRequest *
gst_hls_demux_preload_request_new (GstHLSDemuxPreloader * preloader,
    GstM3U8PreloadHint * hint)
{
  GstHLSDemuxPreloadRequest *req = g_new0 (GstHLSDemuxPreloadRequest, 1);
  req->preloader = preloader;
  req->hint = gst_m3u8_preload_hint_ref (hint);
  req->request_latency = GST_CLOCK_TIME_NONE;
  req->download_first_data_time = GST_CLOCK_TIME_NONE;
  req->download_first_data_offset = GST_BUFFER_OFFSET_NONE;

  return req;
};

static void
gst_hls_demux_preload_request_free (GstHLSDemuxPreloadRequest * req)
{
  gst_m3u8_preload_hint_unref (req->hint);

  if (req->download_request != NULL) {
    /* The download request must have been cancelled by the preload helper,
     * but cancellation is async, so we can't verify */
    download_request_unref (req->download_request);
  }

  if (req->target_request != NULL) {
    download_request_unref (req->target_request);
  }

  g_free (req);
};

static gboolean
gst_hls_demux_preloader_submit (GstHLSDemuxPreloader * preloader,
    GstHLSDemuxPreloadRequest * preload_req, const gchar * referrer_uri);
static void gst_hls_demux_preloader_release_request (GstHLSDemuxPreloader *
    preloader, GstHLSDemuxPreloadRequest * preload_req,
    gboolean cancel_download);

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

      gst_hls_demux_preloader_release_request (preloader, req, TRUE);
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
    gst_hls_demux_preloader_release_request (preloader, req, TRUE);
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
      gst_hls_demux_preloader_release_request (preloader, req, TRUE);
      g_ptr_array_remove_index_fast (preloader->active_preloads, idx);
      continue;                 /* Don't increment idx++, as we just removed an item */
    }

    idx++;
  }
}

/* This function transfers any available data to the target request, and possibly
 * completes it and removes it from the preload */
static void
gst_hls_demux_preloader_despatch (GstHLSDemuxPreloadRequest * preload_req,
    gboolean input_is_finished)
{
  GstHLSDemuxPreloader *preloader = preload_req->preloader;
  DownloadRequest *download_req = preload_req->download_request;

  if (input_is_finished)
    preload_req->download_is_finished = TRUE;
  else
    input_is_finished = preload_req->download_is_finished;

  download_request_lock (download_req);

  /* Update timestamp tracking */
  if (preload_req->request_latency == GST_CLOCK_TIME_NONE) {
    if (GST_CLOCK_TIME_IS_VALID (download_req->download_request_time) &&
        GST_CLOCK_TIME_IS_VALID (download_req->download_start_time)) {

      preload_req->request_latency =
          download_req->download_start_time -
          download_req->download_request_time;
    }
  }

  if (preload_req->download_first_data_time == GST_CLOCK_TIME_NONE &&
      download_request_get_bytes_available (download_req) > 0) {
    /* Got the first data of this download burst */
    preload_req->download_first_data_time = download_req->download_start_time;
    preload_req->download_first_data_offset =
        download_request_get_cur_offset (download_req);
  }

  download_request_unlock (download_req);

  /* If there is a target request, see if any of our data should be
   * transferred to it, and if it should be despatched as complete */
  if (preload_req->target_request != NULL) {
    gboolean output_is_finished = input_is_finished;
    gboolean despatch_progress = FALSE;
    DownloadRequest *target_req = preload_req->target_request;

    download_request_lock (target_req);
    download_request_lock (download_req);

    DownloadRequestState target_state = download_req->state;

    /* Transfer the http status code */
    target_req->status_code = download_req->status_code;

    GstBuffer *target_buf = download_request_take_buffer_range (download_req,
        preload_req->target_cur_offset,
        target_req->range_end);

    if (target_buf != NULL) {
      /* Deliver data to the target, and update our tracked output position */
      preload_req->target_cur_offset =
          GST_BUFFER_OFFSET (target_buf) + gst_buffer_get_size (target_buf);

      GST_LOG ("Adding %" G_GSIZE_FORMAT " bytes at offset %" G_GUINT64_FORMAT
          " to target download request uri %s range %" G_GINT64_FORMAT " - %"
          G_GINT64_FORMAT, gst_buffer_get_size (target_buf),
          GST_BUFFER_OFFSET (target_buf), target_req->uri,
          target_req->range_start, target_req->range_end);

      download_request_add_buffer (target_req, target_buf);
      despatch_progress = TRUE; /* Added a buffer, despatch progress callback */

      /* Transfer timing from the input download as best we can, so the receiver can
       * calculate bitrates. If all preload requests filled one target download,
       * we could just transfer the timestamps, but to handle the case of an
       * ongoing chunked connection needs fancier accounting based on the
       * arrival times of each data burst */
      if (target_req->download_start_time == GST_CLOCK_TIME_NONE) {
        if (preload_req->download_first_data_time >
            preload_req->request_latency) {
          target_req->download_request_time =
              preload_req->download_first_data_time -
              preload_req->request_latency;
        } else {
          target_req->download_request_time = 0;
        }

        target_req->download_start_time = preload_req->download_first_data_time;
        target_req->download_newest_data_time =
            download_req->download_newest_data_time;
      }

      if (target_req->range_end != -1
          && preload_req->target_cur_offset > target_req->range_end) {
        /* We've delivered all data to satisfy the requested byte range - the target request is complete */
        if (target_state == DOWNLOAD_REQUEST_STATE_LOADING) {
          target_state = DOWNLOAD_REQUEST_STATE_COMPLETE;
          GST_LOG ("target download request uri %s range %" G_GINT64_FORMAT
              " - %" G_GINT64_FORMAT " is fully satisfied. Completing",
              target_req->uri, target_req->range_start, target_req->range_end);
        }

        output_is_finished = TRUE;

        /* If there's unconsumed data left in the input download, then update
         * our variable that tracks the first data arrival time in in a prorata
         * fashion (because there's more partial segment data already downloaded
         * and we need to preserve a reasonable bitrate estimate. If there's no
         * data, but the connection is continuing, then it's returned to a blocking
         * read state that'll send more data in the future when
         * a new live segment becomes available, so reset our variable as if that
         * download was starting again */
        guint64 data_avail =
            download_request_get_bytes_available (download_req);
        if (data_avail > 0) {
          /* burst first data offset must have been set by now */
          g_assert (preload_req->download_first_data_offset !=
              GST_BUFFER_OFFSET_NONE);

          /* Calculate how long it took to download the data we have output/discarded
           * based on the average bitrate so far.
           * time_to_download = total_download_time * consumed_bytes / total_download_bytes */
          guint64 new_cur_offset =
              download_request_get_cur_offset (download_req);
          GstClockTime data_time_offset =
              gst_util_uint64_scale (download_req->download_newest_data_time -
              preload_req->download_first_data_time,
              new_cur_offset - preload_req->download_first_data_offset,
              new_cur_offset + data_avail -
              preload_req->download_first_data_offset);

          preload_req->download_first_data_time += data_time_offset;
          preload_req->download_first_data_offset = new_cur_offset;

          GST_LOG ("Advancing request timing tracking by %" GST_TIMEP_FORMAT
              " to time %" GST_TIMEP_FORMAT " @ offset %" G_GUINT64_FORMAT,
              &data_time_offset,
              &preload_req->download_first_data_time,
              preload_req->download_first_data_offset);

          /* Say that this target download finished when the first
           * byte of the remaining data arrived */
          target_req->download_end_time = preload_req->download_first_data_time;
        } else {
          /* Reset the download start time */
          preload_req->download_first_data_time = GST_CLOCK_TIME_NONE;
          preload_req->download_first_data_offset = GST_BUFFER_OFFSET_NONE;

          /* Say that this request finished when the most recent data arrived */
          target_req->download_end_time =
              download_req->download_newest_data_time;
        }
      }
    }

    if (input_is_finished
        && target_req->download_end_time == GST_CLOCK_TIME_NONE) {
      /* No download end time was set yet - use the input download end time */
      target_req->download_end_time = download_req->download_end_time;
    }

    /* Update the target request's state, which may have been adjusted from the
     * input request's state */
    target_req->state = target_state;

    if (target_req->headers == NULL && download_req->headers != NULL) {
      target_req->headers = gst_structure_copy (download_req->headers);
    }

    if (target_req->redirect_uri == NULL && download_req->redirect_uri != NULL) {
      target_req->redirect_uri = g_strdup (download_req->redirect_uri);
      target_req->redirect_permanent = download_req->redirect_permanent;
    }

    /* We're done with the input download request . */
    download_request_unlock (download_req);

    if (output_is_finished) {
      GST_DEBUG ("Finishing target preload request uri: %s, start: %"
          G_GINT64_FORMAT " end: %" G_GINT64_FORMAT, target_req->uri,
          target_req->range_start, target_req->range_end);

      download_request_despatch_completion (target_req);
      download_request_unlock (target_req);

      download_request_unref (target_req);
      preload_req->target_request = NULL;
    } else if (despatch_progress) {
      download_request_despatch_progress (target_req);
    }

    /* Unlock if the target request didn't get released above */
    if (preload_req->target_request != NULL) {
      download_request_unlock (preload_req->target_request);
    }
  }

  if (input_is_finished) {
    if (download_req == NULL
        || download_request_get_bytes_available (download_req)
        == 0) {
      GstM3U8PreloadHint *hint = preload_req->hint;
      GST_DEBUG ("Removing finished+drained preload type %d uri: %s, start: %"
          G_GINT64_FORMAT " size: %" G_GINT64_FORMAT, hint->hint_type,
          hint->uri, hint->offset, hint->size);

      /* The incoming request is complete and the data is drained. Remove this preload request from the list */
      g_ptr_array_remove_fast (preloader->active_preloads, preload_req);
      gst_hls_demux_preloader_release_request (preloader, preload_req, FALSE);
    }
  }
}

static void
on_download_cancellation (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  gst_hls_demux_preloader_despatch (preload_req, TRUE);
}

static void
on_download_error (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;
  GST_DEBUG ("preload type %d uri: %s download error", hint->hint_type,
      hint->uri);

  /* FIXME: Should we attempt to re-request a preload? Should we check if
   * any part was transferred to the target request already? Should we
   * attempt to request a byte range with a new start position if we
   * already despatched data to other requests?
   */
  gst_hls_demux_preloader_despatch (preload_req, TRUE);
}

static void
on_download_progress (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;

  GST_DEBUG ("preload type %d uri: %s download progress. position %"
      G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT " bytes", hint->hint_type,
      hint->uri,
      preload_req->download_cur_offset +
      download_request_get_bytes_available (request), request->content_length);
  preload_req->download_content_length = request->content_length;

  gst_hls_demux_preloader_despatch (preload_req, FALSE);
}

static void
on_download_complete (DownloadRequest * request, DownloadRequestState state,
    GstHLSDemuxPreloadRequest * preload_req)
{
  GstM3U8PreloadHint *hint = preload_req->hint;
  GST_DEBUG ("preload type %d uri: %s download complete. position %"
      G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT " bytes", hint->hint_type,
      hint->uri,
      preload_req->download_cur_offset +
      download_request_get_bytes_available (request), request->content_length);
  preload_req->download_content_length = request->content_length;

  gst_hls_demux_preloader_despatch (preload_req, TRUE);
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

  /* Store the current read offset */
  preload_req->download_cur_offset = hint->offset;
  preload_req->download_request = download_req;
  preload_req->download_is_finished = FALSE;
  return TRUE;
}

static void
gst_hls_demux_preloader_release_request (GstHLSDemuxPreloader * preloader,
    GstHLSDemuxPreloadRequest * preload_req, gboolean cancel_download)
{
  if (preload_req->download_request) {
    if (cancel_download) {
      GstM3U8PreloadHint *hint = preload_req->hint;

      GST_DEBUG ("Cancelling preload type %d uri: %s, range start:%"
          G_GINT64_FORMAT " size %" G_GINT64_FORMAT, hint->hint_type, hint->uri,
          hint->offset, hint->size);

      /* We don't want any callbacks to happen after we cancel here */
      download_request_set_callbacks (preload_req->download_request,
          NULL, NULL, NULL, NULL, NULL);
      downloadhelper_cancel_request (preloader->download_helper,
          preload_req->download_request);
    }
  }

  gst_hls_demux_preload_request_free (preload_req);
}

/* See if we can satisfy a download request from a preload, and fulfil it if so.
 * There are several cases:
 *   * The URI and range exactly match one of our preloads -> OK
 *   * The URI matches, and the requested range is a subset of the preload -> OK
 *   * The URI matches, but the requested range is outside what's available in the preload
 *     and can't be provided.
 *
 * Within those options, there are sub-possibilities:
 *   * The preload request is ongoing. It might have enough data already to completely provide
 *     the requested range.
 *   * The preload request is ongoing, but has already moved past the requested range (no longer available)
 *   * The preload request is ongoing, will feed data to the target req as it arrives
 *   * The preload request is complete already, so can either provide the requested range or not, but
 *     also needs to mark the target_req as completed once it has passed the required data.
 */
gboolean
gst_hls_demux_preloader_provide_request (GstHLSDemuxPreloader * preloader,
    DownloadRequest * target_req)
{
  guint idx;
  for (idx = 0; idx < preloader->active_preloads->len; idx++) {
    GstHLSDemuxPreloadRequest *preload_req =
        g_ptr_array_index (preloader->active_preloads, idx);
    GstM3U8PreloadHint *hint = preload_req->hint;

    if (g_strcmp0 (hint->uri, target_req->uri))
      continue;

    GST_LOG ("Possible matching preload type %d uri: %s, range start:%"
        G_GINT64_FORMAT " size %" G_GINT64_FORMAT " (download position %"
        G_GUINT64_FORMAT ") for req with range %" G_GINT64_FORMAT " to %"
        G_GINT64_FORMAT, hint->hint_type, hint->uri, hint->offset, hint->size,
        preload_req->download_cur_offset, target_req->range_start,
        target_req->range_end);

    if (target_req->range_start > preload_req->download_cur_offset) {
      /* This preload request is for a byte range beyond the desired
       * position (or something already consumed the target data) */
      GST_LOG ("Range start didn't match");
      continue;
    }

    if (target_req->range_end != -1) {
      /* The target request does not want the entire rest of the preload
       * stream, so check that the end is satisfiable */
      gint64 content_length = preload_req->download_content_length;
      if (content_length == 0) {
        /* We don't have information from the preload download's response headers yet,
         * so check against the requested length and error out later if the server
         * doesn't provide all the desired response */
        if (hint->size != -1)
          content_length = hint->size;
      }

      if (content_length != 0) {
        /* We have some idea of the content length. Check if it will provide the requested
         * range */
        if (target_req->range_end > hint->offset + content_length - 1) {
          GST_LOG ("Range end %" G_GINT64_FORMAT " is beyond the end (%"
              G_GINT64_FORMAT ") of this preload", target_req->range_end,
              hint->offset + content_length - 1);
          continue;
        }
      }
    }

    GST_DEBUG ("Found a matching preload type %d uri: %s, range start:%"
        G_GINT64_FORMAT " size %" G_GINT64_FORMAT, hint->hint_type, hint->uri,
        hint->offset, hint->size);

    if (preload_req->target_request != NULL) {
      DownloadRequest *old_request = preload_req->target_request;

      /* Detach the existing target request */
      if (old_request != target_req) {
        download_request_lock (old_request);
        old_request->state = DOWNLOAD_REQUEST_STATE_UNSENT;
        download_request_despatch_completion (old_request);
        download_request_unlock (old_request);
      }

      download_request_unref (old_request);
      preload_req->target_request = NULL;
    }

    /* Attach the new target request and despatch any available data */
    preload_req->target_cur_offset = target_req->range_start;
    preload_req->target_request = download_request_ref (target_req);

    download_request_lock (target_req);
    target_req->state = DOWNLOAD_REQUEST_STATE_UNSENT;
    download_request_begin_download (target_req);
    download_request_unlock (target_req);

    gst_hls_demux_preloader_despatch (preload_req, FALSE);
    return TRUE;
  }

  return FALSE;
}
