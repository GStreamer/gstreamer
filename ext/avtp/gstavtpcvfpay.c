/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-avtpcvfpay
 * @see_also: avtpcvfdepay
 *
 * Payload compressed video (currently, only H.264) into AVTPDUs according
 * to IEEE 1722-2016. For detailed information see
 * https://standards.ieee.org/standard/1722-2016.html.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 videotestsrc ! x264enc ! avtpcvfpay ! avtpsink
 * ]| This example pipeline will payload H.264 video. Refer to the avtpcvfdepay
 * example to depayload and play the AVTP stream.
 * </refsect2>
 */

#include <avtp.h>
#include <avtp_cvf.h>

#include "gstavtpcvfpay.h"

GST_DEBUG_CATEGORY_STATIC (avtpcvfpay_debug);
#define GST_CAT_DEFAULT avtpcvfpay_debug

/* prototypes */

static GstFlowReturn gst_avtp_cvf_pay_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_avtp_cvf_pay_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void gst_avtp_cvf_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_avtp_cvf_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_avtp_cvf_change_state (GstElement *
    element, GstStateChange transition);

enum
{
  PROP_0,
  PROP_MTU,
  PROP_MEASUREMENT_INTERVAL,
  PROP_MAX_INTERVAL_FRAME
};

#define DEFAULT_MTU 1500
#define DEFAULT_MEASUREMENT_INTERVAL 250000
#define DEFAULT_MAX_INTERVAL_FRAMES 1

#define AVTP_CVF_H264_HEADER_SIZE (sizeof(struct avtp_stream_pdu) + sizeof(guint32))
#define FU_A_TYPE 28
#define FU_A_HEADER_SIZE (sizeof(guint16))

#define NRI_MASK            0x60
#define NRI_SHIFT           5
#define START_SHIFT         7
#define END_SHIFT           6
#define NAL_TYPE_MASK       0x1f
#define FIRST_NAL_VCL_TYPE  0x01
#define LAST_NAL_VCL_TYPE   0x05
#define NAL_LEN_SIZE_MASK   0x03

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) avc, alignment = (string) au")
    );

/* class initialization */

#define gst_avtp_cvf_pay_parent_class parent_class
G_DEFINE_TYPE (GstAvtpCvfPay, gst_avtp_cvf_pay, GST_TYPE_AVTP_BASE_PAYLOAD);

static void
gst_avtp_cvf_pay_class_init (GstAvtpCvfPayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAvtpBasePayloadClass *avtpbasepayload_class =
      GST_AVTP_BASE_PAYLOAD_CLASS (klass);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "AVTP Compressed Video Format (CVF) payloader",
      "Codec/Payloader/Network/AVTP",
      "Payload-encode compressed video into CVF AVTPDU (IEEE 1722)",
      "Ederson de Souza <ederson.desouza@intel.com>");

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_avtp_cvf_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_avtp_cvf_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_change_state);

  avtpbasepayload_class->chain = GST_DEBUG_FUNCPTR (gst_avtp_cvf_pay_chain);
  avtpbasepayload_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_pay_sink_event);

  g_object_class_install_property (gobject_class, PROP_MTU,
      g_param_spec_uint ("mtu", "Maximum Transit Unit",
          "Maximum Transit Unit (MTU) of underlying network in bytes", 0,
          G_MAXUINT, DEFAULT_MTU, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MEASUREMENT_INTERVAL,
      g_param_spec_uint64 ("measurement-interval", "Measurement Interval",
          "Measurement interval of stream in nanoseconds", 0,
          G_MAXUINT64, DEFAULT_MEASUREMENT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_INTERVAL_FRAME,
      g_param_spec_uint ("max-interval-frames", "Maximum Interval Frames",
          "Maximum number of network frames to be sent on each Measurement Interval",
          1, G_MAXUINT, DEFAULT_MAX_INTERVAL_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (avtpcvfpay_debug, "avtpcvfpay",
      0, "debug category for avtpcvfpay element");
}

static void
gst_avtp_cvf_pay_init (GstAvtpCvfPay * avtpcvfpay)
{
  avtpcvfpay->mtu = DEFAULT_MTU;
  avtpcvfpay->header = NULL;
  avtpcvfpay->nal_length_size = 0;
  avtpcvfpay->measurement_interval = DEFAULT_MEASUREMENT_INTERVAL;
  avtpcvfpay->max_interval_frames = DEFAULT_MAX_INTERVAL_FRAMES;
  avtpcvfpay->last_interval_ct = 0;
}

static void
gst_avtp_cvf_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (object);

  GST_DEBUG_OBJECT (avtpcvfpay, "prop_id: %u", prop_id);

  switch (prop_id) {
    case PROP_MTU:
      avtpcvfpay->mtu = g_value_get_uint (value);
      break;
    case PROP_MEASUREMENT_INTERVAL:
      avtpcvfpay->measurement_interval = g_value_get_uint64 (value);
      break;
    case PROP_MAX_INTERVAL_FRAME:
      avtpcvfpay->max_interval_frames = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avtp_cvf_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (object);

  GST_DEBUG_OBJECT (avtpcvfpay, "prop_id: %u", prop_id);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, avtpcvfpay->mtu);
      break;
    case PROP_MEASUREMENT_INTERVAL:
      g_value_set_uint64 (value, avtpcvfpay->measurement_interval);
      break;
    case PROP_MAX_INTERVAL_FRAME:
      g_value_set_uint (value, avtpcvfpay->max_interval_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_avtp_cvf_change_state (GstElement * element, GstStateChange transition)
{
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (element);
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpcvfpay);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    GstMapInfo map;
    struct avtp_stream_pdu *pdu;
    int res;

    avtpcvfpay->header = gst_buffer_new_allocate (NULL,
        AVTP_CVF_H264_HEADER_SIZE, NULL);
    if (avtpcvfpay->header == NULL) {
      GST_ERROR_OBJECT (avtpcvfpay, "Could not allocate buffer");
      return GST_STATE_CHANGE_FAILURE;
    }

    gst_buffer_map (avtpcvfpay->header, &map, GST_MAP_WRITE);
    pdu = (struct avtp_stream_pdu *) map.data;

    res = avtp_cvf_pdu_init (pdu, AVTP_CVF_FORMAT_SUBTYPE_H264);
    g_assert (res == 0);

    res =
        avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_STREAM_ID,
        avtpbasepayload->streamid);
    g_assert (res == 0);

    gst_buffer_unmap (avtpcvfpay->header, &map);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    gst_buffer_unref (avtpcvfpay->header);
  }

  return ret;
}

static void
gst_avtp_cvf_pay_extract_nals (GstAvtpCvfPay * avtpcvfpay,
    GstBuffer * buffer, GPtrArray * nals)
{
  /* The buffer may have more than one NAL. They are grouped together, and before
   * each NAL there are some bytes that indicate how big is the NAL */

  gsize size, offset = 0;
  GstMapInfo map;
  guint8 *data;
  gboolean res;

  if (G_UNLIKELY (avtpcvfpay->nal_length_size == 0)) {
    GST_ERROR_OBJECT (avtpcvfpay,
        "Can't extract NAL units without nal length size. Missing codec_data caps?");
    goto end;
  }

  res = gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (!res) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not map buffer");
    goto end;
  }

  size = map.size;
  data = map.data;

  while (size > avtpcvfpay->nal_length_size) {
    gint i;
    guint nal_len = 0;
    GstBuffer *nal;

    /* Gets NAL length */
    for (i = 0; i < avtpcvfpay->nal_length_size; i++) {
      nal_len = (nal_len << 8) + data[i];
    }

    if (nal_len == 0) {
      GST_WARNING_OBJECT (avtpcvfpay, "Invalid NAL unit size: 0");
      break;
    }

    offset += avtpcvfpay->nal_length_size;
    data += avtpcvfpay->nal_length_size;
    size -= avtpcvfpay->nal_length_size;

    if (G_UNLIKELY (size < nal_len)) {
      GST_WARNING_OBJECT (avtpcvfpay,
          "Got incomplete NAL: NAL len %u, buffer len %zu", nal_len, size);
      nal_len = size;
    }

    nal = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, offset, nal_len);
    GST_BUFFER_PTS (nal) = GST_BUFFER_PTS (buffer);
    GST_BUFFER_DTS (nal) = GST_BUFFER_DTS (buffer);
    g_ptr_array_add (nals, nal);

    offset += nal_len;
    data += nal_len;
    size -= nal_len;
  }

  gst_buffer_unmap (buffer, &map);

end:
  /* This function consumes the buffer, and all references to it are in the
   * extracted nals, so we can release the reference to the buffer itself */
  gst_buffer_unref (buffer);

  GST_LOG_OBJECT (avtpcvfpay, "Extracted %u NALu's from buffer", nals->len);
}

static gboolean
gst_avtp_cvf_pay_is_nal_vcl (GstAvtpCvfPay * avtpcvfpay, GstBuffer * nal)
{
  guint8 nal_header, nal_type;

  gst_buffer_extract (nal, 0, &nal_header, 1);
  nal_type = nal_header & NAL_TYPE_MASK;

  return nal_type >= FIRST_NAL_VCL_TYPE && nal_type <= LAST_NAL_VCL_TYPE;
}

static GstBuffer *
gst_avtpcvpay_fragment_nal (GstAvtpCvfPay * avtpcvfpay, GstBuffer * nal,
    gsize * offset, gboolean * last_fragment)
{
  GstBuffer *fragment_header, *fragment;
  guint8 nal_header, nal_type, nal_nri, fu_indicator, fu_header;
  gsize available, nal_size, fragment_size, remaining;
  GstMapInfo map;

  nal_size = gst_buffer_get_size (nal);

  /* If NAL + header will be smaller than MTU, nothing to fragment */
  if (*offset == 0 && (nal_size + AVTP_CVF_H264_HEADER_SIZE) <= avtpcvfpay->mtu) {
    *last_fragment = TRUE;
    *offset = nal_size;
    GST_DEBUG_OBJECT (avtpcvfpay, "Generated fragment with size %lu", nal_size);
    return gst_buffer_ref (nal);
  }

  /* We're done with this buffer */
  if (*offset == nal_size) {
    return NULL;
  }

  *last_fragment = FALSE;

  /* Remaining size is smaller than MTU, so this is the last fragment */
  remaining = nal_size - *offset + AVTP_CVF_H264_HEADER_SIZE + FU_A_HEADER_SIZE;
  if (remaining <= avtpcvfpay->mtu) {
    *last_fragment = TRUE;
  }

  fragment_header = gst_buffer_new_allocate (NULL, FU_A_HEADER_SIZE, NULL);
  if (G_UNLIKELY (fragment_header == NULL)) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not allocate memory for buffer");
    return NULL;
  }

  /* NAL header info is spread to all FUs */
  gst_buffer_extract (nal, 0, &nal_header, 1);
  nal_type = nal_header & NAL_TYPE_MASK;
  nal_nri = (nal_header & NRI_MASK) >> NRI_SHIFT;

  fu_indicator = (nal_nri << NRI_SHIFT) | FU_A_TYPE;
  fu_header = ((*offset == 0) << START_SHIFT) |
      ((*last_fragment == TRUE) << END_SHIFT) | nal_type;

  gst_buffer_map (fragment_header, &map, GST_MAP_WRITE);
  map.data[0] = fu_indicator;
  map.data[1] = fu_header;
  gst_buffer_unmap (fragment_header, &map);

  available =
      avtpcvfpay->mtu - AVTP_CVF_H264_HEADER_SIZE -
      gst_buffer_get_size (fragment_header);

  /* NAL unit header is not sent, but spread into FU indicator and header,
   * and reconstructed on depayloader */
  if (*offset == 0)
    *offset = 1;

  fragment_size =
      available < (nal_size - *offset) ? available : (nal_size - *offset);

  fragment =
      gst_buffer_append (fragment_header, gst_buffer_copy_region (nal,
          GST_BUFFER_COPY_MEMORY, *offset, fragment_size));

  *offset += fragment_size;

  GST_DEBUG_OBJECT (avtpcvfpay, "Generated fragment with size %lu",
      fragment_size);

  return fragment;
}

static void
gst_avtp_cvf_pay_spread_ts (GstAvtpCvfPay * avtpcvfpay,
    GPtrArray * avtp_packets)
{
  /* A bit of the idea of what this function do:
   *
   * After fragmenting the NAL unit, we have a series of AVTPDUs (AVTP Data Units)
   * that should be transmitted. They are going to be transmitted according to GstBuffer
   * DTS (or PTS in case there's no DTS), but all of them have the same DTS, as they
   * came from the same original NAL unit.
   *
   * However, TSN streams should send their data according to a "measurement interval",
   * which is an arbitrary interval defined for the stream. For instance, a class A
   * stream has measurement interval of 125us. Also, there's a MaxIntervalFrames
   * parameter, that defines how many network frames can be sent on a given measurement
   * interval. We also spread MaxIntervalFrames per measurement interval.
   *
   * To that end, this function will spread the DTS so that fragments follow measurement
   * interval and MaxIntervalFrames, adjusting them to end before the actual DTS of the
   * original NAL unit.
   *
   * Roughly, this function does:
   *
   *  DTSn = DTSbase - (measurement_interval/MaxIntervalFrames) * (total - n - 1)
   *
   * Where:
   *  DTSn = DTS of nth fragment
   *  DTSbase = DTS of original NAL unit
   *  total = # of fragments
   *
   * Another issue that this function takes care of is avoiding DTSs that overlap between
   * two different set of fragments. Assuming DTSlast is the DTS of the last fragment
   * generated on previous call to this function, we don't want any DTSn for the current
   * call to be smaller than DTSlast + (measurement_interval / MaxIntervalFrames). If
   * that's the case, we adjust DTSbase to preserve this difference (so we don't schedule
   * packets transmission times that violate stream spec). This will cause the last
   * fragment DTS to be bigger than DTSbase - we emit a warning, as this may be a sign
   * of a bad pipeline setup or inappropriate stream spec.
   *
   * Finally, we also avoid underflows - which would occur when DTSbase is zero or small
   * enough. In this case, we'll again make last fragment DTS > DTSbase, so we log it.
   *
   */

  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpcvfpay);

  gint i, ret;
  guint len;
  guint64 tx_interval, total_interval;
  GstClockTime base_time, base_dts, rt;
  GstBuffer *packet;

  base_time = gst_element_get_base_time (GST_ELEMENT (avtpcvfpay));
  base_dts = GST_BUFFER_DTS (g_ptr_array_index (avtp_packets, 0));

  tx_interval =
      avtpcvfpay->measurement_interval / avtpcvfpay->max_interval_frames;
  len = avtp_packets->len;
  total_interval = tx_interval * (len - 1);

  /* We don't want packets transmission time to overlap, so let's ensure
   * packets are scheduled after last interval used */
  if (avtpcvfpay->last_interval_ct != 0) {
    GstClockTime dts_ct, dts_rt;

    ret =
        gst_segment_to_running_time_full (&avtpbasepayload->segment,
        GST_FORMAT_TIME, base_dts, &dts_rt);
    if (ret == -1)
      dts_rt = -dts_rt;

    dts_ct = base_time + dts_rt;

    if (dts_ct < avtpcvfpay->last_interval_ct + total_interval + tx_interval) {
      base_dts +=
          avtpcvfpay->last_interval_ct + total_interval + tx_interval - dts_ct;

      GST_WARNING_OBJECT (avtpcvfpay,
          "Not enough measurements intervals between frames to transmit fragments"
          ". Check stream transmission spec.");
    }
  }

  /* Not enough room to spread tx before DTS (or we would underflow),
   * add offset */
  if (total_interval > base_dts) {
    base_dts += total_interval - base_dts;

    GST_INFO_OBJECT (avtpcvfpay,
        "Not enough measurements intervals to transmit fragments before base "
        "DTS. Check pipeline settings. Are we live?");
  }

  for (i = 0; i < len; i++) {
    packet = g_ptr_array_index (avtp_packets, i);
    GST_BUFFER_DTS (packet) = base_dts - tx_interval * (len - i - 1);
  }

  /* Remember last interval used, in clock time */
  ret =
      gst_segment_to_running_time_full (&avtpbasepayload->segment,
      GST_FORMAT_TIME, GST_BUFFER_DTS (g_ptr_array_index (avtp_packets,
              avtp_packets->len - 1)), &rt);
  if (ret == -1)
    rt = -rt;
  avtpcvfpay->last_interval_ct = base_time + rt;
}

static gboolean
gst_avtp_cvf_pay_prepare_avtp_packets (GstAvtpCvfPay * avtpcvfpay,
    GPtrArray * nals, GPtrArray * avtp_packets)
{
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpcvfpay);
  GstBuffer *header, *nal;
  GstMapInfo map;
  gint i;

  for (i = 0; i < nals->len; i++) {
    guint64 avtp_time, h264_time;
    gboolean last_fragment;
    GstBuffer *fragment;
    gsize offset;

    nal = g_ptr_array_index (nals, i);
    GST_LOG_OBJECT (avtpcvfpay,
        "Preparing AVTP packets for NAL whose size is %lu",
        gst_buffer_get_size (nal));

    /* Calculate timestamps. Note that we do it twice, one using DTS as base,
     * the other using PTS - using code inherited from avtpbasepayload.
     * Also worth noting: `avtpbasepayload->latency` is updated after
     * first call to gst_avtp_base_payload_calc_ptime, so we MUST call
     * it before using the latency value */
    h264_time = gst_avtp_base_payload_calc_ptime (avtpbasepayload, nal);

    avtp_time =
        gst_element_get_base_time (GST_ELEMENT (avtpcvfpay)) +
        gst_segment_to_running_time (&avtpbasepayload->segment, GST_FORMAT_TIME,
        GST_BUFFER_DTS_OR_PTS (nal)) + avtpbasepayload->mtt +
        avtpbasepayload->tu + avtpbasepayload->processing_deadline +
        avtpbasepayload->latency;

    offset = 0;
    while ((fragment =
            gst_avtpcvpay_fragment_nal (avtpcvfpay, nal, &offset,
                &last_fragment))) {
      GstBuffer *packet;
      struct avtp_stream_pdu *pdu;
      gint res;

      /* Copy header to reuse common fields and change what is needed */
      header = gst_buffer_copy (avtpcvfpay->header);
      gst_buffer_map (header, &map, GST_MAP_WRITE);
      pdu = (struct avtp_stream_pdu *) map.data;

      /* Stream data len includes AVTP H264 header len as this is part of
       * the payload too. It's just the uint32_t with the h264 timestamp*/
      res =
          avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN,
          gst_buffer_get_size (fragment) + sizeof (uint32_t));
      g_assert (res == 0);

      res =
          avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_SEQ_NUM,
          avtpbasepayload->seqnum++);
      g_assert (res == 0);

      /* Although AVTP_TIMESTAMP is only set on the very last fragment, IEEE 1722
       * doesn't mention such need for H264_TIMESTAMP. So, we set it for all
       * fragments */
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_H264_TIMESTAMP, h264_time);
      g_assert (res == 0);
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_H264_PTV, 1);
      g_assert (res == 0);

      /* Only last fragment should have M, AVTP_TS and TV fields set */
      if (last_fragment) {
        gboolean M;

        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TV, 1);
        g_assert (res == 0);

        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TIMESTAMP, avtp_time);
        g_assert (res == 0);

        /* Set M only if last NAL and it is a VCL NAL */
        M = (i == nals->len - 1)
            && gst_avtp_cvf_pay_is_nal_vcl (avtpcvfpay, nal);
        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_M, M);
        g_assert (res == 0);

        if (M) {
          GST_LOG_OBJECT (avtpcvfpay, "M packet sent, PTS: %" GST_TIME_FORMAT
              " DTS: %" GST_TIME_FORMAT " AVTP_TS: %" GST_TIME_FORMAT
              " H264_TS: %" GST_TIME_FORMAT "\navtp_time: %lu h264_time: %lu",
              GST_TIME_ARGS (h264_time),
              GST_TIME_ARGS (avtp_time), GST_TIME_ARGS ((guint32) avtp_time),
              GST_TIME_ARGS ((guint32) h264_time), avtp_time, h264_time);
        }
      }

      packet = gst_buffer_append (header, fragment);

      /* Keep original timestamps */
      GST_BUFFER_PTS (packet) = GST_BUFFER_PTS (nal);
      GST_BUFFER_DTS (packet) = GST_BUFFER_DTS (nal);

      g_ptr_array_add (avtp_packets, packet);

      gst_buffer_unmap (header, &map);
    }

    gst_buffer_unref (nal);
  }

  GST_LOG_OBJECT (avtpcvfpay, "Prepared %u AVTP packets", avtp_packets->len);

  /* Ensure DTS/PTS respect stream transmit spec, so PDUs are transmitted
   * according to measurement interval. */
  if (avtp_packets->len > 0)
    gst_avtp_cvf_pay_spread_ts (avtpcvfpay, avtp_packets);

  return TRUE;
}

static GstFlowReturn
gst_avtp_cvf_pay_push_packets (GstAvtpCvfPay * avtpcvfpay,
    GPtrArray * avtp_packets)
{
  int i;
  GstFlowReturn ret;
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpcvfpay);

  for (i = 0; i < avtp_packets->len; i++) {
    GstBuffer *packet;

    packet = g_ptr_array_index (avtp_packets, i);
    ret = gst_pad_push (avtpbasepayload->srcpad, packet);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_avtp_cvf_pay_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (parent);
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpbasepayload);
  GPtrArray *nals, *avtp_packets;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (avtpcvfpay,
      "Incoming buffer size: %lu PTS: %" GST_TIME_FORMAT " DTS: %"
      GST_TIME_FORMAT, gst_buffer_get_size (buffer),
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));

  /* Get all NALs inside buffer */
  nals = g_ptr_array_new ();
  gst_avtp_cvf_pay_extract_nals (avtpcvfpay, buffer, nals);

  /* Prepare a list of avtp_packets to send */
  avtp_packets = g_ptr_array_new ();
  gst_avtp_cvf_pay_prepare_avtp_packets (avtpcvfpay, nals, avtp_packets);

  ret = gst_avtp_cvf_pay_push_packets (avtpcvfpay, avtp_packets);

  /* Contents of both ptr_arrays should be unref'd or transferred
   * to rightful owner by this point, no need to unref them again */
  g_ptr_array_free (nals, TRUE);
  g_ptr_array_free (avtp_packets, TRUE);

  return ret;
}

static gboolean
gst_avtp_cvf_pay_new_caps (GstAvtpCvfPay * avtpcvfpay, GstCaps * caps)
{
  const GValue *value;
  GstStructure *str;
  GstBuffer *buffer;
  GstMapInfo map;

  str = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (str, "codec_data"))) {
    guint8 *data;
    gsize size;

    buffer = gst_value_get_buffer (value);
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    if (G_UNLIKELY (size < 7)) {
      GST_ERROR_OBJECT (avtpcvfpay, "avcC size %" G_GSIZE_FORMAT " < 7", size);
      goto error;
    }
    if (G_UNLIKELY (data[0] != 1)) {
      GST_ERROR_OBJECT (avtpcvfpay, "avcC version %u != 1", data[0]);
      goto error;
    }

    /* Number of bytes in front of NAL units marking their size */
    avtpcvfpay->nal_length_size = (data[4] & NAL_LEN_SIZE_MASK) + 1;
    GST_DEBUG_OBJECT (avtpcvfpay, "Got NAL length from caps: %u",
        avtpcvfpay->nal_length_size);

    gst_buffer_unmap (buffer, &map);
  }

  return TRUE;

error:
  gst_buffer_unmap (buffer, &map);
  return FALSE;
}

static gboolean
gst_avtp_cvf_pay_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstCaps *caps;
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (parent);
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpbasepayload);
  gboolean ret;

  GST_DEBUG_OBJECT (avtpcvfpay, "Sink event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      ret = gst_avtp_cvf_pay_new_caps (avtpcvfpay, caps);
      gst_event_unref (event);
      return ret;
    case GST_EVENT_FLUSH_STOP:
      if (GST_ELEMENT (avtpcvfpay)->current_state == GST_STATE_PLAYING) {
        /* After a flush, the sink will reset pipeline base_time, but only
         * after it gets the first buffer. So, here, we used the wrong
         * base_time to calculate DTS. We'll just notice base_time changed
         * when we get the next buffer. So, we'll basically mess with
         * timestamps of two frames, which is bad. Known workaround is
         * to pause the pipeline before a flushing seek - so that we'll
         * be up to date to new pipeline base_time */
        GST_WARNING_OBJECT (avtpcvfpay,
            "Flushing seek performed while pipeline is PLAYING, "
            "AVTP timestamps will be incorrect!");
      }
      break;
    default:
      break;
  }

  return GST_AVTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (pad, parent,
      event);
}

gboolean
gst_avtp_cvf_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avtpcvfpay", GST_RANK_NONE,
      GST_TYPE_AVTP_CVF_PAY);
}
