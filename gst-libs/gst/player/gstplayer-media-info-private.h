/* GStreamer
 *
 * Copyright (C) 2015 Brijesh Singh <brijesh.ksingh@gmail.com>
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

#include "gstplayer-media-info.h"

#ifndef __GST_PLAYER_MEDIA_INFO_PRIVATE_H__
#define __GST_PLAYER_MEDIA_INFO_PRIVATE_H__

struct _GstPlayerStreamInfo
{
  GObject parent;

  gchar *codec;

  GstCaps *caps;
  gint stream_index;
  GstTagList  *tags;
  gchar *stream_id;
};

struct _GstPlayerStreamInfoClass
{
  GObjectClass parent_class;
};

struct _GstPlayerSubtitleInfo
{
  GstPlayerStreamInfo  parent;

  gchar *language;
};

struct _GstPlayerSubtitleInfoClass
{
  GstPlayerStreamInfoClass parent_class;
};

struct _GstPlayerAudioInfo
{
  GstPlayerStreamInfo  parent;

  gint channels;
  gint sample_rate;

  guint bitrate;
  guint max_bitrate;

  gchar *language;
};

struct _GstPlayerAudioInfoClass
{
  GstPlayerStreamInfoClass parent_class;
};

struct _GstPlayerVideoInfo
{
  GstPlayerStreamInfo  parent;

  gint width;
  gint height;
  gint framerate_num;
  gint framerate_denom;
  gint par_num;
  gint par_denom;

  guint bitrate;
  guint max_bitrate;
};

struct _GstPlayerVideoInfoClass
{
  GstPlayerStreamInfoClass parent_class;
};

struct _GstPlayerMediaInfo
{
  GObject parent;

  gchar *uri;
  gchar *title;
  gchar *container;
  gboolean seekable, is_live;
  GstTagList *tags;
  GstSample *image_sample;

  GList *stream_list;
  GList *audio_stream_list;
  GList *video_stream_list;
  GList *subtitle_stream_list;

  GstClockTime  duration;
};

struct _GstPlayerMediaInfoClass
{
  GObjectClass parent_class;
};

G_GNUC_INTERNAL GstPlayerMediaInfo*   gst_player_media_info_new
                                      (const gchar *uri);
G_GNUC_INTERNAL GstPlayerMediaInfo*   gst_player_media_info_copy
                                      (GstPlayerMediaInfo *ref);
G_GNUC_INTERNAL GstPlayerStreamInfo*  gst_player_stream_info_new
                                      (gint stream_index, GType type);
G_GNUC_INTERNAL GstPlayerStreamInfo*  gst_player_stream_info_copy
                                      (GstPlayerStreamInfo *ref);

#endif /* __GST_PLAYER_MEDIA_INFO_PRIVATE_H__ */
