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
 * SECTION:element-avtpcvfdepay
 * @see_also: avtpcvfpay
 *
 * De-payload CVF AVTPDUs into compressed video (currently, only H.264) according
 * to IEEE 1722-2016. For detailed information see
 * https://standards.ieee.org/standard/1722-2016.html.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 avtpsrc ! avtpcvfdepay ! decodebin ! videoconvert ! autovideosink
 * ]| This example pipeline will de-payload H.264 video from the AVTPDUs, decode
 * and play them. Refer to the avtpcvfpay example to payload H.264 and send the
 * AVTP stream.
 * </refsect2>
 */

#include <avtp.h>
#include <avtp_cvf.h>
#include <gst/audio/audio-format.h>
#include <arpa/inet.h>

#include "gstavtpcvfdepay.h"

GST_DEBUG_CATEGORY_STATIC (avtpcvfdepay_debug);
#define GST_CAT_DEFAULT avtpcvfdepay_debug

/* prototypes */

static GstFlowReturn gst_avtp_cvf_depay_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_avtp_cvf_depay_change_state (GstElement *
    element, GstStateChange transition);

#define AVTP_CVF_H264_HEADER_SIZE (sizeof(struct avtp_stream_pdu) + sizeof(guint32))
#define FU_A_HEADER_SIZE (sizeof(guint16))
#define STAP_A_TYPE 24
#define STAP_B_TYPE 25
#define MTAP16_TYPE 26
#define MTAP24_TYPE 27
#define FU_A_TYPE   28
#define FU_B_TYPE   29

#define NRI_MASK        0x60
#define NRI_SHIFT       5
#define START_MASK      0x80
#define START_SHIFT     7
#define END_MASK        0x40
#define END_SHIFT       6
#define NAL_TYPE_MASK   0x1f

/* pad templates */

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264,"
        "  stream-format = (string) avc, alignment = (string) au")
    );

#define gst_avtp_cvf_depay_parent_class parent_class
G_DEFINE_TYPE (GstAvtpCvfDepay, gst_avtp_cvf_depay,
    GST_TYPE_AVTP_BASE_DEPAYLOAD);

static void
gst_avtp_cvf_depay_class_init (GstAvtpCvfDepayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAvtpBaseDepayloadClass *avtpbasedepayload_class =
      GST_AVTP_BASE_DEPAYLOAD_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "AVTP Compressed Video Format (CVF) depayloader",
      "Codec/Depayloader/Network/AVTP",
      "Extracts compressed video from CVF AVTPDUs",
      "Ederson de Souza <ederson.desouza@intel.com>");

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_depay_change_state);

  avtpbasedepayload_class->chain = GST_DEBUG_FUNCPTR (gst_avtp_cvf_depay_chain);

  GST_DEBUG_CATEGORY_INIT (avtpcvfdepay_debug, "avtpcvfdepay",
      0, "debug category for avtpcvfdepay element");
}

static void
gst_avtp_cvf_depay_init (GstAvtpCvfDepay * avtpcvfdepay)
{
  avtpcvfdepay->out_buffer = NULL;
  avtpcvfdepay->fragments = NULL;
  avtpcvfdepay->seqnum = 0;
}

static GstStateChangeReturn
gst_avtp_cvf_depay_change_state (GstElement *
    element, GstStateChange transition)
{
  GstAvtpCvfDepay *avtpcvfdepay = GST_AVTP_CVF_DEPAY (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    if (avtpcvfdepay->out_buffer) {
      gst_buffer_unref (avtpcvfdepay->out_buffer);
      avtpcvfdepay->out_buffer = NULL;
    }
  }

  return ret;
}

static gboolean
gst_avtp_cvf_depay_push_caps (GstAvtpCvfDepay * avtpcvfdepay)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  GstBuffer *codec_data;
  GstEvent *event;
  GstMapInfo map;
  GstCaps *caps;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Setting src pad caps");

  /* Send simple codec data, with only the NAL size len, no SPS/PPS.
   * Below, 7 is the minimal codec_data size, when no SPS/PPS is sent */
  codec_data = gst_buffer_new_allocate (NULL, 7, NULL);
  gst_buffer_map (codec_data, &map, GST_MAP_READWRITE);

  memset (map.data, 0, map.size);
  map.data[0] = 1;              /* version */
  map.data[4] = 0x03 | 0xfc;    /* nal len size (4) - 1. Other 6 bits are 1 */
  map.data[5] = 0xe0;           /* first 3 bits are 1 */
  gst_buffer_unmap (codec_data, &map);

  caps = gst_pad_get_pad_template_caps (avtpbasedepayload->srcpad);
  caps = gst_caps_make_writable (caps);
  gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);

  event = gst_event_new_caps (caps);

  gst_buffer_unref (codec_data);
  gst_caps_unref (caps);

  return gst_pad_push_event (avtpbasedepayload->srcpad, event);
}

static GstFlowReturn
gst_avtp_cvf_depay_push (GstAvtpCvfDepay * avtpcvfdepay)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  GstFlowReturn ret;

  if (G_UNLIKELY (!gst_pad_has_current_caps (avtpbasedepayload->srcpad))) {
    guint64 pts_m;
    guint32 dts, pts;

    if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG) {
      GstClock *clock = gst_element_get_clock (GST_ELEMENT_CAST (avtpcvfdepay));
      if (clock == NULL) {
        GST_DEBUG_OBJECT (avtpcvfdepay,
            "Sending initial CAPS and SEGMENT, no pipeline time.");
      } else {
        GST_DEBUG_OBJECT (avtpcvfdepay,
            "Sending initial CAPS and SEGMENT, pipeline time: %"
            GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
      }
    }

    if (!gst_avtp_cvf_depay_push_caps (avtpcvfdepay)) {
      GST_ELEMENT_ERROR (avtpcvfdepay, CORE, CAPS, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }

    if (!gst_avtp_base_depayload_push_segment_event (avtpbasedepayload,
            GST_BUFFER_PTS (avtpcvfdepay->out_buffer))) {
      GST_ELEMENT_ERROR (avtpcvfdepay, CORE, EVENT,
          ("Could not send SEGMENT event"), (NULL));
    }

    /* Now that we sent our segment starting on the first Presentation
     * time available, `avtpbasedepayload->prev_ptime` saves that value,
     * to be a reference for calculating future buffer timestamps from
     * the AVTP timestamps (avtp_ts and h264_ts).
     *
     * However, decode timestamps can be smaller than presentation
     * timestamps. So we can't use `avtpbasedepayload->prev_time` as
     * reference to calculate them. Instead, here, we calculate the
     * first decode timestamp and save it on `avtpcvfdepay->prev_ptime`.
     *
     * The method used to calculate the "absolute" decode timestamp (DTS)
     * from presentation timestamp is as follows:
     *
     *   DTS = dts > pts ? (PTSm - 1) | dts : PTSm | dts
     *
     * Where:
     *   dts: 32 bits gPTP decode timestamp
     *   pts: 32 bits gPTP presentation timestamp
     *   PTSm: 32 most signifactive bits of the "absolute" presentation
     *   timestamp
     *
     * This allow us to handle cases where the pts ends up being smaller
     * than dts due pts falling after an AVTP timestamp wrapping.
     */

    pts = GST_BUFFER_PTS (avtpcvfdepay->out_buffer);
    dts = GST_BUFFER_DTS (avtpcvfdepay->out_buffer);
    pts_m = avtpbasedepayload->prev_ptime & 0xFFFFFFFF00000000ULL;

    avtpbasedepayload->prev_ptime = dts > pts ? (pts_m -=
        (1ULL << 32)) | dts : pts_m | dts;
    GST_DEBUG_OBJECT (avtpcvfdepay, "prev_ptime set to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (avtpbasedepayload->prev_ptime));
  }

  /* At this point, we're sure segment was sent, so we can properly calc
   * buffer timestamps */
  GST_DEBUG_OBJECT (avtpcvfdepay, "Converting %" GST_TIME_FORMAT " to PTS",
      GST_TIME_ARGS (GST_BUFFER_PTS (avtpcvfdepay->out_buffer)));
  GST_BUFFER_PTS (avtpcvfdepay->out_buffer) =
      gst_avtp_base_depayload_tstamp_to_ptime (avtpbasedepayload, GST_BUFFER_PTS
      (avtpcvfdepay->out_buffer), avtpbasedepayload->prev_ptime);

  GST_DEBUG_OBJECT (avtpcvfdepay, "Converting %" GST_TIME_FORMAT " to DTS",
      GST_TIME_ARGS (GST_BUFFER_DTS (avtpcvfdepay->out_buffer)));
  GST_BUFFER_DTS (avtpcvfdepay->out_buffer) =
      gst_avtp_base_depayload_tstamp_to_ptime (avtpbasedepayload, GST_BUFFER_DTS
      (avtpcvfdepay->out_buffer), avtpbasedepayload->prev_ptime);

  /* Use DTS as prev_ptime as it is smaller or equal to PTS, so that
   * next calculations of PTS/DTS won't wrap too early */
  avtpbasedepayload->prev_ptime = GST_BUFFER_DTS (avtpcvfdepay->out_buffer);

  ret = gst_pad_push (GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay)->srcpad,
      avtpcvfdepay->out_buffer);
  avtpcvfdepay->out_buffer = NULL;

  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_push_and_discard (GstAvtpCvfDepay * avtpcvfdepay)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* Push everything we have, hopefully decoder can handle it */
  if (avtpcvfdepay->out_buffer != NULL) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Pushing incomplete buffers");

    ret = gst_avtp_cvf_depay_push (avtpcvfdepay);
  }

  /* Discard any incomplete fragments */
  if (avtpcvfdepay->fragments != NULL) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Discarding incomplete fragments");
    gst_buffer_unref (avtpcvfdepay->fragments);
    avtpcvfdepay->fragments = NULL;
  }

  return ret;
}

static gboolean
gst_avtp_cvf_depay_validate_avtpdu (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, gboolean * lost_packet)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  struct avtp_stream_pdu *pdu;
  gboolean result = FALSE;
  guint64 val;
  guint val32;
  gint r;

  if (G_UNLIKELY (map->size < AVTP_CVF_H264_HEADER_SIZE)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Incomplete AVTP header, expected it to have size of %zd, got %zd",
        AVTP_CVF_H264_HEADER_SIZE, map->size);
    goto end;
  }

  pdu = (struct avtp_stream_pdu *) map->data;

  r = avtp_pdu_get ((struct avtp_common_pdu *) pdu, AVTP_FIELD_SUBTYPE, &val32);
  g_assert (r == 0);
  if (val32 != AVTP_SUBTYPE_CVF) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header subtype %d, expected %d", val32,
        AVTP_SUBTYPE_CVF);
    goto end;
  }

  r = avtp_pdu_get ((struct avtp_common_pdu *) pdu, AVTP_FIELD_VERSION, &val32);
  g_assert (r == 0);
  if (G_UNLIKELY (val32 != 0)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header version %d, expected %d", val32, 0);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_SV, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != 1)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header stream valid %ld, expected %d", val, 1);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_ID, &val);
  g_assert (r == 0);
  if (val != avtpbasedepayload->streamid) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header stream id 0x%lx, expected 0x%lx", val,
        avtpbasedepayload->streamid);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_FORMAT, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != AVTP_CVF_FORMAT_RFC)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header format %ld, expected %d", val,
        AVTP_CVF_FORMAT_RFC);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_FORMAT_SUBTYPE, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != AVTP_CVF_FORMAT_SUBTYPE_H264)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unsupported AVTP header format subtype %ld", val);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (map->size < sizeof (*pdu) + val)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "AVTP packet size %ld too small, expected at least %lu",
        map->size - AVTP_CVF_H264_HEADER_SIZE, sizeof (*pdu) + val);
    goto end;
  }

  *lost_packet = FALSE;
  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_SEQ_NUM, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != avtpcvfdepay->seqnum)) {
    GST_INFO_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header seq num %lu, expected %u", val,
        avtpcvfdepay->seqnum);

    avtpcvfdepay->seqnum = val;
    /* This is not a reason to drop the packet, but it may be a good moment
     * to push everything we have - maybe we lost the M packet? */
    *lost_packet = TRUE;
  }
  avtpcvfdepay->seqnum++;

  result = TRUE;

end:
  return result;
}

static guint8
gst_avtp_cvf_depay_get_nal_type (GstMapInfo * map)
{
  struct avtp_stream_pdu *pdu;
  struct avtp_cvf_h264_payload *pay;
  guint8 nal_header, nal_type;

  pdu = (struct avtp_stream_pdu *) map->data;
  pay = (struct avtp_cvf_h264_payload *) pdu->avtp_payload;
  nal_header = pay->h264_data[0];
  nal_type = nal_header & NAL_TYPE_MASK;

  return nal_type;
}

static void
gst_avtp_cvf_depay_get_avtp_timestamps (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, GstClockTime * pts, GstClockTime * dts)
{
  struct avtp_stream_pdu *pdu;
  guint64 avtp_time, h264_time, tv, ptv;
  gint res;

  *pts = GST_CLOCK_TIME_NONE;
  *dts = GST_CLOCK_TIME_NONE;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TV, &tv);
  g_assert (res == 0);

  if (tv == 1) {
    res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TIMESTAMP, &avtp_time);
    g_assert (res == 0);

    *dts = avtp_time;
  }

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_H264_PTV, &ptv);
  g_assert (res == 0);

  if (ptv == 1) {
    res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_H264_TIMESTAMP, &h264_time);
    g_assert (res == 0);

    *pts = h264_time;
  }
}

static GstFlowReturn
gst_avtp_cvf_depay_internal_push (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * buffer, gboolean M)
{
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (avtpcvfdepay,
      "Adding buffer of size %lu (nalu size %lu) to out_buffer",
      gst_buffer_get_size (buffer),
      gst_buffer_get_size (buffer) - sizeof (guint32));

  if (avtpcvfdepay->out_buffer) {
    avtpcvfdepay->out_buffer =
        gst_buffer_append (avtpcvfdepay->out_buffer, buffer);
  } else {
    avtpcvfdepay->out_buffer = buffer;
  }

  /* We only truly push to decoder when we get the last video buffer */
  if (M) {
    ret = gst_avtp_cvf_depay_push (avtpcvfdepay);
  }

  return ret;
}

static void
gst_avtp_cvf_depay_get_M (GstAvtpCvfDepay * avtpcvfdepay, GstMapInfo * map,
    gboolean * M)
{
  struct avtp_stream_pdu *pdu;
  guint64 val;
  gint res;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_M, &val);
  g_assert (res == 0);

  *M = val;
}

static void
gst_avtp_cvf_depay_get_nalu_size (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, guint16 * nalu_size)
{
  struct avtp_stream_pdu *pdu;
  guint64 val;
  gint res;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN, &val);
  g_assert (res == 0);

  /* We need to discount the H.264 header field */
  *nalu_size = val - sizeof (guint32);
}

static GstFlowReturn
gst_avtp_cvf_depay_process_last_fragment (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map, gsize offset, gsize nalu_size,
    guint8 nri, guint8 nal_type)
{
  GstBuffer *nal;
  GstMapInfo map_nal;
  GstClockTime pts, dts;
  gboolean M;
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_UNLIKELY (avtpcvfdepay->fragments == NULL)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Received final fragment, but no start fragment received. Dropping it.");
    goto end;
  }

  gst_buffer_copy_into (avtpcvfdepay->fragments, avtpdu,
      GST_BUFFER_COPY_MEMORY, offset, nalu_size);

  /* Allocate buffer to keep the nal_header (1 byte) and the NALu size (4 bytes) */
  nal = gst_buffer_new_allocate (NULL, 4 + 1, NULL);
  if (G_UNLIKELY (nal == NULL)) {
    GST_ERROR_OBJECT (avtpcvfdepay, "Could not allocate buffer");
    ret = GST_FLOW_ERROR;
    goto end;
  }

  gst_buffer_map (nal, &map_nal, GST_MAP_READWRITE);
  /* Add NAL size. Extra 1 counts the nal_header */
  nalu_size = gst_buffer_get_size (avtpcvfdepay->fragments) + 1;
  map_nal.data[0] = nalu_size >> 24;
  map_nal.data[1] = nalu_size >> 16;
  map_nal.data[2] = nalu_size >> 8;
  map_nal.data[3] = nalu_size;

  /* Finally, add the nal_header */
  map_nal.data[4] = (nri << 5) | nal_type;

  gst_buffer_unmap (nal, &map_nal);

  nal = gst_buffer_append (nal, avtpcvfdepay->fragments);

  gst_avtp_cvf_depay_get_avtp_timestamps (avtpcvfdepay, map, &pts, &dts);
  GST_BUFFER_PTS (nal) = pts;
  GST_BUFFER_DTS (nal) = dts;

  gst_avtp_cvf_depay_get_M (avtpcvfdepay, map, &M);
  ret = gst_avtp_cvf_depay_internal_push (avtpcvfdepay, nal, M);

  avtpcvfdepay->fragments = NULL;

end:
  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_handle_fu_a (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map)
{
  GstFlowReturn ret = GST_FLOW_OK;
  struct avtp_stream_pdu *pdu;
  struct avtp_cvf_h264_payload *pay;
  guint8 fu_header, fu_indicator, nal_type, start, end, nri;
  guint16 nalu_size;
  gsize offset;

  if (G_UNLIKELY (map->size - AVTP_CVF_H264_HEADER_SIZE < 2)) {
    GST_ERROR_OBJECT (avtpcvfdepay,
        "Buffer too small to contain fragment headers, size: %lu",
        map->size - AVTP_CVF_H264_HEADER_SIZE);
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    goto end;
  }

  pdu = (struct avtp_stream_pdu *) map->data;
  pay = (struct avtp_cvf_h264_payload *) pdu->avtp_payload;
  fu_indicator = pay->h264_data[0];
  nri = (fu_indicator & NRI_MASK) >> NRI_SHIFT;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Fragment indicator - NRI: %u", nri);

  fu_header = pay->h264_data[1];
  nal_type = fu_header & NAL_TYPE_MASK;
  start = (fu_header & START_MASK) >> START_SHIFT;
  end = (fu_header & END_MASK) >> END_SHIFT;

  GST_DEBUG_OBJECT (avtpcvfdepay,
      "Fragment header - type: %u start: %u end: %u", nal_type, start, end);

  if (G_UNLIKELY (start && end)) {
    GST_ERROR_OBJECT (avtpcvfdepay,
        "Invalid fragment header - 'start' and 'end' bits set");
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    goto end;
  }

  /* Size and offset also ignores the FU_HEADER and FU_INDICATOR fields,
   * hence the "sizeof(guint8) * 2" */
  offset = AVTP_CVF_H264_HEADER_SIZE + sizeof (guint8) * 2;
  gst_avtp_cvf_depay_get_nalu_size (avtpcvfdepay, map, &nalu_size);
  nalu_size -= sizeof (guint8) * 2;

  if (start) {
    if (G_UNLIKELY (avtpcvfdepay->fragments != NULL)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Received starting fragment, but previous one is not complete. Dropping old fragment");
      ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
      if (ret != GST_FLOW_OK)
        goto end;
    }

    avtpcvfdepay->fragments =
        gst_buffer_copy_region (avtpdu, GST_BUFFER_COPY_MEMORY, offset,
        nalu_size);
  }

  if (!start && !end) {
    if (G_UNLIKELY (avtpcvfdepay->fragments == NULL)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Received intermediate fragment, but no start fragment received. Dropping it.");
      ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
      goto end;
    }
    gst_buffer_copy_into (avtpcvfdepay->fragments, avtpdu,
        GST_BUFFER_COPY_MEMORY, offset, nalu_size);
  }

  if (end) {
    ret =
        gst_avtp_cvf_depay_process_last_fragment (avtpcvfdepay, avtpdu, map,
        offset, nalu_size, nri, nal_type);
  }

end:
  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_handle_single_nal (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map)
{
  GstClockTime pts, dts;
  GstMapInfo map_nal;
  guint16 nalu_size;
  GstBuffer *nal;
  gboolean M;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Handling single NAL unit");

  if (avtpcvfdepay->fragments != NULL) {
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Received single NAL unit, but previous fragment is incomplete. Dropping fragment.");
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  gst_avtp_cvf_depay_get_avtp_timestamps (avtpcvfdepay, map, &pts, &dts);
  gst_avtp_cvf_depay_get_nalu_size (avtpcvfdepay, map, &nalu_size);
  gst_avtp_cvf_depay_get_M (avtpcvfdepay, map, &M);

  /* Four is the number of bytes containing NALu size just before the NALu */
  nal = gst_buffer_new_allocate (NULL, 4, NULL);
  gst_buffer_map (nal, &map_nal, GST_MAP_READWRITE);

  /* Add NAL size just before the NAL itself (4 bytes before it) */
  map_nal.data[0] = map_nal.data[1] = 0;
  map_nal.data[2] = nalu_size >> 8;
  map_nal.data[3] = nalu_size & 0xff;
  gst_buffer_unmap (nal, &map_nal);

  gst_buffer_copy_into (nal, avtpdu, GST_BUFFER_COPY_MEMORY,
      AVTP_CVF_H264_HEADER_SIZE, nalu_size);
  GST_BUFFER_PTS (nal) = pts;
  GST_BUFFER_DTS (nal) = dts;

  return gst_avtp_cvf_depay_internal_push (avtpcvfdepay, nal, M);
}

static GstFlowReturn
gst_avtp_cvf_depay_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstAvtpCvfDepay *avtpcvfdepay = GST_AVTP_CVF_DEPAY (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean lost_packet;
  GstMapInfo map;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  if (!gst_avtp_cvf_depay_validate_avtpdu (avtpcvfdepay, &map, &lost_packet)) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Invalid AVTPDU buffer, dropping it");
    goto end;
  }
  if (lost_packet) {
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    if (ret != GST_FLOW_OK)
      goto end;
  }

  switch (gst_avtp_cvf_depay_get_nal_type (&map)) {
    case STAP_A_TYPE:
    case STAP_B_TYPE:
    case MTAP16_TYPE:
    case MTAP24_TYPE:
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "AVTP aggregation packets not supported, dropping it");
      break;
    case FU_A_TYPE:
      ret = gst_avtp_cvf_depay_handle_fu_a (avtpcvfdepay, buffer, &map);
      break;
    case FU_B_TYPE:
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "AVTP fragmentation FU-B packets not supported, dropping it");
      break;
    default:
      ret = gst_avtp_cvf_depay_handle_single_nal (avtpcvfdepay, buffer, &map);
      break;
  }

end:
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  return ret;
}

gboolean
gst_avtp_cvf_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avtpcvfdepay", GST_RANK_NONE,
      GST_TYPE_AVTP_CVF_DEPAY);
}
