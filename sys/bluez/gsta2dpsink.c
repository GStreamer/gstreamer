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

struct bluetooth_a2dp
{
  sbc_t sbc;                    /* Codec data */
  int codesize;                 /* SBC codesize */
  int samples;                  /* Number of encoded samples */
  uint8_t buffer[BUFFER_SIZE];  /* Codec transfer buffer */
  int count;                    /* Codec transfer buffer counter */

  int nsamples;                 /* Cumulative number of codec samples */
  uint16_t seq_num;             /* Cumulative packet sequence */
  int frame_count;              /* Current frames in buffer */
};
struct bluetooth_data
{
  struct ipc_data_cfg cfg;      /* Bluetooth device config */
  uint8_t buffer[BUFFER_SIZE];  /* Encoded transfer buffer */
  int count;                    /* Transfer buffer counter */
  struct bluetooth_a2dp a2dp;   /* A2DP data */
};

#if __BYTE_ORDER == __LITTLE_ENDIAN

struct rtp_header
{
  uint8_t cc:4;
  uint8_t x:1;
  uint8_t p:1;
  uint8_t v:2;

  uint8_t pt:7;
  uint8_t m:1;

  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
  uint32_t csrc[0];
} __attribute__ ((packed));

struct rtp_payload
{
  uint8_t frame_count:4;
  uint8_t rfa0:1;
  uint8_t is_last_fragment:1;
  uint8_t is_first_fragment:1;
  uint8_t is_fragmented:1;
} __attribute__ ((packed));

#elif __BYTE_ORDER == __BIG_ENDIAN

struct rtp_header
{
  uint8_t v:2;
  uint8_t p:1;
  uint8_t x:1;
  uint8_t cc:4;

  uint8_t m:1;
  uint8_t pt:7;

  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
  uint32_t csrc[0];
} __attribute__ ((packed));

struct rtp_payload
{
  uint8_t is_fragmented:1;
  uint8_t is_first_fragment:1;
  uint8_t is_last_fragment:1;
  uint8_t rfa0:1;
  uint8_t frame_count:4;
} __attribute__ ((packed));

#else
#error "Unknown byte order"
#endif

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
  struct bluetooth_a2dp *a2dp = &self->data->a2dp;

  self->con_state = NOT_CONFIGURED;
  self->total = 0;

  if (self->stream) {
    g_io_channel_close (self->stream);
    g_io_channel_unref (self->stream);
    self->stream = NULL;
  }

  if (self->server) {
    g_io_channel_close (self->server);
    g_io_channel_unref (self->server);
    self->stream = NULL;
  }

  if (self->data->cfg.codec == CFG_CODEC_SBC)
    sbc_finish (&a2dp->sbc);

  if (self->data) {
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
gst_a2dp_sink_bluetooth_a2dp_init (GstA2dpSink * sink,
    struct ipc_codec_sbc *sbc)
{
  struct bluetooth_a2dp *a2dp = &sink->data->a2dp;
  struct ipc_data_cfg *cfg = &sink->data->cfg;

  if (cfg == NULL) {
    GST_ERROR_OBJECT (sink, "Error getting codec parameters");
    return -1;
  }

  if (cfg->codec != CFG_CODEC_SBC)
    return -1;

  /* FIXME: init using flags? */
  sbc_init (&a2dp->sbc, 0);
  a2dp->sbc.rate = cfg->rate;
  a2dp->sbc.channels = cfg->mode == CFG_MODE_MONO ? 1 : 2;
  if (cfg->mode == CFG_MODE_MONO || cfg->mode == CFG_MODE_JOINT_STEREO)
    a2dp->sbc.joint = 1;
  a2dp->sbc.allocation = sbc->allocation;
  a2dp->sbc.subbands = sbc->subbands;
  a2dp->sbc.blocks = sbc->blocks;
  a2dp->sbc.bitpool = sbc->bitpool;
  a2dp->codesize = a2dp->sbc.subbands * a2dp->sbc.blocks *
      a2dp->sbc.channels * 2;
  a2dp->count = sizeof (struct rtp_header) + sizeof (struct rtp_payload);

  GST_DEBUG_OBJECT (sink, "Codec parameters: \
				\tallocation=%u\n\tsubbands=%u\n \
				\tblocks=%u\n\tbitpool=%u\n", a2dp->sbc.allocation, a2dp->sbc.subbands, a2dp->sbc.blocks, a2dp->sbc.bitpool);

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
  guint ret;
  struct ipc_packet *pkt = (void *) buf;
  struct ipc_data_cfg *cfg = (void *) pkt->data;

  memset (buf, 0, sizeof (buf));

  io_error = g_io_channel_read (sink->server, (gchar *) buf,
      sizeof (*pkt) + sizeof (*cfg), &ret);
  if (io_error != G_IO_ERROR_NONE && ret > 0) {
    GST_ERROR_OBJECT (sink, "Error ocurred while receiving \
					configurarion packet answer");
    return FALSE;
  }

  sink->total = ret;
  if (pkt->type != PKT_TYPE_CFG_RSP) {
    GST_ERROR_OBJECT (sink, "Unexpected packet type %d \
					received", pkt->type);
    return FALSE;
  }

  if (pkt->error != PKT_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error %d while configuring \
					device", pkt->error);
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
  guint ret = 0;
  struct ipc_packet *pkt = (void *) buf;
  struct ipc_data_cfg *cfg = (void *) pkt->data;
  struct ipc_codec_sbc *sbc = (void *) cfg->data;

  io_error = g_io_channel_read (sink->server, (gchar *) sbc,
      sizeof (*sbc), &ret);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error while reading data from socket \
				%s (%d)", strerror (errno), errno);
    return FALSE;
  } else if (ret == 0) {
    GST_ERROR_OBJECT (sink, "Read 0 bytes from socket");
    return FALSE;
  }

  sink->total += ret;
  GST_DEBUG_OBJECT (sink, "OK - %d bytes received", sink->total);

  if (pkt->length != (sink->total - sizeof (struct ipc_packet))) {
    GST_ERROR_OBJECT (sink, "Error while configuring device: "
        "packet size doesn't match");
    return FALSE;
  }

  memcpy (&sink->data->cfg, cfg, sizeof (*cfg));

  GST_DEBUG_OBJECT (sink, "Device configuration:\n\tchannel=%p\n\t\
			fd_opt=%u\n\tpkt_len=%u\n\tsample_size=%u\n\trate=%u", sink->stream, sink->data->cfg.fd_opt, sink->data->cfg.pkt_len, sink->data->cfg.sample_size, sink->data->cfg.rate);

  if (sink->data->cfg.codec == CFG_CODEC_SBC) {
    ret = gst_a2dp_sink_bluetooth_a2dp_init (sink, sbc);
    if (ret < 0)
      return FALSE;

  }
  return TRUE;
}

static gboolean
gst_a2dp_sink_conf_recv_stream_fd (GstA2dpSink * sink)
{
  gint ret;
  GIOError err;
  gsize read;

  ret = gst_a2dp_sink_bluetooth_recvmsg_fd (sink);
  if (ret < 0)
    return FALSE;

  if (!sink->stream) {
    GST_ERROR_OBJECT (sink, "Error while configuring device: "
        "could not acquire audio socket");
    return FALSE;
  }

  /* It is possible there is some outstanding
     data in the pipe - we have to empty it */
  while (1) {
    err = g_io_channel_read (sink->stream,
        (gchar *) sink->data->buffer, (gsize) sink->data->cfg.pkt_len, &read);
    if (err != G_IO_ERROR_NONE || read <= 0)
      break;
  }

  memset (sink->data->buffer, 0, sizeof (sink->data->buffer));

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
  GstA2dpSink *sink = GST_A2DP_SINK (data);

  switch (cond) {
    case G_IO_IN:
      if (sink->con_state != NOT_CONFIGURED && sink->con_state != CONFIGURED)
        gst_a2dp_sink_conf_recv_data (sink);
      else
        GST_WARNING_OBJECT (sink, "Unexpected data received");
      break;
    case G_IO_HUP:
      return FALSE;
      break;
    case G_IO_ERR:
      GST_WARNING_OBJECT (sink, "Untreated callback G_IO_ERR");
      break;
    case G_IO_NVAL:
      return FALSE;
      break;
    default:
      GST_WARNING_OBJECT (sink, "Unexpected callback call");
      break;
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

  g_io_add_watch (self->server, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
      server_callback, self);

  self->data = g_new0 (struct bluetooth_data, 1);
  memset (self->data, 0, sizeof (struct bluetooth_data));

  return TRUE;
}

static gboolean
gst_a2dp_sink_send_conf_pkt (GstA2dpSink * sink, GstCaps * caps)
{
  gchar buf[IPC_MTU];
  struct ipc_packet *pkt = (void *) buf;
  gboolean ret;
  guint bytes_sent;
  GIOError io_error;

  g_assert (sink->con_state == NOT_CONFIGURED);

  memset (pkt, 0, sizeof (buf));
  ret = gst_a2dp_sink_init_pkt_conf (sink, caps, pkt);
  if (!ret) {
    GST_ERROR_OBJECT (sink, "Couldn't initialize parse caps \
				to packet configuration");
    return FALSE;
  }

  sink->con_state = CONFIGURING_INIT;

  io_error = g_io_channel_write (sink->server, (gchar *) pkt,
      sizeof (*pkt) + pkt->length, &bytes_sent);
  if (io_error != G_IO_ERROR_NONE) {
    GST_ERROR_OBJECT (sink, "Error ocurred while sending \
					configurarion packet");
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
gst_a2dp_sink_avdtp_write (GstA2dpSink * sink)
{
  int ret = 0;
  struct bluetooth_data *data;
  struct rtp_header *header;
  struct rtp_payload *payload;
  struct bluetooth_a2dp *a2dp;
  GIOError err;

  data = sink->data;
  a2dp = &data->a2dp;

  header = (void *) a2dp->buffer;
  payload = (void *) (a2dp->buffer + sizeof (*header));

  memset (a2dp->buffer, 0, sizeof (*header) + sizeof (*payload));

  payload->frame_count = a2dp->frame_count;
  header->v = 2;
  header->pt = 1;
  header->sequence_number = htons (a2dp->seq_num);
  header->timestamp = htonl (a2dp->nsamples);
  header->ssrc = htonl (1);

  while (1) {
    err = g_io_channel_write (sink->stream, (const char *) a2dp->buffer,
        (gsize) a2dp->count, (gsize *) & ret);

    if (err == G_IO_ERROR_AGAIN) {
      usleep (100);
      continue;
    }

    break;
  }

  /* Reset buffer of data to send */
  a2dp->count = sizeof (struct rtp_header) + sizeof (struct rtp_payload);
  a2dp->frame_count = 0;
  a2dp->samples = 0;
  a2dp->seq_num++;

  return ret;
}

static GstFlowReturn
gst_a2dp_sink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstA2dpSink *sink;
  struct bluetooth_data *data;
  struct bluetooth_a2dp *a2dp;
  gint encoded, frame_size = 1024;
  gint ret = 0;

  sink = GST_A2DP_SINK (basesink);
  data = (struct bluetooth_data *) sink->data;
  a2dp = &data->a2dp;

  encoded = GST_BUFFER_SIZE (buffer);

  if (a2dp->count + encoded >= data->cfg.pkt_len) {
    ret = gst_a2dp_sink_avdtp_write (sink);
    if (ret < 0)
      return GST_FLOW_ERROR;
  }

  memcpy (a2dp->buffer + a2dp->count, GST_BUFFER_DATA (buffer), encoded);
  a2dp->count += encoded;
  a2dp->frame_count++;
  a2dp->samples += encoded / frame_size;
  a2dp->nsamples += encoded / frame_size;

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
