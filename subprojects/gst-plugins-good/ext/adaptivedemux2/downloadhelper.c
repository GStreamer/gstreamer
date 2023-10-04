/* GStreamer
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
 *
 * downloadhelper.c:
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
 * Youshould have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "downloadhelper.h"
#include "../soup/gstsouploader.h"

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

#define CHUNK_BUFFER_SIZE 32768

typedef struct DownloadHelperTransfer DownloadHelperTransfer;

struct DownloadHelper
{
  GThread *transfer_thread;

  gboolean running;

  GstAdaptiveDemuxClock *clock;

  GMainContext *transfer_context;
  GMainLoop *loop;
  SoupSession *session;

  GMutex transfer_lock;
  GArray *active_transfers;

  GAsyncQueue *transfer_requests;
  GSource *transfer_requests_source;

  gchar *referer;
  gchar *user_agent;
  gchar **cookies;
};

struct DownloadHelperTransfer
{
  DownloadHelper *dh;

  gboolean blocking;
  gboolean complete;
  gboolean progress_pending;

  GCond cond;

  GCancellable *cancellable;

  SoupMessage *msg;
  gboolean request_sent;

  /* Current read buffer */
  char *read_buffer;
  guint64 read_buffer_size;
  guint64 read_position;        /* Start in bytes of the read_buffer */

  DownloadRequest *request;
};

static void
free_transfer (DownloadHelperTransfer * transfer)
{
  DownloadRequest *request = transfer->request;

  if (request)
    download_request_unref (request);

  if (transfer->blocking)
    g_cond_clear (&transfer->cond);

  g_object_unref (transfer->msg);
  g_free (transfer->read_buffer);
  g_free (transfer);
}

static void
transfer_completion_cb (gpointer src_object, GAsyncResult * res,
    gpointer user_data)
{
  DownloadHelperTransfer *transfer = g_task_get_task_data ((GTask *) res);
  DownloadRequest *request = transfer->request;

  if (transfer->blocking)
    return;

  download_request_lock (request);
  request->in_use = FALSE;
  GST_LOG ("Despatching completion for transfer %p request %p", transfer,
      request);
  download_request_despatch_completion (request);
  download_request_unlock (request);
}

static gboolean
transfer_report_progress_cb (gpointer task)
{
  DownloadHelperTransfer *transfer;
  DownloadRequest *request;

  /* Already completed - late callback */
  if (g_task_get_completed (task))
    return FALSE;

  transfer = g_task_get_task_data (task);
  request = transfer->request;

  download_request_lock (request);
  if (request->send_progress) {
    GST_LOG ("Despatching progress for transfer %p request %p", transfer,
        request);
    download_request_despatch_progress (request);
  }
  transfer->progress_pending = FALSE;
  download_request_unlock (request);

  return FALSE;
}

static GTask *
transfer_task_new (DownloadHelper * dh, DownloadRequest * request,
    SoupMessage * msg, gboolean blocking)
{
  GTask *transfer_task = NULL;
  DownloadHelperTransfer *transfer = g_new0 (DownloadHelperTransfer, 1);

  transfer->blocking = blocking;
  if (transfer->blocking)
    g_cond_init (&transfer->cond);

  transfer->cancellable = g_cancellable_new ();
  transfer->request = download_request_ref (request);

  transfer->dh = dh;
  transfer->msg = msg;

  transfer_task =
      g_task_new (NULL, transfer->cancellable,
      (GAsyncReadyCallback) transfer_completion_cb, NULL);
  g_task_set_task_data (transfer_task, transfer,
      (GDestroyNotify) free_transfer);

  return transfer_task;
}

static void
release_transfer_task_by_ref (GTask ** transfer_task)
{
  g_object_unref (*transfer_task);
}

/* Called with download_request lock held */
static void
transfer_task_report_progress (GTask * transfer_task)
{
  DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);
  DownloadRequest *request = transfer->request;
  GSource *idle_source;

  if (transfer->progress_pending == TRUE || !request->send_progress)
    return;

  /* There's no progress cb pending and this download wants reports, so
   * attach an idle source */
  transfer->progress_pending = TRUE;
  idle_source = g_idle_source_new ();
  g_task_attach_source (transfer_task, idle_source,
      transfer_report_progress_cb);
  g_source_unref (idle_source);
}

static void
finish_transfer_task (DownloadHelper * dh, GTask * transfer_task,
    GError * error)
{
  int i;

  g_mutex_lock (&dh->transfer_lock);
  for (i = dh->active_transfers->len - 1; i >= 0; i--) {
    if (transfer_task == g_array_index (dh->active_transfers, GTask *, i)) {
      DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);

      transfer->complete = TRUE;

      if (transfer->blocking)
        g_cond_broadcast (&transfer->cond);

      if (error != NULL)
        g_task_return_error (transfer_task, error);
      else
        g_task_return_boolean (transfer_task, TRUE);

      /* This drops the task ref: */
      g_array_remove_index_fast (dh->active_transfers, i);
      g_mutex_unlock (&dh->transfer_lock);
      return;
    }
  }
  g_mutex_unlock (&dh->transfer_lock);

  GST_WARNING ("Did not find transfer %p in the active transfer list",
      transfer_task);
}

static gboolean
new_read_buffer (DownloadHelperTransfer * transfer)
{
  gint buffer_size = CHUNK_BUFFER_SIZE;
#if 0
  DownloadRequest *request = transfer->request;

  if (request->range_end != -1) {
    if (request->range_end <= transfer->read_position) {
      transfer->read_buffer = NULL;
      transfer->read_buffer_size = 0;
      return FALSE;
    }
    if (request->range_end - transfer->read_position < buffer_size)
      buffer_size = request->range_end - transfer->read_position + 1;
  }
#endif

  transfer->read_buffer = g_new (char, buffer_size);
  transfer->read_buffer_size = buffer_size;
  return TRUE;
}

static void
on_read_ready (GObject * source, GAsyncResult * result, gpointer user_data)
{
  GTask *transfer_task = user_data;
  DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);

  DownloadHelper *dh = transfer->dh;
  DownloadRequest *request = transfer->request;

  GInputStream *in = G_INPUT_STREAM (source);
  GError *error = NULL;
  gsize bytes_read = 0;

  GstClockTime now = gst_adaptive_demux_clock_get_time (dh->clock);

  gboolean read_failed =
      g_input_stream_read_all_finish (in, result, &bytes_read, &error);

  download_request_lock (request);

  if (error) {
    g_free (transfer->read_buffer);
    transfer->read_buffer = NULL;

    if (!g_cancellable_is_cancelled (transfer->cancellable)) {
      GST_ERROR ("Failed to read stream: %s", error->message);
      if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED)
        request->state = DOWNLOAD_REQUEST_STATE_ERROR;
      finish_transfer_task (dh, transfer_task, error);
    } else {
      /* Ignore error from cancelled operation */
      g_error_free (error);
      finish_transfer_task (dh, transfer_task, NULL);
    }
    download_request_unlock (request);

    return;
  }

  if (bytes_read > 0) {
    GstBuffer *gst_buffer =
        gst_buffer_new_wrapped (transfer->read_buffer, bytes_read);

    GST_BUFFER_OFFSET (gst_buffer) = transfer->read_position;
    transfer->read_position += bytes_read;
    transfer->read_buffer = NULL;

    /* Clip the buffer to within the range */
    if (GST_BUFFER_OFFSET (gst_buffer) < request->range_start) {
      if (transfer->read_position <= request->range_start) {
        GST_DEBUG ("Discarding %" G_GSIZE_FORMAT
            " bytes entirely before requested range",
            gst_buffer_get_size (gst_buffer));
        /* This buffer is completely before the range start, discard it */
        gst_buffer_unref (gst_buffer);
        gst_buffer = NULL;
      } else {
        GST_DEBUG ("Clipping first %" G_GINT64_FORMAT
            " bytes before requested range",
            request->range_start - GST_BUFFER_OFFSET (gst_buffer));

        /* This buffer is partially within the requested range, clip the beginning */
        gst_buffer_resize (gst_buffer,
            request->range_start - GST_BUFFER_OFFSET (gst_buffer), -1);
        GST_BUFFER_OFFSET (gst_buffer) = request->range_start;
      }
    }

    if (gst_buffer != NULL) {
      /* Don't override CANCELLED state. Otherwise make sure it is LOADING */
      if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED)
        request->state = DOWNLOAD_REQUEST_STATE_LOADING;

      if (request->download_start_time == GST_CLOCK_TIME_NONE) {
        GST_LOG ("Got first data for URI %s", request->uri);
        request->download_start_time = now;
      }
      request->download_newest_data_time = now;

      GST_LOG ("Adding %u bytes to buffer (request URI %s)",
          (guint) (gst_buffer_get_size (gst_buffer)), request->uri);

      download_request_add_buffer (request, gst_buffer);

      transfer_task_report_progress (transfer_task);
    }
  } else if (read_failed) {
    /* The read failed and returned 0 bytes: We're done */
    goto finish_transfer;
  }

  /* Resubmit the read request to get more */
  if (!new_read_buffer (transfer))
    goto finish_transfer;

  g_main_context_push_thread_default (dh->transfer_context);
  g_input_stream_read_all_async (in, transfer->read_buffer,
      transfer->read_buffer_size, G_PRIORITY_DEFAULT, transfer->cancellable,
      on_read_ready, transfer_task);
  g_main_context_pop_thread_default (dh->transfer_context);

  download_request_unlock (request);
  return;

finish_transfer:
  if (request->in_use && !g_cancellable_is_cancelled (transfer->cancellable)) {
    SoupStatus status_code = _soup_message_get_status (transfer->msg);
#ifndef GST_DISABLE_GST_DEBUG
    guint download_ms = (now - request->download_request_time) / GST_MSECOND;
    GST_LOG ("request complete in %u ms. Code %d URI %s range %" G_GINT64_FORMAT
        " %" G_GINT64_FORMAT, download_ms, status_code,
        request->uri, request->range_start, request->range_end);
#endif

    if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED) {
      if (SOUP_STATUS_IS_SUCCESSFUL (status_code)
          || SOUP_STATUS_IS_REDIRECTION (status_code)) {
        request->state = DOWNLOAD_REQUEST_STATE_COMPLETE;
      } else {
        request->state = DOWNLOAD_REQUEST_STATE_ERROR;
      }
    }
  }
  request->download_end_time = now;

  g_free (transfer->read_buffer);
  transfer->read_buffer = NULL;

  download_request_unlock (request);

  finish_transfer_task (dh, transfer_task, NULL);
}

static void
http_header_to_structure (const gchar * name, const gchar * value,
    gpointer user_data)
{
  GstStructure *headers = user_data;
  const GValue *gv;

  if (!g_utf8_validate (name, -1, NULL) || !g_utf8_validate (value, -1, NULL))
    return;

  gv = gst_structure_get_value (headers, name);
  if (gv && GST_VALUE_HOLDS_ARRAY (gv)) {
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    gst_value_array_append_value ((GValue *) gv, &v);
    g_value_unset (&v);
  } else if (gv && G_VALUE_HOLDS_STRING (gv)) {
    GValue arr = G_VALUE_INIT;
    GValue v = G_VALUE_INIT;
    const gchar *old_value = g_value_get_string (gv);

    g_value_init (&arr, GST_TYPE_ARRAY);
    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, old_value);
    gst_value_array_append_value (&arr, &v);
    g_value_set_string (&v, value);
    gst_value_array_append_value (&arr, &v);

    gst_structure_set_value (headers, name, &arr);
    g_value_unset (&v);
    g_value_unset (&arr);
  } else {
    gst_structure_set (headers, name, G_TYPE_STRING, value, NULL);
  }
}

static void
soup_msg_restarted_cb (SoupMessage * msg, gpointer user_data)
{
  GTask *transfer_task = user_data;
  DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);
  DownloadRequest *request = transfer->request;
  SoupStatus status = _soup_message_get_status (msg);

  if (SOUP_STATUS_IS_REDIRECTION (status)) {
    char *redirect_uri = gst_soup_message_uri_to_string (msg);
    gboolean redirect_permanent = (status == SOUP_STATUS_MOVED_PERMANENTLY);

    GST_DEBUG ("%u redirect to \"%s\" (permanent %d)",
        status, redirect_uri, redirect_permanent);

    download_request_lock (request);
    g_free (request->redirect_uri);
    request->redirect_uri = redirect_uri;
    request->redirect_permanent = redirect_permanent;
    download_request_unlock (request);
  }
}

static GstStructure *
handle_response_headers (DownloadHelperTransfer * transfer)
{
  DownloadRequest *request = transfer->request;
  SoupMessage *msg = transfer->msg;
  SoupMessageHeaders *response_headers;
  GstStructure *http_headers, *headers;

  http_headers = gst_structure_new_empty ("http-headers");

#if 0
  if (msg->status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED &&
      src->proxy_id && src->proxy_pw) {
    /* wait for authenticate callback */
    return GST_FLOW_OK;
  }

  if (src->redirection_uri)
    gst_structure_set (http_headers, "redirection-uri", G_TYPE_STRING,
        src->redirection_uri, NULL);
#endif

  headers = gst_structure_new_empty ("request-headers");
  _soup_message_headers_foreach (_soup_message_get_request_headers (msg),
      http_header_to_structure, headers);
  gst_structure_set (http_headers, "request-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);
  headers = gst_structure_new_empty ("response-headers");
  response_headers = _soup_message_get_response_headers (msg);
  _soup_message_headers_foreach (response_headers, http_header_to_structure,
      headers);
  gst_structure_set (http_headers, "response-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);

#if 0
  if (msg->status_code == SOUP_STATUS_UNAUTHORIZED) {
    /* force an error */
    gst_structure_free (http_headers);
    return gst_soup_http_src_parse_status (msg, src);
  }
#endif

  /* Parse Content-Length. */
  if (SOUP_STATUS_IS_SUCCESSFUL (_soup_message_get_status (msg)) &&
      (_soup_message_headers_get_encoding (response_headers) ==
          SOUP_ENCODING_CONTENT_LENGTH)) {
    request->content_length =
        _soup_message_headers_get_content_length (response_headers);
  }
  /* Parse Content-Range in a partial content response to set our initial read_position */
  transfer->read_position = 0;
  if (_soup_message_get_status (msg) == SOUP_STATUS_PARTIAL_CONTENT) {
    goffset start, end;
    if (_soup_message_headers_get_content_range (response_headers, &start,
            &end, NULL)) {
      GST_DEBUG ("Content-Range response %" G_GOFFSET_FORMAT "-%"
          G_GOFFSET_FORMAT, start, end);
      transfer->read_position = start;
    }
  }
  if (transfer->read_position != request->range_start) {
    GST_WARNING ("Server did not respect our range request for range %"
        G_GINT64_FORMAT " to %" G_GINT64_FORMAT " - starting at offset %"
        G_GUINT64_FORMAT, request->range_start, request->range_end,
        transfer->read_position);
  }

  return http_headers;
}

static void
on_request_sent (GObject * source, GAsyncResult * result, gpointer user_data)
{
  GTask *transfer_task = user_data;
  DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);

  DownloadHelper *dh = transfer->dh;
  DownloadRequest *request = transfer->request;
  SoupMessage *msg = transfer->msg;
  GError *error = NULL;

  GInputStream *in =
      _soup_session_send_finish ((SoupSession *) source, result, &error);

  download_request_lock (request);

  if (in == NULL) {
    request->status_code = _soup_message_get_status (msg);

    if (!g_cancellable_is_cancelled (transfer->cancellable)) {
      GST_LOG ("request errored. Code %d URI %s range %" G_GINT64_FORMAT " %"
          G_GINT64_FORMAT, request->status_code, request->uri,
          request->range_start, request->range_end);

      if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED)
        request->state = DOWNLOAD_REQUEST_STATE_ERROR;
      finish_transfer_task (dh, transfer_task, error);
    } else {
      /* Ignore error from cancelled operation */
      g_error_free (error);
      finish_transfer_task (dh, transfer_task, NULL);
    }
    download_request_unlock (request);

    /* No async callback queued - the transfer is done */
    finish_transfer_task (dh, transfer_task, error);
    return;
  }

  /* If the state is cancelled don't override it */
  if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED &&
      request->state != DOWNLOAD_REQUEST_STATE_HEADERS_RECEIVED) {

    request->state = DOWNLOAD_REQUEST_STATE_HEADERS_RECEIVED;
    request->status_code = _soup_message_get_status (msg);
    request->headers = handle_response_headers (transfer);
    GST_TRACE ("request URI %s range %" G_GINT64_FORMAT " %"
        G_GINT64_FORMAT " headers: %" GST_PTR_FORMAT,
        request->uri, request->range_start, request->range_end,
        request->headers);

    if (SOUP_STATUS_IS_SUCCESSFUL (request->status_code)
        || SOUP_STATUS_IS_REDIRECTION (request->status_code)) {
      request->state = DOWNLOAD_REQUEST_STATE_HEADERS_RECEIVED;
      transfer_task_report_progress (transfer_task);
    } else {
      goto finish_transfer_error;
    }
  }

  if (!new_read_buffer (transfer))
    goto finish_transfer_error;

  download_request_unlock (request);

  g_main_context_push_thread_default (dh->transfer_context);
  g_input_stream_read_all_async (in, transfer->read_buffer,
      transfer->read_buffer_size, G_PRIORITY_DEFAULT, transfer->cancellable,
      on_read_ready, transfer_task);
  g_main_context_pop_thread_default (dh->transfer_context);

  g_object_unref (in);
  return;

finish_transfer_error:
  request->download_end_time = gst_adaptive_demux_clock_get_time (dh->clock);

  if (request->in_use && !g_cancellable_is_cancelled (transfer->cancellable)) {
    GST_LOG ("request complete. Code %d URI %s range %" G_GINT64_FORMAT " %"
        G_GINT64_FORMAT, _soup_message_get_status (msg), request->uri,
        request->range_start, request->range_end);

    /* If the state is cancelled don't override it */
    if (request->state != DOWNLOAD_REQUEST_STATE_CANCELLED)
      request->state = DOWNLOAD_REQUEST_STATE_ERROR;
  }

  g_free (transfer->read_buffer);
  transfer->read_buffer = NULL;

  download_request_unlock (request);
  finish_transfer_task (dh, transfer_task, NULL);
  g_object_unref (in);
}

DownloadHelper *
downloadhelper_new (GstAdaptiveDemuxClock * clock)
{
  DownloadHelper *dh = g_new0 (DownloadHelper, 1);

  dh->transfer_context = g_main_context_new ();
  dh->loop = g_main_loop_new (dh->transfer_context, FALSE);

  dh->clock = gst_adaptive_demux_clock_ref (clock);

  g_mutex_init (&dh->transfer_lock);
  dh->active_transfers = g_array_new (FALSE, FALSE, sizeof (GTask *));

  g_array_set_clear_func (dh->active_transfers,
      (GDestroyNotify) (release_transfer_task_by_ref));

  dh->transfer_requests =
      g_async_queue_new_full ((GDestroyNotify) g_object_unref);
  dh->transfer_requests_source = NULL;

  /* libsoup 3.0 (not 2.74 or 3.1) dispatches using a single source attached
   * when the session is created, so we need to ensure it matches here. */
  g_main_context_push_thread_default (dh->transfer_context);

  /* Set 10 second timeout. Any longer is likely
   * an attempt to reuse an already closed connection */
  dh->session = _soup_session_new_with_options ("timeout", 10, NULL);

  g_main_context_pop_thread_default (dh->transfer_context);

  return dh;
}

void
downloadhelper_free (DownloadHelper * dh)
{
  downloadhelper_stop (dh);

  if (dh->session)
    g_object_unref (dh->session);
  g_main_loop_unref (dh->loop);
  g_main_context_unref (dh->transfer_context);

  if (dh->clock)
    gst_adaptive_demux_clock_unref (dh->clock);

  g_array_free (dh->active_transfers, TRUE);
  g_async_queue_unref (dh->transfer_requests);

  g_free (dh->referer);
  g_free (dh->user_agent);
  g_strfreev (dh->cookies);

  g_free (dh);
}

void
downloadhelper_set_referer (DownloadHelper * dh, const gchar * referer)
{
  g_mutex_lock (&dh->transfer_lock);
  g_free (dh->referer);
  dh->referer = g_strdup (referer);
  g_mutex_unlock (&dh->transfer_lock);
}

void
downloadhelper_set_user_agent (DownloadHelper * dh, const gchar * user_agent)
{
  g_mutex_lock (&dh->transfer_lock);
  g_free (dh->user_agent);
  dh->user_agent = g_strdup (user_agent);
  g_mutex_unlock (&dh->transfer_lock);
}

/* Takes ownership of the strv */
void
downloadhelper_set_cookies (DownloadHelper * dh, gchar ** cookies)
{
  g_mutex_lock (&dh->transfer_lock);
  g_strfreev (dh->cookies);
  dh->cookies = cookies;
  g_mutex_unlock (&dh->transfer_lock);
}

/* Called with the transfer lock held */
static void
submit_transfer (DownloadHelper * dh, GTask * transfer_task)
{
  DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);
  DownloadRequest *request = transfer->request;

  download_request_lock (request);
  if (request->state == DOWNLOAD_REQUEST_STATE_CANCELLED) {
    download_request_unlock (request);

    GST_DEBUG ("Don't submit already cancelled transfer");
    return;
  }

  request->state = DOWNLOAD_REQUEST_STATE_OPEN;
  request->download_request_time =
      gst_adaptive_demux_clock_get_time (dh->clock);

  GST_LOG ("Submitting request URI %s range %" G_GINT64_FORMAT " %"
      G_GINT64_FORMAT, request->uri, request->range_start, request->range_end);

  transfer_task_report_progress (transfer_task);
  download_request_unlock (request);

  _soup_session_send_async (dh->session, transfer->msg, transfer->cancellable,
      on_request_sent, transfer_task);
  g_array_append_val (dh->active_transfers, transfer_task);
}

/* Idle callback that submits all pending transfers */
static gboolean
submit_transfers_cb (DownloadHelper * dh)
{
  GTask *transfer;

  g_mutex_lock (&dh->transfer_lock);
  do {
    transfer = g_async_queue_try_pop (dh->transfer_requests);
    if (transfer) {
      submit_transfer (dh, transfer);
    }
  } while (transfer != NULL);

  /* FIXME: Use a PollFD like GWakeup instead? */
  g_source_destroy (dh->transfer_requests_source);
  g_source_unref (dh->transfer_requests_source);
  dh->transfer_requests_source = NULL;

  g_mutex_unlock (&dh->transfer_lock);

  return G_SOURCE_REMOVE;
}

static gpointer
dh_transfer_thread_func (gpointer data)
{
  DownloadHelper *dh = data;
  GST_DEBUG ("DownloadHelper thread starting");

  g_main_context_push_thread_default (dh->transfer_context);
  g_main_loop_run (dh->loop);
  g_main_context_pop_thread_default (dh->transfer_context);

  GST_DEBUG ("Exiting DownloadHelper thread");
  return NULL;
}

gboolean
downloadhelper_start (DownloadHelper * dh)
{
  g_return_val_if_fail (dh->transfer_thread == NULL, FALSE);

  g_mutex_lock (&dh->transfer_lock);
  if (!dh->running) {

    dh->transfer_thread =
        g_thread_try_new ("adaptive-download-task", dh_transfer_thread_func, dh,
        NULL);
    dh->running = (dh->transfer_thread != NULL);
  }
  g_mutex_unlock (&dh->transfer_lock);

  return dh->running;
}

void
downloadhelper_stop (DownloadHelper * dh)
{
  int i;
  GThread *transfer_thread = NULL;

  GST_DEBUG ("Stopping DownloadHelper loop");

  g_mutex_lock (&dh->transfer_lock);

  dh->running = FALSE;

  for (i = 0; i < dh->active_transfers->len; i++) {
    GTask *transfer_task = g_array_index (dh->active_transfers, GTask *, i);
    DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);
    g_cancellable_cancel (transfer->cancellable);
  }

  g_main_loop_quit (dh->loop);

  transfer_thread = dh->transfer_thread;
  dh->transfer_thread = NULL;

  g_mutex_unlock (&dh->transfer_lock);

  if (transfer_thread != NULL) {
    g_thread_join (transfer_thread);
  }

  /* The transfer thread has exited at this point - any remaining transfers are unfinished
   * and need cleaning up */
  g_mutex_lock (&dh->transfer_lock);

  for (i = 0; i < dh->active_transfers->len; i++) {
    GTask *transfer_task = g_array_index (dh->active_transfers, GTask *, i);
    DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);
    DownloadRequest *request = transfer->request;

    download_request_lock (request);
    request->state = DOWNLOAD_REQUEST_STATE_CANCELLED;
    download_request_unlock (request);

    transfer->complete = TRUE;
    if (transfer->blocking)
      g_cond_broadcast (&transfer->cond);

    g_task_return_boolean (transfer_task, TRUE);
  }

  g_array_set_size (dh->active_transfers, 0);
  g_mutex_unlock (&dh->transfer_lock);
}

gboolean
downloadhelper_submit_request (DownloadHelper * dh,
    const gchar * referer, DownloadFlags flags, DownloadRequest * request,
    GError ** err)
{
  GTask *transfer_task = NULL;
  const gchar *method;
  SoupMessage *msg;
  SoupMessageHeaders *msg_headers;
  gboolean blocking = (flags & DOWNLOAD_FLAG_BLOCKING) != 0;

  method =
      (flags & DOWNLOAD_FLAG_HEADERS_ONLY) ? SOUP_METHOD_HEAD : SOUP_METHOD_GET;

  download_request_lock (request);
  if (request->in_use) {
    GST_ERROR ("Request for URI %s reusing active request object",
        request->uri);
    download_request_unlock (request);
    return FALSE;
  }

  /* Clear the state back to unsent */
  request->state = DOWNLOAD_REQUEST_STATE_UNSENT;

  msg = _soup_message_new (method, request->uri);
  if (msg == NULL) {
    g_set_error (err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Could not parse download URI %s", request->uri);

    request->state = DOWNLOAD_REQUEST_STATE_ERROR;
    download_request_unlock (request);

    return FALSE;
  }

  /* NOTE: There was a bug where Akamai servers return the
   * wrong result for a range request on small files. To avoid
   * it if the range starts within the first KB of the file, just
   * start at 0 instead */
  if (request->range_start < 1024)
    request->range_start = 0;

  msg_headers = _soup_message_get_request_headers (msg);

  if (request->range_start != 0 || request->range_end != -1) {
    _soup_message_headers_set_range (msg_headers, request->range_start,
        request->range_end);
  }

  download_request_unlock (request);

  /* If resubmitting a request, clear any stale / unused data */
  download_request_begin_download (request);

  if ((flags & DOWNLOAD_FLAG_COMPRESS) == 0) {
    _soup_message_disable_feature (msg, _soup_content_decoder_get_type ());
  }
  if (flags & DOWNLOAD_FLAG_FORCE_REFRESH) {
    _soup_message_headers_append (msg_headers, "Cache-Control", "max-age=0");
  }

  /* Take the lock to protect header strings */
  g_mutex_lock (&dh->transfer_lock);

  if (referer != NULL) {
    _soup_message_headers_append (msg_headers, "Referer", referer);
  } else if (dh->referer != NULL) {
    _soup_message_headers_append (msg_headers, "Referer", dh->referer);
  }

  if (dh->user_agent != NULL) {
    _soup_message_headers_append (msg_headers, "User-Agent", dh->user_agent);
  }

  if (dh->cookies != NULL) {
    gchar **cookie;

    for (cookie = dh->cookies; *cookie != NULL; cookie++) {
      _soup_message_headers_append (msg_headers, "Cookie", *cookie);
    }
  }

  transfer_task = transfer_task_new (dh, request, msg, blocking);

  if (!dh->running) {
    /* The download helper was deactivated just as we went to dispatch this request.
     * Abort and manually wake the request, as it never went in the active_transfer list */
    g_mutex_unlock (&dh->transfer_lock);

    download_request_lock (request);
    request->state = DOWNLOAD_REQUEST_STATE_UNSENT;
    request->in_use = FALSE;
    download_request_unlock (request);

    g_cancellable_cancel (g_task_get_cancellable (transfer_task));
    g_task_return_error_if_cancelled (transfer_task);
    g_object_unref (transfer_task);

    return FALSE;
  }

  download_request_lock (request);
  request->in_use = TRUE;
  download_request_unlock (request);

  g_signal_connect (msg, "restarted", G_CALLBACK (soup_msg_restarted_cb),
      transfer_task);

  /* Now send the request over to the main loop for actual submission */
  GST_LOG ("Submitting transfer task %p", transfer_task);
  g_async_queue_push (dh->transfer_requests, transfer_task);

  /* No pending idle source to wake the transfer loop - so create one */
  if (dh->transfer_requests_source == NULL) {
    dh->transfer_requests_source = g_idle_source_new ();
    g_source_set_callback (dh->transfer_requests_source,
        (GSourceFunc) submit_transfers_cb, dh, NULL);
    g_source_attach (dh->transfer_requests_source, dh->transfer_context);
  }

  if (blocking) {
    DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);

    /* We need an extra ref on the task to make sure it stays alive.
     * We pushed it in the async queue, but didn't unlock yet, so while
     * we gave away our ref, the receiver can't have unreffed it */
    g_object_ref (transfer_task);
    while (!transfer->complete)
      g_cond_wait (&transfer->cond, &dh->transfer_lock);
    g_object_unref (transfer_task);
  }

  g_mutex_unlock (&dh->transfer_lock);

  return TRUE;
}

void
downloadhelper_cancel_request (DownloadHelper * dh, DownloadRequest * request)
{
  int i;

  g_mutex_lock (&dh->transfer_lock);

  download_request_lock (request);
  if (!request->in_use)
    goto out;

  GST_DEBUG ("Cancelling request for URI %s range %" G_GINT64_FORMAT " %"
      G_GINT64_FORMAT, request->uri, request->range_start, request->range_end);

  request->state = DOWNLOAD_REQUEST_STATE_CANCELLED;
  for (i = dh->active_transfers->len - 1; i >= 0; i--) {
    GTask *transfer_task = g_array_index (dh->active_transfers, GTask *, i);
    DownloadHelperTransfer *transfer = g_task_get_task_data (transfer_task);

    if (transfer->request == request) {
      GST_DEBUG ("Found transfer %p for request for URI %s range %"
          G_GINT64_FORMAT " %" G_GINT64_FORMAT, transfer, request->uri,
          request->range_start, request->range_end);
      g_cancellable_cancel (transfer->cancellable);
      break;
    }
  }

out:
  download_request_unlock (request);
  g_mutex_unlock (&dh->transfer_lock);
}

DownloadRequest *
downloadhelper_fetch_uri_range (DownloadHelper * dh, const gchar * uri,
    const gchar * referer, DownloadFlags flags, gint64 range_start,
    gint64 range_end, GError ** err)
{
  DownloadRequest *request;

  g_return_val_if_fail (uri != NULL, NULL);

  GST_DEBUG ("Fetching URI %s range %" G_GINT64_FORMAT " %" G_GINT64_FORMAT,
      uri, range_start, range_end);

  flags |= DOWNLOAD_FLAG_BLOCKING;

  request = download_request_new_uri_range (uri, range_start, range_end);

  if (!downloadhelper_submit_request (dh, referer, flags, request, err)) {
    download_request_unref (request);
    return NULL;
  }

  return request;
}

DownloadRequest *
downloadhelper_fetch_uri (DownloadHelper * dh, const gchar * uri,
    const gchar * referer, DownloadFlags flags, GError ** err)
{
  return downloadhelper_fetch_uri_range (dh, uri, referer, flags, 0, -1, err);
}
