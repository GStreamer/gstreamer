/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
 *
 * downloadrequest.h:
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

#ifndef __DOWNLOAD_REQUEST_H__
#define __DOWNLOAD_REQUEST_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define DOWNLOAD_REQUEST(obj) ((DownloadRequest *)(obj))

typedef struct _DownloadRequest DownloadRequest;
typedef enum _DownloadRequestState DownloadRequestState;

typedef void (*DownloadRequestEventCallback) (DownloadRequest *request, DownloadRequestState state, void *cb_data);

enum _DownloadRequestState {
  DOWNLOAD_REQUEST_STATE_UNSENT,
  DOWNLOAD_REQUEST_STATE_OPEN,     /* Request sent, but no response yet */
  DOWNLOAD_REQUEST_STATE_HEADERS_RECEIVED, /* Response headers received, awaiting body */
  DOWNLOAD_REQUEST_STATE_LOADING,  /* Content loading in progress */
  DOWNLOAD_REQUEST_STATE_COMPLETE, /* Request processing finished successfully - check status_code for completion 200-399 codes */
  DOWNLOAD_REQUEST_STATE_ERROR,    /* Request generated an http error - check status_code */
  DOWNLOAD_REQUEST_STATE_CANCELLED, /* Request has been cancelled by the user */
};

struct _DownloadRequest
{
  gint ref_count;

  gboolean in_use; /* TRUE if this request is being serviced */
  gboolean send_progress; /* TRUE if this request wants progress events */

  DownloadRequestState state;
  guint status_code; /* HTTP status code */

  /* Request parameters */
  gchar * uri;                  /* URI of the request */
  gint64 range_start;
  gint64 range_end;

  /* possibly populated during a download */
  gchar * redirect_uri;         /* Redirect target if any */
  gboolean redirect_permanent;  /* If the redirect is permanent */

  GstStructure *headers;        /* HTTP request/response headers */
  guint64 content_length;       /* Response content length, if known (or 0) */
  guint64 content_received;     /* Response content received so far */

  guint64 download_request_time;  /* Epoch time when the download started */
  guint64 download_start_time;    /* Epoch time when the first data for the download arrived */
  guint64 download_newest_data_time; /* Epoch time when the most recent data for the download arrived */
  guint64 download_end_time;      /* Epoch time when the download finished */
};

void download_request_set_uri (DownloadRequest *request, const gchar *uri,
    gint64 range_start, gint64 range_end);

/* Reset the request state back to UNSENT and clear any stored info. The request must not be in use */
void download_request_reset (DownloadRequest *request);

void download_request_begin_download (DownloadRequest *request);

void download_request_set_caps (DownloadRequest * request, GstCaps * caps);

GstCaps * download_request_get_caps (DownloadRequest * request);

GstClockTime download_request_get_age (DownloadRequest *request);

void download_request_add_buffer (DownloadRequest *request, GstBuffer *buffer);
GstBuffer * download_request_take_buffer (DownloadRequest *request);
GstBuffer * download_request_take_buffer_range (DownloadRequest *request, gint64 range_start, gint64 range_end);
guint64 download_request_get_bytes_available (DownloadRequest *request);
guint64 download_request_get_cur_offset (DownloadRequest *request);

DownloadRequest * download_request_new (void);
DownloadRequest * download_request_new_uri (const gchar * uri);
DownloadRequest * download_request_new_uri_range (const gchar * uri, gint64 range_start, gint64 range_end);

void download_request_set_callbacks (DownloadRequest *request,
    DownloadRequestEventCallback on_completion,
    DownloadRequestEventCallback on_error,
    DownloadRequestEventCallback on_cancellation,
    DownloadRequestEventCallback on_progress,
    void *cb_data);

DownloadRequest *download_request_ref (DownloadRequest *request);
void download_request_unref (DownloadRequest *request);

void download_request_lock (DownloadRequest *request);
void download_request_unlock (DownloadRequest *request);

void download_request_despatch_progress (DownloadRequest *request);
void download_request_despatch_completion (DownloadRequest *request);

G_END_DECLS
#endif /* __DOWNLOAD_REQUEST_H__ */
