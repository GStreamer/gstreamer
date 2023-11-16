/* GStreamer
 * Copyright (C) <2018> Havard Graff <havard.graff@gmail.com>
 * Copyright (C) <2020-2021> Guillaume Desmottes <guillaume.desmottes@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

/**
 * SECTION:element-rtphdrextclientaudiolevel
 * @title: rtphdrextclientaudiolevel
 * @short_description: Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension
 *
 * Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension.
 * The extension should be automatically created by payloader and depayloaders,
 * if their `auto-header-extension` property is enabled, if the extension
 * is part of the RTP caps.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 pulsesrc ! level audio-level-meta=true ! audiconvert !
 *   rtpL16pay ! application/x-rtp,
 *     extmap-1=(string)\< \"\", urn:ietf:params:rtp-hdrext:ssrc-audio-level,
 *     \"vad=on\" \> ! udpsink
 * ]|
 *
 * Since: 1.20
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtphdrext-clientaudiolevel.h"

#include <gst/audio/audio.h>

#define CLIENT_AUDIO_LEVEL_HDR_EXT_URI GST_RTP_HDREXT_BASE"ssrc-audio-level"

GST_DEBUG_CATEGORY_STATIC (rtphdrclient_audio_level_debug);
#define GST_CAT_DEFAULT (rtphdrclient_audio_level_debug)

#define DEFAULT_VAD TRUE

enum
{
  PROP_0,
  PROP_VAD,
};

struct _GstRTPHeaderExtensionClientAudioLevel
{
  GstRTPHeaderExtension parent;

  gboolean vad;
};

G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionClientAudioLevel,
    gst_rtp_header_extension_client_audio_level, GST_TYPE_RTP_HEADER_EXTENSION,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtphdrextclientaudiolevel", 0,
        "RTP RFC 6464 Header Extensions"););
GST_ELEMENT_REGISTER_DEFINE (rtphdrextclientaudiolevel,
    "rtphdrextclientaudiolevel", GST_RANK_MARGINAL,
    GST_TYPE_RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL);

static void
gst_rtp_header_extension_client_audio_level_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionClientAudioLevel *self =
      GST_RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL (object);

  switch (prop_id) {
    case PROP_VAD:
      g_value_set_boolean (value, self->vad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstRTPHeaderExtensionFlags
    gst_rtp_header_extension_client_audio_level_get_supported_flags
    (GstRTPHeaderExtension * ext)
{
  return GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE;
}

static gsize
gst_rtp_header_extension_client_audio_level_get_max_size (GstRTPHeaderExtension
    * ext, const GstBuffer * input_meta)
{
  return 2;
}

static void
set_vad (GstRTPHeaderExtension * ext, gboolean vad)
{
  GstRTPHeaderExtensionClientAudioLevel *self =
      GST_RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL (ext);

  if (self->vad == vad)
    return;

  GST_DEBUG_OBJECT (ext, "vad: %d", vad);
  self->vad = vad;
  g_object_notify (G_OBJECT (self), "vad");
}

static gboolean
    gst_rtp_header_extension_client_audio_level_set_attributes
    (GstRTPHeaderExtension * ext, GstRTPHeaderExtensionDirection direction,
    const gchar * attributes)
{
  if (g_str_equal (attributes, "vad=on") || g_str_equal (attributes, "")) {
    set_vad (ext, TRUE);
  } else if (g_str_equal (attributes, "vad=off")) {
    set_vad (ext, FALSE);
  } else {
    GST_WARNING_OBJECT (ext, "Invalid attribute: %s", attributes);
    return FALSE;
  }

  return TRUE;
}

static gboolean
    gst_rtp_header_extension_client_audio_level_set_caps_from_attributes
    (GstRTPHeaderExtension * ext, GstCaps * caps)
{
  GstRTPHeaderExtensionClientAudioLevel *self =
      GST_RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL (ext);
  const gchar *vad;

  if (self->vad)
    vad = "vad=on";
  else
    vad = "vad=off";

  return gst_rtp_header_extension_set_caps_from_attributes_helper (ext, caps,
      vad);
}

static gssize
gst_rtp_header_extension_client_audio_level_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstAudioLevelMeta *meta;
  guint level;

  g_return_val_if_fail (size >=
      gst_rtp_header_extension_client_audio_level_get_max_size (ext, NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_client_audio_level_get_supported_flags (ext),
      -1);

  meta = gst_buffer_get_audio_level_meta ((GstBuffer *) input_meta);
  if (!meta) {
    GST_LOG_OBJECT (ext, "no meta");
    return 0;
  }

  level = meta->level;
  if (level > 127) {
    GST_LOG_OBJECT (ext, "level from meta is higher than 127: %d, cropping",
        meta->level);
    level = 127;
  }

  GST_LOG_OBJECT (ext, "writing ext (level: %d voice: %d)", level,
      meta->voice_activity);

  /* Both one & two byte use the same format, the second byte being padding */
  data[0] = (level & 0x7F) | (meta->voice_activity << 7);
  if (write_flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE) {
    return 1;
  }
  data[1] = 0;
  return 2;
}

static gboolean
gst_rtp_header_extension_client_audio_level_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer)
{
  guint8 level;
  gboolean voice_activity;

  g_return_val_if_fail (read_flags &
      gst_rtp_header_extension_client_audio_level_get_supported_flags (ext),
      -1);

  /* Both one & two byte use the same format, the second byte being padding */
  level = data[0] & 0x7F;
  voice_activity = (data[0] & 0x80) >> 7;

  GST_LOG_OBJECT (ext, "reading ext (level: %d voice: %d)", level,
      voice_activity);

  gst_buffer_add_audio_level_meta (buffer, level, voice_activity);

  return TRUE;
}

static void
    gst_rtp_header_extension_client_audio_level_class_init
    (GstRTPHeaderExtensionClientAudioLevelClass * klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  rtp_hdr_class = GST_RTP_HEADER_EXTENSION_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->get_property =
      gst_rtp_header_extension_client_audio_level_get_property;

  /**
   * rtphdrextclientaudiolevel:vad:
   *
   * If the vad extension attribute is enabled or not, default to %FALSE.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_VAD,
      g_param_spec_boolean ("vad", "vad",
          "If the vad extension attribute is enabled or not",
          DEFAULT_VAD, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  rtp_hdr_class->get_supported_flags =
      gst_rtp_header_extension_client_audio_level_get_supported_flags;
  rtp_hdr_class->get_max_size =
      gst_rtp_header_extension_client_audio_level_get_max_size;
  rtp_hdr_class->set_attributes =
      gst_rtp_header_extension_client_audio_level_set_attributes;
  rtp_hdr_class->set_caps_from_attributes =
      gst_rtp_header_extension_client_audio_level_set_caps_from_attributes;
  rtp_hdr_class->write = gst_rtp_header_extension_client_audio_level_write;
  rtp_hdr_class->read = gst_rtp_header_extension_client_audio_level_read;

  gst_element_class_set_static_metadata (gstelement_class,
      "Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension",
      GST_RTP_HDREXT_ELEMENT_CLASS,
      "Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension",
      "Guillaume Desmottes <guillaume.desmottes@collabora.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class,
      CLIENT_AUDIO_LEVEL_HDR_EXT_URI);
}

static void
    gst_rtp_header_extension_client_audio_level_init
    (GstRTPHeaderExtensionClientAudioLevel * self)
{
  GST_DEBUG_OBJECT (self, "creating element");
  self->vad = DEFAULT_VAD;
}
