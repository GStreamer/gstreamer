/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-decklink2src
 * @short_description: Captures Video and Audio from a BlackMagic DeckLink Device
 * @see_also: decklink2demux, decklink2srcbin
 *
 * Captures Video and Audio from a BlackMagic DeckLink Device. Audio is attached
 * to video buffers as metadata and can be extracted using decklink2demux.
 * decklink2srcbin can be used to capture video and audio as separate streams.
 *
 * ## Sample pipeline
 * |[
 * gst-launch-1.0 decklink2src ! decklink2demux name=demux \
 *   demux.video ! queue ! autovideosink \
 *   demux.audio ! queue ! autoaudiosink
 * ]|
 * Captures video and audio from a DeckLink device.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklink2src.h"
#include "gstdecklink2utils.h"
#include "gstdecklink2device.h"
#include "gstdecklink2srcprops.h"

#include <mutex>
#include <new>
#include <string.h>
#include <atomic>

GST_DEBUG_CATEGORY_STATIC (gst_decklink2_src_debug);
#define GST_CAT_DEFAULT gst_decklink2_src_debug

#define DEFAULT_MODE                bmdModeUnknown
#define DEFAULT_DEVICE_NUMBER       0
#define DEFAULT_PERSISTENT_ID       -1
#define DEFAULT_VIDEO_CONNECTION    bmdVideoConnectionUnspecified
#define DEFAULT_AUDIO_CONNECTION    bmdAudioConnectionUnspecified
#define DEFAULT_VIDEO_FORMAT        bmdFormat8BitYUV
#define DEFAULT_PROFILE_ID          bmdProfileDefault
#define DEFAULT_TIMECODE_FORMAT     bmdTimecodeRP188Any
#define DEFAULT_OUTPUT_CC           FALSE
#define DEFAULT_OUTPUT_AFD_BAR      FALSE
#define DEFAULT_OUTPUT_VANC         FALSE
#define DEFAULT_MAX_BUFFERED_FRAMES 5
#define DEFAULT_AUDIO_CHANNELS      GST_DECKLINK2_AUDIO_CHANNELS_2
#define DEFAULT_SKIP_FIRST_TIME     0
#define DEFAULT_DESYNC_THRESHOLD    0
#define DEFAULT_DROP_NO_SIGNAL_FRAMES FALSE

enum
{
  /* actions */
  SIGNAL_RESTART,

  SIGNAL_LAST,
};

static guint gst_decklink2_src_signals[SIGNAL_LAST] = { 0, };

/* *INDENT-OFF* */
struct GstDeckLink2SrcPrivate
{
  std::mutex lock;

  GstVideoInfo video_info = { };

  GstDeckLink2Input *input = nullptr;
  GstCaps *selected_caps = nullptr;
  gboolean is_gap_buf = FALSE;

  GstDeckLink2InputStats stats = { };
  gboolean input_started = FALSE;
  std::atomic<gboolean> need_restart = { FALSE };

  /* properties */
  BMDDisplayMode display_mode = DEFAULT_MODE;
  gint device_number = DEFAULT_DEVICE_NUMBER;
  gint64 persistent_id = DEFAULT_PERSISTENT_ID;
  BMDVideoConnection video_conn = DEFAULT_VIDEO_CONNECTION;
  BMDAudioConnection audio_conn = DEFAULT_AUDIO_CONNECTION;
  BMDPixelFormat video_format = DEFAULT_VIDEO_FORMAT;
  GstDeckLink2AudioChannels audio_channels = DEFAULT_AUDIO_CHANNELS;
  BMDProfileID profile_id = DEFAULT_PROFILE_ID;
  BMDTimecodeFormat timecode_format = DEFAULT_TIMECODE_FORMAT;
  gboolean output_cc = DEFAULT_OUTPUT_CC;
  gboolean output_afd_bar = DEFAULT_OUTPUT_AFD_BAR;
  gboolean output_vanc = DEFAULT_OUTPUT_VANC;
  std::atomic<gboolean> drop_no_signal_frames =
      { DEFAULT_DROP_NO_SIGNAL_FRAMES };
  guint max_buffered_frames = DEFAULT_MAX_BUFFERED_FRAMES;
  GstClockTime skip_first_time = DEFAULT_SKIP_FIRST_TIME;
  GstClockTime desync_threshold = DEFAULT_DESYNC_THRESHOLD;
};
/* *INDENT-ON* */

struct _GstDeckLink2Src
{
  GstPushSrc parent;

  GstDeckLink2SrcPrivate *priv;
};

static void gst_decklink2_src_finalize (GObject * object);
static void gst_decklink2_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink2_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstCaps *gst_decklink2_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_decklink2_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_decklink2_src_start (GstBaseSrc * src);
static gboolean gst_decklink2_src_stop (GstBaseSrc * src);
static gboolean gst_decklink2_src_unlock (GstBaseSrc * src);
static gboolean gst_decklink2_src_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_decklink2_src_create (GstPushSrc * src,
    GstBuffer ** buffer);

#define gst_decklink2_src_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstDeckLink2Src, gst_decklink2_src,
    GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (decklink2src, "decklink2src",
    GST_RANK_NONE, GST_TYPE_DECKLINK2_SRC);

static void
gst_decklink2_src_class_init (GstDeckLink2SrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstCaps *templ_caps;

  object_class->finalize = gst_decklink2_src_finalize;
  object_class->set_property = gst_decklink2_src_set_property;
  object_class->get_property = gst_decklink2_src_get_property;

  gst_decklink2_src_install_properties (object_class);

  /**
   * GstDeckLink2Src::restart:
   * @decklink2src: the decklink2src element to emit this signal on
   *
   * Signal action used to do soft restart the DeckLink device.
   * Can be useful when the device misbehaves and user wants to do soft
   * restart without stopping the pipeline
   */
  gst_decklink2_src_signals[SIGNAL_RESTART] =
      g_signal_new_class_handler ("restart", G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_decklink2_src_restart), NULL, NULL, NULL, G_TYPE_NONE, 0);

  templ_caps = gst_decklink2_get_default_template_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref (templ_caps);

  gst_element_class_set_static_metadata (element_class,
      "Decklink2 Source", "Video/Audio/Source/Hardware", "Decklink2 Source",
      "Seungha Yang <seungha@centricular.com>");

  basesrc_class->negotiate = nullptr;
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_decklink2_src_get_caps);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_decklink2_src_query);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_decklink2_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_decklink2_src_stop);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_decklink2_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_decklink2_src_unlock_stop);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_decklink2_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_decklink2_src_debug, "decklink2src",
      0, "decklink2src");

  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_MODE, (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_VIDEO_CONNECTION,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_AUDIO_CONNECTION,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_VIDEO_FORMAT,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_AUDIO_CHANNELS,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_PROFILE_ID,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DECKLINK2_TIMECODE_FORMAT,
      (GstPluginAPIFlags) 0);
}

static void
gst_decklink2_src_init (GstDeckLink2Src * self)
{
  auto priv = (GstDeckLink2SrcPrivate *)
      gst_decklink2_src_get_instance_private (self);
  self->priv = new (priv) GstDeckLink2SrcPrivate ();
  gst_decklink2_input_stats_init (&priv->stats);

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_pad_use_fixed_caps (GST_BASE_SRC_PAD (self));
}

static void
gst_decklink2_src_finalize (GObject * object)
{
  auto self = GST_DECKLINK2_SRC (object);

  self->priv->~GstDeckLink2SrcPrivate ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_decklink2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_DECKLINK2_SRC (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_MODE:
      priv->display_mode = (BMDDisplayMode) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      priv->device_number = g_value_get_int (value);
      break;
    case PROP_PERSISTENT_ID:
      priv->persistent_id = g_value_get_int64 (value);
      break;
    case PROP_VIDEO_CONNECTION:
      priv->video_conn = (BMDVideoConnection) g_value_get_enum (value);
      break;
    case PROP_AUDIO_CONNECTION:
      priv->audio_conn = (BMDAudioConnection) g_value_get_enum (value);
      break;
    case PROP_VIDEO_FORMAT:
      priv->video_format = (BMDPixelFormat) g_value_get_enum (value);
      break;
    case PROP_AUDIO_CHANNELS:
      priv->audio_channels =
          (GstDeckLink2AudioChannels) g_value_get_enum (value);
      break;
    case PROP_PROFILE_ID:
      priv->profile_id = (BMDProfileID) g_value_get_enum (value);
      break;
    case PROP_TIMECODE_FORMAT:
      priv->timecode_format = (BMDTimecodeFormat) g_value_get_enum (value);
      break;
    case PROP_OUTPUT_CC:
      priv->output_cc = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_AFD_BAR:
      priv->output_afd_bar = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_VANC:
      priv->output_vanc = g_value_get_boolean (value);
      break;
    case PROP_MAX_BUFFERED_FRAMES:
      priv->max_buffered_frames = g_value_get_uint (value);
      break;
    case PROP_SKIP_FIRST_TIME:
      priv->skip_first_time = g_value_get_uint64 (value);
      break;
    case PROP_DESYNC_THRESHOLD:
      priv->desync_threshold = g_value_get_uint64 (value);
      break;
    case PROP_DROP_NO_SIGNAL_FRAMES:
      priv->drop_no_signal_frames = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decklink2_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  auto self = GST_DECKLINK2_SRC (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, priv->display_mode);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, priv->device_number);
      break;
    case PROP_PERSISTENT_ID:
      g_value_set_int64 (value, priv->persistent_id);
      break;
    case PROP_VIDEO_CONNECTION:
      g_value_set_enum (value, priv->video_conn);
      break;
    case PROP_AUDIO_CONNECTION:
      g_value_set_enum (value, priv->audio_conn);
      break;
    case PROP_VIDEO_FORMAT:
      g_value_set_enum (value, priv->video_format);
      break;
    case PROP_AUDIO_CHANNELS:
      g_value_set_enum (value, priv->audio_channels);
      break;
    case PROP_PROFILE_ID:
      g_value_set_enum (value, priv->profile_id);
      break;
    case PROP_TIMECODE_FORMAT:
      g_value_set_enum (value, priv->timecode_format);
      break;
    case PROP_OUTPUT_CC:
      g_value_set_boolean (value, priv->output_cc);
      break;
    case PROP_OUTPUT_AFD_BAR:
      g_value_set_boolean (value, priv->output_afd_bar);
      break;
    case PROP_OUTPUT_VANC:
      g_value_set_boolean (value, priv->output_vanc);
      break;
    case PROP_MAX_BUFFERED_FRAMES:
      g_value_set_uint (value, priv->max_buffered_frames);
      break;
    case PROP_SIGNAL:
    {
      gboolean has_signal = FALSE;
      if (priv->input)
        has_signal = gst_decklink2_input_has_signal (priv->input);

      g_value_set_boolean (value, has_signal);
      break;
    }
    case PROP_DROP_NO_SIGNAL_FRAMES:
      g_value_set_boolean (value, priv->drop_no_signal_frames);
      break;
    case PROP_HW_SERIAL_NUMBER:
    {
      if (priv->input) {
        auto device = (GstDeckLink2Device *)
            gst_object_get_parent (GST_OBJECT (priv->input));
        g_value_set_string (value,
            gst_decklink2_device_get_serial_number (device));
        gst_object_unref (device);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    }
    case PROP_SKIP_FIRST_TIME:
      g_value_set_uint64 (value, priv->skip_first_time);
      break;
    case PROP_DESYNC_THRESHOLD:
      g_value_set_uint64 (value, priv->desync_threshold);
      break;
    case PROP_INPUT_STATS:
    {
      const auto & stats = priv->stats;
      const auto & c = stats.calibration;
      auto calibration = gst_structure_new ("calibration-stats",
          "have-mapping", G_TYPE_BOOLEAN, c.have_mapping,
          "window-filled", G_TYPE_BOOLEAN, c.window_filled,
          "generation", G_TYPE_UINT64, c.generation,
          "slope", G_TYPE_DOUBLE, c.slope,
          "xbase", G_TYPE_UINT64, c.xbase,
          "b", G_TYPE_UINT64, c.b,
          "num", G_TYPE_UINT64, c.num,
          "den", G_TYPE_UINT64, c.den,
          "r-squared", G_TYPE_DOUBLE, c.r_squared,
          "diff", G_TYPE_INT64, c.diff, NULL);
      auto s = gst_structure_new ("input-stats",
          "stream-time", G_TYPE_UINT64, stats.stream_time,
          "hardware-time", G_TYPE_UINT64, stats.hardware_time,
          "hardware-reference-time", G_TYPE_UINT64,
          stats.hardware_reference_time,
          "capture-time", G_TYPE_UINT64, stats.capture_time,
          "calibrated-time", G_TYPE_UINT64, stats.calibrated_time,
          "audio-packet-time", G_TYPE_UINT64, stats.audio_packet_time,
          "audio-stream-time", G_TYPE_UINT64, stats.audio_stream_time,
          "calibrated-audio-time", G_TYPE_UINT64, stats.calibrated_audio_time,
          "av-sync", G_TYPE_INT64, stats.av_sync,
          "calibration-stats", GST_TYPE_STRUCTURE, calibration, NULL);
      gst_structure_free (calibration);
      g_value_take_boxed (value, s);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_decklink2_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;
  GstCaps *caps;
  GstCaps *ret;
  std::unique_lock < std::mutex > lk (priv->lock);

  if (!priv->input) {
    lk.unlock ();
    return GST_BASE_SRC_CLASS (parent_class)->get_caps (src, filter);
  }

  if (priv->selected_caps) {
    caps = gst_caps_ref (priv->selected_caps);
  } else {
    caps = gst_decklink2_input_get_caps (priv->input, priv->display_mode,
        priv->video_format);
  }

  if (!caps) {
    GST_WARNING_OBJECT (self, "Couldn't get caps");
    caps = gst_caps_new_empty ();
  }

  if (filter) {
    ret = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
  } else {
    ret = caps;
  }

  GST_DEBUG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_decklink2_src_query (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      std::lock_guard < std::mutex > lk (priv->lock);
      gint fps_n, fps_d;
      GstClockTime min, max;
      if (priv->video_info.fps_n > 0 && priv->video_info.fps_d > 0) {
        fps_n = priv->video_info.fps_n;
        fps_d = priv->video_info.fps_d;
      } else {
        fps_n = 30;
        fps_d = 1;
      }

      min = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
      max = priv->max_buffered_frames * min;
      gst_query_set_latency (query, TRUE, min, max);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (src, query);
}

static gboolean
gst_decklink2_src_start (GstBaseSrc * src)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  gst_decklink2_input_stats_init (&priv->stats);
  priv->input_started = FALSE;
  priv->need_restart = FALSE;
  gst_video_info_init (&priv->video_info);

  gst_clear_caps (&priv->selected_caps);
  priv->input = gst_decklink2_acquire_input (priv->device_number,
      priv->persistent_id);

  if (!priv->input) {
    GST_ERROR_OBJECT (self, "Couldn't acquire input object");
    return FALSE;
  }

  lk.unlock ();
  g_object_notify (G_OBJECT (self), "hw-serial-number");

  return TRUE;
}

static void
gst_decklink2_src_stop_input (GstDeckLink2Src * self)
{
  auto priv = self->priv;

  if (priv->input && priv->input_started)
    gst_decklink2_input_stop (priv->input);

  gst_clear_caps (&priv->selected_caps);
  gst_video_info_init (&priv->video_info);
  gst_decklink2_input_stats_init (&priv->stats);
  priv->input_started = FALSE;
  priv->is_gap_buf = FALSE;
}

static gboolean
gst_decklink2_src_stop (GstBaseSrc * src)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  gst_decklink2_src_stop_input (self);
  priv->need_restart = FALSE;

  if (priv->input) {
    gst_decklink2_release_input (priv->input);
    priv->input = NULL;
  }

  return TRUE;
}

static gboolean
gst_decklink2_src_unlock (GstBaseSrc * src)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->input)
    gst_decklink2_input_set_flushing (priv->input, TRUE);

  return TRUE;
}

static gboolean
gst_decklink2_src_unlock_stop (GstBaseSrc * src)
{
  auto self = GST_DECKLINK2_SRC (src);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->input)
    gst_decklink2_input_set_flushing (priv->input, FALSE);

  return TRUE;
}

static gboolean
gst_decklink2_src_ensure_started (GstDeckLink2Src * self)
{
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->input_started)
    return TRUE;

  if (!priv->input) {
    GST_ERROR_OBJECT (self, "Input object was not configured");
    return FALSE;
  }

  GstDeckLink2InputVideoConfig video_config;
  GstDeckLink2InputAudioConfig audio_config;

  video_config.connection = priv->video_conn;
  video_config.display_mode = priv->display_mode;
  video_config.pixel_format = priv->video_format;
  video_config.timecode_format = priv->timecode_format;
  video_config.auto_detect = priv->display_mode == bmdModeUnknown;
  video_config.output_cc = priv->output_cc;
  video_config.output_afd_bar = priv->output_afd_bar;
  video_config.output_vanc = priv->output_vanc;

  audio_config.connection = priv->audio_conn;
  audio_config.sample_type = bmdAudioSampleType32bitInteger;
  audio_config.channels = priv->audio_channels;

  auto hr = gst_decklink2_input_start (priv->input, GST_ELEMENT (self),
      priv->profile_id, priv->max_buffered_frames,
      priv->skip_first_time, &video_config, &audio_config);
  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't start stream, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  priv->input_started = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_decklink2_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  auto self = GST_DECKLINK2_SRC (src);
  GstBuffer *buf = nullptr;
  GstCaps *caps = nullptr;
  GstFlowReturn ret;
  auto priv = self->priv;
  gboolean is_gap_buf = FALSE;
  GstDeckLink2InputStats stats;

  if (priv->need_restart.exchange (FALSE)) {
    std::lock_guard < std::mutex > lk (priv->lock);
    GST_INFO_OBJECT (self, "Restarting as requested");
    gst_decklink2_src_stop_input (self);
  }

  if (!gst_decklink2_src_ensure_started (self)) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to start stream"));
    return GST_FLOW_ERROR;
  }

retry:
  ret = gst_decklink2_input_get_data (priv->input, &buf, &caps, &stats);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!buf) {
    if (!priv->is_gap_buf) {
      priv->is_gap_buf = TRUE;
      g_object_notify (G_OBJECT (self), "signal");
    }
    goto retry;
  }

  if (!caps) {
    GST_ERROR_OBJECT (self, "Buffer without caps");
    gst_clear_buffer (&buf);
    return GST_FLOW_ERROR;
  }

  priv->lock.lock ();
  priv->stats = stats;
  if (!priv->selected_caps || !gst_caps_is_equal (caps, priv->selected_caps)) {
    GST_DEBUG_OBJECT (self, "Set updated caps %" GST_PTR_FORMAT, caps);
    gst_caps_replace (&priv->selected_caps, caps);
    gst_video_info_from_caps (&priv->video_info, caps);
    priv->lock.unlock ();
    if (!gst_base_src_set_caps (GST_BASE_SRC (self), caps)) {
      GST_ERROR_OBJECT (self, "Couldn't set caps");
      gst_clear_buffer (&buf);
      gst_clear_caps (&caps);

      return GST_FLOW_NOT_NEGOTIATED;
    }
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_latency (GST_OBJECT (self)));
  } else {
    priv->lock.unlock ();
  }
  gst_clear_caps (&caps);

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP))
    is_gap_buf = TRUE;

  if (is_gap_buf != priv->is_gap_buf) {
    priv->is_gap_buf = is_gap_buf;
    g_object_notify (G_OBJECT (self), "signal");
  }

  if (is_gap_buf && priv->drop_no_signal_frames) {
    GST_LOG_OBJECT (self,
        "Dropping no-signal-buffer as requested %" GST_PTR_FORMAT, buf);
    gst_clear_buffer (&buf);
    goto retry;
  }

  std::unique_lock < std::mutex > lk (priv->lock);
  *buffer = buf;

  if (priv->desync_threshold != 0 &&
      GST_CLOCK_TIME_IS_VALID (priv->desync_threshold)) {
    GstClockTime diff;
    if (stats.av_sync >= 0)
      diff = stats.av_sync;
    else
      diff = -stats.av_sync;

    GST_LOG_OBJECT (self, "Current AV sync %" GST_STIME_FORMAT,
        GST_STIME_ARGS (stats.av_sync));

    if (diff >= priv->desync_threshold) {
      GST_WARNING_OBJECT (self, "Large AV desync is detected, desync %"
          GST_STIME_FORMAT ", threshold %" GST_TIME_FORMAT,
          GST_STIME_ARGS (stats.av_sync),
          GST_TIME_ARGS (priv->desync_threshold));

      priv->need_restart = TRUE;
    }
  }

  return GST_FLOW_OK;
}

void
gst_decklink2_src_install_properties (GObjectClass * object_class)
{
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("mode", "Playback Mode",
          "Video Mode to use for playback",
          GST_TYPE_DECKLINK2_MODE, DEFAULT_MODE, param_flags));

  g_object_class_install_property (object_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Output device instance to use", 0, G_MAXINT, DEFAULT_DEVICE_NUMBER,
          param_flags));

  g_object_class_install_property (object_class, PROP_PERSISTENT_ID,
      g_param_spec_int64 ("persistent-id", "Persistent id",
          "Output device instance to use. Higher priority than \"device-number\".",
          DEFAULT_PERSISTENT_ID, G_MAXINT64, DEFAULT_PERSISTENT_ID,
          param_flags));

  g_object_class_install_property (object_class, PROP_VIDEO_CONNECTION,
      g_param_spec_enum ("video-connection", "Video Connection",
          "Video input connection to use",
          GST_TYPE_DECKLINK2_VIDEO_CONNECTION, DEFAULT_VIDEO_CONNECTION,
          param_flags));

  g_object_class_install_property (object_class, PROP_AUDIO_CONNECTION,
      g_param_spec_enum ("audio-connection", "Audio Connection",
          "Audio input connection to use",
          GST_TYPE_DECKLINK2_AUDIO_CONNECTION, DEFAULT_AUDIO_CONNECTION,
          param_flags));

  g_object_class_install_property (object_class, PROP_VIDEO_FORMAT,
      g_param_spec_enum ("video-format", "Video format",
          "Video format type to use for playback",
          GST_TYPE_DECKLINK2_VIDEO_FORMAT, DEFAULT_VIDEO_FORMAT, param_flags));

  g_object_class_install_property (object_class, PROP_AUDIO_CHANNELS,
      g_param_spec_enum ("audio-channels", "Audio Channels",
          "Audio Channels",
          GST_TYPE_DECKLINK2_AUDIO_CHANNELS, DEFAULT_AUDIO_CHANNELS,
          param_flags));

  g_object_class_install_property (object_class, PROP_PROFILE_ID,
      g_param_spec_enum ("profile", "Profile",
          "Certain DeckLink devices such as the DeckLink 8K Pro, the DeckLink "
          "Quad 2 and the DeckLink Duo 2 support multiple profiles to "
          "configure the capture and playback behavior of its sub-devices."
          "For the DeckLink Duo 2 and DeckLink Quad 2, a profile is shared "
          "between any 2 sub-devices that utilize the same connectors. For the "
          "DeckLink 8K Pro, a profile is shared between all 4 sub-devices. Any "
          "sub-devices that share a profile are considered to be part of the "
          "same profile group."
          "DeckLink Duo 2 support configuration of the duplex mode of "
          "individual sub-devices.",
          GST_TYPE_DECKLINK2_PROFILE_ID, DEFAULT_PROFILE_ID, param_flags));

  g_object_class_install_property (object_class, PROP_TIMECODE_FORMAT,
      g_param_spec_enum ("timecode-format", "Timecode format",
          "Timecode format type to use for playback",
          GST_TYPE_DECKLINK2_TIMECODE_FORMAT, DEFAULT_TIMECODE_FORMAT,
          param_flags));

  g_object_class_install_property (object_class, PROP_OUTPUT_CC,
      g_param_spec_boolean ("output-cc", "Output Closed Caption",
          "Extract and output CC as GstMeta (if present)",
          DEFAULT_OUTPUT_CC, param_flags));

  g_object_class_install_property (object_class, PROP_OUTPUT_AFD_BAR,
      g_param_spec_boolean ("output-afd-bar", "Output AFD/Bar data",
          "Extract and output AFD/Bar as GstMeta (if present)",
          DEFAULT_OUTPUT_AFD_BAR, param_flags));

  g_object_class_install_property (object_class, PROP_OUTPUT_VANC,
      g_param_spec_boolean ("output-vanc", "Output VANC data",
          "Extract and output VANC as GstAncillaryMeta (if present)",
          DEFAULT_OUTPUT_VANC, param_flags));

  g_object_class_install_property (object_class, PROP_MAX_BUFFERED_FRAMES,
      g_param_spec_uint ("max-buffered-frames", "Maximum Buffered Frames",
          "Maximum number of video frames to buffer", 1,
          16, DEFAULT_MAX_BUFFERED_FRAMES, param_flags));

  g_object_class_install_property (object_class, PROP_SIGNAL,
      g_param_spec_boolean ("signal", "Signal",
          "True if there is a valid input signal available",
          FALSE, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_SKIP_FIRST_TIME,
      g_param_spec_uint64 ("skip-first-time", "Skip First Time",
          "Skip that much time of initial frames after starting", 0,
          G_MAXUINT64, DEFAULT_SKIP_FIRST_TIME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_DESYNC_THRESHOLD,
      g_param_spec_uint64 ("desync-threshold", "Desync Threshold",
          "Maximum allowed a/v desync threshold. "
          "If larger desync is detected, streaming will be restarted "
          "(0 = disable auto-restart)", 0,
          G_MAXUINT64, DEFAULT_DESYNC_THRESHOLD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_DROP_NO_SIGNAL_FRAMES,
      g_param_spec_boolean ("drop-no-signal-frames", "Drop No Signal Frames",
          "Drop frames that are marked as having no input signal",
          DEFAULT_DROP_NO_SIGNAL_FRAMES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_HW_SERIAL_NUMBER,
      g_param_spec_string ("hw-serial-number", "Hardware serial number",
          "The serial number (hardware ID) of the Decklink card",
          NULL, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_INPUT_STATS,
      g_param_spec_boxed ("input-stats", "Input Statistics",
          "Input timestamp and calibration statistics", GST_TYPE_STRUCTURE,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
}

void
gst_decklink2_src_restart (GstDeckLink2Src * self)
{
  g_return_if_fail (GST_IS_DECKLINK2_SRC (self));

  GST_INFO_OBJECT (self, "Scheduling restart");
  self->priv->need_restart = TRUE;
}
