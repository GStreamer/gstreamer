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
 * SECTION:element-gdpdepay
 * @see_also: gdppay
 *
 * <refsect2>
 * <para>
 * This element depayloads GStreamer Data Protocol buffers back to deserialized
 * buffers and events.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=test.gdp ! gdpdepay ! xvimagesink
 * </programlisting>
 * This pipeline plays back a serialized video stream as created in the
 * example for gdppay.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/dataprotocol/dataprotocol.h>

#include "gstgdpdepay.h"

/* elementfactory information */
static const GstElementDetails gdp_depay_details =
GST_ELEMENT_DETAILS ("GDP Depayloader",
    "GDP/Depayloader",
    "Depayloads GStreamer Data Protocol buffers",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

enum
{
  PROP_0,
  /* FILL ME */
};

static GstStaticPadTemplate gdp_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gdp"));

static GstStaticPadTemplate gdp_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_gdp_depay_debug);
#define GST_CAT_DEFAULT gst_gdp_depay_debug

#define _do_init(x) \
    GST_DEBUG_CATEGORY_INIT (gst_gdp_depay_debug, "gdpdepay", 0, \
    "GDP depayloader");

GST_BOILERPLATE_FULL (GstGDPDepay, gst_gdp_depay, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static GstFlowReturn gst_gdp_depay_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_gdp_depay_change_state (GstElement *
    element, GstStateChange transition);

static void gst_gdp_depay_finalize (GObject * object);

static void
gst_gdp_depay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gdp_depay_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gdp_depay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gdp_depay_src_template));
}

static void
gst_gdp_depay_class_init (GstGDPDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_gdp_depay_change_state);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gdp_depay_finalize);
}

static void
gst_gdp_depay_init (GstGDPDepay * gdpdepay, GstGDPDepayClass * g_class)
{
  gdpdepay->sinkpad =
      gst_pad_new_from_static_template (&gdp_depay_sink_template, "sink");
  gst_pad_set_setcaps_function (gdpdepay->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (gdpdepay->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (gdpdepay->sinkpad,
      GST_DEBUG_FUNCPTR (gst_gdp_depay_chain));
  gst_element_add_pad (GST_ELEMENT (gdpdepay), gdpdepay->sinkpad);

  gdpdepay->srcpad =
      gst_pad_new_from_static_template (&gdp_depay_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (gdpdepay), gdpdepay->srcpad);

  /* our caps will always be decided by the incoming GDP caps buffers */
  gst_pad_use_fixed_caps (gdpdepay->srcpad);

  gdpdepay->adapter = gst_adapter_new ();
}

static void
gst_gdp_depay_finalize (GObject * gobject)
{
  GstGDPDepay *this;

  this = GST_GDP_DEPAY (gobject);
  if (this->caps)
    gst_caps_unref (this->caps);
  if (this->header)
    g_free (this->header);
  gst_adapter_clear (this->adapter);
  g_object_unref (this->adapter);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (gobject));
}

static GstFlowReturn
gst_gdp_depay_chain (GstPad * pad, GstBuffer * buffer)
{
  GstGDPDepay *this;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps;
  GstBuffer *buf;
  GstEvent *event;
  guint8 *header = NULL;
  guint8 *payload = NULL;
  guint available;
  gboolean running = TRUE;

  this = GST_GDP_DEPAY (gst_pad_get_parent (pad));

  gst_adapter_push (this->adapter, buffer);

  while (running) {
    switch (this->state) {
      case GST_GDP_DEPAY_STATE_HEADER:
        available = gst_adapter_available (this->adapter);
        if (available < GST_DP_HEADER_LENGTH) {
          running = FALSE;
          break;
        }

        if (this->header)
          g_free (this->header);
        GST_LOG_OBJECT (this, "reading GDP header from adapter");
        header = gst_adapter_take (this->adapter, GST_DP_HEADER_LENGTH);
        if (!gst_dp_validate_header (GST_DP_HEADER_LENGTH, header))
          goto header_validate_error;

        this->payload_length = gst_dp_header_payload_length (header);
        this->payload_type = gst_dp_header_payload_type (header);
        this->header = header;
        GST_LOG_OBJECT (this,
            "read GDP header, payload size %d, switching to state PAYLOAD",
            this->payload_length);
        this->state = GST_GDP_DEPAY_STATE_PAYLOAD;
        break;

      case GST_GDP_DEPAY_STATE_PAYLOAD:
        available = gst_adapter_available (this->adapter);
        if (available < this->payload_length) {
          running = FALSE;
          break;
        }

        /* change state based on type */
        if (this->payload_type == GST_DP_PAYLOAD_BUFFER) {
          GST_LOG_OBJECT (this, "switching to state BUFFER");
          this->state = GST_GDP_DEPAY_STATE_BUFFER;
        } else if (this->payload_type == GST_DP_PAYLOAD_CAPS) {
          GST_LOG_OBJECT (this, "switching to state CAPS");
          this->state = GST_GDP_DEPAY_STATE_CAPS;
        } else if (this->payload_type >= GST_DP_PAYLOAD_EVENT_NONE) {
          GST_LOG_OBJECT (this, "switching to state EVENT");
          this->state = GST_GDP_DEPAY_STATE_EVENT;
        } else
          goto wrong_type;
        break;

      case GST_GDP_DEPAY_STATE_BUFFER:
        if (!this->caps) {
          GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
              ("Received a buffer without first receiving caps"));
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto done;
        }

        GST_LOG_OBJECT (this, "reading GDP buffer from adapter");
        buf = gst_dp_buffer_from_header (GST_DP_HEADER_LENGTH, this->header);
        if (!buf) {
          GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
              ("could not create buffer from GDP packet"));
          ret = GST_FLOW_ERROR;
          goto done;
        }

        payload = gst_adapter_take (this->adapter, this->payload_length);
        memcpy (GST_BUFFER_DATA (buf), payload, this->payload_length);
        g_free (payload);
        payload = NULL;

        gst_buffer_set_caps (buf, this->caps);
        ret = gst_pad_push (this->srcpad, buf);
        if (ret != GST_FLOW_OK) {
          GST_WARNING_OBJECT (this, "pushing depayloaded buffer returned %d",
              ret);
          goto done;
        }

        GST_LOG_OBJECT (this, "switching to state HEADER");
        this->state = GST_GDP_DEPAY_STATE_HEADER;
        break;

      case GST_GDP_DEPAY_STATE_CAPS:
        GST_LOG_OBJECT (this, "reading GDP caps from adapter");
        payload = gst_adapter_take (this->adapter, this->payload_length);
        caps = gst_dp_caps_from_packet (GST_DP_HEADER_LENGTH, this->header,
            payload);
        g_free (payload);
        payload = NULL;
        if (!caps) {
          GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
              ("could not create caps from GDP packet"));
          ret = GST_FLOW_ERROR;
          goto done;
        }
        GST_DEBUG_OBJECT (this, "read caps %" GST_PTR_FORMAT, caps);
        gst_caps_replace (&(this->caps), caps);
        gst_pad_set_caps (this->srcpad, caps);
        /* drop the creation ref we still have */
        gst_caps_unref (caps);

        GST_LOG_OBJECT (this, "switching to state HEADER");
        this->state = GST_GDP_DEPAY_STATE_HEADER;
        break;

      case GST_GDP_DEPAY_STATE_EVENT:
        GST_LOG_OBJECT (this, "reading GDP event from adapter");
        /* adapter doesn't like 0 length payload */
        if (this->payload_length > 0)
          payload = gst_adapter_take (this->adapter, this->payload_length);
        event = gst_dp_event_from_packet (GST_DP_HEADER_LENGTH, this->header,
            payload);
        if (payload) {
          g_free (payload);
          payload = NULL;
        }
        if (!event) {
          GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
              ("could not create event from GDP packet"));
          ret = GST_FLOW_ERROR;
          goto done;
        }
        /* FIXME: set me as source ? */
        GST_DEBUG_OBJECT (this, "sending deserialized event %p of type %s",
            event, gst_event_type_get_name (event->type));
        gst_pad_push_event (this->srcpad, event);

        GST_LOG_OBJECT (this, "switching to state HEADER");
        this->state = GST_GDP_DEPAY_STATE_HEADER;
        break;
    }
  }
  goto done;

header_validate_error:
  GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
      ("GDP packet header does not validate"));
  g_free (header);
  ret = GST_FLOW_ERROR;
  goto done;

wrong_type:
  GST_ELEMENT_ERROR (this, STREAM, DECODE, (NULL),
      ("GDP packet header is of wrong type"));
  g_free (header);
  ret = GST_FLOW_ERROR;
  goto done;

done:
  gst_object_unref (this);
  return ret;
}

static GstStateChangeReturn
gst_gdp_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstGDPDepay *this = GST_GDP_DEPAY (element);

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
gst_gdp_depay_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gdpdepay", GST_RANK_NONE,
          GST_TYPE_GDP_DEPAY))
    return FALSE;

  return TRUE;
}
