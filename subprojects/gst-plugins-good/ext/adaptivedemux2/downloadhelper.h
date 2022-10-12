/* GStreamer
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
 *
 * downloadhelper.h:
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

#include <glib.h>

#include "gstadaptivedemuxutils.h"
#include "downloadrequest.h"

#ifndef __DOWNLOADHELPER_H__
#define __DOWNLOADHELPER_H__

G_BEGIN_DECLS typedef struct DownloadHelper DownloadHelper;
typedef enum DownloadFlags DownloadFlags;

#define HTTP_STATUS_IS_SUCCESSFUL(s) ((s) >= 200 && (s) < 300)

/* RFC8673 recommended last-byte-pos value of 2^^53-1 */
#define RFC8673_LAST_BYTE_POS (9007199254740991)

enum DownloadFlags
{
  DOWNLOAD_FLAG_NONE = 0,
  DOWNLOAD_FLAG_COMPRESS = (1 << 0),
  DOWNLOAD_FLAG_FORCE_REFRESH = (1 << 1),
  DOWNLOAD_FLAG_HEADERS_ONLY = (1 << 2),
  DOWNLOAD_FLAG_BLOCKING = (1 << 3),
};

DownloadHelper *downloadhelper_new (GstAdaptiveDemuxClock *clock);

gboolean downloadhelper_start (DownloadHelper * dh);
void downloadhelper_stop (DownloadHelper * dh);

void downloadhelper_free (DownloadHelper * dh);

void downloadhelper_set_referer (DownloadHelper * dh, const gchar *referer);
void downloadhelper_set_user_agent (DownloadHelper * dh, const gchar *user_agent);
void downloadhelper_set_cookies (DownloadHelper * dh, gchar **cookies);

gboolean downloadhelper_submit_request (DownloadHelper * dh,
    const gchar * referer, DownloadFlags flags, DownloadRequest * request,
    GError ** err);
void downloadhelper_cancel_request (DownloadHelper * dh, DownloadRequest *request);

DownloadRequest *downloadhelper_fetch_uri (DownloadHelper * dh, const gchar * uri,
    const gchar * referer, DownloadFlags flags, GError ** err);
DownloadRequest *downloadhelper_fetch_uri_range (DownloadHelper * dh,
    const gchar * uri, const gchar * referer, DownloadFlags flags,
    gint64 range_start, gint64 range_end, GError ** err);

G_END_DECLS
#endif
