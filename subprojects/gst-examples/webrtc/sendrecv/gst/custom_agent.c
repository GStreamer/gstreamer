#include "custom_agent.h"
#include <gst/webrtc/nice/nice.h>

struct _CustomICEAgent
{
  GstWebRTCICE parent;
  GstWebRTCNice *nice_agent;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (CustomICEAgent, customice_agent, GST_TYPE_WEBRTC_ICE)
/* *INDENT-ON* */

GstWebRTCICEStream *
customice_agent_add_stream (GstWebRTCICE * ice, guint session_id)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_add_stream (c_ice, session_id);
}

GstWebRTCICETransport *
customice_agent_find_transport (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, GstWebRTCICEComponent component)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_find_transport (c_ice, stream, component);
}

void
customice_agent_add_candidate (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, const gchar * candidate, GstPromise * promise)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_add_candidate (c_ice, stream, candidate, promise);
}

gboolean
customice_agent_set_remote_credentials (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, const gchar * ufrag, const gchar * pwd)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_set_remote_credentials (c_ice, stream, ufrag, pwd);
}

gboolean
customice_agent_add_turn_server (GstWebRTCICE * ice, const gchar * uri)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_add_turn_server (c_ice, uri);
}

gboolean
customice_agent_set_local_credentials (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, const gchar * ufrag, const gchar * pwd)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_set_local_credentials (c_ice, stream, ufrag, pwd);
}

gboolean
customice_agent_gather_candidates (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_gather_candidates (c_ice, stream);
}

void
customice_agent_set_is_controller (GstWebRTCICE * ice, gboolean controller)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_is_controller (c_ice, controller);
}

gboolean
customice_agent_get_is_controller (GstWebRTCICE * ice)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_get_is_controller (c_ice);
}

void
customice_agent_set_force_relay (GstWebRTCICE * ice, gboolean force_relay)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_force_relay (c_ice, force_relay);
}

void
customice_agent_set_tos (GstWebRTCICE * ice, GstWebRTCICEStream * stream,
    guint tos)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_tos (c_ice, stream, tos);
}

void
customice_agent_set_on_ice_candidate (GstWebRTCICE * ice,
    GstWebRTCICEOnCandidateFunc func, gpointer user_data, GDestroyNotify notify)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_on_ice_candidate (c_ice, func, user_data, notify);
}

void
customice_agent_set_stun_server (GstWebRTCICE * ice, const gchar * uri_s)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_stun_server (c_ice, uri_s);
}

gchar *
customice_agent_get_stun_server (GstWebRTCICE * ice)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_get_stun_server (c_ice);
}

void
customice_agent_set_turn_server (GstWebRTCICE * ice, const gchar * uri_s)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  gst_webrtc_ice_set_turn_server (c_ice, uri_s);
}

gchar *
customice_agent_get_turn_server (GstWebRTCICE * ice)
{
  GstWebRTCICE *c_ice = GST_WEBRTC_ICE (CUSTOMICE_AGENT (ice)->nice_agent);
  return gst_webrtc_ice_get_turn_server (c_ice);
}

static void
customice_agent_class_init (CustomICEAgentClass * klass)
{
  GstWebRTCICEClass *gst_webrtc_ice_class = GST_WEBRTC_ICE_CLASS (klass);

  // override virtual functions
  gst_webrtc_ice_class->add_candidate = customice_agent_add_candidate;
  gst_webrtc_ice_class->add_stream = customice_agent_add_stream;
  gst_webrtc_ice_class->add_turn_server = customice_agent_add_turn_server;
  gst_webrtc_ice_class->find_transport = customice_agent_find_transport;
  gst_webrtc_ice_class->gather_candidates = customice_agent_gather_candidates;
  gst_webrtc_ice_class->get_is_controller = customice_agent_get_is_controller;
  gst_webrtc_ice_class->get_stun_server = customice_agent_get_stun_server;
  gst_webrtc_ice_class->get_turn_server = customice_agent_get_turn_server;
  gst_webrtc_ice_class->set_force_relay = customice_agent_set_force_relay;
  gst_webrtc_ice_class->set_is_controller = customice_agent_set_is_controller;
  gst_webrtc_ice_class->set_local_credentials =
      customice_agent_set_local_credentials;
  gst_webrtc_ice_class->set_remote_credentials =
      customice_agent_set_remote_credentials;
  gst_webrtc_ice_class->set_stun_server = customice_agent_set_stun_server;
  gst_webrtc_ice_class->set_tos = customice_agent_set_tos;
  gst_webrtc_ice_class->set_turn_server = customice_agent_set_turn_server;
  gst_webrtc_ice_class->set_on_ice_candidate =
      customice_agent_set_on_ice_candidate;
}

static void
customice_agent_init (CustomICEAgent * ice)
{
  ice->nice_agent = gst_webrtc_nice_new ("nice_agent");
}

CustomICEAgent *
customice_agent_new (const gchar * name)
{
  return g_object_new (GST_TYPE_WEBRTC_NICE, "name", name, NULL);
}
