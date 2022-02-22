/* GStreamer
 * Copyright (C) 2008 Axis Communications <dev-gstreamer@axis.com>
 * @author Bjorn Ostby <bjorn.ostby@axis.com>
 * @author Peter Kjellerstedt <peter.kjellerstedt@axis.com>
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
 * SECTION:element-rtpjpegpay
 * @title: rtpjpegpay
 *
 * Payload encode JPEG pictures into RTP packets according to RFC 2435.
 * For detailed information see: http://www.rfc-editor.org/rfc/rfc2435.txt
 *
 * The payloader takes a JPEG picture, scans the header for quantization
 * tables (if needed) and constructs the RTP packet header followed by
 * the actual JPEG entropy scan.
 *
 * The payloader assumes that correct width and height is found in the caps.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

#include "gstrtpelements.h"
#include "gstrtpjpegpay.h"
#include "gstrtputils.h"
#include "gstbuffermemory.h"

static GstStaticPadTemplate gst_rtp_jpeg_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg; " "video/x-jpeg")
    );

static GstStaticPadTemplate gst_rtp_jpeg_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "  media = (string) \"video\", "
        "  payload = (int) " GST_RTP_PAYLOAD_JPEG_STRING ", "
        "  clock-rate = (int) 90000,   "
        "  encoding-name = (string) \"JPEG\", "
        "  width = (int) [ 1, 65536 ], " "  height = (int) [ 1, 65536 ]; "
        " application/x-rtp, "
        "  media = (string) \"video\", "
        "  payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "  clock-rate = (int) 90000,   "
        "  encoding-name = (string) \"JPEG\", "
        "  width = (int) [ 1, 65536 ], " "  height = (int) [ 1, 65536 ]")
    );

GST_DEBUG_CATEGORY_STATIC (rtpjpegpay_debug);
#define GST_CAT_DEFAULT (rtpjpegpay_debug)

/*
 * QUANT_PREFIX_LEN:
 *
 * Prefix length in the header before the quantization tables:
 * Two size bytes and one byte for precision
 */
#define QUANT_PREFIX_LEN     3


typedef enum _RtpJpegMarker RtpJpegMarker;

/*
 * RtpJpegMarker:
 * @JPEG_MARKER: Prefix for JPEG marker
 * @JPEG_MARKER_SOI: Start of Image marker
 * @JPEG_MARKER_JFIF: JFIF marker
 * @JPEG_MARKER_CMT: Comment marker
 * @JPEG_MARKER_DQT: Define Quantization Table marker
 * @JPEG_MARKER_SOF: Start of Frame marker
 * @JPEG_MARKER_DHT: Define Huffman Table marker
 * @JPEG_MARKER_SOS: Start of Scan marker
 * @JPEG_MARKER_EOI: End of Image marker
 * @JPEG_MARKER_DRI: Define Restart Interval marker
 * @JPEG_MARKER_H264: H264 marker
 *
 * Identifiers for markers in JPEG header
 */
enum _RtpJpegMarker
{
  JPEG_MARKER = 0xFF,
  JPEG_MARKER_SOI = 0xD8,
  JPEG_MARKER_JFIF = 0xE0,
  JPEG_MARKER_CMT = 0xFE,
  JPEG_MARKER_DQT = 0xDB,
  JPEG_MARKER_SOF = 0xC0,
  JPEG_MARKER_DHT = 0xC4,
  JPEG_MARKER_JPG = 0xC8,
  JPEG_MARKER_SOS = 0xDA,
  JPEG_MARKER_EOI = 0xD9,
  JPEG_MARKER_DRI = 0xDD,
  JPEG_MARKER_APP0 = 0xE0,
  JPEG_MARKER_H264 = 0xE4,      /* APP4 */
  JPEG_MARKER_APP15 = 0xEF,
  JPEG_MARKER_JPG0 = 0xF0,
  JPEG_MARKER_JPG13 = 0xFD
};

#define DEFAULT_JPEG_QUANT    255

#define DEFAULT_JPEG_QUALITY  255
#define DEFAULT_JPEG_TYPE     1

enum
{
  PROP_0,
  PROP_JPEG_QUALITY,
  PROP_JPEG_TYPE
};

enum
{
  Q_TABLE_0 = 0,
  Q_TABLE_1,
  Q_TABLE_MAX                   /* only support for two tables at the moment */
};

typedef struct _RtpJpegHeader RtpJpegHeader;

/*
 * RtpJpegHeader:
 * @type_spec: type specific
 * @offset: fragment offset
 * @type: type field
 * @q: quantization table for this frame
 * @width: width of image in 8-pixel multiples
 * @height: height of image in 8-pixel multiples
 *
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Type-specific |              Fragment Offset                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Type     |       Q       |     Width     |     Height    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct _RtpJpegHeader
{
  guint type_spec:8;
  guint offset:24;
  guint8 type;
  guint8 q;
  guint8 width;
  guint8 height;
};

/*
 * RtpQuantHeader
 * @mbz: must be zero
 * @precision: specify size of quantization tables
 * @length: length of quantization data
 *
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      MBZ      |   Precision   |             Length            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Quantization Table Data                    |
 * |                              ...                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
typedef struct
{
  guint8 mbz;
  guint8 precision;
  guint16 length;
} RtpQuantHeader;

typedef struct
{
  guint8 size;
  const guint8 *data;
} RtpQuantTable;

/*
 * RtpRestartMarkerHeader:
 * @restartInterval: number of MCUs that appear between restart markers
 * @restartFirstLastCount: a combination of the first packet mark in the chunk
 *                         last packet mark in the chunk and the position of the
 *                         first restart interval in the current "chunk"
 *
 *    0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |       Restart Interval        |F|L|       Restart Count       |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *  The restart marker header is implemented according to the following
 *  methodology specified in section 3.1.7 of rfc2435.txt.
 *
 *  "If the restart intervals in a frame are not guaranteed to be aligned
 *  with packet boundaries, the F (first) and L (last) bits MUST be set
 *  to 1 and the Restart Count MUST be set to 0x3FFF.  This indicates
 *  that a receiver MUST reassemble the entire frame before decoding it."
 *
 */

typedef struct
{
  guint16 restart_interval;
  guint16 restart_count;
} RtpRestartMarkerHeader;

typedef struct
{
  guint8 id;
  guint8 samp;
  guint8 qt;
} CompInfo;

/* FIXME: restart marker header currently unsupported */

static void gst_rtp_jpeg_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_rtp_jpeg_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtp_jpeg_pay_setcaps (GstRTPBasePayload * basepayload,
    GstCaps * caps);

static GstFlowReturn gst_rtp_jpeg_pay_handle_buffer (GstRTPBasePayload * pad,
    GstBuffer * buffer);

#define gst_rtp_jpeg_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpJPEGPay, gst_rtp_jpeg_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpjpegpay, "rtpjpegpay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_JPEG_PAY, rtp_element_init (plugin));

static void
gst_rtp_jpeg_pay_class_init (GstRtpJPEGPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->set_property = gst_rtp_jpeg_pay_set_property;
  gobject_class->get_property = gst_rtp_jpeg_pay_get_property;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_jpeg_pay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_jpeg_pay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "RTP JPEG payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encodes JPEG pictures into RTP packets (RFC 2435)",
      "Axis Communications <dev-gstreamer@axis.com>");

  gstrtpbasepayload_class->set_caps = gst_rtp_jpeg_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_jpeg_pay_handle_buffer;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_JPEG_QUALITY,
      g_param_spec_int ("quality", "Quality",
          "Quality factor on JPEG data (unused)", 0, 255, 255,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_JPEG_TYPE,
      g_param_spec_int ("type", "Type",
          "Default JPEG Type, overwritten by SOF when present", 0, 255,
          DEFAULT_JPEG_TYPE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (rtpjpegpay_debug, "rtpjpegpay", 0,
      "Motion JPEG RTP Payloader");
}

static void
gst_rtp_jpeg_pay_init (GstRtpJPEGPay * pay)
{
  pay->quality = DEFAULT_JPEG_QUALITY;
  pay->quant = DEFAULT_JPEG_QUANT;
  pay->type = DEFAULT_JPEG_TYPE;
  pay->width = -1;
  pay->height = -1;

  GST_RTP_BASE_PAYLOAD_PT (pay) = GST_RTP_PAYLOAD_JPEG;
}

static gboolean
gst_rtp_jpeg_pay_setcaps (GstRTPBasePayload * basepayload, GstCaps * caps)
{
  GstStructure *caps_structure = gst_caps_get_structure (caps, 0);
  GstRtpJPEGPay *pay;
  gboolean res;
  gint width = -1, height = -1;
  gint num = 0, denom;
  gchar *rate = NULL;
  gchar *dim = NULL;

  pay = GST_RTP_JPEG_PAY (basepayload);

  /* these properties are mandatory, but they might be adjusted by the SOF, if there
   * is one. */
  if (!gst_structure_get_int (caps_structure, "height", &height) || height <= 0) {
    goto invalid_dimension;
  }

  if (!gst_structure_get_int (caps_structure, "width", &width) || width <= 0) {
    goto invalid_dimension;
  }

  if (gst_structure_get_fraction (caps_structure, "framerate", &num, &denom) &&
      (num < 0 || denom <= 0)) {
    goto invalid_framerate;
  }

  if (height > 2040 || width > 2040) {
    pay->height = 0;
    pay->width = 0;
  } else {
    pay->height = GST_ROUND_UP_8 (height) / 8;
    pay->width = GST_ROUND_UP_8 (width) / 8;
  }

  gst_rtp_base_payload_set_options (basepayload, "video",
      basepayload->pt != GST_RTP_PAYLOAD_JPEG, "JPEG", 90000);

  if (num > 0) {
    gdouble framerate;
    gst_util_fraction_to_double (num, denom, &framerate);
    rate = g_strdup_printf ("%f", framerate);
  }

  if (pay->width == 0) {
    GST_DEBUG_OBJECT (pay,
        "width or height are greater than 2040, adding x-dimensions to caps");
    dim = g_strdup_printf ("%d,%d", width, height);
  }

  if (rate != NULL && dim != NULL) {
    res = gst_rtp_base_payload_set_outcaps (basepayload, "a-framerate",
        G_TYPE_STRING, rate, "x-dimensions", G_TYPE_STRING, dim, NULL);
  } else if (rate != NULL && dim == NULL) {
    res = gst_rtp_base_payload_set_outcaps (basepayload, "a-framerate",
        G_TYPE_STRING, rate, NULL);
  } else if (rate == NULL && dim != NULL) {
    res = gst_rtp_base_payload_set_outcaps (basepayload, "x-dimensions",
        G_TYPE_STRING, dim, NULL);
  } else {
    res = gst_rtp_base_payload_set_outcaps (basepayload, NULL);
  }

  g_free (dim);
  g_free (rate);

  return res;

  /* ERRORS */
invalid_dimension:
  {
    GST_ERROR_OBJECT (pay, "Invalid width/height from caps");
    return FALSE;
  }
invalid_framerate:
  {
    GST_ERROR_OBJECT (pay, "Invalid framerate from caps");
    return FALSE;
  }
}

/*
 * get uint16 value from current position in mapped memory.
 * the memory offset will be increased with 2.
 */
static guint
parse_mem_inc_offset_guint16 (GstBufferMemoryMap * memory)
{
  guint data;

  g_return_val_if_fail (memory->total_size > (memory->offset + 1), 0);

  data = ((guint) * memory->data) << 8;
  gst_buffer_memory_advance_bytes (memory, 1);
  data = data | (*memory->data);
  gst_buffer_memory_advance_bytes (memory, 1);

  return data;
}

/*
 * get uint8 value from current position in mapped memory.
 * the memory offset will be increased with 1.
 */
static guint
parse_mem_inc_offset_guint8 (GstBufferMemoryMap * memory)
{
  guint data;

  g_return_val_if_fail (memory->total_size > memory->offset, 0);

  data = (*memory->data);
  gst_buffer_memory_advance_bytes (memory, 1);

  return data;
}

static void
gst_rtp_jpeg_pay_read_quant_table (GstBufferMemoryMap * memory,
    RtpQuantTable tables[])
{
  guint quant_size, tab_size;
  guint8 prec;
  guint8 id;

  if (memory->total_size <= (memory->offset + 1))
    goto too_small;

  quant_size = parse_mem_inc_offset_guint16 (memory);
  if (quant_size < 2)
    goto small_quant_size;

  /* clamp to available data */
  if (memory->offset + quant_size > memory->total_size)
    quant_size = memory->total_size - memory->offset;

  quant_size -= 2;

  while (quant_size > 0) {
    guint8 data;
    /* not enough to read the id */
    if (memory->offset + 1 > memory->total_size)
      break;

    data = parse_mem_inc_offset_guint8 (memory);
    id = data & 0x0f;
    if (id == 15)
      /* invalid id received - corrupt data */
      goto invalid_id;

    prec = (data & 0xf0) >> 4;
    if (prec)
      tab_size = 128;
    else
      tab_size = 64;

    /* there is not enough for the table */
    if (quant_size < tab_size + 1)
      goto no_table;

    GST_LOG ("read quant table %d, tab_size %d, prec %02x", id, tab_size, prec);

    tables[id].size = tab_size;
    tables[id].data = memory->data;

    quant_size -= (tab_size + 1);
    if (!gst_buffer_memory_advance_bytes (memory, tab_size)) {
      goto too_small;
    }
  }
done:
  return;

  /* ERRORS */
too_small:
  {
    GST_WARNING ("not enough data");
    return;
  }
small_quant_size:
  {
    GST_WARNING ("quant_size too small (%u < 2)", quant_size);
    return;
  }
invalid_id:
  {
    GST_WARNING ("invalid id");
    goto done;
  }
no_table:
  {
    GST_WARNING ("not enough data for table (%u < %u)", quant_size,
        tab_size + 1);
    goto done;
  }
}

static gboolean
gst_rtp_jpeg_pay_read_sof (GstRtpJPEGPay * pay, GstBufferMemoryMap * memory,
    CompInfo info[], RtpQuantTable tables[], gulong tables_elements)
{
  guint sof_size, off;
  guint width, height, infolen;
  CompInfo elem;
  gint i, j;

  off = memory->offset;

  /* we need at least 17 bytes for the SOF */
  if (off + 17 > memory->total_size)
    goto wrong_size;

  sof_size = parse_mem_inc_offset_guint16 (memory);
  if (sof_size < 17)
    goto wrong_length;

  /* precision should be 8 */
  if (parse_mem_inc_offset_guint8 (memory) != 8)
    goto bad_precision;

  /* read dimensions */
  height = parse_mem_inc_offset_guint16 (memory);
  width = parse_mem_inc_offset_guint16 (memory);

  GST_LOG_OBJECT (pay, "got dimensions %ux%u", height, width);

  if (height == 0) {
    goto invalid_dimension;
  }
  if (height > 2040) {
    height = 0;
  }
  if (width == 0) {
    goto invalid_dimension;
  }
  if (width > 2040) {
    width = 0;
  }

  if (height == 0 || width == 0) {
    pay->height = 0;
    pay->width = 0;
  } else {
    pay->height = GST_ROUND_UP_8 (height) / 8;
    pay->width = GST_ROUND_UP_8 (width) / 8;
  }

  /* we only support 3 components */
  if (parse_mem_inc_offset_guint8 (memory) != 3)
    goto bad_components;

  infolen = 0;
  for (i = 0; i < 3; i++) {
    elem.id = parse_mem_inc_offset_guint8 (memory);
    elem.samp = parse_mem_inc_offset_guint8 (memory);
    elem.qt = parse_mem_inc_offset_guint8 (memory);
    GST_LOG_OBJECT (pay, "got comp %d, samp %02x, qt %d", elem.id, elem.samp,
        elem.qt);
    /* insertion sort from the last element to the first */
    for (j = infolen; j > 1; j--) {
      if (G_LIKELY (info[j - 1].id < elem.id))
        break;
      info[j] = info[j - 1];
    }
    info[j] = elem;
    infolen++;
  }

  /* see that the components are supported */
  if (info[0].samp == 0x21)
    pay->type = 0;
  else if (info[0].samp == 0x22)
    pay->type = 1;
  else
    goto invalid_comp;

  if (!(info[1].samp == 0x11))
    goto invalid_comp;

  if (!(info[2].samp == 0x11))
    goto invalid_comp;

  return TRUE;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT,
        ("Wrong size %u (needed %u).", (guint) memory->total_size, off + 17),
        (NULL));
    return FALSE;
  }
wrong_length:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT,
        ("Wrong SOF length %u.", sof_size), (NULL));
    return FALSE;
  }
bad_precision:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT,
        ("Wrong precision, expecting 8."), (NULL));
    return FALSE;
  }
invalid_dimension:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT,
        ("Wrong dimension, size %ux%u", width, height), (NULL));
    return FALSE;
  }
bad_components:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT,
        ("Wrong number of components"), (NULL));
    return FALSE;
  }
invalid_comp:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT, ("Invalid component"), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtp_jpeg_pay_read_dri (GstRtpJPEGPay * pay, GstBufferMemoryMap * memory,
    RtpRestartMarkerHeader * dri)
{
  guint dri_size, restart_interval;

  /* we need at least 4 bytes for the DRI */
  if (memory->offset + 4 > memory->total_size)
    goto wrong_size;

  dri_size = parse_mem_inc_offset_guint16 (memory);
  if (dri_size < 4)
    goto wrong_length;

  restart_interval = parse_mem_inc_offset_guint16 (memory);
  dri->restart_interval = g_htons (restart_interval);
  dri->restart_count = g_htons (0xFFFF);
  if (!gst_buffer_memory_advance_bytes (memory, dri_size - 4)) {
    goto wrong_size;
  }

  return dri->restart_interval > 0;

wrong_size:
  {
    GST_WARNING ("not enough data for DRI");
    return FALSE;
  }
wrong_length:
  {
    GST_WARNING ("DRI size too small (%u)", dri_size);
    /* offset got incremented by two when dri_size was parsed. */
    if (dri_size > 2)
      gst_buffer_memory_advance_bytes (memory, dri_size - 2);
    return FALSE;
  }
}

static void
gst_rtp_jpeg_pay_skipping_marker (GstBufferMemoryMap * memory)
{
  guint skip;

  if (G_UNLIKELY (((memory->offset + 1) >= memory->total_size))) {
    goto wrong_size;
  }
  skip = parse_mem_inc_offset_guint16 (memory);

  if (G_UNLIKELY (((skip - 2 + memory->offset) > memory->total_size))) {
    goto wrong_size;
  }
  if (skip > 2) {
    gst_buffer_memory_advance_bytes (memory, skip - 2);
  }
  return;

wrong_size:
  {
    GST_WARNING ("not enough data");
  }
}

static RtpJpegMarker
gst_rtp_jpeg_pay_scan_marker (GstBufferMemoryMap * memory)
{
  guint8 marker = parse_mem_inc_offset_guint8 (memory);

  while (marker != JPEG_MARKER && ((memory->offset) < memory->total_size)) {
    marker = parse_mem_inc_offset_guint8 (memory);
  }

  if (G_UNLIKELY ((memory->offset) >= memory->total_size)) {
    GST_LOG ("found EOI marker");
    return JPEG_MARKER_EOI;
  } else {
    marker = parse_mem_inc_offset_guint8 (memory);
    return marker;
  }
}

#define RTP_HEADER_LEN 12

static GstFlowReturn
gst_rtp_jpeg_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpJPEGPay *pay;
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_ERROR;
  RtpJpegHeader jpeg_header;
  RtpQuantHeader quant_header;
  RtpRestartMarkerHeader restart_marker_header;
  RtpQuantTable tables[15] = { {0, NULL}, };
  CompInfo info[3] = { {0,}, };
  guint quant_data_size;
  guint mtu, max_payload_size;
  guint bytes_left;
  guint jpeg_header_size = 0;
  guint offset;
  gboolean frame_done;
  gboolean sos_found, sof_found, dqt_found, dri_found;
  gint i;
  GstBufferList *list = NULL;
  gboolean discont;
  GstBufferMemoryMap memory;

  pay = GST_RTP_JPEG_PAY (basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU (pay);

  gst_buffer_memory_map (buffer, &memory);

  timestamp = GST_BUFFER_PTS (buffer);
  discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_LOG_OBJECT (pay, "got buffer size %" G_GSIZE_FORMAT
      " , timestamp %" GST_TIME_FORMAT, memory.total_size,
      GST_TIME_ARGS (timestamp));

  /* parse the jpeg header for 'start of scan' and read quant tables if needed */
  sos_found = FALSE;
  dqt_found = FALSE;
  sof_found = FALSE;
  dri_found = FALSE;

  while (!sos_found && (memory.offset < memory.total_size)) {
    gint marker;

    GST_LOG_OBJECT (pay, "checking from offset %u", memory.offset);
    marker = gst_rtp_jpeg_pay_scan_marker (&memory);
    switch (marker) {
      case JPEG_MARKER_JFIF:
      case JPEG_MARKER_CMT:
      case JPEG_MARKER_DHT:
      case JPEG_MARKER_H264:
        GST_LOG_OBJECT (pay, "skipping marker");
        gst_rtp_jpeg_pay_skipping_marker (&memory);
        break;
      case JPEG_MARKER_SOF:
        if (!gst_rtp_jpeg_pay_read_sof (pay, &memory, info, tables,
                G_N_ELEMENTS (tables)))
          goto invalid_format;
        sof_found = TRUE;
        break;
      case JPEG_MARKER_DQT:
        GST_LOG ("DQT found");
        gst_rtp_jpeg_pay_read_quant_table (&memory, tables);
        dqt_found = TRUE;
        break;
      case JPEG_MARKER_SOS:
        sos_found = TRUE;
        GST_LOG_OBJECT (pay, "SOS found");
        jpeg_header_size = memory.offset;
        /* Do not re-combine into single statement with previous line! */
        jpeg_header_size += parse_mem_inc_offset_guint16 (&memory);
        break;
      case JPEG_MARKER_EOI:
        GST_WARNING_OBJECT (pay, "EOI reached before SOS!");
        break;
      case JPEG_MARKER_SOI:
        GST_LOG_OBJECT (pay, "SOI found");
        break;
      case JPEG_MARKER_DRI:
        GST_LOG_OBJECT (pay, "DRI found");
        if (gst_rtp_jpeg_pay_read_dri (pay, &memory, &restart_marker_header))
          dri_found = TRUE;
        break;
      default:
        if (marker == JPEG_MARKER_JPG ||
            (marker >= JPEG_MARKER_JPG0 && marker <= JPEG_MARKER_JPG13) ||
            (marker >= JPEG_MARKER_APP0 && marker <= JPEG_MARKER_APP15)) {
          GST_LOG_OBJECT (pay, "skipping marker");
          gst_rtp_jpeg_pay_skipping_marker (&memory);
        } else {
          /* no need to do anything, gst_rtp_jpeg_pay_scan_marker will go on */
          GST_FIXME_OBJECT (pay, "unhandled marker 0x%02x", marker);
        }
        break;
    }
  }
  if (!dqt_found || !sof_found)
    goto unsupported_jpeg;

  /* by now we should either have negotiated the width/height or the SOF header
   * should have filled us in */
  if (pay->width < 0 || pay->height < 0) {
    goto no_dimension;
  }

  GST_LOG_OBJECT (pay, "header size %u", jpeg_header_size);

  offset = 0;

  if (dri_found)
    pay->type += 64;

  /* prepare stuff for the jpeg header */
  jpeg_header.type_spec = 0;
  jpeg_header.type = pay->type;
  jpeg_header.q = pay->quant;
  jpeg_header.width = pay->width;
  jpeg_header.height = pay->height;
  /* collect the quant headers sizes */
  quant_header.mbz = 0;
  quant_header.precision = 0;
  quant_header.length = 0;
  quant_data_size = 0;

  if (pay->quant > 127) {
    /* for the Y and U component, look up the quant table and its size. quant
     * tables for U and V should be the same */
    for (i = 0; i < 2; i++) {
      guint qsize;
      guint qt;

      qt = info[i].qt;
      if (qt >= G_N_ELEMENTS (tables))
        goto invalid_quant;

      qsize = tables[qt].size;
      if (qsize == 0)
        goto invalid_quant;

      quant_header.precision |= (qsize == 64 ? 0 : (1 << i));
      quant_data_size += qsize;
    }
    quant_header.length = g_htons (quant_data_size);
    quant_data_size += sizeof (quant_header);
  }

  GST_LOG_OBJECT (pay, "quant_data size %u", quant_data_size);

  bytes_left =
      sizeof (jpeg_header) + quant_data_size + memory.total_size -
      jpeg_header_size;

  if (dri_found)
    bytes_left += sizeof (restart_marker_header);

  max_payload_size = mtu - (RTP_HEADER_LEN + sizeof (jpeg_header));
  list = gst_buffer_list_new_sized ((bytes_left / max_payload_size) + 1);

  frame_done = FALSE;
  do {
    GstBuffer *outbuf;
    guint8 *payload;
    guint payload_size;
    guint header_size;
    GstBuffer *paybuf;
    GstRTPBuffer rtp = { NULL };
    guint rtp_header_size = gst_rtp_buffer_calc_header_len (0);

    /* The available room is the packet MTU, minus the RTP header length. */
    payload_size =
        (bytes_left < (mtu - rtp_header_size) ? bytes_left :
        (mtu - rtp_header_size));

    header_size = sizeof (jpeg_header) + quant_data_size;
    if (dri_found)
      header_size += sizeof (restart_marker_header);

    outbuf =
        gst_rtp_base_payload_allocate_output_buffer (basepayload, header_size,
        0, 0);

    gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

    if (payload_size == bytes_left) {
      GST_LOG_OBJECT (pay, "last packet of frame");
      frame_done = TRUE;
      gst_rtp_buffer_set_marker (&rtp, 1);
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_MARKER);
    }

    payload = gst_rtp_buffer_get_payload (&rtp);

    /* update offset */
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    jpeg_header.offset = ((offset & 0x0000FF) << 16) |
        ((offset & 0xFF0000) >> 16) | (offset & 0x00FF00);
#else
    jpeg_header.offset = offset;
#endif
    memcpy (payload, &jpeg_header, sizeof (jpeg_header));
    payload += sizeof (jpeg_header);
    payload_size -= sizeof (jpeg_header);

    if (dri_found) {
      memcpy (payload, &restart_marker_header, sizeof (restart_marker_header));
      payload += sizeof (restart_marker_header);
      payload_size -= sizeof (restart_marker_header);
    }

    /* only send quant table with first packet */
    if (G_UNLIKELY (quant_data_size > 0)) {
      memcpy (payload, &quant_header, sizeof (quant_header));
      payload += sizeof (quant_header);

      /* copy the quant tables for luma and chrominance */
      for (i = 0; i < 2; i++) {
        guint qsize;
        guint qt;

        qt = info[i].qt;
        qsize = tables[qt].size;
        memcpy (payload, tables[qt].data, qsize);

        GST_LOG_OBJECT (pay, "component %d using quant %d, size %d", i, qt,
            qsize);

        payload += qsize;
      }
      payload_size -= quant_data_size;
      bytes_left -= quant_data_size;
      quant_data_size = 0;
    }
    GST_LOG_OBJECT (pay, "sending payload size %d", payload_size);
    gst_rtp_buffer_unmap (&rtp);

    /* create a new buf to hold the payload */
    paybuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
        jpeg_header_size + offset, payload_size);

    /* join memory parts */
    gst_rtp_copy_video_meta (pay, outbuf, paybuf);
    outbuf = gst_buffer_append (outbuf, paybuf);

    GST_BUFFER_PTS (outbuf) = timestamp;

    if (discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      /* Only the first outputted buffer has the DISCONT flag */
      discont = FALSE;
    }

    /* and add to list */
    gst_buffer_list_insert (list, -1, outbuf);

    bytes_left -= payload_size;
    offset += payload_size;
  }
  while (!frame_done);
  /* push the whole buffer list at once */
  ret = gst_rtp_base_payload_push_list (basepayload, list);

  gst_buffer_memory_unmap (&memory);
  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
unsupported_jpeg:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT, ("Unsupported JPEG"), (NULL));
    gst_buffer_memory_unmap (&memory);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
no_dimension:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT, ("No size given"), (NULL));
    gst_buffer_memory_unmap (&memory);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
invalid_format:
  {
    /* error was posted */
    gst_buffer_memory_unmap (&memory);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
invalid_quant:
  {
    GST_ELEMENT_WARNING (pay, STREAM, FORMAT, ("Invalid quant tables"), (NULL));
    gst_buffer_memory_unmap (&memory);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static void
gst_rtp_jpeg_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpJPEGPay *rtpjpegpay;

  rtpjpegpay = GST_RTP_JPEG_PAY (object);

  switch (prop_id) {
    case PROP_JPEG_QUALITY:
      rtpjpegpay->quality = g_value_get_int (value);
      GST_DEBUG_OBJECT (object, "quality = %d", rtpjpegpay->quality);
      break;
    case PROP_JPEG_TYPE:
      rtpjpegpay->type = g_value_get_int (value);
      GST_DEBUG_OBJECT (object, "type = %d", rtpjpegpay->type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_jpeg_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpJPEGPay *rtpjpegpay;

  rtpjpegpay = GST_RTP_JPEG_PAY (object);

  switch (prop_id) {
    case PROP_JPEG_QUALITY:
      g_value_set_int (value, rtpjpegpay->quality);
      break;
    case PROP_JPEG_TYPE:
      g_value_set_int (value, rtpjpegpay->type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
