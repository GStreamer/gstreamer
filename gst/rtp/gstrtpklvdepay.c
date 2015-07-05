/* GStreamer RTP KLV Depayloader
 * Copyright (C) 2014-2015 Tim-Philipp Müller <tim@centricular.com>>
 * Copyright (C) 2014-2015 Centricular Ltd
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
 * SECTION:element-rtpklvdepay
 * @see_also: rtpklvpay
 *
 * Extract KLV metadata from RTP packets according to RFC 6597.
 * For detailed information see: http://tools.ietf.org/html/rfc6597
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 udpsrc caps='application/x-rtp, media=(string)application, clock-rate=(int)90000, encoding-name=(string)SMPTE336M' ! rtpklvdepay ! fakesink dump=true
 * ]| This example pipeline will depayload an RTP KLV stream and display
 * a hexdump of the KLV data on stdout.
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpklvdepay.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (klvdepay_debug);
#define GST_CAT_DEFAULT (klvdepay_debug)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("meta/x-klv, parsed = (bool) true"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) application, clock-rate = (int) [1, MAX], "
        "encoding-name = (string) SMPTE336M")
    );

#define gst_rtp_klv_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpKlvDepay, gst_rtp_klv_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_klv_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_klv_depay_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_rtp_klv_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_klv_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_klv_depay_reset (GstRtpKlvDepay * klvdepay);

static void
gst_rtp_klv_depay_class_init (GstRtpKlvDepayClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstRTPBaseDepayloadClass *rtpbasedepayload_class;

  GST_DEBUG_CATEGORY_INIT (klvdepay_debug, "klvdepay", 0,
      "RTP KLV Depayloader");

  gobject_class->finalize = gst_rtp_klv_depay_finalize;

  element_class->change_state = gst_rtp_klv_depay_change_state;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (element_class,
      "RTP KLV Depayloader", "Codec/Depayloader/Network",
      "Extracts KLV (SMPTE ST 336) metadata from RTP packets",
      "Tim-Philipp Müller <tim@centricular.com>");

  rtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  rtpbasedepayload_class->set_caps = gst_rtp_klv_depay_setcaps;
  rtpbasedepayload_class->process = gst_rtp_klv_depay_process;
}

static void
gst_rtp_klv_depay_init (GstRtpKlvDepay * klvdepay)
{
  klvdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_klv_depay_finalize (GObject * object)
{
  GstRtpKlvDepay *klvdepay;

  klvdepay = GST_RTP_KLV_DEPAY (object);

  gst_rtp_klv_depay_reset (klvdepay);
  g_object_unref (klvdepay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_klv_depay_reset (GstRtpKlvDepay * klvdepay)
{
  GST_DEBUG_OBJECT (klvdepay, "resetting");
  gst_adapter_clear (klvdepay->adapter);
  klvdepay->resync = TRUE;
  klvdepay->last_rtp_ts = -1;
}

static gboolean
gst_rtp_klv_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstStructure *s;
  GstCaps *src_caps;
  gboolean res;
  gint clock_rate;

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "clock-rate", &clock_rate))
    return FALSE;

  depayload->clock_rate = clock_rate;

  src_caps = gst_static_pad_template_get_caps (&src_template);
  res = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), src_caps);
  gst_caps_unref (src_caps);

  return res;
}

static gboolean
klv_get_vlen (const guint8 * data, guint data_len, guint64 * v_len,
    gsize * len_size)
{
  guint8 first_byte, len_len;
  guint64 len;

  g_assert (data_len > 0);

  first_byte = *data++;

  if ((first_byte & 0x80) == 0) {
    *v_len = first_byte & 0x7f;
    *len_size = 1;
    return TRUE;
  }

  len_len = first_byte & 0x7f;

  if (len_len == 0 || len_len > 8)
    return FALSE;

  if ((1 + len_len) > data_len)
    return FALSE;

  *len_size = 1 + len_len;

  len = 0;
  while (len_len > 0) {
    len = len << 8 | *data++;
    --len_len;
  }

  *v_len = len;

  return TRUE;
}

static GstBuffer *
gst_rtp_klv_depay_process_data (GstRtpKlvDepay * klvdepay)
{
  gsize avail, data_len, len_size;
  GstBuffer *outbuf;
  guint8 data[1 + 8];
  guint64 v_len;

  avail = gst_adapter_available (klvdepay->adapter);

  if (avail == 0)
    return NULL;

  /* need at least 16 bytes of UL key plus 1 byte of length */
  if (avail < 16 + 1)
    goto bad_klv_packet;

  /* check if the declared KLV unit size matches actual bytes available */
  data_len = MIN (avail - 16, 1 + 8);
  gst_adapter_copy (klvdepay->adapter, data, 16, data_len);
  if (!klv_get_vlen (data, data_len, &v_len, &len_size))
    goto bad_klv_packet;

  if (avail < 16 + len_size + v_len)
    goto bad_klv_packet;

  outbuf = gst_adapter_take_buffer (klvdepay->adapter, avail);

  /* Mark buffers as key unit to signal this is the start of a KLV unit
   * (for now all buffers will be flagged like this, since all buffers are
   * self-contained KLV units, but in future that might change) */
  outbuf = gst_buffer_make_writable (outbuf);
  GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  return outbuf;

/* ERRORS */
bad_klv_packet:
  {
    GST_WARNING_OBJECT (klvdepay, "bad KLV packet, dropping");
    gst_rtp_klv_depay_reset (klvdepay);
    return NULL;
  }
}

static GstBuffer *
gst_rtp_klv_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpKlvDepay *klvdepay = GST_RTP_KLV_DEPAY (depayload);
  GstRTPBuffer rtp = { NULL };
  GstBuffer *payload, *outbuf = NULL;
  gboolean marker, start;
  guint32 rtp_ts;
  guint payload_len;

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);

  payload_len = gst_rtp_buffer_get_payload_len (&rtp);

  /* marker bit signals last fragment of a KLV unit */
  marker = gst_rtp_buffer_get_marker (&rtp);

  /* deduce start of new KLV unit in case sender doesn't set marker bits
   * (it's not like the spec is ambiguous about that, but what can you do) */
  rtp_ts = gst_rtp_buffer_get_timestamp (&rtp);
  start = (klvdepay->last_rtp_ts != -1 && klvdepay->last_rtp_ts != rtp_ts);

  klvdepay->last_rtp_ts = rtp_ts;

  /* yet another fallback to deduce start of new KLV unit */
  if (!marker && !start && payload_len > 16) {
    const guint8 *data;
    guint64 v_len;
    gsize len_size;

    data = gst_rtp_buffer_get_payload (&rtp);
    if (GST_READ_UINT32_BE (data) == 0x060e2b34 &&
        klv_get_vlen (data + 16, payload_len - 16, &v_len, &len_size) &&
        16 + len_size + v_len == payload_len) {
      GST_LOG_OBJECT (klvdepay, "Looks like we got a self-contained KLV unit");
      marker = TRUE;
    }
  }

  GST_LOG_OBJECT (klvdepay, "payload of %u bytes, marker=%d, start=%d",
      payload_len, marker, start);

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_WARNING_OBJECT (klvdepay, "DISCONT, need to resync");
    gst_rtp_klv_depay_reset (klvdepay);
    start = FALSE;
  }

  if (klvdepay->resync && !start) {
    GST_DEBUG_OBJECT (klvdepay, "Dropping buffer, waiting to resync");

    if (marker)
      klvdepay->resync = FALSE;

    goto done;
  }

  if (start && !marker)
    outbuf = gst_rtp_klv_depay_process_data (klvdepay);

  payload = gst_rtp_buffer_get_payload_buffer (&rtp);
  gst_adapter_push (klvdepay->adapter, payload);

  if (marker)
    outbuf = gst_rtp_klv_depay_process_data (klvdepay);

done:

  gst_rtp_buffer_unmap (&rtp);

  return outbuf;
}

static GstStateChangeReturn
gst_rtp_klv_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpKlvDepay *klvdepay;
  GstStateChangeReturn ret;

  klvdepay = GST_RTP_KLV_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_klv_depay_reset (klvdepay);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_klv_depay_reset (klvdepay);
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_klv_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpklvdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_KLV_DEPAY);
}
