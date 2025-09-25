/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-media-info.h - Media information structure
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VALIDATE_MEDIA_INFO_H__
#define __GST_VALIDATE_MEDIA_INFO_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/validate/validate-prelude.h>

G_BEGIN_DECLS

typedef struct _GstValidateMediaInfo GstValidateMediaInfo;
typedef struct _GstValidateStreamInfo GstValidateStreamInfo;

/**
 * GstValidateMediaInfo:
 *
 * GStreamer Validate MediaInfo struct.
 *
 * Stores extracted information about a media
 */
struct _GstValidateMediaInfo {

  /* <File checking data> */
  /* Value for the expected total duration of the file in nanosecs
   * Set to GST_CLOCK_TIME_NONE if it shouldn't be tested */
  GstClockTime duration;
  gboolean is_image;

  /* Expected file_size, set to 0 to skip test */
  guint64 file_size;

  gboolean seekable;

  gchar *playback_error;
  gchar *reverse_playback_error;
  gchar *track_switch_error;

  gchar *uri;

  gboolean discover_only;

  GstValidateStreamInfo *stream_info;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_VALIDATE_API
void gst_validate_media_info_init (GstValidateMediaInfo * mi);
GST_VALIDATE_API
void gst_validate_media_info_clear (GstValidateMediaInfo * mi);
GST_VALIDATE_API
void gst_validate_media_info_free (GstValidateMediaInfo * mi);

GST_VALIDATE_API
gchar * gst_validate_media_info_to_string (GstValidateMediaInfo * mi, gsize * length) G_GNUC_WARN_UNUSED_RESULT;
GST_VALIDATE_API
gboolean gst_validate_media_info_save (GstValidateMediaInfo * mi, const gchar * path, GError ** err);
GST_VALIDATE_API
GstValidateMediaInfo * gst_validate_media_info_load (const gchar * path, GError ** err);

GST_VALIDATE_API
gboolean gst_validate_media_info_inspect_uri (GstValidateMediaInfo * mi, const gchar * uri,
        gboolean discover_only, GError ** err);

GST_VALIDATE_API
gboolean gst_validate_media_info_compare (GstValidateMediaInfo * expected, GstValidateMediaInfo * extracted);

G_END_DECLS

#endif /* __GST_VALIDATE_MEDIA_INFO_H__ */

