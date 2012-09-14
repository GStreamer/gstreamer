/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>

#include <netinet/in.h>

#include <bluetooth/bluetooth.h>

#include <gst/rtp/gstrtpbuffer.h>

#include <dbus/dbus.h>

#include "rtp.h"
#include "a2dp-codecs.h"

#include "gstpragma.h"
#include "gstavdtpsink.h"

GST_DEBUG_CATEGORY_STATIC (avdtp_sink_debug);
#define GST_CAT_DEFAULT avdtp_sink_debug

#define BUFFER_SIZE 2048
#define TEMPLATE_MAX_BITPOOL 64
#define CRC_PROTECTED 1
#define CRC_UNPROTECTED 0

#define DEFAULT_AUTOCONNECT TRUE

#define GST_AVDTP_SINK_MUTEX_LOCK(s) G_STMT_START {	\
		g_mutex_lock(s->sink_lock);		\
	} G_STMT_END

#define GST_AVDTP_SINK_MUTEX_UNLOCK(s) G_STMT_START {	\
		g_mutex_unlock(s->sink_lock);		\
	} G_STMT_END

struct bluetooth_data
{
  guint link_mtu;

  DBusConnection *conn;
  guint8 codec;                 /* Bluetooth transport configuration */
  gchar *uuid;
  guint8 *config;
  gint config_size;

  gchar buffer[BUFFER_SIZE];    /* Codec transfer buffer */
};

#define IS_SBC(n) (strcmp((n), "audio/x-sbc") == 0)
#define IS_MPEG_AUDIO(n) (strcmp((n), "audio/mpeg") == 0)

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_AUTOCONNECT,
  PROP_TRANSPORT
};

GST_BOILERPLATE (GstAvdtpSink, gst_avdtp_sink, GstBaseSink, GST_TYPE_BASE_SINK);

static const GstElementDetails avdtp_sink_details =
GST_ELEMENT_DETAILS ("Bluetooth AVDTP sink",
    "Sink/Audio",
    "Plays audio to an A2DP device",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate avdtp_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\","
        "payload = (int) "
        GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 16000, 32000, "
        "44100, 48000 }, "
        "encoding-name = (string) \"SBC\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) "
        GST_RTP_PAYLOAD_MPA_STRING ", "
        "clock-rate = (int) 90000; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) "
        GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPA\""));

static void
gst_avdtp_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&avdtp_sink_factory));

  gst_element_class_set_details (element_class, &avdtp_sink_details);
}

static void
gst_avdtp_sink_transport_release (GstAvdtpSink * self)
{
  DBusMessage *msg;
  const char *access_type = "w";

  msg = dbus_message_new_method_call ("org.bluez", self->transport,
      "org.bluez.MediaTransport", "Release");

  dbus_message_append_args (msg, DBUS_TYPE_STRING, &access_type,
      DBUS_TYPE_INVALID);

  dbus_connection_send (self->data->conn, msg, NULL);

  dbus_message_unref (msg);
}

static gboolean
gst_avdtp_sink_stop (GstBaseSink * basesink)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);

  GST_INFO_OBJECT (self, "stop");

  if (self->watch_id != 0) {
    g_source_remove (self->watch_id);
    self->watch_id = 0;
  }

  if (self->stream) {
    g_io_channel_shutdown (self->stream, TRUE, NULL);
    g_io_channel_unref (self->stream);
    self->stream = NULL;
  }

  if (self->data) {
    if (self->transport)
      gst_avdtp_sink_transport_release (self);
    if (self->data->conn)
      dbus_connection_unref (self->data->conn);
    g_free (self->data);
    self->data = NULL;
  }

  if (self->stream_caps) {
    gst_caps_unref (self->stream_caps);
    self->stream_caps = NULL;
  }

  if (self->dev_caps) {
    gst_caps_unref (self->dev_caps);
    self->dev_caps = NULL;
  }

  return TRUE;
}

static void
gst_avdtp_sink_finalize (GObject * object)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (object);

  if (self->data)
    gst_avdtp_sink_stop (GST_BASE_SINK (self));

  if (self->device)
    g_free (self->device);

  if (self->transport)
    g_free (self->transport);

  g_mutex_free (self->sink_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avdtp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvdtpSink *sink = GST_AVDTP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      if (sink->device)
        g_free (sink->device);
      sink->device = g_value_dup_string (value);
      break;

    case PROP_AUTOCONNECT:
      sink->autoconnect = g_value_get_boolean (value);
      break;

    case PROP_TRANSPORT:
      if (sink->transport)
        g_free (sink->transport);
      sink->transport = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avdtp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvdtpSink *sink = GST_AVDTP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, sink->device);
      break;

    case PROP_AUTOCONNECT:
      g_value_set_boolean (value, sink->autoconnect);
      break;

    case PROP_TRANSPORT:
      g_value_set_string (value, sink->transport);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStructure *
gst_avdtp_sink_parse_sbc_raw (GstAvdtpSink * self)
{
  a2dp_sbc_t *sbc = (a2dp_sbc_t *) self->data->config;
  GstStructure *structure;
  GValue *value;
  GValue *list;
  gboolean mono, stereo;

  structure = gst_structure_empty_new ("audio/x-sbc");
  value = g_value_init (g_new0 (GValue, 1), G_TYPE_STRING);

  /* mode */
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (sbc->channel_mode & SBC_CHANNEL_MODE_MONO) {
    g_value_set_static_string (value, "mono");
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->channel_mode & SBC_CHANNEL_MODE_STEREO) {
    g_value_set_static_string (value, "stereo");
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->channel_mode & SBC_CHANNEL_MODE_DUAL_CHANNEL) {
    g_value_set_static_string (value, "dual");
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->channel_mode & SBC_CHANNEL_MODE_JOINT_STEREO) {
    g_value_set_static_string (value, "joint");
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "mode", list);
    g_free (list);
    list = NULL;
  }

  /* subbands */
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  value = g_value_init (value, G_TYPE_INT);
  if (sbc->subbands & SBC_SUBBANDS_4) {
    g_value_set_int (value, 4);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->subbands & SBC_SUBBANDS_8) {
    g_value_set_int (value, 8);
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "subbands", list);
    g_free (list);
    list = NULL;
  }

  /* blocks */
  value = g_value_init (value, G_TYPE_INT);
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (sbc->block_length & SBC_BLOCK_LENGTH_16) {
    g_value_set_int (value, 16);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & SBC_BLOCK_LENGTH_12) {
    g_value_set_int (value, 12);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & SBC_BLOCK_LENGTH_8) {
    g_value_set_int (value, 8);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & SBC_BLOCK_LENGTH_4) {
    g_value_set_int (value, 4);
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "blocks", list);
    g_free (list);
    list = NULL;
  }

  /* allocation */
  g_value_init (value, G_TYPE_STRING);
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (sbc->allocation_method & SBC_ALLOCATION_LOUDNESS) {
    g_value_set_static_string (value, "loudness");
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->allocation_method & SBC_ALLOCATION_SNR) {
    g_value_set_static_string (value, "snr");
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "allocation", list);
    g_free (list);
    list = NULL;
  }

  /* rate */
  g_value_init (value, G_TYPE_INT);
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (sbc->frequency & SBC_SAMPLING_FREQ_48000) {
    g_value_set_int (value, 48000);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & SBC_SAMPLING_FREQ_44100) {
    g_value_set_int (value, 44100);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & SBC_SAMPLING_FREQ_32000) {
    g_value_set_int (value, 32000);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & SBC_SAMPLING_FREQ_16000) {
    g_value_set_int (value, 16000);
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "rate", list);
    g_free (list);
    list = NULL;
  }

  /* bitpool */
  value = g_value_init (value, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (value,
      MIN (sbc->min_bitpool, TEMPLATE_MAX_BITPOOL),
      MIN (sbc->max_bitpool, TEMPLATE_MAX_BITPOOL));
  gst_structure_set_value (structure, "bitpool", value);
  g_value_unset (value);

  /* channels */
  mono = FALSE;
  stereo = FALSE;
  if (sbc->channel_mode & SBC_CHANNEL_MODE_MONO)
    mono = TRUE;
  if ((sbc->channel_mode & SBC_CHANNEL_MODE_STEREO) ||
      (sbc->channel_mode &
          SBC_CHANNEL_MODE_DUAL_CHANNEL) ||
      (sbc->channel_mode & SBC_CHANNEL_MODE_JOINT_STEREO))
    stereo = TRUE;

  if (mono && stereo) {
    g_value_init (value, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (value, 1, 2);
  } else {
    g_value_init (value, G_TYPE_INT);
    if (mono)
      g_value_set_int (value, 1);
    else if (stereo)
      g_value_set_int (value, 2);
    else {
      GST_ERROR_OBJECT (self, "Unexpected number of channels");
      g_value_set_int (value, 0);
    }
  }

  gst_structure_set_value (structure, "channels", value);
  g_free (value);

  return structure;
}

static GstStructure *
gst_avdtp_sink_parse_mpeg_raw (GstAvdtpSink * self)
{
  a2dp_mpeg_t *mpeg = (a2dp_mpeg_t *) self->data->config;
  GstStructure *structure;
  GValue *value;
  GValue *list;
  gboolean valid_layer = FALSE;
  gboolean mono, stereo;

  GST_LOG_OBJECT (self, "parsing mpeg caps");

  structure = gst_structure_empty_new ("audio/mpeg");
  value = g_new0 (GValue, 1);
  g_value_init (value, G_TYPE_INT);

  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  g_value_set_int (value, 1);
  gst_value_list_prepend_value (list, value);
  g_value_set_int (value, 2);
  gst_value_list_prepend_value (list, value);
  gst_structure_set_value (structure, "mpegversion", list);
  g_free (list);

  /* layer */
  GST_LOG_OBJECT (self, "setting mpeg layer");
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (mpeg->layer & MPEG_LAYER_MP1) {
    g_value_set_int (value, 1);
    gst_value_list_prepend_value (list, value);
    valid_layer = TRUE;
  }
  if (mpeg->layer & MPEG_LAYER_MP2) {
    g_value_set_int (value, 2);
    gst_value_list_prepend_value (list, value);
    valid_layer = TRUE;
  }
  if (mpeg->layer & MPEG_LAYER_MP3) {
    g_value_set_int (value, 3);
    gst_value_list_prepend_value (list, value);
    valid_layer = TRUE;
  }
  if (list) {
    gst_structure_set_value (structure, "layer", list);
    g_free (list);
    list = NULL;
  }

  if (!valid_layer) {
    gst_structure_free (structure);
    g_free (value);
    return NULL;
  }

  /* rate */
  GST_LOG_OBJECT (self, "setting mpeg rate");
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_48000) {
    g_value_set_int (value, 48000);
    gst_value_list_prepend_value (list, value);
  }
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_44100) {
    g_value_set_int (value, 44100);
    gst_value_list_prepend_value (list, value);
  }
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_32000) {
    g_value_set_int (value, 32000);
    gst_value_list_prepend_value (list, value);
  }
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_24000) {
    g_value_set_int (value, 24000);
    gst_value_list_prepend_value (list, value);
  }
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_22050) {
    g_value_set_int (value, 22050);
    gst_value_list_prepend_value (list, value);
  }
  if (mpeg->frequency & MPEG_SAMPLING_FREQ_16000) {
    g_value_set_int (value, 16000);
    gst_value_list_prepend_value (list, value);
  }
  g_value_unset (value);
  if (list) {
    gst_structure_set_value (structure, "rate", list);
    g_free (list);
    list = NULL;
  }

  /* channels */
  GST_LOG_OBJECT (self, "setting mpeg channels");
  mono = FALSE;
  stereo = FALSE;
  if (mpeg->channel_mode & MPEG_CHANNEL_MODE_MONO)
    mono = TRUE;
  if ((mpeg->channel_mode & MPEG_CHANNEL_MODE_STEREO) ||
      (mpeg->channel_mode &
          MPEG_CHANNEL_MODE_DUAL_CHANNEL) ||
      (mpeg->channel_mode & MPEG_CHANNEL_MODE_JOINT_STEREO))
    stereo = TRUE;

  if (mono && stereo) {
    g_value_init (value, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (value, 1, 2);
  } else {
    g_value_init (value, G_TYPE_INT);
    if (mono)
      g_value_set_int (value, 1);
    else if (stereo)
      g_value_set_int (value, 2);
    else {
      GST_ERROR_OBJECT (self, "Unexpected number of channels");
      g_value_set_int (value, 0);
    }
  }
  gst_structure_set_value (structure, "channels", value);
  g_free (value);

  return structure;
}

static gboolean
gst_avdtp_sink_update_config (GstAvdtpSink * self)
{
  GstStructure *structure;
  gchar *tmp;

  switch (self->data->codec) {
    case A2DP_CODEC_SBC:
      structure = gst_avdtp_sink_parse_sbc_raw (self);
      break;
    case A2DP_CODEC_MPEG12:
      structure = gst_avdtp_sink_parse_mpeg_raw (self);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported configuration");
      return FALSE;
  }

  if (structure == NULL)
    return FALSE;

  if (self->dev_caps != NULL)
    gst_caps_unref (self->dev_caps);

  self->dev_caps = gst_caps_new_full (structure, NULL);

  tmp = gst_caps_to_string (self->dev_caps);
  GST_DEBUG_OBJECT (self, "Transport configuration: %s", tmp);
  g_free (tmp);

  return TRUE;
}

static gboolean
gst_avdtp_sink_update_caps (GstAvdtpSink * self)
{
  GST_LOG_OBJECT (self, "updating device caps");

  if (self->data->config_size == 0 || self->data->config == NULL)
    return FALSE;

  return gst_avdtp_sink_update_config (self);
}

static gint
gst_avdtp_sink_get_channel_mode (const gchar * mode)
{
  if (strcmp (mode, "stereo") == 0)
    return SBC_CHANNEL_MODE_STEREO;
  else if (strcmp (mode, "joint-stereo") == 0)
    return SBC_CHANNEL_MODE_JOINT_STEREO;
  else if (strcmp (mode, "dual-channel") == 0)
    return SBC_CHANNEL_MODE_DUAL_CHANNEL;
  else if (strcmp (mode, "mono") == 0)
    return SBC_CHANNEL_MODE_MONO;
  else
    return -1;
}

static void
gst_avdtp_sink_tag (const GstTagList * taglist,
    const gchar * tag, gpointer user_data)
{
  gboolean crc;
  gchar *channel_mode = NULL;
  GstAvdtpSink *self = GST_AVDTP_SINK (user_data);

  if (strcmp (tag, "has-crc") == 0) {

    if (!gst_tag_list_get_boolean (taglist, tag, &crc)) {
      GST_WARNING_OBJECT (self, "failed to get crc tag");
      return;
    }

    gst_avdtp_sink_set_crc (self, crc);

  } else if (strcmp (tag, "channel-mode") == 0) {

    if (!gst_tag_list_get_string (taglist, tag, &channel_mode)) {
      GST_WARNING_OBJECT (self, "failed to get channel-mode tag");
      return;
    }

    self->channel_mode = gst_avdtp_sink_get_channel_mode (channel_mode);
    if (self->channel_mode == -1)
      GST_WARNING_OBJECT (self, "Received invalid channel "
          "mode: %s", channel_mode);
    g_free (channel_mode);

  } else
    GST_DEBUG_OBJECT (self, "received unused tag: %s", tag);
}

static gboolean
gst_avdtp_sink_event (GstBaseSink * basesink, GstEvent * event)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);
  GstTagList *taglist = NULL;

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
    /* we check the tags, mp3 has tags that are importants and
     * are outside caps */
    gst_event_parse_tag (event, &taglist);
    gst_tag_list_foreach (taglist, gst_avdtp_sink_tag, self);
  }

  return TRUE;
}

static gboolean
gst_avdtp_sink_transport_parse_property (GstAvdtpSink * self,
    DBusMessageIter * i)
{
  const char *key;
  DBusMessageIter variant_i;

  if (dbus_message_iter_get_arg_type (i) != DBUS_TYPE_STRING) {
    GST_ERROR_OBJECT (self, "Property name not a string.");
    return FALSE;
  }

  dbus_message_iter_get_basic (i, &key);

  if (!dbus_message_iter_next (i)) {
    GST_ERROR_OBJECT (self, "Property value missing");
    return FALSE;
  }

  if (dbus_message_iter_get_arg_type (i) != DBUS_TYPE_VARIANT) {
    GST_ERROR_OBJECT (self, "Property value not a variant.");
    return FALSE;
  }

  dbus_message_iter_recurse (i, &variant_i);

  switch (dbus_message_iter_get_arg_type (&variant_i)) {
    case DBUS_TYPE_BYTE:{
      uint8_t value;
      dbus_message_iter_get_basic (&variant_i, &value);

      if (g_str_equal (key, "Codec") == TRUE)
        self->data->codec = value;

      break;
    }
    case DBUS_TYPE_STRING:{
      const char *value;
      dbus_message_iter_get_basic (&variant_i, &value);

      if (g_str_equal (key, "UUID") == TRUE) {
        g_free (self->data->uuid);
        self->data->uuid = g_strdup (value);
      }

      break;
    }
    case DBUS_TYPE_ARRAY:{
      DBusMessageIter array_i;
      char *value;
      int size;

      dbus_message_iter_recurse (&variant_i, &array_i);
      dbus_message_iter_get_fixed_array (&array_i, &value, &size);

      if (g_str_equal (key, "Configuration")) {
        g_free (self->data->config);
        self->data->config = g_new0 (guint8, size);
        self->data->config_size = size;
        memcpy (self->data->config, value, size);
      }

      break;
    }
  }

  return TRUE;
}

static gboolean
gst_avdtp_sink_transport_acquire (GstAvdtpSink * self)
{
  DBusMessage *msg, *reply;
  DBusError err;
  const char *access_type = "w";
  int fd;
  uint16_t imtu, omtu;

  dbus_error_init (&err);

  if (self->data->conn == NULL)
    self->data->conn = dbus_bus_get (DBUS_BUS_SYSTEM, &err);

  msg = dbus_message_new_method_call ("org.bluez", self->transport,
      "org.bluez.MediaTransport", "Acquire");

  dbus_message_append_args (msg, DBUS_TYPE_STRING, &access_type,
      DBUS_TYPE_INVALID);

  reply = dbus_connection_send_with_reply_and_block (self->data->conn,
      msg, -1, &err);

  dbus_message_unref (msg);

  if (dbus_error_is_set (&err))
    goto fail;

  if (dbus_message_get_args (reply, &err, DBUS_TYPE_UNIX_FD, &fd,
          DBUS_TYPE_UINT16, &imtu,
          DBUS_TYPE_UINT16, &omtu, DBUS_TYPE_INVALID) == FALSE)
    goto fail;

  dbus_message_unref (reply);

  self->stream = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (self->stream, NULL, NULL);
  g_io_channel_set_close_on_unref (self->stream, TRUE);
  self->data->link_mtu = omtu;
  GST_DEBUG_OBJECT (self, "stream_fd=%d mtu=%d", fd, omtu);

  return TRUE;

fail:
  GST_ERROR_OBJECT (self, "Failed to acquire transport stream: %s",
      err.message);

  dbus_error_free (&err);

  if (reply)
    dbus_message_unref (reply);

  return FALSE;
}

static gboolean
gst_avdtp_sink_transport_get_properties (GstAvdtpSink * self)
{
  DBusMessage *msg, *reply;
  DBusMessageIter arg_i, ele_i;
  DBusError err;

  dbus_error_init (&err);

  /* Transport need to be acquire first to make sure the MTUs are
     available */
  if (gst_avdtp_sink_transport_acquire (self) == FALSE)
    return FALSE;

  msg = dbus_message_new_method_call ("org.bluez", self->transport,
      "org.bluez.MediaTransport", "GetProperties");
  if (!msg) {
    GST_ERROR_OBJECT (self, "D-Bus Memory allocation failed");
    return FALSE;
  }

  reply = dbus_connection_send_with_reply_and_block (self->data->conn,
      msg, -1, &err);
  dbus_message_unref (msg);

  if (dbus_error_is_set (&err)) {
    GST_ERROR_OBJECT (self, "GetProperties failed: %s", err.message);
    dbus_error_free (&err);
    return FALSE;
  }

  if (!dbus_message_iter_init (reply, &arg_i)) {
    GST_ERROR_OBJECT (self, "GetProperties reply has no arguments.");
    goto fail;
  }

  if (dbus_message_iter_get_arg_type (&arg_i) != DBUS_TYPE_ARRAY) {
    GST_ERROR_OBJECT (self, "GetProperties argument is not an array.");
    goto fail;
  }

  dbus_message_iter_recurse (&arg_i, &ele_i);
  while (dbus_message_iter_get_arg_type (&ele_i) != DBUS_TYPE_INVALID) {

    if (dbus_message_iter_get_arg_type (&ele_i) == DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter dict_i;

      dbus_message_iter_recurse (&ele_i, &dict_i);

      gst_avdtp_sink_transport_parse_property (self, &dict_i);
    }

    if (!dbus_message_iter_next (&ele_i))
      break;
  }

  return gst_avdtp_sink_update_caps (self);

fail:
  dbus_message_unref (reply);
  return FALSE;

}

static gboolean
gst_avdtp_sink_start (GstBaseSink * basesink)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);

  GST_INFO_OBJECT (self, "start");

  self->data = g_new0 (struct bluetooth_data, 1);

  self->stream = NULL;
  self->stream_caps = NULL;
  self->mp3_using_crc = -1;
  self->channel_mode = -1;

  if (self->transport == NULL)
    return FALSE;

  return gst_avdtp_sink_transport_get_properties (self);
}

static gboolean
gst_avdtp_sink_conf_recv_stream_fd (GstAvdtpSink * self)
{
  struct bluetooth_data *data = self->data;
  GError *gerr = NULL;
  GIOStatus status;
  GIOFlags flags;
  int fd;

  /* Proceed if stream was already acquired */
  if (self->stream == NULL) {
    GST_ERROR_OBJECT (self, "Error while configuring device: "
        "could not acquire audio socket");
    return FALSE;
  }

  /* set stream socket to nonblock */
  GST_LOG_OBJECT (self, "setting stream socket to nonblock");
  flags = g_io_channel_get_flags (self->stream);
  flags |= G_IO_FLAG_NONBLOCK;
  status = g_io_channel_set_flags (self->stream, flags, &gerr);
  if (status != G_IO_STATUS_NORMAL) {
    if (gerr)
      GST_WARNING_OBJECT (self, "Error while "
          "setting server socket to nonblock: " "%s", gerr->message);
    else
      GST_WARNING_OBJECT (self, "Error while "
          "setting server " "socket to nonblock");
  }

  fd = g_io_channel_unix_get_fd (self->stream);

  /* It is possible there is some outstanding
     data in the pipe - we have to empty it */
  GST_LOG_OBJECT (self, "emptying stream pipe");
  while (1) {
    ssize_t bread = read (fd, data->buffer, data->link_mtu);
    if (bread <= 0)
      break;
  }

  /* set stream socket to block */
  GST_LOG_OBJECT (self, "setting stream socket to block");
  flags = g_io_channel_get_flags (self->stream);
  flags &= ~G_IO_FLAG_NONBLOCK;
  status = g_io_channel_set_flags (self->stream, flags, &gerr);
  if (status != G_IO_STATUS_NORMAL) {
    if (gerr)
      GST_WARNING_OBJECT (self, "Error while "
          "setting server socket to block:" "%s", gerr->message);
    else
      GST_WARNING_OBJECT (self, "Error while "
          "setting server " "socket to block");
  }

  memset (data->buffer, 0, sizeof (data->buffer));

  return TRUE;
}

static GstFlowReturn
gst_avdtp_sink_preroll (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstAvdtpSink *sink = GST_AVDTP_SINK (basesink);
  gboolean ret;

  GST_AVDTP_SINK_MUTEX_LOCK (sink);

  ret = gst_avdtp_sink_conf_recv_stream_fd (sink);

  GST_AVDTP_SINK_MUTEX_UNLOCK (sink);

  if (!ret)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_avdtp_sink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);
  ssize_t ret;
  int fd;

  fd = g_io_channel_unix_get_fd (self->stream);

  ret = write (fd, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "Error while writting to socket: %s",
        strerror (errno));
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_avdtp_sink_unlock (GstBaseSink * basesink)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);

  if (self->stream != NULL)
    g_io_channel_flush (self->stream, NULL);

  return TRUE;
}

static GstFlowReturn
gst_avdtp_sink_buffer_alloc (GstBaseSink * basesink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstAvdtpSink *self = GST_AVDTP_SINK (basesink);

  *buf = gst_buffer_new_and_alloc (size);
  if (!(*buf)) {
    GST_ERROR_OBJECT (self, "buffer allocation failed");
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_caps (*buf, caps);

  GST_BUFFER_OFFSET (*buf) = offset;

  return GST_FLOW_OK;
}

static void
gst_avdtp_sink_class_init (GstAvdtpSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_avdtp_sink_finalize);
  object_class->set_property = GST_DEBUG_FUNCPTR (gst_avdtp_sink_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_avdtp_sink_get_property);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_avdtp_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_avdtp_sink_stop);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_avdtp_sink_render);
  basesink_class->preroll = GST_DEBUG_FUNCPTR (gst_avdtp_sink_preroll);
  basesink_class->unlock = GST_DEBUG_FUNCPTR (gst_avdtp_sink_unlock);
  basesink_class->event = GST_DEBUG_FUNCPTR (gst_avdtp_sink_event);

  basesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_avdtp_sink_buffer_alloc);

  g_object_class_install_property (object_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Bluetooth remote device address", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_AUTOCONNECT,
      g_param_spec_boolean ("auto-connect",
          "Auto-connect",
          "Automatically attempt to connect "
          "to device", DEFAULT_AUTOCONNECT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_TRANSPORT,
      g_param_spec_string ("transport",
          "Transport", "Use configured transport", NULL, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (avdtp_sink_debug, "avdtpsink", 0,
      "A2DP headset sink element");
}

static void
gst_avdtp_sink_init (GstAvdtpSink * self, GstAvdtpSinkClass * klass)
{
  self->device = NULL;
  self->transport = NULL;
  self->data = NULL;

  self->stream = NULL;

  self->dev_caps = NULL;

  self->autoconnect = DEFAULT_AUTOCONNECT;

  self->sink_lock = g_mutex_new ();

  /* FIXME this is for not synchronizing with clock, should be tested
   * with devices to see the behaviour
   gst_base_sink_set_sync(GST_BASE_SINK(self), FALSE);
   */
}

gboolean
gst_avdtp_sink_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avdtpsink", GST_RANK_NONE,
      GST_TYPE_AVDTP_SINK);
}


/* public functions */
GstCaps *
gst_avdtp_sink_get_device_caps (GstAvdtpSink * sink)
{
  if (sink->dev_caps == NULL)
    return NULL;

  return gst_caps_copy (sink->dev_caps);
}

gboolean
gst_avdtp_sink_set_device_caps (GstAvdtpSink * self, GstCaps * caps)
{
  GST_DEBUG_OBJECT (self, "setting device caps");
  GST_AVDTP_SINK_MUTEX_LOCK (self);

  if (self->stream_caps)
    gst_caps_unref (self->stream_caps);
  self->stream_caps = gst_caps_ref (caps);

  GST_AVDTP_SINK_MUTEX_UNLOCK (self);

  return TRUE;
}

guint
gst_avdtp_sink_get_link_mtu (GstAvdtpSink * sink)
{
  return sink->data->link_mtu;
}

void
gst_avdtp_sink_set_device (GstAvdtpSink * self, const gchar * dev)
{
  if (self->device != NULL)
    g_free (self->device);

  GST_LOG_OBJECT (self, "Setting device: %s", dev);
  self->device = g_strdup (dev);
}

void
gst_avdtp_sink_set_transport (GstAvdtpSink * self, const gchar * trans)
{
  if (self->transport != NULL)
    g_free (self->transport);

  GST_LOG_OBJECT (self, "Setting transport: %s", trans);
  self->transport = g_strdup (trans);
}

gchar *
gst_avdtp_sink_get_device (GstAvdtpSink * self)
{
  return g_strdup (self->device);
}

gchar *
gst_avdtp_sink_get_transport (GstAvdtpSink * self)
{
  return g_strdup (self->transport);
}

void
gst_avdtp_sink_set_crc (GstAvdtpSink * self, gboolean crc)
{
  gint new_crc;

  new_crc = crc ? CRC_PROTECTED : CRC_UNPROTECTED;

  /* test if we already received a different crc */
  if (self->mp3_using_crc != -1 && new_crc != self->mp3_using_crc) {
    GST_WARNING_OBJECT (self, "crc changed during stream");
    return;
  }
  self->mp3_using_crc = new_crc;

}

void
gst_avdtp_sink_set_channel_mode (GstAvdtpSink * self, const gchar * mode)
{
  gint new_mode;

  new_mode = gst_avdtp_sink_get_channel_mode (mode);

  if (self->channel_mode != -1 && new_mode != self->channel_mode) {
    GST_WARNING_OBJECT (self, "channel mode changed during stream");
    return;
  }

  self->channel_mode = new_mode;
  if (self->channel_mode == -1)
    GST_WARNING_OBJECT (self, "Received invalid channel " "mode: %s", mode);
}
