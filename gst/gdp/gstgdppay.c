/* GStreamer
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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
 * SECTION:element-gdppay
 * @see_also: gdpdepay
 *
 * This element payloads GStreamer buffers and events using the
 * GStreamer Data Protocol.
 *
 * <refsect2>
 * |[
 * gst-launch -v -m videotestsrc num-buffers=50 ! gdppay ! filesink location=test.gdp
 * ]| This pipeline creates a serialized video stream that can be played back
 * with the example shown in gdpdepay.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dataprotocol.h"

#include "gstgdppay.h"

static GstStaticPadTemplate gdp_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gdp_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gdp"));

GST_DEBUG_CATEGORY_STATIC (gst_gdp_pay_debug);
#define GST_CAT_DEFAULT gst_gdp_pay_debug

#define DEFAULT_CRC_HEADER TRUE
#define DEFAULT_CRC_PAYLOAD FALSE
#define DEFAULT_VERSION GST_DP_VERSION_1_0

enum
{
  PROP_0,
  PROP_CRC_HEADER,
  PROP_CRC_PAYLOAD,
  PROP_VERSION,
};

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_gdp_pay_debug, "gdppay", 0, \
    "GDP payloader");
#define gst_gdp_pay_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGDPPay, gst_gdp_pay, GST_TYPE_ELEMENT, _do_init);

static void gst_gdp_pay_reset (GstGDPPay * this);

static GstFlowReturn gst_gdp_pay_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_gdp_pay_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_gdp_pay_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_gdp_pay_change_state (GstElement *
    element, GstStateChange transition);

static void gst_gdp_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gdp_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gdp_pay_finalize (GObject * gobject);

static void
gst_gdp_pay_class_init (GstGDPPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_gdp_pay_set_property;
  gobject_class->get_property = gst_gdp_pay_get_property;
  gobject_class->finalize = gst_gdp_pay_finalize;

  g_object_class_install_property (gobject_class, PROP_CRC_HEADER,
      g_param_spec_boolean ("crc-header", "CRC Header",
          "Calculate and store a CRC checksum on the header",
          DEFAULT_CRC_HEADER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CRC_PAYLOAD,
      g_param_spec_boolean ("crc-payload", "CRC Payload",
          "Calculate and store a CRC checksum on the payload",
          DEFAULT_CRC_PAYLOAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VERSION,
      g_param_spec_enum ("version", "Version",
          "Version of the GStreamer Data Protocol",
          GST_TYPE_DP_VERSION, DEFAULT_VERSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "GDP Payloader", "GDP/Payloader",
      "Payloads GStreamer Data Protocol buffers",
      "Thomas Vander Stichele <thomas at apestaart dot org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gdp_pay_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gdp_pay_src_template));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_gdp_pay_change_state);
}

static void
gst_gdp_pay_init (GstGDPPay * gdppay)
{
  gdppay->sinkpad =
      gst_pad_new_from_static_template (&gdp_pay_sink_template, "sink");
  gst_pad_set_chain_function (gdppay->sinkpad,
      GST_DEBUG_FUNCPTR (gst_gdp_pay_chain));
  gst_pad_set_event_function (gdppay->sinkpad,
      GST_DEBUG_FUNCPTR (gst_gdp_pay_sink_event));
  gst_element_add_pad (GST_ELEMENT (gdppay), gdppay->sinkpad);

  gdppay->srcpad =
      gst_pad_new_from_static_template (&gdp_pay_src_template, "src");
  gst_pad_set_event_function (gdppay->srcpad,
      GST_DEBUG_FUNCPTR (gst_gdp_pay_src_event));
  gst_element_add_pad (GST_ELEMENT (gdppay), gdppay->srcpad);

  gdppay->crc_header = DEFAULT_CRC_HEADER;
  gdppay->crc_payload = DEFAULT_CRC_PAYLOAD;
  gdppay->header_flag = gdppay->crc_header | gdppay->crc_payload;
  gdppay->version = DEFAULT_VERSION;
  gdppay->offset = 0;

  gdppay->packetizer = gst_dp_packetizer_new (gdppay->version);
}

static void
gst_gdp_pay_finalize (GObject * gobject)
{
  GstGDPPay *this = GST_GDP_PAY (gobject);

  gst_gdp_pay_reset (this);
  gst_dp_packetizer_free (this->packetizer);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (gobject));
}

static void
gst_gdp_pay_reset (GstGDPPay * this)
{
  GST_DEBUG_OBJECT (this, "Resetting GDP object");
  /* clear the queued buffers */
  while (this->queue) {
    GstBuffer *buffer;

    buffer = GST_BUFFER_CAST (this->queue->data);

    /* delete buffer from queue now */
    this->queue = g_list_delete_link (this->queue, this->queue);

    gst_buffer_unref (buffer);
  }
  if (this->caps) {
    gst_caps_unref (this->caps);
    this->caps = NULL;
  }
  if (this->caps_buf) {
    gst_buffer_unref (this->caps_buf);
    this->caps_buf = NULL;
  }
  if (this->tag_buf) {
    gst_buffer_unref (this->tag_buf);
    this->tag_buf = NULL;
  }
  if (this->new_segment_buf) {
    gst_buffer_unref (this->new_segment_buf);
    this->new_segment_buf = NULL;
  }
  if (this->streamstartid_buf) {
    gst_buffer_unref (this->streamstartid_buf);
    this->streamstartid_buf = NULL;
  }
  this->sent_streamheader = FALSE;
  this->offset = 0;
}

/* set OFFSET and OFFSET_END with running count */
static void
gst_gdp_stamp_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  GST_BUFFER_OFFSET (buffer) = this->offset;
  GST_BUFFER_OFFSET_END (buffer) = this->offset + gst_buffer_get_size (buffer);
  this->offset = GST_BUFFER_OFFSET_END (buffer);
}

static GstBuffer *
gst_gdp_buffer_from_caps (GstGDPPay * this, GstCaps * caps)
{
  GstBuffer *headerbuf;
  GstBuffer *payloadbuf;
  guint8 *header, *payload;
  guint len, plen;

  if (!this->packetizer->packet_from_caps (caps, this->header_flag, &len,
          &header, &payload))
    goto packet_failed;

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from caps");
  headerbuf = gst_buffer_new_wrapped (header, len);

  plen = gst_dp_header_payload_length (header);
  payloadbuf = gst_buffer_new_wrapped (payload, plen);

  return gst_buffer_append (headerbuf, payloadbuf);

  /* ERRORS */
packet_failed:
  {
    GST_WARNING_OBJECT (this, "could not create GDP header from caps");
    return NULL;
  }
}

static GstBuffer *
gst_gdp_pay_buffer_from_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  GstBuffer *headerbuf;
  guint8 *header;
  guint len;

  if (!this->packetizer->header_from_buffer (buffer, this->header_flag, &len,
          &header))
    goto no_buffer;

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from buffer");
  headerbuf = gst_buffer_new_wrapped (header, len);

  /* we do not want to lose the ref on the incoming buffer */
  gst_buffer_ref (buffer);

  return gst_buffer_append (headerbuf, buffer);

  /* ERRORS */
no_buffer:
  {
    GST_WARNING_OBJECT (this, "could not create GDP header from buffer");
    return NULL;
  }
}

static GstBuffer *
gst_gdp_buffer_from_event (GstGDPPay * this, GstEvent * event)
{
  GstBuffer *headerbuf;
  GstBuffer *payloadbuf;
  guint8 *header, *payload;
  guint len, plen;
  gboolean ret;

  ret =
      this->packetizer->packet_from_event (event, this->header_flag, &len,
      &header, &payload);
  if (!ret)
    goto no_event;

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from event");
  headerbuf = gst_buffer_new_wrapped (header, len);

  payloadbuf = gst_buffer_new ();
  plen = gst_dp_header_payload_length (header);
  if (plen && payload != NULL) {
    gst_buffer_append_memory (payloadbuf,
        gst_memory_new_wrapped (0, payload, plen, 0, plen, payload, g_free));
  }

  return gst_buffer_append (headerbuf, payloadbuf);

  /* ERRORS */
no_event:
  {
    GST_WARNING_OBJECT (this, "could not create GDP header from event %s (%d)",
        gst_event_type_get_name (event->type), event->type);
    return NULL;
  }
}


/* set our caps with streamheader, based on the latest newsegment and caps,
 * and (possibly) GDP-serialized buffers of the streamheaders on the src pad */
static GstFlowReturn
gst_gdp_pay_reset_streamheader (GstGDPPay * this)
{
  GstCaps *caps;
  /* We use copies of these to avoid circular refcounts */
  GstBuffer *new_segment_buf, *caps_buf, *tag_buf, *streamstartid_buf;
  GstStructure *structure;
  GstFlowReturn r = GST_FLOW_OK;
  gboolean version_one_zero = TRUE;

  GValue array = { 0 };
  GValue value = { 0 };

  GST_DEBUG_OBJECT (this, "start");
  /* In version 0.2, we didn't need or send new segment or tags */
  if (this->version == GST_DP_VERSION_0_2)
    version_one_zero = FALSE;

  if (version_one_zero) {
    if (!this->new_segment_buf || !this->caps_buf || !this->streamstartid_buf) {
      GST_DEBUG_OBJECT (this, "1.0, missing new_segment or caps or stream "
          "start id, returning");
      return GST_FLOW_OK;
    }
  } else {
    if (!this->caps_buf) {
      GST_DEBUG_OBJECT (this, "0.2, missing caps, returning");
      return GST_FLOW_OK;
    }
  }

  /* put copies of the buffers in a fixed list
   * Stamp the buffers with offset and offset_end as well.
   * We do this here so the offsets match the order the buffers go out in */
  g_value_init (&array, GST_TYPE_ARRAY);

  if (version_one_zero) {
    gst_gdp_stamp_buffer (this, this->streamstartid_buf);
    GST_DEBUG_OBJECT (this, "appending copy of stream start id buffer %p",
        this->streamstartid_buf);
    streamstartid_buf = gst_buffer_copy (this->streamstartid_buf);
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, streamstartid_buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    gst_buffer_unref (streamstartid_buf);
  }

  gst_gdp_stamp_buffer (this, this->caps_buf);
  GST_DEBUG_OBJECT (this, "appending copy of caps buffer %p", this->caps_buf);
  caps_buf = gst_buffer_copy (this->caps_buf);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, caps_buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  gst_buffer_unref (caps_buf);

  if (version_one_zero) {
    gst_gdp_stamp_buffer (this, this->new_segment_buf);
    GST_DEBUG_OBJECT (this, "1.0, appending copy of new segment buffer %p",
        this->new_segment_buf);
    new_segment_buf = gst_buffer_copy (this->new_segment_buf);
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, new_segment_buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    gst_buffer_unref (new_segment_buf);

    if (this->tag_buf) {
      gst_gdp_stamp_buffer (this, this->tag_buf);
      GST_DEBUG_OBJECT (this, "1.0, appending current tags buffer %p",
          this->tag_buf);
      tag_buf = this->tag_buf;
      this->tag_buf = NULL;

      g_value_init (&value, GST_TYPE_BUFFER);
      gst_value_set_buffer (&value, tag_buf);
      gst_value_array_append_value (&array, &value);
      g_value_unset (&value);
      gst_buffer_unref (tag_buf);
    }
  }

  /* we also need to add GDP serializations of the streamheaders of the
   * incoming caps */
  structure = gst_caps_get_structure (this->caps, 0);
  if (gst_structure_has_field (structure, "streamheader")) {
    const GValue *sh;

    GArray *buffers;

    GstBuffer *buffer;

    int i;

    sh = gst_structure_get_value (structure, "streamheader");
    buffers = g_value_peek_pointer (sh);
    GST_DEBUG_OBJECT (this,
        "Need to serialize %d incoming streamheader buffers on ours",
        buffers->len);
    for (i = 0; i < buffers->len; ++i) {
      GValue *bufval;

      GstBuffer *outbuffer;

      bufval = &g_array_index (buffers, GValue, i);
      buffer = g_value_peek_pointer (bufval);
      /* this buffer is deserialized by gdpdepay as a regular buffer,
         it needs HEADER, because it's a streamheader - otherwise it
         is mixed with regular data buffers */
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);
      GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;

      outbuffer = gst_gdp_pay_buffer_from_buffer (this, buffer);
      if (!outbuffer) {
        g_value_unset (&array);
        goto no_buffer;
      }

      /* Setting HEADER as other GDP event buffers */
      GST_DEBUG_OBJECT (this,
          "Setting HEADER flag on outgoing buffer %" GST_PTR_FORMAT, outbuffer);
      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
      GST_BUFFER_OFFSET (outbuffer) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END (outbuffer) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_TIMESTAMP (outbuffer) = GST_CLOCK_TIME_NONE;

      g_value_init (&value, GST_TYPE_BUFFER);
      gst_value_set_buffer (&value, outbuffer);
      gst_value_array_append_value (&array, &value);
      g_value_unset (&value);

      gst_buffer_unref (outbuffer);
    }
  } else {
    GST_DEBUG_OBJECT (this, "no streamheader to serialize");
  }

  GST_DEBUG_OBJECT (this, "%d serialized buffers on streamheaders",
      gst_value_array_get_size (&array));
  caps = gst_caps_from_string ("application/x-gdp");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  GST_DEBUG_OBJECT (this, "Setting caps on src pad %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (this->srcpad, caps);

  /* if these are our first ever buffers, send out new_segment first */
  if (!this->sent_streamheader) {
    GstEvent *event;
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    event = gst_event_new_segment (&segment);

    GST_DEBUG_OBJECT (this, "Sending out new_segment event %p", event);
    if (!gst_pad_push_event (this->srcpad, event)) {
      GST_WARNING_OBJECT (this, "pushing new segment failed");
      r = GST_FLOW_ERROR;
      goto done;
    }
  }

  /* push out these streamheader buffers, then flush our internal queue */
  GST_DEBUG_OBJECT (this, "Pushing GDP stream-start-id buffer %p",
      this->streamstartid_buf);
  r = gst_pad_push (this->srcpad, gst_buffer_ref (this->streamstartid_buf));
  if (r != GST_FLOW_OK) {
    GST_WARNING_OBJECT (this, "pushing GDP stream-start-id buffer returned %d",
        r);
    goto done;
  }
  GST_DEBUG_OBJECT (this, "Pushing GDP caps buffer %p", this->caps_buf);
  r = gst_pad_push (this->srcpad, gst_buffer_ref (this->caps_buf));
  if (r != GST_FLOW_OK) {
    GST_WARNING_OBJECT (this, "pushing GDP caps buffer returned %d", r);
    goto done;
  }
  GST_DEBUG_OBJECT (this, "Pushing GDP new_segment buffer %p with offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT, this->new_segment_buf,
      GST_BUFFER_OFFSET (this->new_segment_buf),
      GST_BUFFER_OFFSET_END (this->new_segment_buf));
  /* we stored these bufs with refcount 1, so make sure we keep a ref */
  r = gst_pad_push (this->srcpad, gst_buffer_ref (this->new_segment_buf));
  if (r != GST_FLOW_OK) {
    GST_WARNING_OBJECT (this, "pushing GDP newsegment buffer returned %d", r);
    goto done;
  }
  if (this->tag_buf) {
    GST_DEBUG_OBJECT (this, "Pushing GDP tag buffer %p", this->tag_buf);
    /* we stored these bufs with refcount 1, so make sure we keep a ref */
    r = gst_pad_push (this->srcpad, gst_buffer_ref (this->tag_buf));
    if (r != GST_FLOW_OK) {
      GST_WARNING_OBJECT (this, "pushing GDP tag buffer returned %d", r);
      goto done;
    }
  }
  this->sent_streamheader = TRUE;
  GST_DEBUG_OBJECT (this, "need to push %d queued buffers",
      g_list_length (this->queue));
  while (this->queue) {
    GstBuffer *buffer;

    buffer = GST_BUFFER_CAST (this->queue->data);
    GST_DEBUG_OBJECT (this, "Pushing queued GDP buffer %p", buffer);

    /* delete buffer from queue now */
    this->queue = g_list_delete_link (this->queue, this->queue);

    /* set caps and push */
    r = gst_pad_push (this->srcpad, buffer);
    if (r != GST_FLOW_OK) {
      GST_WARNING_OBJECT (this, "pushing queued GDP buffer returned %d", r);
      goto done;
    }
  }

done:
  gst_caps_unref (caps);
  GST_DEBUG_OBJECT (this, "stop");
  return r;

  /* ERRORS */
no_buffer:
  {
    GST_ELEMENT_ERROR (this, STREAM, FORMAT, (NULL),
        ("failed to create GDP buffer from streamheader"));
    return GST_FLOW_ERROR;
  }
}

/* queue a buffer internally if we haven't sent streamheader buffers yet;
 * otherwise, just push on, this takes ownership of the buffer. */
static GstFlowReturn
gst_gdp_queue_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  if (this->sent_streamheader) {
    GST_LOG_OBJECT (this, "Pushing GDP buffer %p, caps %" GST_PTR_FORMAT,
        buffer, this->caps);
    return gst_pad_push (this->srcpad, buffer);
  }

  /* store it on an internal queue. buffer remains reffed. */
  this->queue = g_list_append (this->queue, buffer);
  GST_DEBUG_OBJECT (this, "streamheader not sent yet, "
      "queued buffer %p, now %d buffers queued",
      buffer, g_list_length (this->queue));

  gst_gdp_pay_reset_streamheader (this);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gdp_pay_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstGDPPay *this;
#if 0
  GstCaps *caps;
#endif
  GstBuffer *outbuffer;
  GstFlowReturn ret;

  this = GST_GDP_PAY (parent);

  /* we should have received a new_segment before, otherwise it's a bug.
   * fake one in that case */
  if (!this->new_segment_buf) {
    GstEvent *event;
    GstSegment segment;

    GST_WARNING_OBJECT (this,
        "did not receive new-segment before first buffer");
    gst_segment_init (&segment, GST_FORMAT_BYTES);
    event = gst_event_new_segment (&segment);
    outbuffer = gst_gdp_buffer_from_event (this, event);
    gst_event_unref (event);

    /* GDP 0.2 doesn't know about new-segment, so this is not fatal */
    if (!outbuffer) {
      GST_ELEMENT_WARNING (this, STREAM, ENCODE, (NULL),
          ("Could not create GDP buffer from new segment event"));
    } else {
      GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (buffer);
      GST_BUFFER_DURATION (outbuffer) = 0;
      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
      GST_DEBUG_OBJECT (this, "Storing buffer %p as new_segment_buf",
          outbuffer);
      this->new_segment_buf = outbuffer;
    }
  }
  /* make sure we've received caps before */
  if (!this->caps)
    goto no_caps;

  /* create a GDP header packet,
   * then create a GST buffer of the header packet and the buffer contents */
  outbuffer = gst_gdp_pay_buffer_from_buffer (this, buffer);
  if (!outbuffer)
    goto no_buffer;

  /* If the incoming buffer is HEADER, that means we have it on the caps
   * as streamheader, and we have serialized a GDP version of it and put it
   * on our caps */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_HEADER)) {
    GST_DEBUG_OBJECT (this, "Setting HEADER flag on outgoing buffer %p",
        outbuffer);
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
  }

  gst_gdp_stamp_buffer (this, outbuffer);
  GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuffer) = GST_BUFFER_DURATION (buffer);

  ret = gst_gdp_queue_buffer (this, outbuffer);

done:
  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
no_caps:
  {
    /* when returning a fatal error as a GstFlowReturn we must post an error
     * message */
    GST_ELEMENT_ERROR (this, STREAM, FORMAT, (NULL),
        ("first received buffer does not have caps set"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
#if 0
no_caps_buffer:
  {
    GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from caps %" GST_PTR_FORMAT, caps));
    gst_caps_unref (caps);
    ret = GST_FLOW_ERROR;
    goto done;
  }
#endif
no_buffer:
  {
    GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from buffer"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
gst_gdp_pay_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBuffer *outbuffer;
  GstGDPPay *this = GST_GDP_PAY (parent);
  GstFlowReturn flowret;
  GstCaps *caps;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (this, "received event %p of type %s (%d)",
      event, gst_event_type_get_name (event->type), event->type);

  /* now turn the event into a buffer */
  outbuffer = gst_gdp_buffer_from_event (this, event);
  if (!outbuffer)
    goto no_outbuffer;

  GST_BUFFER_TIMESTAMP (outbuffer) = GST_EVENT_TIMESTAMP (event);
  GST_BUFFER_DURATION (outbuffer) = 0;

  /* if we got a new segment or tag event, we should put it on our streamheader,
   * and not send it on */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (this, "Storing stream start id in buffer %p",
          outbuffer);

      if (this->streamstartid_buf)
        gst_buffer_unref (this->streamstartid_buf);
      this->streamstartid_buf = outbuffer;

      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
      gst_gdp_pay_reset_streamheader (this);
      break;
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (this, "Storing in caps buffer %p as new_segment_buf",
          outbuffer);

      if (this->new_segment_buf)
        gst_buffer_unref (this->new_segment_buf);
      this->new_segment_buf = outbuffer;

      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
      gst_gdp_pay_reset_streamheader (this);
      break;
    case GST_EVENT_CAPS:{
      gst_event_parse_caps (event, &caps);
      gst_buffer_replace (&outbuffer, NULL);
      if (this->caps == NULL || !gst_caps_is_equal (this->caps, caps)) {
        GST_INFO_OBJECT (pad, "caps changed to %" GST_PTR_FORMAT, caps);
        gst_caps_replace (&this->caps, caps);
        outbuffer = gst_gdp_buffer_from_caps (this, caps);
        if (outbuffer == NULL)
          goto no_buffer_from_caps;

        GST_BUFFER_DURATION (outbuffer) = 0;
        GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
        if (this->caps_buf)
          gst_buffer_unref (this->caps_buf);
        this->caps_buf = outbuffer;
        gst_gdp_pay_reset_streamheader (this);
      }
      break;
    }
    case GST_EVENT_TAG:
      GST_DEBUG_OBJECT (this, "Storing in caps buffer %p as tag_buf",
          outbuffer);

      if (this->tag_buf)
        gst_buffer_unref (this->tag_buf);
      this->tag_buf = outbuffer;

      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_HEADER);
      gst_gdp_pay_reset_streamheader (this);
      break;
    default:
      GST_DEBUG_OBJECT (this, "queuing GDP buffer %p of event %p", outbuffer,
          event);
      flowret = gst_gdp_queue_buffer (this, outbuffer);
      if (flowret != GST_FLOW_OK)
        goto push_error;
      break;
  }

  /* if we have EOS, we should send on EOS ourselves */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS
      || GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    GST_DEBUG_OBJECT (this, "Sending on event %" GST_PTR_FORMAT, event);
    /* ref, we unref later again */
    ret = gst_pad_push_event (this->srcpad, gst_event_ref (event));
  }

done:
  gst_event_unref (event);

  return ret;

  /* ERRORS */
no_outbuffer:
  {
    GST_ELEMENT_WARNING (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from received event (type %s)",
            gst_event_type_get_name (event->type)));
    ret = FALSE;
    goto done;
  }
no_buffer_from_caps:
  {
    GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from caps %" GST_PTR_FORMAT, caps));
    ret = FALSE;
    goto done;
  }
push_error:
  {
    GST_WARNING_OBJECT (this, "queueing GDP event buffer returned %d", flowret);
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_gdp_pay_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstGDPPay *this;
  gboolean res = TRUE;

  this = GST_GDP_PAY (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* we refuse seek for now. */
      gst_event_unref (event);
      res = FALSE;
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    default:
      /* everything else is passed */
      res = gst_pad_push_event (this->sinkpad, event);
      break;
  }

  return res;
}

static void
gst_gdp_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGDPPay *this;

  g_return_if_fail (GST_IS_GDP_PAY (object));
  this = GST_GDP_PAY (object);

  switch (prop_id) {
    case PROP_CRC_HEADER:
      this->crc_header =
          g_value_get_boolean (value) ? GST_DP_HEADER_FLAG_CRC_HEADER : 0;
      this->header_flag = this->crc_header | this->crc_payload;
      break;
    case PROP_CRC_PAYLOAD:
      this->crc_payload =
          g_value_get_boolean (value) ? GST_DP_HEADER_FLAG_CRC_PAYLOAD : 0;
      this->header_flag = this->crc_header | this->crc_payload;
      break;
    case PROP_VERSION:
      this->version = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gdp_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGDPPay *this;

  g_return_if_fail (GST_IS_GDP_PAY (object));
  this = GST_GDP_PAY (object);

  switch (prop_id) {
    case PROP_CRC_HEADER:
      g_value_set_boolean (value, this->crc_header);
      break;
    case PROP_CRC_PAYLOAD:
      g_value_set_boolean (value, this->crc_payload);
      break;
    case PROP_VERSION:
      g_value_set_enum (value, this->version);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gdp_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstGDPPay *this = GST_GDP_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_gdp_pay_reset (this);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_gdp_pay_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gdppay", GST_RANK_NONE, GST_TYPE_GDP_PAY))
    return FALSE;

  return TRUE;
}
