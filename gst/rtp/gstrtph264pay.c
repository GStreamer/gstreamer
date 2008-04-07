/* GStreamer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtph264pay.h"


#define SPS_TYPE_ID  7
#define PPS_TYPE_ID  8
#define USE_MEMCMP


GST_DEBUG_CATEGORY_STATIC (rtph264pay_debug);
#define GST_CAT_DEFAULT (rtph264pay_debug)

/* references:
 *
 * RFC 3984
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_h264pay_details =
GST_ELEMENT_DETAILS ("RTP packet payloader",
    "Codec/Payloader/Network",
    "Payload-encode H264 video into RTP packets (RFC 3984)",
    "Laurent Glayal <spglegle@yahoo.fr>");

static GstStaticPadTemplate gst_rtp_h264_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264")
    );

static GstStaticPadTemplate gst_rtp_h264_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H264\"")
    );

static void gst_rtp_h264_pay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_h264_pay_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_rtp_h264_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_h264_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpH264Pay, gst_rtp_h264_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_h264_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h264pay_details);
}

static void
gst_rtp_h264_pay_class_init (GstRtpH264PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_h264_pay_finalize;

  gstelement_class->change_state = gst_rtp_h264_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_h264_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_h264_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtph264pay_debug, "rtph264pay", 0,
      "H264 RTP Payloader");
}

static void
gst_rtp_h264_pay_init (GstRtpH264Pay * rtph264pay, GstRtpH264PayClass * klass)
{
  rtph264pay->profile = 0;
  rtph264pay->sps = NULL;
  rtph264pay->pps = NULL;
}

static void
gst_rtp_h264_pay_finalize (GObject * object)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  if (rtph264pay->sps)
    g_free (rtph264pay->sps);
  if (rtph264pay->pps)
    g_free (rtph264pay->pps);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gchar *
encode_base64 (const guint8 * in, guint size, guint * len)
{
  gchar *ret, *d;
  static const gchar *v =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  *len = ((size + 2) / 3) * 4;
  d = ret = (gchar *) g_malloc (*len + 1);
  for (; size; in += 3) {       /* process tuplets */
    *d++ = v[in[0] >> 2];       /* byte 1: high 6 bits (1) */
    /* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((in[0] << 4) + (--size ? (in[1] >> 4) : 0)) & 0x3f];
    /* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = size ? v[((in[1] << 2) + (--size ? (in[2] >> 6) : 0)) & 0x3f] : '=';
    /* byte 4: low 6 bits (3) */
    *d++ = size ? v[in[2] & 0x3f] : '=';
    if (size)
      size--;                   /* count third character if processed */
  }
  *d = '\0';                    /* tie off string */

  return ret;                   /* return the resulting string */
}

static gboolean
gst_rtp_h264_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpH264Pay *rtph264pay;
  GstStructure *str;
  const GValue *value;
  guint8 *data;
  guint size;

  rtph264pay = GST_RTP_H264_PAY (basepayload);

  str = gst_caps_get_structure (caps, 0);

  /* we can only set the output caps when we found the sprops and profile
   * NALs */
  gst_basertppayload_set_options (basepayload, "video", TRUE, "H264", 90000);

  /* packetized AVC video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    GstBuffer *buffer;
    GString *sprops;
    guint num_sps, num_pps;
    gint i, count, nal_size;
    gint profile;
    gchar *profile_str;

    GST_DEBUG_OBJECT (rtph264pay, "have packetized h264");
    rtph264pay->packetized = TRUE;

    buffer = gst_value_get_buffer (value);
    data = GST_BUFFER_DATA (buffer);
    size = GST_BUFFER_SIZE (buffer);

    /* parse the avcC data */
    if (size < 7)
      goto avcc_too_small;
    /* parse the version, this must be 1 */
    if (data[0] != 1)
      goto wrong_version;

    /* AVCProfileIndication */
    /* profile_compat */
    /* AVCLevelIndication */
    profile = (data[1] << 16) | (data[2] << 8) | data[3];
    GST_DEBUG_OBJECT (rtph264pay, "profile %06x", profile);

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    rtph264pay->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (rtph264pay, "nal length %u", rtph264pay->nal_length_size);
    /* 3 bits reserved | 5 bits numOfSequenceParameterSets */
    num_sps = data[5] & 0x1f;
    GST_DEBUG_OBJECT (rtph264pay, "num SPS %u", num_sps);

    data += 6;
    size -= 6;

    /* create the sprop-parameter-sets */
    sprops = g_string_new ("");
    count = 0;

    for (i = 0; i < num_sps; i++) {
      gchar *set;
      guint len;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      if (size < nal_size)
        goto avcc_error;

      set = encode_base64 (data, nal_size, &len);
      g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
      count++;
      g_free (set);

      data += nal_size;
      size -= nal_size;
    }
    if (size < 1)
      goto avcc_error;

    num_pps = data[0];
    data += 1;
    size -= 1;

    GST_DEBUG_OBJECT (rtph264pay, "num PPS %u", num_pps);
    for (i = 0; i < num_pps; i++) {
      gchar *set;
      guint len;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      if (size < nal_size)
        goto avcc_error;

      set = encode_base64 (data, nal_size, &len);
      g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
      count++;
      g_free (set);

      data += nal_size;
      size -= nal_size;
    }
    GST_DEBUG_OBJECT (rtph264pay, "sprops %s", sprops->str);

    profile_str = g_strdup_printf ("%06x", profile);
    gst_basertppayload_set_outcaps (basepayload, "profile-level-id",
        G_TYPE_STRING, profile_str,
        "sprop-parameter-sets", G_TYPE_STRING, sprops->str, NULL);
    g_free (profile_str);

    g_string_free (sprops, TRUE);
  } else {
    GST_DEBUG_OBJECT (rtph264pay, "have bytestream h264");
    rtph264pay->packetized = FALSE;
  }

  return TRUE;

avcc_too_small:
  {
    GST_ERROR_OBJECT (rtph264pay, "avcC size %u < 7", size);
    return FALSE;
  }
wrong_version:
  {
    GST_ERROR_OBJECT (rtph264pay, "wrong avcC version");
    return FALSE;
  }
avcc_error:
  {
    GST_ERROR_OBJECT (rtph264pay, "avcC too small ");
    return FALSE;
  }
}

static guint
next_start_code (guint8 * data, guint size)
{
  /* Boyer-Moore string matching algorithm, in a degenerative
   * sense because our search 'alphabet' is binary - 0 & 1 only.
   * This allow us to simplify the general BM algorithm to a very
   * simple form. */
  /* assume 1 is in the 4th byte */
  guint offset = 3;

  while (offset < size) {
    if (1 == data[offset]) {
      unsigned int shift = offset;

      if (0 == data[--shift]) {
        if (0 == data[--shift]) {
          if (0 == data[--shift]) {
            return shift;
          }
        }
      }
      /* The jump is always 4 because of the 1 previously matched.
       * All the 0's must be after this '1' matched at offset */
      offset += 4;
    } else if (0 == data[offset]) {
      /* maybe next byte is 1? */
      offset++;
    } else {
      /* can jump 4 bytes forward */
      offset += 4;
    }
    /* at each iteration, we rescan in a backward manner until
     * we match 0.0.0.1 in reverse order. Since our search string
     * has only 2 'alpabets' (i.e. 0 & 1), we know that any
     * mismatch will force us to shift a fixed number of steps */
  }
  GST_DEBUG ("Cannot find next NAL start code. returning %u", size);

  return size;
}

/* we don't use memcpy but this faster version (around 20%) because we need to
 * perform it on all data. */
static gboolean
is_nal_equal (const guint8 * nal1, const guint8 * nal2, guint len)
{
  /* if we have a 64-bit processor, we may use guint64 to make 
   * this go faster. Otherwise with 32 bits, we are already
   * going faster than byte to byte compare.
   */
  guint remainder = len & 0x3;
  guint num_int = len >> 2;
  guint32 *pu1 = (guint32 *) nal1, *pu2 = (guint32 *) nal2;
  guint i;

  /* compare by groups of sizeof(guint32) bytes */
  for (i = 0; i < num_int; i++) {
    /* XOR is faster than CMP?... */
    if (pu1[i] ^ pu2[i])
      return FALSE;
  }

  /* check that the remaining bytes are still equal */
  if (!remainder) {
    return TRUE;
  } else if (1 == remainder) {
    return (nal1[--len] == nal2[len]);
  } else {                      /* 2 or 3 */
    if (remainder & 1) {        /* -1 if 3 bytes left */
      if (nal1[--len] != nal2[len])
        return FALSE;
    }
    /* last 2 bytes */
    return ((nal1[--len] == nal2[len])  /* -1 */
        &&(nal1[--len] == nal2[len]));  /* -2 */
  }
}

static void
gst_rtp_h264_pay_decode_nal (GstRtpH264Pay * payloader,
    guint8 * data, guint size, gboolean * updated)
{
  guint8 *sps = NULL, *pps = NULL;
  guint sps_len = 0, pps_len = 0;

  /* default is no update */
  *updated = FALSE;

  if (size <= 3) {
    GST_WARNING ("Encoded buffer len %u <= 3", size);
  } else {
    GST_DEBUG ("NAL payload len=%u", size);

    /* loop through all NAL units and save the locations of any
     * SPS / PPS for later processing. Only the last seen SPS
     * or PPS will be considered */
    while (size > 5) {
      guint8 header, type;
      guint len;

      len = next_start_code (data, size);
      header = data[0];
      type = header & 0x1f;

      /* keep sps & pps separately so that we can update either one 
       * independently */
      if (SPS_TYPE_ID == type) {
        /* encode the entire SPS NAL in base64 */
        GST_DEBUG ("Found SPS %x %x %x Len=%u\n", (header >> 7),
            (header >> 5) & 3, type, len);

        sps = data;
        sps_len = len;
      } else if (PPS_TYPE_ID == type) {
        /* encoder the entire PPS NAL in base64 */
        GST_DEBUG ("Found PPS %x %x %x Len = %u\n",
            (header >> 7), (header >> 5) & 3, type, len);

        pps = data;
        pps_len = len;
      } else {
        GST_DEBUG ("NAL: %x %x %x Len = %u\n", (header >> 7),
            (header >> 5) & 3, type, len);
      }

      /* end of loop */
      if (len >= size - 4) {
        break;
      }

      /* next NAL start */
      data += len + 4;
      size -= len + 4;
    }

    /* If we encountered an SPS and/or a PPS, check if it's the
     * same as the one we have. If not, update our version and
     * set *updated to TRUE
     */
    if (sps_len > 0) {
      if ((payloader->sps_len != sps_len)
          || !is_nal_equal (payloader->sps, sps, sps_len)) {
        payloader->profile = (sps[1] << 16) + (sps[2] << 8) + sps[3];

        GST_DEBUG ("Profile level IDC = %06x", payloader->profile);

        if (payloader->sps_len)
          g_free (payloader->sps);

        payloader->sps = sps_len ? g_new (guint8, sps_len) : NULL;
        memcpy (payloader->sps, sps, sps_len);
        payloader->sps_len = sps_len;
        *updated = TRUE;
      }
    }

    if (pps_len > 0) {
      if ((payloader->pps_len != pps_len)
          || !is_nal_equal (payloader->pps, pps, pps_len)) {
        if (payloader->pps_len)
          g_free (payloader->pps);

        payloader->pps = pps_len ? g_new (guint8, pps_len) : NULL;
        memcpy (payloader->pps, pps, pps_len);
        payloader->pps_len = pps_len;
        *updated = TRUE;
      }
    }
  }
}

static void
gst_rtp_h264_pay_parse_sps_pps (GstBaseRTPPayload * basepayload,
    guint8 * data, guint size)
{
  gboolean update = FALSE;
  GstRtpH264Pay *payloader = GST_RTP_H264_PAY (basepayload);

  gst_rtp_h264_pay_decode_nal (payloader, data, size, &update);

  /* if has new SPS & PPS, update the output caps */
  if (update) {
    gchar *profile;
    gchar *sps;
    gchar *pps;
    gchar *sprops;
    guint len;

    /* profile is 24 bit. Force it to respect the limit */
    profile = g_strdup_printf ("%06x", payloader->profile & 0xffffff);

    /* build the sprop-parameter-sets */
    sps = (payloader->sps_len > 0)
        ? encode_base64 (payloader->sps, payloader->sps_len, &len) : NULL;
    pps = (payloader->pps_len > 0)
        ? encode_base64 (payloader->pps, payloader->pps_len, &len) : NULL;

    if (sps)
      sprops = g_strjoin (",", sps, pps, NULL);
    else
      sprops = g_strdup (pps);

    gst_basertppayload_set_outcaps (basepayload, "profile-level-id",
        G_TYPE_STRING, profile,
        "sprop-parameter-sets", G_TYPE_STRING, sprops, NULL);

    GST_DEBUG ("outcaps udpate: profile=%s, sps=%s, pps=%s\n",
        profile, sps, pps);

    g_free (sprops);
    g_free (profile);
    g_free (sps);
    g_free (pps);
  }
}

static GstFlowReturn
gst_rtp_h264_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  guint size, idxdata;
  GstBuffer *outbuf;
  guint8 *payload, *data, *pdata;
  guint8 nalType;
  GstClockTime timestamp;
  guint packet_len, payload_len, mtu;

  rtph264pay = GST_RTP_H264_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtph264pay);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG_OBJECT (basepayload, "got %d bytes", size);

  /* we don't support AVC input yet */
  if (rtph264pay->packetized)
    goto not_supported;

  /* H264 stream analysis */
  pdata = data;

  /* use next_start_code() to scan buffer.
   * next_start_code() returns the offset in data, 
   * starting from zero to the first byte of 0.0.0.1
   * If no start code is found, it returns the value of the 
   * 'size' parameter. 
   * pdata is unchanged by the call to next_start_code()
   */
  {
    guint offset = next_start_code (pdata, size);

    pdata += offset;
    idxdata = size - offset;
  }

  if (idxdata < 5) {
    GST_DEBUG_OBJECT (basepayload,
        "Returning GST_FLOW_OK without creating RTP packet");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  pdata += 4;
  idxdata -= 4;

  /* We know our stream is a valid H264 NAL packet, 
   * go parse it for SPS/PPS to enrich the caps */
  gst_rtp_h264_pay_parse_sps_pps (basepayload, pdata, idxdata);

  nalType = pdata[0] & 0x1f;
  GST_DEBUG_OBJECT (basepayload, "Processing Buffer with NAL TYPE=%d", nalType);

  packet_len = gst_rtp_buffer_calc_packet_len (idxdata, 0, 0);

  if (packet_len < mtu) {
    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit fit in one packet datasize=%d mtu=%d", idxdata, mtu);
    /* will fit in one packet */
    outbuf = gst_rtp_buffer_new_allocate (idxdata, 0, 0);
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    gst_rtp_buffer_set_marker (outbuf, 1);

    payload = gst_rtp_buffer_get_payload (outbuf);
    GST_DEBUG_OBJECT (basepayload, "Copying %d bytes to outbuf", idxdata);
    memcpy (payload, pdata, idxdata);
    gst_buffer_unref (buffer);
    ret = gst_basertppayload_push (basepayload, outbuf);
    return ret;
  } else {
    /* Fragmentation Units FU-A */
    guint8 nalHeader;
    guint limitedSize;
    int ii = 0, start = 1, end = 0, first = 0;

    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d", idxdata, mtu);

    nalHeader = *pdata;
    pdata++;
    idxdata--;

    ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT (basepayload, "Using FU-A fragmentation for data size=%d",
        idxdata);

    /* We keep 2 bytes for FU indicator and FU Header */
    payload_len = gst_rtp_buffer_calc_payload_len (mtu - 2, 0, 0);

    while (end == 0) {
      limitedSize = idxdata < payload_len ? idxdata : payload_len;
      GST_DEBUG_OBJECT (basepayload,
          "Inside  FU-A fragmentation limitedSize=%d iteration=%d", limitedSize,
          ii);

      outbuf = gst_rtp_buffer_new_allocate (limitedSize + 2, 0, 0);
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      payload = gst_rtp_buffer_get_payload (outbuf);

      if (limitedSize == idxdata) {
        GST_DEBUG_OBJECT (basepayload, "end idxdata=%d iteration=%d", idxdata,
            ii);
        end = 1;
      }
      gst_rtp_buffer_set_marker (outbuf, end);

      /* FU indicator */
      payload[0] = (nalHeader & 0x60) | 28;

      /* FU Header */
      payload[1] = (start << 7) | (end << 6) | (nalHeader & 0x1f);

      memcpy (&payload[2], pdata + first, limitedSize);
      GST_DEBUG_OBJECT (basepayload,
          "recorded %d payload bytes into packet iteration=%d", limitedSize + 2,
          ii);

      ret = gst_basertppayload_push (basepayload, outbuf);
      if (ret != GST_FLOW_OK)
        break;

      idxdata -= limitedSize;
      first += limitedSize;
      ii++;
      start = 0;
    }

    gst_buffer_unref (buffer);
    return ret;
  }

  /* ERRORS */
  GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
      (NULL), ("Should not be there !!"));
  gst_buffer_unref (buffer);

  return GST_FLOW_ERROR;

not_supported:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
        (NULL), ("AVC H264 is not supported yet"));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_SUPPORTED;
  }
}

static GstStateChangeReturn
gst_rtp_h264_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH264Pay *rtph264pay;
  GstStateChangeReturn ret;

  rtph264pay = GST_RTP_H264_PAY (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_h264_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph264pay",
      GST_RANK_NONE, GST_TYPE_RTP_H264_PAY);
}
