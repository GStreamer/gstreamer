/* ex: set tabstop=2 shiftwidth=2 expandtab: */
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


#define IDR_TYPE_ID  5
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
GST_ELEMENT_DETAILS ("RTP H264 payloader",
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

#define GST_TYPE_H264_SCAN_MODE (gst_h264_scan_mode_get_type())
static GType
gst_h264_scan_mode_get_type (void)
{
  static GType h264_scan_mode_type = 0;
  static const GEnumValue h264_scan_modes[] = {
    {GST_H264_SCAN_MODE_BYTESTREAM,
          "Scan complete bytestream for NALUs (not implemented)",
        "bytestream"},
    {GST_H264_SCAN_MODE_MULTI_NAL, "Buffers contain multiple complete NALUs",
        "multiple"},
    {GST_H264_SCAN_MODE_SINGLE_NAL, "Buffers contain a single complete NALU",
        "single"},
    {0, NULL, NULL},
  };

  if (!h264_scan_mode_type) {
    h264_scan_mode_type =
        g_enum_register_static ("GstH264PayScanMode", h264_scan_modes);
  }
  return h264_scan_mode_type;
}

#define DEFAULT_PROFILE_LEVEL_ID        NULL
#define DEFAULT_SPROP_PARAMETER_SETS    NULL
#define DEFAULT_SCAN_MODE               GST_H264_SCAN_MODE_MULTI_NAL
#define DEFAULT_BUFFER_LIST             FALSE
#define DEFAULT_SPSPPS_INTERVAL		      0

enum
{
  PROP_0,
  PROP_PROFILE_LEVEL_ID,
  PROP_SPROP_PARAMETER_SETS,
  PROP_SCAN_MODE,
  PROP_BUFFER_LIST,
  PROP_SPSPPS_INTERVAL,
  PROP_LAST
};

#define IS_ACCESS_UNIT(x) (((x) > 0x00) && ((x) < 0x06))

static void gst_rtp_h264_pay_finalize (GObject * object);

static void gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

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
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->set_property = gst_rtp_h264_pay_set_property;
  gobject_class->get_property = gst_rtp_h264_pay_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PROFILE_LEVEL_ID, g_param_spec_string ("profile-level-id",
          "profile-level-id",
          "The base64 profile-level-id to set in out caps (set to NULL to "
          "extract from stream)",
          DEFAULT_PROFILE_LEVEL_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SPROP_PARAMETER_SETS, g_param_spec_string ("sprop-parameter-sets",
          "sprop-parameter-sets",
          "The base64 sprop-parameter-sets to set in out caps (set to NULL to "
          "extract from stream)",
          DEFAULT_SPROP_PARAMETER_SETS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCAN_MODE,
      g_param_spec_enum ("scan-mode", "Scan Mode",
          "How to scan the input buffers for NAL units. Performance can be "
          "increased when certain assumptions are made about the input buffers",
          GST_TYPE_H264_SCAN_MODE, DEFAULT_SCAN_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_LIST,
      g_param_spec_boolean ("buffer-list", "Buffer List",
          "Use Buffer Lists",
          DEFAULT_BUFFER_LIST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SPSPPS_INTERVAL,
      g_param_spec_uint ("spspps-interval",
          "SPS PPS Send Interval",
          "Send SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.)",
          0, 3600, DEFAULT_SPSPPS_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  gobject_class->finalize = gst_rtp_h264_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_h264_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_h264_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtph264pay_debug, "rtph264pay", 0,
      "H264 RTP Payloader");
}

static void
gst_rtp_h264_pay_init (GstRtpH264Pay * rtph264pay, GstRtpH264PayClass * klass)
{
  rtph264pay->queue = g_array_new (FALSE, FALSE, sizeof (guint));
  rtph264pay->profile = 0;
  rtph264pay->sps = NULL;
  rtph264pay->pps = NULL;
  rtph264pay->last_spspps = -1;
  rtph264pay->scan_mode = GST_H264_SCAN_MODE_MULTI_NAL;
  rtph264pay->buffer_list = DEFAULT_BUFFER_LIST;
  rtph264pay->spspps_interval = DEFAULT_SPSPPS_INTERVAL;
}

static void
gst_rtp_h264_pay_clear_sps_pps (GstRtpH264Pay * rtph264pay)
{
  g_list_foreach (rtph264pay->sps, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtph264pay->sps);
  rtph264pay->sps = NULL;
  g_list_foreach (rtph264pay->pps, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtph264pay->pps);
  rtph264pay->pps = NULL;
}

static void
gst_rtp_h264_pay_finalize (GObject * object)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  g_array_free (rtph264pay->queue, TRUE);

  gst_rtp_h264_pay_clear_sps_pps (rtph264pay);

  g_free (rtph264pay->profile_level_id);
  g_free (rtph264pay->sprop_parameter_sets);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* take the currently configured SPS and PPS lists and set them on the caps as
 * sprop-parameter-sets */
static gboolean
gst_rtp_h264_pay_set_sps_pps (GstBaseRTPPayload * basepayload)
{
  GstRtpH264Pay *payloader = GST_RTP_H264_PAY (basepayload);
  gchar *profile;
  gchar *set;
  GList *walk;
  GString *sprops;
  guint count;
  gboolean res;

  sprops = g_string_new ("");
  count = 0;

  /* build the sprop-parameter-sets */
  for (walk = payloader->sps; walk; walk = g_list_next (walk)) {
    GstBuffer *sps_buf = GST_BUFFER_CAST (walk->data);

    set =
        g_base64_encode (GST_BUFFER_DATA (sps_buf), GST_BUFFER_SIZE (sps_buf));
    g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
    g_free (set);
    count++;
  }
  for (walk = payloader->pps; walk; walk = g_list_next (walk)) {
    GstBuffer *pps_buf = GST_BUFFER_CAST (walk->data);

    set =
        g_base64_encode (GST_BUFFER_DATA (pps_buf), GST_BUFFER_SIZE (pps_buf));
    g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
    g_free (set);
    count++;
  }

  /* profile is 24 bit. Force it to respect the limit */
  profile = g_strdup_printf ("%06x", payloader->profile & 0xffffff);
  /* combine into output caps */
  res = gst_basertppayload_set_outcaps (basepayload, "profile-level-id",
      G_TYPE_STRING, profile,
      "sprop-parameter-sets", G_TYPE_STRING, sprops->str, NULL);
  g_string_free (sprops, TRUE);
  g_free (profile);

  return res;
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
    guint num_sps, num_pps;
    gint i, nal_size;

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
    rtph264pay->profile = (data[1] << 16) | (data[2] << 8) | data[3];
    GST_DEBUG_OBJECT (rtph264pay, "profile %06x", rtph264pay->profile);

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    /* this is the number of bytes in front of the NAL units to mark their
     * length */
    rtph264pay->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (rtph264pay, "nal length %u", rtph264pay->nal_length_size);
    /* 3 bits reserved | 5 bits numOfSequenceParameterSets */
    num_sps = data[5] & 0x1f;
    GST_DEBUG_OBJECT (rtph264pay, "num SPS %u", num_sps);

    data += 6;
    size -= 6;

    /* create the sprop-parameter-sets */
    for (i = 0; i < num_sps; i++) {
      GstBuffer *sps_buf;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      GST_LOG_OBJECT (rtph264pay, "SPS %d size %d", i, nal_size);

      if (size < nal_size)
        goto avcc_error;

      /* make a buffer out of it and add to SPS list */
      sps_buf = gst_buffer_new_and_alloc (nal_size);
      memcpy (GST_BUFFER_DATA (sps_buf), data, nal_size);
      rtph264pay->sps = g_list_append (rtph264pay->sps, sps_buf);

      data += nal_size;
      size -= nal_size;
    }
    if (size < 1)
      goto avcc_error;

    /* 8 bits numOfPictureParameterSets */
    num_pps = data[0];
    data += 1;
    size -= 1;

    GST_DEBUG_OBJECT (rtph264pay, "num PPS %u", num_pps);
    for (i = 0; i < num_pps; i++) {
      GstBuffer *pps_buf;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      GST_LOG_OBJECT (rtph264pay, "PPS %d size %d", i, nal_size);

      if (size < nal_size)
        goto avcc_error;

      /* make a buffer out of it and add to PPS list */
      pps_buf = gst_buffer_new_and_alloc (nal_size);
      memcpy (GST_BUFFER_DATA (pps_buf), data, nal_size);
      rtph264pay->pps = g_list_append (rtph264pay->pps, pps_buf);

      data += nal_size;
      size -= nal_size;
    }
    /* and update the caps with the collected data */
    if (!gst_rtp_h264_pay_set_sps_pps (basepayload))
      return FALSE;
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

static gboolean
gst_rtp_h264_pay_decode_nal (GstRtpH264Pay * payloader,
    guint8 * data, guint size, GstClockTime timestamp)
{
  guint8 *sps = NULL, *pps = NULL;
  guint sps_len = 0, pps_len = 0;
  guint8 header, type;
  guint len;
  gboolean updated;

  /* default is no update */
  updated = FALSE;

  GST_DEBUG ("NAL payload len=%u", size);

  len = size;
  header = data[0];
  type = header & 0x1f;

  /* keep sps & pps separately so that we can update either one 
   * independently. We also record the timestamp of the last SPS/PPS so 
   * that we can insert them at regular intervals and when needed. */
  if (SPS_TYPE_ID == type) {
    /* encode the entire SPS NAL in base64 */
    GST_DEBUG ("Found SPS %x %x %x Len=%u", (header >> 7),
        (header >> 5) & 3, type, len);

    sps = data;
    sps_len = len;
    /* remember when we last saw SPS */
    if (timestamp != -1)
      payloader->last_spspps = timestamp;
  } else if (PPS_TYPE_ID == type) {
    /* encoder the entire PPS NAL in base64 */
    GST_DEBUG ("Found PPS %x %x %x Len = %u",
        (header >> 7), (header >> 5) & 3, type, len);

    pps = data;
    pps_len = len;
    /* remember when we last saw PPS */
    if (timestamp != -1)
      payloader->last_spspps = timestamp;
  } else {
    GST_DEBUG ("NAL: %x %x %x Len = %u", (header >> 7),
        (header >> 5) & 3, type, len);
  }

  /* If we encountered an SPS and/or a PPS, check if it's the
   * same as the one we have. If not, update our version and
   * set updated to TRUE
   */
  if (sps_len > 0) {
    GstBuffer *sps_buf;

    if (payloader->sps != NULL) {
      sps_buf = GST_BUFFER_CAST (payloader->sps->data);

      if ((GST_BUFFER_SIZE (sps_buf) != sps_len)
          || memcmp (GST_BUFFER_DATA (sps_buf), sps, sps_len)) {
        /* something changed, update */
        payloader->profile = (sps[1] << 16) + (sps[2] << 8) + sps[3];
        GST_DEBUG ("Profile level IDC = %06x", payloader->profile);
        updated = TRUE;
      }
    } else {
      /* no previous SPS, update */
      updated = TRUE;
    }

    if (updated) {
      sps_buf = gst_buffer_new_and_alloc (sps_len);
      memcpy (GST_BUFFER_DATA (sps_buf), sps, sps_len);

      if (payloader->sps) {
        /* replace old buffer */
        gst_buffer_unref (payloader->sps->data);
        payloader->sps->data = sps_buf;
      } else {
        /* add new buffer */
        payloader->sps = g_list_prepend (payloader->sps, sps_buf);
      }
    }
  }

  if (pps_len > 0) {
    GstBuffer *pps_buf;

    if (payloader->pps != NULL) {
      pps_buf = GST_BUFFER_CAST (payloader->pps->data);

      if ((GST_BUFFER_SIZE (pps_buf) != pps_len)
          || memcmp (GST_BUFFER_DATA (pps_buf), pps, pps_len)) {
        /* something changed, update */
        updated = TRUE;
      }
    } else {
      /* no previous SPS, update */
      updated = TRUE;
    }

    if (updated) {
      pps_buf = gst_buffer_new_and_alloc (pps_len);
      memcpy (GST_BUFFER_DATA (pps_buf), pps, pps_len);

      if (payloader->pps) {
        /* replace old buffer */
        gst_buffer_unref (payloader->pps->data);
        payloader->pps->data = pps_buf;
      } else {
        /* add new buffer */
        payloader->pps = g_list_prepend (payloader->pps, pps_buf);
      }
    }
  }
  return updated;
}

static GstFlowReturn
gst_rtp_h264_pay_payload_nal (GstBaseRTPPayload * basepayload, guint8 * data,
    guint size, GstClockTime timestamp, GstBuffer * buffer_orig);

static GstFlowReturn
gst_rtp_h264_pay_send_sps_pps (GstBaseRTPPayload * basepayload,
    GstRtpH264Pay * rtph264pay, GstClockTime timestamp)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GList *walk;

  for (walk = rtph264pay->sps; walk; walk = g_list_next (walk)) {
    GstBuffer *sps_buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (rtph264pay, "inserting SPS in the stream");
    /* resend SPS */
    ret = gst_rtp_h264_pay_payload_nal (basepayload,
        GST_BUFFER_DATA (sps_buf), GST_BUFFER_SIZE (sps_buf), timestamp,
        sps_buf);
    /* Not critical here; but throw a warning */
    if (ret != GST_FLOW_OK)
      GST_WARNING ("Problem pushing SPS");
  }
  for (walk = rtph264pay->pps; walk; walk = g_list_next (walk)) {
    GstBuffer *pps_buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (rtph264pay, "inserting PPS in the stream");
    /* resend PPS */
    ret = gst_rtp_h264_pay_payload_nal (basepayload,
        GST_BUFFER_DATA (pps_buf), GST_BUFFER_SIZE (pps_buf), timestamp,
        pps_buf);
    /* Not critical here; but throw a warning */
    if (ret != GST_FLOW_OK)
      GST_WARNING ("Problem pushing PPS");
  }

  if (timestamp != -1)
    rtph264pay->last_spspps = timestamp;

  return ret;
}

static GstFlowReturn
gst_rtp_h264_pay_payload_nal (GstBaseRTPPayload * basepayload, guint8 * data,
    guint size, GstClockTime timestamp, GstBuffer * buffer_orig)
{
  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  guint8 nalType;
  guint packet_len, payload_len, mtu;
  GstBuffer *outbuf;
  guint8 *payload;
  GstBufferList *list = NULL;
  GstBufferListIterator *it = NULL;
  gboolean send_spspps;

  rtph264pay = GST_RTP_H264_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtph264pay);

  nalType = data[0] & 0x1f;
  GST_DEBUG_OBJECT (rtph264pay, "Processing Buffer with NAL TYPE=%d", nalType);

  send_spspps = FALSE;

  /* check if we need to emit an SPS/PPS now */
  if (nalType == IDR_TYPE_ID && rtph264pay->spspps_interval > 0) {
    if (rtph264pay->last_spspps != -1) {
      guint diff;

      /* calculate diff between last SPS/PPS in milliseconds */
      if (timestamp > rtph264pay->last_spspps)
        diff = GST_TIME_AS_MSECONDS (timestamp - rtph264pay->last_spspps);
      else
        diff = 0;

      GST_DEBUG_OBJECT (rtph264pay, "interval since last SPS/PPS %ums", diff);

      /* bigger than interval, queue SPS/PPS */
      if (diff >= rtph264pay->spspps_interval) {
        GST_DEBUG_OBJECT (rtph264pay, "time to send SPS/PPS");
        send_spspps = TRUE;
      }
    } else {
      /* no know previous SPS/PPS time, send now */
      GST_DEBUG_OBJECT (rtph264pay, "no previous SPS/PPS time, send now");
      send_spspps = TRUE;
    }
  }

  if (send_spspps) {
    /* we need to send SPS/PPS now first */
    ret = gst_rtp_h264_pay_send_sps_pps (basepayload, rtph264pay, timestamp);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  packet_len = gst_rtp_buffer_calc_packet_len (size, 0, 0);

  if (packet_len < mtu) {
    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit fit in one packet datasize=%d mtu=%d", size, mtu);
    /* will fit in one packet */

    if (rtph264pay->buffer_list) {
      /* use buffer lists
       * first create buffer without payload containing only the RTP header
       * and then another buffer containing the payload. both buffers will
       * be then added to the list */
      outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);
    } else {
      /* use the old-fashioned way with a single buffer and memcpy */
      outbuf = gst_rtp_buffer_new_allocate (size, 0, 0);
    }

    /* only set the marker bit on packets containing access units */
    if (IS_ACCESS_UNIT (nalType)) {
      gst_rtp_buffer_set_marker (outbuf, 1);
    }

    /* timestamp the outbuffer */
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    if (rtph264pay->buffer_list) {
      GstBuffer *paybuf;

      /* create another buffer with the payload. */
      if (buffer_orig)
        paybuf = gst_buffer_create_sub (buffer_orig, data -
            GST_BUFFER_DATA (buffer_orig), size);
      else {
        paybuf = gst_buffer_new_and_alloc (size);
        memcpy (GST_BUFFER_DATA (paybuf), data, size);
      }

      list = gst_buffer_list_new ();
      it = gst_buffer_list_iterate (list);

      /* add both buffers to the buffer list */
      gst_buffer_list_iterator_add_group (it);
      gst_buffer_list_iterator_add (it, outbuf);
      gst_buffer_list_iterator_add (it, paybuf);

      gst_buffer_list_iterator_free (it);

      /* push the list to the next element in the pipe */
      ret = gst_basertppayload_push_list (basepayload, list);
    } else {
      payload = gst_rtp_buffer_get_payload (outbuf);
      GST_DEBUG_OBJECT (basepayload, "Copying %d bytes to outbuf", size);
      memcpy (payload, data, size);

      ret = gst_basertppayload_push (basepayload, outbuf);
    }
  } else {
    /* fragmentation Units FU-A */
    guint8 nalHeader;
    guint limitedSize;
    int ii = 0, start = 1, end = 0, pos = 0;

    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d", size, mtu);

    nalHeader = *data;
    pos++;
    size--;

    ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT (basepayload, "Using FU-A fragmentation for data size=%d",
        size);

    /* We keep 2 bytes for FU indicator and FU Header */
    payload_len = gst_rtp_buffer_calc_payload_len (mtu - 2, 0, 0);

    if (rtph264pay->buffer_list) {
      list = gst_buffer_list_new ();
      it = gst_buffer_list_iterate (list);
    }

    while (end == 0) {
      limitedSize = size < payload_len ? size : payload_len;
      GST_DEBUG_OBJECT (basepayload,
          "Inside  FU-A fragmentation limitedSize=%d iteration=%d", limitedSize,
          ii);

      if (rtph264pay->buffer_list) {
        /* use buffer lists
         * first create buffer without payload containing only the RTP header
         * and then another buffer containing the payload. both buffers will
         * be then added to the list */
        outbuf = gst_rtp_buffer_new_allocate (2, 0, 0);
      } else {
        /* use the old-fashioned way with a single buffer and memcpy
         * first create buffer to hold the payload */
        outbuf = gst_rtp_buffer_new_allocate (limitedSize + 2, 0, 0);
      }

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      payload = gst_rtp_buffer_get_payload (outbuf);

      if (limitedSize == size) {
        GST_DEBUG_OBJECT (basepayload, "end size=%d iteration=%d", size, ii);
        end = 1;
      }
      if (IS_ACCESS_UNIT (nalType)) {
        gst_rtp_buffer_set_marker (outbuf, end);
      }

      /* FU indicator */
      payload[0] = (nalHeader & 0x60) | 28;

      /* FU Header */
      payload[1] = (start << 7) | (end << 6) | (nalHeader & 0x1f);

      if (rtph264pay->buffer_list) {
        GstBuffer *paybuf;

        /* create another buffer to hold the payload */
        if (buffer_orig)
          paybuf = gst_buffer_create_sub (buffer_orig, data -
              GST_BUFFER_DATA (buffer_orig) + pos, limitedSize);
        else {
          paybuf = gst_buffer_new_and_alloc (limitedSize);
          memcpy (GST_BUFFER_DATA (paybuf), data + pos, limitedSize);
        }

        /* create a new group to hold the header and the payload */
        gst_buffer_list_iterator_add_group (it);

        /* add both buffers to the buffer list */
        gst_buffer_list_iterator_add (it, outbuf);
        gst_buffer_list_iterator_add (it, paybuf);

      } else {
        memcpy (&payload[2], data + pos, limitedSize);
        GST_DEBUG_OBJECT (basepayload,
            "recorded %d payload bytes into packet iteration=%d",
            limitedSize + 2, ii);

        ret = gst_basertppayload_push (basepayload, outbuf);
        if (ret != GST_FLOW_OK)
          break;
      }

      size -= limitedSize;
      pos += limitedSize;
      ii++;
      start = 0;
    }

    if (rtph264pay->buffer_list) {
      /* free iterator and push the whole buffer list at once */
      gst_buffer_list_iterator_free (it);
      ret = gst_basertppayload_push_list (basepayload, list);
    }
  }
  return ret;
}

static GstFlowReturn
gst_rtp_h264_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  guint size, nal_len, i;
  guint8 *data, *nal_data;
  GstClockTime timestamp;
  GArray *nal_queue;

  rtph264pay = GST_RTP_H264_PAY (basepayload);

  /* the input buffer contains one or more NAL units */
  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (basepayload, "got %d bytes", size);

  /* now loop over all NAL units and put them in a packet
   * FIXME, we should really try to pack multiple NAL units into one RTP packet
   * if we can, especially for the config packets that wont't cause decoder 
   * latency. */
  if (rtph264pay->packetized) {
    guint nal_length_size;

    nal_length_size = rtph264pay->nal_length_size;

    while (size > nal_length_size) {
      gint i;

      nal_len = 0;
      for (i = 0; i < nal_length_size; i++) {
        nal_len = ((nal_len << 8) + data[i]);
      }

      /* skip the length bytes, make sure we don't run past the buffer size */
      data += nal_length_size;
      size -= nal_length_size;

      if (size >= nal_len) {
        GST_DEBUG_OBJECT (basepayload, "got NAL of size %u", nal_len);
      } else {
        nal_len = size;
        GST_DEBUG_OBJECT (basepayload, "got incomplete NAL of size %u",
            nal_len);
      }

      ret =
          gst_rtp_h264_pay_payload_nal (basepayload, data, nal_len, timestamp,
          buffer);
      if (ret != GST_FLOW_OK)
        break;

      data += nal_len;
      size -= nal_len;
    }
  } else {
    guint next;
    gboolean update = FALSE;

    /* get offset of first start code */
    next = next_start_code (data, size);

    /* skip to start code, if no start code is found, next will be size and we
     * will not collect data. */
    data += next;
    size -= next;
    nal_data = data;
    nal_queue = rtph264pay->queue;

    /* array must be empty when we get here */
    g_assert (nal_queue->len == 0);

    GST_DEBUG_OBJECT (basepayload, "found first start at %u, bytes left %u",
        next, size);

    /* first pass to locate NALs and parse SPS/PPS */
    while (size > 4) {
      /* skip start code */
      data += 4;
      size -= 4;

      if (rtph264pay->scan_mode == GST_H264_SCAN_MODE_SINGLE_NAL) {
        /* we are told that there is only a single NAL in this packet so that we
         * can avoid scanning for the next NAL. */
        next = size;
      } else {
        /* use next_start_code() to scan buffer.
         * next_start_code() returns the offset in data, 
         * starting from zero to the first byte of 0.0.0.1
         * If no start code is found, it returns the value of the 
         * 'size' parameter. 
         * data is unchanged by the call to next_start_code()
         */
        next = next_start_code (data, size);
      }

      /* nal length is distance to next start code */
      nal_len = next;

      GST_DEBUG_OBJECT (basepayload, "found next start at %u of size %u", next,
          nal_len);

      if (rtph264pay->profile_level_id != NULL &&
          rtph264pay->sprop_parameter_sets != NULL) {
        /* explicitly set profile and sprop, use those */
        if (rtph264pay->update_caps) {
          if (!gst_basertppayload_set_outcaps (basepayload, "profile-level-id",
                  G_TYPE_STRING, rtph264pay->profile_level_id,
                  "sprop-parameter-sets", G_TYPE_STRING,
                  rtph264pay->sprop_parameter_sets, NULL))
            goto caps_rejected;

          rtph264pay->update_caps = FALSE;

          GST_DEBUG
              ("outcaps udpate: profile-level-id=%s, sprop-parameter-sets=%s",
              rtph264pay->profile_level_id, rtph264pay->sprop_parameter_sets);
        }
      } else {
        /* We know our stream is a valid H264 NAL packet,
         * go parse it for SPS/PPS to enrich the caps */
        /* order: make sure to check nal */
        update =
            gst_rtp_h264_pay_decode_nal (rtph264pay, data, nal_len, timestamp)
            || update;
      }
      /* move to next NAL packet */
      data += nal_len;
      size -= nal_len;

      g_array_append_val (nal_queue, nal_len);
    }

    /* if has new SPS & PPS, update the output caps */
    if (G_UNLIKELY (update))
      if (!gst_rtp_h264_pay_set_sps_pps (basepayload))
        goto caps_rejected;

    /* second pass to payload and push */
    data = nal_data;
    for (i = 0; i < nal_queue->len; i++) {
      nal_len = g_array_index (nal_queue, guint, i);
      /* skip start code */
      data += 4;

      /* put the data in one or more RTP packets */
      ret =
          gst_rtp_h264_pay_payload_nal (basepayload, data, nal_len, timestamp,
          buffer);
      if (ret != GST_FLOW_OK) {
        break;
      }

      /* move to next NAL packet */
      data += nal_len;
      size -= nal_len;
    }
    g_array_set_size (nal_queue, 0);
  }

  gst_buffer_unref (buffer);

  return ret;

caps_rejected:

  GST_WARNING_OBJECT (basepayload, "Could not set outcaps");
  g_array_set_size (nal_queue, 0);
  gst_buffer_unref (buffer);
  return GST_FLOW_NOT_NEGOTIATED;
}

static void
gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    case PROP_PROFILE_LEVEL_ID:
      g_free (rtph264pay->profile_level_id);
      rtph264pay->profile_level_id = g_value_dup_string (value);
      rtph264pay->update_caps = TRUE;
      break;
    case PROP_SPROP_PARAMETER_SETS:
      g_free (rtph264pay->sprop_parameter_sets);
      rtph264pay->sprop_parameter_sets = g_value_dup_string (value);
      rtph264pay->update_caps = TRUE;
      break;
    case PROP_SCAN_MODE:
      rtph264pay->scan_mode = g_value_get_enum (value);
      break;
    case PROP_BUFFER_LIST:
      rtph264pay->buffer_list = g_value_get_boolean (value);
      break;
    case PROP_SPSPPS_INTERVAL:
      rtph264pay->spspps_interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    case PROP_PROFILE_LEVEL_ID:
      g_value_set_string (value, rtph264pay->profile_level_id);
      break;
    case PROP_SPROP_PARAMETER_SETS:
      g_value_set_string (value, rtph264pay->sprop_parameter_sets);
      break;
    case PROP_SCAN_MODE:
      g_value_set_enum (value, rtph264pay->scan_mode);
      break;
    case PROP_BUFFER_LIST:
      g_value_set_boolean (value, rtph264pay->buffer_list);
      break;
    case PROP_SPSPPS_INTERVAL:
      g_value_set_uint (value, rtph264pay->spspps_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_h264_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph264pay",
      GST_RANK_NONE, GST_TYPE_RTP_H264_PAY);
}
