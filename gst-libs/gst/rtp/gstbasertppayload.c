/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstbasertppayload.h"

GST_DEBUG_CATEGORY (basertppayload_debug);
#define GST_CAT_DEFAULT (basertppayload_debug)

/* BaseRTPPayload signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MTU			1024
#define DEFAULT_PT			96
#define DEFAULT_SSRC			0
#define DEFAULT_TIMESTAMP_OFFSET	-1

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC,
  PROP_TIMESTAMP_OFFSET,
  PROP_TIMESTAMP,
  PROP_SEQNUM
};

static void gst_basertppayload_class_init (GstBaseRTPPayloadClass * klass);
static void gst_basertppayload_base_init (GstBaseRTPPayloadClass * klass);
static void gst_basertppayload_init (GstBaseRTPPayload * basertppayload,
    gpointer g_class);
static void gst_basertppayload_finalize (GObject * object);

static gboolean gst_basertppayload_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_basertppayload_chain (GstPad * pad,
    GstBuffer * buffer);

static void gst_basertppayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basertppayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_basertppayload_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_basertppayload_get_type (void)
{
  static GType basertppayload_type = 0;

  if (!basertppayload_type) {
    static const GTypeInfo basertppayload_info = {
      sizeof (GstBaseRTPPayloadClass),
      (GBaseInitFunc) gst_basertppayload_base_init,
      NULL,
      (GClassInitFunc) gst_basertppayload_class_init,
      NULL,
      NULL,
      sizeof (GstBaseRTPPayload),
      0,
      (GInstanceInitFunc) gst_basertppayload_init,
    };

    basertppayload_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBaseRTPPayload",
        &basertppayload_info, G_TYPE_FLAG_ABSTRACT);
  }
  return basertppayload_type;
}

static void
gst_basertppayload_base_init (GstBaseRTPPayloadClass * klass)
{
}

static void
gst_basertppayload_class_init (GstBaseRTPPayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_basertppayload_finalize;

  gobject_class->set_property = gst_basertppayload_set_property;
  gobject_class->get_property = gst_basertppayload_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU",
          "Maximum size of one packet",
          28, G_MAXUINT, DEFAULT_MTU, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets",
          0, 0x80, DEFAULT_PT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (0 == random)",
          0, G_MAXUINT, DEFAULT_SSRC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMESTAMP,
      g_param_spec_uint ("timestamp", "Timestamp",
          "The RTP timestamp of the last processed packet", 0, G_MAXUINT, 0,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet", 0, G_MAXUINT,
          0, G_PARAM_READABLE));

  gstelement_class->change_state = gst_basertppayload_change_state;

  GST_DEBUG_CATEGORY_INIT (basertppayload_debug, "basertppayload", 0,
      "Base class for RTP Payloaders");
}

static void
gst_basertppayload_init (GstBaseRTPPayload * basertppayload, gpointer g_class)
{
  GstPadTemplate *templ;

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (templ != NULL);

  basertppayload->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (basertppayload), basertppayload->srcpad);

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (templ != NULL);

  basertppayload->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_setcaps_function (basertppayload->sinkpad,
      gst_basertppayload_setcaps);
  gst_pad_set_chain_function (basertppayload->sinkpad,
      gst_basertppayload_chain);
  gst_element_add_pad (GST_ELEMENT (basertppayload), basertppayload->sinkpad);

  basertppayload->seq_rand = g_rand_new ();
  basertppayload->ssrc_rand = g_rand_new ();
  basertppayload->ts_rand = g_rand_new ();

  basertppayload->mtu = DEFAULT_MTU;
  basertppayload->pt = DEFAULT_PT;
  basertppayload->seqnum =
      g_rand_int_range (basertppayload->seq_rand, 0, G_MAXUINT16);
  basertppayload->ssrc = DEFAULT_SSRC;
  basertppayload->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  basertppayload->ts_base = g_rand_int (basertppayload->ts_rand);

  basertppayload->clock_rate = 0;
}

static void
gst_basertppayload_finalize (GObject * object)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  g_rand_free (basertppayload->seq_rand);
  basertppayload->seq_rand = NULL;
  g_rand_free (basertppayload->ssrc_rand);
  basertppayload->ssrc_rand = NULL;
  g_rand_free (basertppayload->ts_rand);
  basertppayload->ts_rand = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_basertppayload_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPPayloadClass *basertppayload_class;
  gboolean ret = TRUE;

  basertppayload = GST_BASE_RTP_PAYLOAD (gst_pad_get_parent (pad));
  basertppayload_class = GST_BASE_RTP_PAYLOAD_GET_CLASS (basertppayload);

  if (basertppayload_class->set_caps)
    ret = basertppayload_class->set_caps (basertppayload, caps);

  gst_object_unref (basertppayload);

  return ret;
}

static GstFlowReturn
gst_basertppayload_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPPayloadClass *basertppayload_class;
  GstFlowReturn ret;

  basertppayload = GST_BASE_RTP_PAYLOAD (gst_pad_get_parent (pad));
  basertppayload_class = GST_BASE_RTP_PAYLOAD_GET_CLASS (basertppayload);

  if (!basertppayload_class->handle_buffer)
    goto no_function;

  ret = basertppayload_class->handle_buffer (basertppayload, buffer);

  gst_object_unref (basertppayload);

  return ret;

  /* ERRORS */
no_function:
  {
    GST_ELEMENT_ERROR (basertppayload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not implement handle_buffer function"));
    gst_object_unref (basertppayload);
    return GST_FLOW_ERROR;
  }
}

void
gst_basertppayload_set_options (GstBaseRTPPayload * payload,
    gchar * media, gboolean dynamic, gchar * encoding_name, guint32 clock_rate)
{
  g_return_if_fail (payload != NULL);
  g_return_if_fail (clock_rate != 0);

  g_free (payload->media);
  payload->media = g_strdup (media);
  payload->dynamic = dynamic;
  g_free (payload->encoding_name);
  payload->encoding_name = g_strdup (encoding_name);
  payload->clock_rate = clock_rate;
}

gboolean
gst_basertppayload_set_outcaps (GstBaseRTPPayload * payload, gchar * fieldname,
    ...)
{
  GstCaps *srccaps;
  GstStructure *s;

  srccaps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, payload->media,
      "payload", G_TYPE_INT, GST_BASE_RTP_PAYLOAD_PT (payload),
      "clock-rate", G_TYPE_INT, payload->clock_rate,
      "encoding-name", G_TYPE_STRING, payload->encoding_name,
      "ssrc", G_TYPE_UINT, payload->current_ssrc,
      "clock-base", G_TYPE_UINT, payload->ts_base,
      "seqnum-base", G_TYPE_UINT, payload->seqnum_base, NULL);
  s = gst_caps_get_structure (srccaps, 0);

  if (fieldname) {
    va_list varargs;

    va_start (varargs, fieldname);
    gst_structure_set_valist (s, fieldname, varargs);
    va_end (varargs);
  }

  gst_pad_set_caps (GST_BASE_RTP_PAYLOAD_SRCPAD (payload), srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

GstFlowReturn
gst_basertppayload_push (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstClockTime timestamp;
  guint32 ts;

  if (payload->clock_rate == 0)
    goto no_rate;

  gst_rtpbuffer_set_ssrc (buffer, payload->ssrc);

  gst_rtpbuffer_set_payload_type (buffer, payload->pt);

  /* can warp around, which is perfectly fine */
  gst_rtpbuffer_set_seq (buffer, payload->seqnum++);

  /* add our random offset to the timestamp */
  ts = payload->ts_base;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    ts += timestamp * payload->clock_rate / GST_SECOND;
  gst_rtpbuffer_set_timestamp (buffer, ts);

  payload->timestamp = ts;

  /* set caps */
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (payload->srcpad));

  res = gst_pad_push (payload->srcpad, buffer);

  return res;

  /* ERRORS */
no_rate:
  {
    GST_ELEMENT_ERROR (payload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not specify clock_rate"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static void
gst_basertppayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  switch (prop_id) {
    case PROP_MTU:
      basertppayload->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      basertppayload->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      basertppayload->ssrc = g_value_get_uint (value);
      break;
    case PROP_TIMESTAMP_OFFSET:
      basertppayload->ts_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_basertppayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, basertppayload->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, basertppayload->pt);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, basertppayload->ssrc);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, basertppayload->ts_offset);
      break;
    case PROP_TIMESTAMP:
      g_value_set_uint (value, basertppayload->timestamp);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, basertppayload->seqnum);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_basertppayload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseRTPPayload *basertppayload;
  GstStateChangeReturn ret;

  basertppayload = GST_BASE_RTP_PAYLOAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      basertppayload->seqnum_base =
          g_rand_int_range (basertppayload->seq_rand, 0, G_MAXUINT16);
      basertppayload->seqnum = basertppayload->seqnum_base;
      if (basertppayload->ssrc == 0)
        basertppayload->current_ssrc = g_rand_int (basertppayload->ssrc_rand);
      else
        basertppayload->current_ssrc = basertppayload->ssrc;
      if (basertppayload->ts_offset == -1)
        basertppayload->ts_base = g_rand_int (basertppayload->ts_rand);
      else
        basertppayload->ts_base = basertppayload->ts_offset;
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
