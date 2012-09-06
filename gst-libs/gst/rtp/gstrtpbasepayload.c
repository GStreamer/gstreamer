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

/**
 * SECTION:gstrtpbasepayload
 * @short_description: Base class for RTP payloader
 *
 * Provides a base class for RTP payloaders
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbasepayload.h"

GST_DEBUG_CATEGORY_STATIC (rtpbasepayload_debug);
#define GST_CAT_DEFAULT (rtpbasepayload_debug)

#define GST_RTP_BASE_PAYLOAD_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_BASE_PAYLOAD, GstRTPBasePayloadPrivate))

struct _GstRTPBasePayloadPrivate
{
  gboolean ts_offset_random;
  gboolean seqnum_offset_random;
  gboolean ssrc_random;
  guint16 next_seqnum;
  gboolean perfect_rtptime;
  gint notified_first_timestamp;

  guint64 base_offset;
  gint64 base_rtime;

  gint64 prop_max_ptime;
  gint64 caps_max_ptime;

  gboolean negotiated;
};

/* RTPBasePayload signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* FIXME 0.11, a better default is the Ethernet MTU of
 * 1500 - sizeof(headers) as pointed out by marcelm in IRC:
 * So an Ethernet MTU of 1500, minus 60 for the max IP, minus 8 for UDP, gives
 * 1432 bytes or so.  And that should be adjusted downward further for other
 * encapsulations like PPPoE, so 1400 at most.
 */
#define DEFAULT_MTU                     1400
#define DEFAULT_PT                      96
#define DEFAULT_SSRC                    -1
#define DEFAULT_TIMESTAMP_OFFSET        -1
#define DEFAULT_SEQNUM_OFFSET           -1
#define DEFAULT_MAX_PTIME               -1
#define DEFAULT_MIN_PTIME               0
#define DEFAULT_PERFECT_RTPTIME         TRUE
#define DEFAULT_PTIME_MULTIPLE          0

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_MAX_PTIME,
  PROP_MIN_PTIME,
  PROP_TIMESTAMP,
  PROP_SEQNUM,
  PROP_PERFECT_RTPTIME,
  PROP_PTIME_MULTIPLE,
  PROP_LAST
};

static void gst_rtp_base_payload_class_init (GstRTPBasePayloadClass * klass);
static void gst_rtp_base_payload_init (GstRTPBasePayload * rtpbasepayload,
    gpointer g_class);
static void gst_rtp_base_payload_finalize (GObject * object);

static GstCaps *gst_rtp_base_payload_getcaps_default (GstRTPBasePayload *
    rtpbasepayload, GstPad * pad, GstCaps * filter);

static gboolean gst_rtp_base_payload_sink_event_default (GstRTPBasePayload *
    rtpbasepayload, GstEvent * event);
static gboolean gst_rtp_base_payload_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_rtp_base_payload_query_default (GstRTPBasePayload *
    rtpbasepayload, GstPad * pad, GstQuery * query);
static gboolean gst_rtp_base_payload_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_rtp_base_payload_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static void gst_rtp_base_payload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_base_payload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_base_payload_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_rtp_base_payload_get_type (void)
{
  static GType rtpbasepayload_type = 0;

  if (g_once_init_enter ((gsize *) & rtpbasepayload_type)) {
    static const GTypeInfo rtpbasepayload_info = {
      sizeof (GstRTPBasePayloadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_rtp_base_payload_class_init,
      NULL,
      NULL,
      sizeof (GstRTPBasePayload),
      0,
      (GInstanceInitFunc) gst_rtp_base_payload_init,
    };

    g_once_init_leave ((gsize *) & rtpbasepayload_type,
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPBasePayload",
            &rtpbasepayload_info, G_TYPE_FLAG_ABSTRACT));
  }
  return rtpbasepayload_type;
}

static void
gst_rtp_base_payload_class_init (GstRTPBasePayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRTPBasePayloadPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtp_base_payload_finalize;

  gobject_class->set_property = gst_rtp_base_payload_set_property;
  gobject_class->get_property = gst_rtp_base_payload_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU",
          "Maximum size of one packet",
          28, G_MAXUINT, DEFAULT_MTU,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets", 0, 0x80, DEFAULT_PT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (default == random)", 0, G_MAXUINT32,
          DEFAULT_SSRC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_uint ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (default = random)", 0,
          G_MAXUINT32, DEFAULT_TIMESTAMP_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXUINT16,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_PTIME,
      g_param_spec_int64 ("max-ptime", "Max packet time",
          "Maximum duration of the packet data in ns (-1 = unlimited up to MTU)",
          -1, G_MAXINT64, DEFAULT_MAX_PTIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTPBaseAudioPayload:min-ptime:
   *
   * Minimum duration of the packet data in ns (can't go above MTU)
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MIN_PTIME,
      g_param_spec_int64 ("min-ptime", "Min packet time",
          "Minimum duration of the packet data in ns (can't go above MTU)",
          0, G_MAXINT64, DEFAULT_MIN_PTIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMESTAMP,
      g_param_spec_uint ("timestamp", "Timestamp",
          "The RTP timestamp of the last processed packet",
          0, G_MAXUINT32, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT16, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTPBaseAudioPayload:perfect-rtptime:
   *
   * Try to use the offset fields to generate perfect RTP timestamps. when this
   * option is disabled, RTP timestamps are generated from the GStreamer
   * timestamps, which could result in RTP timestamps that don't increment with
   * the amount of data in the packet.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PERFECT_RTPTIME,
      g_param_spec_boolean ("perfect-rtptime", "Perfect RTP Time",
          "Generate perfect RTP timestamps when possible",
          DEFAULT_PERFECT_RTPTIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTPBaseAudioPayload:ptime-multiple:
   *
   * Force buffers to be multiples of this duration in ns (0 disables)
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PTIME_MULTIPLE,
      g_param_spec_int64 ("ptime-multiple", "Packet time multiple",
          "Force buffers to be multiples of this duration in ns (0 disables)",
          0, G_MAXINT64, DEFAULT_PTIME_MULTIPLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_rtp_base_payload_change_state;

  klass->get_caps = gst_rtp_base_payload_getcaps_default;
  klass->sink_event = gst_rtp_base_payload_sink_event_default;
  klass->query = gst_rtp_base_payload_query_default;

  GST_DEBUG_CATEGORY_INIT (rtpbasepayload_debug, "rtpbasepayload", 0,
      "Base class for RTP Payloaders");
}

static void
gst_rtp_base_payload_init (GstRTPBasePayload * rtpbasepayload, gpointer g_class)
{
  GstPadTemplate *templ;
  GstRTPBasePayloadPrivate *priv;

  rtpbasepayload->priv = priv =
      GST_RTP_BASE_PAYLOAD_GET_PRIVATE (rtpbasepayload);

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (templ != NULL);

  rtpbasepayload->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (rtpbasepayload), rtpbasepayload->srcpad);

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (templ != NULL);

  rtpbasepayload->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_chain_function (rtpbasepayload->sinkpad,
      gst_rtp_base_payload_chain);
  gst_pad_set_event_function (rtpbasepayload->sinkpad,
      gst_rtp_base_payload_sink_event);
  gst_pad_set_query_function (rtpbasepayload->sinkpad,
      gst_rtp_base_payload_query);
  gst_element_add_pad (GST_ELEMENT (rtpbasepayload), rtpbasepayload->sinkpad);

  rtpbasepayload->mtu = DEFAULT_MTU;
  rtpbasepayload->pt = DEFAULT_PT;
  rtpbasepayload->seqnum_offset = DEFAULT_SEQNUM_OFFSET;
  rtpbasepayload->ssrc = DEFAULT_SSRC;
  rtpbasepayload->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  priv->seqnum_offset_random = (rtpbasepayload->seqnum_offset == -1);
  priv->ts_offset_random = (rtpbasepayload->ts_offset == -1);
  priv->ssrc_random = (rtpbasepayload->ssrc == -1);

  rtpbasepayload->max_ptime = DEFAULT_MAX_PTIME;
  rtpbasepayload->min_ptime = DEFAULT_MIN_PTIME;
  rtpbasepayload->priv->perfect_rtptime = DEFAULT_PERFECT_RTPTIME;
  rtpbasepayload->ptime_multiple = DEFAULT_PTIME_MULTIPLE;
  rtpbasepayload->priv->base_offset = GST_BUFFER_OFFSET_NONE;
  rtpbasepayload->priv->base_rtime = GST_BUFFER_OFFSET_NONE;

  rtpbasepayload->media = NULL;
  rtpbasepayload->encoding_name = NULL;

  rtpbasepayload->clock_rate = 0;

  rtpbasepayload->priv->caps_max_ptime = DEFAULT_MAX_PTIME;
  rtpbasepayload->priv->prop_max_ptime = DEFAULT_MAX_PTIME;
}

static void
gst_rtp_base_payload_finalize (GObject * object)
{
  GstRTPBasePayload *rtpbasepayload;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (object);

  g_free (rtpbasepayload->media);
  rtpbasepayload->media = NULL;
  g_free (rtpbasepayload->encoding_name);
  rtpbasepayload->encoding_name = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_rtp_base_payload_getcaps_default (GstRTPBasePayload * rtpbasepayload,
    GstPad * pad, GstCaps * filter)
{
  GstCaps *caps;

  caps = GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));
  GST_DEBUG_OBJECT (pad,
      "using pad template %p with caps %p %" GST_PTR_FORMAT,
      GST_PAD_PAD_TEMPLATE (pad), caps, caps);

  if (filter)
    caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else
    caps = gst_caps_ref (caps);

  return caps;
}

static gboolean
gst_rtp_base_payload_sink_event_default (GstRTPBasePayload * rtpbasepayload,
    GstEvent * event)
{
  GstObject *parent = GST_OBJECT_CAST (rtpbasepayload);
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_event_default (rtpbasepayload->sinkpad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_event_default (rtpbasepayload->sinkpad, parent, event);
      gst_segment_init (&rtpbasepayload->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_CAPS:
    {
      GstRTPBasePayloadClass *rtpbasepayload_class;
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      GST_DEBUG_OBJECT (rtpbasepayload, "setting caps %" GST_PTR_FORMAT, caps);

      rtpbasepayload_class = GST_RTP_BASE_PAYLOAD_GET_CLASS (rtpbasepayload);
      if (rtpbasepayload_class->set_caps)
        res = rtpbasepayload_class->set_caps (rtpbasepayload, caps);
      else
        res = TRUE;

      rtpbasepayload->priv->negotiated = res;

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment *segment;

      segment = &rtpbasepayload->segment;
      gst_event_copy_segment (event, segment);

      rtpbasepayload->priv->base_offset = GST_BUFFER_OFFSET_NONE;

      GST_DEBUG_OBJECT (rtpbasepayload,
          "configured SEGMENT %" GST_SEGMENT_FORMAT, segment);
      res = gst_pad_event_default (rtpbasepayload->sinkpad, parent, event);
      break;
    }
    default:
      res = gst_pad_event_default (rtpbasepayload->sinkpad, parent, event);
      break;
  }
  return res;
}

static gboolean
gst_rtp_base_payload_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadClass *rtpbasepayload_class;
  gboolean res = FALSE;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (parent);
  rtpbasepayload_class = GST_RTP_BASE_PAYLOAD_GET_CLASS (rtpbasepayload);

  if (rtpbasepayload_class->sink_event)
    res = rtpbasepayload_class->sink_event (rtpbasepayload, event);
  else
    gst_event_unref (event);

  return res;
}

static gboolean
gst_rtp_base_payload_query_default (GstRTPBasePayload * rtpbasepayload,
    GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstRTPBasePayloadClass *rtpbasepayload_class;
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (rtpbasepayload, "getting caps with filter %"
          GST_PTR_FORMAT, filter);

      rtpbasepayload_class = GST_RTP_BASE_PAYLOAD_GET_CLASS (rtpbasepayload);
      if (rtpbasepayload_class->get_caps) {
        caps = rtpbasepayload_class->get_caps (rtpbasepayload, pad, filter);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        res = TRUE;
      }
      break;
    }
    default:
      res =
          gst_pad_query_default (pad, GST_OBJECT_CAST (rtpbasepayload), query);
      break;
  }
  return res;
}

static gboolean
gst_rtp_base_payload_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadClass *rtpbasepayload_class;
  gboolean res = FALSE;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (parent);
  rtpbasepayload_class = GST_RTP_BASE_PAYLOAD_GET_CLASS (rtpbasepayload);

  if (rtpbasepayload_class->query)
    res = rtpbasepayload_class->query (rtpbasepayload, pad, query);

  return res;
}

static GstFlowReturn
gst_rtp_base_payload_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadClass *rtpbasepayload_class;
  GstFlowReturn ret;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (parent);
  rtpbasepayload_class = GST_RTP_BASE_PAYLOAD_GET_CLASS (rtpbasepayload);

  if (!rtpbasepayload_class->handle_buffer)
    goto no_function;

  if (!rtpbasepayload->priv->negotiated)
    goto not_negotiated;

  ret = rtpbasepayload_class->handle_buffer (rtpbasepayload, buffer);

  return ret;

  /* ERRORS */
no_function:
  {
    GST_ELEMENT_ERROR (rtpbasepayload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not implement handle_buffer function"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (rtpbasepayload, CORE, NEGOTIATION, (NULL),
        ("No input format was negotiated, i.e. no caps event was received. "
            "Perhaps you need a parser or typefind element before the payloader"));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

/**
 * gst_rtp_base_payload_set_options:
 * @payload: a #GstRTPBasePayload
 * @media: the media type (typically "audio" or "video")
 * @dynamic: if the payload type is dynamic
 * @encoding_name: the encoding name
 * @clock_rate: the clock rate of the media
 *
 * Set the rtp options of the payloader. These options will be set in the caps
 * of the payloader. Subclasses must call this method before calling
 * gst_rtp_base_payload_push() or gst_rtp_base_payload_set_outcaps().
 */
void
gst_rtp_base_payload_set_options (GstRTPBasePayload * payload,
    const gchar * media, gboolean dynamic, const gchar * encoding_name,
    guint32 clock_rate)
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

static gboolean
copy_fixed (GQuark field_id, const GValue * value, GstStructure * dest)
{
  if (gst_value_is_fixed (value)) {
    gst_structure_id_set_value (dest, field_id, value);
  }
  return TRUE;
}

static void
update_max_ptime (GstRTPBasePayload * rtpbasepayload)
{
  if (rtpbasepayload->priv->caps_max_ptime != -1 &&
      rtpbasepayload->priv->prop_max_ptime != -1)
    rtpbasepayload->max_ptime = MIN (rtpbasepayload->priv->caps_max_ptime,
        rtpbasepayload->priv->prop_max_ptime);
  else if (rtpbasepayload->priv->caps_max_ptime != -1)
    rtpbasepayload->max_ptime = rtpbasepayload->priv->caps_max_ptime;
  else if (rtpbasepayload->priv->prop_max_ptime != -1)
    rtpbasepayload->max_ptime = rtpbasepayload->priv->prop_max_ptime;
  else
    rtpbasepayload->max_ptime = DEFAULT_MAX_PTIME;
}

/**
 * gst_rtp_base_payload_set_outcaps:
 * @payload: a #GstRTPBasePayload
 * @fieldname: the first field name or %NULL
 * @...: field values
 *
 * Configure the output caps with the optional parameters.
 *
 * Variable arguments should be in the form field name, field type
 * (as a GType), value(s).  The last variable argument should be NULL.
 *
 * Returns: %TRUE if the caps could be set.
 */
gboolean
gst_rtp_base_payload_set_outcaps (GstRTPBasePayload * payload,
    const gchar * fieldname, ...)
{
  GstCaps *srccaps, *peercaps;
  gboolean res;

  /* fill in the defaults, their properties cannot be negotiated. */
  srccaps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, payload->media,
      "clock-rate", G_TYPE_INT, payload->clock_rate,
      "encoding-name", G_TYPE_STRING, payload->encoding_name, NULL);

  GST_DEBUG_OBJECT (payload, "defaults: %" GST_PTR_FORMAT, srccaps);

  if (fieldname) {
    va_list varargs;

    /* override with custom properties */
    va_start (varargs, fieldname);
    gst_caps_set_simple_valist (srccaps, fieldname, varargs);
    va_end (varargs);

    GST_DEBUG_OBJECT (payload, "custom added: %" GST_PTR_FORMAT, srccaps);
  }

  payload->priv->caps_max_ptime = DEFAULT_MAX_PTIME;
  payload->ptime = 0;

  /* the peer caps can override some of the defaults */
  peercaps = gst_pad_peer_query_caps (payload->srcpad, srccaps);
  if (peercaps == NULL) {
    /* no peer caps, just add the other properties */
    gst_caps_set_simple (srccaps,
        "payload", G_TYPE_INT, GST_RTP_BASE_PAYLOAD_PT (payload),
        "ssrc", G_TYPE_UINT, payload->current_ssrc,
        "timestamp-offset", G_TYPE_UINT, payload->ts_base,
        "seqnum-offset", G_TYPE_UINT, payload->seqnum_base, NULL);

    GST_DEBUG_OBJECT (payload, "no peer caps: %" GST_PTR_FORMAT, srccaps);
  } else {
    GstCaps *temp;
    GstStructure *s, *d;
    const GValue *value;
    gint pt;
    guint max_ptime, ptime;

    /* peer provides caps we can use to fixate. They are already intersected
     * with our srccaps, just make them writable */
    temp = gst_caps_make_writable (peercaps);
    gst_caps_unref (srccaps);

    if (gst_caps_is_empty (temp)) {
      gst_caps_unref (temp);
      return FALSE;
    }

    /* now fixate, start by taking the first caps */
    temp = gst_caps_truncate (temp);

    /* get first structure */
    s = gst_caps_get_structure (temp, 0);

    if (gst_structure_get_uint (s, "maxptime", &max_ptime))
      payload->priv->caps_max_ptime = max_ptime * GST_MSECOND;

    if (gst_structure_get_uint (s, "ptime", &ptime))
      payload->ptime = ptime * GST_MSECOND;

    if (gst_structure_get_int (s, "payload", &pt)) {
      /* use peer pt */
      GST_RTP_BASE_PAYLOAD_PT (payload) = pt;
      GST_LOG_OBJECT (payload, "using peer pt %d", pt);
    } else {
      if (gst_structure_has_field (s, "payload")) {
        /* can only fixate if there is a field */
        gst_structure_fixate_field_nearest_int (s, "payload",
            GST_RTP_BASE_PAYLOAD_PT (payload));
        gst_structure_get_int (s, "payload", &pt);
        GST_LOG_OBJECT (payload, "using peer pt %d", pt);
      } else {
        /* no pt field, use the internal pt */
        pt = GST_RTP_BASE_PAYLOAD_PT (payload);
        gst_structure_set (s, "payload", G_TYPE_INT, pt, NULL);
        GST_LOG_OBJECT (payload, "using internal pt %d", pt);
      }
    }

    if (gst_structure_has_field_typed (s, "ssrc", G_TYPE_UINT)) {
      value = gst_structure_get_value (s, "ssrc");
      payload->current_ssrc = g_value_get_uint (value);
      GST_LOG_OBJECT (payload, "using peer ssrc %08x", payload->current_ssrc);
    } else {
      /* FIXME, fixate_nearest_uint would be even better */
      gst_structure_set (s, "ssrc", G_TYPE_UINT, payload->current_ssrc, NULL);
      GST_LOG_OBJECT (payload, "using internal ssrc %08x",
          payload->current_ssrc);
    }

    if (gst_structure_has_field_typed (s, "timestamp-offset", G_TYPE_UINT)) {
      value = gst_structure_get_value (s, "timestamp-offset");
      payload->ts_base = g_value_get_uint (value);
      GST_LOG_OBJECT (payload, "using peer timestamp-offset %u",
          payload->ts_base);
    } else {
      /* FIXME, fixate_nearest_uint would be even better */
      gst_structure_set (s, "timestamp-offset", G_TYPE_UINT, payload->ts_base,
          NULL);
      GST_LOG_OBJECT (payload, "using internal timestamp-offset %u",
          payload->ts_base);
    }
    if (gst_structure_has_field_typed (s, "seqnum-offset", G_TYPE_UINT)) {
      value = gst_structure_get_value (s, "seqnum-offset");
      payload->seqnum_base = g_value_get_uint (value);
      GST_LOG_OBJECT (payload, "using peer seqnum-offset %u",
          payload->seqnum_base);
    } else {
      /* FIXME, fixate_nearest_uint would be even better */
      gst_structure_set (s, "seqnum-offset", G_TYPE_UINT, payload->seqnum_base,
          NULL);
      GST_LOG_OBJECT (payload, "using internal seqnum-offset %u",
          payload->seqnum_base);
    }

    /* make the target caps by copying over all the fixed caps, removing the
     * unfixed caps. */
    srccaps = gst_caps_new_empty_simple (gst_structure_get_name (s));
    d = gst_caps_get_structure (srccaps, 0);

    gst_structure_foreach (s, (GstStructureForeachFunc) copy_fixed, d);

    gst_caps_unref (temp);

    GST_DEBUG_OBJECT (payload, "with peer caps: %" GST_PTR_FORMAT, srccaps);
  }

  update_max_ptime (payload);

  res = gst_pad_set_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload), srccaps);
  gst_caps_unref (srccaps);

  return res;
}

/**
 * gst_rtp_base_payload_is_filled:
 * @payload: a #GstRTPBasePayload
 * @size: the size of the packet
 * @duration: the duration of the packet
 *
 * Check if the packet with @size and @duration would exceed the configured
 * maximum size.
 *
 * Returns: %TRUE if the packet of @size and @duration would exceed the
 * configured MTU or max_ptime.
 */
gboolean
gst_rtp_base_payload_is_filled (GstRTPBasePayload * payload,
    guint size, GstClockTime duration)
{
  if (size > payload->mtu)
    return TRUE;

  if (payload->max_ptime != -1 && duration >= payload->max_ptime)
    return TRUE;

  return FALSE;
}

typedef struct
{
  GstRTPBasePayload *payload;
  guint32 ssrc;
  guint16 seqnum;
  guint8 pt;
  GstClockTime dts;
  GstClockTime pts;
  guint64 offset;
  guint32 rtptime;
} HeaderData;

static gboolean
find_timestamp (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  HeaderData *data = user_data;
  data->dts = GST_BUFFER_DTS (*buffer);
  data->pts = GST_BUFFER_PTS (*buffer);
  data->offset = GST_BUFFER_OFFSET (*buffer);

  /* stop when we find a timestamp. We take whatever offset is associated with
   * the timestamp (if any) to do perfect timestamps when we need to. */
  if (data->pts != -1)
    return FALSE;
  else
    return TRUE;
}

static gboolean
set_headers (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  HeaderData *data = user_data;
  GstRTPBuffer rtp = { NULL, };

  if (!gst_rtp_buffer_map (*buffer, GST_MAP_WRITE, &rtp))
    goto map_failed;

  gst_rtp_buffer_set_ssrc (&rtp, data->ssrc);
  gst_rtp_buffer_set_payload_type (&rtp, data->pt);
  gst_rtp_buffer_set_seq (&rtp, data->seqnum);
  gst_rtp_buffer_set_timestamp (&rtp, data->rtptime);
  gst_rtp_buffer_unmap (&rtp);

  /* increment the seqnum for each buffer */
  data->seqnum++;

  return TRUE;
  /* ERRORS */
map_failed:
  {
    GST_ERROR ("failed to map buffer %p", *buffer);
    return FALSE;
  }
}

/* Updates the SSRC, payload type, seqnum and timestamp of the RTP buffer
 * before the buffer is pushed. */
static GstFlowReturn
gst_rtp_base_payload_prepare_push (GstRTPBasePayload * payload,
    gpointer obj, gboolean is_list)
{
  GstRTPBasePayloadPrivate *priv;
  HeaderData data;

  if (payload->clock_rate == 0)
    goto no_rate;

  priv = payload->priv;

  /* update first, so that the property is set to the last
   * seqnum pushed */
  payload->seqnum = priv->next_seqnum;

  /* fill in the fields we want to set on all headers */
  data.payload = payload;
  data.seqnum = payload->seqnum;
  data.ssrc = payload->current_ssrc;
  data.pt = payload->pt;

  /* find the first buffer with a timestamp */
  if (is_list) {
    data.dts = -1;
    data.pts = -1;
    data.offset = GST_BUFFER_OFFSET_NONE;
    gst_buffer_list_foreach (GST_BUFFER_LIST_CAST (obj), find_timestamp, &data);
  } else {
    data.dts = GST_BUFFER_DTS (GST_BUFFER_CAST (obj));
    data.pts = GST_BUFFER_PTS (GST_BUFFER_CAST (obj));
    data.offset = GST_BUFFER_OFFSET (GST_BUFFER_CAST (obj));
  }

  /* convert to RTP time */
  if (priv->perfect_rtptime && data.offset != GST_BUFFER_OFFSET_NONE &&
      priv->base_offset != GST_BUFFER_OFFSET_NONE) {
    /* if we have an offset, use that for making an RTP timestamp */
    data.rtptime = payload->ts_base + priv->base_rtime +
        data.offset - priv->base_offset;
    GST_LOG_OBJECT (payload,
        "Using offset %" G_GUINT64_FORMAT " for RTP timestamp", data.offset);
  } else if (GST_CLOCK_TIME_IS_VALID (data.pts)) {
    gint64 rtime;

    /* no offset, use the gstreamer pts */
    rtime = gst_segment_to_running_time (&payload->segment, GST_FORMAT_TIME,
        data.pts);

    if (rtime == -1) {
      GST_LOG_OBJECT (payload, "Clipped pts, using base RTP timestamp");
      rtime = 0;
    } else {
      GST_LOG_OBJECT (payload,
          "Using running_time %" GST_TIME_FORMAT " for RTP timestamp",
          GST_TIME_ARGS (rtime));
      rtime =
          gst_util_uint64_scale_int (rtime, payload->clock_rate, GST_SECOND);
      priv->base_offset = data.offset;
      priv->base_rtime = rtime;
    }
    /* add running_time in clock-rate units to the base timestamp */
    data.rtptime = payload->ts_base + rtime;
  } else {
    GST_LOG_OBJECT (payload,
        "Using previous RTP timestamp %" G_GUINT32_FORMAT, payload->timestamp);
    /* no timestamp to convert, take previous timestamp */
    data.rtptime = payload->timestamp;
  }

  /* set ssrc, payload type, seq number, caps and rtptime */
  if (is_list) {
    gst_buffer_list_foreach (GST_BUFFER_LIST_CAST (obj), set_headers, &data);
  } else {
    GstBuffer *buf = GST_BUFFER_CAST (obj);
    set_headers (&buf, 0, &data);
  }

  priv->next_seqnum = data.seqnum;
  payload->timestamp = data.rtptime;

  GST_LOG_OBJECT (payload, "Preparing to push packet with size %"
      G_GSIZE_FORMAT ", seq=%d, rtptime=%u, pts %" GST_TIME_FORMAT,
      (is_list) ? -1 : gst_buffer_get_size (GST_BUFFER (obj)),
      payload->seqnum, data.rtptime, GST_TIME_ARGS (data.pts));

  if (g_atomic_int_compare_and_exchange (&payload->priv->
          notified_first_timestamp, 1, 0)) {
    g_object_notify (G_OBJECT (payload), "timestamp");
    g_object_notify (G_OBJECT (payload), "seqnum");
  }

  return GST_FLOW_OK;

  /* ERRORS */
no_rate:
  {
    GST_ELEMENT_ERROR (payload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not specify clock-rate"));
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_rtp_base_payload_push_list:
 * @payload: a #GstRTPBasePayload
 * @list: a #GstBufferList
 *
 * Push @list to the peer element of the payloader. The SSRC, payload type,
 * seqnum and timestamp of the RTP buffer will be updated first.
 *
 * This function takes ownership of @list.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_rtp_base_payload_push_list (GstRTPBasePayload * payload,
    GstBufferList * list)
{
  GstFlowReturn res;

  res = gst_rtp_base_payload_prepare_push (payload, list, TRUE);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push_list (payload->srcpad, list);
  else
    gst_buffer_list_unref (list);

  return res;
}

/**
 * gst_rtp_base_payload_push:
 * @payload: a #GstRTPBasePayload
 * @buffer: a #GstBuffer
 *
 * Push @buffer to the peer element of the payloader. The SSRC, payload type,
 * seqnum and timestamp of the RTP buffer will be updated first.
 *
 * This function takes ownership of @buffer.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_rtp_base_payload_push (GstRTPBasePayload * payload, GstBuffer * buffer)
{
  GstFlowReturn res;

  res = gst_rtp_base_payload_prepare_push (payload, buffer, FALSE);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push (payload->srcpad, buffer);
  else
    gst_buffer_unref (buffer);

  return res;
}

static void
gst_rtp_base_payload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadPrivate *priv;
  gint64 val;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (object);
  priv = rtpbasepayload->priv;

  switch (prop_id) {
    case PROP_MTU:
      rtpbasepayload->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      rtpbasepayload->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      val = g_value_get_uint (value);
      rtpbasepayload->ssrc = val;
      priv->ssrc_random = FALSE;
      break;
    case PROP_TIMESTAMP_OFFSET:
      val = g_value_get_uint (value);
      rtpbasepayload->ts_offset = val;
      priv->ts_offset_random = FALSE;
      break;
    case PROP_SEQNUM_OFFSET:
      val = g_value_get_int (value);
      rtpbasepayload->seqnum_offset = val;
      priv->seqnum_offset_random = (val == -1);
      GST_DEBUG_OBJECT (rtpbasepayload, "seqnum offset 0x%04x, random %d",
          rtpbasepayload->seqnum_offset, priv->seqnum_offset_random);
      break;
    case PROP_MAX_PTIME:
      rtpbasepayload->priv->prop_max_ptime = g_value_get_int64 (value);
      update_max_ptime (rtpbasepayload);
      break;
    case PROP_MIN_PTIME:
      rtpbasepayload->min_ptime = g_value_get_int64 (value);
      break;
    case PROP_PERFECT_RTPTIME:
      priv->perfect_rtptime = g_value_get_boolean (value);
      break;
    case PROP_PTIME_MULTIPLE:
      rtpbasepayload->ptime_multiple = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_base_payload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadPrivate *priv;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (object);
  priv = rtpbasepayload->priv;

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, rtpbasepayload->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, rtpbasepayload->pt);
      break;
    case PROP_SSRC:
      if (priv->ssrc_random)
        g_value_set_uint (value, -1);
      else
        g_value_set_uint (value, rtpbasepayload->ssrc);
      break;
    case PROP_TIMESTAMP_OFFSET:
      if (priv->ts_offset_random)
        g_value_set_uint (value, -1);
      else
        g_value_set_uint (value, (guint32) rtpbasepayload->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      if (priv->seqnum_offset_random)
        g_value_set_int (value, -1);
      else
        g_value_set_int (value, (guint16) rtpbasepayload->seqnum_offset);
      break;
    case PROP_MAX_PTIME:
      g_value_set_int64 (value, rtpbasepayload->max_ptime);
      break;
    case PROP_MIN_PTIME:
      g_value_set_int64 (value, rtpbasepayload->min_ptime);
      break;
    case PROP_TIMESTAMP:
      g_value_set_uint (value, rtpbasepayload->timestamp);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, rtpbasepayload->seqnum);
      break;
    case PROP_PERFECT_RTPTIME:
      g_value_set_boolean (value, priv->perfect_rtptime);
      break;
    case PROP_PTIME_MULTIPLE:
      g_value_set_int64 (value, rtpbasepayload->ptime_multiple);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_base_payload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRTPBasePayload *rtpbasepayload;
  GstRTPBasePayloadPrivate *priv;
  GstStateChangeReturn ret;

  rtpbasepayload = GST_RTP_BASE_PAYLOAD (element);
  priv = rtpbasepayload->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&rtpbasepayload->segment, GST_FORMAT_UNDEFINED);

      if (priv->seqnum_offset_random)
        rtpbasepayload->seqnum_base = g_random_int_range (0, G_MAXUINT16);
      else
        rtpbasepayload->seqnum_base = rtpbasepayload->seqnum_offset;
      priv->next_seqnum = rtpbasepayload->seqnum_base;
      rtpbasepayload->seqnum = rtpbasepayload->seqnum_base;

      if (priv->ssrc_random)
        rtpbasepayload->current_ssrc = g_random_int ();
      else
        rtpbasepayload->current_ssrc = rtpbasepayload->ssrc;

      if (priv->ts_offset_random)
        rtpbasepayload->ts_base = g_random_int ();
      else
        rtpbasepayload->ts_base = rtpbasepayload->ts_offset;
      rtpbasepayload->timestamp = rtpbasepayload->ts_base;
      g_atomic_int_set (&rtpbasepayload->priv->notified_first_timestamp, 1);
      priv->base_offset = GST_BUFFER_OFFSET_NONE;
      priv->negotiated = FALSE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_atomic_int_set (&rtpbasepayload->priv->notified_first_timestamp, 1);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}
