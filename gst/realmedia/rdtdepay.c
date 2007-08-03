/* GStreamer
 * Copyright (C) <2006> Lutz Mueller <lutz at topfrose dot de>
 *               <2006> Wim Taymans <wim@fluendo.com>
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
#include "rdtdepay.h"

GST_DEBUG_CATEGORY_STATIC (rdtdepay_debug);
#define GST_CAT_DEFAULT rdtdepay_debug

/* elementfactory information */
static const GstElementDetails gst_rdtdepay_details =
GST_ELEMENT_DETAILS ("RDT packet parser",
    "Codec/Depayloader/Network",
    "Extracts RealMedia from RDT packets",
    "Lutz Mueller <lutz at topfrose dot de>, " "Wim Taymans <wim@fluendo.com>");

/* RDTDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate gst_rdt_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstStaticPadTemplate gst_rdt_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rdt, "
        "media = (string) \"application\", "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"X-REAL-RDT\""
        /* All optional parameters
         *
         * "config=" 
         */
    )
    );

GST_BOILERPLATE (GstRDTDepay, gst_rdt_depay, GstElement, GST_TYPE_ELEMENT);

static gboolean gst_rdt_depay_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rdt_depay_chain (GstPad * pad, GstBuffer * buf);

static void gst_rdt_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rdt_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rdt_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rdt_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rdtdepay_details);

  GST_DEBUG_CATEGORY_INIT (rdtdepay_debug, "rdtdepay",
      0, "Depayloader for RDT RealMedia packets");
}

static void
gst_rdt_depay_class_init (GstRDTDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_rdt_depay_set_property;
  gobject_class->get_property = gst_rdt_depay_get_property;

  gstelement_class->change_state = gst_rdt_depay_change_state;
}

static void
gst_rdt_depay_init (GstRDTDepay * rdtdepay, GstRDTDepayClass * klass)
{
  rdtdepay->sinkpad =
      gst_pad_new_from_static_template (&gst_rdt_depay_sink_template, "sink");
  gst_pad_set_chain_function (rdtdepay->sinkpad, gst_rdt_depay_chain);
  gst_pad_set_setcaps_function (rdtdepay->sinkpad, gst_rdt_depay_setcaps);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtdepay), rdtdepay->sinkpad);

  rdtdepay->srcpad =
      gst_pad_new_from_static_template (&gst_rdt_depay_src_template, "src");
  gst_element_add_pad (GST_ELEMENT_CAST (rdtdepay), rdtdepay->srcpad);
}

static gboolean
gst_rdt_depay_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstRDTDepay *rdtdepay;
  GstCaps *srccaps;
  gint clock_rate = 1000;       /* default */
  const GValue *config;
  GstBuffer *header;

  rdtdepay = GST_RDT_DEPAY (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field (structure, "clock-rate"))
    gst_structure_get_int (structure, "clock-rate", &clock_rate);

  /* config contains the RealMedia header as a buffer. */
  config = gst_structure_get_value (structure, "config");
  if (!config)
    goto no_header;

  header = gst_value_get_buffer (config);
  if (!header)
    goto no_header;

  /* need to ref because we are going to give away a ref in push */
  gst_buffer_ref (header);

  /* caps seem good, configure element */
  rdtdepay->clock_rate = clock_rate;

  /* set caps on pad and on header */
  srccaps = gst_caps_new_simple ("application/vnd.rn-realmedia", NULL);
  gst_pad_set_caps (rdtdepay->srcpad, srccaps);
  gst_buffer_set_caps (header, srccaps);
  gst_caps_unref (srccaps);

  /* push header data first */
  gst_pad_push (rdtdepay->srcpad, header);

  return TRUE;

  /* ERRORS */
no_header:
  {
    GST_ERROR_OBJECT (rdtdepay, "no header found in caps, no 'config' field");
    return FALSE;
  }
}

#define ASSERT_SIZE(n) if (size < (n)) goto not_enough_data;

static GstFlowReturn
gst_rdt_depay_chain (GstPad * pad, GstBuffer * buf)
{
  GstRDTDepay *rdtdepay;
  GstFlowReturn ret;
  GstBuffer *outbuf;
  guint8 *data, *outdata;
  guint size, channel = 0;
  gboolean length_included_flag;
  gboolean need_reliable_flag = 0;
  gboolean is_reliable;
  guint16 seq_no;
  gboolean back_to_back;
  gboolean slow_data;
  guint asm_rule;
  guint32 ts;
  guint16 total_reliable;
  guint16 packet_type, packet_length;

  rdtdepay = GST_RDT_DEPAY (GST_PAD_PARENT (pad));

  /* data is in RDT format. */
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  while (size > 0) {
    ASSERT_SIZE (1);
    length_included_flag = (data[0] & 0x80) >> 7;
    need_reliable_flag = (data[0] & 0x40) >> 6;
    is_reliable = (data[0] & 0x01) >> 0;
    channel = (data[0] & 0x3e) >> 1;

    GST_DEBUG_OBJECT (rdtdepay, "length_included_flag=%i "
        "need_reliable_flag=%i is_reliable=%i", length_included_flag,
        need_reliable_flag, is_reliable);

    /* we can stop skipping */
    if (!length_included_flag)
      break;

    ASSERT_SIZE (5);
    packet_type = GST_READ_UINT16_BE (data + 1);
    packet_length = GST_READ_UINT16_BE (data + 3);

    GST_DEBUG_OBJECT (rdtdepay, "Skipping packet of type %02x and length=%d...",
        packet_type, packet_length);

    ASSERT_SIZE (packet_length);
    data += packet_length;
    size -= packet_length;
  }

  ASSERT_SIZE (3);
  seq_no = GST_READ_UINT16_BE (data + 1);

  ASSERT_SIZE (4);
  back_to_back = (data[3] >> 7) & 0x01;
  slow_data = (data[3] >> 6) & 0x01;
  asm_rule = ((data[3] << 2) & 0xf) >> 2;

  ASSERT_SIZE (8);
  ts = GST_READ_UINT32_BE (data + 4);

  if (need_reliable_flag) {
    ASSERT_SIZE (10);
    total_reliable = GST_READ_UINT16_BE (data + 8);
    data += 10;
    size -= 10;
  } else {
    data += 9;
    size -= 9;
  }

  GST_DEBUG_OBJECT (rdtdepay, "Passing on packet %d: "
      "back_to_back=%i slow_data=%i asm_rule=%i timestamp=%u",
      seq_no, back_to_back, slow_data, asm_rule, ts);

  outbuf = gst_buffer_new_and_alloc (12 + size);
  outdata = GST_BUFFER_DATA (outbuf);
  GST_BUFFER_TIMESTAMP (outbuf) =
      gst_util_uint64_scale_int (ts, GST_SECOND, rdtdepay->clock_rate);

  GST_WRITE_UINT16_BE (outdata + 0, 0); /* version   */
  GST_WRITE_UINT16_BE (outdata + 2, size + 12); /* length    */
  GST_WRITE_UINT16_BE (outdata + 4, channel);   /* stream    */
  GST_WRITE_UINT32_BE (outdata + 6, ts);        /* timestamp */
  GST_WRITE_UINT16_BE (outdata + 10, 0);        /* flags     */
  memcpy (outdata + 12, data, size);
  gst_buffer_unref (buf);

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rdtdepay->srcpad));
  ret = gst_pad_push (rdtdepay->srcpad, outbuf);

  return ret;

  /* ERRORS */
not_enough_data:
  {
    GST_ELEMENT_WARNING (rdtdepay, STREAM, DECODE, (NULL),
        ("Not enough data."));
    return GST_FLOW_OK;
  }
}

static void
gst_rdt_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRDTDepay *rdtdepay;

  rdtdepay = GST_RDT_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rdt_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRDTDepay *rdtdepay;

  rdtdepay = GST_RDT_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rdt_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRDTDepay *rdtdepay;
  GstStateChangeReturn ret;

  rdtdepay = GST_RDT_DEPAY (element);

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
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rdt_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rdtdepay",
      GST_RANK_MARGINAL, GST_TYPE_RDT_DEPAY);
}
