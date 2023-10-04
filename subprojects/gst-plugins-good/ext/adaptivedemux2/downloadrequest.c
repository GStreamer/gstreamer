/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
 *
 * gstrequest.c:
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

#include <glib.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/base/gstadapter.h>
#include "downloadrequest.h"
#include <stdlib.h>

typedef struct _DownloadRequestPrivate DownloadRequestPrivate;

struct _DownloadRequestPrivate
{
  DownloadRequest request;

  GstBuffer *buffer;
  GstCaps *caps;
  GRecMutex lock;

  DownloadRequestEventCallback completion_cb;
  DownloadRequestEventCallback cancellation_cb;
  DownloadRequestEventCallback error_cb;
  DownloadRequestEventCallback progress_cb;
  void *cb_data;
};

#define DOWNLOAD_REQUEST_PRIVATE(frag) ((DownloadRequestPrivate *)(frag))

static void download_request_free (DownloadRequest * request);

DownloadRequest *
download_request_new (void)
{
  DownloadRequest *request =
      (DownloadRequest *) g_new0 (DownloadRequestPrivate, 1);
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);

  g_atomic_int_set (&request->ref_count, 1);

  g_rec_mutex_init (&priv->lock);

  priv->buffer = NULL;

  request->state = DOWNLOAD_REQUEST_STATE_UNSENT;
  request->status_code = 0;

  request->download_request_time = GST_CLOCK_TIME_NONE;
  request->download_start_time = GST_CLOCK_TIME_NONE;
  request->download_end_time = GST_CLOCK_TIME_NONE;
  request->headers = NULL;

  return (DownloadRequest *) (request);
}

DownloadRequest *
download_request_new_uri (const gchar * uri)
{
  DownloadRequest *request = download_request_new ();

  request->uri = g_strdup (uri);
  request->range_start = 0;
  request->range_end = -1;

  return request;
}

DownloadRequest *
download_request_new_uri_range (const gchar * uri, gint64 range_start,
    gint64 range_end)
{
  DownloadRequest *request = download_request_new ();

  request->uri = g_strdup (uri);
  request->range_start = range_start;
  request->range_end = range_end;

  return request;
}

static void
download_request_free (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);

  g_free (request->uri);
  g_free (request->redirect_uri);
  if (request->headers) {
    gst_structure_free (request->headers);
    request->headers = NULL;
  }

  if (priv->buffer != NULL) {
    gst_buffer_unref (priv->buffer);
    priv->buffer = NULL;
  }

  if (priv->caps != NULL) {
    gst_caps_unref (priv->caps);
    priv->caps = NULL;
  }

  g_rec_mutex_clear (&priv->lock);

  g_free (priv);
}

void
download_request_set_callbacks (DownloadRequest * request,
    DownloadRequestEventCallback on_completion,
    DownloadRequestEventCallback on_error,
    DownloadRequestEventCallback on_cancellation,
    DownloadRequestEventCallback on_progress, void *cb_data)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  g_rec_mutex_lock (&priv->lock);
  priv->completion_cb = on_completion;
  priv->error_cb = on_error;
  priv->cancellation_cb = on_cancellation;
  priv->progress_cb = on_progress;
  priv->cb_data = cb_data;

  request->send_progress = (on_progress != NULL);

  g_rec_mutex_unlock (&priv->lock);
}

/* Called with request lock held */
void
download_request_despatch_progress (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  if (priv->progress_cb)
    priv->progress_cb (request, request->state, priv->cb_data);
}

/* Called with request lock held */
void
download_request_despatch_completion (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  switch (request->state) {
    case DOWNLOAD_REQUEST_STATE_UNSENT:
    case DOWNLOAD_REQUEST_STATE_CANCELLED:
      if (priv->cancellation_cb)
        priv->cancellation_cb (request, request->state, priv->cb_data);
      break;
    case DOWNLOAD_REQUEST_STATE_COMPLETE:
      if (priv->completion_cb)
        priv->completion_cb (request, request->state, priv->cb_data);
      break;
    case DOWNLOAD_REQUEST_STATE_ERROR:
      if (priv->error_cb)
        priv->error_cb (request, request->state, priv->cb_data);
      break;
    default:
      g_assert_not_reached ();
  }
}


DownloadRequest *
download_request_ref (DownloadRequest * request)
{
  g_return_val_if_fail (request != NULL, NULL);
  g_atomic_int_inc (&request->ref_count);

  return request;
}

void
download_request_unref (DownloadRequest * request)
{
  g_return_if_fail (request != NULL);

  if (g_atomic_int_dec_and_test (&request->ref_count)) {
    download_request_free (request);
  }
}

void
download_request_lock (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  g_rec_mutex_lock (&priv->lock);
}

void
download_request_unlock (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  g_rec_mutex_unlock (&priv->lock);
}

GstBuffer *
download_request_take_buffer (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  GstBuffer *buffer = NULL;

  g_return_val_if_fail (request != NULL, NULL);

  g_rec_mutex_lock (&priv->lock);

  if (request->state != DOWNLOAD_REQUEST_STATE_LOADING
      && request->state != DOWNLOAD_REQUEST_STATE_COMPLETE) {
    g_rec_mutex_unlock (&priv->lock);
    return NULL;
  }

  buffer = priv->buffer;
  priv->buffer = NULL;

  g_rec_mutex_unlock (&priv->lock);

  return buffer;
}

/* Extract the byte range of the download, matching the
 * requested range against the GST_BUFFER_OFFSET() values of the
 * data buffer, which tracks the byte position in the
 * original resource */
GstBuffer *
download_request_take_buffer_range (DownloadRequest * request,
    gint64 target_range_start, gint64 target_range_end)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  GstBuffer *buffer = NULL;
  GstBuffer *input_buffer = NULL;

  g_return_val_if_fail (request != NULL, NULL);

  g_rec_mutex_lock (&priv->lock);

  if (request->state != DOWNLOAD_REQUEST_STATE_LOADING
      && request->state != DOWNLOAD_REQUEST_STATE_COMPLETE) {
    g_rec_mutex_unlock (&priv->lock);
    return NULL;
  }

  input_buffer = priv->buffer;
  priv->buffer = NULL;
  if (input_buffer == NULL)
    goto out;

  /* Figure out how much of the available data (if any) belongs to
   * the target range */
  gint64 avail_start = GST_BUFFER_OFFSET (input_buffer);
  gint64 avail_end = avail_start + gst_buffer_get_size (input_buffer) - 1;

  target_range_start = MAX (avail_start, target_range_start);

  if (target_range_start <= avail_end) {
    /* There's at least 1 byte available that belongs to this target request, but
     * does this buffer need splitting in two? */
    if (target_range_end != -1 && target_range_end < avail_end) {
      /* Yes, it does. Drop the front of the buffer if needed and take the piece we want */
      guint64 start_offset = target_range_start - avail_start;

      buffer =
          gst_buffer_copy_region (input_buffer, GST_BUFFER_COPY_MEMORY,
          start_offset, target_range_end - avail_start);
      GST_BUFFER_OFFSET (buffer) =
          GST_BUFFER_OFFSET (input_buffer) + start_offset;

      /* Put the rest of the buffer back */
      priv->buffer =
          gst_buffer_copy_region (input_buffer, GST_BUFFER_COPY_MEMORY,
          target_range_end - avail_start, -1);

      /* Release the original buffer. The sub-buffers are holding their own refs as needed */
      gst_buffer_unref (input_buffer);
    } else if (target_range_start != avail_start) {
      /* We want to the end of the buffer, but need to drop a piece at the front */
      guint64 start_offset = target_range_start - avail_start;

      buffer =
          gst_buffer_copy_region (input_buffer, GST_BUFFER_COPY_MEMORY,
          start_offset, -1);
      GST_BUFFER_OFFSET (buffer) =
          GST_BUFFER_OFFSET (input_buffer) + start_offset;

      /* Release the original buffer. The sub-buffer is holding its own ref as needed */
      gst_buffer_unref (input_buffer);
    } else {
      /* No, return the entire buffer as-is */
      buffer = input_buffer;
    }
  }

out:
  g_rec_mutex_unlock (&priv->lock);
  return buffer;
}

guint64
download_request_get_bytes_available (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  guint64 ret = 0;

  g_rec_mutex_lock (&priv->lock);

  if (priv->buffer != NULL)
    ret = gst_buffer_get_size (priv->buffer);

  g_rec_mutex_unlock (&priv->lock);

  return ret;
}

guint64
download_request_get_cur_offset (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  guint64 ret = GST_BUFFER_OFFSET_NONE;

  g_rec_mutex_lock (&priv->lock);

  if (priv->buffer != NULL)
    ret = GST_BUFFER_OFFSET (priv->buffer);

  g_rec_mutex_unlock (&priv->lock);

  return ret;
}

void
download_request_set_uri (DownloadRequest * request, const gchar * uri,
    gint64 range_start, gint64 range_end)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  g_rec_mutex_lock (&priv->lock);

  g_assert (request->in_use == FALSE);

  if (request->uri != uri) {
    g_free (request->uri);
    request->uri = g_strdup (uri);
  }

  g_free (request->redirect_uri);
  request->redirect_uri = NULL;
  request->redirect_permanent = FALSE;

  request->range_start = range_start;
  request->range_end = range_end;

  g_rec_mutex_unlock (&priv->lock);
}

void
download_request_reset (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);

  g_rec_mutex_lock (&priv->lock);
  g_assert (request->in_use == FALSE);
  request->state = DOWNLOAD_REQUEST_STATE_UNSENT;

  if (request->headers) {
    gst_structure_free (request->headers);
    request->headers = NULL;
  }

  if (priv->buffer != NULL) {
    gst_buffer_unref (priv->buffer);
    priv->buffer = NULL;
  }

  if (priv->caps != NULL) {
    gst_caps_unref (priv->caps);
    priv->caps = NULL;
  }

  g_rec_mutex_unlock (&priv->lock);
}

/* Called when the request is submitted, to clear any settings from a previous
 * download */
void
download_request_begin_download (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);

  g_return_if_fail (request != NULL);

  g_rec_mutex_lock (&priv->lock);

  if (priv->buffer != NULL) {
    gst_buffer_unref (priv->buffer);
    priv->buffer = NULL;
  }

  if (request->headers) {
    gst_structure_free (request->headers);
    request->headers = NULL;
  }

  if (priv->caps != NULL) {
    gst_caps_unref (priv->caps);
    priv->caps = NULL;
  }

  request->content_length = 0;
  request->content_received = 0;

  request->download_request_time = GST_CLOCK_TIME_NONE;
  request->download_start_time = GST_CLOCK_TIME_NONE;
  request->download_end_time = GST_CLOCK_TIME_NONE;

  g_rec_mutex_unlock (&priv->lock);
}

void
download_request_set_caps (DownloadRequest * request, GstCaps * caps)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  g_return_if_fail (request != NULL);

  g_rec_mutex_lock (&priv->lock);
  gst_caps_replace (&priv->caps, caps);
  g_rec_mutex_unlock (&priv->lock);
}

GstCaps *
download_request_get_caps (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  GstCaps *caps;

  g_return_val_if_fail (request != NULL, NULL);

  if (request->state != DOWNLOAD_REQUEST_STATE_LOADING
      && request->state != DOWNLOAD_REQUEST_STATE_COMPLETE)
    return NULL;

  g_rec_mutex_lock (&priv->lock);
  if (priv->caps == NULL) {
    guint64 offset, offset_end;

    /* FIXME: This is currently necessary as typefinding only
     * works with 0 offsets... need to find a better way to
     * do that */
    offset = GST_BUFFER_OFFSET (priv->buffer);
    offset_end = GST_BUFFER_OFFSET_END (priv->buffer);
    GST_BUFFER_OFFSET (priv->buffer) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (priv->buffer) = GST_BUFFER_OFFSET_NONE;
    priv->caps = gst_type_find_helper_for_buffer (NULL, priv->buffer, NULL);
    GST_BUFFER_OFFSET (priv->buffer) = offset;
    GST_BUFFER_OFFSET_END (priv->buffer) = offset_end;
  }

  caps = gst_caps_ref (priv->caps);
  g_rec_mutex_unlock (&priv->lock);

  return caps;
}

static GstClockTime
_get_age_header (GstStructure * headers)
{
  const GstStructure *response_headers;
  const gchar *http_age;
  const GValue *val;

  val = gst_structure_get_value (headers, "response-headers");
  if (!val) {
    return 0;
  }

  response_headers = gst_value_get_structure (val);
  http_age = gst_structure_get_string (response_headers, "Date");
  if (!http_age) {
    return 0;
  }

  return atoi (http_age) * GST_SECOND;
}

/* Return the age of the download from the Age header,
 * or 0 if there was none */
GstClockTime
download_request_get_age (DownloadRequest * request)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);
  GstClockTime age = 0;

  g_return_val_if_fail (request != NULL, age);

  if (request->state != DOWNLOAD_REQUEST_STATE_LOADING
      && request->state != DOWNLOAD_REQUEST_STATE_COMPLETE)
    return age;

  g_rec_mutex_lock (&priv->lock);

  if (request->headers != NULL) {
    /* We have headers for the download, see if there was an Age
     * header in the response */
    GstClockTime age = _get_age_header (request->headers);
    GST_LOG ("Got cached data with age %" GST_TIMEP_FORMAT, &age);
  }
  g_rec_mutex_unlock (&priv->lock);

  return age;
}

void
download_request_add_buffer (DownloadRequest * request, GstBuffer * buffer)
{
  DownloadRequestPrivate *priv = DOWNLOAD_REQUEST_PRIVATE (request);

  g_return_if_fail (request != NULL);
  g_return_if_fail (buffer != NULL);

  if (request->state == DOWNLOAD_REQUEST_STATE_COMPLETE) {
    GST_WARNING ("Download request is completed, could not add more buffers");
    gst_buffer_unref (buffer);
    return;
  }

  GST_DEBUG ("Adding new buffer %" GST_PTR_FORMAT " to the request data",
      buffer);

  request->content_received += gst_buffer_get_size (buffer);

  /* We steal the buffers you pass in */
  if (priv->buffer == NULL)
    priv->buffer = buffer;
  else
    priv->buffer = gst_buffer_append (priv->buffer, buffer);
}
