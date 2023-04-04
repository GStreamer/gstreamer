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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthlsdemux.h"
#include "gsthlsdemux-playlist-loader.h"
#include "m3u8.h"

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug

#define MAX_DOWNLOAD_ERROR_COUNT 3

typedef enum _PlaylistLoaderState PlaylistLoaderState;

enum _PlaylistLoaderState
{
  PLAYLIST_LOADER_STATE_STOPPED = 0,
  PLAYLIST_LOADER_STATE_STARTING,
  PLAYLIST_LOADER_STATE_LOADING,
  PLAYLIST_LOADER_STATE_WAITING,
};

struct _GstHLSDemuxPlaylistLoaderPrivate
{
  GstAdaptiveDemux *demux;

  GstHLSDemuxPlaylistLoaderSuccessCallback success_cb;
  GstHLSDemuxPlaylistLoaderErrorCallback error_cb;
  gpointer userdata;

  GstAdaptiveDemuxLoop *scheduler_task;
  DownloadHelper *download_helper;
  DownloadRequest *download_request;

  PlaylistLoaderState state;
  guint pending_cb_id;

  gchar *base_uri;
  gchar *target_playlist_uri;

  gchar *loading_playlist_uri;

  gboolean delta_merge_failed;
  gchar *current_playlist_uri;
  GstHLSMediaPlaylist *current_playlist;

  gchar *current_playlist_redirect_uri;

  guint download_error_count;
};

#define gst_hls_demux_playlist_loader_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstHLSDemuxPlaylistLoader,
    gst_hls_demux_playlist_loader, GST_TYPE_OBJECT);

static void gst_hls_demux_playlist_loader_finalize (GObject * object);
static gboolean gst_hls_demux_playlist_loader_update (GstHLSDemuxPlaylistLoader
    * pl);
static void start_playlist_download (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv);

/* Takes ownership of the loop ref */
GstHLSDemuxPlaylistLoader *
gst_hls_demux_playlist_loader_new (GstAdaptiveDemux * demux,
    DownloadHelper * download_helper)
{
  GstHLSDemuxPlaylistLoader *pl =
      g_object_new (GST_TYPE_HLS_DEMUX_PLAYLIST_LOADER, NULL);
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  priv->demux = demux;
  priv->scheduler_task = gst_adaptive_demux_get_loop (demux);
  priv->download_helper = download_helper;

  return pl;
}

void
gst_hls_demux_playlist_loader_set_callbacks (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderSuccessCallback success_cb,
    GstHLSDemuxPlaylistLoaderErrorCallback error_cb, gpointer userdata)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  priv->success_cb = success_cb;
  priv->error_cb = error_cb;
  priv->userdata = userdata;
}

static void
gst_hls_demux_playlist_loader_class_init (GstHLSDemuxPlaylistLoaderClass *
    klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_hls_demux_playlist_loader_finalize;
}

static void
gst_hls_demux_playlist_loader_init (GstHLSDemuxPlaylistLoader * pl)
{
  pl->priv = gst_hls_demux_playlist_loader_get_instance_private (pl);
}

static void
gst_hls_demux_playlist_loader_finalize (GObject * object)
{
  GstHLSDemuxPlaylistLoader *pl = GST_HLS_DEMUX_PLAYLIST_LOADER (object);
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (priv->pending_cb_id != 0) {
    gst_adaptive_demux_loop_cancel_call (priv->scheduler_task,
        priv->pending_cb_id);
    priv->pending_cb_id = 0;
  }

  if (priv->download_request) {
    downloadhelper_cancel_request (priv->download_helper,
        priv->download_request);
    download_request_unref (priv->download_request);
    priv->download_request = NULL;
  }

  if (priv->scheduler_task)
    gst_adaptive_demux_loop_unref (priv->scheduler_task);

  g_free (priv->base_uri);
  g_free (priv->target_playlist_uri);
  g_free (priv->loading_playlist_uri);

  if (priv->current_playlist)
    gst_hls_media_playlist_unref (priv->current_playlist);
  g_free (priv->current_playlist_uri);
  g_free (priv->current_playlist_redirect_uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
schedule_state_update (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv)
{
  g_assert (priv->pending_cb_id == 0);
  priv->pending_cb_id =
      gst_adaptive_demux_loop_call (priv->scheduler_task,
      (GSourceFunc) gst_hls_demux_playlist_loader_update,
      gst_object_ref (pl), (GDestroyNotify) gst_object_unref);
}

static void
schedule_next_playlist_load (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv, GstClockTime next_load_interval)
{
  /* If we have a valid request time, compute a more accurate download time for
   * the playlist.
   *
   * This better takes into account the time it took to actually get and process
   * the current playlist.
   */
  if (priv->current_playlist
      && GST_CLOCK_TIME_IS_VALID (priv->current_playlist->request_time)) {
    GstClockTime now = gst_adaptive_demux2_get_monotonic_time (priv->demux);
    GstClockTimeDiff load_diff = GST_CLOCK_DIFF (now,
        priv->current_playlist->request_time + next_load_interval);
    GST_LOG_OBJECT (pl,
        "now %" GST_TIME_FORMAT " request_time %" GST_TIME_FORMAT
        " next_load_interval %" GST_TIME_FORMAT, GST_TIME_ARGS (now),
        GST_TIME_ARGS (priv->current_playlist->request_time),
        GST_TIME_ARGS (next_load_interval));
    if (load_diff < 0) {
      GST_LOG_OBJECT (pl, "Playlist update already late by %" GST_STIME_FORMAT,
          GST_STIME_ARGS (load_diff));
    };
    next_load_interval = MAX (0, load_diff);
  }

  GST_LOG_OBJECT (pl, "Scheduling next playlist reload in %" GST_TIME_FORMAT,
      GST_TIME_ARGS (next_load_interval));
  g_assert (priv->pending_cb_id == 0);
  priv->state = PLAYLIST_LOADER_STATE_WAITING;
  priv->pending_cb_id =
      gst_adaptive_demux_loop_call_delayed (priv->scheduler_task,
      next_load_interval,
      (GSourceFunc) gst_hls_demux_playlist_loader_update,
      gst_object_ref (pl), (GDestroyNotify) gst_object_unref);
}

void
gst_hls_demux_playlist_loader_start (GstHLSDemuxPlaylistLoader * pl)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (priv->state != PLAYLIST_LOADER_STATE_STOPPED) {
    GST_LOG_OBJECT (pl, "Already started - state %d", priv->state);
    return;                     /* Already active */
  }

  GST_DEBUG_OBJECT (pl, "Starting playlist loading");
  priv->state = PLAYLIST_LOADER_STATE_STARTING;
  schedule_state_update (pl, priv);
}

void
gst_hls_demux_playlist_loader_stop (GstHLSDemuxPlaylistLoader * pl)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (priv->state == PLAYLIST_LOADER_STATE_STOPPED)
    return;                     /* Not runnning */

  GST_DEBUG_OBJECT (pl, "Stopping playlist loading");

  if (priv->pending_cb_id != 0) {
    gst_adaptive_demux_loop_cancel_call (priv->scheduler_task,
        priv->pending_cb_id);
    priv->pending_cb_id = 0;
  }

  if (priv->download_request) {
    downloadhelper_cancel_request (priv->download_helper,
        priv->download_request);
    download_request_unref (priv->download_request);
    priv->download_request = NULL;
  }

  priv->state = PLAYLIST_LOADER_STATE_STOPPED;
}

void
gst_hls_demux_playlist_loader_set_playlist_uri (GstHLSDemuxPlaylistLoader * pl,
    const gchar * base_uri, const gchar * new_playlist_uri)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  gboolean playlist_uri_change = (priv->target_playlist_uri == NULL
      || g_strcmp0 (new_playlist_uri, priv->target_playlist_uri) != 0);

  if (!playlist_uri_change)
    return;

  GST_DEBUG_OBJECT (pl, "Setting target playlist URI to %s", new_playlist_uri);

  g_free (priv->base_uri);
  g_free (priv->target_playlist_uri);

  priv->base_uri = g_strdup (base_uri);
  priv->target_playlist_uri = g_strdup (new_playlist_uri);
  priv->delta_merge_failed = FALSE;

  switch (priv->state) {
    case PLAYLIST_LOADER_STATE_STOPPED:
      return;                   /* Not runnning */
    case PLAYLIST_LOADER_STATE_STARTING:
    case PLAYLIST_LOADER_STATE_LOADING:
      /* If there's no pending state check, trigger one */
      if (priv->pending_cb_id == 0) {
        GST_LOG_OBJECT (pl, "Scheduling state update from state %d",
            priv->state);
        schedule_state_update (pl, priv);
      }
      break;
    case PLAYLIST_LOADER_STATE_WAITING:
      /* Waiting for the next time to load a live playlist, but the playlist has changed so
       * cancel that and trigger a new one */
      g_assert (priv->pending_cb_id != 0);
      gst_adaptive_demux_loop_cancel_call (priv->scheduler_task,
          priv->pending_cb_id);
      priv->pending_cb_id = 0;
      schedule_state_update (pl, priv);
      break;
  }
}

/* Check that the current playlist matches the target URI, and return
 * TRUE if so */
gboolean
gst_hls_demux_playlist_loader_has_current_uri (GstHLSDemuxPlaylistLoader * pl,
    const gchar * target_playlist_uri)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (target_playlist_uri == NULL)
    target_playlist_uri = priv->target_playlist_uri;

  if (priv->current_playlist == NULL
      || !g_str_equal (target_playlist_uri, priv->current_playlist_uri))
    return FALSE;

  return TRUE;
}

enum PlaylistDownloadParamFlags
{
  PLAYLIST_DOWNLOAD_FLAG_SKIP_V1 = (1 << 0),
  PLAYLIST_DOWNLOAD_FLAG_SKIP_V2 = (1 << 1),    /* V2 also skips date-ranges */
  PLAYLIST_DOWNLOAD_FLAG_BLOCKING_REQUEST = (1 << 2),
};

struct PlaylistDownloadParams
{
  enum PlaylistDownloadParamFlags flags;
  gint64 next_msn, next_part;
};

#define HLS_SKIP_QUERY_KEY "_HLS_skip"
#define HLS_MSN_QUERY_KEY "_HLS_msn"
#define HLS_PART_QUERY_KEY "_HLS_part"

static gchar *
remove_HLS_directives_from_uri (const gchar * playlist_uri)
{

  /* Catch the simple case and keep NULL as NULL */
  if (playlist_uri == NULL)
    return NULL;

  GstUri *uri = gst_uri_from_string (playlist_uri);
  gst_uri_remove_query_key (uri, HLS_SKIP_QUERY_KEY);
  gst_uri_remove_query_key (uri, HLS_MSN_QUERY_KEY);
  gst_uri_remove_query_key (uri, HLS_PART_QUERY_KEY);

  GList *keys = gst_uri_get_query_keys (uri);
  if (keys)
    keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);
  gchar *out_uri = gst_uri_to_string_with_keys (uri, keys);
  gst_uri_unref (uri);

  return out_uri;
}

static gchar *
apply_directives_to_uri (GstHLSDemuxPlaylistLoader * pl,
    const gchar * playlist_uri, struct PlaylistDownloadParams *dl_params)
{
  /* Short-circuit URI parsing if nothing will change */
  if (dl_params->flags == 0)
    return g_strdup (playlist_uri);

  GstUri *uri = gst_uri_from_string (playlist_uri);

  if (dl_params->flags & PLAYLIST_DOWNLOAD_FLAG_SKIP_V1) {
    GST_LOG_OBJECT (pl, "Doing HLS skip (v1) request");
    gst_uri_set_query_value (uri, HLS_SKIP_QUERY_KEY, "YES");
  } else if (dl_params->flags & PLAYLIST_DOWNLOAD_FLAG_SKIP_V2) {
    GST_LOG_OBJECT (pl, "Doing HLS skip (v2) request");
    gst_uri_set_query_value (uri, HLS_SKIP_QUERY_KEY, "v2");
  } else {
    gst_uri_remove_query_key (uri, HLS_SKIP_QUERY_KEY);
  }

  if (dl_params->flags & PLAYLIST_DOWNLOAD_FLAG_BLOCKING_REQUEST
      && dl_params->next_msn != -1) {
    GST_LOG_OBJECT (pl,
        "Doing HLS blocking request for URI %s with MSN %" G_GINT64_FORMAT
        " part %" G_GINT64_FORMAT, playlist_uri, dl_params->next_msn,
        dl_params->next_part);

    gchar *next_msn_str =
        g_strdup_printf ("%" G_GINT64_FORMAT, dl_params->next_msn);
    gst_uri_set_query_value (uri, HLS_MSN_QUERY_KEY, next_msn_str);
    g_free (next_msn_str);

    if (dl_params->next_part != -1) {
      gchar *next_part_str =
          g_strdup_printf ("%" G_GINT64_FORMAT, dl_params->next_part);
      gst_uri_set_query_value (uri, HLS_PART_QUERY_KEY, next_part_str);
      g_free (next_part_str);
    } else {
      gst_uri_remove_query_key (uri, HLS_PART_QUERY_KEY);
    }
  } else {
    gst_uri_remove_query_key (uri, HLS_MSN_QUERY_KEY);
    gst_uri_remove_query_key (uri, HLS_PART_QUERY_KEY);
  }

  /* Produce the resulting URI with query arguments in UTF-8 order
   * as required by the HLS spec:
   * `Clients using Delivery Directives (Section 6.2.5) MUST ensure that
   * all query parameters appear in UTF-8 order within the URI.`
   */
  GList *keys = gst_uri_get_query_keys (uri);
  if (keys)
    keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);
  gchar *out_uri = gst_uri_to_string_with_keys (uri, keys);
  gst_uri_unref (uri);

  return out_uri;
}

static GstClockTime
get_playlist_reload_interval (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv, GstHLSMediaPlaylist * playlist)
{
  if (playlist == NULL)
    return GST_CLOCK_TIME_NONE; /* No playlist yet */

  /* Use the most recent segment (or part segment) duration, as per
   * https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-11#section-6.3.4
   */
  GstClockTime target_duration = GST_CLOCK_TIME_NONE;
  GstClockTime min_reload_interval = playlist->targetduration / 2;

  if (playlist->segments->len) {
    GstM3U8MediaSegment *last_seg =
        g_ptr_array_index (playlist->segments, playlist->segments->len - 1);

    if (last_seg->partial_segments) {
      GstM3U8PartialSegment *last_part =
          g_ptr_array_index (last_seg->partial_segments,
          last_seg->partial_segments->len - 1);

      target_duration = last_part->duration;
      if (GST_CLOCK_TIME_IS_VALID (playlist->partial_targetduration)) {
        min_reload_interval = playlist->partial_targetduration / 2;
      } else {
        min_reload_interval = target_duration / 2;
      }
    } else {
      target_duration = last_seg->duration;
      min_reload_interval = target_duration / 2;
    }
  } else if (GST_CLOCK_TIME_IS_VALID (playlist->partial_targetduration)) {
    target_duration = playlist->partial_targetduration;
    min_reload_interval = target_duration / 2;
  } else if (playlist->version > 5) {
    target_duration = playlist->targetduration;
  }

  if (playlist->reloaded && target_duration > min_reload_interval) {
    GST_DEBUG_OBJECT (pl,
        "Playlist didn't change previously, returning lower update interval (%"
        GST_TIME_FORMAT " -> %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (target_duration), GST_TIME_ARGS (min_reload_interval));
    target_duration = min_reload_interval;
  }

  GST_DEBUG_OBJECT (pl, "Returning target duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (target_duration));

  return target_duration;
}

static void
handle_download_error (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv)
{
  if (++priv->download_error_count > MAX_DOWNLOAD_ERROR_COUNT) {
    GST_DEBUG_OBJECT (pl,
        "Reached %d download failures on URI %s. Reporting the failure",
        priv->download_error_count, priv->loading_playlist_uri);
    if (priv->error_cb)
      priv->error_cb (pl, priv->loading_playlist_uri, priv->userdata);
  }

  /* The error callback may have provided a new playlist to load, which
   * will have scheduled a state update immediately. In that case,
   * don't trigger our own delayed retry */
  if (priv->pending_cb_id == 0)
    schedule_next_playlist_load (pl, priv, 100 * GST_MSECOND);
}

static void
on_download_complete (DownloadRequest * download, DownloadRequestState state,
    GstHLSDemuxPlaylistLoader * pl)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (priv->state != PLAYLIST_LOADER_STATE_LOADING) {
    GST_DEBUG_OBJECT (pl, "Loader state changed to %d. Aborting", priv->state);
    return;
  }

  if (!g_str_equal (priv->target_playlist_uri, priv->loading_playlist_uri)) {
    /* This callback happened just as the playlist URI was updated. There should be
     * a pending state update scheduled, but we can just kick off the new download
     * immediately */
    GST_DEBUG_OBJECT (pl,
        "Target playlist URI changed from %s to %s. Discarding download",
        priv->loading_playlist_uri, priv->target_playlist_uri);
    start_playlist_download (pl, priv);
    return;
  }

  GST_DEBUG_OBJECT (pl, "Handling completed playlist download for URI %s",
      download->uri);

  /* If we got a permanent redirect, use that as the new
   * playlist URI, otherwise set the base URI of the playlist
   * to the redirect target if any (NULL if there was no redirect) */
  GstHLSMediaPlaylist *playlist = NULL;
  gchar *base_uri, *uri;

  if (download->redirect_uri) {
    /* Strip HLS request params from the playlist and redirect URI */
    uri = remove_HLS_directives_from_uri (download->redirect_uri);
    base_uri = NULL;

    if (download->redirect_permanent) {
      /* Store this redirect as the future request URI for this playlist */
      g_free (priv->current_playlist_redirect_uri);
      priv->current_playlist_redirect_uri = g_strdup (uri);
    }
  } else {
    /* Strip HLS request params from the playlist and redirect URI */
    uri = remove_HLS_directives_from_uri (download->uri);
    base_uri = remove_HLS_directives_from_uri (download->redirect_uri);
  }

  /* Calculate the newest time we know this playlist was valid to store on the HLS Media Playlist */
  GstClockTime playlist_ts =
      MAX (0, GST_CLOCK_DIFF (download_request_get_age (download),
          download->download_start_time));

  GstBuffer *buf = download_request_take_buffer (download);

  /* there should be a buf if there wasn't an error (handled above) */
  g_assert (buf);

  gchar *playlist_data = gst_hls_buf_to_utf8_text (buf);
  gst_buffer_unref (buf);

  if (playlist_data == NULL) {
    GST_WARNING_OBJECT (pl, "Couldn't validate playlist encoding");
    goto error_retry_out;
  }

  GstHLSMediaPlaylist *current_playlist = priv->current_playlist;
  gboolean playlist_uri_change = (current_playlist == NULL
      || g_strcmp0 (priv->loading_playlist_uri,
          priv->current_playlist_uri) != 0);

  gboolean always_reload = FALSE;
#if 0
  /* Test code the reports a playlist load error if we load
     the same playlist 2 times in a row and the URI contains "video.m3u8"
     https://playertest.longtailvideo.com/adaptive/elephants_dream_v4/redundant.m3u8
     works as a test URL
   */
  static gint playlist_load_counter = 0;
  if (playlist_uri_change)
    playlist_load_counter = 0;
  else if (strstr (priv->loading_playlist_uri, "video.m3u8") != NULL) {
    playlist_load_counter++;
    if (playlist_load_counter > 1) {
      g_print ("Triggering playlist failure for %s\n",
          priv->loading_playlist_uri);
      goto error_retry_out;
    }
  }
  always_reload = TRUE;
#endif

  if (!playlist_uri_change && current_playlist
      && gst_hls_media_playlist_has_same_data (current_playlist,
          playlist_data)) {
    GST_DEBUG_OBJECT (pl, "playlist data was unchanged");
    playlist = gst_hls_media_playlist_ref (current_playlist);
    playlist->reloaded = TRUE;
    playlist->request_time = GST_CLOCK_TIME_NONE;
    g_free (playlist_data);
  } else {
    playlist =
        gst_hls_media_playlist_parse (playlist_data, playlist_ts, uri,
        base_uri);
    if (!playlist) {
      GST_WARNING_OBJECT (pl, "Couldn't parse playlist");
      goto error_retry_out;
    }
    playlist->request_time = download->download_request_time;
  }

  /* Transfer over any skipped segments from the current playlist if
   * we did a delta playlist update */
  if (!playlist_uri_change && current_playlist && playlist
      && playlist->skipped_segments > 0) {
    if (!gst_hls_media_playlist_sync_skipped_segments (playlist,
            current_playlist)) {
      GST_DEBUG_OBJECT (pl,
          "Could not merge delta update to playlist. Retrying with full request");

      gst_hls_media_playlist_unref (playlist);

      /* Delta playlist update failed. Load a full playlist */
      priv->delta_merge_failed = TRUE;
      start_playlist_download (pl, priv);
      goto out;
    }
  }

  g_free (priv->current_playlist_uri);
  if (priv->current_playlist)
    gst_hls_media_playlist_unref (priv->current_playlist);

  priv->current_playlist_uri = g_strdup (priv->loading_playlist_uri);
  priv->current_playlist = playlist;

  /* Successfully loaded the playlist. Forget any prior failures */
  priv->download_error_count = 0;

  if (priv->success_cb)
    priv->success_cb (pl, priv->current_playlist_uri, priv->current_playlist,
        priv->userdata);

  g_free (priv->loading_playlist_uri);
  priv->loading_playlist_uri = NULL;

  if (gst_hls_media_playlist_is_live (playlist) || always_reload) {
    /* Schedule the next playlist load. If we can do a blocking load,
     * do it immediately, otherwise delayed */
    if (playlist->can_block_reload) {
      start_playlist_download (pl, priv);
    } else {
      GstClockTime delay = get_playlist_reload_interval (pl, priv, playlist);
      schedule_next_playlist_load (pl, priv, delay);
    }
  } else {
    GST_LOG_OBJECT (pl, "Playlist is not live. Not scheduling a reload");
    /* Go back to the starting state until/if the playlist uri is updated */
    priv->state = PLAYLIST_LOADER_STATE_STARTING;
  }

out:
  g_free (uri);
  g_free (base_uri);
  return;

error_retry_out:
  /* Got invalid playlist data, retry soon or error out */
  handle_download_error (pl, priv);
  goto out;
}

static void
on_download_error (DownloadRequest * download, DownloadRequestState state,
    GstHLSDemuxPlaylistLoader * pl)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  if (priv->state != PLAYLIST_LOADER_STATE_LOADING) {
    GST_DEBUG_OBJECT (pl, "Loader state changed to %d. Aborting", priv->state);
    return;
  }

  GST_WARNING_OBJECT (pl,
      "Couldn't retrieve playlist, got HTTP status code %d",
      download->status_code);

  handle_download_error (pl, priv);
}

static void
start_playlist_download (GstHLSDemuxPlaylistLoader * pl,
    GstHLSDemuxPlaylistLoaderPrivate * priv)
{
  gboolean allow_skip = !priv->delta_merge_failed;
  const gchar *orig_uri = priv->target_playlist_uri;

  /* Can't download yet */
  if (orig_uri == NULL)
    return;

  struct PlaylistDownloadParams dl_params;
  memset (&dl_params, 0, sizeof (struct PlaylistDownloadParams));

  GstHLSMediaPlaylist *current_playlist = priv->current_playlist;

  /* If there's no previous playlist, or the URI changed this
   * is not a refresh/update but a switch to a new playlist */
  gboolean playlist_uri_change = (current_playlist == NULL
      || g_strcmp0 (orig_uri, priv->current_playlist_uri) != 0);

  if (!playlist_uri_change) {
    GST_LOG_OBJECT (pl, "Updating the playlist");

    /* If we have a redirect stored for this playlist URI, use that instead */
    if (priv->current_playlist_redirect_uri) {
      orig_uri = priv->current_playlist_redirect_uri;
      GST_LOG_OBJECT (pl, "Using redirected playlist URI %s", orig_uri);
    }

    /* See if we can do a delta playlist update (if the playlist age is less than
     * one half of the Skip Boundary */
    if (GST_CLOCK_TIME_IS_VALID (current_playlist->skip_boundary) && allow_skip) {
      GstClockTime now = gst_adaptive_demux2_get_monotonic_time (priv->demux);
      GstClockTimeDiff playlist_age =
          GST_CLOCK_DIFF (current_playlist->playlist_ts, now);

      if (GST_CLOCK_TIME_IS_VALID (current_playlist->playlist_ts) &&
          playlist_age <= current_playlist->skip_boundary / 2) {
        if (current_playlist->can_skip_dateranges) {
          dl_params.flags |= PLAYLIST_DOWNLOAD_FLAG_SKIP_V2;
        } else {
          dl_params.flags |= PLAYLIST_DOWNLOAD_FLAG_SKIP_V1;
        }
      }
    } else if (GST_CLOCK_TIME_IS_VALID (current_playlist->skip_boundary)) {
      GST_DEBUG_OBJECT (pl,
          "Doing full playlist update after failed delta request");
    }
  } else {
    /* This is the first time loading this playlist URI, clear the error counter
     * and redirect URI */
    priv->download_error_count = 0;
    g_free (priv->current_playlist_redirect_uri);
    priv->current_playlist_redirect_uri = NULL;
  }

  /* Blocking playlist reload check */
  if (current_playlist != NULL && current_playlist->can_block_reload) {
    if (playlist_uri_change) {
      /* FIXME: We're changing playlist, but if there's a EXT-X-RENDITION-REPORT
       * for the new playlist we might be able to use it to do a blocking request */
    } else {
      /* Get the next MSN (and/or possibly part number) for the request params */
      gst_hls_media_playlist_get_next_msn_and_part (current_playlist,
          &dl_params.next_msn, &dl_params.next_part);
      dl_params.flags |= PLAYLIST_DOWNLOAD_FLAG_BLOCKING_REQUEST;
    }
  }

  gchar *target_uri = apply_directives_to_uri (pl, orig_uri, &dl_params);

  if (priv->download_request == NULL) {
    priv->download_request = download_request_new_uri (target_uri);

    download_request_set_callbacks (priv->download_request,
        (DownloadRequestEventCallback) on_download_complete,
        (DownloadRequestEventCallback) on_download_error,
        (DownloadRequestEventCallback) NULL,
        (DownloadRequestEventCallback) NULL, pl);
  } else {
    download_request_set_uri (priv->download_request, target_uri, 0, -1);
  }

  GST_DEBUG_OBJECT (pl, "Submitting playlist download request for URI %s",
      target_uri);
  g_free (target_uri);

  g_free (priv->loading_playlist_uri);
  priv->loading_playlist_uri = g_strdup (orig_uri);
  priv->state = PLAYLIST_LOADER_STATE_LOADING;

  if (!downloadhelper_submit_request (priv->download_helper,
          NULL, DOWNLOAD_FLAG_COMPRESS | DOWNLOAD_FLAG_FORCE_REFRESH,
          priv->download_request, NULL)) {
    /* Failed to submit the download - could be invalid URI, but
     * could just mean the download helper was stopped */
    priv->state = PLAYLIST_LOADER_STATE_STOPPED;
  }
}

static gboolean
gst_hls_demux_playlist_loader_update (GstHLSDemuxPlaylistLoader * pl)
{
  GstHLSDemuxPlaylistLoaderPrivate *priv = pl->priv;

  GST_LOG_OBJECT (pl, "Updating at state %d", priv->state);
  priv->pending_cb_id = 0;

  switch (priv->state) {
    case PLAYLIST_LOADER_STATE_STOPPED:
      break;
    case PLAYLIST_LOADER_STATE_STARTING:
      if (priv->target_playlist_uri)
        start_playlist_download (pl, priv);
      break;
    case PLAYLIST_LOADER_STATE_LOADING:
      /* A download is in progress, but if we reach here it's
       * because the target playlist URI got updated, so check
       * for cancelling the current download. */
      if (g_str_equal (priv->target_playlist_uri, priv->current_playlist_uri))
        break;

      /* A download is in progress. Cancel it and trigger a new one */
      if (priv->download_request) {
        GST_DEBUG_OBJECT (pl,
            "Playlist URI changed from %s to %s. Cancelling current download",
            priv->target_playlist_uri, priv->current_playlist_uri);
        downloadhelper_cancel_request (priv->download_helper,
            priv->download_request);
        download_request_unref (priv->download_request);
        priv->download_request = NULL;
      }
      start_playlist_download (pl, priv);
      break;
    case PLAYLIST_LOADER_STATE_WAITING:
      /* We were waiting until time to load a playlist. Load it now */
      start_playlist_download (pl, priv);
      break;
  }

  return G_SOURCE_REMOVE;
}
