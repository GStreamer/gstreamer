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
#include "sbc.h"
#include "rtp.h"

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

#define GST_A2DP_SINK_WAIT_CON_END(s) G_STMT_START {			\
		s->waiting_con_conf = TRUE;				\
		g_cond_wait (s->con_conf_end, s->sink_lock);		\
		s->waiting_con_conf = FALSE;				\
	} G_STMT_END

#define GST_A2DP_SINK_CONFIGURATION_FAIL(s) G_STMT_START {		\
		s->con_state = NOT_CONFIGURED;				\
		g_cond_signal (s->con_conf_end);			\
	} G_STMT_END

#define GST_A2DP_SINK_CONFIGURATION_SUCCESS(s) G_STMT_START {		\
		s->con_state = CONFIGURED;				\
		g_cond_signal (s->con_conf_end);			\
	} G_STMT_END

struct bluetooth_data
{
  struct ipc_data_cfg cfg;      /* Bluetooth device config */
  sbc_t sbc;                    /* Codec data */
  int codesize;                 /* SBC codesize */
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
        "allocation = (string) { snr, loudness }; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]"));

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

  self->con_state = NOT_CONFIGURED;
  self->total = 0;

  if (self->watch_id != 0) {
    g_source_remove (self->watch_id);
    self->watch_id = 0;
  }

  if (self->server) {
    g_io_channel_close (self->server);
    g_io_channel_unref (self->server);
    self->stream = NULL;
  }

  if (self->data) {
    if (self->data->cfg.codec == CFG_CODEC_SBC)
      sbc_finish (&self->data->sbc);
    g_free (self->data);
    self->data = NULL;
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

  /* unlock any thread waiting for this signal */
  GST_A2DP_SINK_MUTEX_LOCK (self);
  GST_A2DP_SINK_CONFIGURATION_FAIL (self);
  GST_A2DP_SINK_MUTEX_UNLOCK (self);

  g_cond_free (self->con_conf_end);
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
  char cmsg_b[CMSG_SPACE (sizeof (int))], m;
  int err, ret, stream_fd;
  struct iovec iov = { &m, sizeof (m) };
  struct msghdr msgh;
  struct cmsghdr *cmsg;

  memset (&msgh, 0, sizeof (msgh));
  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  msgh.msg_control = &cmsg_b;
  msgh.msg_controllen = CMSG_LEN (sizeof (int));

  ret = recvmsg (g_io_channel_unix_get_fd (sink->server), &msgh, 0);
  if (ret < 0) {
    err = errno;
    GST_ERROR_OBJECT (sink, "Unable to receive fd: %s (%d)",
        strerror (err), err);
    return -err;
  }

  /* Receive auxiliary data in msgh */
  for (cmsg = CMSG_FIRSTHDR (&msgh); cmsg != NULL;
      cmsg = CMSG_NXTHDR (&msgh, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      stream_fd = (*(int *) CMSG_DATA (cmsg));
      sink->stream = g_io_channel_unix_new (stream_fd);

      GST_DEBUG_OBJECT (sink, "stream_fd=%d", stream_fd);
      return 0;
    }
  }

  return -EINVAL;
}

static int
gst_a2dp_sink_bluetooth_a2dp_init (GstA2dpSink * self,
    struct ipc_codec_sbc *sbc)
{
  struct bluetooth_data *data = self->data;
  struct ipc_data_cfg *cfg = &data->cfg;

  if (cfg == NULL) {
    GST_ERROR_OBJECT (self, "Error getting codec parameters");
    return -1;
  }

  if (cfg->codec != CFG_CODEC_SBC)
    return -1;

  /* FIXME: init using flags? */
  sbc_init (&data->sbc, 0);
  data->sbc.rate = cfg->rate;
  data->sbc.channels = cfg->mode == CFG_MODE_MONO ? 1 : 2;
  if (cfg->mode == CFG_MODE_MONO || cfg->mode == CFG_MODE_JOINT_STEREO)
    data->sbc.joint = 1;
  data->sbc.allocation = sbc->allocation;
  data->sbc.subbands = sbc->subbands;
  data->sbc.blocks = sbc->blocks;
  data->sbc.bitpool = sbc->bitpool;
  data->codesize = data->sbc.subbands * data->sbc.blocks *
      data->sbc.channels * 2;
  data->count = sizeof (struct rtp_header) + sizeof (struct rtp_payload);

  GST_DEBUG_OBJECT (self, "Codec parameters: "
      "\tallocation=%u\n\tsubbands=%u\n "
      "\tblocks=%u\n\tbitpool=%u\n",
      data->sbc.allocation, data->sbc.subbands,
      data->sbc.blocks, data->sbc.bitpool);

  return 0;
}

static gboolean
gst_a2dp_sink_init_pkt_conf (GstA2dpSink * sink,
    GstCaps * caps, struct ipc_packet *pkt)
{

  struct ipc_data_cfg *cfg = (void *) pkt->data;
  struct ipc_codec_sbc *sbc = (void *) cfg->data;
  const GValue *value = NULL;
  const char *pref, *name;
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  name = gst_structure_get_name (structure);
  /* FIXME only sbc supported here, should suport mp3 */
  if (!(IS_SBC (name))) {
    GST_ERROR_OBJECT (sink, "Unsupported format %s", name);
    return FALSE;
  }

  if (sink->device)
    strncpy (pkt->device, sink->device, 18);

  pkt->role = PKT_ROLE_HIFI;

  value = gst_structure_get_value (structure, "rate");
  cfg->rate = g_value_get_int (value);

  value = gst_structure_get_value (structure, "mode");
  pref = g_value_get_string (value);
  if (strcmp (pref, "auto") == 0)
    cfg->mode = CFG_MODE_AUTO;
  else if (strcmp (pref, "mono") == 0)
    cfg->mode = CFG_MODE_MONO;
  else if (strcmp (pref, "dual") == 0)
    cfg->mode = CFG_MODE_DUAL_CHANNEL;
  else if (strcmp (pref, "stereo") == 0)
    cfg->mode = CFG_MODE_STEREO;
  else if (strcmp (pref, "joint") == 0)
    cfg->mode = CFG_MODE_JOINT_STEREO;
  else {
    GST_ERROR_OBJECT (sink, "Invalid mode %s", pref);
    return FALSE;
  }

  value = gst_structure_get_value (structure, "allocation");
  pref = g_value_get_string (value);
  if (strcmp (pref, "auto") == 0)
    sbc->allocation = CFG_ALLOCATION_AUTO;
  else if (strcmp (pref, "loudness") == 0)
    sbc->allocation = CFG_ALLOCATION_LOUDNESS;
  else if (strcmp (pref, "snr") == 0)
    sbc->allocation = CFG_ALLOCATION_SNR;
  else {
    GST_ERROR_OBJECT (sink, "Invalid allocation: %s", pref);
    return FALSE;
  }

  value = gst_structure_get_value (structure, "subbands");
  sbc->subbands = g_value_get_int (value);

  value = gst_structure_get_value (structure, "blocks");
  sbc->blocks = g_value_get_int (value);
  sbc->bitpool = 32;

  pkt->length = sizeof (*cfg) + sizeof (*sbc);
  pkt->type = PKT_TYPE_CFG_REQ;
  pkt->error = PKT_ERROR_NONE;

  return TRUE;

}

static gboolean
gst_a2dp_sink_conf_resp (GstA2dpSink * sink)
{
  gchar buf[IPC_MTU];
  GIOError io_error;
  gsize ret;
  struct ipc_packet *pkt = (void *) buf;
  struct ipc_data_cfg *cfg = (void *) pkt->data;

  memset (buf, 0, sizeof (buf));

  io_error = g_io_channel_read (sink->server, (gchar *) buf,
      sizeof (*pkt) + sizeof (*cfg), &ret);
  if (io_error != G_IO_ERROR_NONE && ret > 0) {
    GST_ERROR_OBJECT (sink, "Error ocurred while receiving "
        "configurarion packet answer");
    return FALSE;
  }

  sink->total = ret;
  if (pkt->type != PKT_TYPE_CFG_RSP) {
    GST_ERROR_OBJECT (sink, "Unexpected packet type %d " "received", pkt->type);
    return FALSE;
  }

  if (pkt->error != PKT_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error %d while configuring " "device", pkt->error);
    return FALSE;
  }

  if (cfg->codec != CFG_CODEC_SBC) {
    GST_ERROR_OBJECT (sink, "Unsupported format");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_a2dp_sink_conf_recv_dev_conf (GstA2dpSink * sink)
{
  gchar buf[IPC_MTU];
  GIOError io_error;
  gsize ret;
  struct ipc_packet *pkt = (void *) buf;
  struct ipc_data_cfg *cfg = (void *) pkt->data;
  struct ipc_codec_sbc *sbc = (void *) cfg->data;

  io_error = g_io_channel_read (sink->server, (gchar *) sbc,
      sizeof (*sbc), &ret);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error while reading data from socket "
        "%s (%d)", strerror (errno), errno);
    return FALSE;
  } else if (ret == 0) {
    GST_ERROR_OBJECT (sink, "Read 0 bytes from socket");
    return FALSE;
  }

  sink->total += ret;
  GST_DEBUG_OBJECT (sink, "OK - %d bytes received", sink->total);
#if 0
  if (pkt->length != (sink->total - sizeof (struct ipc_packet))) {
    GST_ERROR_OBJECT (sink, "Error while configuring device: "
        "packet size doesn't match");
    return FALSE;
  }
#endif
  memcpy (&sink->data->cfg, cfg, sizeof (*cfg));

  GST_DEBUG_OBJECT (sink, "Device configuration:\n\tchannel=%p\n\t"
      "fd_opt=%u\n\tpkt_len=%u\n\tsample_size=%u\n\trate=%u",
      sink->stream, sink->data->cfg.fd_opt,
      sink->data->cfg.pkt_len, sink->data->cfg.sample_size,
      sink->data->cfg.rate);

  if (sink->data->cfg.codec == CFG_CODEC_SBC) {
    ret = gst_a2dp_sink_bluetooth_a2dp_init (sink, sbc);
    if (ret < 0)
      return FALSE;

  }
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
        (gsize) data->cfg.pkt_len, &read);
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

static void
gst_a2dp_sink_conf_recv_data (GstA2dpSink * sink)
{
  /*
   * We hold the lock, since we can send a signal.
   * It is a good practice, according to the glib api.
   */
  GST_A2DP_SINK_MUTEX_LOCK (sink);

  switch (sink->con_state) {
    case CONFIGURING_SENT_CONF:
      if (gst_a2dp_sink_conf_resp (sink))
        sink->con_state = CONFIGURING_RCVD_CONF_RSP;
      else
        GST_A2DP_SINK_CONFIGURATION_FAIL (sink);
      break;
    case CONFIGURING_RCVD_CONF_RSP:
      if (gst_a2dp_sink_conf_recv_dev_conf (sink))
        sink->con_state = CONFIGURING_RCVD_DEV_CONF;
      else
        GST_A2DP_SINK_CONFIGURATION_FAIL (sink);
    case CONFIGURING_RCVD_DEV_CONF:
      if (gst_a2dp_sink_conf_recv_stream_fd (sink))
        GST_A2DP_SINK_CONFIGURATION_SUCCESS (sink);
      else
        GST_A2DP_SINK_CONFIGURATION_FAIL (sink);
      break;
  }

  GST_A2DP_SINK_MUTEX_UNLOCK (sink);
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
  } else if (cond & G_IO_IN) {
    sink = GST_A2DP_SINK (data);
    if (sink->con_state != NOT_CONFIGURED && sink->con_state != CONFIGURED)
      gst_a2dp_sink_conf_recv_data (sink);
    else
      GST_WARNING_OBJECT (sink, "Unexpected data received");
  } else {
    sink = GST_A2DP_SINK (data);
    GST_WARNING_OBJECT (sink, "Unexpected callback call");
  }

  return TRUE;
}

static gboolean
gst_a2dp_sink_start (GstBaseSink * basesink)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);
  struct sockaddr_un addr = { AF_UNIX, IPC_SOCKET_NAME };
  gint sk;
  gint err;

  self->watch_id = 0;

  sk = socket (PF_LOCAL, SOCK_STREAM, 0);
  if (sk < 0) {
    err = errno;
    GST_ERROR_OBJECT (self, "Cannot open socket: %s (%d)", strerror (err), err);
    return FALSE;
  }

  if (connect (sk, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    err = errno;
    GST_ERROR_OBJECT (self, "Connection fail %s (%d)", strerror (err), err);
    close (sk);
    return FALSE;
  }

  self->server = g_io_channel_unix_new (sk);

  self->watch_id = g_io_add_watch (self->server, G_IO_IN | G_IO_HUP |
      G_IO_ERR | G_IO_NVAL, server_callback, self);

  self->data = g_new0 (struct bluetooth_data, 1);

  self->stream = NULL;
  self->con_state = NOT_CONFIGURED;
  self->total = 0;

  self->waiting_con_conf = FALSE;

  return TRUE;
}

static gboolean
gst_a2dp_sink_send_conf_pkt (GstA2dpSink * sink, GstCaps * caps)
{
  gchar buf[IPC_MTU];
  struct ipc_packet *pkt = (void *) buf;
  gboolean ret;
  gsize bytes_sent;
  GIOError io_error;

  g_assert (sink->con_state == NOT_CONFIGURED);

  memset (pkt, 0, sizeof (buf));
  ret = gst_a2dp_sink_init_pkt_conf (sink, caps, pkt);
  if (!ret) {
    GST_ERROR_OBJECT (sink, "Couldn't initialize parse caps "
        "to packet configuration");
    return FALSE;
  }

  sink->con_state = CONFIGURING_INIT;

  io_error = g_io_channel_write (sink->server, (gchar *) pkt,
      sizeof (*pkt) + pkt->length, &bytes_sent);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error ocurred while sending "
        "configurarion packet");
    sink->con_state = NOT_CONFIGURED;
    return FALSE;
  }

  GST_DEBUG_OBJECT (sink, "%d bytes sent", bytes_sent);
  sink->con_state = CONFIGURING_SENT_CONF;

  return TRUE;
}

static gboolean
gst_a2dp_sink_start_dev_conf (GstA2dpSink * sink, GstCaps * caps)
{
  gboolean ret;

  g_assert (sink->con_state == NOT_CONFIGURED);

  GST_DEBUG_OBJECT (sink, "starting device configuration");

  ret = gst_a2dp_sink_send_conf_pkt (sink, caps);

  return ret;
}

static GstFlowReturn
gst_a2dp_sink_preroll (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstA2dpSink *sink = GST_A2DP_SINK (basesink);

  GST_A2DP_SINK_MUTEX_LOCK (sink);

  if (sink->con_state == NOT_CONFIGURED)
    gst_a2dp_sink_start_dev_conf (sink, GST_BUFFER_CAPS (buffer));

  /* wait for the connection process to finish */
  if (sink->con_state != CONFIGURED)
    GST_A2DP_SINK_WAIT_CON_END (sink);

  GST_A2DP_SINK_MUTEX_UNLOCK (sink);

  if (sink->con_state != CONFIGURED)
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

  if (data->count + encoded >= data->cfg.pkt_len) {
    ret = gst_a2dp_sink_avdtp_write (self);
    if (ret < 0)
      return GST_FLOW_ERROR;
  }

  memcpy (data->buffer + data->count, GST_BUFFER_DATA (buffer), encoded);
  data->count += encoded;
  data->frame_count++;

  return GST_FLOW_OK;
}

static gboolean
gst_a2dp_sink_set_caps (GstBaseSink * basesink, GstCaps * caps)
{
  GstA2dpSink *self = GST_A2DP_SINK (basesink);

  GST_A2DP_SINK_MUTEX_LOCK (self);
  if (self->con_state == NOT_CONFIGURED)
    gst_a2dp_sink_start_dev_conf (self, caps);
  GST_A2DP_SINK_MUTEX_UNLOCK (self);

  return TRUE;
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
  self->con_state = NOT_CONFIGURED;
  self->total = 0;

  self->con_conf_end = g_cond_new ();
  self->waiting_con_conf = FALSE;
  self->sink_lock = g_mutex_new ();
}
