/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Collabora Ltd.
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

#include <stdint.h>
#include <string.h>
#include <poll.h>

#include <gst/rtp/gstrtppayloads.h>
#include "gstavdtpsrc.h"

GST_DEBUG_CATEGORY_STATIC (avdtpsrc_debug);
#define GST_CAT_DEFAULT (avdtpsrc_debug)

enum
{
  PROP_0,
  PROP_TRANSPORT
};

GST_BOILERPLATE (GstAvdtpSrc, gst_avdtp_src, GstBaseSrc, GST_TYPE_BASE_SRC);

static GstStaticPadTemplate gst_avdtp_src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\","
        "payload = (int) "
        GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 16000, 32000, "
        "44100, 48000 }, " "encoding-name = (string) \"SBC\"; "));

static void gst_avdtp_src_finalize (GObject * object);
static void gst_avdtp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_avdtp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstCaps *gst_avdtp_src_getcaps (GstPad * pad);
static gboolean gst_avdtp_src_start (GstBaseSrc * bsrc);
static gboolean gst_avdtp_src_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_avdtp_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint length, GstBuffer ** outbuf);
static gboolean gst_avdtp_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_avdtp_src_unlock_stop (GstBaseSrc * bsrc);

static void
gst_avdtp_src_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_avdtp_src_template));

  gst_element_class_set_details_simple (element_class,
      "Bluetooth AVDTP Source",
      "Source/Audio/Network/RTP",
      "Receives audio from an A2DP device",
      "Arun Raghavan <arun.raghavan@collabora.co.uk>");
}

static void
gst_avdtp_src_class_init (GstAvdtpSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_avdtp_src_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_avdtp_src_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_avdtp_src_get_property);

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_avdtp_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_avdtp_src_stop);
  basesrc_class->create = GST_DEBUG_FUNCPTR (gst_avdtp_src_create);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_avdtp_src_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_avdtp_src_unlock_stop);

  g_object_class_install_property (gobject_class, PROP_TRANSPORT,
      g_param_spec_string ("transport",
          "Transport", "Use configured transport", NULL, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (avdtpsrc_debug, "avdtpsrc", 0,
      "Bluetooth AVDTP Source");
}

static void
gst_avdtp_src_init (GstAvdtpSrc * avdtpsrc, GstAvdtpSrcClass * klass)
{
  avdtpsrc->poll = gst_poll_new (TRUE);

  gst_base_src_set_format (GST_BASE_SRC (avdtpsrc), GST_FORMAT_DEFAULT);
  gst_base_src_set_live (GST_BASE_SRC (avdtpsrc), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (avdtpsrc), TRUE);

  gst_pad_set_getcaps_function (GST_BASE_SRC_PAD (avdtpsrc),
      GST_DEBUG_FUNCPTR (gst_avdtp_src_getcaps));
}

static void
gst_avdtp_src_finalize (GObject * object)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (object);

  gst_poll_free (avdtpsrc->poll);

  gst_avdtp_connection_reset (&avdtpsrc->conn);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avdtp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (object);

  switch (prop_id) {
    case PROP_TRANSPORT:
      g_value_set_string (value, avdtpsrc->conn.transport);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avdtp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (object);

  switch (prop_id) {
    case PROP_TRANSPORT:
      gst_avdtp_connection_set_transport (&avdtpsrc->conn,
          g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_avdtp_src_getcaps (GstPad * pad)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (gst_pad_get_parent_element (pad));
  GstCaps *ret;

  if (avdtpsrc->dev_caps) {
    const GValue *value;
    const char *format;
    int rate;
    GstStructure *structure = gst_caps_get_structure (avdtpsrc->dev_caps, 0);

    format = gst_structure_get_name (structure);

    if (g_str_equal (format, "audio/x-sbc")) {
      /* FIXME: we can return a fixed payload type once we
       * are in PLAYING */
      ret = gst_caps_new_simple ("application/x-rtp",
          "media", G_TYPE_STRING, "audio",
          "payload", GST_TYPE_INT_RANGE, 96, 127,
          "encoding-name", G_TYPE_STRING, "SBC", NULL);
    } else if (g_str_equal (format, "audio/mpeg")) {
      GST_ERROR_OBJECT (avdtpsrc, "Only SBC is supported at " "the moment");
    }

    value = gst_structure_get_value (structure, "rate");
    if (!value || !G_VALUE_HOLDS_INT (value)) {
      GST_ERROR_OBJECT (avdtpsrc, "Failed to get sample rate");
      goto fail;
    }
    rate = g_value_get_int (value);

    gst_caps_set_simple (ret, "clock-rate", G_TYPE_INT, rate, NULL);
  } else {
    ret = gst_caps_ref (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad)));
  }

  return ret;

fail:
  if (ret)
    gst_caps_unref (ret);

  return NULL;
}

static gboolean
gst_avdtp_src_start (GstBaseSrc * bsrc)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (bsrc);

  /* None of this can go into prepare() since we need to set up the
   * connection to figure out what format the device is going to send us.
   */

  if (!gst_avdtp_connection_acquire (&avdtpsrc->conn)) {
    GST_ERROR_OBJECT (avdtpsrc, "Failed to acquire connection");
    return FALSE;
  }

  if (!gst_avdtp_connection_get_properties (&avdtpsrc->conn)) {
    GST_ERROR_OBJECT (avdtpsrc, "Failed to get transport properties");
    goto fail;
  }

  if (!gst_avdtp_connection_conf_recv_stream_fd (&avdtpsrc->conn)) {
    GST_ERROR_OBJECT (avdtpsrc, "Failed to configure stream fd");
    goto fail;
  }

  GST_DEBUG_OBJECT (avdtpsrc, "Setting block size to link MTU (%d)",
      avdtpsrc->conn.data.link_mtu);
  gst_base_src_set_blocksize (GST_BASE_SRC (avdtpsrc),
      avdtpsrc->conn.data.link_mtu);

  avdtpsrc->dev_caps = gst_avdtp_connection_get_caps (&avdtpsrc->conn);
  if (!avdtpsrc->dev_caps) {
    GST_ERROR_OBJECT (avdtpsrc, "Failed to get device caps");
    goto fail;
  }

  gst_poll_fd_init (&avdtpsrc->pfd);
  avdtpsrc->pfd.fd = g_io_channel_unix_get_fd (avdtpsrc->conn.stream);

  gst_poll_add_fd (avdtpsrc->poll, &avdtpsrc->pfd);
  gst_poll_fd_ctl_read (avdtpsrc->poll, &avdtpsrc->pfd, TRUE);
  gst_poll_set_flushing (avdtpsrc->poll, FALSE);

  g_atomic_int_set (&avdtpsrc->unlocked, FALSE);

  return TRUE;

fail:
  gst_avdtp_connection_release (&avdtpsrc->conn);
  return FALSE;
}

static gboolean
gst_avdtp_src_stop (GstBaseSrc * bsrc)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (bsrc);

  gst_poll_remove_fd (avdtpsrc->poll, &avdtpsrc->pfd);
  gst_poll_set_flushing (avdtpsrc->poll, TRUE);

  gst_avdtp_connection_release (&avdtpsrc->conn);

  if (avdtpsrc->dev_caps) {
    gst_caps_unref (avdtpsrc->dev_caps);
    avdtpsrc->dev_caps = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_avdtp_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint length, GstBuffer ** outbuf)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (bsrc);
  GstBuffer *buf = NULL;
  int ret;

  if (g_atomic_int_get (&avdtpsrc->unlocked))
    return GST_FLOW_WRONG_STATE;

  /* We don't operate in GST_FORMAT_BYTES, so offset is ignored */

  while ((ret = gst_poll_wait (avdtpsrc->poll, GST_CLOCK_TIME_NONE))) {
    if (g_atomic_int_get (&avdtpsrc->unlocked))
      /* We're unlocked, time to gtfo */
      return GST_FLOW_WRONG_STATE;

    if (ret < 0)
      /* Something went wrong */
      goto read_error;

    if (ret > 0)
      /* Got some data */
      break;
  }

  buf = gst_buffer_new_and_alloc (length);
  ret = read (avdtpsrc->pfd.fd, GST_BUFFER_DATA (buf), length);

  if (ret < 0)
    goto read_error;

  GST_LOG_OBJECT (avdtpsrc, "Read %d bytes", ret);

  if (ret < length) {
    /* Create a subbuffer for as much as we've actually read */
    *outbuf = gst_buffer_create_sub (buf, 0, ret);
    gst_buffer_unref (buf);
  } else
    *outbuf = buf;

  return GST_FLOW_OK;

read_error:
  gst_buffer_unref (buf);

  GST_ERROR_OBJECT (avdtpsrc, "Error while reading audio data: %s",
      strerror (errno));

  return GST_FLOW_ERROR;
}

static gboolean
gst_avdtp_src_unlock (GstBaseSrc * bsrc)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (bsrc);

  g_atomic_int_set (&avdtpsrc->unlocked, TRUE);

  gst_poll_set_flushing (avdtpsrc->poll, TRUE);
}

static gboolean
gst_avdtp_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAvdtpSrc *avdtpsrc = GST_AVDTP_SRC (bsrc);

  g_atomic_int_set (&avdtpsrc->unlocked, FALSE);

  gst_poll_set_flushing (avdtpsrc->poll, FALSE);

  /* Flush out any stale data that might be buffered */
  gst_avdtp_connection_conf_recv_stream_fd (&avdtpsrc->conn);
}

gboolean
gst_avdtp_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avdtpsrc", GST_RANK_NONE,
      GST_TYPE_AVDTP_SRC);
}
