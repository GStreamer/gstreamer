/*
 * Copyright (c) 2015, Collabora Ltd.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sctpassociation.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define GST_SCTP_ASSOCIATION_STATE_TYPE (gst_sctp_association_state_get_type())
static GType
gst_sctp_association_state_get_type (void)
{
  static const GEnumValue values[] = {
    {GST_SCTP_ASSOCIATION_STATE_NEW, "state-new", "state-new"},
    {GST_SCTP_ASSOCIATION_STATE_READY, "state-ready", "state-ready"},
    {GST_SCTP_ASSOCIATION_STATE_CONNECTING, "state-connecting",
        "state-connecting"},
    {GST_SCTP_ASSOCIATION_STATE_CONNECTED, "state-connected",
        "state-connected"},
    {GST_SCTP_ASSOCIATION_STATE_DISCONNECTING, "state-disconnecting",
        "state-disconnecting"},
    {GST_SCTP_ASSOCIATION_STATE_DISCONNECTED, "state-disconnected",
        "state-disconnected"},
    {GST_SCTP_ASSOCIATION_STATE_ERROR, "state-error", "state-error"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;
    _id = g_enum_register_static ("GstSctpAssociationState", values);
    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

G_DEFINE_TYPE (GstSctpAssociation, gst_sctp_association, G_TYPE_OBJECT);

enum
{
  SIGNAL_STREAM_RESET,
  LAST_SIGNAL
};


enum
{
  PROP_0,

  PROP_ASSOCIATION_ID,
  PROP_LOCAL_PORT,
  PROP_REMOTE_PORT,
  PROP_STATE,
  PROP_USE_SOCK_STREAM,

  NUM_PROPERTIES
};

static guint signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_NUMBER_OF_SCTP_STREAMS 10
#define DEFAULT_LOCAL_SCTP_PORT 0
#define DEFAULT_REMOTE_SCTP_PORT 0

static GHashTable *associations = NULL;
G_LOCK_DEFINE_STATIC (associations_lock);
static guint32 number_of_associations = 0;

/* Interface implementations */
static void gst_sctp_association_finalize (GObject * object);
static void gst_sctp_association_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sctp_association_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static struct socket *create_sctp_socket (GstSctpAssociation *
    gst_sctp_association);
static struct sockaddr_conn get_sctp_socket_address (GstSctpAssociation *
    gst_sctp_association, guint16 port);
static gpointer connection_thread_func (GstSctpAssociation * self);
static gboolean client_role_connect (GstSctpAssociation * self);
static int sctp_packet_out (void *addr, void *buffer, size_t length, guint8 tos,
    guint8 set_df);
static int receive_cb (struct socket *sock, union sctp_sockstore addr,
    void *data, size_t datalen, struct sctp_rcvinfo rcv_info, gint flags,
    void *ulp_info);
static void handle_notification (GstSctpAssociation * self,
    const union sctp_notification *notification, size_t length);
static void handle_association_changed (GstSctpAssociation * self,
    const struct sctp_assoc_change *sac);
static void handle_stream_reset_event (GstSctpAssociation * self,
    const struct sctp_stream_reset_event *ssr);
static void handle_message (GstSctpAssociation * self, guint8 * data,
    guint32 datalen, guint16 stream_id, guint32 ppid);

static void maybe_set_state_to_ready (GstSctpAssociation * self);
static void gst_sctp_association_change_state (GstSctpAssociation * self,
    GstSctpAssociationState new_state, gboolean notify);

static void
gst_sctp_association_class_init (GstSctpAssociationClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_sctp_association_finalize;
  gobject_class->set_property = gst_sctp_association_set_property;
  gobject_class->get_property = gst_sctp_association_get_property;

  signals[SIGNAL_STREAM_RESET] =
      g_signal_new ("stream-reset", G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstSctpAssociationClass,
          on_sctp_stream_reset), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_UINT);

  properties[PROP_ASSOCIATION_ID] = g_param_spec_uint ("association-id",
      "The SCTP association-id", "The SCTP association-id.", 0, G_MAXUSHORT,
      DEFAULT_LOCAL_SCTP_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_LOCAL_PORT] = g_param_spec_uint ("local-port", "Local SCTP",
      "The local SCTP port for this association", 0, G_MAXUSHORT,
      DEFAULT_LOCAL_SCTP_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_REMOTE_PORT] =
      g_param_spec_uint ("remote-port", "Remote SCTP",
      "The remote SCTP port for this association", 0, G_MAXUSHORT,
      DEFAULT_LOCAL_SCTP_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_STATE] = g_param_spec_enum ("state", "SCTP Association state",
      "The state of the SCTP association", GST_SCTP_ASSOCIATION_STATE_TYPE,
      GST_SCTP_ASSOCIATION_STATE_NEW,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_USE_SOCK_STREAM] =
      g_param_spec_boolean ("use-sock-stream", "Use sock-stream",
      "When set to TRUE, a sequenced, reliable, connection-based connection is used."
      "When TRUE the partial reliability parameters of the channel is ignored.",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);
}

static void
gst_sctp_association_init (GstSctpAssociation * self)
{
  /* No need to lock mutex here as long as the function is only called from gst_sctp_association_get */
  if (number_of_associations == 0) {
    usrsctp_init (0, sctp_packet_out, g_print);

    /* Explicit Congestion Notification */
    usrsctp_sysctl_set_sctp_ecn_enable (0);

    usrsctp_sysctl_set_sctp_nr_outgoing_streams_default
        (DEFAULT_NUMBER_OF_SCTP_STREAMS);
  }
  number_of_associations++;

  self->local_port = DEFAULT_LOCAL_SCTP_PORT;
  self->remote_port = DEFAULT_REMOTE_SCTP_PORT;
  self->sctp_ass_sock = NULL;

  self->connection_thread = NULL;
  g_mutex_init (&self->association_mutex);

  self->state = GST_SCTP_ASSOCIATION_STATE_NEW;

  self->use_sock_stream = FALSE;

  usrsctp_register_address ((void *) self);
}

static void
gst_sctp_association_finalize (GObject * object)
{
  GstSctpAssociation *self = GST_SCTP_ASSOCIATION (object);

  G_LOCK (associations_lock);

  g_hash_table_remove (associations, GUINT_TO_POINTER (self->association_id));

  usrsctp_deregister_address ((void *) self);
  number_of_associations--;
  if (number_of_associations == 0) {
    usrsctp_finish ();
  }
  G_UNLOCK (associations_lock);

  if (self->connection_thread)
    g_thread_join (self->connection_thread);

  G_OBJECT_CLASS (gst_sctp_association_parent_class)->finalize (object);
}

static void
gst_sctp_association_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSctpAssociation *self = GST_SCTP_ASSOCIATION (object);

  g_mutex_lock (&self->association_mutex);
  if (self->state != GST_SCTP_ASSOCIATION_STATE_NEW) {
    switch (prop_id) {
      case PROP_LOCAL_PORT:
      case PROP_REMOTE_PORT:
        g_warning ("These properties cannot be set in this state");
        goto error;
    }
  }

  switch (prop_id) {
    case PROP_ASSOCIATION_ID:
      self->association_id = g_value_get_uint (value);
      break;
    case PROP_LOCAL_PORT:
      self->local_port = g_value_get_uint (value);
      break;
    case PROP_REMOTE_PORT:
      self->remote_port = g_value_get_uint (value);
      break;
    case PROP_STATE:
      self->state = g_value_get_enum (value);
      break;
    case PROP_USE_SOCK_STREAM:
      self->use_sock_stream = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&self->association_mutex);
  if (prop_id == PROP_LOCAL_PORT || prop_id == PROP_REMOTE_PORT)
    maybe_set_state_to_ready (self);

  return;

error:
  g_mutex_unlock (&self->association_mutex);
}

static void
maybe_set_state_to_ready (GstSctpAssociation * self)
{
  gboolean signal_ready_state = FALSE;

  g_mutex_lock (&self->association_mutex);
  if ((self->state == GST_SCTP_ASSOCIATION_STATE_NEW) &&
      (self->local_port != 0 && self->remote_port != 0)
      && (self->packet_out_cb != NULL) && (self->packet_received_cb != NULL)) {
    signal_ready_state = TRUE;
    gst_sctp_association_change_state (self, GST_SCTP_ASSOCIATION_STATE_READY,
        FALSE);
  }
  g_mutex_unlock (&self->association_mutex);

  /* The reason the state is changed twice is that we do not want to change state with
   * notification while the association_mutex is locked. If someone listens
   * on property change and call this object a deadlock might occur.*/
  if (signal_ready_state)
    gst_sctp_association_change_state (self, GST_SCTP_ASSOCIATION_STATE_READY,
        TRUE);

}

static void
gst_sctp_association_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSctpAssociation *self = GST_SCTP_ASSOCIATION (object);

  switch (prop_id) {
    case PROP_ASSOCIATION_ID:
      g_value_set_uint (value, self->association_id);
      break;
    case PROP_LOCAL_PORT:
      g_value_set_uint (value, self->local_port);
      break;
    case PROP_REMOTE_PORT:
      g_value_set_uint (value, self->remote_port);
      break;
    case PROP_STATE:
      g_value_set_enum (value, self->state);
      break;
    case PROP_USE_SOCK_STREAM:
      g_value_set_boolean (value, self->use_sock_stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

/* Public functions */

GstSctpAssociation *
gst_sctp_association_get (guint32 association_id)
{
  GstSctpAssociation *association;

  G_LOCK (associations_lock);
  if (!associations) {
    associations =
        g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);
  }

  association =
      g_hash_table_lookup (associations, GUINT_TO_POINTER (association_id));
  if (!association) {
    association =
        g_object_new (GST_SCTP_TYPE_ASSOCIATION, "association-id",
        association_id, NULL);
    g_hash_table_insert (associations, GUINT_TO_POINTER (association_id),
        association);
  } else {
    g_object_ref (association);
  }
  G_UNLOCK (associations_lock);
  return association;
}

gboolean
gst_sctp_association_start (GstSctpAssociation * self)
{
  gchar *thread_name;

  g_mutex_lock (&self->association_mutex);
  if (self->state != GST_SCTP_ASSOCIATION_STATE_READY) {
    g_warning ("SCTP association is in wrong state and cannot be started");
    goto configure_required;
  }

  if ((self->sctp_ass_sock = create_sctp_socket (self)) == NULL)
    goto error;

  gst_sctp_association_change_state (self,
      GST_SCTP_ASSOCIATION_STATE_CONNECTING, FALSE);
  g_mutex_unlock (&self->association_mutex);

  /* The reason the state is changed twice is that we do not want to change state with
   * notification while the association_mutex is locked. If someone listens
   * on property change and call this object a deadlock might occur.*/
  gst_sctp_association_change_state (self,
      GST_SCTP_ASSOCIATION_STATE_CONNECTING, TRUE);

  thread_name = g_strdup_printf ("connection_thread_%u", self->association_id);
  self->connection_thread = g_thread_new (thread_name,
      (GThreadFunc) connection_thread_func, self);
  g_free (thread_name);

  return TRUE;
error:
  g_mutex_unlock (&self->association_mutex);
  gst_sctp_association_change_state (self, GST_SCTP_ASSOCIATION_STATE_ERROR,
      TRUE);
configure_required:
  g_mutex_unlock (&self->association_mutex);
  return FALSE;
}

void
gst_sctp_association_set_on_packet_out (GstSctpAssociation * self,
    GstSctpAssociationPacketOutCb packet_out_cb, gpointer user_data)
{
  g_return_if_fail (GST_SCTP_IS_ASSOCIATION (self));

  g_mutex_lock (&self->association_mutex);
  if (self->state == GST_SCTP_ASSOCIATION_STATE_NEW) {
    self->packet_out_cb = packet_out_cb;
    self->packet_out_user_data = user_data;
  } else {
    /* This is to be thread safe. The Association might try to write to the closure already */
    g_warning ("It is not possible to change packet callback in this state");
  }
  g_mutex_unlock (&self->association_mutex);

  maybe_set_state_to_ready (self);
}

void
gst_sctp_association_set_on_packet_received (GstSctpAssociation * self,
    GstSctpAssociationPacketReceivedCb packet_received_cb, gpointer user_data)
{
  g_return_if_fail (GST_SCTP_IS_ASSOCIATION (self));

  g_mutex_lock (&self->association_mutex);
  if (self->state == GST_SCTP_ASSOCIATION_STATE_NEW) {
    self->packet_received_cb = packet_received_cb;
    self->packet_received_user_data = user_data;
  } else {
    /* This is to be thread safe. The Association might try to write to the closure already */
    g_warning ("It is not possible to change receive callback in this state");
  }
  g_mutex_unlock (&self->association_mutex);

  maybe_set_state_to_ready (self);
}

void
gst_sctp_association_incoming_packet (GstSctpAssociation * self, guint8 * buf,
    guint32 length)
{
  usrsctp_conninput ((void *) self, (const void *) buf, (size_t) length, 0);
}

gboolean
gst_sctp_association_send_data (GstSctpAssociation * self, guint8 * buf,
    guint32 length, guint16 stream_id, guint32 ppid, gboolean ordered,
    GstSctpAssociationPartialReliability pr, guint32 reliability_param)
{
  struct sctp_sendv_spa spa;
  gint32 bytes_sent;
  gboolean result = FALSE;
  struct sockaddr_conn remote_addr;

  g_mutex_lock (&self->association_mutex);
  if (self->state != GST_SCTP_ASSOCIATION_STATE_CONNECTED)
    goto end;

  memset (&spa, 0, sizeof (spa));

  spa.sendv_sndinfo.snd_ppid = g_htonl (ppid);
  spa.sendv_sndinfo.snd_sid = stream_id;
  spa.sendv_sndinfo.snd_flags = ordered ? 0 : SCTP_UNORDERED;
  spa.sendv_sndinfo.snd_context = 0;
  spa.sendv_sndinfo.snd_assoc_id = 0;
  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
  if (pr != GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_NONE) {
    spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
    spa.sendv_prinfo.pr_value = g_htonl (reliability_param);
    if (pr == GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_TTL)
      spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
    else if (pr == GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_RTX)
      spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
    else if (pr == GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_BUF)
      spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_BUF;
  }

  remote_addr = get_sctp_socket_address (self, self->remote_port);
  bytes_sent =
      usrsctp_sendv (self->sctp_ass_sock, buf, length,
      (struct sockaddr *) &remote_addr, 1, (void *) &spa,
      (socklen_t) sizeof (struct sctp_sendv_spa), SCTP_SENDV_SPA, 0);
  if (bytes_sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      /* Resending this buffer is taken care of by the gstsctpenc */
      goto end;
    } else {
      g_warning ("Error sending data on stream %u: (%u) %s", stream_id, errno,
          strerror (errno));
      goto end;
    }
  }

  result = TRUE;
end:
  g_mutex_unlock (&self->association_mutex);
  return result;
}


void
gst_sctp_association_reset_stream (GstSctpAssociation * self, guint16 stream_id)
{
  struct sctp_reset_streams *srs;
  socklen_t length;

  length = (socklen_t) (sizeof (struct sctp_reset_streams) + sizeof (guint16));
  srs = (struct sctp_reset_streams *) g_malloc0 (length);
  srs->srs_flags = SCTP_STREAM_RESET_OUTGOING;
  srs->srs_number_streams = 1;
  srs->srs_stream_list[0] = stream_id;

  g_mutex_lock (&self->association_mutex);
  usrsctp_setsockopt (self->sctp_ass_sock, IPPROTO_SCTP, SCTP_RESET_STREAMS,
      srs, length);
  g_mutex_unlock (&self->association_mutex);

  g_free (srs);
}

void
gst_sctp_association_force_close (GstSctpAssociation * self)
{
  g_mutex_lock (&self->association_mutex);
  if (self->sctp_ass_sock) {
    usrsctp_close (self->sctp_ass_sock);
    self->sctp_ass_sock = NULL;

  }
  g_mutex_unlock (&self->association_mutex);
}

static struct socket *
create_sctp_socket (GstSctpAssociation * self)
{
  struct socket *sock;
  struct linger l;
  struct sctp_event event;
  struct sctp_assoc_value stream_reset;
  int value = 1;
  guint16 event_types[] = {
    SCTP_ASSOC_CHANGE,
    SCTP_PEER_ADDR_CHANGE,
    SCTP_REMOTE_ERROR,
    SCTP_SEND_FAILED,
    SCTP_SHUTDOWN_EVENT,
    SCTP_ADAPTATION_INDICATION,
    /*SCTP_PARTIAL_DELIVERY_EVENT, */
    /*SCTP_AUTHENTICATION_EVENT, */
    SCTP_STREAM_RESET_EVENT,
    /*SCTP_SENDER_DRY_EVENT, */
    /*SCTP_NOTIFICATIONS_STOPPED_EVENT, */
    /*SCTP_ASSOC_RESET_EVENT, */
    SCTP_STREAM_CHANGE_EVENT
  };
  guint32 i;
  guint sock_type = self->use_sock_stream ? SOCK_STREAM : SOCK_SEQPACKET;

  if ((sock =
          usrsctp_socket (AF_CONN, sock_type, IPPROTO_SCTP, receive_cb, NULL, 0,
              (void *) self)) == NULL)
    goto error;

  if (usrsctp_set_non_blocking (sock, 1) < 0) {
    g_warning ("Could not set non-blocking mode on SCTP socket");
    goto error;
  }

  memset (&l, 0, sizeof (l));
  l.l_onoff = 1;
  l.l_linger = 0;
  if (usrsctp_setsockopt (sock, SOL_SOCKET, SO_LINGER, (const void *) &l,
          (socklen_t) sizeof (struct linger)) < 0) {
    g_warning ("Could not set SO_LINGER on SCTP socket");
    goto error;
  }

  if (usrsctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NODELAY, &value,
          sizeof (int))) {
    g_warning ("Could not set SCTP_NODELAY");
    goto error;
  }

  memset (&stream_reset, 0, sizeof (stream_reset));
  stream_reset.assoc_id = SCTP_ALL_ASSOC;
  stream_reset.assoc_value = 1;
  if (usrsctp_setsockopt (sock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET,
          &stream_reset, sizeof (stream_reset))) {
    g_warning ("Could not set SCTP_ENABLE_STREAM_RESET");
    goto error;
  }

  memset (&event, 0, sizeof (event));
  event.se_assoc_id = SCTP_ALL_ASSOC;
  event.se_on = 1;
  for (i = 0; i < sizeof (event_types) / sizeof (event_types[0]); i++) {
    event.se_type = event_types[i];
    if (usrsctp_setsockopt (sock, IPPROTO_SCTP, SCTP_EVENT,
            &event, sizeof (event)) < 0) {
      g_warning ("Failed to register event %u", event_types[i]);
    }
  }

  return sock;
error:
  if (sock) {
    usrsctp_close (sock);
    g_warning ("Could not create socket. Error: (%u) %s", errno,
        strerror (errno));
    errno = 0;
    sock = NULL;
  }
  return NULL;
}

static struct sockaddr_conn
get_sctp_socket_address (GstSctpAssociation * gst_sctp_association,
    guint16 port)
{
  struct sockaddr_conn addr;

  memset ((void *) &addr, 0, sizeof (struct sockaddr_conn));
#ifdef __APPLE__
  addr.sconn_len = sizeof (struct sockaddr_conn);
#endif
  addr.sconn_family = AF_CONN;
  addr.sconn_port = g_htons (port);
  addr.sconn_addr = (void *) gst_sctp_association;

  return addr;
}

static gpointer
connection_thread_func (GstSctpAssociation * self)
{
  /* TODO: Support both server and client role */
  client_role_connect (self);
  return NULL;
}

static gboolean
client_role_connect (GstSctpAssociation * self)
{
  struct sockaddr_conn addr;
  gint ret;

  g_mutex_lock (&self->association_mutex);
  addr = get_sctp_socket_address (self, self->local_port);
  ret =
      usrsctp_bind (self->sctp_ass_sock, (struct sockaddr *) &addr,
      sizeof (struct sockaddr_conn));
  if (ret < 0) {
    g_warning ("usrsctp_bind() error: (%u) %s", errno, strerror (errno));
    goto error;
  }

  addr = get_sctp_socket_address (self, self->remote_port);
  ret =
      usrsctp_connect (self->sctp_ass_sock, (struct sockaddr *) &addr,
      sizeof (struct sockaddr_conn));
  if (ret < 0 && errno != EINPROGRESS) {
    g_warning ("usrsctp_connect() error: (%u) %s", errno, strerror (errno));
    goto error;
  }
  g_mutex_unlock (&self->association_mutex);
  return TRUE;
error:
  g_mutex_unlock (&self->association_mutex);
  return FALSE;
}

static int
sctp_packet_out (void *addr, void *buffer, size_t length, guint8 tos,
    guint8 set_df)
{
  GstSctpAssociation *self = GST_SCTP_ASSOCIATION (addr);

  if (self->packet_out_cb) {
    self->packet_out_cb (self, buffer, length, self->packet_out_user_data);
  }

  return 0;
}

static int
receive_cb (struct socket *sock, union sctp_sockstore addr, void *data,
    size_t datalen, struct sctp_rcvinfo rcv_info, gint flags, void *ulp_info)
{
  GstSctpAssociation *self = GST_SCTP_ASSOCIATION (ulp_info);

  if (!data) {
    /* Not sure if this can happend. */
    g_warning ("Received empty data buffer");
  } else {
    if (flags & MSG_NOTIFICATION) {
      handle_notification (self, (const union sctp_notification *) data,
          datalen);
      free (data);
    } else {
      handle_message (self, data, datalen, rcv_info.rcv_sid,
          ntohl (rcv_info.rcv_ppid));
    }
  }

  return 1;
}

static void
handle_notification (GstSctpAssociation * self,
    const union sctp_notification *notification, size_t length)
{
  g_assert (notification->sn_header.sn_length == length);

  switch (notification->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_ASSOC_CHANGE");
      handle_association_changed (self, &notification->sn_assoc_change);
      break;
    case SCTP_PEER_ADDR_CHANGE:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_PEER_ADDR_CHANGE");
      break;
    case SCTP_REMOTE_ERROR:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_REMOTE_ERROR");
      break;
    case SCTP_SEND_FAILED:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_SEND_FAILED");
      break;
    case SCTP_SHUTDOWN_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_SHUTDOWN_EVENT");
      break;
    case SCTP_ADAPTATION_INDICATION:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
          "Event: SCTP_ADAPTATION_INDICATION");
      break;
    case SCTP_PARTIAL_DELIVERY_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
          "Event: SCTP_PARTIAL_DELIVERY_EVENT");
      break;
    case SCTP_AUTHENTICATION_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
          "Event: SCTP_AUTHENTICATION_EVENT");
      break;
    case SCTP_STREAM_RESET_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_STREAM_RESET_EVENT");
      handle_stream_reset_event (self, &notification->sn_strreset_event);
      break;
    case SCTP_SENDER_DRY_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_SENDER_DRY_EVENT");
      break;
    case SCTP_NOTIFICATIONS_STOPPED_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
          "Event: SCTP_NOTIFICATIONS_STOPPED_EVENT");
      break;
    case SCTP_ASSOC_RESET_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_ASSOC_RESET_EVENT");
      break;
    case SCTP_STREAM_CHANGE_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_STREAM_CHANGE_EVENT");
      break;
    case SCTP_SEND_FAILED_EVENT:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Event: SCTP_SEND_FAILED_EVENT");
      break;
    default:
      break;
  }
}

static void
handle_association_changed (GstSctpAssociation * self,
    const struct sctp_assoc_change *sac)
{
  gboolean change_state = FALSE;
  GstSctpAssociationState new_state;

  switch (sac->sac_state) {
    case SCTP_COMM_UP:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "SCTP_COMM_UP()");
      g_mutex_lock (&self->association_mutex);
      if (self->state == GST_SCTP_ASSOCIATION_STATE_CONNECTING) {
        change_state = TRUE;
        new_state = GST_SCTP_ASSOCIATION_STATE_CONNECTED;
        g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "SCTP association connected!");
      } else if (self->state == GST_SCTP_ASSOCIATION_STATE_CONNECTED) {
        g_warning ("SCTP association already open");
      } else {
        g_warning ("SCTP association in unexpected state");
      }
      g_mutex_unlock (&self->association_mutex);
      break;
    case SCTP_COMM_LOST:
      g_warning ("SCTP event SCTP_COMM_LOST received");
      /* TODO: Tear down association and signal that this has happend */
      break;
    case SCTP_RESTART:
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
          "SCTP event SCTP_RESTART received");
      break;
    case SCTP_SHUTDOWN_COMP:
      g_warning ("SCTP event SCTP_SHUTDOWN_COMP received");
      /* TODO: Tear down association and signal that this has happend */
      break;
    case SCTP_CANT_STR_ASSOC:
      g_warning ("SCTP event SCTP_CANT_STR_ASSOC received");
      break;
  }

  if (change_state)
    gst_sctp_association_change_state (self, new_state, TRUE);
}

static void
handle_stream_reset_event (GstSctpAssociation * self,
    const struct sctp_stream_reset_event *sr)
{
  guint32 i, n;
  if (!(sr->strreset_flags & SCTP_STREAM_RESET_DENIED) &&
      !(sr->strreset_flags & SCTP_STREAM_RESET_DENIED)) {
    n = (sr->strreset_length -
        sizeof (struct sctp_stream_reset_event)) / sizeof (uint16_t);
    for (i = 0; i < n; i++) {
      if (sr->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
        g_signal_emit (self, signals[SIGNAL_STREAM_RESET], 0,
            sr->strreset_stream_list[i]);
      }
    }
  }
}

static void
handle_message (GstSctpAssociation * self, guint8 * data, guint32 datalen,
    guint16 stream_id, guint32 ppid)
{
  if (self->packet_received_cb) {
    self->packet_received_cb (self, data, datalen, stream_id, ppid,
        self->packet_received_user_data);
  }
}

static void
gst_sctp_association_change_state (GstSctpAssociation * self,
    GstSctpAssociationState new_state, gboolean notify)
{
  self->state = new_state;
  if (notify)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}
