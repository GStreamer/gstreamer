/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-media-info.c
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-validate-media-info.h"

#include <glib/gstdio.h>

void
gst_validate_media_info_init (GstValidateMediaInfo * mi)
{
  mi->uri = NULL;
  mi->file_size = 0;
  mi->duration = GST_CLOCK_TIME_NONE;
  mi->seekable = FALSE;
  mi->stream_info = NULL;
}

void
gst_validate_media_info_clear (GstValidateMediaInfo * mi)
{
  g_free (mi->uri);
  if (mi->stream_info)
    gst_object_unref (mi->stream_info);
}

gchar *
gst_validate_media_info_to_string (GstValidateMediaInfo * mi, gsize * length)
{
  GKeyFile *kf = g_key_file_new ();
  gchar *data = NULL;

  /* file info */
  g_key_file_set_string (kf, "file-info", "uri", mi->uri);
  g_key_file_set_uint64 (kf, "file-info", "file-size", mi->file_size);

  /* media info */
  g_key_file_set_uint64 (kf, "media-info", "file-duration", mi->duration);
  g_key_file_set_boolean (kf, "media-info", "seekable", mi->seekable);

  data = g_key_file_to_data (kf, length, NULL);
  g_key_file_free (kf);

  return data;
}

gboolean
gst_validate_media_info_save (GstValidateMediaInfo * mi, const gchar * path,
    GError ** err)
{
  gchar *data = NULL;
  gsize datalength = 0;

  data = gst_validate_media_info_to_string (mi, &datalength);

  g_file_set_contents (path, data, datalength, err);
  if (err)
    return FALSE;
  return TRUE;
}

GstValidateMediaInfo *
gst_validate_media_info_load (const gchar * path, GError ** err)
{
  GKeyFile *kf = g_key_file_new ();
  GstValidateMediaInfo *mi;

  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, err)) {
    g_key_file_free (kf);
    return NULL;
  }

  mi = g_new (GstValidateMediaInfo, 1);
  gst_validate_media_info_init (mi);

  mi->uri = g_key_file_get_string (kf, "file-info", "uri", err);
  if (err)
    goto end;
  mi->file_size = g_key_file_get_uint64 (kf, "file-info", "file-size", err);
  if (err)
    goto end;

  mi->duration = g_key_file_get_uint64 (kf, "media-info", "duration", NULL);
  mi->seekable = g_key_file_get_boolean (kf, "media-info", "seekable", NULL);

end:
  g_key_file_free (kf);
  return mi;
}

static gboolean
check_file_size (GstValidateMediaInfo * mi)
{
  GStatBuf statbuf;
  gchar *filepath;
  guint64 size = 0;
  gboolean ret = TRUE;
  GError *err;

  filepath = g_filename_from_uri (mi->uri, NULL, &err);
  if (!filepath) {
#if 0
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_NOT_FOUND,
        "Failed to get filepath from uri %s. %s", fc->uri, err->message);
#endif
    g_error_free (err);
    return FALSE;
  }

  if (g_stat (filepath, &statbuf) == 0) {
    size = statbuf.st_size;
  } else {
#if 0
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_NOT_FOUND,
        "Failed to get file stats from uri %s", fc->uri);
#endif
    ret = FALSE;
    goto end;
  }

  mi->file_size = size;

end:
  g_free (filepath);
  return ret;
}

static gboolean
check_file_duration (GstValidateMediaInfo * mi, GstDiscovererInfo * info)
{
  mi->duration = gst_discoverer_info_get_duration (info);
  return TRUE;
}

static gboolean
check_seekable (GstValidateMediaInfo * mi, GstDiscovererInfo * info)
{
  mi->seekable = gst_discoverer_info_get_seekable (info);
  return TRUE;
}

#if 0
static inline gboolean
_gst_caps_can_intersect_safe (const GstCaps * a, const GstCaps * b)
{
  if (a == b)
    return TRUE;
  if ((a == NULL) || (b == NULL))
    return FALSE;
  return gst_caps_can_intersect (a, b);
}

#if 0
typedef struct
{
  GstEncodingProfile *profile;
  gint count;
} ExpectedStream;

#define SET_MESSAGE(placeholder, msg) \
G_STMT_START {  \
  if (placeholder) { \
    *placeholder = msg; \
  } \
} G_STMT_END

static gboolean
compare_encoding_profile_with_discoverer_stream (GstValidateFileChecker * fc,
    GstEncodingProfile * prof, GstDiscovererStreamInfo * stream, gchar ** msg);

static gboolean
    compare_container_profile_with_container_discoverer_stream
    (GstValidateFileChecker * fc, GstEncodingContainerProfile * prof,
    GstDiscovererContainerInfo * stream, gchar ** msg)
{
  ExpectedStream *expected_streams = NULL;
  GList *container_streams;
  const GList *profile_iter;
  const GList *streams_iter;
  gint i;
  gint expected_count = g_list_length ((GList *)
      gst_encoding_container_profile_get_profiles (prof));
  gboolean ret = TRUE;

  container_streams = gst_discoverer_container_info_get_streams (stream);

  if (expected_count == 0) {
    if (g_list_length (container_streams) != 0) {
      SET_MESSAGE (msg,
          g_strdup_printf
          ("No streams expected on this container, but found %u",
              g_list_length (container_streams)));
      ret = FALSE;
      goto end;
    }
  }

  /* initialize expected streams data */
  expected_streams = g_malloc0 (sizeof (ExpectedStream) * expected_count);
  for (i = 0, profile_iter = gst_encoding_container_profile_get_profiles (prof);
      profile_iter; profile_iter = g_list_next (profile_iter), i++) {
    GstEncodingProfile *prof = profile_iter->data;
    ExpectedStream *expected = &(expected_streams[i]);

    expected->profile = prof;
  }

  /* look for the streams on discoverer info */
  for (streams_iter = container_streams; streams_iter;
      streams_iter = g_list_next (streams_iter)) {
    GstDiscovererStreamInfo *info = streams_iter->data;
    gboolean found = FALSE;
    for (i = 0; i < expected_count; i++) {
      ExpectedStream *expected = &(expected_streams[i]);

      if (compare_encoding_profile_with_discoverer_stream (fc,
              expected->profile, info, NULL)) {
        found = TRUE;
        break;
      }
    }

    if (!found) {
      GstCaps *caps = gst_discoverer_stream_info_get_caps (info);
      gchar *caps_str = gst_caps_to_string (caps);
      SET_MESSAGE (msg,
          g_strdup_printf ("Stream with caps '%s' wasn't found on file",
              caps_str));
      g_free (caps_str);
      gst_caps_unref (caps);
      ret = FALSE;
      goto end;
    }
  }

  /* check if all expected streams are present */
  for (i = 0; i < expected_count; i++) {
    ExpectedStream *expected = &(expected_streams[i]);
    guint presence = gst_encoding_profile_get_presence (expected->profile);

    if (presence == 0)
      continue;

    if (presence != expected->count) {
      gchar *caps_str =
          gst_caps_to_string (gst_encoding_profile_get_format
          (expected->profile));
      SET_MESSAGE (msg,
          g_strdup_printf ("Stream from profile %s (with caps '%s"
              "' has presence %u but the number of streams found was %d",
              gst_encoding_profile_get_name (expected->profile), caps_str,
              presence, expected->count));
      g_free (caps_str);
      ret = FALSE;
      goto end;
    }
  }

end:
  g_free (expected_streams);
  gst_discoverer_stream_info_list_free (container_streams);
  return ret;
}

static gboolean
compare_encoding_profile_with_discoverer_stream (GstValidateFileChecker * fc,
    GstEncodingProfile * prof, GstDiscovererStreamInfo * stream, gchar ** msg)
{
  gboolean ret = TRUE;
  GstCaps *caps = NULL;
  const GstCaps *profile_caps;
  const GstCaps *restriction_caps;

  caps = gst_discoverer_stream_info_get_caps (stream);
  profile_caps = gst_encoding_profile_get_format (prof);
  restriction_caps = gst_encoding_profile_get_restriction (prof);

  /* TODO need to consider profile caps restrictions */
  if (!_gst_caps_can_intersect_safe (caps, profile_caps)) {
    gchar *caps_str = gst_caps_to_string (caps);
    gchar *profile_caps_str = gst_caps_to_string (profile_caps);
    SET_MESSAGE (msg, g_strdup_printf ("Caps '%s' didn't match profile '%s'",
            profile_caps_str, caps_str));
    g_free (caps_str);
    g_free (profile_caps_str);
    ret = FALSE;
    goto end;
  }

  if (restriction_caps) {
    GstStructure *structure;
    gint i;
    gboolean found = FALSE;

    for (i = 0; i < gst_caps_get_size (restriction_caps); i++) {
      structure = gst_caps_get_structure (restriction_caps, i);
      structure = gst_structure_copy (structure);
      gst_structure_set_name (structure,
          gst_structure_get_name (gst_caps_get_structure (caps, 0)));
      if (gst_structure_can_intersect (structure, gst_caps_get_structure (caps,
                  0))) {
        gst_structure_free (structure);
        found = TRUE;
        break;
      }
      gst_structure_free (structure);
    }
    if (!found) {
      gchar *caps_str = gst_caps_to_string (caps);
      gchar *restriction_caps_str = gst_caps_to_string (restriction_caps);
      SET_MESSAGE (msg,
          g_strdup_printf ("Caps restriction '%s' wasn't respected on file "
              "with caps '%s'", restriction_caps_str, caps_str));
      g_free (caps_str);
      g_free (restriction_caps_str);
      ret = FALSE;
      goto end;
    }
  }

  if (GST_IS_ENCODING_CONTAINER_PROFILE (prof)) {
    if (GST_IS_DISCOVERER_CONTAINER_INFO (stream)) {
      ret =
          ret & compare_container_profile_with_container_discoverer_stream (fc,
          (GstEncodingContainerProfile *) prof,
          (GstDiscovererContainerInfo *) stream, msg);
    } else {
      SET_MESSAGE (msg,
          g_strdup_printf ("Expected container profile but found stream of %s",
              gst_discoverer_stream_info_get_stream_type_nick (stream)));
      ret = FALSE;
      goto end;
    }

  } else if (GST_IS_ENCODING_VIDEO_PROFILE (prof)) {
    if (!GST_IS_DISCOVERER_VIDEO_INFO (stream)) {
      SET_MESSAGE (msg,
          g_strdup_printf ("Expected video profile but found stream of %s",
              gst_discoverer_stream_info_get_stream_type_nick (stream)));
      ret = FALSE;
      goto end;
    }

  } else if (GST_IS_ENCODING_AUDIO_PROFILE (prof)) {
    if (!GST_IS_DISCOVERER_AUDIO_INFO (stream)) {
      SET_MESSAGE (msg,
          g_strdup_printf ("Expected audio profile but found stream of %s",
              gst_discoverer_stream_info_get_stream_type_nick (stream)));
      ret = FALSE;
      goto end;
    }
  } else {
    g_assert_not_reached ();
    return FALSE;
  }


end:
  if (caps)
    gst_caps_unref (caps);

  return ret;
}
#endif

static gboolean
check_encoding_profile (GstValidateFileChecker * fc, GstDiscovererInfo * info)
{
  gboolean ret = TRUE;

  fc->results.stream_info = gst_discoverer_info_get_stream_info (info);

#if 0
  stream = gst_discoverer_info_get_stream_info (info);

  if (!compare_encoding_profile_with_discoverer_stream (fc, fc->profile, stream,
          &msg)) {
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_PROFILE_INCORRECT, msg);
    g_free (msg);
  }

  gst_discoverer_stream_info_unref (stream);
#endif
  return ret;
}

typedef gboolean (*GstElementConfigureFunc) (GstValidateFileChecker *,
    GstElement *);
static gboolean
check_playback_scenario (GstValidateFileChecker * fc,
    GstElementConfigureFunc configure_function, const gchar * messages_prefix)
{
  GstElement *playbin;
  GstElement *videosink, *audiosink;
  GstBus *bus;
  GstMessage *msg;
  gboolean ret = TRUE;

  playbin = gst_element_factory_make ("playbin2", "fc-playbin");
  videosink = gst_element_factory_make ("fakesink", "fc-videosink");
  audiosink = gst_element_factory_make ("fakesink", "fc-audiosink");

  if (!playbin || !videosink || !audiosink) {
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_MISSING_PLUGIN,
        "file check requires " "playbin2 and fakesink to be available");
  }

  g_object_set (playbin, "video-sink", videosink, "audio-sink", audiosink,
      "uri", fc->uri, NULL);

  if (gst_element_set_state (playbin,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_START_FAILURE,
        "Failed to " "change pipeline state to playing");
    ret = FALSE;
    goto end;
  }

  if (configure_function) {
    if (!configure_function (fc, playbin))
      return FALSE;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  if (msg) {
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
      /* all good */
    } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (msg, &error, &debug);
      GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_ERROR,
          "%s - File %s failed " "during playback. Error: %s : %s",
          messages_prefix, fc->uri, error->message, debug);
      g_error_free (error);
      g_free (debug);

      ret = FALSE;
    } else {
      g_assert_not_reached ();
    }
    gst_message_unref (msg);
  } else {
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_ERROR, "%s - "
        "File playback finished unexpectedly", messages_prefix);
  }
  gst_object_unref (bus);

end:
  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);

  return ret;
}

static gboolean
check_playback (GstValidateFileChecker * fc)
{
#if 0
  if (!fc->test_playback)
    return TRUE;
#endif
  return check_playback_scenario (fc, NULL, "Playback");
}

static gboolean
send_reverse_seek (GstValidateFileChecker * fc, GstElement * pipeline)
{
  gboolean ret;

  ret = gst_element_seek (pipeline, -1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);

  if (!ret) {
    GST_VALIDATE_REPORT (fc, GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_ERROR,
        "Reverse playback seek failed");
  }
  return ret;
}

static gboolean
check_reverse_playback (GstValidateFileChecker * fc)
{
#if 0
  if (!fc->test_reverse_playback)
    return TRUE;
#endif
  return check_playback_scenario (fc, send_reverse_seek, "Reverse playback");
}
#endif

gboolean
gst_validate_media_info_inspect_uri (GstValidateMediaInfo * mi,
    const gchar * uri, GError ** err)
{
  GstDiscovererInfo *info;
  GstDiscoverer *discoverer = gst_discoverer_new (GST_SECOND * 60, err);
  gboolean ret = TRUE;

  g_return_val_if_fail (uri != NULL, FALSE);

  g_free (mi->uri);
  mi->uri = g_strdup (uri);

  if (!discoverer) {
    return FALSE;
  }

  info = gst_discoverer_discover_uri (discoverer, uri, err);

  if (gst_discoverer_info_get_result (info) != GST_DISCOVERER_OK) {
    gst_object_unref (discoverer);
    return FALSE;
  }

  ret = check_file_size (mi) & ret;
  ret = check_file_duration (mi, info) & ret;
  ret = check_seekable (mi, info) & ret;
#if 0
  ret = check_encoding_profile (mi, info) & ret;
  ret = check_playback (mi) & ret;
  ret = check_reverse_playback (mi) & ret;
#endif

  gst_object_unref (discoverer);

  return ret;
}
