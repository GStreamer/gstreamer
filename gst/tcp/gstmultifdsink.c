/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst-i18n-plugin.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gstmultifdsink.h"
#include "gsttcp-marshal.h"

#define NOT_IMPLEMENTED 0

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unblock or restart the select call */
#define CONTROL_RESTART		'R'     /* restart the select call */
#define CONTROL_STOP		'S'     /* stop the select call */
#define CONTROL_SOCKETS(sink)	sink->control_sock
#define WRITE_SOCKET(sink)	sink->control_sock[1]
#define READ_SOCKET(sink)	sink->control_sock[0]

#define SEND_COMMAND(sink, command)		\
G_STMT_START {					\
  unsigned char c; c = command;			\
  write (WRITE_SOCKET(sink).fd, &c, 1);		\
} G_STMT_END

#define READ_COMMAND(sink, command, res)	\
G_STMT_START {					\
  res = read(READ_SOCKET(sink).fd, &command, 1);	\
} G_STMT_END

/* elementfactory information */
static GstElementDetails gst_multifdsink_details =
GST_ELEMENT_DETAILS ("MultiFd sink",
    "Sink/Network",
    "Send data to multiple filedescriptors",
    "Thomas Vander Stichele <thomas at apestaart dot org>, "
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY (multifdsink_debug);
#define GST_CAT_DEFAULT (multifdsink_debug)

/* MultiFdSink signals and args */
enum
{
  /* methods */
  SIGNAL_ADD,
  SIGNAL_REMOVE,
  SIGNAL_CLEAR,
  SIGNAL_GET_STATS,

  /* signals */
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,

  LAST_SIGNAL
};

/* this is really arbitrarily chosen */
#define DEFAULT_PROTOCOL		 GST_TCP_PROTOCOL_TYPE_NONE
#define DEFAULT_MODE			 GST_FDSET_MODE_POLL
#define DEFAULT_BUFFERS_MAX		-1
#define DEFAULT_BUFFERS_SOFT_MAX	-1
#define DEFAULT_UNIT_TYPE		GST_UNIT_TYPE_BUFFERS
#define DEFAULT_UNITS_MAX		-1
#define DEFAULT_UNITS_SOFT_MAX		-1
#define DEFAULT_RECOVER_POLICY		 GST_RECOVER_POLICY_NONE
#define DEFAULT_TIMEOUT			 0
#define DEFAULT_SYNC_METHOD		 GST_SYNC_METHOD_NONE

enum
{
  ARG_0,
  ARG_PROTOCOL,
  ARG_MODE,
  ARG_BUFFERS_QUEUED,
  ARG_BYTES_QUEUED,
  ARG_TIME_QUEUED,

  ARG_UNIT_TYPE,
  ARG_UNITS_MAX,
  ARG_UNITS_SOFT_MAX,

  ARG_BUFFERS_MAX,
  ARG_BUFFERS_SOFT_MAX,

  ARG_RECOVER_POLICY,
  ARG_TIMEOUT,
  ARG_SYNC_CLIENTS,             /* deprecated */
  ARG_SYNC_METHOD,
  ARG_BYTES_TO_SERVE,
  ARG_BYTES_SERVED,
};

#define GST_TYPE_RECOVER_POLICY (gst_recover_policy_get_type())
static GType
gst_recover_policy_get_type (void)
{
  static GType recover_policy_type = 0;
  static GEnumValue recover_policy[] = {
    {GST_RECOVER_POLICY_NONE, "GST_RECOVER_POLICY_NONE",
        "Do not try to recover"},
    {GST_RECOVER_POLICY_RESYNC_START, "GST_RECOVER_POLICY_RESYNC_START",
        "Resync client to most recent buffer"},
    {GST_RECOVER_POLICY_RESYNC_SOFT, "GST_RECOVER_POLICY_RESYNC_SOFT",
        "Resync client to soft limit"},
    {GST_RECOVER_POLICY_RESYNC_KEYFRAME, "GST_RECOVER_POLICY_RESYNC_KEYFRAME",
        "Resync client to most recent keyframe"},
    {0, NULL, NULL},
  };

  if (!recover_policy_type) {
    recover_policy_type =
        g_enum_register_static ("GstTCPRecoverPolicy", recover_policy);
  }
  return recover_policy_type;
}

#define GST_TYPE_SYNC_METHOD (gst_sync_method_get_type())
static GType
gst_sync_method_get_type (void)
{
  static GType sync_method_type = 0;
  static GEnumValue sync_method[] = {
    {GST_SYNC_METHOD_NONE, "GST_SYNC_METHOD_NONE",
        "Serve new client the latest buffer"},
    {GST_SYNC_METHOD_WAIT, "GST_SYNC_METHOD_WAIT",
        "Make the new client wait for the next keyframe"},
    {GST_SYNC_METHOD_BURST, "GST_SYNC_METHOD_BURST",
        "Serve the new client the last keyframe, aka burst"},
    {0, NULL, NULL},
  };

  if (!sync_method_type) {
    sync_method_type = g_enum_register_static ("GstTCPSyncMethod", sync_method);
  }
  return sync_method_type;
}

#if NOT_IMPLEMENTED
#define GST_TYPE_UNIT_TYPE (gst_unit_type_get_type())
static GType
gst_unit_type_get_type (void)
{
  static GType unit_type_type = 0;
  static GEnumValue unit_type[] = {
    {GST_UNIT_TYPE_BUFFERS, "GST_UNIT_TYPE_BUFFERS", "Buffers"},
    {GST_UNIT_TYPE_BYTES, "GST_UNIT_TYPE_BYTES", "Bytes"},
    {GST_UNIT_TYPE_TIME, "GST_UNIT_TYPE_TIME", "Time"},
    {0, NULL, NULL},
  };

  if (!unit_type_type) {
    unit_type_type = g_enum_register_static ("GstTCPUnitType", unit_type);
  }
  return unit_type_type;
}
#endif

#define GST_TYPE_CLIENT_STATUS (gst_client_status_get_type())
static GType
gst_client_status_get_type (void)
{
  static GType client_status_type = 0;
  static GEnumValue client_status[] = {
    {GST_CLIENT_STATUS_OK, "GST_CLIENT_STATUS_OK", "OK"},
    {GST_CLIENT_STATUS_CLOSED, "GST_CLIENT_STATUS_CLOSED", "Closed"},
    {GST_CLIENT_STATUS_REMOVED, "GST_CLIENT_STATUS_REMOVED", "Removed"},
    {GST_CLIENT_STATUS_SLOW, "GST_CLIENT_STATUS_SLOW", "Too slow"},
    {GST_CLIENT_STATUS_ERROR, "GST_CLIENT_STATUS_ERROR", "Error"},
    {GST_CLIENT_STATUS_DUPLICATE, "GST_CLIENT_STATUS_DUPLICATE", "Duplicate"},
    {0, NULL, NULL},
  };

  if (!client_status_type) {
    client_status_type =
        g_enum_register_static ("GstTCPClientStatus", client_status);
  }
  return client_status_type;
}

static void gst_multifdsink_base_init (gpointer g_class);
static void gst_multifdsink_class_init (GstMultiFdSinkClass * klass);
static void gst_multifdsink_init (GstMultiFdSink * multifdsink);

static void gst_multifdsink_remove_client_link (GstMultiFdSink * sink,
    GList * link);

static GstFlowReturn gst_multifdsink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static GstElementStateReturn gst_multifdsink_change_state (GstElement *
    element);

static void gst_multifdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multifdsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstElementClass *parent_class = NULL;

static guint gst_multifdsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_multifdsink_get_type (void)
{
  static GType multifdsink_type = 0;


  if (!multifdsink_type) {
    static const GTypeInfo multifdsink_info = {
      sizeof (GstMultiFdSinkClass),
      gst_multifdsink_base_init,
      NULL,
      (GClassInitFunc) gst_multifdsink_class_init,
      NULL,
      NULL,
      sizeof (GstMultiFdSink),
      0,
      (GInstanceInitFunc) gst_multifdsink_init,
      NULL
    };

    multifdsink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstMultiFdSink",
        &multifdsink_info, 0);
  }
  return multifdsink_type;
}

static void
gst_multifdsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &gst_multifdsink_details);
}

static void
gst_multifdsink_class_init (GstMultiFdSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SINK);

  gobject_class->set_property = gst_multifdsink_set_property;
  gobject_class->get_property = gst_multifdsink_get_property;

  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL_TYPE, DEFAULT_PROTOCOL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The mode for selecting activity on the fds", GST_TYPE_FDSET_MODE,
          DEFAULT_MODE, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS_MAX,
      g_param_spec_int ("buffers-max", "Buffers max",
          "max number of buffers to queue (-1 = no limit)", -1, G_MAXINT,
          DEFAULT_BUFFERS_MAX, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS_SOFT_MAX,
      g_param_spec_int ("buffers-soft-max", "Buffers soft max",
          "Recover client when going over this limit (-1 = no limit)", -1,
          G_MAXINT, DEFAULT_BUFFERS_SOFT_MAX, G_PARAM_READWRITE));

#if NOT_IMPLEMENTED
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UNIT_TYPE,
      g_param_spec_enum ("unit-type", "Units type",
          "The unit to measure the max/soft-max/queued properties",
          GST_TYPE_UNIT_TYPE, DEFAULT_UNIT_TYPE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UNITS_MAX,
      g_param_spec_int ("units-max", "Units max",
          "max number of units to queue (-1 = no limit)", -1, G_MAXINT,
          DEFAULT_UNITS_MAX, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UNITS_SOFT_MAX,
      g_param_spec_int ("units-soft-max", "Units soft max",
          "Recover client when going over this limit (-1 = no limit)", -1,
          G_MAXINT, DEFAULT_UNITS_SOFT_MAX, G_PARAM_READWRITE));
#endif

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS_QUEUED,
      g_param_spec_uint ("buffers-queued", "Buffers queued",
          "Number of buffers currently queued", 0, G_MAXUINT, 0,
          G_PARAM_READABLE));
#if NOT_IMPLEMENTED
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTES_QUEUED,
      g_param_spec_uint ("bytes-queued", "Bytes queued",
          "Number of bytes currently queued", 0, G_MAXUINT, 0,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIME_QUEUED,
      g_param_spec_uint64 ("time-queued", "Time queued",
          "Number of time currently queued", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE));
#endif

  g_object_class_install_property (gobject_class, ARG_RECOVER_POLICY,
      g_param_spec_enum ("recover-policy", "Recover Policy",
          "How to recover when client reaches the soft max",
          GST_TYPE_RECOVER_POLICY, DEFAULT_RECOVER_POLICY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Maximum inactivity timeout in nanoseconds for a client (0 = no limit)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC_CLIENTS,
      g_param_spec_boolean ("sync-clients", "Sync clients",
          "(DEPRECATED) Sync clients to a keyframe",
          DEFAULT_SYNC_METHOD == GST_SYNC_METHOD_WAIT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC_METHOD,
      g_param_spec_enum ("sync-method", "Sync Method",
          "How to sync new clients to the stream",
          GST_TYPE_SYNC_METHOD, DEFAULT_SYNC_METHOD, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTES_TO_SERVE,
      g_param_spec_uint64 ("bytes-to-serve", "Bytes to serve",
          "Number of bytes received to serve to clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTES_SERVED,
      g_param_spec_uint64 ("bytes-served", "Bytes served",
          "Total number of bytes send to all clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE));

  gst_multifdsink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, add),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  gst_multifdsink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, remove),
      NULL, NULL, gst_tcp_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  gst_multifdsink_signals[SIGNAL_CLEAR] =
      g_signal_new ("clear", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, clear),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_multifdsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, get_stats),
      NULL, NULL, gst_tcp_marshal_BOXED__INT, G_TYPE_VALUE_ARRAY, 1,
      G_TYPE_INT);

  gst_multifdsink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiFdSinkClass, client_added),
      NULL, NULL, gst_tcp_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  gst_multifdsink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__INT_BOXED,
      G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CLIENT_STATUS);

  gstelement_class->change_state = gst_multifdsink_change_state;

  gstbasesink_class->render = gst_multifdsink_render;

  klass->add = gst_multifdsink_add;
  klass->remove = gst_multifdsink_remove;
  klass->clear = gst_multifdsink_clear;
  klass->get_stats = gst_multifdsink_get_stats;

  GST_DEBUG_CATEGORY_INIT (multifdsink_debug, "multifdsink", 0, "FD sink");
}

static void
gst_multifdsink_init (GstMultiFdSink * this)
{
  GST_FLAG_UNSET (this, GST_MULTIFDSINK_OPEN);

  this->protocol = DEFAULT_PROTOCOL;
  this->mode = DEFAULT_MODE;

  CLIENTS_LOCK_INIT (this);
  this->clients = NULL;
  this->fd_hash = g_hash_table_new (g_int_hash, g_int_equal);

  this->bufqueue = g_array_new (FALSE, TRUE, sizeof (GstBuffer *));
  this->unit_type = DEFAULT_UNIT_TYPE;
  this->units_max = DEFAULT_UNITS_MAX;
  this->units_soft_max = DEFAULT_UNITS_SOFT_MAX;
  this->recover_policy = DEFAULT_RECOVER_POLICY;

  this->timeout = DEFAULT_TIMEOUT;
  this->sync_method = DEFAULT_SYNC_METHOD;
}

void
gst_multifdsink_add (GstMultiFdSink * sink, int fd)
{
  GstTCPClient *client;
  GList *clink;
  GTimeVal now;
  gint flags, res;
  struct stat statbuf;

  GST_DEBUG_OBJECT (sink, "[fd %5d] adding client", fd);

  /* create client datastructure */
  client = g_new0 (GstTCPClient, 1);
  client->fd.fd = fd;
  client->status = GST_CLIENT_STATUS_OK;
  client->bufpos = -1;
  client->bufoffset = 0;
  client->sending = NULL;
  client->bytes_sent = 0;
  client->dropped_buffers = 0;
  client->avg_queue_size = 0;
  client->new_connection = TRUE;

  /* update start time */
  g_get_current_time (&now);
  client->connect_time = GST_TIMEVAL_TO_TIME (now);
  client->disconnect_time = 0;
  /* send last activity time to connect time */
  client->last_activity_time = GST_TIMEVAL_TO_TIME (now);

  CLIENTS_LOCK (sink);

  /* check the hash to find a duplicate fd */
  clink = g_hash_table_lookup (sink->fd_hash, &client->fd.fd);
  if (clink != NULL) {
    client->status = GST_CLIENT_STATUS_DUPLICATE;
    CLIENTS_UNLOCK (sink);
    GST_WARNING_OBJECT (sink, "[fd %5d] duplicate client found, refusing", fd);
    g_signal_emit (G_OBJECT (sink),
        gst_multifdsink_signals[SIGNAL_CLIENT_REMOVED], 0, fd, client->status);
    g_free (client);
    return;
  }

  /* we can add the fd now */
  clink = sink->clients = g_list_prepend (sink->clients, client);
  g_hash_table_insert (sink->fd_hash, &client->fd.fd, clink);

  /* set the socket to non blocking */
  res = fcntl (fd, F_SETFL, O_NONBLOCK);
  /* we always read from a client */
  gst_fdset_add_fd (sink->fdset, &client->fd);

  /* we don't try to read from write only fds */
  flags = fcntl (fd, F_GETFL, 0);
  if ((flags & O_ACCMODE) != O_WRONLY) {
    gst_fdset_fd_ctl_read (sink->fdset, &client->fd, TRUE);
  }
  /* figure out the mode, can't use send() for non sockets */
  res = fstat (fd, &statbuf);
  if (S_ISSOCK (statbuf.st_mode)) {
    client->is_socket = TRUE;
  }

  SEND_COMMAND (sink, CONTROL_RESTART);

  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multifdsink_signals[SIGNAL_CLIENT_ADDED], 0, fd);
}

void
gst_multifdsink_remove (GstMultiFdSink * sink, int fd)
{
  GList *clink;

  GST_DEBUG_OBJECT (sink, "[fd %5d] removing client", fd);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->fd_hash, &fd);
  if (clink != NULL) {
    GstTCPClient *client = (GstTCPClient *) clink->data;

    client->status = GST_CLIENT_STATUS_REMOVED;
    gst_multifdsink_remove_client_link (sink, clink);
    SEND_COMMAND (sink, CONTROL_RESTART);
  } else {
    GST_WARNING_OBJECT (sink, "[fd %5d] no client with this fd found!", fd);
  }
  CLIENTS_UNLOCK (sink);
}

void
gst_multifdsink_clear (GstMultiFdSink * sink)
{
  GList *clients, *next;

  GST_DEBUG_OBJECT (sink, "clearing all clients");

  CLIENTS_LOCK (sink);
  for (clients = sink->clients; clients; clients = next) {
    GstTCPClient *client;

    client = (GstTCPClient *) clients->data;
    next = g_list_next (clients);

    client->status = GST_CLIENT_STATUS_REMOVED;
    gst_multifdsink_remove_client_link (sink, clients);
  }
  SEND_COMMAND (sink, CONTROL_RESTART);
  CLIENTS_UNLOCK (sink);
}

GValueArray *
gst_multifdsink_get_stats (GstMultiFdSink * sink, int fd)
{
  GstTCPClient *client;
  GValueArray *result = NULL;
  GList *clink;

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->fd_hash, &fd);
  client = (GstTCPClient *) clink->data;
  if (client != NULL) {
    GValue value = { 0 };
    guint64 interval;

    result = g_value_array_new (4);

    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, client->bytes_sent);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, client->connect_time);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    if (client->disconnect_time == 0) {
      GTimeVal nowtv;

      g_get_current_time (&nowtv);

      interval = GST_TIMEVAL_TO_TIME (nowtv) - client->connect_time;
    } else {
      interval = client->disconnect_time - client->connect_time;
    }
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, client->disconnect_time);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, interval);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, client->last_activity_time);
    result = g_value_array_append (result, &value);
  }
  CLIENTS_UNLOCK (sink);

  /* python doesn't like a NULL pointer yet */
  if (result == NULL) {
    GST_WARNING_OBJECT (sink, "[fd %5d] no client with this found!", fd);
    result = g_value_array_new (0);
  }

  return result;
}

/* should be called with the clientslock held.
 * Note that we don't close the fd as we didn't open it in the first
 * place. An application should connect to the client-removed signal and
 * close the fd itself.
 */
static void
gst_multifdsink_remove_client_link (GstMultiFdSink * sink, GList * link)
{
  int fd;
  GTimeVal now;
  GstTCPClient *client = (GstTCPClient *) link->data;
  GstMultiFdSinkClass *fclass;

  fclass = GST_MULTIFDSINK_GET_CLASS (sink);

  fd = client->fd.fd;

  /* FIXME: if we keep track of ip we can log it here and signal */
  switch (client->status) {
    case GST_CLIENT_STATUS_OK:
      GST_WARNING_OBJECT (sink, "[fd %5d] removing client %p for no reason",
          fd, client);
      break;
    case GST_CLIENT_STATUS_CLOSED:
      GST_DEBUG_OBJECT (sink, "[fd %5d] removing client %p because of close",
          fd, client);
      break;
    case GST_CLIENT_STATUS_REMOVED:
      GST_DEBUG_OBJECT (sink,
          "[fd %5d] removing client %p because the app removed it", fd, client);
      break;
    case GST_CLIENT_STATUS_SLOW:
      GST_INFO_OBJECT (sink,
          "[fd %5d] removing client %p because it was too slow", fd, client);
      break;
    case GST_CLIENT_STATUS_ERROR:
      GST_WARNING_OBJECT (sink,
          "[fd %5d] removing client %p because of error", fd, client);
      break;
    default:
      GST_WARNING_OBJECT (sink,
          "[fd %5d] removing client %p with invalid reason", fd, client);
      break;
  }

  gst_fdset_remove_fd (sink->fdset, &client->fd);

  g_get_current_time (&now);
  client->disconnect_time = GST_TIMEVAL_TO_TIME (now);

  /* free client buffers */
  g_slist_foreach (client->sending, (GFunc) gst_mini_object_unref, NULL);
  g_slist_free (client->sending);
  client->sending = NULL;

  /* unlock the mutex before signaling because the signal handler
   * might query some properties */
  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multifdsink_signals[SIGNAL_CLIENT_REMOVED], 0, fd, client->status);

  /* lock again before we remove the client completely */
  CLIENTS_LOCK (sink);

  if (!g_hash_table_remove (sink->fd_hash, &client->fd.fd)) {
    GST_WARNING_OBJECT (sink,
        "[fd %5d] error removing client %p from hash", client->fd.fd, client);
  }
  /* after releasing the lock above, the link could be invalid, more
   * precisely, the next and prev pointers could point to invalid list
   * links. One optimisation could be to add a cookie to the linked list
   * and take a shortcut when it did not change between unlocking and locking
   * our mutex. For now we just walk the list again. */
  sink->clients = g_list_remove (sink->clients, client);

  if (fclass->removed)
    fclass->removed (sink, client->fd.fd);

  g_free (client);
}

/* handle a read on a client fd,
 * which either indicates a close or should be ignored
 * returns FALSE if some error occured or the client closed. */
static gboolean
gst_multifdsink_handle_client_read (GstMultiFdSink * sink,
    GstTCPClient * client)
{
  int avail, fd;
  gboolean ret;

  fd = client->fd.fd;

  if (ioctl (fd, FIONREAD, &avail) < 0) {
    GST_WARNING_OBJECT (sink, "[fd %5d] ioctl failed: %s (%d)",
        fd, g_strerror (errno), errno);
    client->status = GST_CLIENT_STATUS_ERROR;
    ret = FALSE;
    return ret;
  }

  GST_DEBUG_OBJECT (sink, "[fd %5d] select reports client read of %d bytes",
      fd, avail);

  ret = TRUE;

  if (avail == 0) {
    /* client sent close, so remove it */
    GST_DEBUG_OBJECT (sink, "[fd %5d] client asked for close, removing", fd);
    client->status = GST_CLIENT_STATUS_CLOSED;
    ret = FALSE;
  } else if (avail < 0) {
    GST_WARNING_OBJECT (sink, "[fd %5d] avail < 0, removing", fd);
    client->status = GST_CLIENT_STATUS_ERROR;
    ret = FALSE;
  } else {
    guint8 dummy[512];
    gint nread;

    /* just Read 'n' Drop, could also just drop the client as it's not supposed
     * to write to us except for closing the socket, I guess it's because we
     * like to listen to our customers. */
    do {
      /* this is the maximum we can read */
      gint to_read = MIN (avail, 512);

      GST_DEBUG_OBJECT (sink, "[fd %5d] client wants us to read %d bytes",
          fd, to_read);

      nread = read (fd, dummy, to_read);
      if (nread < -1) {
        GST_WARNING_OBJECT (sink, "[fd %5d] could not read %d bytes: %s (%d)",
            fd, to_read, g_strerror (errno), errno);
        client->status = GST_CLIENT_STATUS_ERROR;
        ret = FALSE;
        break;
      } else if (nread == 0) {
        GST_WARNING_OBJECT (sink, "[fd %5d] 0 bytes in read, removing", fd);
        client->status = GST_CLIENT_STATUS_ERROR;
        ret = FALSE;
        break;
      }
      avail -= nread;
    }
    while (avail > 0);
  }
  return ret;
}

static gboolean
gst_multifdsink_client_queue_data (GstMultiFdSink * sink, GstTCPClient * client,
    gchar * data, gint len)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) data;
  GST_BUFFER_SIZE (buf) = len;

  GST_LOG_OBJECT (sink, "[fd %5d] queueing data of length %d",
      client->fd.fd, len);

  client->sending = g_slist_append (client->sending, buf);

  return TRUE;
}

static gboolean
gst_multifdsink_client_queue_caps (GstMultiFdSink * sink, GstTCPClient * client,
    const GstCaps * caps)
{
  guint8 *header;
  guint8 *payload;
  guint length;
  gchar *string;

  string = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (sink, "[fd %5d] Queueing caps %s through GDP",
      client->fd.fd, string);
  g_free (string);

  if (!gst_dp_packet_from_caps (caps, 0, &length, &header, &payload)) {
    GST_DEBUG_OBJECT (sink, "Could not create GDP packet from caps");
    return FALSE;
  }
  gst_multifdsink_client_queue_data (sink, client, (gchar *) header, length);

  length = gst_dp_header_payload_length (header);
  gst_multifdsink_client_queue_data (sink, client, (gchar *) payload, length);

  return TRUE;
}

static gboolean
is_sync_frame (GstMultiFdSink * sink, GstBuffer * buffer)
{
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
    return FALSE;
  } else if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_IN_CAPS)) {
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_multifdsink_client_queue_buffer (GstMultiFdSink * sink,
    GstTCPClient * client, GstBuffer * buffer)
{
  if (sink->protocol == GST_TCP_PROTOCOL_TYPE_GDP) {
    guint8 *header;
    guint len;

    if (!gst_dp_header_from_buffer (buffer, 0, &len, &header)) {
      GST_DEBUG_OBJECT (sink,
          "[fd %5d] could not create header, removing client", client->fd.fd);
      return FALSE;
    }
    gst_multifdsink_client_queue_data (sink, client, (gchar *) header, len);
  }

  GST_LOG_OBJECT (sink, "[fd %5d] queueing buffer of length %d",
      client->fd.fd, GST_BUFFER_SIZE (buffer));

  gst_buffer_ref (buffer);
  client->sending = g_slist_append (client->sending, buffer);

  return TRUE;
}

static gint
gst_multifdsink_new_client (GstMultiFdSink * sink, GstTCPClient * client)
{
  gint result;

  switch (sink->sync_method) {
    case GST_SYNC_METHOD_WAIT:
    {
      /* if the buffer at the head of the queue is a sync point we can proceed,
       * else we need to skip the buffer and wait for a new one */
      GST_LOG_OBJECT (sink,
          "[fd %5d] new client, bufpos %d, waiting for keyframe", client->fd.fd,
          client->bufpos);

      /* the client is not yet alligned to a buffer */
      if (client->bufpos < 0) {
        result = -1;
      } else {
        GstBuffer *buf;
        gint i;

        for (i = client->bufpos; i >= 0; i--) {
          /* get the buffer for the client */
          buf = g_array_index (sink->bufqueue, GstBuffer *, i);
          if (is_sync_frame (sink, buf)) {
            GST_LOG_OBJECT (sink, "[fd %5d] new client, found sync",
                client->fd.fd);
            result = i;
            goto done;
          } else {
            /* client is not on a buffer, need to skip this buffer and
             * wait some more */
            GST_LOG_OBJECT (sink, "[fd %5d] new client, skipping buffer",
                client->fd.fd);
            client->bufpos--;
          }
        }
        result = -1;
      }
      break;
    }
    case GST_SYNC_METHOD_BURST:
    {
      /* FIXME for new clients we constantly scan the complete
       * buffer queue for sync point whenever a buffer is added. This is
       * suboptimal because if we cannot find a sync point the first time,
       * the algorithm should behave as GST_SYNC_METHOD_WAIT */
      gint i, len;

      GST_LOG_OBJECT (sink, "[fd %5d] new client, bufpos %d, bursting keyframe",
          client->fd.fd, client->bufpos);

      /* take length of queued buffers */
      len = sink->bufqueue->len;
      /* assume we don't find a keyframe */
      result = -1;
      /* then loop over all buffers to find the first keyframe */
      for (i = 0; i < len; i++) {
        GstBuffer *buf;

        buf = g_array_index (sink->bufqueue, GstBuffer *, i);
        if (is_sync_frame (sink, buf)) {
          /* found a keyframe, return its position */
          GST_LOG_OBJECT (sink, "found keyframe at %d", i);
          result = i;
          goto done;
        }
      }
      GST_LOG_OBJECT (sink, "no keyframe found");
      /* throw client to the waiting state */
      client->bufpos = -1;
      break;
    }
    default:
      /* no syncing, we are happy with whatever the client is going to get */
      GST_LOG_OBJECT (sink, "no client syn needed");
      result = client->bufpos;
      break;
  }
done:
  return result;
}

/* handle a write on a client,
 * which indicates a read request from a client.
 *
 * The strategy is as follows, for each client we maintain a queue of GstBuffers
 * that contain the raw bytes we need to send to the client. In the case of the
 * GDP protocol, we create buffers out of the header bytes so that we can only
 * focus on sending buffers.
 *
 * We first check to see if we need to send caps (in GDP) and streamheaders.
 * If so, we queue them.
 *
 * Then we run into the main loop that tries to send as many buffers as
 * possible. It will first exhaust the client->sending queue and if the queue
 * is empty, it will pick a buffer from the global queue.
 *
 * Sending the Buffers from the client->sending queue is basically writing
 * the bytes to the socket and maintaining a count of the bytes that were
 * sent. When the buffer is completely sent, it is removed from the
 * client->sending queue and we try to pick a new buffer for sending.
 *
 * When the sending returns a partial buffer we stop sending more data as
 * the next send operation could block.
 *
 * This functions returns FALSE if some error occured.
 */
static gboolean
gst_multifdsink_handle_client_write (GstMultiFdSink * sink,
    GstTCPClient * client)
{
  int fd = client->fd.fd;
  gboolean more;
  gboolean res;
  GstClockTime now;
  GTimeVal nowtv;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  /* when using GDP, first check if we have queued caps yet */
  if (sink->protocol == GST_TCP_PROTOCOL_TYPE_GDP) {
    if (!client->caps_sent) {
      const GstCaps *caps =
          GST_PAD_CAPS (GST_PAD_PEER (GST_BASE_SINK_PAD (sink)));

      /* queue caps for sending */
      res = gst_multifdsink_client_queue_caps (sink, client, caps);
      if (!res) {
        GST_DEBUG_OBJECT (sink, "Failed queueing caps, removing client");
        return FALSE;
      }
      client->caps_sent = TRUE;
    }
  }
  /* if we have streamheader buffers, and haven't sent them to this client
   * yet, send them out one by one */
  if (!client->streamheader_sent) {
    GST_DEBUG_OBJECT (sink, "[fd %5d] Sending streamheader, %d buffers", fd,
        g_slist_length (sink->streamheader));
    if (sink->streamheader) {
      GSList *l;

      for (l = sink->streamheader; l; l = l->next) {
        /* queue stream headers for sending */
        res =
            gst_multifdsink_client_queue_buffer (sink, client,
            GST_BUFFER (l->data));
        if (!res) {
          GST_DEBUG_OBJECT (sink,
              "Failed queueing streamheader, removing client");
          return FALSE;
        }
      }
    }
    client->streamheader_sent = TRUE;
  }

  more = TRUE;
  do {
    gint maxsize;

    if (!client->sending) {
      /* client is not working on a buffer */
      if (client->bufpos == -1) {
        /* client is too fast, remove from write queue until new buffer is
         * available */
        gst_fdset_fd_ctl_write (sink->fdset, &client->fd, FALSE);
        return TRUE;
      } else {
        /* client can pick a buffer from the global queue */
        GstBuffer *buf;

        /* for new connections, we need to find a good spot in the
         * bufqueue to start streaming from */
        if (client->new_connection) {
          gint position = gst_multifdsink_new_client (sink, client);

          if (position >= 0) {
            /* we got a valid spot in the queue */
            client->new_connection = FALSE;
            client->bufpos = position;
          } else {
            /* cannot send data to this client yet */
            gst_fdset_fd_ctl_write (sink->fdset, &client->fd, FALSE);
            return TRUE;
          }
        }

        /* grab buffer */
        buf = g_array_index (sink->bufqueue, GstBuffer *, client->bufpos);
        client->bufpos--;
        GST_LOG_OBJECT (sink, "[fd %5d] client %p at position %d",
            fd, client, client->bufpos);

        /* queueing a buffer will ref it */
        gst_multifdsink_client_queue_buffer (sink, client, buf);

        /* need to start from the first byte for this new buffer */
        client->bufoffset = 0;
      }
    }

    /* see if we need to send something */
    if (client->sending) {
      ssize_t wrote;
      GstBuffer *head;

      /* pick first buffer from list */
      head = GST_BUFFER (client->sending->data);
      maxsize = GST_BUFFER_SIZE (head) - client->bufoffset;

      /* try to write the complete buffer */
#ifdef MSG_NOSIGNAL
#define FLAGS MSG_NOSIGNAL
#else
#define FLAGS 0
#endif
      if (client->is_socket) {
        wrote =
            send (fd, GST_BUFFER_DATA (head) + client->bufoffset, maxsize,
            FLAGS);
      } else {
        wrote = write (fd, GST_BUFFER_DATA (head) + client->bufoffset, maxsize);
      }

      if (wrote < 0) {
        /* hmm error.. */
        if (errno == EAGAIN) {
          /* nothing serious, resource was unavailable, try again later */
          more = FALSE;
        } else if (errno == ECONNRESET) {
          GST_DEBUG_OBJECT (sink, "[fd %5d] connection reset by peer, removing",
              fd);
          client->status = GST_CLIENT_STATUS_CLOSED;
          return FALSE;
        } else {
          GST_WARNING_OBJECT (sink,
              "[fd %5d] could not write, removing client: %s (%d)", fd,
              g_strerror (errno), errno);
          client->status = GST_CLIENT_STATUS_ERROR;
          return FALSE;
        }
      } else {
        if (wrote < maxsize) {
          /* partial write means that the client cannot read more and we should
           * stop sending more */
          GST_LOG_OBJECT (sink, "partial write on %d of %d bytes", fd, wrote);
          client->bufoffset += wrote;
          more = FALSE;
        } else {
          /* complete buffer was written, we can proceed to the next one */
          client->sending = g_slist_remove (client->sending, head);
          gst_buffer_unref (head);
          /* make sure we start from byte 0 for the next buffer */
          client->bufoffset = 0;
        }
        /* update stats */
        client->bytes_sent += wrote;
        client->last_activity_time = now;
        sink->bytes_served += wrote;
      }
    }
  } while (more);

  return TRUE;
}

/* calculate the new position for a client after recovery. This function
 * does not update the client position but merely returns the required
 * position.
 */
static gint
gst_multifdsink_recover_client (GstMultiFdSink * sink, GstTCPClient * client)
{
  gint newbufpos;

  GST_WARNING_OBJECT (sink,
      "[fd %5d] client %p is lagging at %d, recover using policy %d",
      client->fd.fd, client, client->bufpos, sink->recover_policy);

  switch (sink->recover_policy) {
    case GST_RECOVER_POLICY_NONE:
      /* do nothing, client will catch up or get kicked out when it reaches
       * the hard max */
      newbufpos = client->bufpos;
      break;
    case GST_RECOVER_POLICY_RESYNC_START:
      /* move to beginning of queue */
      newbufpos = -1;
      break;
    case GST_RECOVER_POLICY_RESYNC_SOFT:
      /* move to beginning of soft max */
      newbufpos = sink->units_soft_max;
      break;
    case GST_RECOVER_POLICY_RESYNC_KEYFRAME:
      /* find keyframe in buffers, we search backwards to find the 
       * closest keyframe relative to what this client already received. */
      newbufpos = MIN (sink->bufqueue->len - 1, sink->units_soft_max - 1);

      while (newbufpos >= 0) {
        GstBuffer *buf;

        buf = g_array_index (sink->bufqueue, GstBuffer *, newbufpos);
        if (is_sync_frame (sink, buf)) {
          /* found a buffer that is not a delta unit */
          break;
        }
        newbufpos--;
      }
      break;
    default:
      /* unknown recovery procedure */
      newbufpos = sink->units_soft_max;
      break;
  }
  return newbufpos;
}

/* Queue a buffer on the global queue. 
 *
 * This functions adds the buffer to the front of a GArray. It removes the
 * tail buffer if the max queue size is exceeded. Unreffing the buffer that
 * is queued. Note that unreffing the buffer is not a problem as clients who
 * started writing out this buffer will still have a reference to it in the
 * client->sending queue.
 *
 * After adding the buffer, we update all client positions in the queue. If
 * a client moves over the soft max, we start the recovery procedure for this
 * slow client. If it goes over the hard max, it is put into the slow list
 * and removed.
 *
 * Special care is taken of clients that were waiting for a new buffer (they
 * had a position of -1) because they can proceed after adding this new buffer.
 * This is done by adding the client back into the write fd_set and signalling
 * the select thread that the fd_set changed.
 *
 */
static void
gst_multifdsink_queue_buffer (GstMultiFdSink * sink, GstBuffer * buf)
{
  GList *clients, *next;
  gint queuelen;
  gboolean need_signal = FALSE;
  gint max_buffer_usage;
  gint i;
  GTimeVal nowtv;
  GstClockTime now;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (sink);
  /* add buffer to queue */
  g_array_prepend_val (sink->bufqueue, buf);
  queuelen = sink->bufqueue->len;

  /* then loop over the clients and update the positions */
  max_buffer_usage = 0;
  for (clients = sink->clients; clients; clients = next) {
    GstTCPClient *client;

    client = (GstTCPClient *) clients->data;
    next = g_list_next (clients);

    client->bufpos++;
    GST_LOG_OBJECT (sink, "[fd %5d] client %p at position %d",
        client->fd.fd, client, client->bufpos);
    /* check soft max if needed, recover client */
    if (sink->units_soft_max > 0 && client->bufpos >= sink->units_soft_max) {
      gint newpos;

      newpos = gst_multifdsink_recover_client (sink, client);
      if (newpos != client->bufpos) {
        client->bufpos = newpos;
        client->discont = TRUE;
        GST_INFO_OBJECT (sink, "[fd %5d] client %p position reset to %d",
            client->fd.fd, client, client->bufpos);
      } else {
        GST_INFO_OBJECT (sink,
            "[fd %5d] client %p not recovering position",
            client->fd.fd, client);
      }
    }
    /* check hard max and timeout, remove client */
    if ((sink->units_max > 0 && client->bufpos >= sink->units_max) ||
        (sink->timeout > 0
            && now - client->last_activity_time > sink->timeout)) {
      /* remove client */
      GST_WARNING_OBJECT (sink, "[fd %5d] client %p is too slow, removing",
          client->fd.fd, client);
      /* remove the client, the fd set will be cleared and the select thread will
       * be signaled */
      client->status = GST_CLIENT_STATUS_SLOW;
      gst_multifdsink_remove_client_link (sink, clients);
      /* set client to invalid position while being removed */
      client->bufpos = -1;
      need_signal = TRUE;
    } else if (client->bufpos == 0 || client->new_connection) {
      /* can send data to this client now. need to signal the select thread that
       * the fd_set changed */
      gst_fdset_fd_ctl_write (sink->fdset, &client->fd, TRUE);
      need_signal = TRUE;
    }
    /* keep track of maximum buffer usage */
    if (client->bufpos > max_buffer_usage) {
      max_buffer_usage = client->bufpos;
    }
  }

  /* now look for sync points and make sure there is at least one
   * sync point in the queue. We only do this if the burst mode
   * is enabled. */
  if (sink->sync_method == GST_SYNC_METHOD_BURST) {
    /* no point in searching beyond the queue length */
    gint limit = queuelen;
    GstBuffer *buf;

    /* no point in searching beyond the soft-max if any. */
    if (sink->units_soft_max > 0) {
      limit = MIN (limit, sink->units_soft_max);
    }
    GST_LOG_OBJECT (sink, "extending queue to include sync point, now at %d",
        max_buffer_usage);
    for (i = 0; i < limit; i++) {
      buf = g_array_index (sink->bufqueue, GstBuffer *, i);
      if (is_sync_frame (sink, buf)) {
        /* found a sync frame, now extend the buffer usage to
         * include at least this frame. */
        max_buffer_usage = MAX (max_buffer_usage, i);
        break;
      }
    }
    GST_LOG_OBJECT (sink, "max buffer usage is now %d", max_buffer_usage);
  }

  /* nobody is referencing units after max_buffer_usage so we can
   * remove them from the queue. We remove them in reverse order as
   * this is the most optimal for GArray. */
  for (i = queuelen - 1; i > max_buffer_usage; i--) {
    GstBuffer *old;

    /* queue exceeded max size */
    queuelen--;
    old = g_array_index (sink->bufqueue, GstBuffer *, i);
    sink->bufqueue = g_array_remove_index (sink->bufqueue, i);

    /* unref tail buffer */
    gst_buffer_unref (old);
  }
  /* save for stats */
  sink->buffers_queued = max_buffer_usage;
  CLIENTS_UNLOCK (sink);

  /* and send a signal to thread if fd_set changed */
  if (need_signal) {
    SEND_COMMAND (sink, CONTROL_RESTART);
  }
}

/* Handle the clients. Basically does a blocking select for one
 * of the client fds to become read or writable. We also have a
 * filedescriptor to receive commands on that we need to check.
 *
 * After going out of the select call, we read and write to all
 * clients that can do so. Badly behaving clients are put on a
 * garbage list and removed.
 */
static void
gst_multifdsink_handle_clients (GstMultiFdSink * sink)
{
  int result;
  GList *clients, *next;
  gboolean try_again;
  GstMultiFdSinkClass *fclass;

  fclass = GST_MULTIFDSINK_GET_CLASS (sink);

  do {
    gboolean stop = FALSE;

    try_again = FALSE;

    /* check for:
     * - server socket input (ie, new client connections)
     * - client socket input (ie, clients saying goodbye)
     * - client socket output (ie, client reads)          */
    GST_LOG_OBJECT (sink, "waiting on action on fdset");
    result = gst_fdset_wait (sink->fdset, -1);

    /* < 0 is an error, 0 just means a timeout happened, which is impossible */
    if (result < 0) {
      GST_WARNING_OBJECT (sink, "wait failed: %s (%d)", g_strerror (errno),
          errno);
      if (errno == EBADF) {
        /* ok, so one or more of the fds is invalid. We loop over them to find
         * the ones that give an error to the F_GETFL fcntl. */
        CLIENTS_LOCK (sink);
        for (clients = sink->clients; clients; clients = next) {
          GstTCPClient *client;
          int fd;
          long flags;
          int res;

          client = (GstTCPClient *) clients->data;
          next = g_list_next (clients);

          fd = client->fd.fd;

          res = fcntl (fd, F_GETFL, &flags);
          if (res == -1) {
            GST_WARNING_OBJECT (sink, "fnctl failed for %d, removing: %s (%d)",
                fd, g_strerror (errno), errno);
            if (errno == EBADF) {
              client->status = GST_CLIENT_STATUS_ERROR;
              gst_multifdsink_remove_client_link (sink, clients);
            }
          }
        }
        CLIENTS_UNLOCK (sink);
        /* after this, go back in the select loop as the read/writefds
         * are not valid */
        try_again = TRUE;
      } else if (errno == EINTR) {
        /* interrupted system call, just redo the select */
        try_again = TRUE;
      } else {
        /* this is quite bad... */
        GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
            ("select failed: %s (%d)", g_strerror (errno), errno));
        return;
      }
    } else {
      GST_LOG_OBJECT (sink, "wait done: %d sockets with events", result);
      /* read all commands */
      if (gst_fdset_fd_can_read (sink->fdset, &READ_SOCKET (sink))) {
        GST_LOG_OBJECT (sink, "have a command");
        while (TRUE) {
          gchar command;
          int res;

          READ_COMMAND (sink, command, res);
          if (res < 0) {
            GST_LOG_OBJECT (sink, "no more commands");
            /* no more commands */
            break;
          }

          switch (command) {
            case CONTROL_RESTART:
              GST_LOG_OBJECT (sink, "restart");
              /* need to restart the select call as the fd_set changed */
              /* if other file descriptors than the READ_SOCKET had activity,
               * we don't restart just yet, but handle the other clients first */
              if (result == 1)
                try_again = TRUE;
              break;
            case CONTROL_STOP:
              /* break out of the select loop */
              GST_LOG_OBJECT (sink, "stop");
              /* stop this function */
              stop = TRUE;
              break;
            default:
              GST_WARNING_OBJECT (sink, "unkown");
              g_warning ("multifdsink: unknown control message received");
              break;
          }
        }
      }
    }
    if (stop) {
      return;
    }
  } while (try_again);

  /* subclasses can check fdset with this virtual function */
  if (fclass->wait)
    fclass->wait (sink, sink->fdset);

  /* Check the clients */
  CLIENTS_LOCK (sink);
  for (clients = sink->clients; clients; clients = next) {
    GstTCPClient *client;

    client = (GstTCPClient *) clients->data;
    next = g_list_next (clients);

    if (client->status != GST_CLIENT_STATUS_OK) {
      gst_multifdsink_remove_client_link (sink, clients);
      continue;
    }

    if (gst_fdset_fd_has_closed (sink->fdset, &client->fd)) {
      client->status = GST_CLIENT_STATUS_CLOSED;
      gst_multifdsink_remove_client_link (sink, clients);
      continue;
    }
    if (gst_fdset_fd_has_error (sink->fdset, &client->fd)) {
      GST_WARNING_OBJECT (sink, "gst_fdset_fd_has_error for %d", client->fd);
      client->status = GST_CLIENT_STATUS_ERROR;
      gst_multifdsink_remove_client_link (sink, clients);
      continue;
    }
    if (gst_fdset_fd_can_read (sink->fdset, &client->fd)) {
      /* handle client read */
      if (!gst_multifdsink_handle_client_read (sink, client)) {
        gst_multifdsink_remove_client_link (sink, clients);
        continue;
      }
    }
    if (gst_fdset_fd_can_write (sink->fdset, &client->fd)) {
      /* handle client write */
      if (!gst_multifdsink_handle_client_write (sink, client)) {
        gst_multifdsink_remove_client_link (sink, clients);
        continue;
      }
    }
  }
  CLIENTS_UNLOCK (sink);
}

/* we handle the client communication in another thread so that we do not block
 * the gstreamer thread while we select() on the client fds */
static gpointer
gst_multifdsink_thread (GstMultiFdSink * sink)
{
  while (sink->running) {
    gst_multifdsink_handle_clients (sink);
  }
  return NULL;
}

static GstFlowReturn
gst_multifdsink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstMultiFdSink *sink;

  sink = GST_MULTIFDSINK (bsink);

  /* since we keep this buffer out of the scope of this method */
  gst_buffer_ref (buf);

  g_return_val_if_fail (GST_FLAG_IS_SET (sink, GST_MULTIFDSINK_OPEN),
      GST_FLOW_ERROR);

  GST_LOG_OBJECT (sink, "received buffer %p", buf);
  /* if we get IN_CAPS buffers, but the previous buffer was not IN_CAPS,
   * it means we're getting new streamheader buffers, and we should clear
   * the old ones */
  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_IN_CAPS) &&
      sink->previous_buffer_in_caps == FALSE) {
    GST_DEBUG_OBJECT (sink,
        "receiving new IN_CAPS buffers, clearing old streamheader");
    g_slist_foreach (sink->streamheader, (GFunc) gst_mini_object_unref, NULL);
    g_slist_free (sink->streamheader);
    sink->streamheader = NULL;
  }
  /* if the incoming buffer is marked as IN CAPS, then we assume for now
   * it's a streamheader that needs to be sent to each new client, so we
   * put it on our internal list of streamheader buffers.
   * After that we return, since we only send these out when we get
   * non IN_CAPS buffers so we properly keep track of clients that got
   * streamheaders. */
  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_IN_CAPS)) {
    sink->previous_buffer_in_caps = TRUE;
    GST_DEBUG_OBJECT (sink,
        "appending IN_CAPS buffer with length %d to streamheader",
        GST_BUFFER_SIZE (buf));
    sink->streamheader = g_slist_append (sink->streamheader, buf);
    return GST_FLOW_OK;
  }

  sink->previous_buffer_in_caps = FALSE;
  /* queue the buffer */
  gst_multifdsink_queue_buffer (sink, buf);

  sink->bytes_to_serve += GST_BUFFER_SIZE (buf);

  return GST_FLOW_OK;
}

static void
gst_multifdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  g_return_if_fail (GST_IS_MULTIFDSINK (object));
  multifdsink = GST_MULTIFDSINK (object);

  switch (prop_id) {
    case ARG_PROTOCOL:
      multifdsink->protocol = g_value_get_enum (value);
      break;
    case ARG_MODE:
      multifdsink->mode = g_value_get_enum (value);
      break;
    case ARG_BUFFERS_MAX:
      multifdsink->units_max = g_value_get_int (value);
      break;
    case ARG_BUFFERS_SOFT_MAX:
      multifdsink->units_soft_max = g_value_get_int (value);
      break;
    case ARG_UNIT_TYPE:
      multifdsink->unit_type = g_value_get_enum (value);
      break;
    case ARG_UNITS_MAX:
      multifdsink->units_max = g_value_get_int (value);
      break;
    case ARG_UNITS_SOFT_MAX:
      multifdsink->units_soft_max = g_value_get_int (value);
      break;
    case ARG_RECOVER_POLICY:
      multifdsink->recover_policy = g_value_get_enum (value);
      break;
    case ARG_TIMEOUT:
      multifdsink->timeout = g_value_get_uint64 (value);
      break;
    case ARG_SYNC_CLIENTS:
      if (g_value_get_boolean (value) == TRUE) {
        multifdsink->sync_method = GST_SYNC_METHOD_WAIT;
      } else {
        multifdsink->sync_method = GST_SYNC_METHOD_NONE;
      }
      break;
    case ARG_SYNC_METHOD:
      multifdsink->sync_method = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multifdsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  g_return_if_fail (GST_IS_MULTIFDSINK (object));
  multifdsink = GST_MULTIFDSINK (object);

  switch (prop_id) {
    case ARG_PROTOCOL:
      g_value_set_enum (value, multifdsink->protocol);
      break;
    case ARG_MODE:
      g_value_set_enum (value, multifdsink->mode);
      break;
    case ARG_BUFFERS_MAX:
      g_value_set_int (value, multifdsink->units_max);
      break;
    case ARG_BUFFERS_SOFT_MAX:
      g_value_set_int (value, multifdsink->units_soft_max);
      break;
    case ARG_BUFFERS_QUEUED:
      g_value_set_uint (value, multifdsink->buffers_queued);
      break;
    case ARG_BYTES_QUEUED:
      g_value_set_uint (value, multifdsink->bytes_queued);
      break;
    case ARG_TIME_QUEUED:
      g_value_set_uint64 (value, multifdsink->time_queued);
      break;
    case ARG_UNIT_TYPE:
      g_value_set_enum (value, multifdsink->unit_type);
      break;
    case ARG_UNITS_MAX:
      g_value_set_int (value, multifdsink->units_max);
      break;
    case ARG_UNITS_SOFT_MAX:
      g_value_set_int (value, multifdsink->units_soft_max);
      break;
    case ARG_RECOVER_POLICY:
      g_value_set_enum (value, multifdsink->recover_policy);
      break;
    case ARG_TIMEOUT:
      g_value_set_uint64 (value, multifdsink->timeout);
      break;
    case ARG_SYNC_CLIENTS:
      g_value_set_boolean (value,
          multifdsink->sync_method == GST_SYNC_METHOD_WAIT);
      break;
    case ARG_SYNC_METHOD:
      g_value_set_enum (value, multifdsink->sync_method);
      break;
    case ARG_BYTES_TO_SERVE:
      g_value_set_uint64 (value, multifdsink->bytes_to_serve);
      break;
    case ARG_BYTES_SERVED:
      g_value_set_uint64 (value, multifdsink->bytes_served);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_multifdsink_start (GstBaseSink * bsink)
{
  GstMultiFdSinkClass *fclass;
  int control_socket[2];
  GstMultiFdSink *this;

  if (GST_FLAG_IS_SET (bsink, GST_MULTIFDSINK_OPEN))
    return TRUE;

  this = GST_MULTIFDSINK (bsink);
  fclass = GST_MULTIFDSINK_GET_CLASS (this);

  GST_INFO_OBJECT (this, "starting in mode %d", this->mode);
  this->fdset = gst_fdset_new (this->mode);

  if (socketpair (PF_UNIX, SOCK_STREAM, 0, control_socket) < 0)
    goto socket_pair;

  READ_SOCKET (this).fd = control_socket[0];
  WRITE_SOCKET (this).fd = control_socket[1];

  gst_fdset_add_fd (this->fdset, &READ_SOCKET (this));
  gst_fdset_fd_ctl_read (this->fdset, &READ_SOCKET (this), TRUE);

  fcntl (READ_SOCKET (this).fd, F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (this).fd, F_SETFL, O_NONBLOCK);

  this->streamheader = NULL;
  this->bytes_to_serve = 0;
  this->bytes_served = 0;

  if (fclass->init) {
    fclass->init (this);
  }

  this->running = TRUE;
  this->thread = g_thread_create ((GThreadFunc) gst_multifdsink_thread,
      this, TRUE, NULL);

  GST_FLAG_SET (this, GST_MULTIFDSINK_OPEN);

  return TRUE;

  /* ERRORS */
socket_pair:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ_WRITE, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static gboolean
gst_multifdsink_stop (GstBaseSink * bsink)
{
  GstMultiFdSinkClass *fclass;
  GstMultiFdSink *this;

  this = GST_MULTIFDSINK (bsink);
  fclass = GST_MULTIFDSINK_GET_CLASS (this);

  if (!GST_FLAG_IS_SET (bsink, GST_MULTIFDSINK_OPEN))
    return TRUE;

  this->running = FALSE;

  SEND_COMMAND (this, CONTROL_STOP);
  if (this->thread) {
    g_thread_join (this->thread);
    this->thread = NULL;
  }

  /* free the clients */
  gst_multifdsink_clear (this);

  close (READ_SOCKET (this).fd);
  close (WRITE_SOCKET (this).fd);

  if (this->streamheader) {
    g_slist_foreach (this->streamheader, (GFunc) gst_mini_object_unref, NULL);
    g_slist_free (this->streamheader);
    this->streamheader = NULL;
  }

  if (fclass->close)
    fclass->close (this);

  if (this->fdset) {
    gst_fdset_remove_fd (this->fdset, &READ_SOCKET (this));
    gst_fdset_free (this->fdset);
    this->fdset = NULL;
  }
  GST_FLAG_UNSET (this, GST_MULTIFDSINK_OPEN);
  CLIENTS_LOCK_FREE (this);
  g_hash_table_destroy (this->fd_hash);

  return TRUE;
}

static GstElementStateReturn
gst_multifdsink_change_state (GstElement * element)
{
  GstMultiFdSink *sink;
  gint transition;
  GstElementStateReturn ret;

  sink = GST_MULTIFDSINK (element);

  /* we disallow changing the state from the streaming thread */
  if (g_thread_self () == sink->thread)
    return GST_STATE_FAILURE;

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_multifdsink_start (GST_BASE_SINK (sink)))
        goto start_failed;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      gst_multifdsink_stop (GST_BASE_SINK (sink));
      break;
  }
  return ret;

  /* ERRORS */
start_failed:
  {
    return GST_STATE_FAILURE;
  }
}
