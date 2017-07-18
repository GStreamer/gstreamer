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

/**
 * SECTION:gstplayer-mediainfo
 * @title: GstPlayerMediaInfo
 * @short_description: Player Media Information
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstplayer-media-info.h"
#include "gstplayer-media-info-private.h"

/* Per-stream information */
G_DEFINE_ABSTRACT_TYPE (GstPlayerStreamInfo, gst_player_stream_info,
    G_TYPE_OBJECT);

static void
gst_player_stream_info_init (GstPlayerStreamInfo * sinfo)
{
  sinfo->stream_index = -1;
}

static void
gst_player_stream_info_finalize (GObject * object)
{
  GstPlayerStreamInfo *sinfo = GST_PLAYER_STREAM_INFO (object);

  g_free (sinfo->codec);
  g_free (sinfo->stream_id);

  if (sinfo->caps)
    gst_caps_unref (sinfo->caps);

  if (sinfo->tags)
    gst_tag_list_unref (sinfo->tags);

  G_OBJECT_CLASS (gst_player_stream_info_parent_class)->finalize (object);
}

static void
gst_player_stream_info_class_init (GstPlayerStreamInfoClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_player_stream_info_finalize;
}

/**
 * gst_player_stream_info_get_index:
 * @info: a #GstPlayerStreamInfo
 *
 * Function to get stream index from #GstPlayerStreamInfo instance.
 *
 * Returns: the stream index of this stream.
 */
gint
gst_player_stream_info_get_index (const GstPlayerStreamInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_STREAM_INFO (info), -1);

  return info->stream_index;
}

/**
 * gst_player_stream_info_get_stream_type:
 * @info: a #GstPlayerStreamInfo
 *
 * Function to return human readable name for the stream type
 * of the given @info (ex: "audio", "video", "subtitle")
 *
 * Returns: a human readable name
 */
const gchar *
gst_player_stream_info_get_stream_type (const GstPlayerStreamInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_STREAM_INFO (info), NULL);

  if (GST_IS_PLAYER_VIDEO_INFO (info))
    return "video";
  else if (GST_IS_PLAYER_AUDIO_INFO (info))
    return "audio";
  else
    return "subtitle";
}

/**
 * gst_player_stream_info_get_tags:
 * @info: a #GstPlayerStreamInfo
 *
 * Returns: (transfer none): the tags contained in this stream.
 */
GstTagList *
gst_player_stream_info_get_tags (const GstPlayerStreamInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_STREAM_INFO (info), NULL);

  return info->tags;
}

/**
 * gst_player_stream_info_get_codec:
 * @info: a #GstPlayerStreamInfo
 *
 * A string describing codec used in #GstPlayerStreamInfo.
 *
 * Returns: codec string or NULL on unknown.
 */
const gchar *
gst_player_stream_info_get_codec (const GstPlayerStreamInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_STREAM_INFO (info), NULL);

  return info->codec;
}

/**
 * gst_player_stream_info_get_caps:
 * @info: a #GstPlayerStreamInfo
 *
 * Returns: (transfer none): the #GstCaps of the stream.
 */
GstCaps *
gst_player_stream_info_get_caps (const GstPlayerStreamInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_STREAM_INFO (info), NULL);

  return info->caps;
}

/* Video information */
G_DEFINE_TYPE (GstPlayerVideoInfo, gst_player_video_info,
    GST_TYPE_PLAYER_STREAM_INFO);

static void
gst_player_video_info_init (GstPlayerVideoInfo * info)
{
  info->width = -1;
  info->height = -1;
  info->framerate_num = 0;
  info->framerate_denom = 1;
  info->par_num = 1;
  info->par_denom = 1;
}

static void
gst_player_video_info_class_init (G_GNUC_UNUSED GstPlayerVideoInfoClass * klass)
{
  /* nothing to do here */
}

/**
 * gst_player_video_info_get_width:
 * @info: a #GstPlayerVideoInfo
 *
 * Returns: the width of video in #GstPlayerVideoInfo.
 */
gint
gst_player_video_info_get_width (const GstPlayerVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_VIDEO_INFO (info), -1);

  return info->width;
}

/**
 * gst_player_video_info_get_height:
 * @info: a #GstPlayerVideoInfo
 *
 * Returns: the height of video in #GstPlayerVideoInfo.
 */
gint
gst_player_video_info_get_height (const GstPlayerVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_VIDEO_INFO (info), -1);

  return info->height;
}

/**
 * gst_player_video_info_get_framerate:
 * @info: a #GstPlayerVideoInfo
 * @fps_n: (out): Numerator of frame rate
 * @fps_d: (out): Denominator of frame rate
 *
 */
void
gst_player_video_info_get_framerate (const GstPlayerVideoInfo * info,
    gint * fps_n, gint * fps_d)
{
  g_return_if_fail (GST_IS_PLAYER_VIDEO_INFO (info));

  *fps_n = info->framerate_num;
  *fps_d = info->framerate_denom;
}

/**
 * gst_player_video_info_get_pixel_aspect_ratio:
 * @info: a #GstPlayerVideoInfo
 * @par_n: (out): numerator
 * @par_d: (out): denominator
 *
 * Returns the pixel aspect ratio in @par_n and @par_d
 *
 */
void
gst_player_video_info_get_pixel_aspect_ratio (const GstPlayerVideoInfo * info,
    guint * par_n, guint * par_d)
{
  g_return_if_fail (GST_IS_PLAYER_VIDEO_INFO (info));

  *par_n = info->par_num;
  *par_d = info->par_denom;
}

/**
 * gst_player_video_info_get_bitrate:
 * @info: a #GstPlayerVideoInfo
 *
 * Returns: the current bitrate of video in #GstPlayerVideoInfo.
 */
gint
gst_player_video_info_get_bitrate (const GstPlayerVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_VIDEO_INFO (info), -1);

  return info->bitrate;
}

/**
 * gst_player_video_info_get_max_bitrate:
 * @info: a #GstPlayerVideoInfo
 *
 * Returns: the maximum bitrate of video in #GstPlayerVideoInfo.
 */
gint
gst_player_video_info_get_max_bitrate (const GstPlayerVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_VIDEO_INFO (info), -1);

  return info->max_bitrate;
}

/* Audio information */
G_DEFINE_TYPE (GstPlayerAudioInfo, gst_player_audio_info,
    GST_TYPE_PLAYER_STREAM_INFO);

static void
gst_player_audio_info_init (GstPlayerAudioInfo * info)
{
  info->channels = 0;
  info->sample_rate = 0;
  info->bitrate = -1;
  info->max_bitrate = -1;
}

static void
gst_player_audio_info_finalize (GObject * object)
{
  GstPlayerAudioInfo *info = GST_PLAYER_AUDIO_INFO (object);

  g_free (info->language);

  G_OBJECT_CLASS (gst_player_audio_info_parent_class)->finalize (object);
}

static void
gst_player_audio_info_class_init (GstPlayerAudioInfoClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_player_audio_info_finalize;
}

/**
 * gst_player_audio_info_get_language:
 * @info: a #GstPlayerAudioInfo
 *
 * Returns: the language of the stream, or NULL if unknown.
 */
const gchar *
gst_player_audio_info_get_language (const GstPlayerAudioInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_AUDIO_INFO (info), NULL);

  return info->language;
}

/**
 * gst_player_audio_info_get_channels:
 * @info: a #GstPlayerAudioInfo
 *
 * Returns: the number of audio channels in #GstPlayerAudioInfo.
 */
gint
gst_player_audio_info_get_channels (const GstPlayerAudioInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_AUDIO_INFO (info), 0);

  return info->channels;
}

/**
 * gst_player_audio_info_get_sample_rate:
 * @info: a #GstPlayerAudioInfo
 *
 * Returns: the audio sample rate in #GstPlayerAudioInfo.
 */
gint
gst_player_audio_info_get_sample_rate (const GstPlayerAudioInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_AUDIO_INFO (info), 0);

  return info->sample_rate;
}

/**
 * gst_player_audio_info_get_bitrate:
 * @info: a #GstPlayerAudioInfo
 *
 * Returns: the audio bitrate in #GstPlayerAudioInfo.
 */
gint
gst_player_audio_info_get_bitrate (const GstPlayerAudioInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_AUDIO_INFO (info), -1);

  return info->bitrate;
}

/**
 * gst_player_audio_info_get_max_bitrate:
 * @info: a #GstPlayerAudioInfo
 *
 * Returns: the audio maximum bitrate in #GstPlayerAudioInfo.
 */
gint
gst_player_audio_info_get_max_bitrate (const GstPlayerAudioInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_AUDIO_INFO (info), -1);

  return info->max_bitrate;
}

/* Subtitle information */
G_DEFINE_TYPE (GstPlayerSubtitleInfo, gst_player_subtitle_info,
    GST_TYPE_PLAYER_STREAM_INFO);

static void
gst_player_subtitle_info_init (G_GNUC_UNUSED GstPlayerSubtitleInfo * info)
{
  /* nothing to do */
}

static void
gst_player_subtitle_info_finalize (GObject * object)
{
  GstPlayerSubtitleInfo *info = GST_PLAYER_SUBTITLE_INFO (object);

  g_free (info->language);

  G_OBJECT_CLASS (gst_player_subtitle_info_parent_class)->finalize (object);
}

static void
gst_player_subtitle_info_class_init (GstPlayerSubtitleInfoClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_player_subtitle_info_finalize;
}

/**
 * gst_player_subtitle_info_get_language:
 * @info: a #GstPlayerSubtitleInfo
 *
 * Returns: the language of the stream, or NULL if unknown.
 */
const gchar *
gst_player_subtitle_info_get_language (const GstPlayerSubtitleInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_SUBTITLE_INFO (info), NULL);

  return info->language;
}

/* Global media information */
G_DEFINE_TYPE (GstPlayerMediaInfo, gst_player_media_info, G_TYPE_OBJECT);

static void
gst_player_media_info_init (GstPlayerMediaInfo * info)
{
  info->duration = -1;
  info->is_live = FALSE;
  info->seekable = FALSE;
}

static void
gst_player_media_info_finalize (GObject * object)
{
  GstPlayerMediaInfo *info = GST_PLAYER_MEDIA_INFO (object);

  g_free (info->uri);

  if (info->tags)
    gst_tag_list_unref (info->tags);

  g_free (info->title);

  g_free (info->container);

  if (info->image_sample)
    gst_sample_unref (info->image_sample);

  if (info->audio_stream_list)
    g_list_free (info->audio_stream_list);

  if (info->video_stream_list)
    g_list_free (info->video_stream_list);

  if (info->subtitle_stream_list)
    g_list_free (info->subtitle_stream_list);

  if (info->stream_list)
    g_list_free_full (info->stream_list, g_object_unref);

  G_OBJECT_CLASS (gst_player_media_info_parent_class)->finalize (object);
}

static void
gst_player_media_info_class_init (GstPlayerMediaInfoClass * klass)
{
  GObjectClass *oclass = (GObjectClass *) klass;

  oclass->finalize = gst_player_media_info_finalize;
}

static GstPlayerVideoInfo *
gst_player_video_info_new (void)
{
  return g_object_new (GST_TYPE_PLAYER_VIDEO_INFO, NULL);
}

static GstPlayerAudioInfo *
gst_player_audio_info_new (void)
{
  return g_object_new (GST_TYPE_PLAYER_AUDIO_INFO, NULL);
}

static GstPlayerSubtitleInfo *
gst_player_subtitle_info_new (void)
{
  return g_object_new (GST_TYPE_PLAYER_SUBTITLE_INFO, NULL);
}

static GstPlayerStreamInfo *
gst_player_video_info_copy (GstPlayerVideoInfo * ref)
{
  GstPlayerVideoInfo *ret;

  ret = gst_player_video_info_new ();

  ret->width = ref->width;
  ret->height = ref->height;
  ret->framerate_num = ref->framerate_num;
  ret->framerate_denom = ref->framerate_denom;
  ret->par_num = ref->par_num;
  ret->par_denom = ref->par_denom;
  ret->bitrate = ref->bitrate;
  ret->max_bitrate = ref->max_bitrate;

  return (GstPlayerStreamInfo *) ret;
}

static GstPlayerStreamInfo *
gst_player_audio_info_copy (GstPlayerAudioInfo * ref)
{
  GstPlayerAudioInfo *ret;

  ret = gst_player_audio_info_new ();

  ret->sample_rate = ref->sample_rate;
  ret->channels = ref->channels;
  ret->bitrate = ref->bitrate;
  ret->max_bitrate = ref->max_bitrate;

  if (ref->language)
    ret->language = g_strdup (ref->language);

  return (GstPlayerStreamInfo *) ret;
}

static GstPlayerStreamInfo *
gst_player_subtitle_info_copy (GstPlayerSubtitleInfo * ref)
{
  GstPlayerSubtitleInfo *ret;

  ret = gst_player_subtitle_info_new ();
  if (ref->language)
    ret->language = g_strdup (ref->language);

  return (GstPlayerStreamInfo *) ret;
}

GstPlayerStreamInfo *
gst_player_stream_info_copy (GstPlayerStreamInfo * ref)
{
  GstPlayerStreamInfo *info = NULL;

  if (!ref)
    return NULL;

  if (GST_IS_PLAYER_VIDEO_INFO (ref))
    info = gst_player_video_info_copy ((GstPlayerVideoInfo *) ref);
  else if (GST_IS_PLAYER_AUDIO_INFO (ref))
    info = gst_player_audio_info_copy ((GstPlayerAudioInfo *) ref);
  else
    info = gst_player_subtitle_info_copy ((GstPlayerSubtitleInfo *) ref);

  info->stream_index = ref->stream_index;
  if (ref->tags)
    info->tags = gst_tag_list_ref (ref->tags);
  if (ref->caps)
    info->caps = gst_caps_copy (ref->caps);
  if (ref->codec)
    info->codec = g_strdup (ref->codec);
  if (ref->stream_id)
    info->stream_id = g_strdup (ref->stream_id);

  return info;
}

GstPlayerMediaInfo *
gst_player_media_info_copy (GstPlayerMediaInfo * ref)
{
  GList *l;
  GstPlayerMediaInfo *info;

  if (!ref)
    return NULL;

  info = gst_player_media_info_new (ref->uri);
  info->duration = ref->duration;
  info->seekable = ref->seekable;
  info->is_live = ref->is_live;
  if (ref->tags)
    info->tags = gst_tag_list_ref (ref->tags);
  if (ref->title)
    info->title = g_strdup (ref->title);
  if (ref->container)
    info->container = g_strdup (ref->container);
  if (ref->image_sample)
    info->image_sample = gst_sample_ref (ref->image_sample);

  for (l = ref->stream_list; l != NULL; l = l->next) {
    GstPlayerStreamInfo *s;

    s = gst_player_stream_info_copy ((GstPlayerStreamInfo *) l->data);
    info->stream_list = g_list_append (info->stream_list, s);

    if (GST_IS_PLAYER_AUDIO_INFO (s))
      info->audio_stream_list = g_list_append (info->audio_stream_list, s);
    else if (GST_IS_PLAYER_VIDEO_INFO (s))
      info->video_stream_list = g_list_append (info->video_stream_list, s);
    else
      info->subtitle_stream_list =
          g_list_append (info->subtitle_stream_list, s);
  }

  return info;
}

GstPlayerStreamInfo *
gst_player_stream_info_new (gint stream_index, GType type)
{
  GstPlayerStreamInfo *info = NULL;

  if (type == GST_TYPE_PLAYER_AUDIO_INFO)
    info = (GstPlayerStreamInfo *) gst_player_audio_info_new ();
  else if (type == GST_TYPE_PLAYER_VIDEO_INFO)
    info = (GstPlayerStreamInfo *) gst_player_video_info_new ();
  else
    info = (GstPlayerStreamInfo *) gst_player_subtitle_info_new ();

  info->stream_index = stream_index;

  return info;
}

GstPlayerMediaInfo *
gst_player_media_info_new (const gchar * uri)
{
  GstPlayerMediaInfo *info;

  g_return_val_if_fail (uri != NULL, NULL);

  info = g_object_new (GST_TYPE_PLAYER_MEDIA_INFO, NULL);
  info->uri = g_strdup (uri);

  return info;
}

/**
 * gst_player_media_info_get_uri:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: the URI associated with #GstPlayerMediaInfo.
 */
const gchar *
gst_player_media_info_get_uri (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->uri;
}

/**
 * gst_player_media_info_is_seekable:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: %TRUE if the media is seekable.
 */
gboolean
gst_player_media_info_is_seekable (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), FALSE);

  return info->seekable;
}

/**
 * gst_player_media_info_is_live:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: %TRUE if the media is live.
 */
gboolean
gst_player_media_info_is_live (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), FALSE);

  return info->is_live;
}

/**
 * gst_player_media_info_get_stream_list:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerStreamInfo): A #GList of
 * matching #GstPlayerStreamInfo.
 */
GList *
gst_player_media_info_get_stream_list (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->stream_list;
}

/**
 * gst_player_media_info_get_video_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerVideoInfo): A #GList of
 * matching #GstPlayerVideoInfo.
 */
GList *
gst_player_media_info_get_video_streams (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->video_stream_list;
}

/**
 * gst_player_media_info_get_subtitle_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerSubtitleInfo): A #GList of
 * matching #GstPlayerSubtitleInfo.
 */
GList *
gst_player_media_info_get_subtitle_streams (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->subtitle_stream_list;
}

/**
 * gst_player_media_info_get_audio_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerAudioInfo): A #GList of
 * matching #GstPlayerAudioInfo.
 */
GList *
gst_player_media_info_get_audio_streams (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->audio_stream_list;
}

/**
 * gst_player_media_info_get_duration:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: duration of the media.
 */
GstClockTime
gst_player_media_info_get_duration (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), -1);

  return info->duration;
}

/**
 * gst_player_media_info_get_tags:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none): the tags contained in media info.
 */
GstTagList *
gst_player_media_info_get_tags (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->tags;
}

/**
 * gst_player_media_info_get_title:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: the media title.
 */
const gchar *
gst_player_media_info_get_title (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->title;
}

/**
 * gst_player_media_info_get_container_format:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: the container format.
 */
const gchar *
gst_player_media_info_get_container_format (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->container;
}

/**
 * gst_player_media_info_get_image_sample:
 * @info: a #GstPlayerMediaInfo
 *
 * Function to get the image (or preview-image) stored in taglist.
 * Application can use gst_sample_*_() API's to get caps, buffer etc.
 *
 * Returns: (transfer none): GstSample or NULL.
 */
GstSample *
gst_player_media_info_get_image_sample (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), NULL);

  return info->image_sample;
}

/**
 * gst_player_media_info_get_number_of_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: number of total streams.
 * Since: 1.12
 */
guint
gst_player_media_info_get_number_of_streams (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), 0);

  return g_list_length (info->stream_list);
}

/**
 * gst_player_media_info_get_number_of_video_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: number of video streams.
 * Since: 1.12
 */
guint
gst_player_media_info_get_number_of_video_streams (const GstPlayerMediaInfo *
    info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), 0);

  return g_list_length (info->video_stream_list);
}

/**
 * gst_player_media_info_get_number_of_audio_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: number of audio streams.
 * Since: 1.12
 */
guint
gst_player_media_info_get_number_of_audio_streams (const GstPlayerMediaInfo *
    info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), 0);

  return g_list_length (info->audio_stream_list);
}

/**
 * gst_player_media_info_get_number_of_subtitle_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: number of subtitle streams.
 * Since: 1.12
 */
guint gst_player_media_info_get_number_of_subtitle_streams
    (const GstPlayerMediaInfo * info)
{
  g_return_val_if_fail (GST_IS_PLAYER_MEDIA_INFO (info), 0);

  return g_list_length (info->subtitle_stream_list);
}

/**
 * gst_player_get_video_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerVideoInfo): A #GList of
 * matching #GstPlayerVideoInfo.
 */
#ifndef GST_REMOVE_DEPRECATED
GList *
gst_player_get_video_streams (const GstPlayerMediaInfo * info)
{
  return gst_player_media_info_get_video_streams (info);
}
#endif

/**
 * gst_player_get_audio_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerAudioInfo): A #GList of
 * matching #GstPlayerAudioInfo.
 */
#ifndef GST_REMOVE_DEPRECATED
GList *
gst_player_get_audio_streams (const GstPlayerMediaInfo * info)
{
  return gst_player_media_info_get_audio_streams (info);
}
#endif

/**
 * gst_player_get_subtitle_streams:
 * @info: a #GstPlayerMediaInfo
 *
 * Returns: (transfer none) (element-type GstPlayerSubtitleInfo): A #GList of
 * matching #GstPlayerSubtitleInfo.
 */
#ifndef GST_REMOVE_DEPRECATED
GList *
gst_player_get_subtitle_streams (const GstPlayerMediaInfo * info)
{
  return gst_player_media_info_get_subtitle_streams (info);
}
#endif
