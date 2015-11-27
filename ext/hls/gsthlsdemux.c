/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * Gsthlsdemux.c:
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
 * SECTION:element-hlsdemux
 *
 * HTTP Live Streaming demuxer element.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin ! videoconvert ! videoscale ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include "gsthlsdemux.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

GST_DEBUG_CATEGORY_STATIC (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

#define GST_M3U8_CLIENT_LOCK(l) /* FIXME */
#define GST_M3U8_CLIENT_UNLOCK(l)       /* FIXME */

/* GObject */
static void gst_hls_demux_finalize (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);
static gchar *gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf);

static gboolean gst_hls_demux_change_playlist (GstHLSDemux * demux,
    guint max_bitrate, gboolean * changed);
static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data);
static void gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream);

static gboolean gst_hls_demux_is_live (GstAdaptiveDemux * demux);
static GstClockTime gst_hls_demux_get_duration (GstAdaptiveDemux * demux);
static gint64 gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux *
    demux);
static gboolean gst_hls_demux_process_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buf);
static GstFlowReturn gst_hls_demux_update_manifest (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);
static gboolean
gst_hls_demux_start_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_hls_demux_finish_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_hls_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer);
static void gst_hls_demux_stream_free (GstAdaptiveDemuxStream * stream);
static gboolean gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemuxStream *
    stream);
static GstFlowReturn gst_hls_demux_advance_fragment (GstAdaptiveDemuxStream *
    stream);
static GstFlowReturn gst_hls_demux_update_fragment_info (GstAdaptiveDemuxStream
    * stream);
static gboolean gst_hls_demux_select_bitrate (GstAdaptiveDemuxStream * stream,
    guint64 bitrate);
static void gst_hls_demux_reset (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * start, gint64 * stop);

#define gst_hls_demux_parent_class parent_class
G_DEFINE_TYPE (GstHLSDemux, gst_hls_demux, GST_TYPE_ADAPTIVE_DEMUX);

static void
gst_hls_demux_finalize (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
  g_mutex_clear (&demux->keys_lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAdaptiveDemuxClass *adaptivedemux_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  adaptivedemux_class = (GstAdaptiveDemuxClass *) klass;

  gobject_class->finalize = gst_hls_demux_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_static_metadata (element_class,
      "HLS Demuxer",
      "Codec/Demuxer/Adaptive",
      "HTTP Live Streaming demuxer",
      "Marc-Andre Lureau <marcandre.lureau@gmail.com>\n"
      "Andoni Morales Alastruey <ylatuya@gmail.com>");

  adaptivedemux_class->is_live = gst_hls_demux_is_live;
  adaptivedemux_class->get_live_seek_range = gst_hls_demux_get_live_seek_range;
  adaptivedemux_class->get_duration = gst_hls_demux_get_duration;
  adaptivedemux_class->get_manifest_update_interval =
      gst_hls_demux_get_manifest_update_interval;
  adaptivedemux_class->process_manifest = gst_hls_demux_process_manifest;
  adaptivedemux_class->update_manifest = gst_hls_demux_update_manifest;
  adaptivedemux_class->reset = gst_hls_demux_reset;
  adaptivedemux_class->seek = gst_hls_demux_seek;
  adaptivedemux_class->stream_has_next_fragment =
      gst_hls_demux_stream_has_next_fragment;
  adaptivedemux_class->stream_advance_fragment = gst_hls_demux_advance_fragment;
  adaptivedemux_class->stream_update_fragment_info =
      gst_hls_demux_update_fragment_info;
  adaptivedemux_class->stream_select_bitrate = gst_hls_demux_select_bitrate;
  adaptivedemux_class->stream_free = gst_hls_demux_stream_free;

  adaptivedemux_class->start_fragment = gst_hls_demux_start_fragment;
  adaptivedemux_class->finish_fragment = gst_hls_demux_finish_fragment;
  adaptivedemux_class->data_received = gst_hls_demux_data_received;

  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

static void
gst_hls_demux_init (GstHLSDemux * demux)
{
  gst_adaptive_demux_set_stream_struct_size (GST_ADAPTIVE_DEMUX_CAST (demux),
      sizeof (GstHLSDemuxStream));

  demux->keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_mutex_init (&demux->keys_lock);
}

static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      g_hash_table_remove_all (demux->keys);
      break;
    default:
      break;
  }
  return ret;
}

static GstPad *
gst_hls_demux_create_pad (GstHLSDemux * hlsdemux)
{
  gchar *name;
  GstPad *pad;

  name = g_strdup_printf ("src_%u", hlsdemux->srcpad_counter++);
  pad = gst_pad_new_from_static_template (&srctemplate, name);
  g_free (name);

  return pad;
}

static guint64
gst_hls_demux_get_bitrate (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (hlsdemux);

  /* Valid because hlsdemux only has a single output */
  if (demux->streams) {
    GstAdaptiveDemuxStream *stream = demux->streams->data;
    return stream->current_download_rate;
  }

  return 0;
}

static void
gst_hls_demux_stream_clear_pending_data (GstHLSDemuxStream * hls_stream)
{
  if (hls_stream->pending_encrypted_data)
    gst_adapter_clear (hls_stream->pending_encrypted_data);
  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);
  hls_stream->current_offset = -1;
  gst_hls_demux_stream_decrypt_end (hls_stream);
}

static void
gst_hls_demux_clear_all_pending_data (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *walk;

  for (walk = demux->streams; walk != NULL; walk = walk->next) {
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
    gst_hls_demux_stream_clear_pending_data (hls_stream);
  }
}

static void
gst_hls_demux_set_current (GstHLSDemux * self, GstM3U8 * m3u8)
{
  GST_M3U8_CLIENT_LOCK (self);
  if (m3u8 != self->current) {
    self->current = m3u8;
    self->current->duration = GST_CLOCK_TIME_NONE;
    self->current->current_file = NULL;

#if 0
    // FIXME: this makes no sense after we just set self->current=m3u8 above (tpm)
    // also, these values don't necessarily align between different lists
    m3u8->current_file_duration = self->current->current_file_duration;
    m3u8->sequence = self->current->sequence;
    m3u8->sequence_position = self->current->sequence_position;
    m3u8->highest_sequence_number = self->current->highest_sequence_number;
    m3u8->first_file_start = self->current->first_file_start;
    m3u8->last_file_end = self->current->last_file_end;
#endif
  }
  GST_M3U8_CLIENT_UNLOCK (self);
}

static gboolean
gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate;
  GList *walk, *current_file = NULL;
  GstClockTime current_pos, target_pos;
  gint64 current_sequence;
  GstM3U8MediaFile *file;
  guint64 bitrate;
  gboolean snap_before, snap_after, snap_nearest, keyunit;
  gboolean reverse;

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  bitrate = gst_hls_demux_get_bitrate (hlsdemux);

  /* properly cleanup pending decryption status */
  if (flags & GST_SEEK_FLAG_FLUSH) {
    gst_hls_demux_clear_all_pending_data (hlsdemux);
  }

  /* Use I-frame variants for trick modes */
  if (hlsdemux->main->iframe_lists && rate < -1.0
      && demux->segment.rate >= -1.0 && demux->segment.rate <= 1.0) {
    GError *err = NULL;

    GST_M3U8_CLIENT_LOCK (hlsdemux->client);
    /* Switch to I-frame variant */
    hlsdemux->main->current_variant = hlsdemux->main->iframe_lists;
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    gst_hls_demux_set_current (hlsdemux, hlsdemux->main->iframe_lists->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;

    gst_hls_demux_change_playlist (hlsdemux, bitrate / ABS (rate), NULL);
  } else if (rate > -1.0 && rate <= 1.0 && (demux->segment.rate < -1.0
          || demux->segment.rate > 1.0)) {
    GError *err = NULL;
    GST_M3U8_CLIENT_LOCK (hlsdemux->client);
    /* Switch to normal variant */
    hlsdemux->main->current_variant = hlsdemux->main->lists;
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    gst_hls_demux_set_current (hlsdemux, hlsdemux->main->lists->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;
    /* TODO why not continue using the same? that was being used up to now? */
    gst_hls_demux_change_playlist (hlsdemux, bitrate, NULL);
  }
  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  file = GST_M3U8_MEDIA_FILE (hlsdemux->current->files->data);
  current_sequence = file->sequence;
  current_pos = 0;
  reverse = rate < 0;
  target_pos = reverse ? stop : start;

  /* Snap to segment boundary. Improves seek performance on slow machines. */
  keyunit = ! !(flags & GST_SEEK_FLAG_KEY_UNIT);
  snap_nearest =
      (flags & GST_SEEK_FLAG_SNAP_NEAREST) == GST_SEEK_FLAG_SNAP_NEAREST;
  snap_before = ! !(flags & GST_SEEK_FLAG_SNAP_BEFORE);
  snap_after = ! !(flags & GST_SEEK_FLAG_SNAP_AFTER);

  /* FIXME: Here we need proper discont handling */
  for (walk = hlsdemux->current->files; walk; walk = walk->next) {
    file = walk->data;

    current_sequence = file->sequence;
    current_file = walk;
    if ((!reverse && snap_after) || snap_nearest) {
      if (current_pos >= target_pos)
        break;
      if (snap_nearest && target_pos - current_pos < file->duration / 2)
        break;
    } else if (reverse && snap_after) {
      /* check if the next fragment is our target, in this case we want to
       * start from the previous fragment */
      GstClockTime next_pos = current_pos + file->duration;

      if (next_pos <= target_pos && target_pos < next_pos + file->duration) {
        break;
      }
    } else if (current_pos <= target_pos
        && target_pos < current_pos + file->duration) {
      break;
    }
    current_pos += file->duration;
  }

  if (walk == NULL) {
    GST_DEBUG_OBJECT (demux, "seeking further than track duration");
    current_sequence++;
  }

  GST_DEBUG_OBJECT (demux, "seeking to sequence %u", (guint) current_sequence);
  for (walk = demux->streams; walk != NULL; walk = walk->next)
    GST_HLS_DEMUX_STREAM_CAST (walk->data)->reset_pts = TRUE;
  hlsdemux->current->sequence = current_sequence;
  hlsdemux->current->current_file =
      current_file ? current_file : hlsdemux->current->files;
  hlsdemux->current->sequence_position = current_pos;
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

  /* Play from the end of the current selected segment */
  if (reverse && (snap_before || snap_after || snap_nearest))
    current_pos += file->duration;

  if (keyunit || snap_before || snap_after || snap_nearest) {
    if (!reverse)
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          current_pos, stop_type, stop, NULL);
    else
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, current_pos, NULL);
  }

  return TRUE;
}

static GstFlowReturn
gst_hls_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  if (!gst_hls_demux_update_playlist (hlsdemux, TRUE, NULL))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static gboolean
gst_hls_demux_setup_streams (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstAdaptiveDemuxStream *stream;
  GstHLSDemuxStream *hlsdemux_stream;

  /* only 1 output supported */
  gst_hls_demux_clear_all_pending_data (hlsdemux);
  stream = gst_adaptive_demux_stream_new (demux,
      gst_hls_demux_create_pad (hlsdemux));

  hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  hlsdemux_stream->do_typefind = TRUE;
  hlsdemux_stream->reset_pts = TRUE;

  return TRUE;
}


static gboolean
gst_hls_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gchar *playlist = NULL;

  hlsdemux->main = gst_m3u8_new ();
  gst_m3u8_set_uri (hlsdemux->main, demux->manifest_uri,
      demux->manifest_base_uri, NULL);
  hlsdemux->current = NULL;

  GST_INFO_OBJECT (demux, "Changed location: %s (base uri: %s)",
      demux->manifest_uri, GST_STR_NULL (demux->manifest_base_uri));

  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Error validating first playlist.");
    return FALSE;
  }

  if (!gst_m3u8_update (hlsdemux->main, playlist)) {
    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the playlist */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."), (NULL));
    return FALSE;
  }

  /* If this playlist is a variant playlist, select the first one
   * and update it */
  if (gst_m3u8_has_variant_playlist (hlsdemux->main)) {
    GstM3U8 *child = NULL;
    GError *err = NULL;

    if (demux->connection_speed == 0) {
      GST_M3U8_CLIENT_LOCK (hlsdemux->client);
      child = hlsdemux->main->current_variant->data;
      GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    } else {
      GList *tmp = gst_m3u8_get_playlist_for_bitrate (hlsdemux->main,
          demux->connection_speed);
      GST_M3U8_CLIENT_LOCK (hlsdemux->client);
      hlsdemux->main->current_variant = tmp;
      GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

      child = GST_M3U8 (tmp->data);
    }

    gst_hls_demux_set_current (hlsdemux, child);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not fetch the child playlist",
          err);
      return FALSE;
    }
  } else {
    gst_hls_demux_set_current (hlsdemux, hlsdemux->main);
  }

  return gst_hls_demux_setup_streams (demux);
}

static GstClockTime
gst_hls_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  if (hlsdemux->current != NULL)
    duration = gst_m3u8_get_duration (hlsdemux->current);

  return duration;
}

static gboolean
gst_hls_demux_is_live (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean is_live = FALSE;

  if (hlsdemux->current)
    is_live = gst_m3u8_is_live (hlsdemux->current);

  return is_live;
}

static const GstHLSKey *
gst_hls_demux_get_key (GstHLSDemux * demux, const gchar * key_url,
    const gchar * referer, gboolean allow_cache)
{
  GstFragment *key_fragment;
  GstBuffer *key_buffer;
  GstHLSKey *key;
  GError *err = NULL;

  GST_LOG_OBJECT (demux, "Looking up key for key url %s", key_url);

  g_mutex_lock (&demux->keys_lock);

  key = g_hash_table_lookup (demux->keys, key_url);

  if (key != NULL) {
    GST_LOG_OBJECT (demux, "Found key for key url %s in key cache", key_url);
    goto out;
  }

  GST_INFO_OBJECT (demux, "Fetching key %s", key_url);

  key_fragment =
      gst_uri_downloader_fetch_uri (GST_ADAPTIVE_DEMUX (demux)->downloader,
      key_url, referer, FALSE, FALSE, allow_cache, &err);

  if (key_fragment == NULL) {
    GST_WARNING_OBJECT (demux, "Failed to download key to decrypt data: %s",
        err ? err->message : "error");
    g_clear_error (&err);
    goto out;
  }

  key_buffer = gst_fragment_get_buffer (key_fragment);

  key = g_new0 (GstHLSKey, 1);
  if (gst_buffer_extract (key_buffer, 0, key->data, 16) < 16)
    GST_WARNING_OBJECT (demux, "Download decryption key is too short!");

  g_hash_table_insert (demux->keys, g_strdup (key_url), key);

  gst_buffer_unref (key_buffer);
  g_object_unref (key_fragment);

out:

  g_mutex_unlock (&demux->keys_lock);

  if (key != NULL)
    GST_MEMDUMP_OBJECT (demux, "Key", key->data, 16);

  return key;
}

static gboolean
gst_hls_demux_start_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  const GstHLSKey *key;

  gst_hls_demux_stream_clear_pending_data (hls_stream);

  /* If no decryption is needed, there's nothing to be done here */
  if (hls_stream->current_key == NULL)
    return TRUE;

  key = gst_hls_demux_get_key (hlsdemux, hls_stream->current_key,
      hlsdemux->main ? hlsdemux->main->uri : NULL,
      hlsdemux->current ? hlsdemux->current->allowcache : TRUE);

  if (key == NULL)
    goto key_failed;

  gst_hls_demux_stream_decrypt_start (hls_stream, key->data,
      hls_stream->current_iv);

  return TRUE;

key_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        ("Couldn't retrieve key for decryption"), (NULL));
    GST_WARNING_OBJECT (demux, "Failed to decrypt data");
    return FALSE;
  }
}

static GstFlowReturn
gst_hls_demux_handle_buffer (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer, gboolean force)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  if (buffer == NULL)
    return GST_FLOW_OK;

  if (G_UNLIKELY (hls_stream->do_typefind)) {
    GstCaps *caps = NULL;
    GstMapInfo info;
    guint buffer_size;
    GstTypeFindProbability prob = GST_TYPE_FIND_NONE;

    if (hls_stream->pending_typefind_buffer)
      buffer = gst_buffer_append (hls_stream->pending_typefind_buffer, buffer);
    hls_stream->pending_typefind_buffer = NULL;

    gst_buffer_map (buffer, &info, GST_MAP_READ);
    buffer_size = info.size;

    /* Typefind could miss if buffer is too small. In this case we
     * will retry later */
    if (buffer_size >= (2 * 1024)) {
      caps =
          gst_type_find_helper_for_data (GST_OBJECT_CAST (hlsdemux), info.data,
          info.size, &prob);
    }
    gst_buffer_unmap (buffer, &info);

    if (G_UNLIKELY (!caps)) {
      /* Only fail typefinding if we already a good amount of data
       * and we still don't know the type */
      if (buffer_size > (2 * 1024 * 1024) || force) {
        GST_ELEMENT_ERROR (hlsdemux, STREAM, TYPE_NOT_FOUND,
            ("Could not determine type of stream"), (NULL));
        gst_buffer_unref (buffer);
        return GST_FLOW_NOT_NEGOTIATED;
      }

      hls_stream->pending_typefind_buffer = buffer;

      return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT (hlsdemux, "Typefind result: %" GST_PTR_FORMAT " prob:%d",
        caps, prob);

    gst_adaptive_demux_stream_set_caps (stream, caps);
    hls_stream->do_typefind = FALSE;
  }
  g_assert (hls_stream->pending_typefind_buffer == NULL);

  if (buffer) {
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_OFFSET (buffer) = hls_stream->current_offset;
    hls_stream->current_offset += gst_buffer_get_size (buffer);
    GST_BUFFER_OFFSET_END (buffer) = hls_stream->current_offset;
    return gst_adaptive_demux_stream_push_buffer (stream, buffer);
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_finish_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstFlowReturn ret = GST_FLOW_OK;

  if (hls_stream->current_key)
    gst_hls_demux_stream_decrypt_end (hls_stream);

  if (stream->last_ret == GST_FLOW_OK) {
    if (hls_stream->pending_decrypted_buffer) {
      if (hls_stream->current_key) {
        GstMapInfo info;
        gssize unpadded_size;

        /* Handle pkcs7 unpadding here */
        gst_buffer_map (hls_stream->pending_decrypted_buffer, &info,
            GST_MAP_READ);
        unpadded_size = info.size - info.data[info.size - 1];
        gst_buffer_unmap (hls_stream->pending_decrypted_buffer, &info);

        gst_buffer_resize (hls_stream->pending_decrypted_buffer, 0,
            unpadded_size);
      }

      ret =
          gst_hls_demux_handle_buffer (demux, stream,
          hls_stream->pending_decrypted_buffer, TRUE);
      hls_stream->pending_decrypted_buffer = NULL;
    }
  }
  gst_hls_demux_stream_clear_pending_data (hls_stream);

  if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED)
    return gst_adaptive_demux_stream_advance_fragment (demux, stream,
        stream->fragment.duration);
  return ret;
}

static GstFlowReturn
gst_hls_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  if (hls_stream->current_offset == -1)
    hls_stream->current_offset =
        GST_BUFFER_OFFSET_IS_VALID (buffer) ? GST_BUFFER_OFFSET (buffer) : 0;

  /* Is it encrypted? */
  if (hls_stream->current_key) {
    GError *err = NULL;
    gsize size;
    GstBuffer *tmp_buffer;

    if (hls_stream->pending_encrypted_data == NULL)
      hls_stream->pending_encrypted_data = gst_adapter_new ();

    gst_adapter_push (hls_stream->pending_encrypted_data, buffer);
    size = gst_adapter_available (hls_stream->pending_encrypted_data);

    /* must be a multiple of 16 */
    size &= (~0xF);

    if (size == 0) {
      return GST_FLOW_OK;
    }

    buffer = gst_adapter_take_buffer (hls_stream->pending_encrypted_data, size);
    buffer =
        gst_hls_demux_decrypt_fragment (hlsdemux, hls_stream, buffer, &err);
    if (buffer == NULL) {
      GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Failed to decrypt buffer"),
          ("decryption failed %s", err->message));
      g_error_free (err);
      return GST_FLOW_ERROR;
    }

    tmp_buffer = hls_stream->pending_decrypted_buffer;
    hls_stream->pending_decrypted_buffer = buffer;
    buffer = tmp_buffer;
  }

  return gst_hls_demux_handle_buffer (demux, stream, buffer, FALSE);
}

static void
gst_hls_demux_stream_free (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  if (hls_stream->pending_encrypted_data)
    g_object_unref (hls_stream->pending_encrypted_data);

  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);

  if (hls_stream->current_key) {
    g_free (hls_stream->current_key);
    hls_stream->current_key = NULL;
  }
  if (hls_stream->current_iv) {
    g_free (hls_stream->current_iv);
    hls_stream->current_iv = NULL;
  }
  gst_hls_demux_stream_decrypt_end (hls_stream);
}

static gboolean
gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  gboolean has_next;

  has_next = gst_m3u8_has_next_fragment (hlsdemux->current,
      stream->demux->segment.rate > 0);

  return has_next;
}

static GstFlowReturn
gst_hls_demux_advance_fragment (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);

  gst_m3u8_advance_fragment (hlsdemux->current,
      stream->demux->segment.rate > 0);
  hlsdemux_stream->reset_pts = FALSE;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_update_fragment_info (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  GstM3U8MediaFile *file;
  GstClockTime sequence_pos;
  gboolean discont;

  file = gst_m3u8_get_next_fragment (hlsdemux->current,
      stream->demux->segment.rate > 0, &sequence_pos, &discont);

  if (file == NULL) {
    GST_INFO_OBJECT (hlsdemux, "This playlist doesn't contain more fragments");
    return GST_FLOW_EOS;
  }

  if (stream->discont)
    discont = TRUE;

  /* set up our source for download */
  if (hlsdemux_stream->reset_pts || discont
      || stream->demux->segment.rate < 0.0) {
    stream->fragment.timestamp = sequence_pos;
  } else {
    stream->fragment.timestamp = GST_CLOCK_TIME_NONE;
  }

  g_free (hlsdemux_stream->current_key);
  hlsdemux_stream->current_key = g_strdup (file->key);
  g_free (hlsdemux_stream->current_iv);
  hlsdemux_stream->current_iv = g_memdup (file->iv, sizeof (file->iv));

  g_free (stream->fragment.uri);
  stream->fragment.uri = g_strdup (file->uri);
  stream->fragment.range_start = file->offset;
  if (file->size != -1)
    stream->fragment.range_end = file->offset + file->size - 1;
  else
    stream->fragment.range_end = -1;

  stream->fragment.duration = file->duration;

  if (discont)
    stream->discont = TRUE;

  return GST_FLOW_OK;
}

static gboolean
gst_hls_demux_select_bitrate (GstAdaptiveDemuxStream * stream, guint64 bitrate)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (stream->demux);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  gboolean changed = FALSE;

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  if (!hlsdemux->main->lists) {
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    return FALSE;
  }
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

  /* FIXME: Currently several issues have be found when letting bitrate adaptation
   * happen using trick modes (such as 'All streams finished without buffers') and
   * the adaptive algorithm does not properly behave. */
  if (demux->segment.rate != 1.0)
    return FALSE;

  gst_hls_demux_change_playlist (hlsdemux, bitrate, &changed);
  if (changed)
    gst_hls_demux_setup_streams (GST_ADAPTIVE_DEMUX_CAST (hlsdemux));
  return changed;
}

static void
gst_hls_demux_reset (GstAdaptiveDemux * ademux)
{
  GstHLSDemux *demux = GST_HLS_DEMUX_CAST (ademux);

  if (demux->main) {
    gst_m3u8_unref (demux->main);
    demux->main = NULL;
  }
  demux->current = NULL;
  demux->srcpad_counter = 0;

  gst_hls_demux_clear_all_pending_data (demux);
}

static gchar *
gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf)
{
  GstMapInfo info;
  gchar *playlist;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    goto map_error;

  if (!g_utf8_validate ((gchar *) info.data, info.size, NULL))
    goto validate_error;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (info.size + 1);
  memcpy (playlist, info.data, info.size);

  gst_buffer_unmap (buf, &info);
  return playlist;

validate_error:
  gst_buffer_unmap (buf, &info);
map_error:
  return NULL;
}

static gint
gst_hls_demux_find_m3u8_list_match (const GstM3U8 * a, const GstM3U8 * b)
{
  if (g_strcmp0 (a->name, b->name) == 0 &&
      a->bandwidth == b->bandwidth &&
      a->program_id == b->program_id &&
      g_strcmp0 (a->codecs, b->codecs) == 0 &&
      a->width == b->width &&
      a->height == b->height && a->iframe == b->iframe) {
    return 0;
  }

  return 1;
}

static gboolean
gst_hls_demux_update_variant_playlist (GstHLSDemux * hlsdemux, gchar * data,
    const gchar * uri, const gchar * base_uri)
{
  GstM3U8 *new_main, *old;
  gboolean ret = FALSE;
  GList *l, *unmatched_lists;

  new_main = gst_m3u8_new ();
  gst_m3u8_set_uri (new_main, uri, base_uri, NULL);
  if (gst_m3u8_update (new_main, data)) {
    if (!new_main->lists) {
      GST_ERROR
          ("Cannot update variant playlist: New playlist is not a variant playlist");
      gst_m3u8_unref (new_main);
      return FALSE;
    }

    GST_M3U8_CLIENT_LOCK (self);

    if (!hlsdemux->main->lists) {
      GST_ERROR
          ("Cannot update variant playlist: Current playlist is not a variant playlist");
      goto out;
    }

    /* Now see if the variant playlist still has the same lists */
    unmatched_lists = g_list_copy (hlsdemux->main->lists);
    for (l = new_main->lists; l != NULL; l = l->next) {
      GList *match = g_list_find_custom (unmatched_lists, l->data,
          (GCompareFunc) gst_hls_demux_find_m3u8_list_match);
      if (match) {
        unmatched_lists = g_list_delete_link (unmatched_lists, match);
        // FIXME: copy over state variables of playlist, or keep old instance
        GST_ERROR ("FIXME: copy over state variables of playlist");
      }
    }

    if (unmatched_lists != NULL) {
      GST_WARNING ("Unable to match all playlists");

      for (l = unmatched_lists; l != NULL; l = l->next) {
        if (l->data == hlsdemux->current) {
          GST_WARNING ("Unable to match current playlist");
        }
      }

      g_list_free (unmatched_lists);
    }

    /* Switch out the variant playlist, steal it from new_client */
    old = hlsdemux->main;

    hlsdemux->main = new_main;

    if (hlsdemux->main->lists)
      hlsdemux->current = hlsdemux->main->current_variant->data;
    else
      hlsdemux->current = hlsdemux->main;

    gst_m3u8_unref (old);

    ret = TRUE;

  out:
    GST_M3U8_CLIENT_UNLOCK (self);
  }

  return ret;
}

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean update,
    GError ** err)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  GstFragment *download;
  GstBuffer *buf;
  gchar *playlist;
  gboolean main_checked = FALSE;
  gchar *uri, *main_uri;

retry:
  uri = gst_m3u8_get_uri (demux->current);
  main_uri = gst_m3u8_get_uri (demux->main);
  download =
      gst_uri_downloader_fetch_uri (adaptive_demux->downloader, uri, main_uri,
      TRUE, TRUE, TRUE, err);
  g_free (main_uri);
  if (download == NULL) {
    gchar *base_uri;

    if (!update || main_checked || !gst_m3u8_has_variant_playlist (demux->main)) {
      g_free (uri);
      return FALSE;
    }
    g_clear_error (err);
    main_uri = gst_m3u8_get_uri (demux->main);
    GST_INFO_OBJECT (demux,
        "Updating playlist %s failed, attempt to refresh variant playlist %s",
        uri, main_uri);
    download =
        gst_uri_downloader_fetch_uri (adaptive_demux->downloader,
        main_uri, NULL, TRUE, TRUE, TRUE, err);
    g_free (main_uri);
    if (download == NULL) {
      g_free (uri);
      return FALSE;
    }

    buf = gst_fragment_get_buffer (download);
    playlist = gst_hls_src_buf_to_utf8_playlist (buf);
    gst_buffer_unref (buf);

    if (playlist == NULL) {
      GST_WARNING_OBJECT (demux,
          "Failed to validate variant playlist encoding");
      g_free (uri);
      g_object_unref (download);
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
          "Couldn't validate playlist encoding");
      return FALSE;
    }

    g_free (uri);
    if (download->redirect_permanent && download->redirect_uri) {
      uri = download->redirect_uri;
      base_uri = NULL;
    } else {
      uri = download->uri;
      base_uri = download->redirect_uri;
    }

    if (!gst_hls_demux_update_variant_playlist (demux, playlist, uri, base_uri)) {
      GST_WARNING_OBJECT (demux, "Failed to update the variant playlist");
      g_object_unref (download);
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
          "Couldn't update playlist");
      return FALSE;
    }

    g_object_unref (download);

    main_checked = TRUE;
    goto retry;
  }
  g_free (uri);

  /* Set the base URI of the playlist to the redirect target if any */
  GST_M3U8_CLIENT_LOCK (demux->client);
  g_free (demux->current->uri);
  g_free (demux->current->base_uri);
  if (download->redirect_permanent && download->redirect_uri) {
    demux->current->uri = g_strdup (download->redirect_uri);
    demux->current->base_uri = NULL;
  } else {
    demux->current->uri = g_strdup (download->uri);
    demux->current->base_uri = g_strdup (download->redirect_uri);
  }
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  buf = gst_fragment_get_buffer (download);
  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  gst_buffer_unref (buf);
  g_object_unref (download);

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't validate playlist encoding");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
        "Couldn't validate playlist encoding");
    return FALSE;
  }

  if (!gst_m3u8_update (demux->current, playlist)) {
    GST_WARNING_OBJECT (demux, "Couldn't update playlist");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Couldn't update playlist");
    return FALSE;
  }

  /* If it's a live source, do not let the sequence number go beyond
   * three fragments before the end of the list */
  if (update == FALSE && demux->current && gst_m3u8_is_live (demux->current)) {
    gint64 last_sequence, first_sequence;

    GST_M3U8_CLIENT_LOCK (demux->client);
    last_sequence =
        GST_M3U8_MEDIA_FILE (g_list_last (demux->current->files)->
        data)->sequence;
    first_sequence =
        GST_M3U8_MEDIA_FILE (demux->current->files->data)->sequence;

    GST_DEBUG_OBJECT (demux,
        "sequence:%" G_GINT64_FORMAT " , first_sequence:%" G_GINT64_FORMAT
        " , last_sequence:%" G_GINT64_FORMAT, demux->current->sequence,
        first_sequence, last_sequence);
    if (demux->current->sequence >= last_sequence - 3) {
      //demux->need_segment = TRUE;
      /* Make sure we never go below the minimum sequence number */
      demux->current->sequence = MAX (first_sequence, last_sequence - 3);
      GST_DEBUG_OBJECT (demux,
          "Sequence is beyond playlist. Moving back to %" G_GINT64_FORMAT,
          demux->current->sequence);
    }
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  } else if (demux->current && !gst_m3u8_is_live (demux->current)) {
    GstClockTime current_pos, target_pos;
    guint sequence = 0;
    GList *walk;

    /* Sequence numbers are not guaranteed to be the same in different
     * playlists, so get the correct fragment here based on the current
     * position
     */
    GST_M3U8_CLIENT_LOCK (demux->client);

    /* Valid because hlsdemux only has a single output */
    if (GST_ADAPTIVE_DEMUX_CAST (demux)->streams) {
      GstAdaptiveDemuxStream *stream =
          GST_ADAPTIVE_DEMUX_CAST (demux)->streams->data;
      target_pos = stream->segment.position;
    } else {
      target_pos = 0;
    }
    if (GST_CLOCK_TIME_IS_VALID (demux->current->sequence_position)) {
      target_pos = MAX (target_pos, demux->current->sequence_position);
    }

    GST_LOG_OBJECT (demux, "Looking for sequence position %"
        GST_TIME_FORMAT " in updated playlist", GST_TIME_ARGS (target_pos));

    current_pos = 0;
    for (walk = demux->current->files; walk; walk = walk->next) {
      GstM3U8MediaFile *file = walk->data;

      sequence = file->sequence;
      if (current_pos <= target_pos
          && target_pos < current_pos + file->duration) {
        break;
      }
      current_pos += file->duration;
    }
    /* End of playlist */
    if (!walk)
      sequence++;
    demux->current->sequence = sequence;
    demux->current->sequence_position = current_pos;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  }

  return TRUE;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, guint max_bitrate,
    gboolean * changed)
{
  GList *previous_variant, *current_variant;
  gint old_bandwidth, new_bandwidth;
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX_CAST (demux);
  GstAdaptiveDemuxStream *stream;

  g_return_val_if_fail (adaptive_demux->streams != NULL, FALSE);

  stream = adaptive_demux->streams->data;

  previous_variant = demux->main->current_variant;
  current_variant =
      gst_m3u8_get_playlist_for_bitrate (demux->main, max_bitrate);

  GST_M3U8_CLIENT_LOCK (demux->client);

retry_failover_protection:
  old_bandwidth = GST_M3U8 (previous_variant->data)->bandwidth;
  new_bandwidth = GST_M3U8 (current_variant->data)->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }

  demux->main->current_variant = current_variant;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_hls_demux_set_current (demux, current_variant->data);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);

  if (gst_hls_demux_update_playlist (demux, TRUE, NULL)) {
    gchar *uri;
    gchar *main_uri;
    uri = gst_m3u8_get_uri (demux->current);
    main_uri = gst_m3u8_get_uri (demux->main);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux),
            gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                "manifest-uri", G_TYPE_STRING,
                main_uri, "uri", G_TYPE_STRING,
                uri, "bitrate", G_TYPE_INT, new_bandwidth, NULL)));
    g_free (uri);
    g_free (main_uri);
    if (changed)
      *changed = TRUE;
    stream->discont = TRUE;
  } else {
    GList *failover = NULL;

    GST_INFO_OBJECT (demux, "Unable to update playlist. Switching back");
    GST_M3U8_CLIENT_LOCK (demux->client);

    failover = g_list_previous (current_variant);
    if (failover && new_bandwidth == GST_M3U8 (failover->data)->bandwidth) {
      current_variant = failover;
      goto retry_failover_protection;
    }

    demux->main->current_variant = previous_variant;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_hls_demux_set_current (demux, previous_variant->data);
    /*  Try a lower bitrate (or stop if we just tried the lowest) */
    if (GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->main->iframe_lists)->data)->bandwidth)
      return FALSE;
    else if (!GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->main->lists)->data)->bandwidth)
      return FALSE;
    else
      return gst_hls_demux_change_playlist (demux, new_bandwidth - 1, changed);
  }

  return TRUE;
}

#if defined(HAVE_OPENSSL)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  EVP_CIPHER_CTX_init (&stream->aes_ctx);
  if (!EVP_DecryptInit_ex (&stream->aes_ctx, EVP_aes_128_cbc (), NULL, key_data,
          iv_data))
    return FALSE;
  EVP_CIPHER_CTX_set_padding (&stream->aes_ctx, 0);
  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  int len, flen = 0;

  if (G_UNLIKELY (length > G_MAXINT || length % 16 != 0))
    return FALSE;

  len = (int) length;
  if (!EVP_DecryptUpdate (&stream->aes_ctx, decrypted_data, &len,
          encrypted_data, len))
    return FALSE;
  EVP_DecryptFinal_ex (&stream->aes_ctx, decrypted_data + len, &flen);
  g_return_val_if_fail (len + flen == length, FALSE);
  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  EVP_CIPHER_CTX_cleanup (&stream->aes_ctx);
}

#elif defined(HAVE_NETTLE)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  aes_set_decrypt_key (&stream->aes_ctx.ctx, 16, key_data);
  CBC_SET_IV (&stream->aes_ctx, iv_data);

  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  if (length % 16 != 0)
    return FALSE;

  CBC_DECRYPT (&stream->aes_ctx, aes_decrypt, length, decrypted_data,
      encrypted_data);

  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  /* NOP */
}

#else
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&stream->aes_ctx, GCRY_CIPHER_AES128,
      GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (stream->aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (stream->aes_ctx, iv_data, 16);
  if (!err)
    ret = TRUE;

out:
  if (!ret)
    if (stream->aes_ctx)
      gcry_cipher_close (stream->aes_ctx);

  return ret;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  gcry_error_t err = 0;

  err = gcry_cipher_decrypt (stream->aes_ctx, decrypted_data, length,
      encrypted_data, length);

  return err == 0;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  if (stream->aes_ctx) {
    gcry_cipher_close (stream->aes_ctx);
    stream->aes_ctx = NULL;
  }
}
#endif

static GstBuffer *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux, GstHLSDemuxStream * stream,
    GstBuffer * encrypted_buffer, GError ** err)
{
  GstBuffer *decrypted_buffer = NULL;
  GstMapInfo encrypted_info, decrypted_info;

  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (!decrypt_fragment (stream, encrypted_info.size,
          encrypted_info.data, decrypted_info.data))
    goto decrypt_error;


  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);

  return decrypted_buffer;

decrypt_error:
  GST_ERROR_OBJECT (demux, "Failed to decrypt fragment");
  g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_DECRYPT,
      "Failed to decrypt fragment");

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);
  gst_buffer_unref (decrypted_buffer);

  return NULL;
}

static gint64
gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime target_duration;

  target_duration = gst_m3u8_get_target_duration (hlsdemux->current);

  return gst_util_uint64_scale (target_duration, G_USEC_PER_SEC, GST_SECOND);
}

static gboolean
gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean ret = FALSE;

  if (hlsdemux->current)
    ret = gst_m3u8_get_seek_range (hlsdemux->current, start, stop);

  return ret;
}
