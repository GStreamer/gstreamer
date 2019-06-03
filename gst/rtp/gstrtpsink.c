/* GStreamer
 * Copyright (C) <2018> Marc Leeman <marc.leeman@gmail.com>
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
 * SECTION: gstrtsinkp
 * @title: GstRtpSink
 * @short description: element with Uri interface to stream RTP data to
 * the network.
 *
 * RTP (RFC 3550) is a protocol to stream media over the network while
 * retaining the timing information and providing enough information to
 * reconstruct the correct timing domain by the receiver.
 *
 * The RTP data port should be even, while the RTCP port should be
 * odd. The URI that is entered defines the data port, the RTCP port will
 * be allocated to the next port.
 *
 * This element hooks up the correct sockets to support both RTP as the
 * accompanying RTCP layer.
 *
 * This Bin handles streaming RTP payloaded data on the network.
 *
 * This element also implements the URI scheme `rtp://` allowing to send
 * data on the network by bins that allow use the URI to determine the sink.
 * The RTP URI handler also allows setting properties through the URI query.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#include "gstrtpsink.h"
#include "gstrtp-utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_sink_debug);
#define GST_CAT_DEFAULT gst_rtp_sink_debug

#define DEFAULT_PROP_URI              "rtp://0.0.0.0:5004"
#define DEFAULT_PROP_TTL              64
#define DEFAULT_PROP_TTL_MC           1

enum
{
  PROP_0,

  PROP_URI,
  PROP_TTL,
  PROP_TTL_MC,

  PROP_LAST
};

static void gst_rtp_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_rtp_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRtpSink, gst_rtp_sink, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_rtp_sink_uri_handler_init);
    GST_DEBUG_CATEGORY_INIT (gst_rtp_sink_debug, "rtpsink", 0, "RTP Sink"));

#define GST_RTP_SINK_GET_LOCK(obj) (&((GstRtpSink*)(obj))->lock)
#define GST_RTP_SINK_LOCK(obj) (g_mutex_lock (GST_RTP_SINK_GET_LOCK(obj)))
#define GST_RTP_SINK_UNLOCK(obj) (g_mutex_unlock (GST_RTP_SINK_GET_LOCK(obj)))

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStateChangeReturn
gst_rtp_sink_change_state (GstElement * element, GstStateChange transition);

static void
gst_rtp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpSink *self = GST_RTP_SINK (object);

  switch (prop_id) {
    case PROP_URI:{
      GstUri *uri = NULL;

      GST_RTP_SINK_LOCK (object);
      uri = gst_uri_from_string (g_value_get_string (value));
      if (uri == NULL)
        break;

      if (self->uri)
        gst_uri_unref (self->uri);
      self->uri = uri;
      /* RTP data ports should be even according to RFC 3550, while the
       * RTCP is sent on odd ports. Just warn if there is a mismatch. */
      if (gst_uri_get_port (self->uri) % 2)
        GST_WARNING_OBJECT (self,
            "Port %u is not even, this is not standard (see RFC 3550).",
            gst_uri_get_port (self->uri));

      gst_rtp_utils_set_properties_from_uri_query (G_OBJECT (self), self->uri);
      GST_RTP_SINK_UNLOCK (object);
      break;
    }
    case PROP_TTL:
      self->ttl = g_value_get_int (value);
      break;
    case PROP_TTL_MC:
      self->ttl_mc = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpSink *self = GST_RTP_SINK (object);

  switch (prop_id) {
    case PROP_URI:
      GST_RTP_SINK_LOCK (object);
      if (self->uri)
        g_value_take_string (value, gst_uri_to_string (self->uri));
      else
        g_value_set_string (value, NULL);
      GST_RTP_SINK_UNLOCK (object);
      break;
    case PROP_TTL:
      g_value_set_int (value, self->ttl);
      break;
    case PROP_TTL_MC:
      g_value_set_int (value, self->ttl_mc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_sink_finalize (GObject * gobject)
{
  GstRtpSink *self = GST_RTP_SINK (gobject);

  if (self->uri)
    gst_uri_unref (self->uri);

  g_mutex_clear (&self->lock);
  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gboolean
gst_rtp_sink_setup_elements (GstRtpSink * self)
{
  /*GstPad *pad; */
  GSocket *socket;
  GInetAddress *addr;
  gchar name[48];
  GstCaps *caps;

  /* Should not be NULL */
  g_return_val_if_fail (self->uri != NULL, FALSE);

  /* if not already configured */
  if (self->funnel_rtp == NULL) {
    self->funnel_rtp = gst_element_factory_make ("funnel", NULL);
    if (self->funnel_rtp == NULL) {
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
          ("%s", "funnel_rtp element is not available"));
      return FALSE;
    }

    self->funnel_rtcp = gst_element_factory_make ("funnel", NULL);
    if (self->funnel_rtcp == NULL) {
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
          ("%s", "funnel_rtcp element is not available"));
      return FALSE;
    }

    self->rtp_sink = gst_element_factory_make ("udpsink", NULL);
    if (self->rtp_sink == NULL) {
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
          ("%s", "rtp_sink element is not available"));
      return FALSE;
    }

    self->rtcp_src = gst_element_factory_make ("udpsrc", NULL);
    if (self->rtcp_src == NULL) {
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
          ("%s", "rtcp_src element is not available"));
      return FALSE;
    }

    self->rtcp_sink = gst_element_factory_make ("udpsink", NULL);
    if (self->rtcp_sink == NULL) {
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
          ("%s", "rtcp_sink element is not available"));
      return FALSE;
    }

    gst_bin_add (GST_BIN (self), self->funnel_rtp);
    gst_bin_add (GST_BIN (self), self->funnel_rtcp);

    /* Add elements as needed, since udpsrc/udpsink for RTCP share a socket,
     * not all at the same moment */
    g_object_set (self->rtp_sink,
        "host", gst_uri_get_host (self->uri),
        "port", gst_uri_get_port (self->uri),
        "ttl", self->ttl, "ttl-mc", self->ttl_mc, NULL);

    gst_bin_add (GST_BIN (self), self->rtp_sink);

    g_object_set (self->rtcp_sink,
        "host", gst_uri_get_host (self->uri),
        "port", gst_uri_get_port (self->uri) + 1,
        "ttl", self->ttl, "ttl-mc", self->ttl_mc,
        /* Set false since we're reusing a socket */
        "auto-multicast", FALSE, NULL);

    gst_bin_add (GST_BIN (self), self->rtcp_sink);

    /* no need to set address if unicast */
    caps = gst_caps_new_empty_simple ("application/x-rtcp");
    g_object_set (self->rtcp_src,
        "port", gst_uri_get_port (self->uri) + 1, "caps", caps, NULL);
    gst_caps_unref (caps);

    addr = g_inet_address_new_from_string (gst_uri_get_host (self->uri));
    if (g_inet_address_get_is_multicast (addr)) {
      g_object_set (self->rtcp_src, "address", gst_uri_get_host (self->uri),
          NULL);
    }
    g_object_unref (addr);

    gst_bin_add (GST_BIN (self), self->rtcp_src);

    gst_element_link (self->funnel_rtp, self->rtp_sink);
    gst_element_link (self->funnel_rtcp, self->rtcp_sink);

    gst_element_sync_state_with_parent (self->funnel_rtp);
    gst_element_sync_state_with_parent (self->funnel_rtcp);
    gst_element_sync_state_with_parent (self->rtp_sink);
    gst_element_sync_state_with_parent (self->rtcp_src);

    g_object_get (G_OBJECT (self->rtcp_src), "used-socket", &socket, NULL);
    g_object_set (G_OBJECT (self->rtcp_sink), "socket", socket, NULL);

    gst_element_sync_state_with_parent (self->rtcp_sink);

  }

  /* pads are all named */
  g_snprintf (name, 48, "send_rtp_src_%u", GST_ELEMENT (self)->numpads);
  gst_element_link_pads (self->rtpbin, name, self->funnel_rtp, "sink_%u");

  g_snprintf (name, 48, "send_rtcp_src_%u", GST_ELEMENT (self)->numpads);
  gst_element_link_pads (self->rtpbin, name, self->funnel_rtcp, "sink_%u");

  g_snprintf (name, 48, "recv_rtcp_sink_%u", GST_ELEMENT (self)->numpads);
  gst_element_link_pads (self->rtcp_src, "src", self->rtpbin, name);

  return TRUE;
}

static GstPad *
gst_rtp_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstRtpSink *self = GST_RTP_SINK (element);
  GstPad *pad = NULL;

  if (self->rtpbin == NULL) {
    GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
        ("%s", "rtpbin element is not available"));
    return NULL;
  }

  if (gst_rtp_sink_setup_elements (self) == FALSE)
    return NULL;

  GST_RTP_SINK_LOCK (self);

  pad = gst_element_get_request_pad (self->rtpbin, "send_rtp_sink_%u");
  g_return_val_if_fail (pad != NULL, NULL);

  GST_RTP_SINK_UNLOCK (self);

  return pad;
}

static void
gst_rtp_sink_release_pad (GstElement * element, GstPad * pad)
{
  GstRtpSink *self = GST_RTP_SINK (element);
  GstPad *rpad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));

  GST_RTP_SINK_LOCK (self);
  gst_element_release_request_pad (self->rtpbin, rpad);
  gst_object_unref (rpad);

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (self), pad);

  GST_RTP_SINK_UNLOCK (self);
}

static void
gst_rtp_sink_class_init (GstRtpSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_rtp_sink_set_property;
  gobject_class->get_property = gst_rtp_sink_get_property;
  gobject_class->finalize = gst_rtp_sink_finalize;
  gstelement_class->change_state = gst_rtp_sink_change_state;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_sink_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_sink_release_pad);

  /**
   * GstRtpSink:uri:
   *
   * uri to stream RTP to. All GStreamer parameters can be
   * encoded in the URI, this URI format is RFC compliant.
   */
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI in the form of rtp://host:port?query", DEFAULT_PROP_URI,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpSink:ttl:
   *
   * Set the unicast TTL parameter.
   */
  g_object_class_install_property (gobject_class, PROP_TTL,
      g_param_spec_int ("ttl", "Unicast TTL",
          "Used for setting the unicast TTL parameter",
          0, 255, DEFAULT_PROP_TTL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpSink:ttl-mc:
   *
   * Set the multicast TTL parameter.
   */
  g_object_class_install_property (gobject_class, PROP_TTL_MC,
      g_param_spec_int ("ttl-mc", "Multicast TTL",
          "Used for setting the multicast TTL parameter", 0, 255,
          DEFAULT_PROP_TTL_MC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Sink element",
      "Generic/Bin/Sink",
      "Simple RTP sink", "Marc Leeman <marc.leeman@gmail.com>");
}

static void
gst_rtp_sink_rtpbin_element_added_cb (GstBin * element,
    GstElement * new_element, gpointer data)
{
  GstRtpSink *self = GST_RTP_SINK (data);
  GST_INFO_OBJECT (self,
      "Element %" GST_PTR_FORMAT " added element %" GST_PTR_FORMAT ".", element,
      new_element);
}

static void
gst_rtp_sink_rtpbin_pad_added_cb (GstElement * element, GstPad * pad,
    gpointer data)
{
  GstRtpSink *self = GST_RTP_SINK (data);
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  GstPad *upad;

  /* Expose RTP data pad only */
  GST_INFO_OBJECT (self,
      "Element %" GST_PTR_FORMAT " added pad %" GST_PTR_FORMAT "with caps %"
      GST_PTR_FORMAT ".", element, pad, caps);

  /* Sanity checks */
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    /* Src pad, do not expose */
    gst_caps_unref (caps);
    return;
  }

  if (G_LIKELY (caps)) {
    GstCaps *ref_caps = gst_caps_new_empty_simple ("application/x-rtcp");

    if (gst_caps_can_intersect (caps, ref_caps)) {
      /* SRC RTCP caps, do not expose */
      gst_caps_unref (ref_caps);
      gst_caps_unref (caps);

      return;
    }
    gst_caps_unref (ref_caps);
  } else {
    GST_ERROR_OBJECT (self, "Pad with no caps detected.");
    gst_caps_unref (caps);

    return;
  }
  gst_caps_unref (caps);

  upad = gst_element_get_compatible_pad (self->funnel_rtp, pad, NULL);
  if (upad == NULL) {
    GST_ERROR_OBJECT (self, "No compatible pad found to link pad.");
    gst_caps_unref (caps);

    return;
  }
  GST_INFO_OBJECT (self, "Linking with pad %" GST_PTR_FORMAT ".", upad);
  gst_pad_link (pad, upad);
  gst_object_unref (upad);
}

static void
gst_rtp_sink_rtpbin_pad_removed_cb (GstElement * element, GstPad * pad,
    gpointer data)
{
  GstRtpSink *self = GST_RTP_SINK (data);
  GST_INFO_OBJECT (self,
      "Element %" GST_PTR_FORMAT " removed pad %" GST_PTR_FORMAT ".", element,
      pad);
}

static gboolean
gst_rtp_sink_setup_rtpbin (GstRtpSink * self)
{
  self->rtpbin = gst_element_factory_make ("rtpbin", NULL);
  if (self->rtpbin == NULL) {
    GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
        ("%s", "rtpbin element is not available"));
    return FALSE;
  }

  /* Add rtpbin callbacks to monitor the operation of rtpbin */
  g_signal_connect (self->rtpbin, "element-added",
      G_CALLBACK (gst_rtp_sink_rtpbin_element_added_cb), self);
  g_signal_connect (self->rtpbin, "pad-added",
      G_CALLBACK (gst_rtp_sink_rtpbin_pad_added_cb), self);
  g_signal_connect (self->rtpbin, "pad-removed",
      G_CALLBACK (gst_rtp_sink_rtpbin_pad_removed_cb), self);

  gst_bin_add (GST_BIN (self), self->rtpbin);

  gst_element_sync_state_with_parent (self->rtpbin);

  return TRUE;
}

static GstStateChangeReturn
gst_rtp_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpSink *self = GST_RTP_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (self, "changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}


static void
gst_rtp_sink_init (GstRtpSink * self)
{
  self->rtpbin = NULL;
  self->funnel_rtp = NULL;
  self->funnel_rtcp = NULL;
  self->rtp_sink = NULL;
  self->rtcp_src = NULL;
  self->rtcp_sink = NULL;

  self->uri = gst_uri_from_string (DEFAULT_PROP_URI);
  self->ttl = DEFAULT_PROP_TTL;
  self->ttl_mc = DEFAULT_PROP_TTL_MC;

  if (gst_rtp_sink_setup_rtpbin (self) == FALSE)
    return;

  GST_OBJECT_FLAG_SET (GST_OBJECT (self), GST_ELEMENT_FLAG_SINK);
  gst_bin_set_suppressed_flags (GST_BIN (self),
      GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK);

  g_mutex_init (&self->lock);
}

static guint
gst_rtp_sink_uri_get_type (GType type)
{
  return GST_URI_SINK;
}

static const gchar *const *
gst_rtp_sink_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { (char *) "rtp", NULL };

  return protocols;
}

static gchar *
gst_rtp_sink_uri_get_uri (GstURIHandler * handler)
{
  GstRtpSink *self = (GstRtpSink *) handler;

  return gst_uri_to_string (self->uri);
}

static gboolean
gst_rtp_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstRtpSink *self = (GstRtpSink *) handler;

  g_object_set (G_OBJECT (self), "uri", uri, NULL);

  return TRUE;
}

static void
gst_rtp_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtp_sink_uri_get_type;
  iface->get_protocols = gst_rtp_sink_uri_get_protocols;
  iface->get_uri = gst_rtp_sink_uri_get_uri;
  iface->set_uri = gst_rtp_sink_uri_set_uri;
}

/* ex: set tabstop=2 shiftwidth=2 expandtab: */
