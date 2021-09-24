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
 * SECTION:element-rtphdrextrfc6464
 * @title: rtphdrextrfc6464
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

#include "gstrtphdrext-rfc6464.h"

#include <gst/audio/audio.h>

#define RFC6464_HDR_EXT_URI GST_RTP_HDREXT_BASE"ssrc-audio-level"

GST_DEBUG_CATEGORY_STATIC (rtphdrrfc6464_twcc_debug);
#define GST_CAT_DEFAULT (rtphdrrfc6464_twcc_debug)

#define DEFAULT_VAD TRUE

enum
{
  PROP_0,
  PROP_VAD,
};

struct _GstRTPHeaderExtensionRfc6464
{
  GstRTPHeaderExtension parent;

  gboolean vad;
};

G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionRfc6464,
    gst_rtp_header_extension_rfc6464, GST_TYPE_RTP_HEADER_EXTENSION,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtphdrextrfc6464", 0,
        "RTP RFC 6464 Header Extensions"););
GST_ELEMENT_REGISTER_DEFINE (rtphdrextrfc6464, "rtphdrextrfc6464",
    GST_RANK_MARGINAL, GST_TYPE_RTP_HEADER_EXTENSION_RFC6464);

static void
gst_rtp_header_extension_rfc6464_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionRfc6464 *self =
      GST_RTP_HEADER_EXTENSION_RFC6464 (object);

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
gst_rtp_header_extension_rfc6464_get_supported_flags (GstRTPHeaderExtension *
    ext)
{
  return GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE;
}

static gsize
gst_rtp_header_extension_rfc6464_get_max_size (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta)
{
  return 2;
}

static void
set_vad (GstRTPHeaderExtension * ext, gboolean vad)
{
  GstRTPHeaderExtensionRfc6464 *self = GST_RTP_HEADER_EXTENSION_RFC6464 (ext);

  if (self->vad == vad)
    return;

  GST_DEBUG_OBJECT (ext, "vad: %d", vad);
  self->vad = vad;
  g_object_notify (G_OBJECT (self), "vad");
}

static gboolean
gst_rtp_header_extension_rfc6464_set_attributes_from_caps (GstRTPHeaderExtension
    * ext, const GstCaps * caps)
{
  gchar *field_name = gst_rtp_header_extension_get_sdp_caps_field_name (ext);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *ext_uri;
  const GValue *arr;

  if (!field_name)
    return FALSE;

  if ((ext_uri = gst_structure_get_string (s, field_name))) {
    if (g_strcmp0 (ext_uri, gst_rtp_header_extension_get_uri (ext)) != 0) {
      /* incompatible extension uri for this instance */
      goto error;
    }
    set_vad (ext, DEFAULT_VAD);
  } else if ((arr = gst_structure_get_value (s, field_name))
      && GST_VALUE_HOLDS_ARRAY (arr)
      && gst_value_array_get_size (arr) == 3) {
    const GValue *val;
    const gchar *vad_attr;

    val = gst_value_array_get_value (arr, 1);
    if (!G_VALUE_HOLDS_STRING (val))
      goto error;
    if (g_strcmp0 (g_value_get_string (val),
            gst_rtp_header_extension_get_uri (ext)) != 0)
      goto error;

    val = gst_value_array_get_value (arr, 2);
    if (!G_VALUE_HOLDS_STRING (val))
      goto error;

    vad_attr = g_value_get_string (val);

    if (g_str_equal (vad_attr, "vad=on"))
      set_vad (ext, TRUE);
    else if (g_str_equal (vad_attr, "vad=off"))
      set_vad (ext, FALSE);
    else {
      GST_WARNING_OBJECT (ext, "Invalid attribute: %s", vad_attr);
      goto error;
    }
  } else {
    /* unknown caps format */
    goto error;
  }

  g_free (field_name);
  return TRUE;

error:
  g_free (field_name);
  return FALSE;
}

static gboolean
gst_rtp_header_extension_rfc6464_set_caps_from_attributes (GstRTPHeaderExtension
    * ext, GstCaps * caps)
{
  GstRTPHeaderExtensionRfc6464 *self = GST_RTP_HEADER_EXTENSION_RFC6464 (ext);
  gchar *field_name = gst_rtp_header_extension_get_sdp_caps_field_name (ext);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  GValue arr = G_VALUE_INIT;
  GValue val = G_VALUE_INIT;

  if (!field_name)
    return FALSE;

  g_value_init (&arr, GST_TYPE_ARRAY);
  g_value_init (&val, G_TYPE_STRING);

  /* direction */
  g_value_set_string (&val, "");
  gst_value_array_append_value (&arr, &val);

  /* uri */
  g_value_set_string (&val, gst_rtp_header_extension_get_uri (ext));
  gst_value_array_append_value (&arr, &val);

  /* attributes */
  if (self->vad)
    g_value_set_string (&val, "vad=on");
  else
    g_value_set_string (&val, "vad=off");
  gst_value_array_append_value (&arr, &val);

  gst_structure_set_value (s, field_name, &arr);

  GST_DEBUG_OBJECT (self, "%" GST_PTR_FORMAT, caps);

  g_value_unset (&val);
  g_value_unset (&arr);

  g_free (field_name);
  return TRUE;
}

static gssize
gst_rtp_header_extension_rfc6464_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstAudioLevelMeta *meta;
  guint level;

  g_return_val_if_fail (size >=
      gst_rtp_header_extension_rfc6464_get_max_size (ext, NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_rfc6464_get_supported_flags (ext), -1);

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

  GST_LOG_OBJECT (ext, "writing ext (level: %d voice: %d)", meta->level,
      meta->voice_activity);

  /* Both one & two byte use the same format, the second byte being padding */
  data[0] = (meta->level & 0x7F) | (meta->voice_activity << 7);
  if (write_flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE) {
    return 1;
  }
  data[1] = 0;
  return 2;
}

static gboolean
gst_rtp_header_extension_rfc6464_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer)
{
  guint8 level;
  gboolean voice_activity;

  g_return_val_if_fail (read_flags &
      gst_rtp_header_extension_rfc6464_get_supported_flags (ext), -1);

  /* Both one & two byte use the same format, the second byte being padding */
  level = data[0] & 0x7F;
  voice_activity = (data[0] & 0x80) >> 7;

  GST_LOG_OBJECT (ext, "reading ext (level: %d voice: %d)", level,
      voice_activity);

  gst_buffer_add_audio_level_meta (buffer, level, voice_activity);

  return TRUE;
}

static void
gst_rtp_header_extension_rfc6464_class_init (GstRTPHeaderExtensionRfc6464Class *
    klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  rtp_hdr_class = GST_RTP_HEADER_EXTENSION_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->get_property = gst_rtp_header_extension_rfc6464_get_property;

  /**
   * rtphdrextrfc6464:vad:
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
      gst_rtp_header_extension_rfc6464_get_supported_flags;
  rtp_hdr_class->get_max_size = gst_rtp_header_extension_rfc6464_get_max_size;
  rtp_hdr_class->set_attributes_from_caps =
      gst_rtp_header_extension_rfc6464_set_attributes_from_caps;
  rtp_hdr_class->set_caps_from_attributes =
      gst_rtp_header_extension_rfc6464_set_caps_from_attributes;
  rtp_hdr_class->write = gst_rtp_header_extension_rfc6464_write;
  rtp_hdr_class->read = gst_rtp_header_extension_rfc6464_read;

  gst_element_class_set_static_metadata (gstelement_class,
      "Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension",
      GST_RTP_HDREXT_ELEMENT_CLASS,
      "Client-to-Mixer Audio Level Indication (RFC6464) RTP Header Extension",
      "Guillaume Desmottes <guillaume.desmottes@collabora.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class, RFC6464_HDR_EXT_URI);
}

static void
gst_rtp_header_extension_rfc6464_init (GstRTPHeaderExtensionRfc6464 * self)
{
  GST_DEBUG_OBJECT (self, "creating element");
  self->vad = DEFAULT_VAD;
}
