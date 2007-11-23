/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
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

#include "ipc.h"
#include "rtp.h"
#include "gstsbcutil.h"

#include "gsta2dpsink.h"

GST_DEBUG_CATEGORY_STATIC (a2dp_sink_debug);
#define GST_CAT_DEFAULT a2dp_sink_debug

#define BUFFER_SIZE 2048

#define GST_A2DP_SINK_MUTEX_LOCK(s) G_STMT_START {	\
		g_mutex_lock (s->sink_lock);		\
	} G_STMT_END

#define GST_A2DP_SINK_MUTEX_UNLOCK(s) G_STMT_START {	\
		g_mutex_unlock (s->sink_lock);		\
	} G_STMT_END


struct bluetooth_data
{
  struct bt_getcapabilities_rsp cfg;    /* Bluetooth device config */
  gint link_mtu;
  int samples;                  /* Number of encoded samples */
  gchar buffer[BUFFER_SIZE];    /* Codec transfer buffer */
  gsize count;                  /* Codec transfer buffer counter */

  int nsamples;                 /* Cumulative number of codec samples */
  uint16_t seq_num;             /* Cumulative packet sequence */
  int frame_count;              /* Current frames in buffer */
};

#define IS_SBC(n) (strcmp((n), "audio/x-sbc") == 0)
#define IS_MPEG(n) (strcmp((n), "audio/mpeg") == 0)

enum
{
  PROP_0,
  PROP_DEVICE,
};

GST_BOILERPLATE (GstA2dpSink, gst_a2dp_sink, GstBaseSink, GST_TYPE_BASE_SINK);

static const GstElementDetails a2dp_sink_details =
GST_ELEMENT_DETAILS ("Bluetooth A2DP sink",
    "Sink/Audio",
    "Plays audio to an A2DP device",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate a2dp_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation = (string) { snr, loudness },"
        "bitpool = (int) [ 2, 64 ]; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]"));

static GIOError gst_a2dp_sink_audioservice_send (GstA2dpSink * self,
    const bt_audio_msg_header_t * msg);
static GIOError gst_a2dp_sink_audioservice_expect (GstA2dpSink * self,
    bt_audio_msg_header_t * outmsg, int expected_type);


static void
gst_a2dp_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&a2dp_sink_factory));

  gst_element_class_set_details (element_class, &a2dp_sink_details);
}

static gboolean
gst_a2dp_sink_stop (GstBaseSink * basesink)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);

  GST_INFO_OBJECT (self, "stop");

  if (self->watch_id != 0) {
    g_source_remove (self->watch_id);
    self->watch_id = 0;
  }

  if (self->stream) {
    g_io_channel_flush (self->stream, NULL);
    g_io_channel_close (self->stream);
    g_io_channel_unref (self->stream);
    self->stream = NULL;
  }

  if (self->server) {
    bt_audio_service_close (g_io_channel_unix_get_fd (self->server));
    g_io_channel_unref (self->server);
    self->server = NULL;
  }

  if (self->data) {
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
gst_a2dp_sink_finalize (GObject * object)
{
  GstA2dpSink *self = GST_A2DP_SINK (object);

  if (self->data)
    gst_a2dp_sink_stop (GST_BASE_SINK (self));

  if (self->device)
    g_free (self->device);

  g_mutex_free (self->sink_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_a2dp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstA2dpSink *sink = GST_A2DP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      if (sink->device)
        g_free (sink->device);
      sink->device = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a2dp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstA2dpSink *sink = GST_A2DP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, sink->device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
gst_a2dp_sink_bluetooth_recvmsg_fd (GstA2dpSink * sink)
{
  int err, ret;

  ret = bt_audio_service_get_data_fd (g_io_channel_unix_get_fd (sink->server));

  if (ret < 0) {
    err = errno;
    GST_ERROR_OBJECT (sink, "Unable to receive fd: %s (%d)",
        strerror (err), err);
    return -err;
  }

  sink->stream = g_io_channel_unix_new (ret);
  GST_DEBUG_OBJECT (sink, "stream_fd=%d", ret);

  return 0;
}

static gboolean
gst_a2dp_sink_init_pkt_conf (GstA2dpSink * sink,
    GstCaps * caps, sbc_capabilities_t * pkt)
{
  sbc_capabilities_t *cfg = &sink->data->cfg.sbc_capabilities;
  const GValue *value = NULL;
  const char *pref, *name;
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  name = gst_structure_get_name (structure);
  /* FIXME only sbc supported here, should suport mp3 */
  if (!(IS_SBC (name))) {
    GST_ERROR_OBJECT (sink, "Unsupported format %s", name);
    return FALSE;
  }

  value = gst_structure_get_value (structure, "rate");
  cfg->frequency = g_value_get_int (value);

  value = gst_structure_get_value (structure, "mode");
  pref = g_value_get_string (value);
  if (strcmp (pref, "auto") == 0)
    cfg->channel_mode = BT_A2DP_CHANNEL_MODE_AUTO;
  else if (strcmp (pref, "mono") == 0)
    cfg->channel_mode = BT_A2DP_CHANNEL_MODE_MONO;
  else if (strcmp (pref, "dual") == 0)
    cfg->channel_mode = BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL;
  else if (strcmp (pref, "stereo") == 0)
    cfg->channel_mode = BT_A2DP_CHANNEL_MODE_STEREO;
  else if (strcmp (pref, "joint") == 0)
    cfg->channel_mode = BT_A2DP_CHANNEL_MODE_JOINT_STEREO;
  else {
    GST_ERROR_OBJECT (sink, "Invalid mode %s", pref);
    return FALSE;
  }

  value = gst_structure_get_value (structure, "allocation");
  pref = g_value_get_string (value);
  if (strcmp (pref, "auto") == 0)
    cfg->allocation_method = BT_A2DP_ALLOCATION_AUTO;
  else if (strcmp (pref, "loudness") == 0)
    cfg->allocation_method = BT_A2DP_ALLOCATION_LOUDNESS;
  else if (strcmp (pref, "snr") == 0)
    cfg->allocation_method = BT_A2DP_ALLOCATION_SNR;
  else {
    GST_ERROR_OBJECT (sink, "Invalid allocation: %s", pref);
    return FALSE;
  }

  value = gst_structure_get_value (structure, "subbands");
  cfg->subbands = g_value_get_int (value);

  value = gst_structure_get_value (structure, "blocks");
  cfg->block_length = g_value_get_int (value);

  /* FIXME min and max ??? */
  value = gst_structure_get_value (structure, "bitpool");
  cfg->max_bitpool = cfg->min_bitpool = g_value_get_int (value);

  memcpy (pkt, cfg, sizeof (*pkt));

  return TRUE;
}

static gboolean
gst_a2dp_sink_conf_recv_stream_fd (GstA2dpSink * self)
{
  struct bluetooth_data *data = self->data;
  gint ret;
  GIOError err;
  GError *gerr = NULL;
  GIOStatus status;
  GIOFlags flags;
  gsize read;

  ret = gst_a2dp_sink_bluetooth_recvmsg_fd (self);
  if (ret < 0)
    return FALSE;

  if (!self->stream) {
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

  /* It is possible there is some outstanding
     data in the pipe - we have to empty it */
  GST_LOG_OBJECT (self, "emptying stream pipe");
  while (1) {
    err = g_io_channel_read (self->stream, data->buffer,
        (gsize) data->cfg.link_mtu, &read);
    if (err != G_IO_ERROR_NONE || read <= 0)
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

static gboolean
server_callback (GIOChannel * chan, GIOCondition cond, gpointer data)
{
  GstA2dpSink *sink;

  if (cond & G_IO_HUP || cond & G_IO_NVAL)
    return FALSE;
  else if (cond & G_IO_ERR) {
    sink = GST_A2DP_SINK (data);
    GST_WARNING_OBJECT (sink, "Untreated callback G_IO_ERR");
  }

  return TRUE;
}

static gboolean
gst_a2dp_sink_update_caps (GstA2dpSink * self)
{
  sbc_capabilities_t *sbc = &self->data->cfg.sbc_capabilities;
  GstStructure *structure;
  GValue *value;
  GValue *list;
  gchar *tmp;

  GST_LOG_OBJECT (self, "updating device caps");

  structure = gst_structure_empty_new ("audio/x-sbc");
  value = g_value_init (g_new0 (GValue, 1), G_TYPE_STRING);

  /* mode */
  list = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);
  if (sbc->channel_mode == BT_A2DP_CHANNEL_MODE_AUTO) {
    g_value_set_static_string (value, "joint");
    gst_value_list_prepend_value (list, value);
    g_value_set_static_string (value, "stereo");
    gst_value_list_prepend_value (list, value);
    g_value_set_static_string (value, "mono");
    gst_value_list_prepend_value (list, value);
    g_value_set_static_string (value, "dual");
    gst_value_list_prepend_value (list, value);
  } else {
    if (sbc->channel_mode & BT_A2DP_CHANNEL_MODE_MONO) {
      g_value_set_static_string (value, "mono");
      gst_value_list_prepend_value (list, value);
    }
    if (sbc->channel_mode & BT_A2DP_CHANNEL_MODE_STEREO) {
      g_value_set_static_string (value, "stereo");
      gst_value_list_prepend_value (list, value);
    }
    if (sbc->channel_mode & BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL) {
      g_value_set_static_string (value, "dual");
      gst_value_list_prepend_value (list, value);
    }
    if (sbc->channel_mode & BT_A2DP_CHANNEL_MODE_JOINT_STEREO) {
      g_value_set_static_string (value, "joint");
      gst_value_list_prepend_value (list, value);
    }
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
  if (sbc->subbands & BT_A2DP_SUBBANDS_4) {
    g_value_set_int (value, 4);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->subbands & BT_A2DP_SUBBANDS_8) {
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
  if (sbc->block_length & BT_A2DP_BLOCK_LENGTH_16) {
    g_value_set_int (value, 16);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & BT_A2DP_BLOCK_LENGTH_12) {
    g_value_set_int (value, 12);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & BT_A2DP_BLOCK_LENGTH_8) {
    g_value_set_int (value, 8);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->block_length & BT_A2DP_BLOCK_LENGTH_4) {
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
  if (sbc->allocation_method == BT_A2DP_ALLOCATION_AUTO) {
    g_value_set_static_string (value, "loudness");
    gst_value_list_prepend_value (list, value);
    g_value_set_static_string (value, "snr");
    gst_value_list_prepend_value (list, value);
  } else {
    if (sbc->allocation_method & BT_A2DP_ALLOCATION_LOUDNESS) {
      g_value_set_static_string (value, "loudness");
      gst_value_list_prepend_value (list, value);
    }
    if (sbc->allocation_method & BT_A2DP_ALLOCATION_SNR) {
      g_value_set_static_string (value, "snr");
      gst_value_list_prepend_value (list, value);
    }
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
  if (sbc->frequency & BT_A2DP_SAMPLING_FREQ_48000) {
    g_value_set_int (value, 48000);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & BT_A2DP_SAMPLING_FREQ_44100) {
    g_value_set_int (value, 44100);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & BT_A2DP_SAMPLING_FREQ_32000) {
    g_value_set_int (value, 32000);
    gst_value_list_prepend_value (list, value);
  }
  if (sbc->frequency & BT_A2DP_SAMPLING_FREQ_16000) {
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
  gst_value_set_int_range (value, sbc->min_bitpool, sbc->max_bitpool);
  gst_structure_set_value (structure, "bitpool", value);

  /* channels */
  gst_value_set_int_range (value, 1, 2);
  gst_structure_set_value (structure, "channels", value);

  g_free (value);

  self->dev_caps = gst_caps_new_full (structure, NULL);

  tmp = gst_caps_to_string (self->dev_caps);
  GST_DEBUG_OBJECT (self, "Device capabilities: %s", tmp);
  g_free (tmp);

  return TRUE;
}

static gboolean
gst_a2dp_sink_get_capabilities (GstA2dpSink * self)
{
  gchar *buf[BT_AUDIO_IPC_PACKET_SIZE];
  struct bt_getcapabilities_req *req = (void *) buf;
  struct bt_getcapabilities_rsp *rsp = (void *) buf;
  GIOError io_error;

  memset (req, 0, BT_AUDIO_IPC_PACKET_SIZE);

  req->h.msg_type = BT_GETCAPABILITIES_REQ;
  strncpy (req->device, self->device, 18);

  req->transport = BT_CAPABILITIES_TRANSPORT_A2DP;
  req->access_mode = BT_CAPABILITIES_ACCESS_MODE_WRITE;
  io_error = gst_a2dp_sink_audioservice_send (self, &req->h);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while asking device caps");
  }

  io_error = gst_a2dp_sink_audioservice_expect (self, &rsp->h,
      BT_GETCAPABILITIES_RSP);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while getting device caps");
    return FALSE;
  }

  if (rsp->posix_errno != 0) {
    GST_ERROR_OBJECT (self, "BT_GETCAPABILITIES failed : %s(%d)",
        strerror (rsp->posix_errno), rsp->posix_errno);
    return FALSE;
  }

  if (rsp->transport != BT_CAPABILITIES_TRANSPORT_A2DP) {
    GST_ERROR_OBJECT (self, "Non a2dp answer from device");
    return FALSE;
  }

  memcpy (&self->data->cfg, rsp, sizeof (*rsp));
  if (!gst_a2dp_sink_update_caps (self)) {
    GST_WARNING_OBJECT (self, "failed to update capabilities");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_a2dp_sink_start (GstBaseSink * basesink)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);
  gint sk;
  gint err;

  GST_INFO_OBJECT (self, "start");

  self->watch_id = 0;

  sk = bt_audio_service_open ();
  if (sk <= 0) {
    err = errno;
    GST_ERROR_OBJECT (self, "Cannot open connection to bt "
        "audio service: %s %d", strerror (err), err);
    goto failed;
  }

  self->server = g_io_channel_unix_new (sk);
  self->watch_id = g_io_add_watch (self->server, G_IO_HUP | G_IO_ERR |
      G_IO_NVAL, server_callback, self);

  self->data = g_new0 (struct bluetooth_data, 1);
  memset (self->data, 0, sizeof (struct bluetooth_data));

  self->stream = NULL;
  self->stream_caps = NULL;

  if (!gst_a2dp_sink_get_capabilities (self))
    goto failed;

  return TRUE;

failed:
  bt_audio_service_close (sk);
  return FALSE;
}

static gboolean
gst_a2dp_sink_stream_start (GstA2dpSink * self)
{
  gchar buf[BT_AUDIO_IPC_PACKET_SIZE];
  struct bt_streamstart_req *req = (void *) buf;
  struct bt_streamstart_rsp *rsp = (void *) buf;
  struct bt_datafd_ind *ind = (void *) buf;
  GIOError io_error;

  GST_DEBUG_OBJECT (self, "stream start");

  memset (req, 0, sizeof (buf));
  req->h.msg_type = BT_STREAMSTART_REQ;

  io_error = gst_a2dp_sink_audioservice_send (self, &req->h);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error ocurred while sending " "start packet");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "stream start packet sent");

  io_error = gst_a2dp_sink_audioservice_expect (self, &rsp->h,
      BT_STREAMSTART_RSP);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while stream start confirmation");
    return FALSE;
  }

  if (rsp->posix_errno != 0) {
    GST_ERROR_OBJECT (self, "BT_STREAMSTART_RSP failed : %s(%d)",
        strerror (rsp->posix_errno), rsp->posix_errno);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "stream started");

  io_error = gst_a2dp_sink_audioservice_expect (self, &ind->h, BT_STREAMFD_IND);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while receiving stream fd");
    return FALSE;
  }

  if (!gst_a2dp_sink_conf_recv_stream_fd (self))
    return FALSE;

  return TRUE;
}

static gboolean
gst_a2dp_sink_configure (GstA2dpSink * self, GstCaps * caps)
{
  gchar buf[BT_AUDIO_IPC_PACKET_SIZE];
  struct bt_setconfiguration_req *req = (void *) buf;
  struct bt_setconfiguration_rsp *rsp = (void *) buf;
  gboolean ret;
  GIOError io_error;

  GST_DEBUG_OBJECT (self, "configuring device");

  memset (req, 0, sizeof (buf));
  req->h.msg_type = BT_SETCONFIGURATION_REQ;
  strncpy (req->device, self->device, 18);
  ret = gst_a2dp_sink_init_pkt_conf (self, caps, &req->sbc_capabilities);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Couldn't parse caps " "to packet configuration");
    return FALSE;
  }

  io_error = gst_a2dp_sink_audioservice_send (self, &req->h);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error ocurred while sending "
        "configurarion packet");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "configuration packet sent");

  io_error = gst_a2dp_sink_audioservice_expect (self, &rsp->h,
      BT_SETCONFIGURATION_RSP);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while receiving device confirmation");
    return FALSE;
  }

  if (rsp->posix_errno != 0) {
    GST_ERROR_OBJECT (self, "BT_SETCONFIGURATION_RSP failed : %s(%d)",
        strerror (rsp->posix_errno), rsp->posix_errno);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "configuration set");

  return TRUE;
}

static GstFlowReturn
gst_a2dp_sink_preroll (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstA2dpSink *sink = GST_A2DP_SINK (basesink);
  gboolean ret;

  GST_A2DP_SINK_MUTEX_LOCK (sink);

  ret = gst_a2dp_sink_stream_start (sink);

  GST_A2DP_SINK_MUTEX_UNLOCK (sink);

  if (!ret)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static int
gst_a2dp_sink_avdtp_write (GstA2dpSink * self)
{
  gsize ret;
  struct bluetooth_data *data = self->data;
  struct rtp_header *header;
  struct rtp_payload *payload;
  GIOError err;

  header = (void *) data->buffer;
  payload = (void *) (data->buffer + sizeof (*header));

  memset (data->buffer, 0, sizeof (*header) + sizeof (*payload));

  payload->frame_count = data->frame_count;
  header->v = 2;
  header->pt = 1;
  header->sequence_number = htons (data->seq_num);
  header->timestamp = htonl (data->nsamples);
  header->ssrc = htonl (1);

  err = g_io_channel_write (self->stream, data->buffer, data->count, &ret);
  if (err != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error while sending data");
    ret = -1;
  }

  /* Reset buffer of data to send */
  data->count = sizeof (struct rtp_header) + sizeof (struct rtp_payload);
  data->frame_count = 0;
  data->samples = 0;
  data->seq_num++;

  return ret;
}

static GstFlowReturn
gst_a2dp_sink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);
  struct bluetooth_data *data = self->data;
  gint encoded;
  gint ret;

  encoded = GST_BUFFER_SIZE (buffer);

  if (data->count + encoded >= data->cfg.link_mtu) {
    ret = gst_a2dp_sink_avdtp_write (self);
    if (ret < 0)
      return GST_FLOW_ERROR;
  }

  memcpy (data->buffer + data->count, GST_BUFFER_DATA (buffer), encoded);
  data->count += encoded;
  data->frame_count++;

  return GST_FLOW_OK;
}

static GstCaps *
gst_a2dp_sink_get_caps (GstBaseSink * basesink)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);

  return self->dev_caps ? gst_caps_ref (self->dev_caps) : NULL;
}

static gboolean
gst_a2dp_sink_set_caps (GstBaseSink * basesink, GstCaps * caps)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);
  gboolean ret;

  GST_A2DP_SINK_MUTEX_LOCK (self);
  ret = gst_a2dp_sink_configure (self, caps);

  if (self->stream_caps)
    gst_caps_unref (self->stream_caps);
  self->stream_caps = gst_caps_ref (caps);

  GST_A2DP_SINK_MUTEX_UNLOCK (self);

  return ret;
}

static gboolean
gst_a2dp_sink_unlock (GstBaseSink * basesink)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);

  if (self->stream != NULL)
    g_io_channel_flush (self->stream, NULL);

  return TRUE;
}

static void
gst_a2dp_sink_class_init (GstA2dpSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_a2dp_sink_finalize);
  object_class->set_property = GST_DEBUG_FUNCPTR (gst_a2dp_sink_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_a2dp_sink_get_property);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_a2dp_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_a2dp_sink_stop);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_a2dp_sink_render);
  basesink_class->preroll = GST_DEBUG_FUNCPTR (gst_a2dp_sink_preroll);
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_a2dp_sink_set_caps);
  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_a2dp_sink_get_caps);
  basesink_class->unlock = GST_DEBUG_FUNCPTR (gst_a2dp_sink_unlock);

  g_object_class_install_property (object_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Bluetooth remote device address", NULL, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (a2dp_sink_debug, "a2dpsink", 0, "A2DP sink element");
}

static void
gst_a2dp_sink_init (GstA2dpSink * self, GstA2dpSinkClass * klass)
{
  self->device = NULL;
  self->data = NULL;

  self->stream = NULL;

  self->dev_caps = NULL;

  self->sink_lock = g_mutex_new ();
}

static GIOError
gst_a2dp_sink_audioservice_send (GstA2dpSink * self,
    const bt_audio_msg_header_t * msg)
{
  gint err;
  GIOError error;
  gsize written;

  GST_DEBUG_OBJECT (self, "sending %s", bt_audio_strmsg (msg->msg_type));

  error = g_io_channel_write (self->server, (const gchar *) msg,
      BT_AUDIO_IPC_PACKET_SIZE, &written);
  if (error != G_IO_ERROR_NONE) {
    err = errno;
    GST_ERROR_OBJECT (self, "Error sending data to audio service:"
        " %s(%d)", strerror (err), err);
  }

  return error;
}

static GIOError
gst_a2dp_sink_audioservice_recv (GstA2dpSink * self,
    bt_audio_msg_header_t * inmsg)
{
  GIOError status;
  gsize bytes_read;
  const char *type;

  status = g_io_channel_read (self->server, (gchar *) inmsg,
      BT_AUDIO_IPC_PACKET_SIZE, &bytes_read);
  if (status != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (self, "Error receiving data from service");
    return status;
  }

  type = bt_audio_strmsg (inmsg->msg_type);
  if (!type) {
    GST_ERROR_OBJECT (self, "Bogus message type %d "
        "received from audio service", inmsg->msg_type);
    return G_IO_ERROR_INVAL;
  }

  GST_DEBUG_OBJECT (self, "Received %s", type);

  return status;
}

static GIOError
gst_a2dp_sink_audioservice_expect (GstA2dpSink * self,
    bt_audio_msg_header_t * outmsg, int expected_type)
{
  GIOError status;

  status = gst_a2dp_sink_audioservice_recv (self, outmsg);
  if (status != G_IO_ERROR_NONE)
    return status;

  if (outmsg->msg_type != expected_type) {
    GST_ERROR_OBJECT (self, "Bogus message %s "
        "received while %s was expected",
        bt_audio_strmsg (outmsg->msg_type), bt_audio_strmsg (expected_type));
    return G_IO_ERROR_INVAL;
  }

  return status;
}
