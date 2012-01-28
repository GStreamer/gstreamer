/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2006 Wim Taymans <wim at fluendo dot com>
 * Copyright (C) <2011> Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-multisocketsink
 * @see_also: tcpserversink
 *
 * This plugin writes incoming data to a set of file descriptors. The
 * file descriptors can be added to multisocketsink by emitting the #GstMultiSocketSink::add signal. 
 * For each descriptor added, the #GstMultiSocketSink::client-added signal will be called.
 *
 * As of version 0.10.8, a client can also be added with the #GstMultiSocketSink::add-full signal
 * that allows for more control over what and how much data a client 
 * initially receives.
 *
 * Clients can be removed from multisocketsink by emitting the #GstMultiSocketSink::remove signal. For
 * each descriptor removed, the #GstMultiSocketSink::client-removed signal will be called. The
 * #GstMultiSocketSink::client-removed signal can also be fired when multisocketsink decides that a
 * client is not active anymore or, depending on the value of the
 * #GstMultiSocketSink:recover-policy property, if the client is reading too slowly.
 * In all cases, multisocketsink will never close a file descriptor itself.
 * The user of multisocketsink is responsible for closing all file descriptors.
 * This can for example be done in response to the #GstMultiSocketSink::client-fd-removed signal.
 * Note that multisocketsink still has a reference to the file descriptor when the
 * #GstMultiSocketSink::client-removed signal is emitted, so that "get-stats" can be performed on
 * the descriptor; it is therefore not safe to close the file descriptor in
 * the #GstMultiSocketSink::client-removed signal handler, and you should use the 
 * #GstMultiSocketSink::client-fd-removed signal to safely close the fd.
 *
 * Multisocketsink internally keeps a queue of the incoming buffers and uses a
 * separate thread to send the buffers to the clients. This ensures that no
 * client write can block the pipeline and that clients can read with different
 * speeds.
 *
 * When adding a client to multisocketsink, the #GstMultiSocketSink:sync-method property will define
 * which buffer in the queued buffers will be sent first to the client. Clients 
 * can be sent the most recent buffer (which might not be decodable by the 
 * client if it is not a keyframe), the next keyframe received in 
 * multisocketsink (which can take some time depending on the keyframe rate), or the
 * last received keyframe (which will cause a simple burst-on-connect). 
 * Multisocketsink will always keep at least one keyframe in its internal buffers
 * when the sync-mode is set to latest-keyframe.
 *
 * As of version 0.10.8, there are additional values for the #GstMultiSocketSink:sync-method 
 * property to allow finer control over burst-on-connect behaviour. By selecting
 * the 'burst' method a minimum burst size can be chosen, 'burst-keyframe'
 * additionally requires that the burst begin with a keyframe, and 
 * 'burst-with-keyframe' attempts to burst beginning with a keyframe, but will
 * prefer a minimum burst size even if it requires not starting with a keyframe.
 *
 * Multisocketsink can be instructed to keep at least a minimum amount of data
 * expressed in time or byte units in its internal queues with the 
 * #GstMultiSocketSink:time-min and #GstMultiSocketSink:bytes-min properties respectively.
 * These properties are useful if the application adds clients with the 
 * #GstMultiSocketSink::add-full signal to make sure that a burst connect can
 * actually be honored. 
 *
 * When streaming data, clients are allowed to read at a different rate than
 * the rate at which multisocketsink receives data. If the client is reading too
 * fast, no data will be send to the client until multisocketsink receives more
 * data. If the client, however, reads too slowly, data for that client will be 
 * queued up in multisocketsink. Two properties control the amount of data 
 * (buffers) that is queued in multisocketsink: #GstMultiSocketSink:buffers-max and 
 * #GstMultiSocketSink:buffers-soft-max. A client that falls behind by
 * #GstMultiSocketSink:buffers-max is removed from multisocketsink forcibly.
 *
 * A client with a lag of at least #GstMultiSocketSink:buffers-soft-max enters the recovery
 * procedure which is controlled with the #GstMultiSocketSink:recover-policy property.
 * A recover policy of NONE will do nothing, RESYNC_LATEST will send the most recently
 * received buffer as the next buffer for the client, RESYNC_SOFT_LIMIT
 * positions the client to the soft limit in the buffer queue and
 * RESYNC_KEYFRAME positions the client at the most recent keyframe in the
 * buffer queue.
 *
 * multisocketsink will by default synchronize on the clock before serving the 
 * buffers to the clients. This behaviour can be disabled by setting the sync 
 * property to FALSE. Multisocketsink will by default not do QoS and will never
 * drop late buffers.
 *
 * Last reviewed on 2006-09-12 (0.10.10)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include <string.h>

#include "gstmultisocketsink.h"
#include "gsttcp-marshal.h"

#ifndef G_OS_WIN32
#include <netinet/in.h>
#endif

#define NOT_IMPLEMENTED 0

GST_DEBUG_CATEGORY_STATIC (multisocketsink_debug);
#define GST_CAT_DEFAULT (multisocketsink_debug)

/* MultiSocketSink signals and args */
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
  SIGNAL_CLIENT_SOCKET_REMOVED,

  LAST_SIGNAL
};

enum
{
  PROP_0,

  PROP_NUM_SOCKETS,

  PROP_LAST
};

static void gst_multi_socket_sink_finalize (GObject * object);

static void gst_multi_socket_sink_stop_pre (GstMultiHandleSink * mhsink);
static void gst_multi_socket_sink_stop_post (GstMultiHandleSink * mhsink);
static gboolean gst_multi_socket_sink_start_pre (GstMultiHandleSink * mhsink);
static gpointer gst_multi_socket_sink_thread (GstMultiHandleSink * mhsink);
static void gst_multi_socket_sink_queue_buffer (GstMultiHandleSink * mhsink,
    GstBuffer * buffer);
static gboolean gst_multi_socket_sink_client_queue_buffer (GstMultiHandleSink *
    mhsink, GstMultiHandleClient * mhclient, GstBuffer * buffer);
static int gst_multi_socket_sink_client_get_fd (GstMultiHandleClient * client);
static void gst_multi_socket_sink_handle_debug (GstMultiSinkHandle handle,
    gchar debug[30]);


static void gst_multi_socket_sink_remove_client_link (GstMultiHandleSink * sink,
    GList * link);
static gboolean gst_multi_socket_sink_socket_condition (GstMultiSinkHandle
    handle, GIOCondition condition, GstMultiSocketSink * sink);

static gboolean gst_multi_socket_sink_unlock (GstBaseSink * bsink);
static gboolean gst_multi_socket_sink_unlock_stop (GstBaseSink * bsink);

static void gst_multi_socket_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_socket_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_multi_socket_sink_parent_class parent_class
G_DEFINE_TYPE (GstMultiSocketSink, gst_multi_socket_sink,
    GST_TYPE_MULTI_HANDLE_SINK);

static guint gst_multi_socket_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_socket_sink_class_init (GstMultiSocketSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstMultiHandleSinkClass *gstmultihandlesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstmultihandlesink_class = (GstMultiHandleSinkClass *) klass;

  gobject_class->set_property = gst_multi_socket_sink_set_property;
  gobject_class->get_property = gst_multi_socket_sink_get_property;
  gobject_class->finalize = gst_multi_socket_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_NUM_SOCKETS,
      g_param_spec_uint ("num-sockets", "Number of sockets",
          "The current number of client sockets",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiSocketSink::add:
   * @gstmultisocketsink: the multisocketsink element to emit this signal on
   * @socket:             the socket to add to multisocketsink
   *
   * Hand the given open socket to multisocketsink to write to.
   */
  gst_multi_socket_sink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiSocketSinkClass, add), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_SOCKET);
  /**
   * GstMultiSocketSink::add-full:
   * @gstmultisocketsink: the multisocketsink element to emit this signal on
   * @socket:         the socket to add to multisocketsink
   * @sync:           the sync method to use
   * @format_min:     the format of @value_min
   * @value_min:      the minimum amount of data to burst expressed in
   *                  @format_min units.
   * @format_max:     the format of @value_max
   * @value_max:      the maximum amount of data to burst expressed in
   *                  @format_max units.
   *
   * Hand the given open socket to multisocketsink to write to and
   * specify the burst parameters for the new connection.
   */
  gst_multi_socket_sink_signals[SIGNAL_ADD_BURST] =
      g_signal_new ("add-full", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiSocketSinkClass, add_full), NULL, NULL,
      gst_tcp_marshal_VOID__OBJECT_ENUM_ENUM_UINT64_ENUM_UINT64, G_TYPE_NONE, 6,
      G_TYPE_SOCKET, GST_TYPE_SYNC_METHOD, GST_TYPE_FORMAT, G_TYPE_UINT64,
      GST_TYPE_FORMAT, G_TYPE_UINT64);
  /**
   * GstMultiSocketSink::remove:
   * @gstmultisocketsink: the multisocketsink element to emit this signal on
   * @socket:             the socket to remove from multisocketsink
   *
   * Remove the given open socket from multisocketsink.
   */
  gst_multi_socket_sink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiSocketSinkClass, remove), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_SOCKET);
  /**
   * GstMultiSocketSink::remove-flush:
   * @gstmultisocketsink: the multisocketsink element to emit this signal on
   * @socket:             the socket to remove from multisocketsink
   *
   * Remove the given open socket from multisocketsink after flushing all
   * the pending data to the socket.
   */
  gst_multi_socket_sink_signals[SIGNAL_REMOVE_FLUSH] =
      g_signal_new ("remove-flush", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiSocketSinkClass, remove_flush), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_SOCKET);

  /**
   * GstMultiSocketSink::get-stats:
   * @gstmultisocketsink: the multisocketsink element to emit this signal on
   * @socket:             the socket to get stats of from multisocketsink
   *
   * Get statistics about @socket. This function returns a GstStructure.
   *
   * Returns: a GstStructure with the statistics. The structure contains
   *     values that represent: total number of bytes sent, time
   *     when the client was added, time when the client was
   *     disconnected/removed, time the client is/was active, last activity
   *     time (in epoch seconds), number of buffers dropped.
   *     All times are expressed in nanoseconds (GstClockTime).
   */
  gst_multi_socket_sink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiSocketSinkClass, get_stats), NULL, NULL,
      gst_tcp_marshal_BOXED__OBJECT, GST_TYPE_STRUCTURE, 1, G_TYPE_SOCKET);

  /**
   * GstMultiSocketSink::client-added:
   * @gstmultisocketsink: the multisocketsink element that emitted this signal
   * @socket:             the socket that was added to multisocketsink
   *
   * The given socket was added to multisocketsink. This signal will
   * be emitted from the streaming thread so application should be prepared
   * for that.
   */
  gst_multi_socket_sink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiSocketSinkClass,
          client_added), NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, G_TYPE_OBJECT);
  /**
   * GstMultiSocketSink::client-removed:
   * @gstmultisocketsink: the multisocketsink element that emitted this signal
   * @socket:             the socket that is to be removed from multisocketsink
   * @status:             the reason why the client was removed
   *
   * The given socket is about to be removed from multisocketsink. This
   * signal will be emitted from the streaming thread so applications should
   * be prepared for that.
   *
   * @gstmultisocketsink still holds a handle to @socket so it is possible to call
   * the get-stats signal from this callback. For the same reason it is
   * not safe to close() and reuse @socket in this callback.
   */
  gst_multi_socket_sink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiSocketSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__OBJECT_ENUM,
      G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CLIENT_STATUS);
  /**
   * GstMultiSocketSink::client-socket-removed:
   * @gstmultisocketsink: the multisocketsink element that emitted this signal
   * @socket:             the socket that was removed from multisocketsink
   *
   * The given socket was removed from multisocketsink. This signal will
   * be emitted from the streaming thread so applications should be prepared
   * for that.
   *
   * In this callback, @gstmultisocketsink has removed all the information
   * associated with @socket and it is therefore not possible to call get-stats
   * with @socket. It is however safe to close() and reuse @fd in the callback.
   *
   * Since: 0.10.7
   */
  gst_multi_socket_sink_signals[SIGNAL_CLIENT_SOCKET_REMOVED] =
      g_signal_new ("client-socket-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiSocketSinkClass,
          client_socket_removed), NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, G_TYPE_SOCKET);

  gst_element_class_set_details_simple (gstelement_class,
      "Multi socket sink", "Sink/Network",
      "Send data to multiple sockets",
      "Thomas Vander Stichele <thomas at apestaart dot org>, "
      "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_unlock_stop);

  gstmultihandlesink_class->stop_pre =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_stop_pre);
  gstmultihandlesink_class->stop_post =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_stop_post);
  gstmultihandlesink_class->start_pre =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_start_pre);
  gstmultihandlesink_class->thread =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_thread);
  gstmultihandlesink_class->queue_buffer =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_queue_buffer);
  gstmultihandlesink_class->client_queue_buffer =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_client_queue_buffer);
  gstmultihandlesink_class->client_get_fd =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_client_get_fd);
  gstmultihandlesink_class->handle_debug =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_handle_debug);

  gstmultihandlesink_class->remove_client_link =
      GST_DEBUG_FUNCPTR (gst_multi_socket_sink_remove_client_link);

  klass->add = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_add);
  klass->add_full = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_add_full);
  klass->remove = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_remove);
  klass->remove_flush = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_remove_flush);
  klass->get_stats = GST_DEBUG_FUNCPTR (gst_multi_socket_sink_get_stats);

  GST_DEBUG_CATEGORY_INIT (multisocketsink_debug, "multisocketsink", 0,
      "Multi socket sink");
}

static void
gst_multi_socket_sink_init (GstMultiSocketSink * this)
{
  this->handle_hash = g_hash_table_new (g_direct_hash, g_int_equal);

  this->cancellable = g_cancellable_new ();
}

static void
gst_multi_socket_sink_finalize (GObject * object)
{
  GstMultiSocketSink *this = GST_MULTI_SOCKET_SINK (object);

  g_hash_table_destroy (this->handle_hash);
  if (this->cancellable) {
    g_object_unref (this->cancellable);
    this->cancellable = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static int
gst_multi_socket_sink_client_get_fd (GstMultiHandleClient * client)
{
  return g_socket_get_fd (client->handle.socket);
}

static void
gst_multi_socket_sink_handle_debug (GstMultiSinkHandle handle, gchar debug[30])
{
  g_snprintf (debug, 30, "[socket %p]", handle.socket);
}


/* "add-full" signal implementation */
void
gst_multi_socket_sink_add_full (GstMultiSocketSink * sink,
    GstMultiSinkHandle handle, GstSyncMethod sync_method, GstFormat min_format,
    guint64 min_value, GstFormat max_format, guint64 max_value)
{
  GstSocketClient *client;
  GstMultiHandleClient *mhclient;
  GList *clink;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  gchar debug[30];
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  // FIXME: remove assert
  g_assert (G_IS_SOCKET (handle.socket));

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
  client = g_new0 (GstSocketClient, 1);
  mhclient = (GstMultiHandleClient *) client;
  gst_multi_handle_sink_client_init (mhclient, sync_method);
  strncpy (mhclient->debug, debug, 30);
  mhclient->handle.socket = G_SOCKET (g_object_ref (handle.socket));

  mhclient->burst_min_format = min_format;
  mhclient->burst_min_value = min_value;
  mhclient->burst_max_format = max_format;
  mhclient->burst_max_value = max_value;

  CLIENTS_LOCK (sink);

  /* check the hash to find a duplicate fd */
  clink = g_hash_table_lookup (sink->handle_hash, handle.socket);
  if (clink != NULL)
    goto duplicate;

  /* we can add the fd now */
  clink = mhsink->clients = g_list_prepend (mhsink->clients, client);
  g_hash_table_insert (sink->handle_hash, handle.socket, clink);
  mhsink->clients_cookie++;

  /* set the socket to non blocking */
  g_socket_set_blocking (handle.socket, FALSE);

  /* we always read from a client */
  if (sink->main_context) {
    client->source =
        g_socket_create_source (mhclient->handle.socket,
        G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP, sink->cancellable);
    g_source_set_callback (client->source,
        (GSourceFunc) gst_multi_socket_sink_socket_condition,
        gst_object_ref (sink), (GDestroyNotify) gst_object_unref);
    g_source_attach (client->source, sink->main_context);
  }

  gst_multi_handle_sink_setup_dscp_client (mhsink, mhclient);

  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multi_socket_sink_signals[SIGNAL_CLIENT_ADDED], 0, handle);

  return;

  /* errors */
wrong_limits:
  {
    GST_WARNING_OBJECT (sink,
        "%s wrong values min =%" G_GUINT64_FORMAT ", max=%"
        G_GUINT64_FORMAT ", format %d specified when adding client",
        mhclient->debug, min_value, max_value, min_format);
    return;
  }
duplicate:
  {
    mhclient->status = GST_CLIENT_STATUS_DUPLICATE;
    CLIENTS_UNLOCK (sink);
    GST_WARNING_OBJECT (sink, "%s duplicate client found, refusing",
        mhclient->debug);
    g_signal_emit (G_OBJECT (sink),
        gst_multi_socket_sink_signals[SIGNAL_CLIENT_REMOVED], 0, handle,
        mhclient->status);
    g_free (client);
    return;
  }
}

/* "add" signal implementation */
void
gst_multi_socket_sink_add (GstMultiSocketSink * sink, GstMultiSinkHandle handle)
{
  GstMultiHandleSink *mhsink;

  mhsink = GST_MULTI_HANDLE_SINK (sink);
  gst_multi_socket_sink_add_full (sink, handle, mhsink->def_sync_method,
      mhsink->def_burst_format, mhsink->def_burst_value,
      mhsink->def_burst_format, -1);
}

/* "remove" signal implementation */
void
gst_multi_socket_sink_remove (GstMultiSocketSink * sink,
    GstMultiSinkHandle handle)
{
  GList *clink;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  gchar debug[30];

  mhsinkclass->handle_debug (handle, debug);
  // FIXME; how to vfunc this ?
  GST_DEBUG_OBJECT (sink, "%s removing client", debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, handle.socket);
  if (clink != NULL) {
    GstSocketClient *client = clink->data;
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

    if (mhclient->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "%s Client already disconnecting with status %d",
          mhclient->debug, mhclient->status);
      goto done;
    }

    mhclient->status = GST_CLIENT_STATUS_REMOVED;
    mhsinkclass->remove_client_link (GST_MULTI_HANDLE_SINK (sink), clink);
  } else {
    GST_WARNING_OBJECT (sink, "%s no client with this socket found!", debug);
  }

done:
  CLIENTS_UNLOCK (sink);
}

/* "remove-flush" signal implementation */
void
gst_multi_socket_sink_remove_flush (GstMultiSocketSink * sink,
    GstMultiSinkHandle handle)
{
  GList *clink;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  gchar debug[30];

  mhsinkclass->handle_debug (handle, debug);

  GST_DEBUG_OBJECT (sink, "%s flushing client", debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, handle.socket);
  if (clink != NULL) {
    GstSocketClient *client = clink->data;
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

    if (mhclient->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "%s Client already disconnecting with status %d",
          mhclient->debug, mhclient->status);
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

/* "get-stats" signal implementation
 */
GstStructure *
gst_multi_socket_sink_get_stats (GstMultiSocketSink * sink,
    GstMultiSinkHandle handle)
{
  GstSocketClient *client;
  GstStructure *result = NULL;
  GList *clink;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);
  gchar debug[30];

  mhsinkclass->handle_debug (handle, debug);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, handle.socket);
  if (clink == NULL)
    goto noclient;

  client = clink->data;
  if (client != NULL) {
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
    guint64 interval;

    result = gst_structure_new_empty ("multisocketsink-stats");

    if (mhclient->disconnect_time == 0) {
      GTimeVal nowtv;

      g_get_current_time (&nowtv);

      interval = GST_TIMEVAL_TO_TIME (nowtv) - mhclient->connect_time;
    } else {
      interval = mhclient->disconnect_time - mhclient->connect_time;
    }

    gst_structure_set (result,
        "bytes-sent", G_TYPE_UINT64, mhclient->bytes_sent,
        "connect-time", G_TYPE_UINT64, mhclient->connect_time,
        "disconnect-time", G_TYPE_UINT64, mhclient->disconnect_time,
        "connected-duration", G_TYPE_UINT64, interval,
        "last-activatity-time", G_TYPE_UINT64, mhclient->last_activity_time,
        "dropped-buffers", G_TYPE_UINT64, mhclient->dropped_buffers,
        "first-buffer-ts", G_TYPE_UINT64, mhclient->first_buffer_ts,
        "last-buffer-ts", G_TYPE_UINT64, mhclient->last_buffer_ts, NULL);
  }

noclient:
  CLIENTS_UNLOCK (sink);

  /* python doesn't like a NULL pointer yet */
  if (result == NULL) {
    GST_WARNING_OBJECT (sink, "%s no client with this found!", debug);
    result = gst_structure_new_empty ("multisocketsink-stats");
  }

  return result;
}

/* should be called with the clientslock helt.
 * Note that we don't close the fd as we didn't open it in the first
 * place. An application should connect to the client-fd-removed signal and
 * close the fd itself.
 */
static void
gst_multi_socket_sink_remove_client_link (GstMultiHandleSink * sink,
    GList * link)
{
  GTimeVal now;
  GstSocketClient *client = link->data;
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
  GstMultiSocketSink *mssink = GST_MULTI_SOCKET_SINK (sink);
  GstMultiSocketSinkClass *fclass;

  fclass = GST_MULTI_SOCKET_SINK_GET_CLASS (sink);

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

  if (client->source) {
    g_source_destroy (client->source);
    g_source_unref (client->source);
    client->source = NULL;
  }

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
      gst_multi_socket_sink_signals[SIGNAL_CLIENT_REMOVED], 0, mhclient->handle,
      mhclient->status);

  /* lock again before we remove the client completely */
  CLIENTS_LOCK (sink);

  /* fd cannot be reused in the above signal callback so we can safely
   * remove it from the hashtable here */
  if (!g_hash_table_remove (mssink->handle_hash, mhclient->handle.socket)) {
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
    fclass->removed (sink, mhclient->handle);

  CLIENTS_UNLOCK (sink);

  /* and the fd is really gone now */
  g_signal_emit (G_OBJECT (sink),
      gst_multi_socket_sink_signals[SIGNAL_CLIENT_SOCKET_REMOVED], 0,
      mhclient->handle);
  g_assert (G_IS_SOCKET (mhclient->handle.socket));
  g_object_unref (mhclient->handle.socket);

  g_free (client);

  CLIENTS_LOCK (sink);
}

/* handle a read on a client socket,
 * which either indicates a close or should be ignored
 * returns FALSE if some error occured or the client closed. */
static gboolean
gst_multi_socket_sink_handle_client_read (GstMultiSocketSink * sink,
    GstSocketClient * client)
{
  gboolean ret;
  gchar dummy[256];
  gssize nread;
  GError *err = NULL;
  gboolean first = TRUE;
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

  GST_DEBUG_OBJECT (sink, "%s select reports client read", mhclient->debug);

  ret = TRUE;

  /* just Read 'n' Drop, could also just drop the client as it's not supposed
   * to write to us except for closing the socket, I guess it's because we
   * like to listen to our customers. */
  do {
    gssize navail;

    GST_DEBUG_OBJECT (sink, "%s client wants us to read", mhclient->debug);

    navail = g_socket_get_available_bytes (mhclient->handle.socket);
    if (navail < 0)
      break;

    nread =
        g_socket_receive (mhclient->handle.socket, dummy, MIN (navail,
            sizeof (dummy)), sink->cancellable, &err);
    if (first && nread == 0) {
      /* client sent close, so remove it */
      GST_DEBUG_OBJECT (sink, "%s client asked for close, removing",
          mhclient->debug);
      mhclient->status = GST_CLIENT_STATUS_CLOSED;
      ret = FALSE;
    } else if (nread < 0) {
      GST_WARNING_OBJECT (sink, "%s could not read: %s",
          mhclient->debug, err->message);
      mhclient->status = GST_CLIENT_STATUS_ERROR;
      ret = FALSE;
      break;
    }
    first = FALSE;
  } while (nread > 0);
  g_clear_error (&err);

  return ret;
}

/* queue the given buffer for the given client */
static gboolean
gst_multi_socket_sink_client_queue_buffer (GstMultiHandleSink * mhsink,
    GstMultiHandleClient * mhclient, GstBuffer * buffer)
{
  GstMultiSocketSink *sink = GST_MULTI_SOCKET_SINK (mhsink);
  GstCaps *caps;

  /* TRUE: send them if the new caps have them */
  gboolean send_streamheader = FALSE;
  GstStructure *s;

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
            "%s queueing streamheader buffer of length %"
            G_GSIZE_FORMAT, mhclient->debug, gst_buffer_get_size (buffer));
        gst_buffer_ref (buffer);

        mhclient->sending = g_slist_append (mhclient->sending, buffer);
      }
    }
  }

  gst_caps_unref (caps);
  caps = NULL;

  GST_LOG_OBJECT (sink,
      "%s queueing buffer of length %" G_GSIZE_FORMAT, mhclient->debug,
      gst_buffer_get_size (buffer));

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
gst_multi_socket_sink_handle_client_write (GstMultiSocketSink * sink,
    GstSocketClient * client)
{
  gboolean more;
  gboolean flushing;
  GstClockTime now;
  GTimeVal nowtv;
  GError *err = NULL;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);


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
        if (client->source) {
          g_source_destroy (client->source);
          g_source_unref (client->source);
          client->source = NULL;
        }
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
            if (client->source) {
              g_source_destroy (client->source);
              g_source_unref (client->source);
              client->source = NULL;
            }
            //
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
            socket, client, mhclient->bufpos);

        /* queueing a buffer will ref it */
        mhsinkclass->client_queue_buffer (mhsink, mhclient, buf);

        /* need to start from the first byte for this new buffer */
        mhclient->bufoffset = 0;
      }
    }

    /* see if we need to send something */
    if (mhclient->sending) {
      gssize wrote;
      GstBuffer *head;
      GstMapInfo map;

      /* pick first buffer from list */
      head = GST_BUFFER (mhclient->sending->data);

      gst_buffer_map (head, &map, GST_MAP_READ);
      maxsize = map.size - mhclient->bufoffset;

      // FIXME: specific
      /* try to write the complete buffer */

      wrote =
          g_socket_send (mhclient->handle.socket,
          (gchar *) map.data + mhclient->bufoffset, maxsize, sink->cancellable,
          &err);
      gst_buffer_unmap (head, &map);

      if (wrote < 0) {
        /* hmm error.. */
        if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED)) {
          goto connection_reset;
        } else {
          goto write_error;
        }
      } else {
        if (wrote < maxsize) {
          /* partial write means that the client cannot read more and we should
           * stop sending more */
          GST_LOG_OBJECT (sink,
              "partial write on %p of %" G_GSSIZE_FORMAT " bytes", socket,
              wrote);
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
    g_clear_error (&err);
    return FALSE;
  }
write_error:
  {
    GST_WARNING_OBJECT (sink,
        "%s could not write, removing client: %s", mhclient->debug,
        err->message);
    g_clear_error (&err);
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
gst_multi_socket_sink_queue_buffer (GstMultiHandleSink * mhsink,
    GstBuffer * buf)
{
  GList *clients, *next;
  gint queuelen;
  gint max_buffer_usage;
  gint i;
  GTimeVal nowtv;
  GstClockTime now;
  gint max_buffers, soft_max_buffers;
  guint cookie;
  GstMultiSocketSink *sink = GST_MULTI_SOCKET_SINK (mhsink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (sink);
  /* add buffer to queue */
  gst_buffer_ref (buf);
  g_array_prepend_val (mhsink->bufqueue, buf);
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
    GstSocketClient *client;
    GstMultiHandleClient *mhclient;

    if (cookie != mhsink->clients_cookie) {
      GST_DEBUG_OBJECT (sink, "Clients cookie outdated, restarting");
      goto restart;
    }

    client = clients->data;
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
      continue;
    } else if (mhclient->bufpos == 0 || mhclient->new_connection) {
      /* can send data to this client now. need to signal the select thread that
       * the fd_set changed */
      if (!client->source) {
        client->source =
            g_socket_create_source (mhclient->handle.socket,
            G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP,
            sink->cancellable);
        g_source_set_callback (client->source,
            (GSourceFunc) gst_multi_socket_sink_socket_condition,
            gst_object_ref (sink), (GDestroyNotify) gst_object_unref);
        g_source_attach (client->source, sink->main_context);
      }
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
}

/* Handle the clients. This is called when a socket becomes ready
 * to read or writable. Badly behaving clients are put on a
 * garbage list and removed.
 */
static gboolean
gst_multi_socket_sink_socket_condition (GstMultiSinkHandle handle,
    GIOCondition condition, GstMultiSocketSink * sink)
{
  GList *clink;
  GstSocketClient *client;
  gboolean ret = TRUE;
  GstMultiHandleClient *mhclient;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->handle_hash, handle.socket);
  if (clink == NULL) {
    ret = FALSE;
    goto done;
  }

  client = clink->data;
  mhclient = (GstMultiHandleClient *) client;

  if (mhclient->status != GST_CLIENT_STATUS_FLUSHING
      && mhclient->status != GST_CLIENT_STATUS_OK) {
    mhsinkclass->remove_client_link (mhsink, clink);
    ret = FALSE;
    goto done;
  }

  if ((condition & G_IO_ERR)) {
    GST_WARNING_OBJECT (sink, "%s has error", mhclient->debug);
    mhclient->status = GST_CLIENT_STATUS_ERROR;
    mhsinkclass->remove_client_link (mhsink, clink);
    ret = FALSE;
    goto done;
  } else if ((condition & G_IO_HUP)) {
    mhclient->status = GST_CLIENT_STATUS_CLOSED;
    mhsinkclass->remove_client_link (mhsink, clink);
    ret = FALSE;
    goto done;
  } else if ((condition & G_IO_IN) || (condition & G_IO_PRI)) {
    /* handle client read */
    if (!gst_multi_socket_sink_handle_client_read (sink, client)) {
      mhsinkclass->remove_client_link (mhsink, clink);
      ret = FALSE;
      goto done;
    }
  } else if ((condition & G_IO_OUT)) {
    /* handle client write */
    if (!gst_multi_socket_sink_handle_client_write (sink, client)) {
      mhsinkclass->remove_client_link (mhsink, clink);
      ret = FALSE;
      goto done;
    }
  }

done:
  CLIENTS_UNLOCK (sink);

  return ret;
}

static gboolean
gst_multi_socket_sink_timeout (GstMultiSocketSink * sink)
{
  GstClockTime now;
  GTimeVal nowtv;
  GList *clients;
  GstMultiHandleSink *mhsink = GST_MULTI_HANDLE_SINK (sink);
  GstMultiHandleSinkClass *mhsinkclass =
      GST_MULTI_HANDLE_SINK_GET_CLASS (mhsink);

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (sink);
  for (clients = mhsink->clients; clients; clients = clients->next) {
    GstSocketClient *client;
    GstMultiHandleClient *mhclient;

    client = clients->data;
    mhclient = (GstMultiHandleClient *) client;
    if (mhsink->timeout > 0
        && now - mhclient->last_activity_time > mhsink->timeout) {
      mhclient->status = GST_CLIENT_STATUS_SLOW;
      mhsinkclass->remove_client_link (mhsink, clients);
    }
  }
  CLIENTS_UNLOCK (sink);

  return FALSE;
}

/* we handle the client communication in another thread so that we do not block
 * the gstreamer thread while we select() on the client fds */
static gpointer
gst_multi_socket_sink_thread (GstMultiHandleSink * mhsink)
{
  GstMultiSocketSink *sink = GST_MULTI_SOCKET_SINK (mhsink);
  GSource *timeout = NULL;

  while (mhsink->running) {
    if (mhsink->timeout > 0) {
      timeout = g_timeout_source_new (mhsink->timeout / GST_MSECOND);

      g_source_set_callback (timeout,
          (GSourceFunc) gst_multi_socket_sink_timeout, gst_object_ref (sink),
          (GDestroyNotify) gst_object_unref);
      g_source_attach (timeout, sink->main_context);
    }

    /* Returns after handling all pending events or when
     * _wakeup() was called. In any case we have to add
     * a new timeout because something happened.
     */
    g_main_context_iteration (sink->main_context, TRUE);

    if (timeout) {
      g_source_destroy (timeout);
      g_source_unref (timeout);
    }
  }

  return NULL;
}

static void
gst_multi_socket_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_socket_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiSocketSink *multisocketsink;

  multisocketsink = GST_MULTI_SOCKET_SINK (object);

  switch (prop_id) {
    case PROP_NUM_SOCKETS:
      g_value_set_uint (value,
          g_hash_table_size (multisocketsink->handle_hash));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_multi_socket_sink_start_pre (GstMultiHandleSink * mhsink)
{
  GstMultiSocketSink *mssink = GST_MULTI_SOCKET_SINK (mhsink);
  GList *clients;

  GST_INFO_OBJECT (mssink, "starting");

  mssink->main_context = g_main_context_new ();

  CLIENTS_LOCK (mssink);
  for (clients = mhsink->clients; clients; clients = clients->next) {
    GstSocketClient *client = clients->data;
    GstMultiHandleClient *mhclient = (GstMultiHandleClient *) client;

    if (client->source)
      continue;
    client->source =
        g_socket_create_source (mhclient->handle.socket,
        G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP,
        mssink->cancellable);
    g_source_set_callback (client->source,
        (GSourceFunc) gst_multi_socket_sink_socket_condition,
        gst_object_ref (mssink), (GDestroyNotify) gst_object_unref);
    g_source_attach (client->source, mssink->main_context);
  }
  CLIENTS_UNLOCK (mssink);

  return TRUE;
}


static gboolean
multisocketsink_hash_remove (gpointer key, gpointer value, gpointer data)
{
  return TRUE;
}

static void
gst_multi_socket_sink_stop_pre (GstMultiHandleSink * mhsink)
{
  GstMultiSocketSink *mssink = GST_MULTI_SOCKET_SINK (mhsink);

  if (mssink->main_context)
    g_main_context_wakeup (mssink->main_context);
}

static void
gst_multi_socket_sink_stop_post (GstMultiHandleSink * mhsink)
{
  GstMultiSocketSink *mssink = GST_MULTI_SOCKET_SINK (mhsink);

  if (mssink->main_context) {
    g_main_context_unref (mssink->main_context);
    mssink->main_context = NULL;
  }

  g_hash_table_foreach_remove (mssink->handle_hash, multisocketsink_hash_remove,
      mssink);
}

static gboolean
gst_multi_socket_sink_unlock (GstBaseSink * bsink)
{
  GstMultiSocketSink *sink;

  sink = GST_MULTI_SOCKET_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "set to flushing");
  g_cancellable_cancel (sink->cancellable);
  if (sink->main_context)
    g_main_context_wakeup (sink->main_context);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_multi_socket_sink_unlock_stop (GstBaseSink * bsink)
{
  GstMultiSocketSink *sink;

  sink = GST_MULTI_SOCKET_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "unset flushing");
  g_cancellable_reset (sink->cancellable);

  return TRUE;
}
