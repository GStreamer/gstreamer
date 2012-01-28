/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2006 Wim Taymans <wim at fluendo dot com>
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
 * SECTION:element-multifdsink
 * @see_also: tcpserversink
 *
 * This plugin writes incoming data to a set of file descriptors. The
 * file descriptors can be added to multifdsink by emitting the #GstMultiFdSink::add signal. 
 * For each descriptor added, the #GstMultiFdSink::client-added signal will be called.
 *
 * As of version 0.10.8, a client can also be added with the #GstMultiFdSink::add-full signal
 * that allows for more control over what and how much data a client 
 * initially receives.
 *
 * Clients can be removed from multifdsink by emitting the #GstMultiFdSink::remove signal. For
 * each descriptor removed, the #GstMultiFdSink::client-removed signal will be called. The
 * #GstMultiFdSink::client-removed signal can also be fired when multifdsink decides that a
 * client is not active anymore or, depending on the value of the
 * #GstMultiFdSink:recover-policy property, if the client is reading too slowly.
 * In all cases, multifdsink will never close a file descriptor itself.
 * The user of multifdsink is responsible for closing all file descriptors.
 * This can for example be done in response to the #GstMultiFdSink::client-fd-removed signal.
 * Note that multifdsink still has a reference to the file descriptor when the
 * #GstMultiFdSink::client-removed signal is emitted, so that "get-stats" can be performed on
 * the descriptor; it is therefore not safe to close the file descriptor in
 * the #GstMultiFdSink::client-removed signal handler, and you should use the 
 * #GstMultiFdSink::client-fd-removed signal to safely close the fd.
 *
 * Multifdsink internally keeps a queue of the incoming buffers and uses a
 * separate thread to send the buffers to the clients. This ensures that no
 * client write can block the pipeline and that clients can read with different
 * speeds.
 *
 * When adding a client to multifdsink, the #GstMultiFdSink:sync-method property will define
 * which buffer in the queued buffers will be sent first to the client. Clients 
 * can be sent the most recent buffer (which might not be decodable by the 
 * client if it is not a keyframe), the next keyframe received in 
 * multifdsink (which can take some time depending on the keyframe rate), or the
 * last received keyframe (which will cause a simple burst-on-connect). 
 * Multifdsink will always keep at least one keyframe in its internal buffers
 * when the sync-mode is set to latest-keyframe.
 *
 * As of version 0.10.8, there are additional values for the #GstMultiFdSink:sync-method 
 * property to allow finer control over burst-on-connect behaviour. By selecting
 * the 'burst' method a minimum burst size can be chosen, 'burst-keyframe'
 * additionally requires that the burst begin with a keyframe, and 
 * 'burst-with-keyframe' attempts to burst beginning with a keyframe, but will
 * prefer a minimum burst size even if it requires not starting with a keyframe.
 *
 * Multifdsink can be instructed to keep at least a minimum amount of data
 * expressed in time or byte units in its internal queues with the 
 * #GstMultiFdSink:time-min and #GstMultiFdSink:bytes-min properties respectively.
 * These properties are useful if the application adds clients with the 
 * #GstMultiFdSink::add-full signal to make sure that a burst connect can
 * actually be honored. 
 *
 * When streaming data, clients are allowed to read at a different rate than
 * the rate at which multifdsink receives data. If the client is reading too
 * fast, no data will be send to the client until multifdsink receives more
 * data. If the client, however, reads too slowly, data for that client will be 
 * queued up in multifdsink. Two properties control the amount of data 
 * (buffers) that is queued in multifdsink: #GstMultiFdSink:buffers-max and 
 * #GstMultiFdSink:buffers-soft-max. A client that falls behind by
 * #GstMultiFdSink:buffers-max is removed from multifdsink forcibly.
 *
 * A client with a lag of at least #GstMultiFdSink:buffers-soft-max enters the recovery
 * procedure which is controlled with the #GstMultiFdSink:recover-policy property.
 * A recover policy of NONE will do nothing, RESYNC_LATEST will send the most recently
 * received buffer as the next buffer for the client, RESYNC_SOFT_LIMIT
 * positions the client to the soft limit in the buffer queue and
 * RESYNC_KEYFRAME positions the client at the most recent keyframe in the
 * buffer queue.
 *
 * multifdsink will by default synchronize on the clock before serving the 
 * buffers to the clients. This behaviour can be disabled by setting the sync 
 * property to FALSE. Multifdsink will by default not do QoS and will never
 * drop late buffers.
 *
 * Last reviewed on 2006-09-12 (0.10.10)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include <sys/ioctl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

// FIXME: remove
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gstmultifdsink.h"
#include "gsttcp-marshal.h"

#define NOT_IMPLEMENTED 0

GST_DEBUG_CATEGORY_STATIC (multifdsink_debug);
#define GST_CAT_DEFAULT (multifdsink_debug)

/* MultiFdSink signals and args */
enum
{
  /* methods */
  SIGNAL_ADD,
  SIGNAL_ADD_BURST,
  SIGNAL_REMOVE,
  SIGNAL_REMOVE_FLUSH,
  SIGNAL_GET_STATS,

  /* signals */
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,
  SIGNAL_CLIENT_FD_REMOVED,

  LAST_SIGNAL
};

/* this is really arbitrarily chosen */
#define DEFAULT_MODE                    1

#define DEFAULT_HANDLE_READ             TRUE

enum
{
  PROP_0,
  PROP_MODE,

  PROP_HANDLE_READ,

  PROP_NUM_FDS,

  PROP_LAST
};

/* For backward compat, we can't really select the poll mode anymore with
 * GstPoll. */
#define GST_TYPE_FDSET_MODE (gst_fdset_mode_get_type())
static GType
gst_fdset_mode_get_type (void)
{
  static GType fdset_mode_type = 0;
  static const GEnumValue fdset_mode[] = {
    {0, "Select", "select"},
    {1, "Poll", "poll"},
    {2, "EPoll", "epoll"},
    {0, NULL, NULL},
  };

  if (!fdset_mode_type) {
    fdset_mode_type = g_enum_register_static ("GstFDSetMode", fdset_mode);
  }
  return fdset_mode_type;
}

static void gst_multi_fd_sink_finalize (GObject * object);

static void gst_multi_fd_sink_clear_post (GstMultiHandleSink * mhsink);
static void gst_multi_fd_sink_stop_pre (GstMultiHandleSink * mhsink);
static void gst_multi_fd_sink_stop_post (GstMultiHandleSink * mhsink);
static gboolean gst_multi_fd_sink_start_pre (GstMultiHandleSink * mhsink);
static gpointer gst_multi_fd_sink_thread (GstMultiHandleSink * mhsink);
static void gst_multi_fd_sink_queue_buffer (GstMultiHandleSink * mhsink,
    GstBuffer * buffer);
static gboolean gst_multi_fd_sink_client_queue_buffer (GstMultiHandleSink *
    mhsink, GstMultiHandleClient * mhclient, GstBuffer * buffer);
static int gst_multi_fd_sink_client_get_fd (GstMultiHandleClient * client);
static void
gst_multi_fd_sink_handle_debug (GstMultiSinkHandle handle, gchar debug[30]);

static void gst_multi_fd_sink_remove_client_link (GstMultiHandleSink * sink,
    GList * link);

static void gst_multi_fd_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_fd_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_multi_fd_sink_parent_class parent_class
G_DEFINE_TYPE (GstMultiFdSink, gst_multi_fd_sink, GST_TYPE_MULTI_HANDLE_SINK);

static guint gst_multi_fd_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_fd_sink_class_init (GstMultiFdSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstMultiHandleSinkClass *gstmultihandlesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstmultihandlesink_class = (GstMultiHandleSinkClass *) klass;

  gobject_class->set_property = gst_multi_fd_sink_set_property;
  gobject_class->get_property = gst_multi_fd_sink_get_property;
  gobject_class->finalize = gst_multi_fd_sink_finalize;

  /**
   * GstMultiFdSink::mode
   *
   * The mode for selecting activity on the fds. 
   *
   * This property is deprecated since 0.10.18, if will now automatically
   * select and use the most optimal method.
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The mode for selecting activity on the fds (deprecated)",
          GST_TYPE_FDSET_MODE, DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiFdSink::handle-read
   *
   * Handle read requests from clients and discard the data.
   *
   * Since: 0.10.23
   */
  g_object_class_install_property (gobject_class, PROP_HANDLE_READ,
      g_param_spec_boolean ("handle-read", "Handle Read",
          "Handle client reads and discard the data",
          DEFAULT_HANDLE_READ, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_FDS,
      g_param_spec_uint ("num-fds", "Number of fds",
          "The current number of client file descriptors.",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiFdSink::add:
   * @gstmultifdsink: the multifdsink element to emit this signal on
   * @fd:             the file descriptor to add to multifdsink
   *
   * Hand the given open file descriptor to multifdsink to write to.
   */
  gst_multi_fd_sink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          add), NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1,
      G_TYPE_INT);
  /**
   * GstMultiFdSink::add-full:
   * @gstmultifdsink: the multifdsink element to emit this signal on
   * @fd:             the file descriptor to add to multifdsink
   * @sync:           the sync method to use
   * @unit_format_min:  the unit-format of @value_min
   * @value_min:      the minimum amount of data to burst expressed in
   *                  @unit_format_min units.
   * @unit_format_max:  the unit-format of @value_max
   * @value_max:      the maximum amount of data to burst expressed in
   *                  @unit_format_max units.
   *
   * Hand the given open file descriptor to multifdsink to write to and
   * specify the burst parameters for the new connection.
   */
  gst_multi_fd_sink_signals[SIGNAL_ADD_BURST] =
      g_signal_new ("add-full", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          add_full), NULL, NULL,
      gst_tcp_marshal_VOID__INT_ENUM_INT_UINT64_INT_UINT64,
      G_TYPE_NONE, 6, G_TYPE_INT, GST_TYPE_SYNC_METHOD, GST_TYPE_FORMAT,
      G_TYPE_UINT64, GST_TYPE_FORMAT, G_TYPE_UINT64);
  /**
   * GstMultiFdSink::remove:
   * @gstmultifdsink: the multifdsink element to emit this signal on
   * @fd:             the file descriptor to remove from multifdsink
   *
   * Remove the given open file descriptor from multifdsink.
   */
  gst_multi_fd_sink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          remove), NULL, NULL, gst_tcp_marshal_VOID__INT, G_TYPE_NONE,
      1, G_TYPE_INT);
  /**
   * GstMultiFdSink::remove-flush:
   * @gstmultifdsink: the multifdsink element to emit this signal on
   * @fd:             the file descriptor to remove from multifdsink
   *
   * Remove the given open file descriptor from multifdsink after flushing all
   * the pending data to the fd.
   */
  gst_multi_fd_sink_signals[SIGNAL_REMOVE_FLUSH] =
      g_signal_new ("remove-flush", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          remove_flush), NULL, NULL, gst_tcp_marshal_VOID__INT,
      G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstMultiFdSink::get-stats:
   * @gstmultifdsink: the multifdsink element to emit this signal on
   * @fd:             the file descriptor to get stats of from multifdsink
   *
   * Get statistics about @fd. This function returns a GValueArray to ease
   * automatic wrapping for bindings.
   *
   * Returns: a GValueArray with the statistics. The array contains guint64
   *     values that represent respectively: total number of bytes sent, time
   *     when the client was added, time when the client was
   *     disconnected/removed, time the client is/was active, last activity
   *     time (in epoch seconds), number of buffers dropped.
   *     All times are expressed in nanoseconds (GstClockTime).
   *     The array can be 0-length if the client was not found.
   */
  gst_multi_fd_sink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          get_stats), NULL, NULL, gst_tcp_marshal_BOXED__INT,
      G_TYPE_VALUE_ARRAY, 1, G_TYPE_INT);

  /**
   * GstMultiFdSink::client-added:
   * @gstmultifdsink: the multifdsink element that emitted this signal
   * @fd:             the file descriptor that was added to multifdsink
   *
   * The given file descriptor was added to multifdsink. This signal will
   * be emitted from the streaming thread so application should be prepared
   * for that.
   */
  gst_multi_fd_sink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiFdSinkClass, client_added),
      NULL, NULL, gst_tcp_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  /**
   * GstMultiFdSink::client-removed:
   * @gstmultifdsink: the multifdsink element that emitted this signal
   * @fd:             the file descriptor that is to be removed from multifdsink
   * @status:         the reason why the client was removed
   *
   * The given file descriptor is about to be removed from multifdsink. This
   * signal will be emitted from the streaming thread so applications should
   * be prepared for that.
   *
   * @gstmultifdsink still holds a handle to @fd so it is possible to call
   * the get-stats signal from this callback. For the same reason it is
   * not safe to close() and reuse @fd in this callback.
   */
  gst_multi_fd_sink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__INT_ENUM,
      G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CLIENT_STATUS);
  /**
   * GstMultiFdSink::client-fd-removed:
   * @gstmultifdsink: the multifdsink element that emitted this signal
   * @fd:             the file descriptor that was removed from multifdsink
   *
   * The given file descriptor was removed from multifdsink. This signal will
   * be emitted from the streaming thread so applications should be prepared
   * for that.
   *
   * In this callback, @gstmultifdsink has removed all the information
   * associated with @fd and it is therefore not possible to call get-stats
   * with @fd. It is however safe to close() and reuse @fd in the callback.
   *
   * Since: 0.10.7
   */
  gst_multi_fd_sink_signals[SIGNAL_CLIENT_FD_REMOVED] =
      g_signal_new ("client-fd-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiFdSinkClass,
          client_fd_removed), NULL, NULL, gst_tcp_marshal_VOID__INT,
      G_TYPE_NONE, 1, G_TYPE_INT);

  gst_element_class_set_details_simple (gstelement_class,
      "Multi filedescriptor sink", "Sink/Network",
      "Send data to multiple filedescriptors",
      "Thomas Vander Stichele <thomas at apestaart dot org>, "
      "Wim Taymans <wim@fluendo.com>");

  gstmultihandlesink_class->clear_post =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_clear_post);

  gstmultihandlesink_class->stop_pre =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_stop_pre);
  gstmultihandlesink_class->stop_post =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_stop_post);
  gstmultihandlesink_class->start_pre =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_start_pre);
  gstmultihandlesink_class->thread =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_thread);
  gstmultihandlesink_class->queue_buffer =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_queue_buffer);
  gstmultihandlesink_class->client_queue_buffer =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_client_queue_buffer);
  gstmultihandlesink_class->client_get_fd =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_client_get_fd);
  gstmultihandlesink_class->handle_debug =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_handle_debug);

  gstmultihandlesink_class->remove_client_link =
      GST_DEBUG_FUNCPTR (gst_multi_fd_sink_remove_client_link);


  klass->add = GST_DEBUG_FUNCPTR (gst_multi_fd_sink_add);
  klass->add_full = GST_DEBUG_FUNCPTR (gst_multi_fd_sink_add_full);
  klass->remove = GST_DEBUG_FUNCPTR (gst_multi_fd_sink_remove);
  klass->remove_flush = GST_DEBUG_FUNCPTR (gst_multi_fd_sink_remove_flush);
  klass->get_stats = GST_DEBUG_FUNCPTR (gst_multi_fd_sink_get_stats);

  GST_DEBUG_CATEGORY_INIT (multifdsink_debug, "multifdsink", 0, "FD sink");
}

static void
gst_multi_fd_sink_init (GstMultiFdSink * this)
{
  this->mode = DEFAULT_MODE;

  this->handle_hash = g_hash_table_new (g_int_hash, g_int_equal);

  this->handle_read = DEFAULT_HANDLE_READ;
}

static void
gst_multi_fd_sink_finalize (GObject * object)
{
  GstMultiFdSink *this = GST_MULTI_FD_SINK (object);

  g_hash_table_destroy (this->handle_hash);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static int
gst_multi_fd_sink_client_get_fd (GstMultiHandleClient * client)
{
  GstTCPClient *tclient = (GstTCPClient *) client;

  return tclient->gfd.fd;
}

static void
gst_multi_fd_sink_handle_debug (GstMultiSinkHandle handle, gchar debug[30])
{
  g_snprintf (debug, 30, "[fd %5d]", handle.fd);
}

/* "add-full" signal implementation */
void
gst_multi_fd_sink_add_full (GstMultiFdSink * sink, GstMultiSinkHandle handle,
    GstSyncMethod sync_method, GstFormat min_format, guint64 min_value,
    GstFormat max_format, guint64 max_value)
{
  GstTCPClient *client;
  GstMultiHandleClient *mhclient;
  GList *clink;
  gint flags;
  struct stat statbuf;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  // FIXME: convert to a function so we can vfunc this
  int fd = handle.fd;
  gchar debug[30];
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  mhsinkclass->handle_debug (handle, debug);
  GST_DEBUG_OBJECT (sink, "%s adding client, sync_method %d, "
      "min_format %d, min_value %" G_GUINT64_FORMAT
      ", max_format %d, max_value %" G_GUINT64_FORMAT, debug,
      sync_method, min_format, min_value, max_format, max_value);

  /* do limits check if we can */
  if (min_format == max_format) {
    if (max_value != -1 && min_value != -1 && max_value < min_value)
      goto wrong_limits;
  }

  /* create client datastructure */
  client = g_new0 (GstTCPClient, 1);
  mhclient = (GstMultiHandleClient *) client;
  gst_multi_handle_sink_client_init (mhclient, sync_method);
  strncpy (mhclient->debug, debug, 30);

  gst_poll_fd_init (&client->gfd);
  client->gfd.fd = fd;
  mhclient->handle.fd = fd;
  mhclient->burst_min_format = min_format;
  mhclient->burst_min_value = min_value;
  mhclient->burst_max_format = max_format;
  mhclient->burst_max_value = max_value;

  CLIENTS_LOCK (sink);

  /* check the hash to find a duplicate fd */
  clink = g_hash_table_lookup (sink->handle_hash, &client->gfd.fd);
  if (clink != NULL)
    goto duplicate;

  /* we can add the fd now */
  clink = mhsink->clients = g_list_prepend (mhsink->clients, client);
  g_hash_table_insert (sink->handle_hash, &client->gfd.fd, clink);
  mhsink->clients_cookie++;

  /* set the socket to non blocking */
  if (fcntl (fd, F_SETFL, O_NONBLOCK) < 0) {
    GST_ERROR_OBJECT (sink, "failed to make socket %d non-blocking: %s",
        mhclient->debug, g_strerror (errno));
  }

  /* we always read from a client */
  gst_poll_add_fd (sink->fdset, &client->gfd);

  /* we don't try to read from write only fds */
  if (sink->handle_read) {
    flags = fcntl (fd, F_GETFL, 0);
    if ((flags & O_ACCMODE) != O_WRONLY) {
      gst_poll_fd_ctl_read (sink->fdset, &client->gfd, TRUE);
    }
  }
  /* figure out the mode, can't use send() for non sockets */
  if (fstat (fd, &statbuf) == 0 && S_ISSOCK (statbuf.st_mode)) {
    client->is_socket = TRUE;
    gst_multi_handle_sink_setup_dscp_client (mhsink, mhclient);
  }

  gst_poll_restart (sink->fdset);

  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multi_fd_sink_signals[SIGNAL_CLIENT_ADDED], 0, fd);

  return;

  /* errors */
wrong_limits:
  {
    GST_WARNING_OBJECT (sink,
        "%s wrong values min =%" G_GUINT64_FORMAT ", max=%"
        G_GUINT64_FORMAT ", unit %d specified when adding client",
        mhclient->debug, min_value, max_value, min_format);
    return;
  }
duplicate:
  {
    mhclient->status = GST_CLIENT_STATUS_DUPLICATE;
    CLIENTS_UNLOCK (sink);
    GST_WARNING_OBJECT (sink, "%s duplicate client found, refusing", fd);
    g_signal_emit (G_OBJECT (sink),
        gst_multi_fd_sink_signals[SIGNAL_CLIENT_REMOVED], 0, mhclient->debug,
        mhclient->status);
    g_free (client);
    return;
  }
}

/* "add" signal implementation */
void
gst_multi_fd_sink_add (GstMultiFdSink * sink, GstMultiSinkHandle handle)
{
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);

  gst_multi_fd_sink_add_full (sink, handle, mhsink->def_sync_method,
      mhsink->def_burst_format, mhsink->def_burst_value,
      mhsink->def_burst_format, -1);
}

/* "remove" signal implementation */
void
gst_multi_fd_sink_remove (GstMultiFdSink * sink, GstMultiSinkHandle handle)
{
  GList *clink;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  // FIXME: convert to a function so we can vfunc this
  gchar debug[30];
  int fd = handle.fd;

  mhsinkclass->handle_debug (handle, debug);
  GST_DEBUG_OBJECT (sink, "%s removing client", debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, &fd);
  if (clink != NULL) {
    GstTCPClient *client = (GstTCPClient *) clink->data;
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

    if (mhclient->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "%s Client already disconnecting with status %d",
          debug, mhclient->status);
      goto done;
    }

    mhclient->status = GST_CLIENT_STATUS_REMOVED;
    mhsinkclass->remove_client_link (GST_MULTI_HANDLE_SINK (sink), clink);
    // FIXME: specific poll
    gst_poll_restart (sink->fdset);
  } else {
    GST_WARNING_OBJECT (sink, "%s no client with this fd found!", debug);
  }

done:
  CLIENTS_UNLOCK (sink);
}

/* "remove-flush" signal implementation */
void
gst_multi_fd_sink_remove_flush (GstMultiFdSink * sink,
    GstMultiSinkHandle handle)
{
  GList *clink;
  // FIXME: convert to a function so we can vfunc this
  gchar debug[30];
  int fd = handle.fd;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  mhsinkclass->handle_debug (handle, debug);

  GST_DEBUG_OBJECT (sink, "%s flushing client", debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, &fd);
  if (clink != NULL) {
    GstTCPClient *client = (GstTCPClient *) clink->data;
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

    if (mhclient->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "%s Client already disconnecting with status %d",
          debug, mhclient->status);
      goto done;
    }

    /* take the position of the client as the number of buffers left to flush.
     * If the client was at position -1, we flush 0 buffers, 0 == flush 1
     * buffer, etc... */
    mhclient->flushcount = mhclient->bufpos + 1;
    /* mark client as flushing. We can not remove the client right away because
     * it might have some buffers to flush in the ->sending queue. */
    mhclient->status = GST_CLIENT_STATUS_FLUSHING;
  } else {
    GST_WARNING_OBJECT (sink, "%s no client with this fd found!", debug);
  }
done:
  CLIENTS_UNLOCK (sink);
}

/* called with the CLIENTS_LOCK held */
static void
gst_multi_fd_sink_clear_post (GstMultiHandleSink * mhsink)
{
  GstMultiFdSink *sink = GST_MULTI_FD_SINK (mhsink);

  gst_poll_restart (sink->fdset);
}

/* "get-stats" signal implementation
 * the array returned contains:
 *
 * guint64 : bytes_sent
 * guint64 : connect time (in nanoseconds, since Epoch)
 * guint64 : disconnect time (in nanoseconds, since Epoch)
 * guint64 : time the client is/was connected (in nanoseconds)
 * guint64 : last activity time (in nanoseconds, since Epoch)
 * guint64 : buffers dropped due to recovery
 * guint64 : timestamp of the first buffer sent (in nanoseconds)
 * guint64 : timestamp of the last buffer sent (in nanoseconds)
 */
GValueArray *
gst_multi_fd_sink_get_stats (GstMultiFdSink * sink, GstMultiSinkHandle handle)
{
  GstTCPClient *client;
  GValueArray *result = NULL;
  GList *clink;
  // FIXME: convert to a function so we can vfunc this
  gchar debug[30];
  int fd = handle.fd;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  mhsinkclass->handle_debug (handle, debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, &fd);
  if (clink == NULL)
    goto noclient;

  client = (GstTCPClient *) clink->data;
  if (client != NULL) {
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
    GValue value = { 0 };
    guint64 interval;

    result = g_value_array_new (7);

    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->bytes_sent);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->connect_time);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    if (mhclient->disconnect_time == 0) {
      GTimeVal nowtv;

      g_get_current_time (&nowtv);

      interval = GST_TIMEVAL_TO_TIME (nowtv) - mhclient->connect_time;
    } else {
      interval = mhclient->disconnect_time - mhclient->connect_time;
    }
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->disconnect_time);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, interval);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->last_activity_time);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->dropped_buffers);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->first_buffer_ts);
    result = g_value_array_append (result, &value);
    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);
    g_value_set_uint64 (&value, mhclient->last_buffer_ts);
    result = g_value_array_append (result, &value);
  }

noclient:
  CLIENTS_UNLOCK (sink);

  /* python doesn't like a NULL pointer yet */
  if (result == NULL) {
    GST_WARNING_OBJECT (sink, "%s no client with this found!", debug);
    result = g_value_array_new (0);
  }

  return result;
}

/* should be called with the clientslock helt.
 * Note that we don't close the fd as we didn't open it in the first
 * place. An application should connect to the client-fd-removed signal and
 * close the fd itself.
 */
static void
gst_multi_fd_sink_remove_client_link (GstMultiHandleSink * sink, GList * link)
{
  GTimeVal now;
  GstTCPClient *client = (GstTCPClient *) link->data;
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
  GstMultiFdSink *mfsink = GST_MULTI_FD_SINK (sink);
  GstMultiFdSinkClass *fclass = GST_MULTI_FD_SINK_GET_CLASS (sink);
  int fd = client->gfd.fd;

  if (mhclient->currently_removing) {
    GST_WARNING_OBJECT (sink, "%s client is already being removed",
        mhclient->debug);
    return;
  } else {
    mhclient->currently_removing = TRUE;
  }

  /* FIXME: if we keep track of ip we can log it here and signal */
  switch (mhclient->status) {
    case GST_CLIENT_STATUS_OK:
      GST_WARNING_OBJECT (sink, "%s removing client %p for no reason",
          mhclient->debug, client);
      break;
    case GST_CLIENT_STATUS_CLOSED:
      GST_DEBUG_OBJECT (sink, "%s removing client %p because of close",
          mhclient->debug, client);
      break;
    case GST_CLIENT_STATUS_REMOVED:
      GST_DEBUG_OBJECT (sink,
          "%s removing client %p because the app removed it", mhclient->debug,
          client);
      break;
    case GST_CLIENT_STATUS_SLOW:
      GST_INFO_OBJECT (sink,
          "%s removing client %p because it was too slow", mhclient->debug,
          client);
      break;
    case GST_CLIENT_STATUS_ERROR:
      GST_WARNING_OBJECT (sink,
          "%s removing client %p because of error", mhclient->debug, client);
      break;
    case GST_CLIENT_STATUS_FLUSHING:
    default:
      GST_WARNING_OBJECT (sink,
          "%s removing client %p with invalid reason %d", mhclient->debug,
          client, mhclient->status);
      break;
  }

  gst_poll_remove_fd (mfsink->fdset, &client->gfd);

  g_get_current_time (&now);
  mhclient->disconnect_time = GST_TIMEVAL_TO_TIME (now);

  /* free client buffers */
  g_slist_foreach (mhclient->sending, (GFunc) gst_mini_object_unref, NULL);
  g_slist_free (mhclient->sending);
  mhclient->sending = NULL;

  if (mhclient->caps)
    gst_caps_unref (mhclient->caps);
  mhclient->caps = NULL;

  /* unlock the mutex before signaling because the signal handler
   * might query some properties */
  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multi_fd_sink_signals[SIGNAL_CLIENT_REMOVED], 0, fd,
      mhclient->status);

  /* lock again before we remove the client completely */
  CLIENTS_LOCK (sink);

  /* fd cannot be reused in the above signal callback so we can safely
   * remove it from the hashtable here */
  if (!g_hash_table_remove (mfsink->handle_hash, &client->gfd.fd)) {
    GST_WARNING_OBJECT (sink,
        "%s error removing client %p from hash", mhclient->debug, client);
  }
  /* after releasing the lock above, the link could be invalid, more
   * precisely, the next and prev pointers could point to invalid list
   * links. One optimisation could be to add a cookie to the linked list
   * and take a shortcut when it did not change between unlocking and locking
   * our mutex. For now we just walk the list again. */
  sink->clients = g_list_remove (sink->clients, client);
  sink->clients_cookie++;

  if (fclass->removed)
    fclass->removed (mfsink, (GstMultiSinkHandle) client->gfd.fd);

  g_free (client);
  CLIENTS_UNLOCK (sink);

  /* and the fd is really gone now */
  g_signal_emit (G_OBJECT (sink),
      gst_multi_fd_sink_signals[SIGNAL_CLIENT_FD_REMOVED], 0, fd);

  CLIENTS_LOCK (sink);
}

/* handle a read on a client fd,
 * which either indicates a close or should be ignored
 * returns FALSE if some error occured or the client closed. */
static gboolean
gst_multi_fd_sink_handle_client_read (GstMultiFdSink * sink,
    GstTCPClient * client)
{
  int avail, fd;
  gboolean ret;
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

  fd = client->gfd.fd;

  if (ioctl (fd, FIONREAD, &avail) < 0)
    goto ioctl_failed;

  GST_DEBUG_OBJECT (sink, "%s select reports client read of %d bytes",
      mhclient->debug, avail);

  ret = TRUE;

  if (avail == 0) {
    /* client sent close, so remove it */
    GST_DEBUG_OBJECT (sink, "%s client asked for close, removing",
        mhclient->debug);
    mhclient->status = GST_CLIENT_STATUS_CLOSED;
    ret = FALSE;
  } else if (avail < 0) {
    GST_WARNING_OBJECT (sink, "%s avail < 0, removing", mhclient->debug);
    mhclient->status = GST_CLIENT_STATUS_ERROR;
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

      GST_DEBUG_OBJECT (sink, "%s client wants us to read %d bytes",
          mhclient->debug, to_read);

      nread = read (fd, dummy, to_read);
      if (nread < -1) {
        GST_WARNING_OBJECT (sink, "%s could not read %d bytes: %s (%d)",
            mhclient->debug, to_read, g_strerror (errno), errno);
        mhclient->status = GST_CLIENT_STATUS_ERROR;
        ret = FALSE;
        break;
      } else if (nread == 0) {
        GST_WARNING_OBJECT (sink, "%s 0 bytes in read, removing",
            mhclient->debug);
        mhclient->status = GST_CLIENT_STATUS_ERROR;
        ret = FALSE;
        break;
      }
      avail -= nread;
    }
    while (avail > 0);
  }
  return ret;

  /* ERRORS */
ioctl_failed:
  {
    GST_WARNING_OBJECT (sink, "%s ioctl failed: %s (%d)",
        mhclient->debug, g_strerror (errno), errno);
    mhclient->status = GST_CLIENT_STATUS_ERROR;
    return FALSE;
  }
}

/* queue the given buffer for the given client */
static gboolean
gst_multi_fd_sink_client_queue_buffer (GstMultiHandleSink * mhsink,
    GstMultiHandleClient * mhclient, GstBuffer * buffer)
{
  GstCaps *caps;

  /* TRUE: send them if the new caps have them */
  gboolean send_streamheader = FALSE;
  GstStructure *s;
  GstMultiFdSink *sink = GST_MULTI_FD_SINK (mhsink);

  /* before we queue the buffer, we check if we need to queue streamheader
   * buffers (because it's a new client, or because they changed) */
  caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (sink));

  if (!mhclient->caps) {
    GST_DEBUG_OBJECT (sink,
        "%s no previous caps for this client, send streamheader",
        mhclient->debug);
    send_streamheader = TRUE;
    mhclient->caps = gst_caps_ref (caps);
  } else {
    /* there were previous caps recorded, so compare */
    if (!gst_caps_is_equal (caps, mhclient->caps)) {
      const GValue *sh1, *sh2;

      /* caps are not equal, but could still have the same streamheader */
      s = gst_caps_get_structure (caps, 0);
      if (!gst_structure_has_field (s, "streamheader")) {
        /* no new streamheader, so nothing new to send */
        GST_DEBUG_OBJECT (sink,
            "%s new caps do not have streamheader, not sending",
            mhclient->debug);
      } else {
        /* there is a new streamheader */
        s = gst_caps_get_structure (mhclient->caps, 0);
        if (!gst_structure_has_field (s, "streamheader")) {
          /* no previous streamheader, so send the new one */
          GST_DEBUG_OBJECT (sink,
              "%s previous caps did not have streamheader, sending",
              mhclient->debug);
          send_streamheader = TRUE;
        } else {
          /* both old and new caps have streamheader set */
          if (!mhsink->resend_streamheader) {
            GST_DEBUG_OBJECT (sink,
                "%s asked to not resend the streamheader, not sending",
                mhclient->debug);
            send_streamheader = FALSE;
          } else {
            sh1 = gst_structure_get_value (s, "streamheader");
            s = gst_caps_get_structure (caps, 0);
            sh2 = gst_structure_get_value (s, "streamheader");
            if (gst_value_compare (sh1, sh2) != GST_VALUE_EQUAL) {
              GST_DEBUG_OBJECT (sink,
                  "%s new streamheader different from old, sending",
                  mhclient->debug);
              send_streamheader = TRUE;
            }
          }
        }
      }
    }
    /* Replace the old caps */
    gst_caps_unref (mhclient->caps);
    mhclient->caps = gst_caps_ref (caps);
  }

  if (G_UNLIKELY (send_streamheader)) {
    const GValue *sh;
    GArray *buffers;
    int i;

    GST_LOG_OBJECT (sink,
        "%s sending streamheader from caps %" GST_PTR_FORMAT,
        mhclient->debug, caps);
    s = gst_caps_get_structure (caps, 0);
    if (!gst_structure_has_field (s, "streamheader")) {
      GST_DEBUG_OBJECT (sink,
          "%s no new streamheader, so nothing to send", mhclient->debug);
    } else {
      GST_LOG_OBJECT (sink,
          "%s sending streamheader from caps %" GST_PTR_FORMAT,
          mhclient->debug, caps);
      sh = gst_structure_get_value (s, "streamheader");
      g_assert (G_VALUE_TYPE (sh) == GST_TYPE_ARRAY);
      buffers = g_value_peek_pointer (sh);
      GST_DEBUG_OBJECT (sink, "%d streamheader buffers", buffers->len);
      for (i = 0; i < buffers->len; ++i) {
        GValue *bufval;
        GstBuffer *buffer;

        bufval = &g_array_index (buffers, GValue, i);
        g_assert (G_VALUE_TYPE (bufval) == GST_TYPE_BUFFER);
        buffer = g_value_peek_pointer (bufval);
        GST_DEBUG_OBJECT (sink,
            "%s queueing streamheader buffer of length %" G_GSIZE_FORMAT,
            mhclient->debug, gst_buffer_get_size (buffer));
        gst_buffer_ref (buffer);

        mhclient->sending = g_slist_append (mhclient->sending, buffer);
      }
    }
  }

  gst_caps_unref (caps);
  caps = NULL;

  GST_LOG_OBJECT (sink, "%s queueing buffer of length %" G_GSIZE_FORMAT,
      mhclient->debug, gst_buffer_get_size (buffer));

  gst_buffer_ref (buffer);
  mhclient->sending = g_slist_append (mhclient->sending, buffer);

  return TRUE;
}

/* Handle a write on a client,
 * which indicates a read request from a client.
 *
 * For each client we maintain a queue of GstBuffers that contain the raw bytes
 * we need to send to the client.
 *
 * We first check to see if we need to send streamheaders. If so, we queue them.
 *
 * Then we run into the main loop that tries to send as many buffers as
 * possible. It will first exhaust the mhclient->sending queue and if the queue
 * is empty, it will pick a buffer from the global queue.
 *
 * Sending the buffers from the mhclient->sending queue is basically writing
 * the bytes to the socket and maintaining a count of the bytes that were
 * sent. When the buffer is completely sent, it is removed from the
 * mhclient->sending queue and we try to pick a new buffer for sending.
 *
 * When the sending returns a partial buffer we stop sending more data as
 * the next send operation could block.
 *
 * This functions returns FALSE if some error occured.
 */
static gboolean
gst_multi_fd_sink_handle_client_write (GstMultiFdSink * sink,
    GstTCPClient * client)
{
  GstMultiSinkHandle handle = (GstMultiSinkHandle) client->gfd.fd;
  gboolean more;
  gboolean flushing;
  GstClockTime now;
  GTimeVal nowtv;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
  int fd = handle.fd;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  flushing = mhclient->status == GST_CLIENT_STATUS_FLUSHING;

  more = TRUE;
  do {
    gint maxsize;

    if (!mhclient->sending) {
      /* client is not working on a buffer */
      if (mhclient->bufpos == -1) {
        /* client is too fast, remove from write queue until new buffer is
         * available */
        // FIXME: specific
        gst_poll_fd_ctl_write (sink->fdset, &client->gfd, FALSE);
        //
        /* if we flushed out all of the client buffers, we can stop */
        if (mhclient->flushcount == 0)
          goto flushed;

        return TRUE;
      } else {
        /* client can pick a buffer from the global queue */
        GstBuffer *buf;
        GstClockTime timestamp;

        /* for new connections, we need to find a good spot in the
         * bufqueue to start streaming from */
        if (mhclient->new_connection && !flushing) {
          gint position = gst_multi_handle_sink_new_client (mhsink, mhclient);

          if (position >= 0) {
            /* we got a valid spot in the queue */
            mhclient->new_connection = FALSE;
            mhclient->bufpos = position;
          } else {
            /* cannot send data to this client yet */
            // FIXME: specific
            gst_poll_fd_ctl_write (sink->fdset, &client->gfd, FALSE);
            return TRUE;
          }
        }

        /* we flushed all remaining buffers, no need to get a new one */
        if (mhclient->flushcount == 0)
          goto flushed;

        /* grab buffer */
        buf = g_array_index (mhsink->bufqueue, GstBuffer *, mhclient->bufpos);
        mhclient->bufpos--;

        /* update stats */
        timestamp = GST_BUFFER_TIMESTAMP (buf);
        if (mhclient->first_buffer_ts == GST_CLOCK_TIME_NONE)
          mhclient->first_buffer_ts = timestamp;
        if (timestamp != -1)
          mhclient->last_buffer_ts = timestamp;

        /* decrease flushcount */
        if (mhclient->flushcount != -1)
          mhclient->flushcount--;

        GST_LOG_OBJECT (sink, "%s client %p at position %d",
            mhclient->debug, client, mhclient->bufpos);

        /* queueing a buffer will ref it */
        mhsinkclass->client_queue_buffer (mhsink, mhclient, buf);

        /* need to start from the first byte for this new buffer */
        mhclient->bufoffset = 0;
      }
    }

    /* see if we need to send something */
    if (mhclient->sending) {
      ssize_t wrote;
      GstBuffer *head;
      GstMapInfo info;
      guint8 *data;

      /* pick first buffer from list */
      head = GST_BUFFER (mhclient->sending->data);

      g_assert (gst_buffer_map (head, &info, GST_MAP_READ));
      data = info.data;
      maxsize = info.size - mhclient->bufoffset;

      // FIXME: specific
      /* try to write the complete buffer */
#ifdef MSG_NOSIGNAL
#define FLAGS MSG_NOSIGNAL
#else
#define FLAGS 0
#endif
      if (client->is_socket) {
        wrote = send (fd, data + mhclient->bufoffset, maxsize, FLAGS);
      } else {
        wrote = write (fd, data + mhclient->bufoffset, maxsize);
      }
      gst_buffer_unmap (head, &info);

      if (wrote < 0) {
        /* hmm error.. */
        if (errno == EAGAIN) {
          /* nothing serious, resource was unavailable, try again later */
          more = FALSE;
        } else if (errno == ECONNRESET) {
          goto connection_reset;
        } else {
          goto write_error;
        }
      } else {
        if (wrote < maxsize) {
          /* partial write means that the client cannot read more and we should
           * stop sending more */
          GST_LOG_OBJECT (sink,
              "partial write on %d of %" G_GSSIZE_FORMAT " bytes",
              mhclient->debug, wrote);
          mhclient->bufoffset += wrote;
          more = FALSE;
        } else {
          /* complete buffer was written, we can proceed to the next one */
          mhclient->sending = g_slist_remove (mhclient->sending, head);
          gst_buffer_unref (head);
          /* make sure we start from byte 0 for the next buffer */
          mhclient->bufoffset = 0;
        }
        /* update stats */
        mhclient->bytes_sent += wrote;
        mhclient->last_activity_time = now;
        mhsink->bytes_served += wrote;
      }
    }
  } while (more);

  return TRUE;

  /* ERRORS */
flushed:
  {
    GST_DEBUG_OBJECT (sink, "%s flushed, removing", mhclient->debug);
    mhclient->status = GST_CLIENT_STATUS_REMOVED;
    return FALSE;
  }
connection_reset:
  {
    GST_DEBUG_OBJECT (sink, "%s connection reset by peer, removing",
        mhclient->debug);
    mhclient->status = GST_CLIENT_STATUS_CLOSED;
    return FALSE;
  }
write_error:
  {
    GST_WARNING_OBJECT (sink,
        "%s could not write, removing client: %s (%d)", mhclient->debug,
        g_strerror (errno), errno);
    mhclient->status = GST_CLIENT_STATUS_ERROR;
    return FALSE;
  }
}

/* Queue a buffer on the global queue.
 *
 * This function adds the buffer to the front of a GArray. It removes the
 * tail buffer if the max queue size is exceeded, unreffing the queued buffer.
 * Note that unreffing the buffer is not a problem as clients who
 * started writing out this buffer will still have a reference to it in the
 * mhclient->sending queue.
 *
 * After adding the buffer, we update all client positions in the queue. If
 * a client moves over the soft max, we start the recovery procedure for this
 * slow client. If it goes over the hard max, it is put into the slow list
 * and removed.
 *
 * Special care is taken of clients that were waiting for a new buffer (they
 * had a position of -1) because they can proceed after adding this new buffer.
 * This is done by adding the client back into the write fd_set and signaling
 * the select thread that the fd_set changed.
 */
static void
gst_multi_fd_sink_queue_buffer (GstMultiHandleSink * mhsink, GstBuffer * buffer)
{
  GList *clients, *next;
  gint queuelen;
  gboolean need_signal = FALSE;
  gint max_buffer_usage;
  gint i;
  GTimeVal nowtv;
  GstClockTime now;
  gint max_buffers, soft_max_buffers;
  guint cookie;
  GstMultiFdSink *sink = GST_MULTI_FD_SINK (mhsink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (mhsink);
  /* add buffer to queue */
  g_array_prepend_val (mhsink->bufqueue, buffer);
  queuelen = mhsink->bufqueue->len;

  if (mhsink->units_max > 0)
    max_buffers = get_buffers_max (mhsink, mhsink->units_max);
  else
    max_buffers = -1;

  if (mhsink->units_soft_max > 0)
    soft_max_buffers = get_buffers_max (mhsink, mhsink->units_soft_max);
  else
    soft_max_buffers = -1;
  GST_LOG_OBJECT (sink, "Using max %d, softmax %d", max_buffers,
      soft_max_buffers);

  /* then loop over the clients and update the positions */
  max_buffer_usage = 0;

restart:
  cookie = mhsink->clients_cookie;
  for (clients = mhsink->clients; clients; clients = next) {
    GstTCPClient *client;
    GstMultiHandleClient *mhclient;

    if (cookie != mhsink->clients_cookie) {
      GST_DEBUG_OBJECT (sink, "Clients cookie outdated, restarting");
      goto restart;
    }

    client = (GstTCPClient *) clients->data;
    mhclient = (GstMultiHandleClient *) client;
    next = g_list_next (clients);

    mhclient->bufpos++;
    GST_LOG_OBJECT (sink, "%s client %p at position %d",
        mhclient->debug, client, mhclient->bufpos);
    /* check soft max if needed, recover client */
    if (soft_max_buffers > 0 && mhclient->bufpos >= soft_max_buffers) {
      gint newpos;

      newpos = gst_multi_handle_sink_recover_client (mhsink, mhclient);
      if (newpos != mhclient->bufpos) {
        mhclient->dropped_buffers += mhclient->bufpos - newpos;
        mhclient->bufpos = newpos;
        mhclient->discont = TRUE;
        GST_INFO_OBJECT (sink, "%s client %p position reset to %d",
            mhclient->debug, client, mhclient->bufpos);
      } else {
        GST_INFO_OBJECT (sink,
            "%s client %p not recovering position", mhclient->debug, client);
      }
    }
    /* check hard max and timeout, remove client */
    if ((max_buffers > 0 && mhclient->bufpos >= max_buffers) ||
        (mhsink->timeout > 0
            && now - mhclient->last_activity_time > mhsink->timeout)) {
      /* remove client */
      GST_WARNING_OBJECT (sink, "%s client %p is too slow, removing",
          mhclient->debug, client);
      /* remove the client, the fd set will be cleared and the select thread
       * will be signaled */
      mhclient->status = GST_CLIENT_STATUS_SLOW;
      /* set client to invalid position while being removed */
      mhclient->bufpos = -1;
      mhsinkclass->remove_client_link (mhsink, clients);
      need_signal = TRUE;
      continue;
    } else if (mhclient->bufpos == 0 || mhclient->new_connection) {
      /* can send data to this client now. need to signal the select thread that
       * the fd_set changed */
      gst_poll_fd_ctl_write (sink->fdset, &client->gfd, TRUE);
      need_signal = TRUE;
    }
    /* keep track of maximum buffer usage */
    if (mhclient->bufpos > max_buffer_usage) {
      max_buffer_usage = mhclient->bufpos;
    }
  }

  /* make sure we respect bytes-min, buffers-min and time-min when they are set */
  {
    gint usage, max;

    GST_LOG_OBJECT (sink,
        "extending queue %d to respect time_min %" GST_TIME_FORMAT
        ", bytes_min %d, buffers_min %d", max_buffer_usage,
        GST_TIME_ARGS (mhsink->time_min), mhsink->bytes_min,
        mhsink->buffers_min);

    /* get index where the limits are ok, we don't really care if all limits
     * are ok, we just queue as much as we need. We also don't compare against
     * the max limits. */
    find_limits (mhsink, &usage, mhsink->bytes_min, mhsink->buffers_min,
        mhsink->time_min, &max, -1, -1, -1);

    max_buffer_usage = MAX (max_buffer_usage, usage + 1);
    GST_LOG_OBJECT (sink, "extended queue to %d", max_buffer_usage);
  }

  /* now look for sync points and make sure there is at least one
   * sync point in the queue. We only do this if the LATEST_KEYFRAME or 
   * BURST_KEYFRAME mode is selected */
  if (mhsink->def_sync_method == GST_SYNC_METHOD_LATEST_KEYFRAME ||
      mhsink->def_sync_method == GST_SYNC_METHOD_BURST_KEYFRAME) {
    /* no point in searching beyond the queue length */
    gint limit = queuelen;
    GstBuffer *buf;

    /* no point in searching beyond the soft-max if any. */
    if (soft_max_buffers > 0) {
      limit = MIN (limit, soft_max_buffers);
    }
    GST_LOG_OBJECT (sink,
        "extending queue to include sync point, now at %d, limit is %d",
        max_buffer_usage, limit);
    for (i = 0; i < limit; i++) {
      buf = g_array_index (mhsink->bufqueue, GstBuffer *, i);
      if (is_sync_frame (mhsink, buf)) {
        /* found a sync frame, now extend the buffer usage to
         * include at least this frame. */
        max_buffer_usage = MAX (max_buffer_usage, i);
        break;
      }
    }
    GST_LOG_OBJECT (sink, "max buffer usage is now %d", max_buffer_usage);
  }

  GST_LOG_OBJECT (sink, "len %d, usage %d", queuelen, max_buffer_usage);

  /* nobody is referencing units after max_buffer_usage so we can
   * remove them from the queue. We remove them in reverse order as
   * this is the most optimal for GArray. */
  for (i = queuelen - 1; i > max_buffer_usage; i--) {
    GstBuffer *old;

    /* queue exceeded max size */
    queuelen--;
    old = g_array_index (mhsink->bufqueue, GstBuffer *, i);
    mhsink->bufqueue = g_array_remove_index (mhsink->bufqueue, i);

    /* unref tail buffer */
    gst_buffer_unref (old);
  }
  /* save for stats */
  mhsink->buffers_queued = max_buffer_usage;
  CLIENTS_UNLOCK (sink);

  /* and send a signal to thread if fd_set changed */
  if (need_signal) {
    gst_poll_restart (sink->fdset);
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
gst_multi_fd_sink_handle_clients (GstMultiFdSink * sink)
{
  int result;
  GList *clients, *next;
  gboolean try_again;
  GstMultiFdSinkClass *fclass;
  guint cookie;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  int fd;


  fclass = GST_MULTI_FD_SINK_GET_CLASS (sink);

  do {
    try_again = FALSE;

    /* check for:
     * - server socket input (ie, new client connections)
     * - client socket input (ie, clients saying goodbye)
     * - client socket output (ie, client reads)          */
    GST_LOG_OBJECT (sink, "waiting on action on fdset");

    result =
        gst_poll_wait (sink->fdset,
        mhsink->timeout != 0 ? mhsink->timeout : GST_CLOCK_TIME_NONE);

    /* Handle the special case in which the sink is not receiving more buffers
     * and will not disconnect inactive client in the streaming thread. */
    if (G_UNLIKELY (result == 0)) {
      GstClockTime now;
      GTimeVal nowtv;

      g_get_current_time (&nowtv);
      now = GST_TIMEVAL_TO_TIME (nowtv);

      CLIENTS_LOCK (sink);
      for (clients = mhsink->clients; clients; clients = next) {
        GstTCPClient *client;
        GstMultiHandleClient *mhclient;

        client = (GstTCPClient *) clients->data;
        mhclient = (GstMultiHandleClient *) client;
        next = g_list_next (clients);
        if (mhsink->timeout > 0
            && now - mhclient->last_activity_time > mhsink->timeout) {
          mhclient->status = GST_CLIENT_STATUS_SLOW;
          mhsinkclass->remove_client_link (mhsink, clients);
        }
      }
      CLIENTS_UNLOCK (sink);
      return;
    } else if (result < 0) {
      GST_WARNING_OBJECT (sink, "wait failed: %s (%d)", g_strerror (errno),
          errno);
      if (errno == EBADF) {
        /* ok, so one or more of the fds is invalid. We loop over them to find
         * the ones that give an error to the F_GETFL fcntl. */
        CLIENTS_LOCK (sink);
      restart:
        cookie = mhsink->clients_cookie;
        for (clients = mhsink->clients; clients; clients = next) {
          GstTCPClient *client;
          GstMultiHandleClient *mhclient;
          long flags;
          int res;

          if (cookie != mhsink->clients_cookie) {
            GST_DEBUG_OBJECT (sink, "Cookie changed finding bad fd");
            goto restart;
          }

          client = (GstTCPClient *) clients->data;
          mhclient = (GstMultiHandleClient *) client;
          next = g_list_next (clients);

          fd = client->gfd.fd;

          res = fcntl (fd, F_GETFL, &flags);
          if (res == -1) {
            GST_WARNING_OBJECT (sink, "fnctl failed for %d, removing: %s (%d)",
                fd, g_strerror (errno), errno);
            if (errno == EBADF) {
              mhclient->status = GST_CLIENT_STATUS_ERROR;
              /* releases the CLIENTS lock */
              mhsinkclass->remove_client_link (mhsink, clients);
            }
          }
        }
        CLIENTS_UNLOCK (sink);
        /* after this, go back in the select loop as the read/writefds
         * are not valid */
        try_again = TRUE;
      } else if (errno == EINTR) {
        /* interrupted system call, just redo the wait */
        try_again = TRUE;
      } else if (errno == EBUSY) {
        /* the call to gst_poll_wait() was flushed */
        return;
      } else {
        /* this is quite bad... */
        GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
            ("select failed: %s (%d)", g_strerror (errno), errno));
        return;
      }
    } else {
      GST_LOG_OBJECT (sink, "wait done: %d sockets with events", result);
    }
  } while (try_again);

  /* subclasses can check fdset with this virtual function */
  if (fclass->wait)
    fclass->wait (sink, sink->fdset);

  /* Check the clients */
  CLIENTS_LOCK (sink);

restart2:
  cookie = mhsink->clients_cookie;
  for (clients = mhsink->clients; clients; clients = next) {
    GstTCPClient *client;
    GstMultiHandleClient *mhclient;

    if (mhsink->clients_cookie != cookie) {
      GST_DEBUG_OBJECT (sink, "Restarting loop, cookie out of date");
      goto restart2;
    }

    client = (GstTCPClient *) clients->data;
    mhclient = (GstMultiHandleClient *) client;
    next = g_list_next (clients);

    if (mhclient->status != GST_CLIENT_STATUS_FLUSHING
        && mhclient->status != GST_CLIENT_STATUS_OK) {
      mhsinkclass->remove_client_link (mhsink, clients);
      continue;
    }

    if (gst_poll_fd_has_closed (sink->fdset, &client->gfd)) {
      mhclient->status = GST_CLIENT_STATUS_CLOSED;
      mhsinkclass->remove_client_link (mhsink, clients);
      continue;
    }
    if (gst_poll_fd_has_error (sink->fdset, &client->gfd)) {
      GST_WARNING_OBJECT (sink, "gst_poll_fd_has_error for %d", client->gfd.fd);
      mhclient->status = GST_CLIENT_STATUS_ERROR;
      mhsinkclass->remove_client_link (mhsink, clients);
      continue;
    }
    if (gst_poll_fd_can_read (sink->fdset, &client->gfd)) {
      /* handle client read */
      if (!gst_multi_fd_sink_handle_client_read (sink, client)) {
        mhsinkclass->remove_client_link (mhsink, clients);
        continue;
      }
    }
    if (gst_poll_fd_can_write (sink->fdset, &client->gfd)) {
      /* handle client write */
      if (!gst_multi_fd_sink_handle_client_write (sink, client)) {
        mhsinkclass->remove_client_link (mhsink, clients);
        continue;
      }
    }
  }
  CLIENTS_UNLOCK (sink);
}

/* we handle the client communication in another thread so that we do not block
 * the gstreamer thread while we select() on the client fds */
static gpointer
gst_multi_fd_sink_thread (GstMultiHandleSink * mhsink)
{
  GstMultiFdSink *sink = GST_MULTI_FD_SINK (mhsink);

  while (mhsink->running) {
    gst_multi_fd_sink_handle_clients (sink);
  }
  return NULL;
}

static void
gst_multi_fd_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  multifdsink = GST_MULTI_FD_SINK (object);

  switch (prop_id) {
    case PROP_MODE:
      multifdsink->mode = g_value_get_enum (value);
      break;
    case PROP_HANDLE_READ:
      multifdsink->handle_read = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_fd_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  multifdsink = GST_MULTI_FD_SINK (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, multifdsink->mode);
      break;
    case PROP_HANDLE_READ:
      g_value_set_boolean (value, multifdsink->handle_read);
      break;
    case PROP_NUM_FDS:
      g_value_set_uint (value, g_hash_table_size (multifdsink->handle_hash));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_multi_fd_sink_start_pre (GstMultiHandleSink * mhsink)
{
  GstMultiFdSink *mfsink = GST_MULTI_FD_SINK (mhsink);

  GST_INFO_OBJECT (mfsink, "starting in mode %d", mfsink->mode);
  if ((mfsink->fdset = gst_poll_new (TRUE)) == NULL)
    goto socket_pair;

  return TRUE;

  /* ERRORS */
socket_pair:
  {
    GST_ELEMENT_ERROR (mfsink, RESOURCE, OPEN_READ_WRITE, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static gboolean
multifdsink_hash_remove (gpointer key, gpointer value, gpointer data)
{
  return TRUE;
}

static void
gst_multi_fd_sink_stop_pre (GstMultiHandleSink * mhsink)
{
  GstMultiFdSink *mfsink = GST_MULTI_FD_SINK (mhsink);

  gst_poll_set_flushing (mfsink->fdset, TRUE);

}

static void
gst_multi_fd_sink_stop_post (GstMultiHandleSink * mhsink)
{
  GstMultiFdSink *mfsink = GST_MULTI_FD_SINK (mhsink);

  if (mfsink->fdset) {
    gst_poll_free (mfsink->fdset);
    mfsink->fdset = NULL;
  }
  g_hash_table_foreach_remove (mfsink->handle_hash, multifdsink_hash_remove,
      mfsink);
}
