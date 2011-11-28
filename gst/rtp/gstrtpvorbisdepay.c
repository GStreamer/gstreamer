/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/tag/tag.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpvorbisdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvorbisdepay_debug);
#define GST_CAT_DEFAULT (rtpvorbisdepay_debug)

/* references:
 * http://www.rfc-editor.org/rfc/rfc5215.txt
 */

static GstStaticPadTemplate gst_rtp_vorbis_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"VORBIS\""
        /* All required parameters 
         *
         * "encoding-params = (string) <num channels>"
         * "configuration = (string) ANY" 
         */
    )
    );

static GstStaticPadTemplate gst_rtp_vorbis_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstRtpVorbisDepay, gst_rtp_vorbis_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_vorbis_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_vorbis_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_vorbis_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_vorbis_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_vorbis_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vorbis_depay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vorbis_depay_src_template);

  gst_element_class_set_details_simple (element_class, "RTP Vorbis depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts Vorbis Audio from RTP packets (RFC 5215)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_vorbis_depay_class_init (GstRtpVorbisDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_vorbis_depay_finalize;

  gstelement_class->change_state = gst_rtp_vorbis_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_vorbis_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_vorbis_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpvorbisdepay_debug, "rtpvorbisdepay", 0,
      "Vorbis RTP Depayloader");
}

static void
gst_rtp_vorbis_depay_init (GstRtpVorbisDepay * rtpvorbisdepay,
    GstRtpVorbisDepayClass * klass)
{
  rtpvorbisdepay->adapter = gst_adapter_new ();
}

static void
free_config (GstRtpVorbisConfig * conf)
{
  GList *headers;

  for (headers = conf->headers; headers; headers = g_list_next (headers)) {
    GstBuffer *header = GST_BUFFER_CAST (headers->data);

    gst_buffer_unref (header);
  }
  g_list_free (conf->headers);
  g_free (conf);
}

static void
free_indents (GstRtpVorbisDepay * rtpvorbisdepay)
{
  GList *walk;

  for (walk = rtpvorbisdepay->configs; walk; walk = g_list_next (walk)) {
    free_config ((GstRtpVorbisConfig *) walk->data);
  }
  g_list_free (rtpvorbisdepay->configs);
  rtpvorbisdepay->configs = NULL;
}

static void
gst_rtp_vorbis_depay_finalize (GObject * object)
{
  GstRtpVorbisDepay *rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (object);

  g_object_unref (rtpvorbisdepay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* takes ownership of confbuf */
static gboolean
gst_rtp_vorbis_depay_parse_configuration (GstRtpVorbisDepay * rtpvorbisdepay,
    GstBuffer * confbuf)
{
  GstBuffer *buf;
  guint32 num_headers;
  guint8 *data;
  guint size;
  guint offset;
  gint i, j;

  data = GST_BUFFER_DATA (confbuf);
  size = GST_BUFFER_SIZE (confbuf);

  GST_DEBUG_OBJECT (rtpvorbisdepay, "config size %u", size);

  /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Number of packed headers                  |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          ....                                 |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  if (size < 4)
    goto too_small;

  num_headers = GST_READ_UINT32_BE (data);
  size -= 4;
  data += 4;
  offset = 4;

  GST_DEBUG_OBJECT (rtpvorbisdepay, "have %u headers", num_headers);

  /*  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                   Ident                       | length       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              | n. of headers |    length1    |    length2   ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |             Identification Header            ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |         Comment Header                       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        Comment Header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Setup Header                        ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                         Setup Header                         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  for (i = 0; i < num_headers; i++) {
    guint32 ident;
    guint16 length;
    guint8 n_headers, b;
    GstRtpVorbisConfig *conf;
    guint *h_sizes;
    guint extra = 1;

    if (size < 6)
      goto too_small;

    ident = (data[0] << 16) | (data[1] << 8) | data[2];
    length = (data[3] << 8) | data[4];
    n_headers = data[5];
    size -= 6;
    data += 6;
    offset += 6;

    GST_DEBUG_OBJECT (rtpvorbisdepay,
        "header %d, ident 0x%08x, length %u, left %u", i, ident, length, size);

    /* FIXME check if we already got this ident */

    /* length might also include count of following size fields */
    if (size < length && size + 1 != length)
      goto too_small;

    /* read header sizes we read 2 sizes, the third size (for which we allocate
     * space) must be derived from the total packed header length. */
    h_sizes = g_newa (guint, n_headers + 1);
    for (j = 0; j < n_headers; j++) {
      guint h_size;

      h_size = 0;
      do {
        if (size < 1)
          goto too_small;
        b = *data++;
        offset++;
        extra++;
        size--;
        h_size = (h_size << 7) | (b & 0x7f);
      } while (b & 0x80);
      GST_DEBUG_OBJECT (rtpvorbisdepay, "headers %d: size: %u", j, h_size);

      if (length < h_size)
        goto too_small;

      h_sizes[j] = h_size;
      length -= h_size;
    }
    /* last header length is the remaining space */
    GST_DEBUG_OBJECT (rtpvorbisdepay, "last header size: %u", length);
    h_sizes[j] = length;

    GST_DEBUG_OBJECT (rtpvorbisdepay, "preparing headers");
    conf = g_new0 (GstRtpVorbisConfig, 1);
    conf->ident = ident;

    for (j = 0; j <= n_headers; j++) {
      guint h_size;

      h_size = h_sizes[j];
      if (size < h_size) {
        if (j != n_headers || size + extra != h_size) {
          free_config (conf);
          goto too_small;
        } else {
          /* otherwise means that overall length field contained total length,
           * including extra fields */
          h_size -= extra;
        }
      }

      GST_DEBUG_OBJECT (rtpvorbisdepay, "reading header %d, size %u", j,
          h_size);

      buf = gst_buffer_create_sub (confbuf, offset, h_size);
      conf->headers = g_list_append (conf->headers, buf);
      offset += h_size;
      size -= h_size;
    }
    rtpvorbisdepay->configs = g_list_append (rtpvorbisdepay->configs, conf);
  }
  gst_buffer_unref (confbuf);

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_DEBUG_OBJECT (rtpvorbisdepay, "configuration too small");
    gst_buffer_unref (confbuf);
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_depay_parse_inband_configuration (GstRtpVorbisDepay *
    rtpvorbisdepay, guint ident, guint8 * configuration, guint size,
    guint length)
{
  GstBuffer *confbuf;
  guint8 *conf;

  if (G_UNLIKELY (size < 4))
    return FALSE;

  /* transform inline to out-of-band and parse that one */
  confbuf = gst_buffer_new_and_alloc (size + 9);
  conf = GST_BUFFER_DATA (confbuf);
  /* 1 header */
  GST_WRITE_UINT32_BE (conf, 1);
  /* write Ident */
  GST_WRITE_UINT24_BE (conf + 4, ident);
  /* write sort-of-length */
  GST_WRITE_UINT16_BE (conf + 7, length);
  /* copy remainder */
  memcpy (conf + 9, configuration, size);

  return gst_rtp_vorbis_depay_parse_configuration (rtpvorbisdepay, confbuf);
}

static gboolean
gst_rtp_vorbis_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstCaps *srccaps;
  const gchar *configuration;
  gint clock_rate;
  gboolean res;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  /* get clockrate */
  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    goto no_rate;

  /* read and parse configuration string */
  configuration = gst_structure_get_string (structure, "configuration");
  if (configuration) {
    GstBuffer *confbuf;
    guint8 *data;
    gsize size;

    /* deserialize base64 to buffer */
    data = g_base64_decode (configuration, &size);

    confbuf = gst_buffer_new ();
    GST_BUFFER_DATA (confbuf) = data;
    GST_BUFFER_MALLOCDATA (confbuf) = data;
    GST_BUFFER_SIZE (confbuf) = size;
    if (!gst_rtp_vorbis_depay_parse_configuration (rtpvorbisdepay, confbuf))
      goto invalid_configuration;
  } else {
    GST_WARNING_OBJECT (rtpvorbisdepay, "no configuration specified");
  }

  /* caps seem good, configure element */
  depayload->clock_rate = clock_rate;

  /* set caps on pad and on header */
  srccaps = gst_caps_new_simple ("audio/x-vorbis", NULL);
  res = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return res;

  /* ERRORS */
invalid_configuration:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay, "invalid configuration specified");
    return FALSE;
  }
no_rate:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay, "no clock-rate specified");
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_depay_switch_codebook (GstRtpVorbisDepay * rtpvorbisdepay,
    guint32 ident)
{
  GList *walk;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (rtpvorbisdepay, "Looking up code book ident 0x%08x", ident);
  for (walk = rtpvorbisdepay->configs; walk; walk = g_list_next (walk)) {
    GstRtpVorbisConfig *conf = (GstRtpVorbisConfig *) walk->data;

    if (conf->ident == ident) {
      GList *headers;

      /* FIXME, remove pads, create new pad.. */

      /* push out all the headers */
      for (headers = conf->headers; headers; headers = g_list_next (headers)) {
        GstBuffer *header = GST_BUFFER_CAST (headers->data);

        gst_buffer_ref (header);
        gst_base_rtp_depayload_push (GST_BASE_RTP_DEPAYLOAD (rtpvorbisdepay),
            header);
      }
      /* remember the current config */
      rtpvorbisdepay->config = conf;
      res = TRUE;
    }
  }
  if (!res) {
    /* we don't know about the headers, figure out an alternative method for
     * getting the codebooks. FIXME, fail for now. */
  }
  return res;
}

static GstBuffer *
gst_rtp_vorbis_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gint payload_len;
  guint8 *payload, *to_free = NULL;
  guint32 timestamp;
  guint32 header, ident;
  guint8 F, VDT, packets;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (depayload);

  payload_len = gst_rtp_buffer_get_payload_len (buf);

  GST_DEBUG_OBJECT (depayload, "got RTP packet of size %d", payload_len);

  /* we need at least 4 bytes for the packet header */
  if (G_UNLIKELY (payload_len < 4))
    goto packet_short;

  payload = gst_rtp_buffer_get_payload (buf);

  header = GST_READ_UINT32_BE (payload);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |VDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * VDT: Vorbis data type (0=vorbis, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  VDT = (header & 0x30) >> 4;
  if (G_UNLIKELY (VDT == 3))
    goto ignore_reserved;

  GST_DEBUG_OBJECT (depayload, "header: 0x%08x", header);
  ident = (header >> 8) & 0xffffff;
  F = (header & 0xc0) >> 6;
  packets = (header & 0xf);

  if (VDT == 0) {
    gboolean do_switch = FALSE;

    /* we have a raw payload, find the codebook for the ident */
    if (!rtpvorbisdepay->config) {
      /* we don't have an active codebook, find the codebook and
       * activate it */
      GST_DEBUG_OBJECT (rtpvorbisdepay, "No active codebook, switching");
      do_switch = TRUE;
    } else if (rtpvorbisdepay->config->ident != ident) {
      /* codebook changed */
      GST_DEBUG_OBJECT (rtpvorbisdepay, "codebook changed, switching");
      do_switch = TRUE;
    }
    if (do_switch) {
      if (!gst_rtp_vorbis_depay_switch_codebook (rtpvorbisdepay, ident))
        goto switch_failed;
    }
  }

  /* skip header */
  payload += 4;
  payload_len -= 4;

  GST_DEBUG_OBJECT (depayload, "ident: %u, F: %d, VDT: %d, packets: %d", ident,
      F, VDT, packets);

  /* fragmented packets, assemble */
  if (F != 0) {
    GstBuffer *vdata;
    guint headerskip;

    if (F == 1) {
      /* if we start a packet, clear adapter and start assembling. */
      gst_adapter_clear (rtpvorbisdepay->adapter);
      GST_DEBUG_OBJECT (depayload, "start assemble");
      rtpvorbisdepay->assembling = TRUE;
    }

    if (!rtpvorbisdepay->assembling)
      goto no_output;

    /* first assembled packet, reuse 2 bytes to store the length */
    headerskip = (F == 1 ? 4 : 6);
    /* skip header and length. */
    vdata = gst_rtp_buffer_get_payload_subbuffer (buf, headerskip, -1);

    GST_DEBUG_OBJECT (depayload, "assemble vorbis packet");
    gst_adapter_push (rtpvorbisdepay->adapter, vdata);

    /* packet is not complete, we are done */
    if (F != 3)
      goto no_output;

    /* construct assembled buffer */
    payload_len = gst_adapter_available (rtpvorbisdepay->adapter);
    payload = gst_adapter_take (rtpvorbisdepay->adapter, payload_len);
    /* fix the length */
    payload[0] = ((payload_len - 2) >> 8) & 0xff;
    payload[1] = (payload_len - 2) & 0xff;
    to_free = payload;
  }

  GST_DEBUG_OBJECT (depayload, "assemble done");

  /* we not assembling anymore now */
  rtpvorbisdepay->assembling = FALSE;
  gst_adapter_clear (rtpvorbisdepay->adapter);

  /* payload now points to a length with that many vorbis data bytes.
   * Iterate over the packets and send them out.
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |             length            |          vorbis data         ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        vorbis data                           |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |            length             |   next vorbis packet data    ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        vorbis data                           |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*
   */
  timestamp = gst_rtp_buffer_get_timestamp (buf);

  while (payload_len > 2) {
    guint16 length;

    length = GST_READ_UINT16_BE (payload);
    payload += 2;
    payload_len -= 2;

    GST_DEBUG_OBJECT (depayload, "read length %u, avail: %d", length,
        payload_len);

    /* skip packet if something odd happens */
    if (G_UNLIKELY (length > payload_len))
      goto length_short;

    /* handle in-band configuration */
    if (G_UNLIKELY (VDT == 1)) {
      GST_DEBUG_OBJECT (rtpvorbisdepay, "in-band configuration");
      if (!gst_rtp_vorbis_depay_parse_inband_configuration (rtpvorbisdepay,
              ident, payload, payload_len, length))
        goto invalid_configuration;
      goto no_output;
    }

    /* create buffer for packet */
    if (G_UNLIKELY (to_free)) {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = payload;
      GST_BUFFER_MALLOCDATA (outbuf) = to_free;
      GST_BUFFER_SIZE (outbuf) = length;
      to_free = NULL;
    } else {
      outbuf = gst_buffer_new_and_alloc (length);
      memcpy (GST_BUFFER_DATA (outbuf), payload, length);
    }

    payload += length;
    payload_len -= length;

    if (timestamp != -1)
      /* push with timestamp of the last packet, which is the same timestamp that
       * should apply to the first assembled packet. */
      ret = gst_base_rtp_depayload_push_ts (depayload, timestamp, outbuf);
    else
      ret = gst_base_rtp_depayload_push (depayload, outbuf);

    if (ret != GST_FLOW_OK)
      break;

    /* make sure we don't set a timestamp on next buffers */
    timestamp = -1;
  }

  g_free (to_free);

  return NULL;

no_output:
  {
    return NULL;
  }
  /* ERORRS */
switch_failed:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Could not switch codebooks"));
    return NULL;
  }
packet_short:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet was too short (%d < 4)", payload_len));
    return NULL;
  }
ignore_reserved:
  {
    GST_WARNING_OBJECT (rtpvorbisdepay, "reserved VDT ignored");
    return NULL;
  }
length_short:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet contains invalid data"));
    return NULL;
  }
invalid_configuration:
  {
    /* fatal, as we otherwise risk carrying on without output */
    GST_ELEMENT_ERROR (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet contains invalid configuration"));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_vorbis_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstStateChangeReturn ret;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      free_indents (rtpvorbisdepay);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_vorbis_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvorbisdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_VORBIS_DEPAY);
}
