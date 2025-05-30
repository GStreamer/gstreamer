/* GStreamer
 *   Author: Jonas Danielsson <jonas.danielsson@spiideo,com>
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
 */

#include "gstsrtobject.h"
#include "gstsrtcaller.h"
#include "gstsrtlistenerconnection.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_srtobject);
#define GST_CAT_DEFAULT gst_debug_srtobject

/*
 * This code is responsible for SRT listening connections. That means
 * listening for- and accepting connections from SRT callers.
 *
 * We want to be able to multiplex several SRT connections over the same UDP
 * socket so we keep a global table of connections, indexed by an unique id.
 *
 * When a GstSRTConnection is created we start a thread for accepting callers
 * and subsequent users of the same connection will rely on that thread for
 * accepting callers.
 */

/* This is a global table of all listening connections for an application */
static GHashTable *connections_table;

/* This lock guards access to the connections_table.
 *
 * It is recursive because gst_srt_listener_connection_get_object() sets a
 * GstSRTObject's "streamid" property while holding it, and a notify::streamid
 * handler is allowed to call back into gst_srt_listener_connection_add_object()
 * on the same thread.
 */
static GRecMutex connections_lock;

static void
gst_srt_listener_connection_destroy (GstSRTListenerConnection * connection)
{
  GST_DEBUG ("Destroying listener connection");

  if (connection->sock != SRT_INVALID_SOCK) {
    srt_close (connection->sock);
  }

  if (connection->rsock != SRT_INVALID_SOCK) {
    srt_close (connection->rsock);
  }

  g_thread_join (connection->accept_thread);
  g_free (connection->key);
  g_list_free (connection->objects);
  g_free (connection);
}

static GHashTable *
gst_srt_connections_get_unlocked (gboolean create)
{
  if (connections_table == NULL && create) {
    connections_table =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  }
  return connections_table;
}

static void
gst_srt_listener_connection_remove_unlocked (GstSRTListenerConnection *
    connection)
{
  GHashTable *connections = gst_srt_connections_get_unlocked (FALSE);
  if (connections) {
    g_hash_table_remove (connections, connection->key);
  }
}

static int
gst_srt_listener_connection_compare_func (gconstpointer object,
    gconstpointer stream_id)
{
  const char *object_stream_id =
      gst_structure_get_string (((GstSRTObject *) object)->parameters,
      "streamid");

  return g_strcmp0 (object_stream_id, (const gchar *) stream_id);
}

static GstSRTObject *
gst_srt_listener_connection_get_object (GstSRTListenerConnection
    * connection, char *stream_id)
{
  GList *item;

  g_rec_mutex_lock (&connections_lock);

  // If no connection key is set then this is a single element connection
  if (connection->key_is_set) {
    item = g_list_find_custom (connection->objects, stream_id,
        gst_srt_listener_connection_compare_func);

    if (item == NULL) {
      item =
          g_list_find_custom (connection->objects, NULL,
          gst_srt_listener_connection_compare_func);

      if (item != NULL) {
        GstSRTObject *object = (GstSRTObject *) item->data;
        g_object_set (G_OBJECT (object->element), "streamid", stream_id, NULL);
      }
    }
  } else {
    item = g_list_first (connection->objects);
  }

  g_rec_mutex_unlock (&connections_lock);

  if (item) {
    return item->data;
  }

  return NULL;
}

static GSocketAddress *
peeraddr_to_g_socket_address (const struct sockaddr *peeraddr)
{
  gsize peeraddr_len;

  switch (peeraddr->sa_family) {
    case AF_INET:
      peeraddr_len = sizeof (struct sockaddr_in);
      break;
    case AF_INET6:
      peeraddr_len = sizeof (struct sockaddr_in6);
      break;
    default:
      g_warning ("Unsupported address family %d", peeraddr->sa_family);
      return NULL;
  }
  return g_socket_address_new_from_native ((gpointer) peeraddr, peeraddr_len);
}

/**
 * parse_streamid:
 * @streamid: The streamid string from an SRT connection
 *
 * Parses a streamid to extract the username/identifier for connection matching.
 * Supports both simple streamids (used as-is) and structured streamids following
 * the SRT access control specification.
 *
 * Structured streamids use the format: #!::key1=value1,key2=value2,...
 * Common keys include:
 *   - u: Username/user identifier
 *   - r: Resource name 
 *   - h: Hostname
 *   - t: Stream type (stream, file, auth)
 *   - m: Mode (request, publish, bidirectional)
 *
 * For structured streamids, this function extracts the value of the "u" key
 * (standard username) or "streamid" key (compatibility). For simple streamids,
 * the entire string is returned as the username.
 *
 * Examples:
 *   - Simple: "mystream" → returns "mystream"
 *   - Structured: "#!::u=admin,r=stream1" → returns "admin"
 *   - Blackmagic: "#!::u=1288406c-...,bmd_uuid=..." → returns "1288406c-..."
 *
 * Returns: (transfer full): The extracted username/identifier,
 *          or empty string if parsing fails. Caller must free with g_free().
 */
static gchar *
parse_streamid (const char *streamid)
{
  if (!streamid) {
    return g_strdup ("");       /* Handle NULL streamid - return empty string for compatibility */
  }

  if (*streamid == '\0') {
    return g_strdup (streamid); /* Handle empty streamid - return as-is for compatibility */
  }

  /* Check for structured format starting with "#!::" */
  static const char stdhdr[] = "#!::";
  if (g_str_has_prefix (streamid, stdhdr)) {
    /* Parse structured streamid format */
    const char *content = streamid + sizeof (stdhdr) - 1;
    gchar **items = g_strsplit (content, ",", 0);

    for (gint i = 0; items[i]; i++) {
      gchar **kv = g_strsplit (items[i], "=", 2);
      if (kv[0] && kv[1] && (g_strcmp0 (g_strstrip (kv[0]), "u") == 0
              || g_strcmp0 (g_strstrip (kv[0]), "streamid") == 0)) {
        gchar *username = g_strdup (g_strstrip (kv[1]));
        g_strfreev (kv);
        g_strfreev (items);
        return username;
      }
      g_strfreev (kv);
    }
    g_strfreev (items);

    /* No username found in structured streamid - return empty string for compatibility */
    return g_strdup ("");
  }

  /* For simple streamid, return the whole thing as username */
  return g_strdup (streamid);
}

static gint
srt_listen_callback_func (GstSRTListenerConnection * connection, SRTSOCKET sock,
    int hs_version, const struct sockaddr *peeraddr, const char *stream_id)
{
  GSocketAddress *addr = NULL;
  GstSRTObject *object = NULL;
  gchar *parsed_streamid = NULL;

  addr = peeraddr_to_g_socket_address (peeraddr);
  if (!addr) {
    GST_WARNING ("Invalid peer address. Rejecting sink %d streamid: %s",
        sock, stream_id);
    return -1;
  }

  /* Parse the streamid to extract username for structured access_control formats */
  parsed_streamid = parse_streamid (stream_id);

  object = gst_srt_listener_connection_get_object (connection, parsed_streamid);
  if (!object) {
    GList *iter = NULL;
    GST_DEBUG ("Caller with parsed streamid: %s not part of connection: %s",
        parsed_streamid, connection->key);
    for (iter = connection->objects; iter; iter = iter->next) {
      object = iter->data;
      g_signal_emit_by_name (object->element, "caller-rejected", addr,
          stream_id);
    }
    g_free (parsed_streamid);
    g_object_unref (addr);
    return -1;
  }

  if (object->authentication) {
    gboolean authenticated = FALSE;

    /* notifying caller-connecting */
    g_signal_emit_by_name (object->element, "caller-connecting", addr,
        stream_id, &authenticated);

    if (!authenticated)
      goto reject_auth;
  }

  GST_INFO_OBJECT (object->element, "Accepting sink %d streamid: %s", sock,
      stream_id);
  g_free (parsed_streamid);
  g_object_unref (addr);
  return 0;
reject_auth:
  GST_WARNING_OBJECT (object->element,
      "Rejecting based on authentication, sink %d streamid: %s", sock,
      stream_id);

  /* notifying caller-rejected */
  g_signal_emit_by_name (object->element, "caller-rejected", addr, stream_id);
  g_free (parsed_streamid);
  g_object_unref (addr);
  return -1;
}

static gpointer
gst_srt_accept_thread_func (gpointer data)
{
  GstSRTListenerConnection *connection = data;

  while (connection->objects != NULL) {
    gint ret;
    SRTSOCKET rsock = SRT_INVALID_SOCK;
    gint rsocklen = 1;
    SYSSOCKET rsys, wsys;
    gint rsyslen = 1, wsyslen = 1;
    SRTSOCKET caller_sock;
    union
    {
      struct sockaddr_storage ss;
      struct sockaddr sa;
    } caller_sa = { 0, };
    int caller_sa_len = sizeof (caller_sa);
    GstSRTCaller *caller;
    gint flag = SRT_EPOLL_ERR;
    gint fd, fd_flags = SRT_EPOLL_ERR | SRT_EPOLL_IN;
    char caller_streamid[513];

    switch (srt_getsockstate (connection->sock)) {
      case SRTS_BROKEN:
      case SRTS_CLOSING:
      case SRTS_CLOSED:
      case SRTS_NONEXIST:
        SRT_CONNECTION_ELEMENT_ERROR (connection, RESOURCE, FAILED,
            (NULL), ("Socket is broken or closed"));
        break;

      default:
        break;
    }

    ret =
        srt_epoll_wait (connection->poll_id, &rsock, &rsocklen, NULL, 0,
        connection->poll_timeout, &rsys, &rsyslen, &wsys, &wsyslen);
    if (ret < 0) {
      gint srt_errno = srt_getlasterror (NULL);
      if (srt_errno != SRT_ETIMEOUT) {
        GST_WARNING ("Failed to poll socket: %s", srt_getlasterror_str ());
        break;
      }
    }

    if (rsock == SRT_INVALID_SOCK || rsocklen != 1) {
      continue;
    }

    connection->rsock = rsock;

    GST_DEBUG ("Waiting for accept, connection: %s", connection->key);
    caller_sock = srt_accept (connection->rsock, &caller_sa.sa, &caller_sa_len);
    if (caller_sock == SRT_INVALID_SOCK) {
      GST_DEBUG ("Failed to accept connection: %s", srt_getlasterror_str ());
      continue;
    }

    caller = gst_srt_caller_new (caller_sock, srt_epoll_create (),
        g_socket_address_new_from_native (&caller_sa.sa, caller_sa_len));

    int len = 512 + 1;
    srt_getsockopt (caller->sock, 0, SRTO_STREAMID, &caller_streamid, &len);

    /* Parse the streamid to extract username for structured formats */
    gchar *parsed_streamid = parse_streamid (caller_streamid);

    GstSRTObject *srtobject =
        gst_srt_listener_connection_get_object (connection, parsed_streamid);
    g_free (parsed_streamid);
    if (srtobject == NULL) {
      gst_srt_caller_close (caller);
      continue;
    }
    fd = g_cancellable_get_fd (srtobject->cancellable);
    if (fd >= 0)
      srt_epoll_add_ssock (srtobject->poll_id, fd, &fd_flags);

    if (GST_OBJECT_FLAG_IS_SET (srtobject->element, GST_ELEMENT_FLAG_SOURCE)) {
      flag |= SRT_EPOLL_IN;
    } else {
      flag |= SRT_EPOLL_OUT;
    }

    if (srt_epoll_add_usock (caller->poll_id, caller_sock, &flag) < 0) {
      GST_ELEMENT_WARNING (srtobject->element, LIBRARY, SETTINGS,
          (NULL), ("%s", srt_getlasterror_str ()));

      gst_srt_caller_close (caller);

      /* try-again */
      continue;
    }

    GST_INFO_OBJECT (srtobject->element,
        "Accepted to connect, socket: %d, streamid: %s, connection: %s",
        caller->sock, caller_streamid, connection->key);

    g_mutex_lock (&srtobject->sock_lock);
    srtobject->callers = g_list_prepend (srtobject->callers, caller);
    g_cond_signal (&srtobject->sock_cond);
    g_mutex_unlock (&srtobject->sock_lock);

    /* notifying caller-added */
    g_signal_emit_by_name (srtobject->element, "caller-added", 0,
        caller->sockaddr);
  }

  GST_DEBUG ("Accept thread for connection: %s exited", connection->key);

  return NULL;
}

static gboolean
gst_srt_listener_connection_accept_callers (GstSRTListenerConnection *
    connection)
{
  gboolean ret = TRUE;

  if (!connection->accept_thread) {
    connection->accept_thread =
        g_thread_try_new ("GstSRTObjectAccepter", gst_srt_accept_thread_func,
        connection, NULL);
    if (connection->accept_thread == NULL) {
      GST_ERROR ("Failed to start thread");
      ret = FALSE;
    }
  }

  return ret;
}


static gboolean
gst_srt_listener_connection_init (GstSRTListenerConnection * connection,
    GstSRTObject * srtobject, guint local_port, GError ** error)
{
  gint sock_flags = SRT_EPOLL_ERR | SRT_EPOLL_IN;
  GSocketAddress *bind_addr = NULL;
  gboolean poll_added = FALSE;
  struct sockaddr_storage bind_sa;
  SRTSOCKET sock = SRT_INVALID_SOCK;

  bind_addr =
      gst_srt_object_resolve (srtobject, GST_SRT_DEFAULT_LOCALADDRESS,
      local_port, error);
  if (!bind_addr) {
    goto failed;
  }

  if (!g_socket_address_to_native (bind_addr, &bind_sa, sizeof (bind_sa),
          error)) {
    goto failed;
  }

  g_clear_object (&bind_addr);

  sock = srt_create_socket ();
  if (sock == SRT_INVALID_SOCK) {
    g_set_error (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, "%s",
        srt_getlasterror_str ());
    goto failed;
  }

  if (!gst_srt_object_set_common_params (sock, srtobject, error)) {
    goto failed;
  }

  GST_DEBUG_OBJECT (srtobject->element, "Binding SRT connection to port: %d",
      local_port);
  if (srt_bind (sock, (struct sockaddr *) &bind_sa,
          sizeof (struct sockaddr)) == SRT_ERROR) {
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ_WRITE,
        "Cannot bind to %s:%d - %s", GST_SRT_DEFAULT_LOCALADDRESS, local_port,
        srt_getlasterror_str ());
    goto failed;
  }

  connection->sock = sock;

  if (srt_epoll_add_usock (connection->poll_id, sock, &sock_flags) < 0) {
    g_set_error (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_SETTINGS,
        "%s", srt_getlasterror_str ());
    goto failed;
  }
  poll_added = TRUE;

  /* Register the SRT listen callback */
  if (srt_listen_callback (connection->sock,
          (srt_listen_callback_fn *) srt_listen_callback_func, connection)) {
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_OPEN_READ_WRITE,
        "Failed to register SRT listen callback: %s", srt_getlasterror_str ());
    goto failed;
  }

  GST_DEBUG_OBJECT (srtobject->element, "Starting to listen on bind socket");
  if (srt_listen (sock, 1) == SRT_ERROR) {
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_OPEN_READ_WRITE, "Cannot listen on bind socket: %s",
        srt_getlasterror_str ());
    goto failed;
  }

  gst_srt_listener_connection_accept_callers (connection);
  connection->initialized = TRUE;

  return TRUE;

failed:
  if (poll_added) {
    srt_epoll_remove_usock (connection->poll_id, sock);
  }
  if (sock != SRT_INVALID_SOCK) {
    srt_close (sock);
  }
  g_clear_object (&bind_addr);
  srtobject->sock = SRT_INVALID_SOCK;
  return FALSE;
}


/**
 * gst_srt_listener_connection_add_object:
 * @srtobject: A #GstSRTObject
 * @error: A #GError to be set in case something goes wrong
 *
 * Add a #GstSRTObject to a listener connection. A new connection will be
 * created if one matching the `connection-key` property of the object does
 * not exist. If the `connection-key` property is not set the connection will
 * be identified by a UUID.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_srt_listener_connection_add_object (GstSRTObject * srtobject,
    GError ** error)
{
  GstSRTListenerConnection *connection = NULL;
  const char *stream_id =
      gst_structure_get_string (srtobject->parameters, "streamid");
  char *connection_key = NULL;
  gboolean key_is_set = FALSE;
  gboolean ret = TRUE;

  g_rec_mutex_lock (&connections_lock);
  GST_OBJECT_LOCK (srtobject->element);

  GHashTable *connections = gst_srt_connections_get_unlocked (TRUE);
  if (srtobject->connection_key != NULL) {
    connection_key = g_strdup (srtobject->connection_key);
    key_is_set = TRUE;
  } else {
    connection_key = g_uuid_string_random ();
    srtobject->connection_key = g_strdup (connection_key);
  }
  GST_OBJECT_UNLOCK (srtobject->element);

  GST_DEBUG_OBJECT (srtobject->element, "Looking for connection with key: %s",
      connection_key);

  connection = g_hash_table_lookup (connections, connection_key);
  if (connection == NULL) {
    connection = g_new0 (GstSRTListenerConnection, 1);
    connection->initialized = FALSE;
    connection->poll_id = srt_epoll_create ();
    connection->key = g_strdup (connection_key);
    connection->key_is_set = key_is_set;
    connection->poll_timeout = GST_SRT_DEFAULT_POLL_TIMEOUT;
    GST_DEBUG_OBJECT (srtobject->element, "Creating new connection: %s",
        connection->key);
    g_hash_table_insert (connections, g_strdup (connection_key), connection);
  } else {
    GST_INFO_OBJECT (srtobject->element, "Found existing connection: %s",
        connection->key);

    gboolean added = FALSE;
    if (connection->key_is_set) {
      if (stream_id != NULL) {
        added = g_list_find_custom (connection->objects, stream_id,
            gst_srt_listener_connection_compare_func) != NULL;
      } else {
        // stream id will be set upon accepting new callers
        added = FALSE;
      }
    } else {
      added = (connection->objects != NULL);
    }

    if (added) {
      GST_WARNING ("The streamid '%s' is already part of the connection",
          stream_id);
      goto out;
    }
  }

  connection->objects = g_list_append (connection->objects, srtobject);
  GST_INFO_OBJECT (srtobject->element,
      "Added object with streamid: %s to connection: %s",
      stream_id == NULL ? "<unset>" : stream_id, connection_key);

  if (!connection->initialized) {
    guint local_port = 0;

    gst_structure_get_uint (srtobject->parameters, "localport", &local_port);
    if (!gst_srt_listener_connection_init (connection, srtobject, local_port,
            error)) {
      ret = FALSE;
      goto out;
    }
  }

out:
  g_free (connection_key);
  g_rec_mutex_unlock (&connections_lock);

  return ret;
}

/**
 * gst_srt_listener_connection_remove_object:
 * @srtobject: A #GstSRTObject
 * @error: A #GError to be set in case something goes wrong
 *
 * Remove a #GstSRTObject from a listener connection.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_srt_listener_connection_remove_object (GstSRTObject * srtobject,
    GError ** error)
{
  gboolean ret = TRUE;
  GstSRTListenerConnection *connection = NULL;
  GList *item = NULL;
  const char *stream_id = gst_structure_get_string (srtobject->parameters,
      "streamid");

  if (srtobject->connection_key == NULL)
    return TRUE;

  g_rec_mutex_lock (&connections_lock);

  GHashTable *connections = gst_srt_connections_get_unlocked (FALSE);
  if (connections == NULL) {
    ret = FALSE;
    goto out;
  }

  connection = g_hash_table_lookup (connections, srtobject->connection_key);
  if (connection == NULL) {
    ret = FALSE;
    goto out;
  }
  item = g_list_find_custom (connection->objects, stream_id,
      gst_srt_listener_connection_compare_func);
  if (!item) {
    ret = FALSE;
    goto out;
  }

  connection->objects = g_list_remove (connection->objects, item->data);
  GST_DEBUG_OBJECT (srtobject->element,
      "Removed from connection %s, remaining objects in connection: %u",
      srtobject->connection_key, g_list_length (connection->objects));

  // If this was a single element connection then make sure we clean up the
  // generated UUID connection key.
  if (!connection->key_is_set) {
    g_free (srtobject->connection_key);
    srtobject->connection_key = NULL;
  }

  if (connection->objects == NULL) {
    gst_srt_listener_connection_remove_unlocked (connection);
    g_rec_mutex_unlock (&connections_lock);
    /* This needs to be run without the connections lock, to not cause
       internal libsrt issues when holding lock in the listener callback */
    gst_srt_listener_connection_destroy (connection);
    return ret;
  }

out:
  g_rec_mutex_unlock (&connections_lock);
  return ret;
}
