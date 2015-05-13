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
 * gst-launch souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin2 ! videoconvert ! videoscale ! autovideosink
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

enum
{
  PROP_0,

  PROP_FRAGMENTS_CACHE,
  PROP_LAST
};

#define DEFAULT_FRAGMENTS_CACHE 1

/* GObject */
static void gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hls_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_hls_demux_dispose (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);
static gboolean gst_hls_demux_set_location (GstHLSDemux * demux,
    const gchar * uri, const gchar * base_uri);
static gchar *gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf);

static gboolean gst_hls_demux_change_playlist (GstHLSDemux * demux,
    guint max_bitrate, gboolean * changed);
static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data);
static void gst_hls_demux_decrypt_end (GstHLSDemux * demux);

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
    GstAdaptiveDemuxStream * stream);
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
gst_hls_demux_dispose (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
  gst_m3u8_client_free (demux->client);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
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

  gobject_class->set_property = gst_hls_demux_set_property;
  gobject_class->get_property = gst_hls_demux_get_property;
  gobject_class->dispose = gst_hls_demux_dispose;

#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_FRAGMENTS_CACHE,
      g_param_spec_uint ("fragments-cache", "Fragments cache",
          "Number of fragments needed to be cached to start playing "
          "(DEPRECATED: Has no effect since 1.3.1)",
          1, G_MAXUINT, DEFAULT_FRAGMENTS_CACHE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED));
#endif

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

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

  adaptivedemux_class->start_fragment = gst_hls_demux_start_fragment;
  adaptivedemux_class->finish_fragment = gst_hls_demux_finish_fragment;
  adaptivedemux_class->data_received = gst_hls_demux_data_received;

  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

static void
gst_hls_demux_init (GstHLSDemux * demux)
{
  demux->do_typefind = TRUE;
}

static void
gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      g_value_set_uint (value, 1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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
  GstPadTemplate *tmpl;
  GstPad *pad;

  name = g_strdup_printf ("src_%u", hlsdemux->srcpad_counter++);
  tmpl = gst_static_pad_template_get (&srctemplate);
  pad = gst_ghost_pad_new_no_target_from_template (name, tmpl);
  gst_object_unref (tmpl);
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

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  bitrate = gst_hls_demux_get_bitrate (hlsdemux);

  /* properly cleanup pending decryption status */
  if (flags & GST_SEEK_FLAG_FLUSH) {
    gst_hls_demux_decrypt_end (hlsdemux);
  }

  /* Use I-frame variants for trick modes */
  if (hlsdemux->client->main->iframe_lists && rate < -1.0
      && demux->segment.rate >= -1.0 && demux->segment.rate <= 1.0) {
    GError *err = NULL;

    GST_M3U8_CLIENT_LOCK (hlsdemux->client);
    /* Switch to I-frame variant */
    hlsdemux->client->main->current_variant =
        hlsdemux->client->main->iframe_lists;
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    gst_m3u8_client_set_current (hlsdemux->client,
        hlsdemux->client->main->iframe_lists->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;
    hlsdemux->new_playlist = TRUE;
    hlsdemux->do_typefind = TRUE;

    gst_hls_demux_change_playlist (hlsdemux, bitrate / ABS (rate), NULL);
  } else if (rate > -1.0 && rate <= 1.0 && (demux->segment.rate < -1.0
          || demux->segment.rate > 1.0)) {
    GError *err = NULL;
    GST_M3U8_CLIENT_LOCK (hlsdemux->client);
    /* Switch to normal variant */
    hlsdemux->client->main->current_variant = hlsdemux->client->main->lists;
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    gst_m3u8_client_set_current (hlsdemux->client,
        hlsdemux->client->main->lists->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;
    hlsdemux->new_playlist = TRUE;
    hlsdemux->do_typefind = TRUE;
    /* TODO why not continue using the same? that was being used up to now? */
    gst_hls_demux_change_playlist (hlsdemux, bitrate, NULL);
  }

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  file = GST_M3U8_MEDIA_FILE (hlsdemux->client->current->files->data);
  current_sequence = file->sequence;
  current_pos = 0;
  target_pos = rate > 0 ? start : stop;
  /* FIXME: Here we need proper discont handling */
  for (walk = hlsdemux->client->current->files; walk; walk = walk->next) {
    file = walk->data;

    current_sequence = file->sequence;
    current_file = walk;
    if (current_pos <= target_pos && target_pos < current_pos + file->duration) {
      break;
    }
    current_pos += file->duration;
  }

  if (walk == NULL) {
    GST_DEBUG_OBJECT (demux, "seeking further than track duration");
    current_sequence++;
  }

  GST_DEBUG_OBJECT (demux, "seeking to sequence %u", (guint) current_sequence);
  hlsdemux->reset_pts = TRUE;
  hlsdemux->client->sequence = current_sequence;
  hlsdemux->client->current_file =
      current_file ? current_file : hlsdemux->client->current->files;
  hlsdemux->client->sequence_position = current_pos;
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

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

  /* only 1 output supported */
  gst_adaptive_demux_stream_new (demux, gst_hls_demux_create_pad (hlsdemux));

  hlsdemux->reset_pts = TRUE;

  return TRUE;
}


static gboolean
gst_hls_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gchar *playlist = NULL;

  gst_hls_demux_set_location (hlsdemux, demux->manifest_uri,
      demux->manifest_base_uri);

  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Error validating first playlist.");
    return FALSE;
  } else if (!gst_m3u8_client_update (hlsdemux->client, playlist)) {
    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the playlist */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."), (NULL));
    return FALSE;
  }

  /* If this playlist is a variant playlist, select the first one
   * and update it */
  if (gst_m3u8_client_has_variant_playlist (hlsdemux->client)) {
    GstM3U8 *child = NULL;
    GError *err = NULL;
    GList *current_variant = NULL;

    current_variant =
        gst_m3u8_client_get_playlist_for_bitrate (hlsdemux->client,
        demux->connection_speed);
    child = GST_M3U8 (current_variant->data);

    gst_m3u8_client_set_current (hlsdemux->client, child);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not fetch the child playlist",
          err);
      return FALSE;
    }
  }

  return gst_hls_demux_setup_streams (demux);
}

static GstClockTime
gst_hls_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  return gst_m3u8_client_get_duration (hlsdemux->client);
}

static gboolean
gst_hls_demux_is_live (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  return gst_m3u8_client_is_live (hlsdemux->client);
}

static gboolean
gst_hls_demux_start_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  if (hlsdemux->current_key) {
    GError *err = NULL;
    GstFragment *key_fragment;
    GstBuffer *key_buffer;
    GstMapInfo key_info;

    /* new key? */
    if (hlsdemux->key_url
        && strcmp (hlsdemux->key_url, hlsdemux->current_key) == 0) {
      key_fragment = g_object_ref (hlsdemux->key_fragment);
    } else {
      g_free (hlsdemux->key_url);
      hlsdemux->key_url = NULL;

      if (hlsdemux->key_fragment)
        g_object_unref (hlsdemux->key_fragment);
      hlsdemux->key_fragment = NULL;

      GST_INFO_OBJECT (demux, "Fetching key %s", hlsdemux->current_key);
      key_fragment =
          gst_uri_downloader_fetch_uri (demux->downloader,
          hlsdemux->current_key, hlsdemux->client->main ?
          hlsdemux->client->main->uri : NULL, FALSE, FALSE,
          hlsdemux->client->current ? hlsdemux->client->current->
          allowcache : TRUE, &err);
      if (key_fragment == NULL)
        goto key_failed;
      hlsdemux->key_url = g_strdup (hlsdemux->current_key);
      hlsdemux->key_fragment = g_object_ref (key_fragment);
    }

    key_buffer = gst_fragment_get_buffer (key_fragment);
    gst_buffer_map (key_buffer, &key_info, GST_MAP_READ);

    gst_hls_demux_decrypt_start (hlsdemux, key_info.data, hlsdemux->current_iv);

    gst_buffer_unmap (key_buffer, &key_info);
    gst_buffer_unref (key_buffer);
    g_object_unref (key_fragment);
  }

  return TRUE;

key_failed:
  /* TODO Raise this error to the user */
  GST_WARNING_OBJECT (demux, "Failed to decrypt data");
  return FALSE;
}

static GstFlowReturn
gst_hls_demux_handle_buffer (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer, gboolean force)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  if (G_UNLIKELY (hlsdemux->do_typefind && buffer != NULL)) {
    GstCaps *caps = NULL;
    GstMapInfo info;
    guint buffer_size;
    GstTypeFindProbability prob = GST_TYPE_FIND_NONE;

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
      } else {
        if (hlsdemux->pending_buffer)
          hlsdemux->pending_buffer =
              gst_buffer_append (buffer, hlsdemux->pending_buffer);
        else
          hlsdemux->pending_buffer = buffer;
        return GST_FLOW_OK;
      }
    }

    GST_DEBUG_OBJECT (hlsdemux, "Typefind result: %" GST_PTR_FORMAT " prob:%d",
        caps, prob);

    if (!hlsdemux->input_caps
        || !gst_caps_is_equal (caps, hlsdemux->input_caps)) {
      gst_caps_replace (&hlsdemux->input_caps, caps);
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          hlsdemux->input_caps);
    }
    gst_adaptive_demux_stream_set_caps (stream, caps);
    hlsdemux->do_typefind = FALSE;
  }

  if (buffer)
    return gst_adaptive_demux_stream_push_buffer (stream, buffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_finish_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstFlowReturn ret = GST_FLOW_OK;

  if (hlsdemux->current_key)
    gst_hls_demux_decrypt_end (hlsdemux);

  /* ideally this should be empty, but this eos might have been
   * caused by an error on the source element */
  GST_DEBUG_OBJECT (demux, "Data still on the adapter when EOS was received"
      ": %" G_GSIZE_FORMAT, gst_adapter_available (stream->adapter));
  gst_adapter_clear (stream->adapter);

  if (stream->last_ret == GST_FLOW_OK) {
    if (hlsdemux->pending_buffer) {
      if (hlsdemux->current_key) {
        GstMapInfo info;
        gssize unpadded_size;

        /* Handle pkcs7 unpadding here */
        gst_buffer_map (hlsdemux->pending_buffer, &info, GST_MAP_READ);
        unpadded_size = info.size - info.data[info.size - 1];
        gst_buffer_unmap (hlsdemux->pending_buffer, &info);

        gst_buffer_resize (hlsdemux->pending_buffer, 0, unpadded_size);
      }

      ret =
          gst_hls_demux_handle_buffer (demux, stream, hlsdemux->pending_buffer,
          TRUE);
      hlsdemux->pending_buffer = NULL;
    }
  } else {
    if (hlsdemux->pending_buffer)
      gst_buffer_unref (hlsdemux->pending_buffer);
    hlsdemux->pending_buffer = NULL;
  }

  if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED)
    return gst_adaptive_demux_stream_advance_fragment (demux, stream,
        stream->fragment.duration);
  return ret;
}

static GstFlowReturn
gst_hls_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gsize available;
  GstBuffer *buffer = NULL;

  available = gst_adapter_available (stream->adapter);

  /* Is it encrypted? */
  if (hlsdemux->current_key) {
    GError *err = NULL;
    GstBuffer *tmp_buffer;

    /* must be a multiple of 16 */
    available = available & (~0xF);

    if (available == 0) {
      return GST_FLOW_OK;
    }

    buffer = gst_adapter_take_buffer (stream->adapter, available);
    buffer = gst_hls_demux_decrypt_fragment (hlsdemux, buffer, &err);
    if (buffer == NULL) {
      GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Failed to decrypt buffer"),
          ("decryption failed %s", err->message));
      g_error_free (err);
      return GST_FLOW_ERROR;
    }

    tmp_buffer = hlsdemux->pending_buffer;
    hlsdemux->pending_buffer = buffer;
    buffer = tmp_buffer;
  } else {
    buffer = gst_adapter_take_buffer (stream->adapter, available);
    if (hlsdemux->pending_buffer) {
      buffer = gst_buffer_append (hlsdemux->pending_buffer, buffer);
      hlsdemux->pending_buffer = NULL;
    }
  }

  return gst_hls_demux_handle_buffer (demux, stream, buffer, FALSE);
}

static gboolean
gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);

  return gst_m3u8_client_has_next_fragment (hlsdemux->client,
      stream->demux->segment.rate > 0);
}

static GstFlowReturn
gst_hls_demux_advance_fragment (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);

  gst_m3u8_client_advance_fragment (hlsdemux->client,
      stream->demux->segment.rate > 0);
  hlsdemux->reset_pts = FALSE;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_update_fragment_info (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
  gint64 range_start, range_end;
  gchar *key = NULL;
  guint8 *iv = NULL;

  if (!gst_m3u8_client_get_next_fragment (hlsdemux->client, &discont,
          &next_fragment_uri, &duration, &timestamp, &range_start, &range_end,
          &key, &iv, stream->demux->segment.rate > 0)) {
    GST_INFO_OBJECT (hlsdemux, "This playlist doesn't contain more fragments");
    return GST_FLOW_EOS;
  }

  /* set up our source for download */
  if (hlsdemux->reset_pts || discont) {
    stream->fragment.timestamp = timestamp;
  } else {
    stream->fragment.timestamp = GST_CLOCK_TIME_NONE;
  }

  if (hlsdemux->current_key)
    g_free (hlsdemux->current_key);
  hlsdemux->current_key = key;
  if (hlsdemux->current_iv)
    g_free (hlsdemux->current_iv);
  hlsdemux->current_iv = iv;
  g_free (stream->fragment.uri);
  stream->fragment.uri = next_fragment_uri;
  stream->fragment.range_start = range_start;
  stream->fragment.range_end = range_end;
  if (discont)
    stream->discont = discont;

  return GST_FLOW_OK;
}

static gboolean
gst_hls_demux_select_bitrate (GstAdaptiveDemuxStream * stream, guint64 bitrate)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (stream->demux);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  gboolean changed = FALSE;

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  if (!hlsdemux->client->main->lists) {
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

  demux->do_typefind = TRUE;
  demux->reset_pts = TRUE;

  g_free (demux->key_url);
  demux->key_url = NULL;

  if (demux->key_fragment)
    g_object_unref (demux->key_fragment);
  demux->key_fragment = NULL;

  if (demux->input_caps) {
    gst_caps_unref (demux->input_caps);
    demux->input_caps = NULL;
  }

  if (demux->client) {
    gst_m3u8_client_free (demux->client);
    demux->client = NULL;
  }
  /* TODO recreated on hls only if reset was not for disposing */
  demux->client = gst_m3u8_client_new ("", NULL);

  demux->srcpad_counter = 0;
  if (demux->pending_buffer)
    gst_buffer_unref (demux->pending_buffer);
  demux->pending_buffer = NULL;
  if (demux->current_key) {
    g_free (demux->current_key);
    demux->current_key = NULL;
  }
  if (demux->current_iv) {
    g_free (demux->current_iv);
    demux->current_iv = NULL;
  }

  gst_hls_demux_decrypt_end (demux);
}

static gboolean
gst_hls_demux_set_location (GstHLSDemux * demux, const gchar * uri,
    const gchar * base_uri)
{
  if (demux->client)
    gst_m3u8_client_free (demux->client);
  demux->client = gst_m3u8_client_new (uri, base_uri);
  GST_INFO_OBJECT (demux, "Changed location: %s (base uri: %s)", uri,
      GST_STR_NULL (base_uri));
  return TRUE;
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

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean update,
    GError ** err)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  GstFragment *download;
  GstBuffer *buf;
  gchar *playlist;
  gboolean main_checked = FALSE, updated = FALSE;
  gchar *uri, *main_uri;

retry:
  uri = gst_m3u8_client_get_current_uri (demux->client);
  main_uri = gst_m3u8_client_get_uri (demux->client);
  download =
      gst_uri_downloader_fetch_uri (adaptive_demux->downloader, uri, main_uri,
      TRUE, TRUE, TRUE, err);
  g_free (main_uri);
  if (download == NULL) {
    if (update && !main_checked
        && gst_m3u8_client_has_variant_playlist (demux->client)
        && gst_m3u8_client_has_main (demux->client)) {
      GError *err2 = NULL;
      main_uri = gst_m3u8_client_get_uri (demux->client);
      GST_INFO_OBJECT (demux,
          "Updating playlist %s failed, attempt to refresh variant playlist %s",
          uri, main_uri);
      download =
          gst_uri_downloader_fetch_uri (adaptive_demux->downloader,
          main_uri, NULL, TRUE, TRUE, TRUE, &err2);
      g_free (main_uri);
      g_clear_error (&err2);
      if (download != NULL) {
        gchar *base_uri;

        buf = gst_fragment_get_buffer (download);
        playlist = gst_hls_src_buf_to_utf8_playlist (buf);
        gst_buffer_unref (buf);

        if (playlist == NULL) {
          GST_WARNING_OBJECT (demux,
              "Failed to validate variant playlist encoding");
          g_free (uri);
          g_object_unref (download);
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

        if (!gst_m3u8_client_update_variant_playlist (demux->client, playlist,
                uri, base_uri)) {
          GST_WARNING_OBJECT (demux, "Failed to update the variant playlist");
          g_object_unref (download);
          return FALSE;
        }

        g_object_unref (download);

        g_clear_error (err);
        main_checked = TRUE;
        goto retry;
      } else {
        g_free (uri);
        return FALSE;
      }
    } else {
      g_free (uri);
      return FALSE;
    }
  }
  g_free (uri);

  /* Set the base URI of the playlist to the redirect target if any */
  GST_M3U8_CLIENT_LOCK (demux->client);
  g_free (demux->client->current->uri);
  g_free (demux->client->current->base_uri);
  if (download->redirect_permanent && download->redirect_uri) {
    demux->client->current->uri = g_strdup (download->redirect_uri);
    demux->client->current->base_uri = NULL;
  } else {
    demux->client->current->uri = g_strdup (download->uri);
    demux->client->current->base_uri = g_strdup (download->redirect_uri);
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

  updated = gst_m3u8_client_update (demux->client, playlist);
  if (!updated) {
    GST_WARNING_OBJECT (demux, "Couldn't update playlist");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Couldn't update playlist");
    return FALSE;
  }

  /* If it's a live source, do not let the sequence number go beyond
   * three fragments before the end of the list */
  if (update == FALSE && demux->client->current &&
      gst_m3u8_client_is_live (demux->client)) {
    gint64 last_sequence;

    GST_M3U8_CLIENT_LOCK (demux->client);
    last_sequence =
        GST_M3U8_MEDIA_FILE (g_list_last (demux->client->current->
            files)->data)->sequence;

    if (demux->client->sequence >= last_sequence - 3) {
      GST_DEBUG_OBJECT (demux, "Sequence is beyond playlist. Moving back to %u",
          (guint) (last_sequence - 3));
      //demux->need_segment = TRUE;
      demux->client->sequence = last_sequence - 3;
    }
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  } else if (demux->client->current && !gst_m3u8_client_is_live (demux->client)) {
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
    if (GST_CLOCK_TIME_IS_VALID (demux->client->sequence_position)) {
      target_pos = MAX (target_pos, demux->client->sequence_position);
    }

    current_pos = 0;
    for (walk = demux->client->current->files; walk; walk = walk->next) {
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
    demux->client->sequence = sequence;
    demux->client->sequence_position = current_pos;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  }

  return updated;
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

  previous_variant = demux->client->main->current_variant;
  current_variant = gst_m3u8_client_get_playlist_for_bitrate (demux->client,
      max_bitrate);

  GST_M3U8_CLIENT_LOCK (demux->client);

retry_failover_protection:
  old_bandwidth = GST_M3U8 (previous_variant->data)->bandwidth;
  new_bandwidth = GST_M3U8 (current_variant->data)->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }

  demux->client->main->current_variant = current_variant;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_m3u8_client_set_current (demux->client, current_variant->data);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);
  stream->discont = TRUE;
  demux->new_playlist = TRUE;

  if (gst_hls_demux_update_playlist (demux, FALSE, NULL)) {
    gchar *uri;
    gchar *main_uri;
    uri = gst_m3u8_client_get_current_uri (demux->client);
    main_uri = gst_m3u8_client_get_uri (demux->client);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux),
            gst_structure_new (STATISTICS_MESSAGE_NAME,
                "manifest-uri", G_TYPE_STRING,
                main_uri, "uri", G_TYPE_STRING,
                uri, "bitrate", G_TYPE_INT, new_bandwidth, NULL)));
    g_free (uri);
    g_free (main_uri);
    if (changed)
      *changed = TRUE;
  } else {
    GList *failover = NULL;

    GST_INFO_OBJECT (demux, "Unable to update playlist. Switching back");
    GST_M3U8_CLIENT_LOCK (demux->client);

    failover = g_list_previous (current_variant);
    if (failover && new_bandwidth == GST_M3U8 (failover->data)->bandwidth) {
      current_variant = failover;
      goto retry_failover_protection;
    }

    demux->client->main->current_variant = previous_variant;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_m3u8_client_set_current (demux->client, previous_variant->data);
    /*  Try a lower bitrate (or stop if we just tried the lowest) */
    if (GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->client->main->iframe_lists)->data)->
        bandwidth)
      return FALSE;
    else if (!GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->client->main->lists)->data)->bandwidth)
      return FALSE;
    else
      return gst_hls_demux_change_playlist (demux, new_bandwidth - 1, changed);
  }

  /* Force typefinding since we might have changed media type */
  demux->do_typefind = TRUE;

  return TRUE;
}

#if defined(HAVE_OPENSSL)
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data)
{
  EVP_CIPHER_CTX_init (&demux->aes_ctx);
  if (!EVP_DecryptInit_ex (&demux->aes_ctx, EVP_aes_128_cbc (), NULL, key_data,
          iv_data))
    return FALSE;
  EVP_CIPHER_CTX_set_padding (&demux->aes_ctx, 0);
  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  int len, flen = 0;

  if (G_UNLIKELY (length > G_MAXINT || length % 16 != 0))
    return FALSE;

  len = (int) length;
  if (!EVP_DecryptUpdate (&demux->aes_ctx, decrypted_data, &len, encrypted_data,
          len))
    return FALSE;
  EVP_DecryptFinal_ex (&demux->aes_ctx, decrypted_data + len, &flen);
  g_return_val_if_fail (len + flen == length, FALSE);
  return TRUE;
}

static void
gst_hls_demux_decrypt_end (GstHLSDemux * demux)
{
  EVP_CIPHER_CTX_cleanup (&demux->aes_ctx);
}

#elif defined(HAVE_NETTLE)
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data)
{
  aes_set_decrypt_key (&demux->aes_ctx.ctx, 16, key_data);
  CBC_SET_IV (&demux->aes_ctx, iv_data);

  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  if (length % 16 != 0)
    return FALSE;

  CBC_DECRYPT (&demux->aes_ctx, aes_decrypt, length, decrypted_data,
      encrypted_data);

  return TRUE;
}

static void
gst_hls_demux_decrypt_end (GstHLSDemux * demux)
{
  /* NOP */
}

#else
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data)
{
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&demux->aes_ctx, GCRY_CIPHER_AES128,
      GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (demux->aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (demux->aes_ctx, iv_data, 16);
  if (!err)
    ret = TRUE;

out:
  if (!ret)
    if (demux->aes_ctx)
      gcry_cipher_close (demux->aes_ctx);

  return ret;
}

static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  gcry_error_t err = 0;

  err = gcry_cipher_decrypt (demux->aes_ctx, decrypted_data, length,
      encrypted_data, length);

  return err == 0;
}

static void
gst_hls_demux_decrypt_end (GstHLSDemux * demux)
{
  if (demux->aes_ctx) {
    gcry_cipher_close (demux->aes_ctx);
    demux->aes_ctx = NULL;
  }
}
#endif

static GstBuffer *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstBuffer * encrypted_buffer, GError ** err)
{
  GstBuffer *decrypted_buffer = NULL;
  GstMapInfo encrypted_info, decrypted_info;

  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (!decrypt_fragment (demux, encrypted_info.size,
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

  return gst_util_uint64_scale (gst_m3u8_client_get_target_duration
      (hlsdemux->client), G_USEC_PER_SEC, GST_SECOND);
}

static gboolean
gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  return gst_m3u8_client_get_seek_range (hlsdemux->client, start, stop);
}
