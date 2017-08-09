/*
 * Copyright (C) 2010, 2013 Ole André Vadla Ravnås <oleavr@soundrop.com>
 * Copyright (C) 2013 Intel Corporation
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vtenc.h"

#include "coremediabuffer.h"
#include "corevideobuffer.h"
#include "vtutil.h"
#include <gst/pbutils/codec-utils.h>

#define VTENC_DEFAULT_USAGE       6     /* Profile: Baseline  Level: 2.1 */
#define VTENC_DEFAULT_BITRATE     0
#define VTENC_DEFAULT_FRAME_REORDERING TRUE
#define VTENC_DEFAULT_REALTIME FALSE
#define VTENC_DEFAULT_QUALITY 0.5
#define VTENC_DEFAULT_MAX_KEYFRAME_INTERVAL 0
#define VTENC_DEFAULT_MAX_KEYFRAME_INTERVAL_DURATION 0

GST_DEBUG_CATEGORY (gst_vtenc_debug);
#define GST_CAT_DEFAULT (gst_vtenc_debug)

#define GST_VTENC_CODEC_DETAILS_QDATA \
    g_quark_from_static_string ("vtenc-codec-details")

/* define EnableHardwareAcceleratedVideoEncoder in < 10.9 */
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1090
const CFStringRef
    kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder =
CFSTR ("EnableHardwareAcceleratedVideoEncoder");
const CFStringRef
    kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder =
CFSTR ("RequireHardwareAcceleratedVideoEncoder");
const CFStringRef kVTCompressionPropertyKey_ProfileLevel =
CFSTR ("ProfileLevel");
const CFStringRef kVTProfileLevel_H264_Baseline_AutoLevel =
CFSTR ("H264_Baseline_AutoLevel");
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1080
const CFStringRef kVTCompressionPropertyKey_Quality = CFSTR ("Quality");
#endif

#ifdef HAVE_VIDEOTOOLBOX_10_9_6
extern OSStatus
VTCompressionSessionPrepareToEncodeFrames (VTCompressionSessionRef session)
    __attribute__ ((weak_import));
#endif

enum
{
  PROP_0,
  PROP_USAGE,
  PROP_BITRATE,
  PROP_ALLOW_FRAME_REORDERING,
  PROP_REALTIME,
  PROP_QUALITY,
  PROP_MAX_KEYFRAME_INTERVAL,
  PROP_MAX_KEYFRAME_INTERVAL_DURATION
};

typedef struct _GstVTEncFrame GstVTEncFrame;

struct _GstVTEncFrame
{
  GstBuffer *buf;
  GstVideoFrame videoframe;
};

static GstElementClass *parent_class = NULL;

static void gst_vtenc_get_property (GObject * obj, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vtenc_set_property (GObject * obj, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vtenc_finalize (GObject * obj);

static gboolean gst_vtenc_start (GstVideoEncoder * enc);
static gboolean gst_vtenc_stop (GstVideoEncoder * enc);
static gboolean gst_vtenc_set_format (GstVideoEncoder * enc,
    GstVideoCodecState * input_state);
static GstFlowReturn gst_vtenc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_vtenc_finish (GstVideoEncoder * enc);

static void gst_vtenc_clear_cached_caps_downstream (GstVTEnc * self);

static VTCompressionSessionRef gst_vtenc_create_session (GstVTEnc * self);
static void gst_vtenc_destroy_session (GstVTEnc * self,
    VTCompressionSessionRef * session);
static void gst_vtenc_session_dump_properties (GstVTEnc * self,
    VTCompressionSessionRef session);
static void gst_vtenc_session_configure_expected_framerate (GstVTEnc * self,
    VTCompressionSessionRef session, gdouble framerate);
static void gst_vtenc_session_configure_max_keyframe_interval (GstVTEnc * self,
    VTCompressionSessionRef session, gint interval);
static void gst_vtenc_session_configure_max_keyframe_interval_duration
    (GstVTEnc * self, VTCompressionSessionRef session, gdouble duration);
static void gst_vtenc_session_configure_bitrate (GstVTEnc * self,
    VTCompressionSessionRef session, guint bitrate);
static OSStatus gst_vtenc_session_configure_property_int (GstVTEnc * self,
    VTCompressionSessionRef session, CFStringRef name, gint value);
static OSStatus gst_vtenc_session_configure_property_double (GstVTEnc * self,
    VTCompressionSessionRef session, CFStringRef name, gdouble value);
static void gst_vtenc_session_configure_allow_frame_reordering (GstVTEnc * self,
    VTCompressionSessionRef session, gboolean allow_frame_reordering);
static void gst_vtenc_session_configure_realtime (GstVTEnc * self,
    VTCompressionSessionRef session, gboolean realtime);

static GstFlowReturn gst_vtenc_encode_frame (GstVTEnc * self,
    GstVideoCodecFrame * frame);
static void gst_vtenc_enqueue_buffer (void *outputCallbackRefCon,
    void *sourceFrameRefCon, OSStatus status, VTEncodeInfoFlags infoFlags,
    CMSampleBufferRef sampleBuffer);
static gboolean gst_vtenc_buffer_is_keyframe (GstVTEnc * self,
    CMSampleBufferRef sbuf);


#ifndef HAVE_IOS
static GstVTEncFrame *gst_vtenc_frame_new (GstBuffer * buf,
    GstVideoInfo * videoinfo);
static void gst_vtenc_frame_free (GstVTEncFrame * frame);

static void gst_pixel_buffer_release_cb (void *releaseRefCon,
    const void *dataPtr, size_t dataSize, size_t numberOfPlanes,
    const void *planeAddresses[]);
#endif

#ifdef HAVE_IOS
static GstStaticCaps sink_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, I420 }"));
#else
static GstStaticCaps sink_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ UYVY, NV12, I420 }"));
#endif

static void
gst_vtenc_base_init (GstVTEncClass * klass)
{
  const GstVTEncoderDetails *codec_details =
      GST_VTENC_CLASS_GET_CODEC_DETAILS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  const int min_width = 1, max_width = G_MAXINT;
  const int min_height = 1, max_height = G_MAXINT;
  const int min_fps_n = 0, max_fps_n = G_MAXINT;
  const int min_fps_d = 1, max_fps_d = 1;
  GstPadTemplate *sink_template, *src_template;
  GstCaps *src_caps;
  gchar *longname, *description;

  longname = g_strdup_printf ("%s encoder", codec_details->name);
  description = g_strdup_printf ("%s encoder", codec_details->name);

  gst_element_class_set_metadata (element_class, longname,
      "Codec/Encoder/Video", description,
      "Ole André Vadla Ravnås <oleavr@soundrop.com>, Dominik Röttsches <dominik.rottsches@intel.com>");

  g_free (longname);
  g_free (description);

  sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, gst_static_caps_get (&sink_caps));
  gst_element_class_add_pad_template (element_class, sink_template);

  src_caps = gst_caps_new_simple (codec_details->mimetype,
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height,
      "framerate", GST_TYPE_FRACTION_RANGE,
      min_fps_n, min_fps_d, max_fps_n, max_fps_d, NULL);
  if (codec_details->format_id == kCMVideoCodecType_H264) {
    gst_structure_set (gst_caps_get_structure (src_caps, 0),
        "stream-format", G_TYPE_STRING, "avc",
        "alignment", G_TYPE_STRING, "au", NULL);
  }
  src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      src_caps);
  gst_element_class_add_pad_template (element_class, src_template);
  gst_caps_unref (src_caps);
}

static void
gst_vtenc_class_init (GstVTEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *gstvideoencoder_class;

  gobject_class = (GObjectClass *) klass;
  gstvideoencoder_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_vtenc_get_property;
  gobject_class->set_property = gst_vtenc_set_property;
  gobject_class->finalize = gst_vtenc_finalize;

  gstvideoencoder_class->start = gst_vtenc_start;
  gstvideoencoder_class->stop = gst_vtenc_stop;
  gstvideoencoder_class->set_format = gst_vtenc_set_format;
  gstvideoencoder_class->handle_frame = gst_vtenc_handle_frame;
  gstvideoencoder_class->finish = gst_vtenc_finish;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Target video bitrate in kbps (0 = auto)",
          0, G_MAXUINT, VTENC_DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOW_FRAME_REORDERING,
      g_param_spec_boolean ("allow-frame-reordering", "Allow frame reordering",
          "Whether to allow frame reordering or not",
          VTENC_DEFAULT_FRAME_REORDERING,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REALTIME,
      g_param_spec_boolean ("realtime", "Realtime",
          "Configure the encoder for realtime output",
          VTENC_DEFAULT_REALTIME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_double ("quality", "Quality",
          "The desired compression quality",
          0.0, 1.0, VTENC_DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_KEYFRAME_INTERVAL,
      g_param_spec_int ("max-keyframe-interval", "Max Keyframe Interval",
          "Maximum number of frames between keyframes (0 = auto)",
          0, G_MAXINT, VTENC_DEFAULT_MAX_KEYFRAME_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MAX_KEYFRAME_INTERVAL_DURATION,
      g_param_spec_uint64 ("max-keyframe-interval-duration",
          "Max Keyframe Interval Duration",
          "Maximum number of nanoseconds between keyframes (0 = no limit)", 0,
          G_MAXUINT64, VTENC_DEFAULT_MAX_KEYFRAME_INTERVAL_DURATION,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_vtenc_init (GstVTEnc * self)
{
  GstVTEncClass *klass = (GstVTEncClass *) G_OBJECT_GET_CLASS (self);
  CFStringRef keyframe_props_keys[] = { kVTEncodeFrameOptionKey_ForceKeyFrame };
  CFBooleanRef keyframe_props_values[] = { kCFBooleanTrue };

  self->details = GST_VTENC_CLASS_GET_CODEC_DETAILS (klass);

  /* These could be controlled by properties later */
  self->dump_properties = FALSE;
  self->dump_attributes = FALSE;
  self->latency_frames = -1;
  self->session = NULL;
  self->profile_level = NULL;

  self->keyframe_props =
      CFDictionaryCreate (NULL, (const void **) keyframe_props_keys,
      (const void **) keyframe_props_values, G_N_ELEMENTS (keyframe_props_keys),
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static void
gst_vtenc_finalize (GObject * obj)
{
  GstVTEnc *self = GST_VTENC_CAST (obj);

  CFRelease (self->keyframe_props);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static guint
gst_vtenc_get_bitrate (GstVTEnc * self)
{
  guint result;

  GST_OBJECT_LOCK (self);
  result = self->bitrate;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_bitrate (GstVTEnc * self, guint bitrate)
{
  GST_OBJECT_LOCK (self);

  self->bitrate = bitrate;

  if (self->session != NULL)
    gst_vtenc_session_configure_bitrate (self, self->session, bitrate);

  GST_OBJECT_UNLOCK (self);
}

static gboolean
gst_vtenc_get_allow_frame_reordering (GstVTEnc * self)
{
  gboolean result;

  GST_OBJECT_LOCK (self);
  result = self->allow_frame_reordering;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_allow_frame_reordering (GstVTEnc * self,
    gboolean allow_frame_reordering)
{
  GST_OBJECT_LOCK (self);
  self->allow_frame_reordering = allow_frame_reordering;
  if (self->session != NULL) {
    gst_vtenc_session_configure_allow_frame_reordering (self,
        self->session, allow_frame_reordering);
  }
  GST_OBJECT_UNLOCK (self);
}

static gboolean
gst_vtenc_get_realtime (GstVTEnc * self)
{
  gboolean result;

  GST_OBJECT_LOCK (self);
  result = self->realtime;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_realtime (GstVTEnc * self, gboolean realtime)
{
  GST_OBJECT_LOCK (self);
  self->realtime = realtime;
  if (self->session != NULL)
    gst_vtenc_session_configure_realtime (self, self->session, realtime);
  GST_OBJECT_UNLOCK (self);
}

static gdouble
gst_vtenc_get_quality (GstVTEnc * self)
{
  gdouble result;

  GST_OBJECT_LOCK (self);
  result = self->quality;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_quality (GstVTEnc * self, gdouble quality)
{
  GST_OBJECT_LOCK (self);
  self->quality = quality;
  GST_INFO_OBJECT (self, "setting quality %f", quality);
  if (self->session != NULL) {
    gst_vtenc_session_configure_property_double (self, self->session,
        kVTCompressionPropertyKey_Quality, quality);
  }
  GST_OBJECT_UNLOCK (self);
}

static gint
gst_vtenc_get_max_keyframe_interval (GstVTEnc * self)
{
  gint result;

  GST_OBJECT_LOCK (self);
  result = self->max_keyframe_interval;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_max_keyframe_interval (GstVTEnc * self, gint interval)
{
  GST_OBJECT_LOCK (self);
  self->max_keyframe_interval = interval;
  if (self->session != NULL) {
    gst_vtenc_session_configure_max_keyframe_interval (self, self->session,
        interval);
  }
  GST_OBJECT_UNLOCK (self);
}

static GstClockTime
gst_vtenc_get_max_keyframe_interval_duration (GstVTEnc * self)
{
  GstClockTime result;

  GST_OBJECT_LOCK (self);
  result = self->max_keyframe_interval_duration;
  GST_OBJECT_UNLOCK (self);

  return result;
}

static void
gst_vtenc_set_max_keyframe_interval_duration (GstVTEnc * self,
    GstClockTime interval)
{
  GST_OBJECT_LOCK (self);
  self->max_keyframe_interval_duration = interval;
  if (self->session != NULL) {
    gst_vtenc_session_configure_max_keyframe_interval_duration (self,
        self->session, interval / ((gdouble) GST_SECOND));
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_vtenc_get_property (GObject * obj, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVTEnc *self = GST_VTENC_CAST (obj);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, gst_vtenc_get_bitrate (self) / 1000);
      break;
    case PROP_ALLOW_FRAME_REORDERING:
      g_value_set_boolean (value, gst_vtenc_get_allow_frame_reordering (self));
      break;
    case PROP_REALTIME:
      g_value_set_boolean (value, gst_vtenc_get_realtime (self));
      break;
    case PROP_QUALITY:
      g_value_set_double (value, gst_vtenc_get_quality (self));
      break;
    case PROP_MAX_KEYFRAME_INTERVAL:
      g_value_set_int (value, gst_vtenc_get_max_keyframe_interval (self));
      break;
    case PROP_MAX_KEYFRAME_INTERVAL_DURATION:
      g_value_set_uint64 (value,
          gst_vtenc_get_max_keyframe_interval_duration (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
gst_vtenc_set_property (GObject * obj, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVTEnc *self = GST_VTENC_CAST (obj);

  switch (prop_id) {
    case PROP_BITRATE:
      gst_vtenc_set_bitrate (self, g_value_get_uint (value) * 1000);
      break;
    case PROP_ALLOW_FRAME_REORDERING:
      gst_vtenc_set_allow_frame_reordering (self, g_value_get_boolean (value));
      break;
    case PROP_REALTIME:
      gst_vtenc_set_realtime (self, g_value_get_boolean (value));
      break;
    case PROP_QUALITY:
      gst_vtenc_set_quality (self, g_value_get_double (value));
      break;
    case PROP_MAX_KEYFRAME_INTERVAL:
      gst_vtenc_set_max_keyframe_interval (self, g_value_get_int (value));
      break;
    case PROP_MAX_KEYFRAME_INTERVAL_DURATION:
      gst_vtenc_set_max_keyframe_interval_duration (self,
          g_value_get_uint64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vtenc_start (GstVideoEncoder * enc)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);

  self->cur_outframes = g_async_queue_new ();

  return TRUE;
}

static gboolean
gst_vtenc_stop (GstVideoEncoder * enc)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);

  GST_OBJECT_LOCK (self);
  gst_vtenc_destroy_session (self, &self->session);
  GST_OBJECT_UNLOCK (self);

  if (self->profile_level)
    CFRelease (self->profile_level);
  self->profile_level = NULL;

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  self->negotiated_width = self->negotiated_height = 0;
  self->negotiated_fps_n = self->negotiated_fps_d = 0;

  gst_vtenc_clear_cached_caps_downstream (self);

  g_async_queue_unref (self->cur_outframes);
  self->cur_outframes = NULL;

  return TRUE;
}

static CFStringRef
gst_vtenc_profile_level_key (GstVTEnc * self, const gchar * profile,
    const gchar * level_arg)
{
  char level[64];
  gchar *key = NULL;
  CFStringRef ret = NULL;

  if (profile == NULL)
    profile = "main";
  if (level_arg == NULL)
    level_arg = "AutoLevel";
  strncpy (level, level_arg, sizeof (level));

  if (!strcmp (profile, "constrained-baseline") ||
      !strcmp (profile, "baseline")) {
    profile = "Baseline";
  } else if (g_str_has_prefix (profile, "high")) {
    profile = "High";
  } else if (!strcmp (profile, "main")) {
    profile = "Main";
  } else {
    g_assert_not_reached ();
  }

  if (strlen (level) == 1) {
    level[1] = '_';
    level[2] = '0';
  } else if (strlen (level) == 3) {
    level[1] = '_';
  }

  key = g_strdup_printf ("H264_%s_%s", profile, level);
  ret = CFStringCreateWithBytes (NULL, (const guint8 *) key, strlen (key),
      kCFStringEncodingASCII, 0);

  GST_INFO_OBJECT (self, "negotiated profile and level %s", key);

  g_free (key);

  return ret;
}

static gboolean
gst_vtenc_negotiate_profile_and_level (GstVideoEncoder * enc)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);
  GstCaps *allowed_caps = NULL;
  gboolean ret = TRUE;
  const gchar *profile = NULL;
  const gchar *level = NULL;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (enc));
  if (allowed_caps) {
    GstStructure *s;

    if (gst_caps_is_empty (allowed_caps)) {
      GST_ERROR_OBJECT (self, "no allowed downstream caps");
      goto fail;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    level = gst_structure_get_string (s, "level");
  }

  if (self->profile_level)
    CFRelease (self->profile_level);
  self->profile_level = gst_vtenc_profile_level_key (self, profile, level);
  if (self->profile_level == NULL) {
    GST_ERROR_OBJECT (enc, "invalid profile and level");
    goto fail;
  }

out:
  if (allowed_caps)
    gst_caps_unref (allowed_caps);

  return ret;

fail:
  ret = FALSE;
  goto out;
}

static gboolean
gst_vtenc_set_format (GstVideoEncoder * enc, GstVideoCodecState * state)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);
  VTCompressionSessionRef session;

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  self->negotiated_width = state->info.width;
  self->negotiated_height = state->info.height;
  self->negotiated_fps_n = state->info.fps_n;
  self->negotiated_fps_d = state->info.fps_d;
  self->video_info = state->info;

  GST_OBJECT_LOCK (self);
  gst_vtenc_destroy_session (self, &self->session);
  GST_OBJECT_UNLOCK (self);

  gst_vtenc_negotiate_profile_and_level (enc);

  session = gst_vtenc_create_session (self);
  GST_OBJECT_LOCK (self);
  self->session = session;
  GST_OBJECT_UNLOCK (self);

  return session != NULL;
}

static gboolean
gst_vtenc_is_negotiated (GstVTEnc * self)
{
  return self->negotiated_width != 0;
}

static gboolean
gst_vtenc_negotiate_downstream (GstVTEnc * self, CMSampleBufferRef sbuf)
{
  gboolean result;
  GstCaps *caps;
  GstStructure *s;
  GstVideoCodecState *state;

  if (self->caps_width == self->negotiated_width &&
      self->caps_height == self->negotiated_height &&
      self->caps_fps_n == self->negotiated_fps_n &&
      self->caps_fps_d == self->negotiated_fps_d) {
    return TRUE;
  }

  caps = gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (self));
  caps = gst_caps_make_writable (caps);
  s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s,
      "width", G_TYPE_INT, self->negotiated_width,
      "height", G_TYPE_INT, self->negotiated_height,
      "framerate", GST_TYPE_FRACTION,
      self->negotiated_fps_n, self->negotiated_fps_d, NULL);

  if (self->details->format_id == kCMVideoCodecType_H264) {
    CMFormatDescriptionRef fmt;
    CFDictionaryRef atoms;
    CFStringRef avccKey;
    CFDataRef avcc;
    guint8 *codec_data;
    gsize codec_data_size;
    GstBuffer *codec_data_buf;
    guint8 sps[3];

    fmt = CMSampleBufferGetFormatDescription (sbuf);
    atoms = CMFormatDescriptionGetExtension (fmt,
        kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
    avccKey = CFStringCreateWithCString (NULL, "avcC", kCFStringEncodingUTF8);
    avcc = CFDictionaryGetValue (atoms, avccKey);
    CFRelease (avccKey);
    codec_data_size = CFDataGetLength (avcc);
    codec_data = g_malloc (codec_data_size);
    CFDataGetBytes (avcc, CFRangeMake (0, codec_data_size), codec_data);
    codec_data_buf = gst_buffer_new_wrapped (codec_data, codec_data_size);

    gst_structure_set (s, "codec_data", GST_TYPE_BUFFER, codec_data_buf, NULL);

    sps[0] = codec_data[1];
    sps[1] = codec_data[2] & ~0xDF;
    sps[2] = codec_data[3];

    gst_codec_utils_h264_caps_set_level_and_profile (caps, sps, 3);

    gst_buffer_unref (codec_data_buf);
  }

  state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER_CAST (self), caps,
      self->input_state);
  gst_video_codec_state_unref (state);
  result = gst_video_encoder_negotiate (GST_VIDEO_ENCODER_CAST (self));

  self->caps_width = self->negotiated_width;
  self->caps_height = self->negotiated_height;
  self->caps_fps_n = self->negotiated_fps_n;
  self->caps_fps_d = self->negotiated_fps_d;

  return result;
}

static void
gst_vtenc_clear_cached_caps_downstream (GstVTEnc * self)
{
  self->caps_width = self->caps_height = 0;
  self->caps_fps_n = self->caps_fps_d = 0;
}

static GstFlowReturn
gst_vtenc_handle_frame (GstVideoEncoder * enc, GstVideoCodecFrame * frame)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);

  if (!gst_vtenc_is_negotiated (self))
    goto not_negotiated;

  return gst_vtenc_encode_frame (self, frame);

not_negotiated:
  gst_video_codec_frame_unref (frame);
  return GST_FLOW_NOT_NEGOTIATED;
}

static GstFlowReturn
gst_vtenc_finish (GstVideoEncoder * enc)
{
  GstVTEnc *self = GST_VTENC_CAST (enc);
  GstVideoCodecFrame *outframe;
  GstFlowReturn ret = GST_FLOW_OK;
  OSStatus vt_status;

  /* We need to unlock the stream lock here because
   * it can wait for gst_vtenc_enqueue_buffer() to
   * handle a buffer... which will take the stream
   * lock from another thread and then deadlock */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  vt_status =
      VTCompressionSessionCompleteFrames (self->session,
      kCMTimePositiveInfinity);
  GST_VIDEO_ENCODER_STREAM_LOCK (self);
  if (vt_status != noErr) {
    GST_WARNING_OBJECT (self, "VTCompressionSessionCompleteFrames returned %d",
        (int) vt_status);
  }

  while ((outframe = g_async_queue_try_pop (self->cur_outframes))) {
    ret =
        gst_video_encoder_finish_frame (GST_VIDEO_ENCODER_CAST (self),
        outframe);
  }

  return ret;
}

static VTCompressionSessionRef
gst_vtenc_create_session (GstVTEnc * self)
{
  VTCompressionSessionRef session = NULL;
  CFMutableDictionaryRef encoder_spec = NULL, pb_attrs;
  OSStatus status;

#if !HAVE_IOS
  const GstVTEncoderDetails *codec_details =
      GST_VTENC_CLASS_GET_CODEC_DETAILS (G_OBJECT_GET_CLASS (self));

  encoder_spec =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_boolean (encoder_spec,
      kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder, true);
  if (codec_details->require_hardware)
    gst_vtutil_dict_set_boolean (encoder_spec,
        kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
        TRUE);
#endif

  pb_attrs = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_i32 (pb_attrs, kCVPixelBufferWidthKey,
      self->negotiated_width);
  gst_vtutil_dict_set_i32 (pb_attrs, kCVPixelBufferHeightKey,
      self->negotiated_height);

  status = VTCompressionSessionCreate (NULL,
      self->negotiated_width, self->negotiated_height,
      self->details->format_id, encoder_spec, pb_attrs, NULL,
      gst_vtenc_enqueue_buffer, self, &session);
  GST_INFO_OBJECT (self, "VTCompressionSessionCreate for %d x %d => %d",
      self->negotiated_width, self->negotiated_height, (int) status);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "VTCompressionSessionCreate() returned: %d",
        (int) status);
    goto beach;
  }

  gst_vtenc_session_configure_expected_framerate (self, session,
      (gdouble) self->negotiated_fps_n / (gdouble) self->negotiated_fps_d);

  status = VTSessionSetProperty (session,
      kVTCompressionPropertyKey_ProfileLevel, self->profile_level);
  GST_DEBUG_OBJECT (self, "kVTCompressionPropertyKey_ProfileLevel => %d",
      (int) status);

  status = VTSessionSetProperty (session,
      kVTCompressionPropertyKey_AllowTemporalCompression, kCFBooleanTrue);
  GST_DEBUG_OBJECT (self,
      "kVTCompressionPropertyKey_AllowTemporalCompression => %d", (int) status);

  gst_vtenc_session_configure_max_keyframe_interval (self, session,
      self->max_keyframe_interval);
  gst_vtenc_session_configure_max_keyframe_interval_duration (self, session,
      self->max_keyframe_interval_duration / ((gdouble) GST_SECOND));

  gst_vtenc_session_configure_bitrate (self, session,
      gst_vtenc_get_bitrate (self));
  gst_vtenc_session_configure_realtime (self, session,
      gst_vtenc_get_realtime (self));
  gst_vtenc_session_configure_allow_frame_reordering (self, session,
      gst_vtenc_get_allow_frame_reordering (self));
  gst_vtenc_set_quality (self, self->quality);

  if (self->dump_properties) {
    gst_vtenc_session_dump_properties (self, session);
    self->dump_properties = FALSE;
  }
#ifdef HAVE_VIDEOTOOLBOX_10_9_6
  if (VTCompressionSessionPrepareToEncodeFrames) {
    status = VTCompressionSessionPrepareToEncodeFrames (session);
    if (status != noErr) {
      GST_ERROR_OBJECT (self,
          "VTCompressionSessionPrepareToEncodeFrames() returned: %d",
          (int) status);
    }
  }
#endif

beach:
  if (encoder_spec)
    CFRelease (encoder_spec);
  CFRelease (pb_attrs);

  return session;
}

static void
gst_vtenc_destroy_session (GstVTEnc * self, VTCompressionSessionRef * session)
{
  VTCompressionSessionInvalidate (*session);
  if (*session != NULL) {
    CFRelease (*session);
    *session = NULL;
  }
}

typedef struct
{
  GstVTEnc *self;
  VTCompressionSessionRef session;
} GstVTDumpPropCtx;

static void
gst_vtenc_session_dump_property (CFStringRef prop_name,
    CFDictionaryRef prop_attrs, GstVTDumpPropCtx * dpc)
{
  gchar *name_str;
  CFTypeRef prop_value;
  OSStatus status;

  name_str = gst_vtutil_string_to_utf8 (prop_name);
  if (dpc->self->dump_attributes) {
    gchar *attrs_str;

    attrs_str = gst_vtutil_object_to_string (prop_attrs);
    GST_DEBUG_OBJECT (dpc->self, "%s = %s", name_str, attrs_str);
    g_free (attrs_str);
  }

  status = VTSessionCopyProperty (dpc->session, prop_name, NULL, &prop_value);
  if (status == noErr) {
    gchar *value_str;

    value_str = gst_vtutil_object_to_string (prop_value);
    GST_DEBUG_OBJECT (dpc->self, "%s = %s", name_str, value_str);
    g_free (value_str);

    if (prop_value != NULL)
      CFRelease (prop_value);
  } else {
    GST_DEBUG_OBJECT (dpc->self, "%s = <failed to query: %d>",
        name_str, (int) status);
  }

  g_free (name_str);
}

static void
gst_vtenc_session_dump_properties (GstVTEnc * self,
    VTCompressionSessionRef session)
{
  GstVTDumpPropCtx dpc = { self, session };
  CFDictionaryRef dict;
  OSStatus status;

  status = VTSessionCopySupportedPropertyDictionary (session, &dict);
  if (status != noErr)
    goto error;
  CFDictionaryApplyFunction (dict,
      (CFDictionaryApplierFunction) gst_vtenc_session_dump_property, &dpc);
  CFRelease (dict);

  return;

error:
  GST_WARNING_OBJECT (self, "failed to dump properties");
}

static void
gst_vtenc_session_configure_expected_framerate (GstVTEnc * self,
    VTCompressionSessionRef session, gdouble framerate)
{
  gst_vtenc_session_configure_property_double (self, session,
      kVTCompressionPropertyKey_ExpectedFrameRate, framerate);
}

static void
gst_vtenc_session_configure_max_keyframe_interval (GstVTEnc * self,
    VTCompressionSessionRef session, gint interval)
{
  gst_vtenc_session_configure_property_int (self, session,
      kVTCompressionPropertyKey_MaxKeyFrameInterval, interval);
}

static void
gst_vtenc_session_configure_max_keyframe_interval_duration (GstVTEnc * self,
    VTCompressionSessionRef session, gdouble duration)
{
  gst_vtenc_session_configure_property_double (self, session,
      kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, duration);
}

static void
gst_vtenc_session_configure_bitrate (GstVTEnc * self,
    VTCompressionSessionRef session, guint bitrate)
{
  gst_vtenc_session_configure_property_int (self, session,
      kVTCompressionPropertyKey_AverageBitRate, bitrate);
}

static void
gst_vtenc_session_configure_allow_frame_reordering (GstVTEnc * self,
    VTCompressionSessionRef session, gboolean allow_frame_reordering)
{
  VTSessionSetProperty (session, kVTCompressionPropertyKey_AllowFrameReordering,
      allow_frame_reordering ? kCFBooleanTrue : kCFBooleanFalse);
}

static void
gst_vtenc_session_configure_realtime (GstVTEnc * self,
    VTCompressionSessionRef session, gboolean realtime)
{
  VTSessionSetProperty (session, kVTCompressionPropertyKey_RealTime,
      realtime ? kCFBooleanTrue : kCFBooleanFalse);
}

static OSStatus
gst_vtenc_session_configure_property_int (GstVTEnc * self,
    VTCompressionSessionRef session, CFStringRef name, gint value)
{
  CFNumberRef num;
  OSStatus status;
  gchar name_str[128];

  num = CFNumberCreate (NULL, kCFNumberIntType, &value);
  status = VTSessionSetProperty (session, name, num);
  CFRelease (num);

  CFStringGetCString (name, name_str, sizeof (name_str), kCFStringEncodingUTF8);
  GST_DEBUG_OBJECT (self, "%s(%d) => %d", name_str, value, (int) status);

  return status;
}

static OSStatus
gst_vtenc_session_configure_property_double (GstVTEnc * self,
    VTCompressionSessionRef session, CFStringRef name, gdouble value)
{
  CFNumberRef num;
  OSStatus status;
  gchar name_str[128];

  num = CFNumberCreate (NULL, kCFNumberDoubleType, &value);
  status = VTSessionSetProperty (session, name, num);
  CFRelease (num);

  CFStringGetCString (name, name_str, sizeof (name_str), kCFStringEncodingUTF8);
  GST_DEBUG_OBJECT (self, "%s(%f) => %d", name_str, value, (int) status);

  return status;
}

static void
gst_vtenc_update_latency (GstVTEnc * self)
{
  OSStatus status;
  CFNumberRef value;
  int frames = 0;
  GstClockTime frame_duration;
  GstClockTime latency;

  if (self->video_info.fps_d == 0) {
    GST_INFO_OBJECT (self, "framerate not known, can't set latency");
    return;
  }

  status = VTSessionCopyProperty (self->session,
      kVTCompressionPropertyKey_NumberOfPendingFrames, NULL, &value);
  if (status != noErr || !value) {
    GST_INFO_OBJECT (self, "failed to get NumberOfPendingFrames: %d", status);
    return;
  }

  CFNumberGetValue (value, kCFNumberSInt32Type, &frames);
  if (self->latency_frames == -1 || self->latency_frames != frames) {
    self->latency_frames = frames;
    if (self->video_info.fps_d == 0 || self->video_info.fps_n == 0) {
      /* FIXME: Assume 25fps. This is better than reporting no latency at
       * all and then later failing in live pipelines
       */
      frame_duration = gst_util_uint64_scale (GST_SECOND, 1, 25);
    } else {
      frame_duration = gst_util_uint64_scale (GST_SECOND,
          self->video_info.fps_d, self->video_info.fps_n);
    }
    latency = frame_duration * frames;
    GST_INFO_OBJECT (self,
        "latency status %d frames %d fps %d/%d time %" GST_TIME_FORMAT, status,
        frames, self->video_info.fps_n, self->video_info.fps_d,
        GST_TIME_ARGS (latency));
    gst_video_encoder_set_latency (GST_VIDEO_ENCODER (self), latency, latency);
  }
  CFRelease (value);
}

static GstFlowReturn
gst_vtenc_encode_frame (GstVTEnc * self, GstVideoCodecFrame * frame)
{
  CMTime ts, duration;
  GstCoreMediaMeta *meta;
  CVPixelBufferRef pbuf = NULL;
  GstVideoCodecFrame *outframe;
  OSStatus vt_status;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean renegotiated;
  CFDictionaryRef frame_props = NULL;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    GST_INFO_OBJECT (self, "received force-keyframe-event, will force intra");
    frame_props = self->keyframe_props;
  }

  ts = CMTimeMake (frame->pts, GST_SECOND);
  if (frame->duration != GST_CLOCK_TIME_NONE)
    duration = CMTimeMake (frame->duration, GST_SECOND);
  else
    duration = kCMTimeInvalid;

  meta = gst_buffer_get_core_media_meta (frame->input_buffer);
  if (meta != NULL) {
    pbuf = gst_core_media_buffer_get_pixel_buffer (frame->input_buffer);
  }
#ifdef HAVE_IOS
  if (pbuf == NULL) {
    GstVideoFrame inframe, outframe;
    GstBuffer *outbuf;
    OSType pixel_format_type;
    CVReturn cv_ret;

    /* FIXME: iOS has special stride requirements that we don't know yet.
     * Copy into a newly allocated pixelbuffer for now. Probably makes
     * sense to create a buffer pool around these at some point.
     */

    switch (GST_VIDEO_INFO_FORMAT (&self->video_info)) {
      case GST_VIDEO_FORMAT_I420:
        pixel_format_type = kCVPixelFormatType_420YpCbCr8Planar;
        break;
      case GST_VIDEO_FORMAT_NV12:
        pixel_format_type = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        break;
      default:
        goto cv_error;
    }

    if (!gst_video_frame_map (&inframe, &self->video_info, frame->input_buffer,
            GST_MAP_READ))
      goto cv_error;

    cv_ret =
        CVPixelBufferCreate (NULL, self->negotiated_width,
        self->negotiated_height, pixel_format_type, NULL, &pbuf);

    if (cv_ret != kCVReturnSuccess) {
      gst_video_frame_unmap (&inframe);
      goto cv_error;
    }

    outbuf =
        gst_core_video_buffer_new ((CVBufferRef) pbuf, &self->video_info, NULL);
    if (!gst_video_frame_map (&outframe, &self->video_info, outbuf,
            GST_MAP_WRITE)) {
      gst_video_frame_unmap (&inframe);
      gst_buffer_unref (outbuf);
      CVPixelBufferRelease (pbuf);
      goto cv_error;
    }

    if (!gst_video_frame_copy (&outframe, &inframe)) {
      gst_video_frame_unmap (&inframe);
      gst_buffer_unref (outbuf);
      CVPixelBufferRelease (pbuf);
      goto cv_error;
    }

    gst_buffer_unref (outbuf);
    gst_video_frame_unmap (&inframe);
    gst_video_frame_unmap (&outframe);
  }
#else
  if (pbuf == NULL) {
    GstVTEncFrame *vframe;
    CVReturn cv_ret;

    vframe = gst_vtenc_frame_new (frame->input_buffer, &self->video_info);
    if (!vframe)
      goto cv_error;

    {
      const size_t num_planes = GST_VIDEO_FRAME_N_PLANES (&vframe->videoframe);
      void *plane_base_addresses[GST_VIDEO_MAX_PLANES];
      size_t plane_widths[GST_VIDEO_MAX_PLANES];
      size_t plane_heights[GST_VIDEO_MAX_PLANES];
      size_t plane_bytes_per_row[GST_VIDEO_MAX_PLANES];
      OSType pixel_format_type;
      size_t i;

      for (i = 0; i < num_planes; i++) {
        plane_base_addresses[i] =
            GST_VIDEO_FRAME_PLANE_DATA (&vframe->videoframe, i);
        plane_widths[i] = GST_VIDEO_FRAME_COMP_WIDTH (&vframe->videoframe, i);
        plane_heights[i] = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe->videoframe, i);
        plane_bytes_per_row[i] =
            GST_VIDEO_FRAME_COMP_STRIDE (&vframe->videoframe, i);
        plane_bytes_per_row[i] =
            GST_VIDEO_FRAME_COMP_STRIDE (&vframe->videoframe, i);
      }

      switch (GST_VIDEO_INFO_FORMAT (&self->video_info)) {
        case GST_VIDEO_FORMAT_I420:
          pixel_format_type = kCVPixelFormatType_420YpCbCr8Planar;
          break;
        case GST_VIDEO_FORMAT_NV12:
          pixel_format_type = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
          break;
        case GST_VIDEO_FORMAT_UYVY:
          pixel_format_type = kCVPixelFormatType_422YpCbCr8;
          break;
        default:
          gst_vtenc_frame_free (vframe);
          goto cv_error;
      }

      cv_ret = CVPixelBufferCreateWithPlanarBytes (NULL,
          self->negotiated_width, self->negotiated_height,
          pixel_format_type,
          frame,
          GST_VIDEO_FRAME_SIZE (&vframe->videoframe),
          num_planes,
          plane_base_addresses,
          plane_widths,
          plane_heights,
          plane_bytes_per_row, gst_pixel_buffer_release_cb, vframe, NULL,
          &pbuf);
      if (cv_ret != kCVReturnSuccess) {
        gst_vtenc_frame_free (vframe);
        goto cv_error;
      }
    }
  }
#endif

  /* We need to unlock the stream lock here because
   * it can wait for gst_vtenc_enqueue_buffer() to
   * handle a buffer... which will take the stream
   * lock from another thread and then deadlock */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  vt_status = VTCompressionSessionEncodeFrame (self->session,
      pbuf, ts, duration, frame_props,
      GINT_TO_POINTER (frame->system_frame_number), NULL);
  GST_VIDEO_ENCODER_STREAM_LOCK (self);

  if (vt_status != noErr) {
    GST_WARNING_OBJECT (self, "VTCompressionSessionEncodeFrame returned %d",
        (int) vt_status);
  }

  gst_video_codec_frame_unref (frame);

  CVPixelBufferRelease (pbuf);

  renegotiated = FALSE;
  while ((outframe = g_async_queue_try_pop (self->cur_outframes))) {
    if (outframe->output_buffer) {
      if (!renegotiated) {
        meta = gst_buffer_get_core_media_meta (outframe->output_buffer);
        /* Try to renegotiate once */
        if (meta) {
          if (gst_vtenc_negotiate_downstream (self, meta->sample_buf)) {
            renegotiated = TRUE;
          } else {
            ret = GST_FLOW_NOT_NEGOTIATED;
            gst_video_codec_frame_unref (outframe);
            /* the rest of the frames will be pop'd and unref'd later */
            break;
          }
        }
      }

      gst_vtenc_update_latency (self);
    }

    /* releases frame, even if it has no output buffer (i.e. failed to encode) */
    ret =
        gst_video_encoder_finish_frame (GST_VIDEO_ENCODER_CAST (self),
        outframe);
  }

  return ret;

cv_error:
  {
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
}

static void
gst_vtenc_enqueue_buffer (void *outputCallbackRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer)
{
  GstVTEnc *self = outputCallbackRefCon;
  gboolean is_keyframe;
  GstVideoCodecFrame *frame;

  frame =
      gst_video_encoder_get_frame (GST_VIDEO_ENCODER_CAST (self),
      GPOINTER_TO_INT (sourceFrameRefCon));

  if (status != noErr) {
    if (frame) {
      GST_ELEMENT_ERROR (self, LIBRARY, ENCODE, (NULL),
          ("Failed to encode frame %d: %d", frame->system_frame_number,
              (int) status));
    } else {
      GST_ELEMENT_ERROR (self, LIBRARY, ENCODE, (NULL),
          ("Failed to encode (frame unknown): %d", (int) status));
    }
    goto beach;
  }

  if (!frame) {
    GST_WARNING_OBJECT (self, "No corresponding frame found!");
    goto beach;
  }

  /* This may happen if we don't have enough bitrate */
  if (sampleBuffer == NULL)
    goto beach;

  is_keyframe = gst_vtenc_buffer_is_keyframe (self, sampleBuffer);

  if (is_keyframe) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    gst_vtenc_clear_cached_caps_downstream (self);
  }

  /* We are dealing with block buffers here, so we don't need
   * to enable the use of the video meta API on the core media buffer */
  frame->output_buffer = gst_core_media_buffer_new (sampleBuffer, FALSE, NULL);

beach:
  /* needed anyway so the frame will be released */
  if (frame)
    g_async_queue_push (self->cur_outframes, frame);
}

static gboolean
gst_vtenc_buffer_is_keyframe (GstVTEnc * self, CMSampleBufferRef sbuf)
{
  gboolean result = FALSE;
  CFArrayRef attachments_for_sample;

  attachments_for_sample = CMSampleBufferGetSampleAttachmentsArray (sbuf, 0);
  if (attachments_for_sample != NULL) {
    CFDictionaryRef attachments;
    CFBooleanRef depends_on_others;

    attachments = CFArrayGetValueAtIndex (attachments_for_sample, 0);
    depends_on_others = CFDictionaryGetValue (attachments,
        kCMSampleAttachmentKey_DependsOnOthers);
    result = (depends_on_others == kCFBooleanFalse);
  }

  return result;
}

#ifndef HAVE_IOS
static GstVTEncFrame *
gst_vtenc_frame_new (GstBuffer * buf, GstVideoInfo * video_info)
{
  GstVTEncFrame *frame;

  frame = g_slice_new (GstVTEncFrame);
  frame->buf = gst_buffer_ref (buf);
  if (!gst_video_frame_map (&frame->videoframe, video_info, buf, GST_MAP_READ)) {
    gst_buffer_unref (frame->buf);
    g_slice_free (GstVTEncFrame, frame);
    return NULL;
  }

  return frame;
}

static void
gst_vtenc_frame_free (GstVTEncFrame * frame)
{
  gst_video_frame_unmap (&frame->videoframe);
  gst_buffer_unref (frame->buf);
  g_slice_free (GstVTEncFrame, frame);
}

static void
gst_pixel_buffer_release_cb (void *releaseRefCon, const void *dataPtr,
    size_t dataSize, size_t numberOfPlanes, const void *planeAddresses[])
{
  GstVTEncFrame *frame = (GstVTEncFrame *) releaseRefCon;
  gst_vtenc_frame_free (frame);
}
#endif

static void
gst_vtenc_register (GstPlugin * plugin,
    const GstVTEncoderDetails * codec_details)
{
  GTypeInfo type_info = {
    sizeof (GstVTEncClass),
    (GBaseInitFunc) gst_vtenc_base_init,
    NULL,
    (GClassInitFunc) gst_vtenc_class_init,
    NULL,
    NULL,
    sizeof (GstVTEnc),
    0,
    (GInstanceInitFunc) gst_vtenc_init,
  };
  gchar *type_name;
  GType type;
  gboolean result;

  type_name = g_strdup_printf ("vtenc_%s", codec_details->element_name);

  type =
      g_type_register_static (GST_TYPE_VIDEO_ENCODER, type_name, &type_info, 0);

  g_type_set_qdata (type, GST_VTENC_CODEC_DETAILS_QDATA,
      (gpointer) codec_details);

  result = gst_element_register (plugin, type_name, GST_RANK_PRIMARY, type);
  if (!result) {
    GST_ERROR_OBJECT (plugin, "failed to register element %s", type_name);
  }

  g_free (type_name);
}

static const GstVTEncoderDetails gst_vtenc_codecs[] = {
  {"H.264", "h264", "video/x-h264", kCMVideoCodecType_H264, FALSE},
#ifndef HAVE_IOS
  {"H.264 (HW only)", "h264_hw", "video/x-h264", kCMVideoCodecType_H264, TRUE},
#endif
};

void
gst_vtenc_register_elements (GstPlugin * plugin)
{
  guint i;

  GST_DEBUG_CATEGORY_INIT (gst_vtenc_debug, "vtenc",
      0, "Apple VideoToolbox Encoder Wrapper");

  for (i = 0; i != G_N_ELEMENTS (gst_vtenc_codecs); i++)
    gst_vtenc_register (plugin, &gst_vtenc_codecs[i]);
}
