/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *  Copyright (C) 2012 Collabora Ltd.
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

#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include "a2dp-codecs.h"

#include <gst/gst.h>
#include "gstavdtputil.h"

#define TEMPLATE_MAX_BITPOOL 64

GST_DEBUG_CATEGORY_EXTERN (avdtp_debug);
#define GST_CAT_DEFAULT avdtp_debug

gboolean
gst_avdtp_connection_acquire (GstAvdtpConnection * conn)
{
  DBusMessage *msg, *reply;
  DBusError err;
  const char *access_type = "rw";
  int fd;
  uint16_t imtu, omtu;

  dbus_error_init (&err);

  if (conn->transport == NULL) {
    GST_ERROR ("No transport specified");
    return FALSE;
  }

  if (conn->data.conn == NULL)
    conn->data.conn = dbus_bus_get (DBUS_BUS_SYSTEM, &err);

  msg = dbus_message_new_method_call ("org.bluez", conn->transport,
      "org.bluez.MediaTransport", "Acquire");

  dbus_message_append_args (msg, DBUS_TYPE_STRING, &access_type,
      DBUS_TYPE_INVALID);

  reply = dbus_connection_send_with_reply_and_block (conn->data.conn,
      msg, -1, &err);

  dbus_message_unref (msg);

  if (dbus_error_is_set (&err))
    goto fail;

  if (dbus_message_get_args (reply, &err, DBUS_TYPE_UNIX_FD, &fd,
          DBUS_TYPE_UINT16, &imtu,
          DBUS_TYPE_UINT16, &omtu, DBUS_TYPE_INVALID) == FALSE)
    goto fail;

  dbus_message_unref (reply);

  conn->stream = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (conn->stream, NULL, NULL);
  g_io_channel_set_close_on_unref (conn->stream, TRUE);
  conn->data.link_mtu = omtu;

  return TRUE;

fail:
  GST_ERROR ("Failed to acquire transport stream: %s", err.message);

  dbus_error_free (&err);

  if (reply)
    dbus_message_unref (reply);

  return FALSE;
}

static void
gst_avdtp_connection_transport_release (GstAvdtpConnection * conn)
{
  DBusMessage *msg;
  const char *access_type = "rw";

  msg = dbus_message_new_method_call ("org.bluez", conn->transport,
      "org.bluez.MediaTransport", "Release");

  dbus_message_append_args (msg, DBUS_TYPE_STRING, &access_type,
      DBUS_TYPE_INVALID);

  dbus_connection_send (conn->data.conn, msg, NULL);

  dbus_message_unref (msg);
}

void
gst_avdtp_connection_release (GstAvdtpConnection * conn)
{
  if (conn->stream) {
    g_io_channel_shutdown (conn->stream, TRUE, NULL);
    g_io_channel_unref (conn->stream);
    conn->stream = NULL;
  }

  if (conn->data.uuid) {
    g_free (conn->data.uuid);
    conn->data.uuid = NULL;
  }

  if (conn->data.config) {
    g_free (conn->data.config);
    conn->data.config = NULL;
  }

  if (conn->data.conn) {
    if (conn->transport)
      gst_avdtp_connection_transport_release (conn);

    dbus_connection_unref (conn->data.conn);

    conn->data.conn = NULL;
  }
}

void
gst_avdtp_connection_reset (GstAvdtpConnection * conn)
{
  gst_avdtp_connection_release (conn);

  if (conn->device) {
    g_free (conn->device);
    conn->device = NULL;
  }

  if (conn->transport) {
    g_free (conn->transport);
    conn->transport = NULL;
  }
}

void
gst_avdtp_connection_set_device (GstAvdtpConnection * conn, const char *device)
{
  if (conn->device)
    g_free (conn->device);

  conn->device = g_strdup (device);
}

void
gst_avdtp_connection_set_transport (GstAvdtpConnection * conn,
    const char *transport)
{
  if (conn->transport)
    g_free (conn->transport);

  conn->transport = g_strdup (transport);
}

static gboolean
gst_avdtp_connection_parse_property (GstAvdtpConnection * conn,
    DBusMessageIter * i)
{
  const char *key;
  DBusMessageIter variant_i;

  if (dbus_message_iter_get_arg_type (i) != DBUS_TYPE_STRING) {
    GST_ERROR ("Property name not a string.");
    return FALSE;
  }

  dbus_message_iter_get_basic (i, &key);

  if (!dbus_message_iter_next (i)) {
    GST_ERROR ("Property value missing");
    return FALSE;
  }

  if (dbus_message_iter_get_arg_type (i) != DBUS_TYPE_VARIANT) {
    GST_ERROR ("Property value not a variant.");
    return FALSE;
  }

  dbus_message_iter_recurse (i, &variant_i);

  switch (dbus_message_iter_get_arg_type (&variant_i)) {
    case DBUS_TYPE_BYTE:{
      uint8_t value;
      dbus_message_iter_get_basic (&variant_i, &value);

      if (g_str_equal (key, "Codec") == TRUE)
        conn->data.codec = value;

      break;
    }
    case DBUS_TYPE_STRING:{
      const char *value;
      dbus_message_iter_get_basic (&variant_i, &value);

      if (g_str_equal (key, "UUID") == TRUE) {
        g_free (conn->data.uuid);
        conn->data.uuid = g_strdup (value);
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
        g_free (conn->data.config);
        conn->data.config = g_new0 (guint8, size);
        conn->data.config_size = size;
        memcpy (conn->data.config, value, size);
      }

      break;
    }
  }

  return TRUE;
}

gboolean
gst_avdtp_connection_get_properties (GstAvdtpConnection * conn)
{
  DBusMessage *msg, *reply;
  DBusMessageIter arg_i, ele_i;
  DBusError err;

  dbus_error_init (&err);

  msg = dbus_message_new_method_call ("org.bluez", conn->transport,
      "org.bluez.MediaTransport", "GetProperties");

  if (!msg) {
    GST_ERROR ("D-Bus Memory allocation failed");
    return FALSE;
  }

  reply = dbus_connection_send_with_reply_and_block (conn->data.conn,
      msg, -1, &err);

  dbus_message_unref (msg);

  if (dbus_error_is_set (&err)) {
    GST_ERROR ("GetProperties failed: %s", err.message);
    dbus_error_free (&err);
    return FALSE;
  }

  if (!dbus_message_iter_init (reply, &arg_i)) {
    GST_ERROR ("GetProperties reply has no arguments.");
    goto fail;
  }

  if (dbus_message_iter_get_arg_type (&arg_i) != DBUS_TYPE_ARRAY) {
    GST_ERROR ("GetProperties argument is not an array.");
    goto fail;
  }

  dbus_message_iter_recurse (&arg_i, &ele_i);
  while (dbus_message_iter_get_arg_type (&ele_i) != DBUS_TYPE_INVALID) {

    if (dbus_message_iter_get_arg_type (&ele_i) == DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter dict_i;

      dbus_message_iter_recurse (&ele_i, &dict_i);

      gst_avdtp_connection_parse_property (conn, &dict_i);
    }

    if (!dbus_message_iter_next (&ele_i))
      break;
  }

  return TRUE;

fail:
  dbus_message_unref (reply);
  return FALSE;

}

static GstStructure *
gst_avdtp_util_parse_sbc_raw (void *config)
{
  a2dp_sbc_t *sbc = (a2dp_sbc_t *) config;
  GstStructure *structure;
  GValue *value;
  GValue *list;
  gboolean mono, stereo;

  structure = gst_structure_new_empty ("audio/x-sbc");
  value = g_value_init (g_new0 (GValue, 1), G_TYPE_STRING);
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);

  /* mode */
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
  if (gst_value_list_get_size (list) == 1)
    gst_structure_set_value (structure, "channel-mode", value);
  else
    gst_structure_take_value (structure, "channel-mode", list);

  g_value_unset (value);
  g_value_reset (list);

  /* subbands */
  value = g_value_init (value, G_TYPE_INT);
  if (sbc->subbands & SBC_SUBBANDS_4) {
    g_value_set_int (value, 4);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->subbands & SBC_SUBBANDS_8) {
    g_value_set_int (value, 8);
    gst_value_list_prepend_value (list, value);
  }
  if (gst_value_list_get_size (list) == 1)
    gst_structure_set_value (structure, "subbands", value);
  else
    gst_structure_take_value (structure, "subbands", list);

  g_value_unset (value);
  g_value_reset (list);

  /* blocks */
  value = g_value_init (value, G_TYPE_INT);
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
  if (gst_value_list_get_size (list) == 1)
    gst_structure_set_value (structure, "blocks", value);
  else
    gst_structure_take_value (structure, "blocks", list);

  g_value_unset (value);
  g_value_reset (list);

  /* allocation */
  g_value_init (value, G_TYPE_STRING);
  if (sbc->allocation_method & SBC_ALLOCATION_LOUDNESS) {
    g_value_set_static_string (value, "loudness");
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->allocation_method & SBC_ALLOCATION_SNR) {
    g_value_set_static_string (value, "snr");
    gst_value_list_prepend_value (list, value);
  }
  if (gst_value_list_get_size (list) == 1)
    gst_structure_set_value (structure, "allocation-method", value);
  else
    gst_structure_take_value (structure, "allocation-method", list);

  g_value_unset (value);
  g_value_reset (list);

  /* rate */
  g_value_init (value, G_TYPE_INT);
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
  if (gst_value_list_get_size (list) == 1)
    gst_structure_set_value (structure, "rate", value);
  else
    gst_structure_take_value (structure, "rate", list);

  g_value_unset (value);
  g_value_reset (list);

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
      GST_ERROR ("Unexpected number of channels");
      g_value_set_int (value, 0);
    }
  }

  gst_structure_set_value (structure, "channels", value);

  g_value_unset (value);
  g_free (value);
  g_value_unset (list);
  g_free (list);

  return structure;
}

static GstStructure *
gst_avdtp_util_parse_mpeg_raw (void *config)
{
  a2dp_mpeg_t *mpeg = (a2dp_mpeg_t *) config;
  GstStructure *structure;
  GValue *value;
  GValue *list;
  gboolean valid_layer = FALSE;
  gboolean mono, stereo;

  structure = gst_structure_new_empty ("audio/mpeg");
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
    if (gst_value_list_get_size (list) == 1)
      gst_structure_set_value (structure, "layer", value);
    else
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
    if (gst_value_list_get_size (list) == 1)
      gst_structure_set_value (structure, "rate", value);
    else
      gst_structure_set_value (structure, "rate", list);
    g_free (list);
    list = NULL;
  }

  /* channels */
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
      GST_ERROR ("Unexpected number of channels");
      g_value_set_int (value, 0);
    }
  }
  gst_structure_set_value (structure, "channels", value);
  g_free (value);

  return structure;
}

static GstStructure *
gst_avdtp_util_parse_aac_raw (void *config)
{
  GstStructure *structure;
  GValue value = G_VALUE_INIT;
  GValue value_str = G_VALUE_INIT;
  GValue list = G_VALUE_INIT;
  a2dp_aac_t aac_local = { 0 };
  a2dp_aac_t *aac = &aac_local;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  uint8_t *raw = (uint8_t *) config;
  aac->object_type = raw[0];
  aac->frequency = (raw[1] << 4) | ((raw[2] & 0xFF) >> 4);
  aac->channels = (raw[2] >> 2) & 0x3;
  aac->rfa = raw[2] & 0x3;
  aac->vbr = (raw[3] >> 7) & 0x1;
  aac->bitrate = (raw[4] << 16) | (raw[3] << 8) | raw[4];
  aac->bitrate &= ~0x800000;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  *aac = *((a2dp_aac_t *) config);
#else
#error "Unknown byte order"
#endif

  GST_LOG ("aac objtype=%x freq=%x rfa=%x channels=%x vbr=%x bitrate=%x",
      aac->object_type, aac->frequency, aac->rfa, aac->channels, aac->vbr,
      aac->bitrate);

  structure = gst_structure_new_empty ("audio/mpeg");
  g_value_init (&value, G_TYPE_INT);
  g_value_init (&value_str, G_TYPE_STRING);

  /* mpegversion */
  g_value_init (&list, GST_TYPE_LIST);
  if (aac->object_type & AAC_OBJECT_TYPE_MPEG2_AAC_LC) {
    g_value_set_int (&value, 2);
    gst_value_list_prepend_value (&list, &value);
  }
  if ((aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_LC)
      || (aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_LTP)
      || (aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_SCALABLE)) {
    g_value_set_int (&value, 4);
    gst_value_list_prepend_value (&list, &value);
  }
  if (gst_value_list_get_size (&list) == 1)
    gst_structure_set_value (structure, "mpegversion", &value);
  else
    gst_structure_set_value (structure, "mpegversion", &list);

  g_value_reset (&list);

  /* base-profile */
  if (aac->object_type & AAC_OBJECT_TYPE_MPEG2_AAC_LC
      || aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_LC) {
    g_value_set_string (&value_str, "lc");
    gst_value_list_prepend_value (&list, &value_str);
  }
  if (aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_LTP) {
    g_value_set_string (&value_str, "ltp");
    gst_value_list_prepend_value (&list, &value_str);
  }
  if (aac->object_type & AAC_OBJECT_TYPE_MPEG4_AAC_SCALABLE) {
    g_value_set_string (&value_str, "ssr");
    gst_value_list_prepend_value (&list, &value_str);
  }
  if (gst_value_list_get_size (&list) == 1)
    gst_structure_set_value (structure, "base-profile", &value_str);
  else
    gst_structure_set_value (structure, "base-profile", &list);

  g_value_reset (&list);

  /* rate */
  g_value_init (&list, GST_TYPE_LIST);
  if (aac->frequency & AAC_SAMPLING_FREQ_8000) {
    g_value_set_int (&value, 8000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_11025) {
    g_value_set_int (&value, 11025);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_12000) {
    g_value_set_int (&value, 12000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_16000) {
    g_value_set_int (&value, 16000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_22050) {
    g_value_set_int (&value, 22050);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_24000) {
    g_value_set_int (&value, 24000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_32000) {
    g_value_set_int (&value, 32000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_44100) {
    g_value_set_int (&value, 44100);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_48000) {
    g_value_set_int (&value, 48000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_64000) {
    g_value_set_int (&value, 64000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_88200) {
    g_value_set_int (&value, 88200);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->frequency & AAC_SAMPLING_FREQ_96000) {
    g_value_set_int (&value, 96000);
    gst_value_list_prepend_value (&list, &value);
  }
  if (gst_value_list_get_size (&list) == 1)
    gst_structure_set_value (structure, "rate", &value);
  else
    gst_structure_set_value (structure, "rate", &list);

  g_value_reset (&list);

  /* channels */
  g_value_init (&list, GST_TYPE_LIST);
  if (aac->channels & AAC_CHANNELS_1) {
    g_value_set_int (&value, 1);
    gst_value_list_prepend_value (&list, &value);
  }
  if (aac->channels & AAC_CHANNELS_2) {
    g_value_set_int (&value, 2);
    gst_value_list_prepend_value (&list, &value);
  }
  if (gst_value_list_get_size (&list) == 1)
    gst_structure_set_value (structure, "channels", &value);
  else
    gst_structure_set_value (structure, "channels", &list);

  GST_LOG ("AAC caps: %" GST_PTR_FORMAT, structure);

  g_value_unset (&list);
  g_value_unset (&value);
  g_value_unset (&value_str);

  return structure;
}

GstCaps *
gst_avdtp_connection_get_caps (GstAvdtpConnection * conn)
{
  GstCaps *caps;
  GstStructure *structure;

  if (conn->data.config_size == 0 || conn->data.config == NULL)
    return NULL;

  switch (conn->data.codec) {
    case A2DP_CODEC_SBC:
      structure = gst_avdtp_util_parse_sbc_raw (conn->data.config);
      break;
    case A2DP_CODEC_MPEG12:
      structure = gst_avdtp_util_parse_mpeg_raw (conn->data.config);
      break;
    case A2DP_CODEC_MPEG24:
      structure = gst_avdtp_util_parse_aac_raw (conn->data.config);
      break;
    default:
      GST_ERROR ("Unsupported configuration");
      return NULL;
  }

  if (structure == NULL)
    return FALSE;

  caps = gst_caps_new_full (structure, NULL);

  return caps;
}

gboolean
gst_avdtp_connection_conf_recv_stream_fd (GstAvdtpConnection * conn)
{
  struct bluetooth_data *data = &conn->data;
  GError *gerr = NULL;
  GIOStatus status;
  GIOFlags flags;
  int fd;
  int priority;

  /* Proceed if stream was already acquired */
  if (conn->stream == NULL) {
    GST_ERROR ("Error while configuring device: "
        "could not acquire audio socket");
    return FALSE;
  }

  /* set stream socket to nonblock */
  flags = g_io_channel_get_flags (conn->stream);
  flags |= G_IO_FLAG_NONBLOCK;
  status = g_io_channel_set_flags (conn->stream, flags, &gerr);
  if (status != G_IO_STATUS_NORMAL)
    GST_WARNING ("Error while setting server socket to nonblock");

  fd = g_io_channel_unix_get_fd (conn->stream);

  /* It is possible there is some outstanding
     data in the pipe - we have to empty it */
  while (1) {
    ssize_t bread = read (fd, data->buffer, data->link_mtu);
    if (bread <= 0)
      break;
  }

  /* set stream socket to block */
  flags = g_io_channel_get_flags (conn->stream);
  flags &= ~G_IO_FLAG_NONBLOCK;
  status = g_io_channel_set_flags (conn->stream, flags, &gerr);
  if (status != G_IO_STATUS_NORMAL)
    GST_WARNING ("Error while setting server socket to block");

  priority = 6;
  if (setsockopt (fd, SOL_SOCKET, SO_PRIORITY, (const void *) &priority,
          sizeof (priority)) < 0)
    GST_WARNING ("Unable to set socket to low delay");

  memset (data->buffer, 0, sizeof (data->buffer));

  return TRUE;
}
