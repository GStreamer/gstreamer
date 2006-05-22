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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-gdppay
 *
 * <refsect2>
 * <para>
 * This element payloads GStreamer buffers and events using the
 * GStreamer Data Protocol.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/dataprotocol/dataprotocol.h>

#include "gstgdppay.h"

/* elementfactory information */
static const GstElementDetails gdp_pay_details =
GST_ELEMENT_DETAILS ("GDP Payloader",
    "GDP/Payloader",
    "Payloads GStreamer Data Protocol buffers",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

enum
{
  PROP_0,
  /* FILL ME */
};

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

GST_DEBUG_CATEGORY (gst_gdp_pay_debug);
#define GST_CAT_DEFAULT gst_gdp_pay_debug

#define _do_init(x) \
    GST_DEBUG_CATEGORY_INIT (gst_gdp_pay_debug, "gdppay", 0, \
    "GDP payloader");

GST_BOILERPLATE_FULL (GstGDPPay, gst_gdp_pay, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static GstFlowReturn gst_gdp_pay_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_gdp_pay_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_gdp_pay_change_state (GstElement *
    element, GstStateChange transition);

static void gst_gdp_pay_dispose (GObject * gobject);

static void
gst_gdp_pay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gdp_pay_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gdp_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gdp_pay_src_template));
}

static void
gst_gdp_pay_class_init (GstGDPPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_gdp_pay_dispose);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_gdp_pay_change_state);
}

static void
gst_gdp_pay_init (GstGDPPay * gdppay, GstGDPPayClass * g_class)
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
  gst_element_add_pad (GST_ELEMENT (gdppay), gdppay->srcpad);

  gdppay->offset = 0;
}

static void
gst_gdp_pay_dispose (GObject * gobject)
{
  GstGDPPay *this = GST_GDP_PAY (gobject);

  if (this->caps_buf) {
    gst_buffer_unref (this->caps_buf);
    this->caps_buf = NULL;
  }
  if (this->new_segment_buf) {
    gst_buffer_unref (this->new_segment_buf);
    this->new_segment_buf = NULL;
  }
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (gobject));
}

/* set OFFSET and OFFSET_END with running count */
static void
gst_gdp_stamp_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  GST_BUFFER_OFFSET (buffer) = this->offset;
  GST_BUFFER_OFFSET_END (buffer) = this->offset + GST_BUFFER_SIZE (buffer);
  this->offset = GST_BUFFER_OFFSET_END (buffer);
}

static GstBuffer *
gst_gdp_buffer_from_caps (GstGDPPay * this, GstCaps * caps)
{
  GstBuffer *headerbuf;
  GstBuffer *payloadbuf;
  guint8 *header, *payload;
  guint len;

  if (!gst_dp_packet_from_caps (caps, 0, &len, &header, &payload)) {
    GST_WARNING_OBJECT (this, "could not create GDP header from caps");
    return NULL;
  }

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from caps");
  headerbuf = gst_buffer_new ();
  gst_buffer_set_data (headerbuf, header, len);
  GST_BUFFER_MALLOCDATA (headerbuf) = header;

  payloadbuf = gst_buffer_new ();
  gst_buffer_set_data (payloadbuf, payload,
      gst_dp_header_payload_length (header));
  GST_BUFFER_MALLOCDATA (payloadbuf) = payload;

  return gst_buffer_join (headerbuf, payloadbuf);
}

static GstBuffer *
gst_gdp_pay_buffer_from_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  GstBuffer *headerbuf;
  guint8 *header;
  guint len;

  if (!gst_dp_header_from_buffer (buffer, 0, &len, &header)) {
    GST_WARNING_OBJECT (this, "could not create GDP header from buffer");
    return NULL;
  }

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from buffer");
  headerbuf = gst_buffer_new ();
  gst_buffer_set_data (headerbuf, header, len);
  GST_BUFFER_MALLOCDATA (headerbuf) = header;

  /* we do not want to lose the ref on the incoming buffer */
  gst_buffer_ref (buffer);
  return gst_buffer_join (headerbuf, buffer);
}

static GstBuffer *
gst_gdp_buffer_from_event (GstGDPPay * this, GstEvent * event)
{
  GstBuffer *headerbuf;
  GstBuffer *payloadbuf;
  guint8 *header, *payload;
  guint len;

  if (!gst_dp_packet_from_event (event, 0, &len, &header, &payload)) {
    GST_WARNING_OBJECT (this, "could not create GDP header from event");
    return NULL;
  }

  GST_LOG_OBJECT (this, "creating GDP header and payload buffer from event");
  headerbuf = gst_buffer_new ();
  gst_buffer_set_data (headerbuf, header, len);
  GST_BUFFER_MALLOCDATA (headerbuf) = header;

  payloadbuf = gst_buffer_new ();
  gst_buffer_set_data (payloadbuf, payload,
      gst_dp_header_payload_length (header));
  GST_BUFFER_MALLOCDATA (payloadbuf) = payload;

  return gst_buffer_join (headerbuf, payloadbuf);
}


/* set our caps with streamheader, based on the latest newsegment and caps,
 * and (possibly) GDP-serialized buffers of the streamheaders on the src pad */
static GstFlowReturn
gst_gdp_pay_reset_streamheader (GstGDPPay * this)
{
  GstCaps *caps;
  GstStructure *structure;
  GstBuffer *new_segment_buf, *caps_buf;
  GstFlowReturn r = GST_FLOW_OK;

  GValue array = { 0 };
  GValue value = { 0 };

  /* we need both new segment and caps before we can set streamheader */
  if (!this->new_segment_buf || !this->caps_buf)
    return GST_FLOW_OK;

  /* we copy to avoid circular refcounts */
  new_segment_buf = gst_buffer_copy (this->new_segment_buf);
  caps_buf = gst_buffer_copy (this->caps_buf);

  /* put copies of the buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, new_segment_buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, caps_buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

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
        "Need to serialize %d incoming streamheader buffers on our streamheader",
        buffers->len);
    for (i = 0; i < buffers->len; ++i) {
      GValue *bufval;
      GstBuffer *outbuffer;

      bufval = &g_array_index (buffers, GValue, i);
      buffer = g_value_peek_pointer (bufval);
      outbuffer = gst_gdp_pay_buffer_from_buffer (this, buffer);
      if (outbuffer) {
        g_value_init (&value, GST_TYPE_BUFFER);
        gst_value_set_buffer (&value, outbuffer);
        gst_value_array_append_value (&array, &value);
        g_value_unset (&value);
      }
      /* FIXME: if one or more in this loop fail to produce and outbuffer,
       * should we error out ? Once ? Every time ? */
    }
  }

  caps = gst_caps_from_string ("application/x-gdp");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  /* Unref our copies */
  gst_buffer_unref (new_segment_buf);
  gst_buffer_unref (caps_buf);

  GST_DEBUG_OBJECT (this, "Setting caps on src pad %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (this->srcpad, caps);
  gst_buffer_set_caps (this->caps_buf, caps);
  gst_buffer_set_caps (this->new_segment_buf, caps);

  /* if these are our first ever buffers, send out new_segment first */
  if (!this->sent_streamheader) {
    GstEvent *event =
        gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES, 0, -1, 0);
    GST_DEBUG_OBJECT (this, "Sending out new_segment event %p", event);
    if (!gst_pad_push_event (this->srcpad, event)) {
      GST_WARNING_OBJECT (this, "pushing new segment failed");
      return GST_FLOW_ERROR;
    }
  }

  /* push out these streamheader buffers, then flush our internal queue */
  GST_DEBUG_OBJECT (this, "Pushing GDP new_segment buffer %p",
      this->new_segment_buf);
  /* we stored these bufs with refcount 1, so make sure we keep a ref */
  r = gst_pad_push (this->srcpad, gst_buffer_ref (this->new_segment_buf));
  if (r != GST_FLOW_OK) {
    GST_WARNING_OBJECT (this, "pushing GDP newsegment buffer returned %d", r);
    return r;
  }
  GST_DEBUG_OBJECT (this, "Pushing GDP caps buffer %p", this->new_segment_buf);
  r = gst_pad_push (this->srcpad, gst_buffer_ref (this->caps_buf));
  if (r != GST_FLOW_OK) {
    GST_WARNING_OBJECT (this, "pushing GDP caps buffer returned %d", r);
    return r;
  }
  this->sent_streamheader = TRUE;
  GST_DEBUG_OBJECT (this, "need to push %d queued buffers",
      g_list_length (this->queue));
  if (this->queue) {
    GList *l;

    for (l = this->queue; l; l = g_list_next (l)) {
      GST_DEBUG_OBJECT (this, "Pushing queued GDP buffer %p", l->data);
      gst_buffer_set_caps (l->data, caps);
      r = gst_pad_push (this->srcpad, l->data);
      if (r != GST_FLOW_OK) {
        GST_WARNING_OBJECT (this, "pushing queued GDP buffer returned %d", r);
        return r;
      }
    }
  }

  return r;
}

/* queue a buffer internally if we haven't sent streamheader buffers yet;
 * otherwise, just push on */
static GstFlowReturn
gst_gdp_queue_buffer (GstGDPPay * this, GstBuffer * buffer)
{
  if (this->sent_streamheader) {
    GST_LOG_OBJECT (this, "Pushing GDP buffer %p", buffer);
    GST_LOG_OBJECT (this, "set caps %" GST_PTR_FORMAT, this->caps);
    return gst_pad_push (this->srcpad, buffer);
  }

  /* store it on an internal queue */
  this->queue = g_list_append (this->queue, buffer);
  GST_DEBUG_OBJECT (this, "queued buffer %p, now %d buffers queued",
      buffer, g_list_length (this->queue));
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gdp_pay_chain (GstPad * pad, GstBuffer * buffer)
{
  GstGDPPay *this;
  GstCaps *caps;
  GstBuffer *outbuffer;
  GstFlowReturn ret;

  this = GST_GDP_PAY (gst_pad_get_parent (pad));

  /* we should have received a new_segment before, otherwise it's a bug.
   * fake one in that case */
  if (!this->new_segment_buf) {
    GstEvent *event;

    GST_WARNING_OBJECT (this,
        "did not receive new-segment before first buffer");
    event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES, 0, -1, 0);
    outbuffer = gst_gdp_buffer_from_event (this, event);
    gst_event_unref (event);

    if (!outbuffer) {
      GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
          ("Could not create GDP buffer from new segment event"));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    gst_gdp_stamp_buffer (this, outbuffer);
    GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (buffer);
    GST_BUFFER_DURATION (outbuffer) = 0;
    this->new_segment_buf = outbuffer;
  }

  /* make sure we've received caps before */
  caps = gst_buffer_get_caps (buffer);
  if (!this->caps && !caps) {
    GST_WARNING_OBJECT (this, "first received buffer does not have caps set");
    if (caps)
      gst_caps_unref (caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

  /* if the caps have changed, process caps first */
  if (caps && !gst_caps_is_equal (this->caps, caps)) {
    GST_LOG_OBJECT (this, "caps changed to %p, %" GST_PTR_FORMAT, caps, caps);
    gst_caps_replace (&(this->caps), caps);
    outbuffer = gst_gdp_buffer_from_caps (this, caps);
    if (!outbuffer) {
      GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
          ("Could not create GDP buffer from caps %" GST_PTR_FORMAT, caps));
      gst_caps_unref (caps);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    gst_gdp_stamp_buffer (this, outbuffer);
    GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (buffer);
    GST_BUFFER_DURATION (outbuffer) = 0;
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_IN_CAPS);
    this->caps_buf = outbuffer;
    gst_gdp_pay_reset_streamheader (this);
  }

  /* create a GDP header packet,
   * then create a GST buffer of the header packet and the buffer contents */
  outbuffer = gst_gdp_pay_buffer_from_buffer (this, buffer);
  if (!outbuffer) {
    GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from buffer"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  gst_gdp_stamp_buffer (this, outbuffer);
  GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuffer) = GST_BUFFER_DURATION (buffer);

  ret = gst_gdp_queue_buffer (this, outbuffer);

done:
  gst_buffer_unref (buffer);
  gst_object_unref (this);
  return ret;
}

static gboolean
gst_gdp_pay_sink_event (GstPad * pad, GstEvent * event)
{
  GstBuffer *outbuffer;
  GstGDPPay *this = GST_GDP_PAY (gst_pad_get_parent (pad));
  GstFlowReturn flowret;
  gboolean ret = TRUE;

  /* now turn the event into a buffer */
  outbuffer = gst_gdp_buffer_from_event (this, event);
  if (!outbuffer) {
    GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
        ("Could not create GDP buffer from event"));
    ret = FALSE;
    goto done;
  }
  gst_gdp_stamp_buffer (this, outbuffer);
  GST_BUFFER_TIMESTAMP (outbuffer) = GST_EVENT_TIMESTAMP (event);
  GST_BUFFER_DURATION (outbuffer) = 0;

  /* if we got a new segment, we should put it on our streamheader,
   * and not send it on */
  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    if (this->new_segment_buf) {
      gst_buffer_unref (this->new_segment_buf);
    }
    this->new_segment_buf = outbuffer;
    gst_gdp_pay_reset_streamheader (this);
  } else {
    flowret = gst_gdp_queue_buffer (this, outbuffer);
    if (flowret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (this, "queueing GDP caps buffer returned %d",
          flowret);
      ret = FALSE;
      goto done;
    }
  }

  /* if we have EOS, we should send on EOS ourselves */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GST_DEBUG_OBJECT (this, "Sending on EOS event %p", event);
    return gst_pad_push_event (this->srcpad, event);
  };

done:
  gst_object_unref (this);
  gst_event_unref (event);
  return ret;
}

static GstStateChangeReturn
gst_gdp_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstGDPPay *this = GST_GDP_PAY (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (this->caps) {
        gst_caps_unref (this->caps);
        this->caps = NULL;
      }
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
