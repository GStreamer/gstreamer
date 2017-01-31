/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gstwebrtcice.h"
/* libnice */
#include <agent.h>
#include "icestream.h"
#include "nicetransport.h"

/* XXX:
 *
 * - are locally generated remote candidates meant to be readded to libnice?
 */

#define GST_CAT_DEFAULT gst_webrtc_ice_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_ice_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCICE, gst_webrtc_ice,
    GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_ice_debug, "webrtcice", 0, "webrtcice");
    );

GQuark
gst_webrtc_ice_error_quark (void)
{
  return g_quark_from_static_string ("gst-webrtc-ice-error-quark");
}

enum
{
  SIGNAL_0,
  ON_ICE_CANDIDATE_SIGNAL,
  ON_ICE_GATHERING_STATE_CHANGE_SIGNAL,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_ICE_GATHERING_STATE,
  PROP_STUN_SERVER,
  PROP_TURN_SERVER,
  PROP_CONTROLLER,
  PROP_AGENT,
};

static guint gst_webrtc_ice_signals[LAST_SIGNAL] = { 0 };

struct _GstWebRTCICEPrivate
{
  NiceAgent *nice_agent;

  GArray *nice_stream_map;

  GThread *thread;
  GMainContext *main_context;
  GMainLoop *loop;
  GMutex lock;
  GCond cond;
};

static gboolean
_unlock_pc_thread (GMutex * lock)
{
  g_mutex_unlock (lock);
  return G_SOURCE_REMOVE;
}

static gpointer
_gst_nice_thread (GstWebRTCICE * ice)
{
  g_mutex_lock (&ice->priv->lock);
  ice->priv->main_context = g_main_context_new ();
  ice->priv->loop = g_main_loop_new (ice->priv->main_context, FALSE);

  g_cond_broadcast (&ice->priv->cond);
  g_main_context_invoke (ice->priv->main_context,
      (GSourceFunc) _unlock_pc_thread, &ice->priv->lock);

  g_main_loop_run (ice->priv->loop);

  g_mutex_lock (&ice->priv->lock);
  g_main_context_unref (ice->priv->main_context);
  ice->priv->main_context = NULL;
  g_main_loop_unref (ice->priv->loop);
  ice->priv->loop = NULL;
  g_cond_broadcast (&ice->priv->cond);
  g_mutex_unlock (&ice->priv->lock);

  return NULL;
}

static void
_start_thread (GstWebRTCICE * ice)
{
  g_mutex_lock (&ice->priv->lock);
  ice->priv->thread = g_thread_new ("gst-nice-ops",
      (GThreadFunc) _gst_nice_thread, ice);

  while (!ice->priv->loop)
    g_cond_wait (&ice->priv->cond, &ice->priv->lock);
  g_mutex_unlock (&ice->priv->lock);
}

static void
_stop_thread (GstWebRTCICE * ice)
{
  g_mutex_lock (&ice->priv->lock);
  g_main_loop_quit (ice->priv->loop);
  while (ice->priv->loop)
    g_cond_wait (&ice->priv->cond, &ice->priv->lock);
  g_mutex_unlock (&ice->priv->lock);

  g_thread_unref (ice->priv->thread);
}

#if 0
static NiceComponentType
_webrtc_component_to_nice (GstWebRTCICEComponent comp)
{
  switch (comp) {
    case GST_WEBRTC_ICE_COMPONENT_RTP:
      return NICE_COMPONENT_TYPE_RTP;
    case GST_WEBRTC_ICE_COMPONENT_RTCP:
      return NICE_COMPONENT_TYPE_RTCP;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static GstWebRTCICEComponent
_nice_component_to_webrtc (NiceComponentType comp)
{
  switch (comp) {
    case NICE_COMPONENT_TYPE_RTP:
      return GST_WEBRTC_ICE_COMPONENT_RTP;
    case NICE_COMPONENT_TYPE_RTCP:
      return GST_WEBRTC_ICE_COMPONENT_RTCP;
    default:
      g_assert_not_reached ();
      return 0;
  }
}
#endif
struct NiceStreamItem
{
  guint session_id;
  guint nice_stream_id;
  GstWebRTCICEStream *stream;
};

/* TRUE to continue, FALSE to stop */
typedef gboolean (*NiceStreamItemForeachFunc) (struct NiceStreamItem * item,
    gpointer user_data);

static void
_nice_stream_item_foreach (GstWebRTCICE * ice, NiceStreamItemForeachFunc func,
    gpointer data)
{
  int i, len;

  len = ice->priv->nice_stream_map->len;
  for (i = 0; i < len; i++) {
    struct NiceStreamItem *item =
        &g_array_index (ice->priv->nice_stream_map, struct NiceStreamItem,
        i);

    if (!func (item, data))
      break;
  }
}

/* TRUE for match, FALSE otherwise */
typedef gboolean (*NiceStreamItemFindFunc) (struct NiceStreamItem * item,
    gpointer user_data);

struct nice_find
{
  NiceStreamItemFindFunc func;
  gpointer data;
  struct NiceStreamItem *ret;
};

static gboolean
_find_nice_item (struct NiceStreamItem *item, gpointer user_data)
{
  struct nice_find *f = user_data;
  if (f->func (item, f->data)) {
    f->ret = item;
    return FALSE;
  }
  return TRUE;
}

static struct NiceStreamItem *
_nice_stream_item_find (GstWebRTCICE * ice, NiceStreamItemFindFunc func,
    gpointer data)
{
  struct nice_find f;

  f.func = func;
  f.data = data;
  f.ret = NULL;

  _nice_stream_item_foreach (ice, _find_nice_item, &f);

  return f.ret;
}

#define NICE_MATCH_INIT { -1, -1, NULL }

static gboolean
_match (struct NiceStreamItem *item, struct NiceStreamItem *m)
{
  if (m->session_id != -1 && m->session_id != item->session_id)
    return FALSE;
  if (m->nice_stream_id != -1 && m->nice_stream_id != item->nice_stream_id)
    return FALSE;
  if (m->stream != NULL && m->stream != item->stream)
    return FALSE;

  return TRUE;
}

static struct NiceStreamItem *
_find_item (GstWebRTCICE * ice, guint session_id, guint nice_stream_id,
    GstWebRTCICEStream * stream)
{
  struct NiceStreamItem m = NICE_MATCH_INIT;

  m.session_id = session_id;
  m.nice_stream_id = nice_stream_id;
  m.stream = stream;

  return _nice_stream_item_find (ice, (NiceStreamItemFindFunc) _match, &m);
}

static struct NiceStreamItem *
_create_nice_stream_item (GstWebRTCICE * ice, guint session_id)
{
  struct NiceStreamItem item;

  item.session_id = session_id;
  item.nice_stream_id = nice_agent_add_stream (ice->priv->nice_agent, 2);
  item.stream = gst_webrtc_ice_stream_new (ice, item.nice_stream_id);
  g_array_append_val (ice->priv->nice_stream_map, item);

  return _find_item (ice, item.session_id, item.nice_stream_id, item.stream);
}

static void
_parse_userinfo (const gchar * userinfo, gchar ** user, gchar ** pass)
{
  const gchar *colon;

  if (!userinfo) {
    *user = NULL;
    *pass = NULL;
    return;
  }

  colon = g_strstr_len (userinfo, -1, ":");
  if (!colon) {
    *user = g_strdup (userinfo);
    *pass = NULL;
    return;
  }

  *user = g_strndup (userinfo, colon - userinfo);
  *pass = g_strdup (&colon[1]);
}

GstWebRTCICEStream *
gst_webrtc_ice_add_stream (GstWebRTCICE * ice, guint session_id)
{
  struct NiceStreamItem m = NICE_MATCH_INIT;
  struct NiceStreamItem *item;

  m.session_id = session_id;
  item = _nice_stream_item_find (ice, (NiceStreamItemFindFunc) _match, &m);
  if (item) {
    GST_ERROR_OBJECT (ice, "stream already added with session_id=%u",
        session_id);
    return 0;
  }

  item = _create_nice_stream_item (ice, session_id);

  if (ice->turn_server) {
    gboolean ret;
    gchar *user, *pass;
    const gchar *userinfo, *transport, *scheme;
    NiceRelayType relays[4] = { 0, };
    int i, relay_n = 0;

    scheme = gst_uri_get_scheme (ice->turn_server);
    transport = gst_uri_get_query_value (ice->turn_server, "transport");
    userinfo = gst_uri_get_userinfo (ice->turn_server);
    _parse_userinfo (userinfo, &user, &pass);

    if (g_strcmp0 (scheme, "turns") == 0) {
      relays[relay_n++] = NICE_RELAY_TYPE_TURN_TLS;
    } else if (g_strcmp0 (scheme, "turn") == 0) {
      if (!transport || g_strcmp0 (transport, "udp") == 0)
        relays[relay_n++] = NICE_RELAY_TYPE_TURN_UDP;
      if (!transport || g_strcmp0 (transport, "tcp") == 0)
        relays[relay_n++] = NICE_RELAY_TYPE_TURN_TCP;
    }
    g_assert (relay_n < G_N_ELEMENTS (relays));

    for (i = 0; i < relay_n; i++) {
      ret = nice_agent_set_relay_info (ice->priv->nice_agent,
          item->nice_stream_id, NICE_COMPONENT_TYPE_RTP,
          gst_uri_get_host (ice->turn_server),
          gst_uri_get_port (ice->turn_server), user, pass, relays[i]);
      if (!ret) {
        gchar *uri = gst_uri_to_string (ice->turn_server);
        GST_ERROR_OBJECT (ice, "Failed to set TURN server '%s'", uri);
        g_free (uri);
        break;
      }
      ret = nice_agent_set_relay_info (ice->priv->nice_agent,
          item->nice_stream_id, NICE_COMPONENT_TYPE_RTCP,
          gst_uri_get_host (ice->turn_server),
          gst_uri_get_port (ice->turn_server), user, pass, relays[i]);
      if (!ret) {
        gchar *uri = gst_uri_to_string (ice->turn_server);
        GST_ERROR_OBJECT (ice, "Failed to set TURN server '%s'", uri);
        g_free (uri);
        break;
      }
    }
    g_free (user);
    g_free (pass);
  }

  return item->stream;
}

static void
_on_new_candidate (NiceAgent * agent, NiceCandidate * candidate,
    GstWebRTCICE * ice)
{
  struct NiceStreamItem *item;
  gchar *attr;

  item = _find_item (ice, -1, candidate->stream_id, NULL);
  if (!item) {
    GST_WARNING_OBJECT (ice, "received signal for non-existent stream %u",
        candidate->stream_id);
    return;
  }

  if (!candidate->username || !candidate->password) {
    gboolean got_credentials;
    gchar *ufrag, *password;

    got_credentials = nice_agent_get_local_credentials (ice->priv->nice_agent,
        candidate->stream_id, &ufrag, &password);
    g_warn_if_fail (got_credentials);

    if (!candidate->username)
      candidate->username = ufrag;
    else
      g_free (ufrag);

    if (!candidate->password)
      candidate->password = password;
    else
      g_free (password);
  }

  attr = nice_agent_generate_local_candidate_sdp (agent, candidate);
  g_signal_emit (ice, gst_webrtc_ice_signals[ON_ICE_CANDIDATE_SIGNAL],
      0, item->session_id, attr);
  g_free (attr);
}

GstWebRTCICETransport *
gst_webrtc_ice_find_transport (GstWebRTCICE * ice, GstWebRTCICEStream * stream,
    GstWebRTCICEComponent component)
{
  struct NiceStreamItem *item;

  item = _find_item (ice, -1, -1, stream);
  g_return_val_if_fail (item != NULL, NULL);

  return gst_webrtc_ice_stream_find_transport (item->stream, component);
}

#if 0
/* TODO don't rely on libnice to (de)serialize candidates */
static NiceCandidateType
_candidate_type_from_string (const gchar * s)
{
  if (g_strcmp0 (s, "host") == 0) {
    return NICE_CANDIDATE_TYPE_HOST;
  } else if (g_strcmp0 (s, "srflx") == 0) {
    return NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
  } else if (g_strcmp0 (s, "prflx") == 0) {     /* FIXME: is the right string? */
    return NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
  } else if (g_strcmp0 (s, "relay") == 0) {
    return NICE_CANDIDATE_TYPE_RELAY;
  } else {
    g_assert_not_reached ();
    return 0;
  }
}

static const gchar *
_candidate_type_to_string (NiceCandidateType type)
{
  switch (type) {
    case NICE_CANDIDATE_TYPE_HOST:
      return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
      return "srflx";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
      return "prflx";
    case NICE_CANDIDATE_TYPE_RELAY:
      return "relay";
    default:
      g_assert_not_reached ();
      return NULL;
  }
}

static NiceCandidateTransport
_candidate_transport_from_string (const gchar * s)
{
  if (g_strcmp0 (s, "UDP") == 0) {
    return NICE_CANDIDATE_TRANSPORT_UDP;
  } else if (g_strcmp0 (s, "TCP tcptype") == 0) {
    return NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE;
  } else if (g_strcmp0 (s, "tcp-passive") == 0) {       /* FIXME: is the right string? */
    return NICE_CANDIDATE_TRANSPORT_TCP_PASSIVE;
  } else if (g_strcmp0 (s, "tcp-so") == 0) {
    return NICE_CANDIDATE_TRANSPORT_TCP_SO;
  } else {
    g_assert_not_reached ();
    return 0;
  }
}

static const gchar *
_candidate_type_to_string (NiceCandidateType type)
{
  switch (type) {
    case NICE_CANDIDATE_TYPE_HOST:
      return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
      return "srflx";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
      return "prflx";
    case NICE_CANDIDATE_TYPE_RELAY:
      return "relay";
    default:
      g_assert_not_reached ();
      return NULL;
  }
}
#endif

/* must start with "a=candidate:" */
void
gst_webrtc_ice_add_candidate (GstWebRTCICE * ice, GstWebRTCICEStream * stream,
    const gchar * candidate)
{
  struct NiceStreamItem *item;
  NiceCandidate *cand;
  GSList *candidates = NULL;

  item = _find_item (ice, -1, -1, stream);
  g_return_if_fail (item != NULL);

  cand =
      nice_agent_parse_remote_candidate_sdp (ice->priv->nice_agent,
      item->nice_stream_id, candidate);
  if (!cand) {
    GST_WARNING_OBJECT (ice, "Could not parse candidate \'%s\'", candidate);
    return;
  }

  candidates = g_slist_append (candidates, cand);

  nice_agent_set_remote_candidates (ice->priv->nice_agent, item->nice_stream_id,
      cand->component_id, candidates);

  g_slist_free (candidates);
  nice_candidate_free (cand);
}

gboolean
gst_webrtc_ice_set_remote_credentials (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, gchar * ufrag, gchar * pwd)
{
  struct NiceStreamItem *item;

  g_return_val_if_fail (ufrag != NULL, FALSE);
  g_return_val_if_fail (pwd != NULL, FALSE);
  item = _find_item (ice, -1, -1, stream);
  g_return_val_if_fail (item != NULL, FALSE);

  GST_DEBUG_OBJECT (ice, "Setting remote ICE credentials on "
      "ICE stream %u ufrag:%s pwd:%s", item->nice_stream_id, ufrag, pwd);

  nice_agent_set_remote_credentials (ice->priv->nice_agent,
      item->nice_stream_id, ufrag, pwd);

  return TRUE;
}

gboolean
gst_webrtc_ice_set_local_credentials (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream, gchar * ufrag, gchar * pwd)
{
  struct NiceStreamItem *item;

  g_return_val_if_fail (ufrag != NULL, FALSE);
  g_return_val_if_fail (pwd != NULL, FALSE);
  item = _find_item (ice, -1, -1, stream);
  g_return_val_if_fail (item != NULL, FALSE);

  GST_DEBUG_OBJECT (ice, "Setting local ICE credentials on "
      "ICE stream %u ufrag:%s pwd:%s", item->nice_stream_id, ufrag, pwd);

  nice_agent_set_local_credentials (ice->priv->nice_agent, item->nice_stream_id,
      ufrag, pwd);

  return TRUE;
}

gboolean
gst_webrtc_ice_gather_candidates (GstWebRTCICE * ice,
    GstWebRTCICEStream * stream)
{
  struct NiceStreamItem *item;

  item = _find_item (ice, -1, -1, stream);
  g_return_val_if_fail (item != NULL, FALSE);

  GST_DEBUG_OBJECT (ice, "gather candidates for stream %u",
      item->nice_stream_id);

  return gst_webrtc_ice_stream_gather_candidates (stream);
}

static void
_clear_ice_stream (struct NiceStreamItem *item)
{
  if (!item)
    return;

  if (item->stream) {
    g_signal_handlers_disconnect_by_data (item->stream->ice->priv->nice_agent,
        item->stream);
    gst_object_unref (item->stream);
  }
}

static gchar *
_resolve_host (const gchar * host)
{
  GResolver *resolver = g_resolver_get_default ();
  GError *error = NULL;
  GInetAddress *addr;
  GList *addresses;

  if (!(addresses = g_resolver_lookup_by_name (resolver, host, NULL, &error))) {
    GST_ERROR ("%s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  /* XXX: only the first address is used */
  addr = addresses->data;

  return g_inet_address_to_string (addr);
}

static void
_set_turn_server (GstWebRTCICE * ice, const gchar * s)
{
  GstUri *uri = gst_uri_from_string (s);
  const gchar *userinfo, *host, *scheme;
  GList *keys = NULL, *l;
  gchar *ip = NULL, *user = NULL, *pass = NULL;
  gboolean turn_tls = FALSE;
  guint port;

  GST_DEBUG_OBJECT (ice, "setting turn server, %s", s);

  if (!uri) {
    GST_ERROR_OBJECT (ice, "Could not parse turn server '%s'", s);
    return;
  }

  scheme = gst_uri_get_scheme (uri);
  if (g_strcmp0 (scheme, "turn") == 0) {
  } else if (g_strcmp0 (scheme, "turns") == 0) {
    turn_tls = TRUE;
  } else {
    GST_ERROR_OBJECT (ice, "unknown scheme '%s'", scheme);
    goto out;
  }

  keys = gst_uri_get_query_keys (uri);
  for (l = keys; l; l = l->next) {
    gchar *key = l->data;

    if (g_strcmp0 (key, "transport") == 0) {
      const gchar *transport = gst_uri_get_query_value (uri, "transport");
      if (!transport) {
      } else if (g_strcmp0 (transport, "udp") == 0) {
      } else if (g_strcmp0 (transport, "tcp") == 0) {
      } else {
        GST_ERROR_OBJECT (ice, "unknown transport value, '%s'", transport);
        goto out;
      }
    } else {
      GST_ERROR_OBJECT (ice, "unknown query key, '%s'", key);
      goto out;
    }
  }

  /* TODO: Implement error checking similar to the stun server below */
  userinfo = gst_uri_get_userinfo (uri);
  _parse_userinfo (userinfo, &user, &pass);
  if (!user) {
    GST_ERROR_OBJECT (ice, "No username specified in '%s'", s);
    goto out;
  }
  if (!pass) {
    GST_ERROR_OBJECT (ice, "No password specified in '%s'", s);
    goto out;
  }

  host = gst_uri_get_host (uri);
  if (!host) {
    GST_ERROR_OBJECT (ice, "Turn server has no host");
    goto out;
  }
  ip = _resolve_host (host);
  if (!ip) {
    GST_ERROR_OBJECT (ice, "Failed to resolve turn server '%s'", host);
    goto out;
  }
  port = gst_uri_get_port (uri);

  if (port == GST_URI_NO_PORT) {
    if (turn_tls) {
      gst_uri_set_port (uri, 5349);
    } else {
      gst_uri_set_port (uri, 3478);
    }
  }
  /* Set the resolved IP as the host since that's what libnice wants */
  gst_uri_set_host (uri, ip);

  if (ice->turn_server)
    gst_uri_unref (ice->turn_server);
  ice->turn_server = uri;

out:
  g_list_free (keys);
  g_free (ip);
  g_free (user);
  g_free (pass);
}

static void
gst_webrtc_ice_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCICE *ice = GST_WEBRTC_ICE (object);

  switch (prop_id) {
    case PROP_STUN_SERVER:{
      const gchar *s = g_value_get_string (value);
      GstUri *uri = gst_uri_from_string (s);
      const gchar *msg = "must be of the form stun://<host>:<port>";
      const gchar *host;
      gchar *ip;
      guint port;

      GST_DEBUG_OBJECT (ice, "setting stun server, %s", s);

      if (!uri) {
        GST_ERROR_OBJECT (ice, "Couldn't parse stun server '%s', %s", s, msg);
        return;
      }

      host = gst_uri_get_host (uri);
      if (!host) {
        GST_ERROR_OBJECT (ice, "Stun server '%s' has no host, %s", s, msg);
        return;
      }
      port = gst_uri_get_port (uri);
      if (port == GST_URI_NO_PORT) {
        GST_INFO_OBJECT (ice, "Stun server '%s' has no port, assuming 3478", s);
        port = 3478;
        gst_uri_set_port (uri, port);
      }

      ip = _resolve_host (host);
      if (!ip) {
        GST_ERROR_OBJECT (ice, "Failed to resolve stun server '%s'", host);
        return;
      }

      if (ice->stun_server)
        gst_uri_unref (ice->stun_server);
      ice->stun_server = uri;

      g_object_set (ice->priv->nice_agent, "stun-server", ip,
          "stun-server-port", port, NULL);

      g_free (ip);
      break;
    }
    case PROP_TURN_SERVER:{
      _set_turn_server (ice, g_value_get_string (value));
      break;
    }
    case PROP_CONTROLLER:
      g_object_set_property (G_OBJECT (ice->priv->nice_agent),
          "controlling-mode", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_ice_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCICE *ice = GST_WEBRTC_ICE (object);

  switch (prop_id) {
    case PROP_STUN_SERVER:
      if (ice->stun_server)
        g_value_take_string (value, gst_uri_to_string (ice->stun_server));
      else
        g_value_take_string (value, NULL);
      break;
    case PROP_TURN_SERVER:
      if (ice->turn_server)
        g_value_take_string (value, gst_uri_to_string (ice->turn_server));
      else
        g_value_take_string (value, NULL);
      break;
    case PROP_CONTROLLER:
      g_object_get_property (G_OBJECT (ice->priv->nice_agent),
          "controlling-mode", value);
      break;
    case PROP_AGENT:
      g_value_set_object (value, ice->priv->nice_agent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_ice_finalize (GObject * object)
{
  GstWebRTCICE *ice = GST_WEBRTC_ICE (object);

  g_signal_handlers_disconnect_by_data (ice->priv->nice_agent, ice);

  _stop_thread (ice);

  if (ice->turn_server)
    gst_uri_unref (ice->turn_server);
  if (ice->stun_server)
    gst_uri_unref (ice->stun_server);

  g_mutex_clear (&ice->priv->lock);
  g_cond_clear (&ice->priv->cond);

  g_array_free (ice->priv->nice_stream_map, TRUE);

  g_object_unref (ice->priv->nice_agent);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_ice_class_init (GstWebRTCICEClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstWebRTCICEPrivate));

  gobject_class->get_property = gst_webrtc_ice_get_property;
  gobject_class->set_property = gst_webrtc_ice_set_property;
  gobject_class->finalize = gst_webrtc_ice_finalize;

  g_object_class_install_property (gobject_class,
      PROP_STUN_SERVER,
      g_param_spec_string ("stun-server", "STUN Server",
          "The STUN server of the form stun://hostname:port",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_TURN_SERVER,
      g_param_spec_string ("turn-server", "TURN Server",
          "The TURN server of the form turn(s)://username:password@host:port",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_CONTROLLER,
      g_param_spec_boolean ("controller", "ICE controller",
          "Whether the ICE agent is the controller or controlled. "
          "In WebRTC, the initial offerrer is the ICE controller.", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGENT,
      g_param_spec_object ("agent", "ICE agent",
          "ICE agent in use by this object", NICE_TYPE_AGENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstWebRTCICE::on-ice-candidate:
   * @object: the #GstWebRtcBin
   * @candidate: the ICE candidate
   */
  gst_webrtc_ice_signals[ON_ICE_CANDIDATE_SIGNAL] =
      g_signal_new ("on-ice-candidate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

static void
gst_webrtc_ice_init (GstWebRTCICE * ice)
{
  ice->priv =
      G_TYPE_INSTANCE_GET_PRIVATE ((ice), GST_TYPE_WEBRTC_ICE,
      GstWebRTCICEPrivate);

  g_mutex_init (&ice->priv->lock);
  g_cond_init (&ice->priv->cond);

  _start_thread (ice);

  ice->priv->nice_agent = nice_agent_new (ice->priv->main_context,
      NICE_COMPATIBILITY_RFC5245);
  g_signal_connect (ice->priv->nice_agent, "new-candidate-full",
      G_CALLBACK (_on_new_candidate), ice);

  ice->priv->nice_stream_map =
      g_array_new (FALSE, TRUE, sizeof (struct NiceStreamItem));
  g_array_set_clear_func (ice->priv->nice_stream_map,
      (GDestroyNotify) _clear_ice_stream);
}

GstWebRTCICE *
gst_webrtc_ice_new (void)
{
  return g_object_new (GST_TYPE_WEBRTC_ICE, NULL);
}
