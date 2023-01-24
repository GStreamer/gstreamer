/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
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
#include "validate.h"

#include <glib/gstdio.h>
#include <string.h>

struct _GstValidateStreamInfo
{
  GstCaps *caps;

  GList *children;
};

static GstValidateStreamInfo *
gst_validate_stream_info_from_discoverer_info (GstDiscovererStreamInfo * info)
{
  GstValidateStreamInfo *ret = g_new0 (GstValidateStreamInfo, 1);

  ret->caps = gst_discoverer_stream_info_get_caps (info);
  if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *streams =
        gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO
        (info));
    GList *iter;

    for (iter = streams; iter; iter = g_list_next (iter)) {
      ret->children = g_list_append (ret->children,
          gst_validate_stream_info_from_discoverer_info (iter->data));
    }
    gst_discoverer_stream_info_list_free (streams);
  }

  return ret;
}

static GstValidateStreamInfo *
gst_validate_stream_info_from_caps_string (gchar * capsstr)
{
  GstValidateStreamInfo *ret = g_new0 (GstValidateStreamInfo, 1);

  ret->caps = gst_caps_from_string (capsstr);

  return ret;
}

static void
gst_validate_stream_info_free (GstValidateStreamInfo * si)
{
  if (si->caps)
    gst_caps_unref (si->caps);
  g_list_free_full (si->children,
      (GDestroyNotify) gst_validate_stream_info_free);
  g_free (si);
}

void
gst_validate_media_info_init (GstValidateMediaInfo * mi)
{
  mi->uri = NULL;
  mi->file_size = 0;
  mi->duration = GST_CLOCK_TIME_NONE;
  mi->seekable = FALSE;
  mi->stream_info = NULL;
  mi->playback_error = NULL;
  mi->reverse_playback_error = NULL;
  mi->track_switch_error = NULL;
  mi->is_image = FALSE;
  mi->discover_only = FALSE;
}

void
gst_validate_media_info_clear (GstValidateMediaInfo * mi)
{
  g_free (mi->uri);
  g_free (mi->playback_error);
  g_free (mi->reverse_playback_error);
  g_free (mi->track_switch_error);
  if (mi->stream_info)
    gst_validate_stream_info_free (mi->stream_info);
}

void
gst_validate_media_info_free (GstValidateMediaInfo * mi)
{
  gst_validate_media_info_clear (mi);
  g_free (mi);
}

gchar *
gst_validate_media_info_to_string (GstValidateMediaInfo * mi, gsize * length)
{
  GKeyFile *kf = g_key_file_new ();
  gchar *data = NULL;
  gchar *str;

  /* file info */
  g_key_file_set_string (kf, "file-info", "uri", mi->uri);
  g_key_file_set_uint64 (kf, "file-info", "file-size", mi->file_size);

  /* media info */
  g_key_file_set_uint64 (kf, "media-info", "file-duration", mi->duration);
  g_key_file_set_boolean (kf, "media-info", "seekable", mi->seekable);
  g_key_file_set_boolean (kf, "media-info", "is-image", mi->is_image);

  if (mi->stream_info && mi->stream_info->caps) {
    str = gst_caps_to_string (mi->stream_info->caps);
    g_key_file_set_string (kf, "media-info", "caps", str);
    g_free (str);
  }

  /* playback tests */
  g_key_file_set_string (kf, "playback-tests", "playback-error",
      mi->playback_error ? mi->playback_error : "");
  g_key_file_set_string (kf, "playback-tests", "reverse-playback-error",
      mi->reverse_playback_error ? mi->reverse_playback_error : "");
  g_key_file_set_string (kf, "playback-tests", "track-switch-error",
      mi->track_switch_error ? mi->track_switch_error : "");

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

  if (!g_file_set_contents (path, data, datalength, err))
    return FALSE;
  return TRUE;
}

/**
 * gst_validate_media_info_load: (skip):
 */
GstValidateMediaInfo *
gst_validate_media_info_load (const gchar * path, GError ** err)
{
  GKeyFile *kf = g_key_file_new ();
  GstValidateMediaInfo *mi;
  gchar *str;

  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, err)) {
    g_key_file_free (kf);
    return NULL;
  }

  mi = g_new (GstValidateMediaInfo, 1);
  gst_validate_media_info_init (mi);

  mi->uri = g_key_file_get_string (kf, "file-info", "uri", err);
  if (err && *err)
    goto end;
  mi->file_size = g_key_file_get_uint64 (kf, "file-info", "file-size", err);
  if (err && *err)
    goto end;

  mi->duration =
      g_key_file_get_uint64 (kf, "media-info", "file-duration", NULL);
  mi->seekable = g_key_file_get_boolean (kf, "media-info", "seekable", NULL);
  mi->is_image = g_key_file_get_boolean (kf, "media-info", "is-image", NULL);

  str = g_key_file_get_string (kf, "media-info", "caps", NULL);
  if (str) {
    mi->stream_info = gst_validate_stream_info_from_caps_string (str);
    g_free (str);
  }

  mi->playback_error =
      g_key_file_get_string (kf, "playback-tests", "playback-error", NULL);
  mi->reverse_playback_error =
      g_key_file_get_string (kf, "playback-tests", "reverse-playback-error",
      NULL);
  mi->track_switch_error =
      g_key_file_get_string (kf, "playback-tests", "track-switch-error", NULL);
  if (mi->playback_error && strlen (mi->playback_error) == 0) {
    g_free (mi->playback_error);
    mi->playback_error = NULL;
  }
  if (mi->reverse_playback_error && strlen (mi->reverse_playback_error) == 0) {
    g_free (mi->reverse_playback_error);
    mi->reverse_playback_error = NULL;
  }
  if (mi->track_switch_error && strlen (mi->track_switch_error) == 0) {
    g_free (mi->track_switch_error);
    mi->track_switch_error = NULL;
  }

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
  GError *err = NULL;

  filepath = g_filename_from_uri (mi->uri, NULL, &err);
  if (!filepath) {
    g_error_free (err);
    return FALSE;
  }

  if (g_stat (filepath, &statbuf) == 0) {
    size = statbuf.st_size;
  } else {
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
#endif

static gboolean
check_encoding_profile (GstValidateMediaInfo * mi, GstDiscovererInfo * info)
{
  gboolean ret = TRUE;
  GstDiscovererStreamInfo *streaminfo;

  streaminfo = gst_discoverer_info_get_stream_info (info);
  mi->stream_info = gst_validate_stream_info_from_discoverer_info (streaminfo);

  gst_discoverer_info_unref (streaminfo);

  return ret;
}

typedef gboolean (*GstElementConfigureFunc) (GstValidateMediaInfo *,
    GstElement *, gchar ** msg);
static gboolean
check_playback_scenario (GstValidateMediaInfo * mi,
    GstElementConfigureFunc configure_function, gchar ** error_message)
{
  GstElement *playbin;
  GstElement *videosink, *audiosink;
  GstBus *bus;
  GstMessage *msg;
  gboolean ret = TRUE;
  GstStateChangeReturn state_ret;

  playbin = gst_element_factory_make ("playbin", "fc-playbin");
  videosink = gst_element_factory_make ("fakesink", "fc-videosink");
  audiosink = gst_element_factory_make ("fakesink", "fc-audiosink");

  if (!playbin || !videosink || !audiosink) {
    *error_message = g_strdup ("Playbin and/or fakesink not available");
  }

  g_object_set (playbin, "video-sink", videosink, "audio-sink", audiosink,
      "uri", mi->uri, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));

  state_ret = gst_element_set_state (playbin, GST_STATE_PAUSED);
  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    *error_message = g_strdup ("Failed to change pipeline to paused");
    ret = FALSE;
    goto end;
  } else if (state_ret == GST_STATE_CHANGE_ASYNC) {
    msg =
        gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    if (msg && GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
      gst_message_unref (msg);
    } else {
      ret = FALSE;
      *error_message = g_strdup ("Playback finihshed unexpectedly");
      goto end;
    }
  }

  if (configure_function) {
    if (!configure_function (mi, playbin, error_message)) {
      gst_object_unref (bus);
      gst_object_unref (playbin);
      return FALSE;
    }
  }

  if (gst_element_set_state (playbin,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    *error_message = g_strdup ("Failed to set pipeline to playing");
    ret = FALSE;
    goto end;
  }

  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  if (msg) {
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
      /* all good */
      ret = TRUE;
    } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (msg, &error, &debug);
      *error_message = g_strdup_printf ("Playback error: %s : %s",
          error->message, debug);
      g_error_free (error);
      g_free (debug);

      ret = FALSE;
    } else {
      g_assert_not_reached ();
    }
    gst_message_unref (msg);
  } else {
    ret = FALSE;
    *error_message = g_strdup ("Playback finihshed unexpectedly");
  }

end:
  gst_object_unref (bus);
  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);

  return ret;
}

static gboolean
check_playback (GstValidateMediaInfo * mi, gchar ** msg)
{
  return check_playback_scenario (mi, NULL, msg);
}

static gboolean
send_reverse_seek (GstValidateMediaInfo * mi, GstElement * pipeline,
    gchar ** msg)
{
  gboolean ret;

  ret = gst_element_seek (pipeline, -1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);

  if (!ret) {
    *msg = g_strdup ("Reverse playback seek failed");
  }
  return ret;
}

static gboolean
check_reverse_playback (GstValidateMediaInfo * mi, gchar ** msg)
{
  return check_playback_scenario (mi, send_reverse_seek, msg);
}

typedef struct
{
  guint counter;
  guint back_counter;
  gulong probe_id;
  GstPad *pad;
} BufferCountData;

static GstPadProbeReturn
input_selector_pad_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer userdata)
{
  GstPad *sink_pad = NULL;

  if (info->type == GST_PAD_PROBE_TYPE_BUFFER) {
    BufferCountData *bcd =
        g_object_get_data (G_OBJECT (pad), "buffer-count-data");
    if (!bcd) {
      GST_ERROR_OBJECT (pad, "No buffer-count-data found");
      return GST_PAD_PROBE_OK;
    }

    ++bcd->counter;
    if (GST_PAD_IS_SRC (pad)) {
      g_object_get (GST_PAD_PARENT (pad), "active-pad", &sink_pad, NULL);
      if (sink_pad) {
        bcd = g_object_get_data (G_OBJECT (sink_pad), "buffer-count-data");
        if (!bcd) {
          gst_object_unref (sink_pad);
          GST_ERROR_OBJECT (pad, "No buffer-count-data found");
          return GST_PAD_PROBE_OK;
        }
        ++bcd->back_counter;
        gst_object_unref (sink_pad);
      }
    }
  }
  return GST_PAD_PROBE_OK;
}

static void
setup_input_selector_counters (GstElement * element)
{
  GstIterator *iterator;
  gboolean done = FALSE;
  GValue value = { 0, };
  GstPad *pad;
  BufferCountData *bcd;

  iterator = gst_element_iterate_pads (element);
  while (!done) {
    switch (gst_iterator_next (iterator, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_dup_object (&value);
        bcd = g_new0 (BufferCountData, 1);
        g_object_set_data (G_OBJECT (pad), "buffer-count-data", bcd);
        bcd->probe_id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
            (GstPadProbeCallback) input_selector_pad_probe, NULL, NULL);
        bcd->pad = pad;
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);
}

static gboolean
check_and_remove_input_selector_counters (GstElement * element,
    gchar ** error_message)
{
  GstIterator *iterator;
  gboolean done = FALSE;
  GstPad *pad;
  GValue value = { 0, };
  guint id, ncounters = 0;
#if 0
  guint total_sink_count = 0;
#endif
  BufferCountData *bcd, **bcds =
      g_malloc0 (sizeof (BufferCountData *) * element->numpads);
  gboolean ret = TRUE;

  /* First gather all counts, and free memory, etc */
  iterator = gst_element_iterate_pads (element);
  while (!done) {
    switch (gst_iterator_next (iterator, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&value);
        bcd = g_object_get_data (G_OBJECT (pad), "buffer-count-data");
        if (GST_PAD_IS_SINK (pad)) {
          bcds[++ncounters] = bcd;
#if 0
          total_sink_count += bcd->counter;
#endif
        } else {
          bcds[0] = bcd;
        }
        gst_pad_remove_probe (pad, bcd->probe_id);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        *error_message = g_strdup ("Failed to iterate through pads");
        ret = FALSE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);

  if (!ret) {
    g_free (bcds);
    return FALSE;
  }

  /* Now bcd[0] contains the total number of buffers received,
     and subsequent bcd slots contain the total number of buffers sent
     by each source pad. Check that the totals match, and that every
     source pad got at least one buffer.
     Or that's the theory. It doesn't work in practice, the number of
     raw buffers flowing is non deterministic. */
#if 0
  if (bcds[0]->counter != total_sink_count) {
    *error_message = g_strdup_printf ("%u buffers received, %u buffers sent",
        total_sink_count, bcds[0]->counter);
    ret = FALSE;
  }
  for (id = 1; id < element->numpads; ++id) {
    if (bcds[id]->counter == 0) {
      *error_message =
          g_strdup_printf ("Sink pad %s got no buffers",
          GST_PAD_NAME (bcds[id]->pad));
      ret = FALSE;
    }
  }
#endif
  /* We at least check that at least one buffer was sent while the
     selected sink was a given sink, for all sinks */
  for (id = 1; id < element->numpads; ++id) {
    if (bcds[id]->back_counter == 0) {
      *error_message =
          g_strdup_printf ("No buffer was sent while sink pad %s was active",
          GST_PAD_NAME (bcds[id]->pad));
      ret = FALSE;
    }
  }

  for (id = 0; id < element->numpads; ++id) {
    gst_object_unref (bcds[id]->pad);
    g_free (bcds[id]);
  }
  g_free (bcds);
  return ret;
}

static GstPad *
find_next_pad (GstElement * element, GstPad * pad)
{
  GstIterator *iterator;
  gboolean done = FALSE, pick = FALSE;
  GstPad *tmp, *next = NULL, *first = NULL;
  GValue value = { 0, };

  iterator = gst_element_iterate_sink_pads (element);

  while (!done) {
    switch (gst_iterator_next (iterator, &value)) {
      case GST_ITERATOR_OK:
        tmp = g_value_dup_object (&value);
        if (first == NULL)
          first = gst_object_ref (tmp);
        if (pick) {
          next = tmp;
          done = TRUE;
        } else {
          pick = (tmp == pad);
          gst_object_unref (tmp);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        /* When we reach the end, we may be in the case where the pad
           to search from was the last one in the list, in which case
           we want to return the first pad. */
        if (pick) {
          next = first;
          first = NULL;
        }
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);
  if (first)
    gst_object_unref (first);
  return next;
}

static int
find_input_selector (GValue * value, void *userdata)
{
  GstElement *element = g_value_get_object (value);
  g_assert (GST_IS_ELEMENT (element));
  if (g_str_has_prefix (GST_ELEMENT_NAME (element), "inputselector")) {
    guint npads;
    g_object_get (element, "n-pads", &npads, NULL);
    if (npads > 1)
      return 0;
  }
  return !0;
}

/* This function looks for an input-selector, and, if one is found,
   cycle through its sink pads */
static gboolean
check_track_selection (GstValidateMediaInfo * mi, gchar ** error_message)
{
  GstElement *playbin;
  GstElement *videosink, *audiosink;
  GstElement *input_selector = NULL;
  GstBus *bus;
  GstMessage *msg;
  gboolean ret = TRUE;
  GstStateChangeReturn state_ret;
  GstIterator *iterator;
  GstPad *original_pad;
  static const GstClockTime switch_delay = GST_SECOND * 5;
  GValue value = { 0, };

  playbin = gst_element_factory_make ("playbin", "fc-playbin");
  videosink = gst_element_factory_make ("fakesink", "fc-videosink");
  audiosink = gst_element_factory_make ("fakesink", "fc-audiosink");

  if (!playbin || !videosink || !audiosink) {
    *error_message = g_strdup ("Playbin and/or fakesink not available");
  }

  g_object_set (playbin, "video-sink", videosink, "audio-sink", audiosink,
      "uri", mi->uri, NULL);
  g_object_set (videosink, "sync", TRUE, NULL);
  g_object_set (audiosink, "sync", TRUE, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));

  state_ret = gst_element_set_state (playbin, GST_STATE_PAUSED);
  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    *error_message = g_strdup ("Failed to change pipeline to paused");
    ret = FALSE;
    goto end;
  } else if (state_ret == GST_STATE_CHANGE_ASYNC) {
    msg =
        gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    if (msg && GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
      gst_message_unref (msg);
    } else {
      ret = FALSE;
      *error_message = g_strdup ("Playback finihshed unexpectedly");
      goto end;
    }
  }

  if (gst_element_set_state (playbin,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    *error_message = g_strdup ("Failed to set pipeline to playing");
    ret = FALSE;
    goto end;
  }

  iterator = gst_bin_iterate_recurse (GST_BIN (playbin));
  if (!gst_iterator_find_custom (iterator,
          (GCompareFunc) find_input_selector, &value, NULL)) {
    /* It's fine, there's only one if several tracks of the same type */
    gst_iterator_free (iterator);
    input_selector = NULL;
    goto end;
  }
  input_selector = g_value_dup_object (&value);
  g_value_reset (&value);
  gst_iterator_free (iterator);
  g_object_get (input_selector, "active-pad", &original_pad, NULL);
  if (!original_pad) {
    /* Unexpected, log an error somehow ? */
    ret = FALSE;
    gst_object_unref (input_selector);
    input_selector = NULL;
    goto end;
  }

  /* Attach a buffer counter to each pad */
  setup_input_selector_counters (input_selector);

  while (1) {
    msg =
        gst_bus_timed_pop_filtered (bus, switch_delay,
        GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (msg) {
      if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
        /* all good */
        ret = TRUE;
      } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
        GError *error = NULL;
        gchar *debug = NULL;

        gst_message_parse_error (msg, &error, &debug);
        *error_message = g_strdup_printf ("Playback error: %s : %s",
            error->message, debug);
        g_error_free (error);
        g_free (debug);

        ret = FALSE;
      } else {
        g_assert_not_reached ();
      }
      gst_message_unref (msg);
    } else {
      /* Timeout, switch track if we have more, or stop */
      GstPad *active_pad, *next_pad;

      g_object_get (input_selector, "active-pad", &active_pad, NULL);
      if (!active_pad) {
        *error_message =
            g_strdup ("Failed to get active-pad from input-selector");
        ret = FALSE;
        goto end;
      }
      next_pad = find_next_pad (input_selector, active_pad);
      gst_object_unref (active_pad);
      if (!next_pad) {
        ret = FALSE;
        goto end;
      }
      if (next_pad == original_pad) {
        goto end;
      }
      g_object_set (input_selector, "active-pad", next_pad, NULL);
      gst_object_unref (next_pad);
    }
  }

end:
  if (input_selector) {
    if (!check_and_remove_input_selector_counters (input_selector,
            error_message))
      ret = FALSE;
    gst_object_unref (input_selector);
  }
  gst_object_unref (bus);
  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);

  return ret;
}

static gboolean
check_is_image (GstDiscovererInfo * info)
{
  gboolean ret = FALSE;
  GList *video_streams = gst_discoverer_info_get_video_streams (info);

  if (g_list_length (video_streams) == 1) {
    if (gst_discoverer_video_info_is_image (video_streams->data)) {
      GList *audio_streams = gst_discoverer_info_get_audio_streams (info);

      if (audio_streams == NULL)
        ret = TRUE;
      else
        gst_discoverer_stream_info_list_free (audio_streams);
    }
  }

  gst_discoverer_stream_info_list_free (video_streams);

  return ret;
}

gboolean
gst_validate_media_info_inspect_uri (GstValidateMediaInfo * mi,
    const gchar * uri, gboolean discover_only, GError ** err)
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

  mi->is_image = check_is_image (info);
  ret = check_file_size (mi) & ret;
  ret = check_encoding_profile (mi, info) & ret;
  ret = check_file_duration (mi, info) & ret;

  if (mi->is_image)
    goto done;

  check_seekable (mi, info);
  if (discover_only)
    goto done;

  ret = check_playback (mi, &mi->playback_error) & ret;
  ret = check_reverse_playback (mi, &mi->reverse_playback_error) & ret;
  ret = check_track_selection (mi, &mi->track_switch_error) & ret;

done:
  gst_object_unref (discoverer);

  return ret;
}

gboolean
gst_validate_media_info_compare (GstValidateMediaInfo * expected,
    GstValidateMediaInfo * extracted)
{
  gboolean ret = TRUE;
  if (expected->duration != extracted->duration) {
    gst_validate_printf (NULL,
        "Duration changed: %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT "\n",
        GST_TIME_ARGS (expected->duration),
        GST_TIME_ARGS (extracted->duration));
    ret = FALSE;
  }
  if (expected->file_size != extracted->file_size) {
    gst_validate_printf (NULL,
        "File size changed: %" G_GUINT64_FORMAT " -> %" G_GUINT64_FORMAT "\n",
        expected->file_size, extracted->file_size);
    ret = FALSE;
  }
  if (expected->seekable && !extracted->seekable) {
    gst_validate_printf (NULL, "File isn't seekable anymore\n");
    ret = FALSE;
  }

  if (extracted->discover_only == FALSE) {
    if (expected->playback_error == NULL && extracted->playback_error) {
      gst_validate_printf (NULL, "Playback is now failing with: %s\n",
          extracted->playback_error);
      ret = FALSE;
    }
    if (expected->reverse_playback_error == NULL
        && extracted->reverse_playback_error) {
      gst_validate_printf (NULL, "Reverse playback is now failing with: %s\n",
          extracted->reverse_playback_error);
      ret = FALSE;
    }
    if (expected->track_switch_error == NULL && extracted->track_switch_error) {
      gst_validate_printf (NULL, "Track switching is now failing with: %s\n",
          extracted->track_switch_error);
      ret = FALSE;
    }
  }

  if (extracted->stream_info == NULL || expected->stream_info == NULL) {
    gst_validate_printf (NULL,
        "Stream infos could not be retrieved, an error occured\n");
    ret = FALSE;
  } else if (expected->stream_info
      && !gst_caps_is_equal_fixed (expected->stream_info->caps,
          extracted->stream_info->caps)) {
    gchar *caps1 = gst_caps_to_string (expected->stream_info->caps);
    gchar *caps2 = gst_caps_to_string (extracted->stream_info->caps);

    gst_validate_printf (NULL, "Media caps changed: '%s' -> '%s'\n", caps1,
        caps2);
    g_free (caps1);
    g_free (caps2);
    ret = FALSE;
  }
  return ret;
}
