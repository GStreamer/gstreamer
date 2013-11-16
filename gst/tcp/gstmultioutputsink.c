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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-multioutputsink
 * @see_also: tcpserversink
 *
 * This plugin writes incoming data to a set of file descriptors. The
 * file descriptors can be added to multioutputsink by emitting the #GstMultiOutputSink::add signal. 
 * For each descriptor added, the #GstMultiOutputSink::client-added signal will be called.
 *
 * A client can also be added with the #GstMultiOutputSink::add-full signal
 * that allows for more control over what and how much data a client 
 * initially receives.
 *
 * Clients can be removed from multioutputsink by emitting the #GstMultiOutputSink::remove signal. For
 * each descriptor removed, the #GstMultiOutputSink::client-removed signal will be called. The
 * #GstMultiOutputSink::client-removed signal can also be fired when multioutputsink decides that a
 * client is not active anymore or, depending on the value of the
 * #GstMultiOutputSink:recover-policy property, if the client is reading too slowly.
 * In all cases, multioutputsink will never close a file descriptor itself.
 * The user of multioutputsink is responsible for closing all file descriptors.
 * This can for example be done in response to the #GstMultiOutputSink::client-fd-removed signal.
 * Note that multioutputsink still has a reference to the file descriptor when the
 * #GstMultiOutputSink::client-removed signal is emitted, so that "get-stats" can be performed on
 * the descriptor; it is therefore not safe to close the file descriptor in
 * the #GstMultiOutputSink::client-removed signal handler, and you should use the 
 * #GstMultiOutputSink::client-fd-removed signal to safely close the fd.
 *
 * Multioutputsink internally keeps a queue of the incoming buffers and uses a
 * separate thread to send the buffers to the clients. This ensures that no
 * client write can block the pipeline and that clients can read with different
 * speeds.
 *
 * When adding a client to multioutputsink, the #GstMultiOutputSink:sync-method property will define
 * which buffer in the queued buffers will be sent first to the client. Clients 
 * can be sent the most recent buffer (which might not be decodable by the 
 * client if it is not a keyframe), the next keyframe received in 
 * multioutputsink (which can take some time depending on the keyframe rate), or the
 * last received keyframe (which will cause a simple burst-on-connect). 
 * Multioutputsink will always keep at least one keyframe in its internal buffers
 * when the sync-mode is set to latest-keyframe.
 *
 * There are additional values for the #GstMultiOutputSink:sync-method
 * property to allow finer control over burst-on-connect behaviour. By selecting
 * the 'burst' method a minimum burst size can be chosen, 'burst-keyframe'
 * additionally requires that the burst begin with a keyframe, and 
 * 'burst-with-keyframe' attempts to burst beginning with a keyframe, but will
 * prefer a minimum burst size even if it requires not starting with a keyframe.
 *
 * Multioutputsink can be instructed to keep at least a minimum amount of data
 * expressed in time or byte units in its internal queues with the 
 * #GstMultiOutputSink:time-min and #GstMultiOutputSink:bytes-min properties respectively.
 * These properties are useful if the application adds clients with the 
 * #GstMultiOutputSink::add-full signal to make sure that a burst connect can
 * actually be honored. 
 *
 * When streaming data, clients are allowed to read at a different rate than
 * the rate at which multioutputsink receives data. If the client is reading too
 * fast, no data will be send to the client until multioutputsink receives more
 * data. If the client, however, reads too slowly, data for that client will be 
 * queued up in multioutputsink. Two properties control the amount of data 
 * (buffers) that is queued in multioutputsink: #GstMultiOutputSink:buffers-max and 
 * #GstMultiOutputSink:buffers-soft-max. A client that falls behind by
 * #GstMultiOutputSink:buffers-max is removed from multioutputsink forcibly.
 *
 * A client with a lag of at least #GstMultiOutputSink:buffers-soft-max enters the recovery
 * procedure which is controlled with the #GstMultiOutputSink:recover-policy property.
 * A recover policy of NONE will do nothing, RESYNC_LATEST will send the most recently
 * received buffer as the next buffer for the client, RESYNC_SOFT_LIMIT
 * positions the client to the soft limit in the buffer queue and
 * RESYNC_KEYFRAME positions the client at the most recent keyframe in the
 * buffer queue.
 *
 * multioutputsink will by default synchronize on the clock before serving the 
 * buffers to the clients. This behaviour can be disabled by setting the sync 
 * property to FALSE. Multioutputsink will by default not do QoS and will never
 * drop late buffers.
 *
 * Last reviewed on 2006-09-12 (0.10.10)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstmultioutputsink.h"
#include "gsttcp-marshal.h"

#ifndef G_OS_WIN32
#include <netinet/in.h>
#endif

#define NOT_IMPLEMENTED 0

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (multioutputsink_debug);
#define GST_CAT_DEFAULT (multioutputsink_debug)

/* MultiOutputSink signals and args */
enum
{
  /* methods */
  SIGNAL_ADD,
  SIGNAL_ADD_BURST,
  SIGNAL_REMOVE,
  SIGNAL_REMOVE_FLUSH,
  SIGNAL_CLEAR,
  SIGNAL_GET_STATS,

  /* signals */
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,
  SIGNAL_CLIENT_OUTPUT_REMOVED,

  LAST_SIGNAL
};


/* this is really arbitrarily chosen */
#define DEFAULT_MODE                    1
#define DEFAULT_BUFFERS_MAX             -1
#define DEFAULT_BUFFERS_SOFT_MAX        -1
#define DEFAULT_TIME_MIN                -1
#define DEFAULT_BYTES_MIN               -1
#define DEFAULT_BUFFERS_MIN             -1
#define DEFAULT_UNIT_TYPE               GST_FORMAT_BUFFERS
#define DEFAULT_UNITS_MAX               -1
#define DEFAULT_UNITS_SOFT_MAX          -1
#define DEFAULT_RECOVER_POLICY          GST_RECOVER_POLICY_NONE
#define DEFAULT_TIMEOUT                 0
#define DEFAULT_SYNC_METHOD             GST_SYNC_METHOD_LATEST

#define DEFAULT_BURST_FORMAT            GST_FORMAT_UNDEFINED
#define DEFAULT_BURST_VALUE             0

#define DEFAULT_QOS_DSCP                -1
#define DEFAULT_HANDLE_READ             TRUE

#define DEFAULT_RESEND_STREAMHEADER      TRUE

enum
{
  PROP_0,
  PROP_MODE,
  PROP_BUFFERS_QUEUED,
  PROP_BYTES_QUEUED,
  PROP_TIME_QUEUED,

  PROP_UNIT_TYPE,
  PROP_UNITS_MAX,
  PROP_UNITS_SOFT_MAX,

  PROP_BUFFERS_MAX,
  PROP_BUFFERS_SOFT_MAX,

  PROP_TIME_MIN,
  PROP_BYTES_MIN,
  PROP_BUFFERS_MIN,

  PROP_RECOVER_POLICY,
  PROP_TIMEOUT,
  PROP_SYNC_METHOD,
  PROP_BYTES_TO_SERVE,
  PROP_BYTES_SERVED,

  PROP_BURST_FORMAT,
  PROP_BURST_VALUE,

  PROP_QOS_DSCP,

  PROP_HANDLE_READ,

  PROP_RESEND_STREAMHEADER,

  PROP_NUM_OUTPUTS,

  PROP_LAST
};

#define GST_TYPE_RECOVER_POLICY (gst_multi_output_sink_recover_policy_get_type())
static GType
gst_multi_output_sink_recover_policy_get_type (void)
{
  static GType recover_policy_type = 0;
  static const GEnumValue recover_policy[] = {
    {GST_RECOVER_POLICY_NONE,
        "Do not try to recover", "none"},
    {GST_RECOVER_POLICY_RESYNC_LATEST,
        "Resync client to latest buffer", "latest"},
    {GST_RECOVER_POLICY_RESYNC_SOFT_LIMIT,
        "Resync client to soft limit", "soft-limit"},
    {GST_RECOVER_POLICY_RESYNC_KEYFRAME,
        "Resync client to most recent keyframe", "keyframe"},
    {0, NULL, NULL},
  };

  if (!recover_policy_type) {
    recover_policy_type =
        g_enum_register_static ("GstMultiOutputSinkRecoverPolicy",
        recover_policy);
  }
  return recover_policy_type;
}

#define GST_TYPE_SYNC_METHOD (gst_multi_output_sink_sync_method_get_type())
static GType
gst_multi_output_sink_sync_method_get_type (void)
{
  static GType sync_method_type = 0;
  static const GEnumValue sync_method[] = {
    {GST_SYNC_METHOD_LATEST,
        "Serve starting from the latest buffer", "latest"},
    {GST_SYNC_METHOD_NEXT_KEYFRAME,
        "Serve starting from the next keyframe", "next-keyframe"},
    {GST_SYNC_METHOD_LATEST_KEYFRAME,
          "Serve everything since the latest keyframe (burst)",
        "latest-keyframe"},
    {GST_SYNC_METHOD_BURST, "Serve burst-value data to client", "burst"},
    {GST_SYNC_METHOD_BURST_KEYFRAME,
          "Serve burst-value data starting on a keyframe",
        "burst-keyframe"},
    {GST_SYNC_METHOD_BURST_WITH_KEYFRAME,
          "Serve burst-value data preferably starting on a keyframe",
        "burst-with-keyframe"},
    {0, NULL, NULL},
  };

  if (!sync_method_type) {
    sync_method_type =
        g_enum_register_static ("GstMultiOutputSinkSyncMethod", sync_method);
  }
  return sync_method_type;
}

#define GST_TYPE_CLIENT_STATUS (gst_multi_output_sink_client_status_get_type())
static GType
gst_multi_output_sink_client_status_get_type (void)
{
  static GType client_status_type = 0;
  static const GEnumValue client_status[] = {
    {GST_CLIENT_STATUS_OK, "ok", "ok"},
    {GST_CLIENT_STATUS_CLOSED, "Closed", "closed"},
    {GST_CLIENT_STATUS_REMOVED, "Removed", "removed"},
    {GST_CLIENT_STATUS_SLOW, "Too slow", "slow"},
    {GST_CLIENT_STATUS_ERROR, "Error", "error"},
    {GST_CLIENT_STATUS_DUPLICATE, "Duplicate", "duplicate"},
    {GST_CLIENT_STATUS_FLUSHING, "Flushing", "flushing"},
    {0, NULL, NULL},
  };

  if (!client_status_type) {
    client_status_type =
        g_enum_register_static ("GstMultiOutputSinkClientStatus",
        client_status);
  }
  return client_status_type;
}

static void gst_multi_output_sink_finalize (GObject * object);

static void gst_multi_output_sink_remove_client_link (GstMultiOutputSink * sink,
    GList * link);
static gboolean gst_multi_output_sink_output_condition (GstOutput * output,
    GIOCondition condition, GstMultiOutputSink * sink);

static GstFlowReturn gst_multi_output_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
#if 0
static gboolean gst_multi_output_sink_unlock (GstBaseSink * bsink);
static gboolean gst_multi_output_sink_unlock_stop (GstBaseSink * bsink);
#endif
static GstStateChangeReturn gst_multi_output_sink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_multi_output_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_output_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_multi_output_sink_parent_class parent_class
G_DEFINE_TYPE (GstMultiOutputSink, gst_multi_output_sink, GST_TYPE_BASE_SINK);

static guint gst_multi_output_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_output_sink_class_init (GstMultiOutputSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_multi_output_sink_set_property;
  gobject_class->get_property = gst_multi_output_sink_get_property;
  gobject_class->finalize = gst_multi_output_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_BUFFERS_MAX,
      g_param_spec_int ("buffers-max", "Buffers max",
          "max number of buffers to queue for a client (-1 = no limit)", -1,
          G_MAXINT, DEFAULT_BUFFERS_MAX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BUFFERS_SOFT_MAX,
      g_param_spec_int ("buffers-soft-max", "Buffers soft max",
          "Recover client when going over this limit (-1 = no limit)", -1,
          G_MAXINT, DEFAULT_BUFFERS_SOFT_MAX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BYTES_MIN,
      g_param_spec_int ("bytes-min", "Bytes min",
          "min number of bytes to queue (-1 = as little as possible)", -1,
          G_MAXINT, DEFAULT_BYTES_MIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIME_MIN,
      g_param_spec_int64 ("time-min", "Time min",
          "min number of time to queue (-1 = as little as possible)", -1,
          G_MAXINT64, DEFAULT_TIME_MIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BUFFERS_MIN,
      g_param_spec_int ("buffers-min", "Buffers min",
          "min number of buffers to queue (-1 = as few as possible)", -1,
          G_MAXINT, DEFAULT_BUFFERS_MIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_UNIT_TYPE,
      g_param_spec_enum ("unit-type", "Units type",
          "The unit to measure the max/soft-max/queued properties",
          GST_TYPE_FORMAT, DEFAULT_UNIT_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_UNITS_MAX,
      g_param_spec_int64 ("units-max", "Units max",
          "max number of units to queue (-1 = no limit)", -1, G_MAXINT64,
          DEFAULT_UNITS_MAX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_UNITS_SOFT_MAX,
      g_param_spec_int64 ("units-soft-max", "Units soft max",
          "Recover client when going over this limit (-1 = no limit)", -1,
          G_MAXINT64, DEFAULT_UNITS_SOFT_MAX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFERS_QUEUED,
      g_param_spec_uint ("buffers-queued", "Buffers queued",
          "Number of buffers currently queued", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
#if NOT_IMPLEMENTED
  g_object_class_install_property (gobject_class, PROP_BYTES_QUEUED,
      g_param_spec_uint ("bytes-queued", "Bytes queued",
          "Number of bytes currently queued", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIME_QUEUED,
      g_param_spec_uint64 ("time-queued", "Time queued",
          "Number of time currently queued", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_RECOVER_POLICY,
      g_param_spec_enum ("recover-policy", "Recover Policy",
          "How to recover when client reaches the soft max",
          GST_TYPE_RECOVER_POLICY, DEFAULT_RECOVER_POLICY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Maximum inactivity timeout in nanoseconds for a client (0 = no limit)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SYNC_METHOD,
      g_param_spec_enum ("sync-method", "Sync Method",
          "How to sync new clients to the stream", GST_TYPE_SYNC_METHOD,
          DEFAULT_SYNC_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BYTES_TO_SERVE,
      g_param_spec_uint64 ("bytes-to-serve", "Bytes to serve",
          "Number of bytes received to serve to clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BYTES_SERVED,
      g_param_spec_uint64 ("bytes-served", "Bytes served",
          "Total number of bytes send to all clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BURST_FORMAT,
      g_param_spec_enum ("burst-format", "Burst format",
          "The format of the burst units (when sync-method is burst[[-with]-keyframe])",
          GST_TYPE_FORMAT, DEFAULT_BURST_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BURST_VALUE,
      g_param_spec_uint64 ("burst-value", "Burst value",
          "The amount of burst expressed in burst-unit", 0, G_MAXUINT64,
          DEFAULT_BURST_VALUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp", "QoS diff srv code point",
          "Quality of Service, differentiated services code point (-1 default)",
          -1, 63, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiOutputSink::handle-read
   *
   * Handle read requests from clients and discard the data.
   */
  g_object_class_install_property (gobject_class, PROP_HANDLE_READ,
      g_param_spec_boolean ("handle-read", "Handle Read",
          "Handle client reads and discard the data",
          DEFAULT_HANDLE_READ, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiOutputSink::resend-streamheader
   *
   * Resend the streamheaders to existing clients when they change.
   */
  g_object_class_install_property (gobject_class, PROP_RESEND_STREAMHEADER,
      g_param_spec_boolean ("resend-streamheader", "Resend streamheader",
          "Resend the streamheader if it changes in the caps",
          DEFAULT_RESEND_STREAMHEADER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_OUTPUTS,
      g_param_spec_uint ("num-outputs", "Number of outputs",
          "The current number of client outputs",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiOutputSink::add:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   * @output:             the output to add to multioutputsink
   *
   * Hand the given open output to multioutputsink to write to.
   */
  gst_multi_output_sink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, add), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OUTPUT);
  /**
   * GstMultiOutputSink::add-full:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   * @output:         the output to add to multioutputsink
   * @sync:           the sync method to use
   * @format_min:     the format of @value_min
   * @value_min:      the minimum amount of data to burst expressed in
   *                  @format_min units.
   * @format_max:     the format of @value_max
   * @value_max:      the maximum amount of data to burst expressed in
   *                  @format_max units.
   *
   * Hand the given open output to multioutputsink to write to and
   * specify the burst parameters for the new connection.
   */
  gst_multi_output_sink_signals[SIGNAL_ADD_BURST] =
      g_signal_new ("add-full", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, add_full), NULL, NULL,
      gst_tcp_marshal_VOID__OBJECT_ENUM_ENUM_UINT64_ENUM_UINT64, G_TYPE_NONE, 6,
      G_TYPE_OUTPUT, GST_TYPE_SYNC_METHOD, GST_TYPE_FORMAT, G_TYPE_UINT64,
      GST_TYPE_FORMAT, G_TYPE_UINT64);
  /**
   * GstMultiOutputSink::remove:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   * @output:             the output to remove from multioutputsink
   *
   * Remove the given open output from multioutputsink.
   */
  gst_multi_output_sink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, remove), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OUTPUT);
  /**
   * GstMultiOutputSink::remove-flush:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   * @output:             the output to remove from multioutputsink
   *
   * Remove the given open output from multioutputsink after flushing all
   * the pending data to the output.
   */
  gst_multi_output_sink_signals[SIGNAL_REMOVE_FLUSH] =
      g_signal_new ("remove-flush", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, remove_flush), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OUTPUT);
  /**
   * GstMultiOutputSink::clear:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   *
   * Remove all outputs from multioutputsink.  Since multioutputsink did not
   * open outputs itself, it does not explicitly close the outputs. The application
   * should do so by connecting to the client-output-removed callback.
   */
  gst_multi_output_sink_signals[SIGNAL_CLEAR] =
      g_signal_new ("clear", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, clear), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstMultiOutputSink::get-stats:
   * @gstmultioutputsink: the multioutputsink element to emit this signal on
   * @output:             the output to get stats of from multioutputsink
   *
   * Get statistics about @output. This function returns a GstStructure.
   *
   * Returns: a GstStructure with the statistics. The structure contains
   *     values that represent: total number of bytes sent, time
   *     when the client was added, time when the client was
   *     disconnected/removed, time the client is/was active, last activity
   *     time (in epoch seconds), number of buffers dropped.
   *     All times are expressed in nanoseconds (GstClockTime).
   */
  gst_multi_output_sink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiOutputSinkClass, get_stats), NULL, NULL,
      gst_tcp_marshal_BOXED__OBJECT, GST_TYPE_STRUCTURE, 1, G_TYPE_OUTPUT);

  /**
   * GstMultiOutputSink::client-added:
   * @gstmultioutputsink: the multioutputsink element that emitted this signal
   * @output:             the output that was added to multioutputsink
   *
   * The given output was added to multioutputsink. This signal will
   * be emitted from the streaming thread so application should be prepared
   * for that.
   */
  gst_multi_output_sink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiOutputSinkClass,
          client_added), NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, G_TYPE_OBJECT);
  /**
   * GstMultiOutputSink::client-removed:
   * @gstmultioutputsink: the multioutputsink element that emitted this signal
   * @output:             the output that is to be removed from multioutputsink
   * @status:             the reason why the client was removed
   *
   * The given output is about to be removed from multioutputsink. This
   * signal will be emitted from the streaming thread so applications should
   * be prepared for that.
   *
   * @gstmultioutputsink still holds a handle to @output so it is possible to call
   * the get-stats signal from this callback. For the same reason it is
   * not safe to close() and reuse @output in this callback.
   */
  gst_multi_output_sink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiOutputSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__OBJECT_ENUM,
      G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CLIENT_STATUS);
  /**
   * GstMultiOutputSink::client-output-removed:
   * @gstmultioutputsink: the multioutputsink element that emitted this signal
   * @output:             the output that was removed from multioutputsink
   *
   * The given output was removed from multioutputsink. This signal will
   * be emitted from the streaming thread so applications should be prepared
   * for that.
   *
   * In this callback, @gstmultioutputsink has removed all the information
   * associated with @output and it is therefore not possible to call get-stats
   * with @output. It is however safe to close() and reuse @fd in the callback.
   */
  gst_multi_output_sink_signals[SIGNAL_CLIENT_OUTPUT_REMOVED] =
      g_signal_new ("client-output-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiOutputSinkClass,
          client_output_removed), NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, G_TYPE_OUTPUT);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "Multi output sink", "Sink/Network",
      "Send data to multiple outputs",
      "Thomas Vander Stichele <thomas at apestaart dot org>, "
      "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_multi_output_sink_change_state);

  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_multi_output_sink_render);
#if 0
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_multi_output_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_multi_output_sink_unlock_stop);
#endif
  klass->add = GST_DEBUG_FUNCPTR (gst_multi_output_sink_add);
  klass->add_full = GST_DEBUG_FUNCPTR (gst_multi_output_sink_add_full);
  klass->remove = GST_DEBUG_FUNCPTR (gst_multi_output_sink_remove);
  klass->remove_flush = GST_DEBUG_FUNCPTR (gst_multi_output_sink_remove_flush);
  klass->clear = GST_DEBUG_FUNCPTR (gst_multi_output_sink_clear);
  klass->get_stats = GST_DEBUG_FUNCPTR (gst_multi_output_sink_get_stats);

  GST_DEBUG_CATEGORY_INIT (multioutputsink_debug, "multioutputsink", 0,
      "Multi output sink");
}

static void
gst_multi_output_sink_init (GstMultiOutputSink * this)
{
  GST_OBJECT_FLAG_UNSET (this, GST_MULTI_OUTPUT_SINK_OPEN);

  CLIENTS_LOCK_INIT (this);
  this->clients = NULL;
  this->output_hash = g_hash_table_new (g_direct_hash, g_int_equal);

  this->bufqueue = g_array_new (FALSE, TRUE, sizeof (GstBuffer *));
  this->unit_type = DEFAULT_UNIT_TYPE;
  this->units_max = DEFAULT_UNITS_MAX;
  this->units_soft_max = DEFAULT_UNITS_SOFT_MAX;
  this->time_min = DEFAULT_TIME_MIN;
  this->bytes_min = DEFAULT_BYTES_MIN;
  this->buffers_min = DEFAULT_BUFFERS_MIN;
  this->recover_policy = DEFAULT_RECOVER_POLICY;

  this->timeout = DEFAULT_TIMEOUT;
  this->def_sync_method = DEFAULT_SYNC_METHOD;
  this->def_burst_format = DEFAULT_BURST_FORMAT;
  this->def_burst_value = DEFAULT_BURST_VALUE;

  this->qos_dscp = DEFAULT_QOS_DSCP;
  this->handle_read = DEFAULT_HANDLE_READ;

  this->resend_streamheader = DEFAULT_RESEND_STREAMHEADER;

  this->header_flags = 0;
#if 0
  this->cancellable = g_cancellable_new ();
#endif
}

static void
gst_multi_output_sink_finalize (GObject * object)
{
  GstMultiOutputSink *this;

  this = GST_MULTI_OUTPUT_SINK (object);

  CLIENTS_LOCK_CLEAR (this);
  g_hash_table_destroy (this->output_hash);
  g_array_free (this->bufqueue, TRUE);

  if (this->cancellable) {
    g_object_unref (this->cancellable);
    this->cancellable = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
setup_dscp_client (GstMultiOutputSink * sink, GstOutputClient * client)
{
#ifndef IP_TOS
  return 0;
#else
  gint tos;
  gint ret;
  int fd;
  union gst_sockaddr
  {
    struct sockaddr sa;
    struct sockaddr_in6 sa_in6;
    struct sockaddr_storage sa_stor;
  } sa;
  socklen_t slen = sizeof (sa);
  gint af;

  /* don't touch */
  if (sink->qos_dscp < 0)
    return 0;

  fd = g_output_get_fd (client->output);

  if ((ret = getsockname (fd, &sa.sa, &slen)) < 0) {
    GST_DEBUG_OBJECT (sink, "could not get sockname: %s", g_strerror (errno));
    return ret;
  }

  af = sa.sa.sa_family;

  /* if this is an IPv4-mapped address then do IPv4 QoS */
  if (af == AF_INET6) {

    GST_DEBUG_OBJECT (sink, "check IP6 output");
    if (IN6_IS_ADDR_V4MAPPED (&(sa.sa_in6.sin6_addr))) {
      GST_DEBUG_OBJECT (sink, "mapped to IPV4");
      af = AF_INET;
    }
  }

  /* extract and shift 6 bits of the DSCP */
  tos = (sink->qos_dscp & 0x3f) << 2;

  switch (af) {
    case AF_INET:
      ret = setsockopt (fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos));
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      ret = setsockopt (fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof (tos));
      break;
#endif
    default:
      ret = 0;
      GST_ERROR_OBJECT (sink, "unsupported AF");
      break;
  }
  if (ret)
    GST_DEBUG_OBJECT (sink, "could not set DSCP: %s", g_strerror (errno));

  return ret;
#endif
}

static void
setup_dscp (GstMultiOutputSink * sink)
{
  GList *clients;

  CLIENTS_LOCK (sink);
  for (clients = sink->clients; clients; clients = clients->next) {
    GstOutputClient *client;

    client = clients->data;

    setup_dscp_client (sink, client);
  }
  CLIENTS_UNLOCK (sink);
}

/* "add-full" signal implementation */
void
gst_multi_output_sink_add_full (GstMultiOutputSink * sink, GstOutput * output,
    GstSyncMethod sync_method, GstFormat min_format, guint64 min_value,
    GstFormat max_format, guint64 max_value)
{
  GstOutputClient *client;
  GList *clink;
  GTimeVal now;

  GST_DEBUG_OBJECT (sink, "[output %p] adding client, sync_method %d, "
      "min_format %d, min_value %" G_GUINT64_FORMAT
      ", max_format %d, max_value %" G_GUINT64_FORMAT, output,
      sync_method, min_format, min_value, max_format, max_value);

  /* do limits check if we can */
  if (min_format == max_format) {
    if (max_value != -1 && min_value != -1 && max_value < min_value)
      goto wrong_limits;
  }

  /* create client datastructure */
  client = g_new0 (GstOutputClient, 1);
  client->output = G_OUTPUT (g_object_ref (output));
  client->status = GST_CLIENT_STATUS_OK;
  client->bufpos = -1;
  client->flushcount = -1;
  client->bufoffset = 0;
  client->sending = NULL;
  client->bytes_sent = 0;
  client->dropped_buffers = 0;
  client->avg_queue_size = 0;
  client->first_buffer_ts = GST_CLOCK_TIME_NONE;
  client->last_buffer_ts = GST_CLOCK_TIME_NONE;
  client->new_connection = TRUE;
  client->burst_min_format = min_format;
  client->burst_min_value = min_value;
  client->burst_max_format = max_format;
  client->burst_max_value = max_value;
  client->sync_method = sync_method;
  client->currently_removing = FALSE;

  /* update start time */
  g_get_current_time (&now);
  client->connect_time = GST_TIMEVAL_TO_TIME (now);
  client->disconnect_time = 0;
  /* set last activity time to connect time */
  client->last_activity_time = client->connect_time;

  CLIENTS_LOCK (sink);

  /* check the hash to find a duplicate fd */
  clink = g_hash_table_lookup (sink->output_hash, output);
  if (clink != NULL)
    goto duplicate;

  /* we can add the fd now */
  clink = sink->clients = g_list_prepend (sink->clients, client);
  g_hash_table_insert (sink->output_hash, output, clink);
  sink->clients_cookie++;

  /* set the output to non blocking */
  g_output_set_blocking (output, FALSE);

  /* we always read from a client */
  if (sink->main_context) {
    client->source =
        g_output_create_source (client->output,
        G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP, sink->cancellable);
    g_source_set_callback (client->source,
        (GSourceFunc) gst_multi_output_sink_output_condition,
        gst_object_ref (sink), (GDestroyNotify) gst_object_unref);
    g_source_attach (client->source, sink->main_context);
  }

  setup_dscp_client (sink, client);

  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multi_output_sink_signals[SIGNAL_CLIENT_ADDED], 0, output);

  return;

  /* errors */
wrong_limits:
  {
    GST_WARNING_OBJECT (sink,
        "[output %p] wrong values min =%" G_GUINT64_FORMAT ", max=%"
        G_GUINT64_FORMAT ", format %d specified when adding client", output,
        min_value, max_value, min_format);
    return;
  }
duplicate:
  {
    client->status = GST_CLIENT_STATUS_DUPLICATE;
    CLIENTS_UNLOCK (sink);
    GST_WARNING_OBJECT (sink, "[output %p] duplicate client found, refusing",
        output);
    g_signal_emit (G_OBJECT (sink),
        gst_multi_output_sink_signals[SIGNAL_CLIENT_REMOVED], 0, output,
        client->status);
    g_free (client);
    return;
  }
}

/* "add" signal implementation */
void
gst_multi_output_sink_add (GstMultiOutputSink * sink, GstOutput * output)
{
  gst_multi_output_sink_add_full (sink, output, sink->def_sync_method,
      sink->def_burst_format, sink->def_burst_value, sink->def_burst_format,
      -1);
}

/* "remove" signal implementation */
void
gst_multi_output_sink_remove (GstMultiOutputSink * sink, GstOutput * output)
{
  GList *clink;

  GST_DEBUG_OBJECT (sink, "[output %p] removing client", output);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->output_hash, output);
  if (clink != NULL) {
    GstOutputClient *client = clink->data;

    if (client->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "[output %p] Client already disconnecting with status %d",
          output, client->status);
      goto done;
    }

    client->status = GST_CLIENT_STATUS_REMOVED;
    gst_multi_output_sink_remove_client_link (sink, clink);
  } else {
    GST_WARNING_OBJECT (sink, "[output %p] no client with this output found!",
        output);
  }

done:
  CLIENTS_UNLOCK (sink);
}

/* "remove-flush" signal implementation */
void
gst_multi_output_sink_remove_flush (GstMultiOutputSink * sink,
    GstOutput * output)
{
  GList *clink;

  GST_DEBUG_OBJECT (sink, "[output %p] flushing client", output);

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->output_hash, output);
  if (clink != NULL) {
    GstOutputClient *client = clink->data;

    if (client->status != GST_CLIENT_STATUS_OK) {
      GST_INFO_OBJECT (sink,
          "[output %p] Client already disconnecting with status %d",
          output, client->status);
      goto done;
    }

    /* take the position of the client as the number of buffers left to flush.
     * If the client was at position -1, we flush 0 buffers, 0 == flush 1
     * buffer, etc... */
    client->flushcount = client->bufpos + 1;
    /* mark client as flushing. We can not remove the client right away because
     * it might have some buffers to flush in the ->sending queue. */
    client->status = GST_CLIENT_STATUS_FLUSHING;
  } else {
    GST_WARNING_OBJECT (sink, "[output %p] no client with this fd found!",
        output);
  }
done:
  CLIENTS_UNLOCK (sink);
}

/* can be called both through the signal (i.e. from any thread) or when 
 * stopping, after the writing thread has shut down */
void
gst_multi_output_sink_clear (GstMultiOutputSink * sink)
{
  GList *clients;
  guint32 cookie;

  GST_DEBUG_OBJECT (sink, "clearing all clients");

  CLIENTS_LOCK (sink);
restart:
  cookie = sink->clients_cookie;
  for (clients = sink->clients; clients; clients = clients->next) {
    GstOutputClient *client;

    if (cookie != sink->clients_cookie) {
      GST_DEBUG_OBJECT (sink, "cookie changed while removing all clients");
      goto restart;
    }

    client = clients->data;
    client->status = GST_CLIENT_STATUS_REMOVED;
    gst_multi_output_sink_remove_client_link (sink, clients);
  }

  CLIENTS_UNLOCK (sink);
}

/* "get-stats" signal implementation
 */
GstStructure *
gst_multi_output_sink_get_stats (GstMultiOutputSink * sink, GstOutput * output)
{
  GstOutputClient *client;
  GstStructure *result = NULL;
  GList *clink;

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->output_hash, output);
  if (clink == NULL)
    goto noclient;

  client = clink->data;
  if (client != NULL) {
    guint64 interval;

    result = gst_structure_new_empty ("multioutputsink-stats");

    if (client->disconnect_time == 0) {
      GTimeVal nowtv;

      g_get_current_time (&nowtv);

      interval = GST_TIMEVAL_TO_TIME (nowtv) - client->connect_time;
    } else {
      interval = client->disconnect_time - client->connect_time;
    }

    gst_structure_set (result,
        "bytes-sent", G_TYPE_UINT64, client->bytes_sent,
        "connect-time", G_TYPE_UINT64, client->connect_time,
        "disconnect-time", G_TYPE_UINT64, client->disconnect_time,
        "connected-duration", G_TYPE_UINT64, interval,
        "last-activatity-time", G_TYPE_UINT64, client->last_activity_time,
        "dropped-buffers", G_TYPE_UINT64, client->dropped_buffers,
        "first-buffer-ts", G_TYPE_UINT64, client->first_buffer_ts,
        "last-buffer-ts", G_TYPE_UINT64, client->last_buffer_ts, NULL);
  }

noclient:
  CLIENTS_UNLOCK (sink);

  /* python doesn't like a NULL pointer yet */
  if (result == NULL) {
    GST_WARNING_OBJECT (sink, "[output %p] no client with this found!", output);
    result = gst_structure_new_empty ("multioutputsink-stats");
  }

  return result;
}

/* should be called with the clientslock held.
 * Note that we don't close the fd as we didn't open it in the first
 * place. An application should connect to the client-fd-removed signal and
 * close the fd itself.
 */
static void
gst_multi_output_sink_remove_client_link (GstMultiOutputSink * sink,
    GList * link)
{
  GstOutput *output;
  GTimeVal now;
  GstOutputClient *client = link->data;
  GstMultiOutputSinkClass *fclass;

  fclass = GST_MULTI_OUTPUT_SINK_GET_CLASS (sink);

  output = client->output;

  if (client->currently_removing) {
    GST_WARNING_OBJECT (sink, "[output %p] client is already being removed",
        output);
    return;
  } else {
    client->currently_removing = TRUE;
  }

  /* FIXME: if we keep track of ip we can log it here and signal */
  switch (client->status) {
    case GST_CLIENT_STATUS_OK:
      GST_WARNING_OBJECT (sink, "[output %p] removing client %p for no reason",
          output, client);
      break;
    case GST_CLIENT_STATUS_CLOSED:
      GST_DEBUG_OBJECT (sink, "[output %p] removing client %p because of close",
          output, client);
      break;
    case GST_CLIENT_STATUS_REMOVED:
      GST_DEBUG_OBJECT (sink,
          "[output %p] removing client %p because the app removed it", output,
          client);
      break;
    case GST_CLIENT_STATUS_SLOW:
      GST_INFO_OBJECT (sink,
          "[output %p] removing client %p because it was too slow", output,
          client);
      break;
    case GST_CLIENT_STATUS_ERROR:
      GST_WARNING_OBJECT (sink,
          "[output %p] removing client %p because of error", output, client);
      break;
    case GST_CLIENT_STATUS_FLUSHING:
    default:
      GST_WARNING_OBJECT (sink,
          "[output %p] removing client %p with invalid reason %d", output,
          client, client->status);
      break;
  }

  /* FIXME: convert to vfunc to cleanup a client */

  fclass->delete_client (sink, client);

  g_get_current_time (&now);
  client->disconnect_time = GST_TIMEVAL_TO_TIME (now);

  /* free client buffers */
  g_slist_foreach (client->sending, (GFunc) gst_mini_object_unref, NULL);
  g_slist_free (client->sending);
  client->sending = NULL;

  if (client->caps)
    gst_caps_unref (client->caps);
  client->caps = NULL;

  /* unlock the mutex before signaling because the signal handler
   * might query some properties */
  CLIENTS_UNLOCK (sink);

  g_signal_emit (G_OBJECT (sink),
      gst_multi_output_sink_signals[SIGNAL_CLIENT_REMOVED], 0, output,
      client->status);

  /* lock again before we remove the client completely */
  CLIENTS_LOCK (sink);

  /* fd cannot be reused in the above signal callback so we can safely
   * remove it from the hashtable here */
  if (!g_hash_table_remove (sink->output_hash, output)) {
    GST_WARNING_OBJECT (sink,
        "[output %p] error removing client %p from hash", output, client);
  }
  /* after releasing the lock above, the link could be invalid, more
   * precisely, the next and prev pointers could point to invalid list
   * links. One optimisation could be to add a cookie to the linked list
   * and take a shortcut when it did not change between unlocking and locking
   * our mutex. For now we just walk the list again. */
  sink->clients = g_list_remove (sink->clients, client);
  sink->clients_cookie++;

  if (fclass->removed)
    fclass->removed (sink, output);

  g_free (client);
  CLIENTS_UNLOCK (sink);

  /* and the fd is really gone now */
  g_signal_emit (G_OBJECT (sink),
      gst_multi_output_sink_signals[SIGNAL_CLIENT_OUTPUT_REMOVED], 0, output);
  g_object_unref (output);

  CLIENTS_LOCK (sink);
}

/* handle a read on a client output,
 * which either indicates a close or should be ignored
 * returns FALSE if some error occured or the client closed. */
static gboolean
gst_multi_output_sink_handle_client_read (GstMultiOutputSink * sink,
    GstOutputClient * client)
{
  gboolean ret;
  gchar dummy[256];
  gssize nread;
  GError *err = NULL;
  gboolean first = TRUE;

  GST_DEBUG_OBJECT (sink, "[output %p] select reports client read",
      client->output);

  ret = TRUE;

  /* just Read 'n' Drop, could also just drop the client as it's not supposed
   * to write to us except for closing the output, I guess it's because we
   * like to listen to our customers. */
  do {
    gssize navail;

    GST_DEBUG_OBJECT (sink, "[output %p] client wants us to read",
        client->output);

    navail = g_output_get_available_bytes (client->output);
    if (navail < 0)
      break;

    nread =
        g_output_receive (client->output, dummy, MIN (navail, sizeof (dummy)),
        sink->cancellable, &err);
    if (first && nread == 0) {
      /* client sent close, so remove it */
      GST_DEBUG_OBJECT (sink, "[output %p] client asked for close, removing",
          client->output);
      client->status = GST_CLIENT_STATUS_CLOSED;
      ret = FALSE;
    } else if (nread < 0) {
      GST_WARNING_OBJECT (sink, "[output %p] could not read: %s",
          client->output, err->message);
      client->status = GST_CLIENT_STATUS_ERROR;
      ret = FALSE;
      break;
    }
    first = FALSE;
  } while (nread > 0);
  g_clear_error (&err);

  return ret;
}

static gboolean
is_sync_frame (GstMultiOutputSink * sink, GstBuffer * buffer)
{
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
    return FALSE;
  } else if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_IN_CAPS)) {
    return TRUE;
  }

  return FALSE;
}

#if 0
/* queue the given buffer for the given client */
static gboolean
gst_multi_output_sink_client_queue_buffer (GstMultiOutputSink * sink,
    GstOutputClient * client, GstBuffer * buffer)
{
  GstCaps *caps;

  /* TRUE: send them if the new caps have them */
  gboolean send_streamheader = FALSE;
  GstStructure *s;

  /* before we queue the buffer, we check if we need to queue streamheader
   * buffers (because it's a new client, or because they changed) */
  caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (sink));

  if (!client->caps) {
    GST_DEBUG_OBJECT (sink,
        "[output %p] no previous caps for this client, send streamheader",
        client->output);
    send_streamheader = TRUE;
    client->caps = gst_caps_ref (caps);
  } else {
    /* there were previous caps recorded, so compare */
    if (!gst_caps_is_equal (caps, client->caps)) {
      const GValue *sh1, *sh2;

      /* caps are not equal, but could still have the same streamheader */
      s = gst_caps_get_structure (caps, 0);
      if (!gst_structure_has_field (s, "streamheader")) {
        /* no new streamheader, so nothing new to send */
        GST_DEBUG_OBJECT (sink,
            "[output %p] new caps do not have streamheader, not sending",
            client->output);
      } else {
        /* there is a new streamheader */
        s = gst_caps_get_structure (client->caps, 0);
        if (!gst_structure_has_field (s, "streamheader")) {
          /* no previous streamheader, so send the new one */
          GST_DEBUG_OBJECT (sink,
              "[output %p] previous caps did not have streamheader, sending",
              client->output);
          send_streamheader = TRUE;
        } else {
          /* both old and new caps have streamheader set */
          if (!sink->resend_streamheader) {
            GST_DEBUG_OBJECT (sink,
                "[output %p] asked to not resend the streamheader, not sending",
                client->output);
            send_streamheader = FALSE;
          } else {
            sh1 = gst_structure_get_value (s, "streamheader");
            s = gst_caps_get_structure (caps, 0);
            sh2 = gst_structure_get_value (s, "streamheader");
            if (gst_value_compare (sh1, sh2) != GST_VALUE_EQUAL) {
              GST_DEBUG_OBJECT (sink,
                  "[output %p] new streamheader different from old, sending",
                  client->output);
              send_streamheader = TRUE;
            }
          }
        }
      }
    }
    /* Replace the old caps */
    gst_caps_unref (client->caps);
    client->caps = gst_caps_ref (caps);
  }

  if (G_UNLIKELY (send_streamheader)) {
    const GValue *sh;
    GArray *buffers;
    int i;

    GST_LOG_OBJECT (sink,
        "[output %p] sending streamheader from caps %" GST_PTR_FORMAT,
        client->output, caps);
    s = gst_caps_get_structure (caps, 0);
    if (!gst_structure_has_field (s, "streamheader")) {
      GST_DEBUG_OBJECT (sink,
          "[output %p] no new streamheader, so nothing to send",
          client->output);
    } else {
      GST_LOG_OBJECT (sink,
          "[output %p] sending streamheader from caps %" GST_PTR_FORMAT,
          client->output, caps);
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
            "[output %p] queueing streamheader buffer of length %"
            G_GSIZE_FORMAT, client->output, gst_buffer_get_size (buffer));
        gst_buffer_ref (buffer);

        client->sending = g_slist_append (client->sending, buffer);
      }
    }
  }

  gst_caps_unref (caps);
  caps = NULL;

  GST_LOG_OBJECT (sink,
      "[output %p] queueing buffer of length %" G_GSIZE_FORMAT, client->output,
      gst_buffer_get_size (buffer));

  gst_buffer_ref (buffer);
  client->sending = g_slist_append (client->sending, buffer);

  return TRUE;
}
#endif

/* find the keyframe in the list of buffers starting the
 * search from @idx. @direction as -1 will search backwards, 
 * 1 will search forwards.
 * Returns: the index or -1 if there is no keyframe after idx.
 */
static gint
find_syncframe (GstMultiOutputSink * sink, gint idx, gint direction)
{
  gint i, len, result;

  /* take length of queued buffers */
  len = sink->bufqueue->len;

  /* assume we don't find a keyframe */
  result = -1;

  /* then loop over all buffers to find the first keyframe */
  for (i = idx; i >= 0 && i < len; i += direction) {
    GstBuffer *buf;

    buf = g_array_index (sink->bufqueue, GstBuffer *, i);
    if (is_sync_frame (sink, buf)) {
      GST_LOG_OBJECT (sink, "found keyframe at %d from %d, direction %d",
          i, idx, direction);
      result = i;
      break;
    }
  }
  return result;
}

#define find_next_syncframe(s,i) 	find_syncframe(s,i,1)
#define find_prev_syncframe(s,i) 	find_syncframe(s,i,-1)

/* Get the number of buffers from the buffer queue needed to satisfy
 * the maximum max in the configured units.
 * If units are not BUFFERS, and there are insufficient buffers in the
 * queue to satify the limit, return len(queue) + 1 */
static gint
get_buffers_max (GstMultiOutputSink * sink, gint64 max)
{
  switch (sink->unit_type) {
    case GST_FORMAT_BUFFERS:
      return max;
    case GST_FORMAT_TIME:
    {
      GstBuffer *buf;
      int i;
      int len;
      gint64 diff;
      GstClockTime first = GST_CLOCK_TIME_NONE;

      len = sink->bufqueue->len;

      for (i = 0; i < len; i++) {
        buf = g_array_index (sink->bufqueue, GstBuffer *, i);
        if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
          if (first == -1)
            first = GST_BUFFER_TIMESTAMP (buf);

          diff = first - GST_BUFFER_TIMESTAMP (buf);

          if (diff > max)
            return i + 1;
        }
      }
      return len + 1;
    }
    case GST_FORMAT_BYTES:
    {
      GstBuffer *buf;
      int i;
      int len;
      gint acc = 0;

      len = sink->bufqueue->len;

      for (i = 0; i < len; i++) {
        buf = g_array_index (sink->bufqueue, GstBuffer *, i);
        acc += gst_buffer_get_size (buf);

        if (acc > max)
          return i + 1;
      }
      return len + 1;
    }
    default:
      return max;
  }
}

/* find the positions in the buffer queue where *_min and *_max
 * is satisfied
 */
/* count the amount of data in the buffers and return the index
 * that satifies the given limits.
 *
 * Returns: index @idx in the buffer queue so that the given limits are
 * satisfied. TRUE if all the limits could be satisfied, FALSE if not
 * enough data was in the queue.
 *
 * FIXME, this code might now work if any of the units is in buffers...
 */
static gboolean
find_limits (GstMultiOutputSink * sink,
    gint * min_idx, gint bytes_min, gint buffers_min, gint64 time_min,
    gint * max_idx, gint bytes_max, gint buffers_max, gint64 time_max)
{
  GstClockTime first, time;
  gint i, len, bytes;
  gboolean result, max_hit;

  /* take length of queue */
  len = sink->bufqueue->len;

  /* this must hold */
  g_assert (len > 0);

  GST_LOG_OBJECT (sink,
      "bytes_min %d, buffers_min %d, time_min %" GST_TIME_FORMAT
      ", bytes_max %d, buffers_max %d, time_max %" GST_TIME_FORMAT, bytes_min,
      buffers_min, GST_TIME_ARGS (time_min), bytes_max, buffers_max,
      GST_TIME_ARGS (time_max));

  /* do the trivial buffer limit test */
  if (buffers_min != -1 && len < buffers_min) {
    *min_idx = len - 1;
    *max_idx = len - 1;
    return FALSE;
  }

  result = FALSE;
  /* else count bytes and time */
  first = -1;
  bytes = 0;
  /* unset limits */
  *min_idx = -1;
  *max_idx = -1;
  max_hit = FALSE;

  i = 0;
  /* loop through the buffers, when a limit is ok, mark it 
   * as -1, we have at least one buffer in the queue. */
  do {
    GstBuffer *buf;

    /* if we checked all min limits, update result */
    if (bytes_min == -1 && time_min == -1 && *min_idx == -1) {
      /* don't go below 0 */
      *min_idx = MAX (i - 1, 0);
    }
    /* if we reached one max limit break out */
    if (max_hit) {
      /* i > 0 when we get here, we subtract one to get the position
       * of the previous buffer. */
      *max_idx = i - 1;
      /* we have valid complete result if we found a min_idx too */
      result = *min_idx != -1;
      break;
    }
    buf = g_array_index (sink->bufqueue, GstBuffer *, i);

    bytes += gst_buffer_get_size (buf);

    /* take timestamp and save for the base first timestamp */
    if ((time = GST_BUFFER_TIMESTAMP (buf)) != -1) {
      GST_LOG_OBJECT (sink, "Ts %" GST_TIME_FORMAT " on buffer",
          GST_TIME_ARGS (time));
      if (first == -1)
        first = time;

      /* increase max usage if we did not fill enough. Note that
       * buffers are sorted from new to old, so the first timestamp is
       * bigger than the next one. */
      if (time_min != -1 && first - time >= time_min)
        time_min = -1;
      if (time_max != -1 && first - time >= time_max)
        max_hit = TRUE;
    } else {
      GST_LOG_OBJECT (sink, "No timestamp on buffer");
    }
    /* time is OK or unknown, check and increase if not enough bytes */
    if (bytes_min != -1) {
      if (bytes >= bytes_min)
        bytes_min = -1;
    }
    if (bytes_max != -1) {
      if (bytes >= bytes_max) {
        max_hit = TRUE;
      }
    }
    i++;
  }
  while (i < len);

  /* if we did not hit the max or min limit, set to buffer size */
  if (*max_idx == -1)
    *max_idx = len - 1;
  /* make sure min does not exceed max */
  if (*min_idx == -1)
    *min_idx = *max_idx;

  return result;
}

/* parse the unit/value pair and assign it to the result value of the
 * right type, leave the other values untouched 
 *
 * Returns: FALSE if the unit is unknown or undefined. TRUE otherwise.
 */
static gboolean
assign_value (GstFormat format, guint64 value, gint * bytes, gint * buffers,
    GstClockTime * time)
{
  gboolean res = TRUE;

  /* set only the limit of the given format to the given value */
  switch (format) {
    case GST_FORMAT_BUFFERS:
      *buffers = (gint) value;
      break;
    case GST_FORMAT_TIME:
      *time = value;
      break;
    case GST_FORMAT_BYTES:
      *bytes = (gint) value;
      break;
    case GST_FORMAT_UNDEFINED:
    default:
      res = FALSE;
      break;
  }
  return res;
}

/* count the index in the buffer queue to satisfy the given unit
 * and value pair starting from buffer at index 0.
 *
 * Returns: TRUE if there was enough data in the queue to satisfy the
 * burst values. @idx contains the index in the buffer that contains enough
 * data to satisfy the limits or the last buffer in the queue when the
 * function returns FALSE.
 */
static gboolean
count_burst_unit (GstMultiOutputSink * sink, gint * min_idx,
    GstFormat min_format, guint64 min_value, gint * max_idx,
    GstFormat max_format, guint64 max_value)
{
  gint bytes_min = -1, buffers_min = -1;
  gint bytes_max = -1, buffers_max = -1;
  GstClockTime time_min = GST_CLOCK_TIME_NONE, time_max = GST_CLOCK_TIME_NONE;

  assign_value (min_format, min_value, &bytes_min, &buffers_min, &time_min);
  assign_value (max_format, max_value, &bytes_max, &buffers_max, &time_max);

  return find_limits (sink, min_idx, bytes_min, buffers_min, time_min,
      max_idx, bytes_max, buffers_max, time_max);
}

/* decide where in the current buffer queue this new client should start
 * receiving buffers from.
 * This function is called whenever a client is connected and has not yet
 * received a buffer.
 * If this returns -1, it means that we haven't found a good point to
 * start streaming from yet, and this function should be called again later
 * when more buffers have arrived.
 */
static gint
gst_multi_output_sink_new_client (GstMultiOutputSink * sink,
    GstOutputClient * client)
{
  gint result;

  GST_DEBUG_OBJECT (sink,
      "[output %p] new client, deciding where to start in queue",
      client->output);
  GST_DEBUG_OBJECT (sink, "queue is currently %d buffers long",
      sink->bufqueue->len);
  switch (client->sync_method) {
    case GST_SYNC_METHOD_LATEST:
      /* no syncing, we are happy with whatever the client is going to get */
      result = client->bufpos;
      GST_DEBUG_OBJECT (sink,
          "[output %p] SYNC_METHOD_LATEST, position %d", client->output,
          result);
      break;
    case GST_SYNC_METHOD_NEXT_KEYFRAME:
    {
      /* if one of the new buffers (between client->bufpos and 0) in the queue
       * is a sync point, we can proceed, otherwise we need to keep waiting */
      GST_LOG_OBJECT (sink,
          "[output %p] new client, bufpos %d, waiting for keyframe",
          client->output, client->bufpos);

      result = find_prev_syncframe (sink, client->bufpos);
      if (result != -1) {
        GST_DEBUG_OBJECT (sink,
            "[output %p] SYNC_METHOD_NEXT_KEYFRAME: result %d",
            client->output, result);
        break;
      }

      /* client is not on a syncbuffer, need to skip these buffers and
       * wait some more */
      GST_LOG_OBJECT (sink,
          "[output %p] new client, skipping buffer(s), no syncpoint found",
          client->output);
      client->bufpos = -1;
      break;
    }
    case GST_SYNC_METHOD_LATEST_KEYFRAME:
    {
      GST_DEBUG_OBJECT (sink,
          "[output %p] SYNC_METHOD_LATEST_KEYFRAME", client->output);

      /* for new clients we initially scan the complete buffer queue for
       * a sync point when a buffer is added. If we don't find a keyframe,
       * we need to wait for the next keyframe and so we change the client's
       * sync method to GST_SYNC_METHOD_NEXT_KEYFRAME.
       */
      result = find_next_syncframe (sink, 0);
      if (result != -1) {
        GST_DEBUG_OBJECT (sink,
            "[output %p] SYNC_METHOD_LATEST_KEYFRAME: result %d",
            client->output, result);
        break;
      }

      GST_DEBUG_OBJECT (sink,
          "[output %p] SYNC_METHOD_LATEST_KEYFRAME: no keyframe found, "
          "switching to SYNC_METHOD_NEXT_KEYFRAME", client->output);
      /* throw client to the waiting state */
      client->bufpos = -1;
      /* and make client sync to next keyframe */
      client->sync_method = GST_SYNC_METHOD_NEXT_KEYFRAME;
      break;
    }
    case GST_SYNC_METHOD_BURST:
    {
      gboolean ok;
      gint max;

      /* move to the position where we satisfy the client's burst
       * parameters. If we could not satisfy the parameters because there
       * is not enough data, we just send what we have (which is in result).
       * We use the max value to limit the search
       */
      ok = count_burst_unit (sink, &result, client->burst_min_format,
          client->burst_min_value, &max, client->burst_max_format,
          client->burst_max_value);
      GST_DEBUG_OBJECT (sink,
          "[output %p] SYNC_METHOD_BURST: burst_unit returned %d, result %d",
          client->output, ok, result);

      GST_LOG_OBJECT (sink, "min %d, max %d", result, max);

      /* we hit the max and it is below the min, use that then */
      if (max != -1 && max <= result) {
        result = MAX (max - 1, 0);
        GST_DEBUG_OBJECT (sink,
            "[output %p] SYNC_METHOD_BURST: result above max, taken down to %d",
            client->output, result);
      }
      break;
    }
    case GST_SYNC_METHOD_BURST_KEYFRAME:
    {
      gint min_idx, max_idx;
      gint next_syncframe, prev_syncframe;

      /* BURST_KEYFRAME:
       *
       * _always_ start sending a keyframe to the client. We first search
       * a keyframe between min/max limits. If there is none, we send it the
       * last keyframe before min. If there is none, the behaviour is like
       * NEXT_KEYFRAME.
       */
      /* gather burst limits */
      count_burst_unit (sink, &min_idx, client->burst_min_format,
          client->burst_min_value, &max_idx, client->burst_max_format,
          client->burst_max_value);

      GST_LOG_OBJECT (sink, "min %d, max %d", min_idx, max_idx);

      /* first find a keyframe after min_idx */
      next_syncframe = find_next_syncframe (sink, min_idx);
      if (next_syncframe != -1 && next_syncframe < max_idx) {
        /* we have a valid keyframe and it's below the max */
        GST_LOG_OBJECT (sink, "found keyframe in min/max limits");
        result = next_syncframe;
        break;
      }

      /* no valid keyframe, try to find one below min */
      prev_syncframe = find_prev_syncframe (sink, min_idx);
      if (prev_syncframe != -1) {
        GST_WARNING_OBJECT (sink,
            "using keyframe below min in BURST_KEYFRAME sync mode");
        result = prev_syncframe;
        break;
      }

      /* no prev keyframe or not enough data  */
      GST_WARNING_OBJECT (sink,
          "no prev keyframe found in BURST_KEYFRAME sync mode, waiting for next");

      /* throw client to the waiting state */
      client->bufpos = -1;
      /* and make client sync to next keyframe */
      client->sync_method = GST_SYNC_METHOD_NEXT_KEYFRAME;
      result = -1;
      break;
    }
    case GST_SYNC_METHOD_BURST_WITH_KEYFRAME:
    {
      gint min_idx, max_idx;
      gint next_syncframe;

      /* BURST_WITH_KEYFRAME:
       *
       * try to start sending a keyframe to the client. We first search
       * a keyframe between min/max limits. If there is none, we send it the
       * amount of data up 'till min.
       */
      /* gather enough data to burst */
      count_burst_unit (sink, &min_idx, client->burst_min_format,
          client->burst_min_value, &max_idx, client->burst_max_format,
          client->burst_max_value);

      GST_LOG_OBJECT (sink, "min %d, max %d", min_idx, max_idx);

      /* first find a keyframe after min_idx */
      next_syncframe = find_next_syncframe (sink, min_idx);
      if (next_syncframe != -1 && next_syncframe < max_idx) {
        /* we have a valid keyframe and it's below the max */
        GST_LOG_OBJECT (sink, "found keyframe in min/max limits");
        result = next_syncframe;
        break;
      }

      /* no keyframe, send data from min_idx */
      GST_WARNING_OBJECT (sink, "using min in BURST_WITH_KEYFRAME sync mode");

      /* make sure we don't go over the max limit */
      if (max_idx != -1 && max_idx <= min_idx) {
        result = MAX (max_idx - 1, 0);
      } else {
        result = min_idx;
      }

      break;
    }
    default:
      g_warning ("unknown sync method %d", client->sync_method);
      result = client->bufpos;
      break;
  }
  return result;
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
 * possible. It will first exhaust the client->sending queue and if the queue
 * is empty, it will pick a buffer from the global queue.
 *
 * Sending the buffers from the client->sending queue is basically writing
 * the bytes to the output and maintaining a count of the bytes that were
 * sent. When the buffer is completely sent, it is removed from the
 * client->sending queue and we try to pick a new buffer for sending.
 *
 * When the sending returns a partial buffer we stop sending more data as
 * the next send operation could block.
 *
 * This functions returns FALSE if some error occured.
 */
static gboolean
gst_multi_output_sink_handle_client_write (GstMultiOutputSink * sink,
    GstOutputClient * client)
{
  GstOutput *output = client->output;
  gboolean more;
  gboolean flushing;
  GstClockTime now;
  GTimeVal nowtv;
  GError *err = NULL;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  flushing = client->status == GST_CLIENT_STATUS_FLUSHING;

  more = TRUE;
  do {
    gint maxsize;

    if (!client->sending) {
      /* client is not working on a buffer */
      if (client->bufpos == -1) {
        /* client is too fast, remove from write queue until new buffer is
         * available */
        if (client->source) {
          g_source_destroy (client->source);
          g_source_unref (client->source);
          client->source = NULL;
        }
        /* if we flushed out all of the client buffers, we can stop */
        if (client->flushcount == 0)
          goto flushed;

        return TRUE;
      } else {
        /* client can pick a buffer from the global queue */
        GstBuffer *buf;
        GstClockTime timestamp;

        /* for new connections, we need to find a good spot in the
         * bufqueue to start streaming from */
        if (client->new_connection && !flushing) {
          gint position = gst_multi_output_sink_new_client (sink, client);

          if (position >= 0) {
            /* we got a valid spot in the queue */
            client->new_connection = FALSE;
            client->bufpos = position;
          } else {
            /* cannot send data to this client yet */
            if (client->source) {
              g_source_destroy (client->source);
              g_source_unref (client->source);
              client->source = NULL;
            }
            return TRUE;
          }
        }

        /* we flushed all remaining buffers, no need to get a new one */
        if (client->flushcount == 0)
          goto flushed;

        /* grab buffer */
        buf = g_array_index (sink->bufqueue, GstBuffer *, client->bufpos);
        client->bufpos--;

        /* update stats */
        timestamp = GST_BUFFER_TIMESTAMP (buf);
        if (client->first_buffer_ts == GST_CLOCK_TIME_NONE)
          client->first_buffer_ts = timestamp;
        if (timestamp != -1)
          client->last_buffer_ts = timestamp;

        /* decrease flushcount */
        if (client->flushcount != -1)
          client->flushcount--;

        GST_LOG_OBJECT (sink, "[output %p] client %p at position %d",
            output, client, client->bufpos);

        /* queueing a buffer will ref it */
        gst_multi_output_sink_client_queue_buffer (sink, client, buf);

        /* need to start from the first byte for this new buffer */
        client->bufoffset = 0;
      }
    }

    /* see if we need to send something */
    if (client->sending) {
      gssize wrote;
      GstBuffer *head;
      GstMapInfo map;

      /* pick first buffer from list */
      head = GST_BUFFER (client->sending->data);

      gst_buffer_map (head, &map, GST_MAP_READ);
      maxsize = map.size - client->bufoffset;

      /* try to write the complete buffer */

      wrote =
          g_output_send (output, (gchar *) map.data + client->bufoffset,
          maxsize, sink->cancellable, &err);
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
              "partial write on %p of %" G_GSSIZE_FORMAT " bytes", output,
              wrote);
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

  /* ERRORS */
flushed:
  {
    GST_DEBUG_OBJECT (sink, "[output %p] flushed, removing", output);
    client->status = GST_CLIENT_STATUS_REMOVED;
    return FALSE;
  }
connection_reset:
  {
    GST_DEBUG_OBJECT (sink, "[output %p] connection reset by peer, removing",
        output);
    client->status = GST_CLIENT_STATUS_CLOSED;
    g_clear_error (&err);
    return FALSE;
  }
write_error:
  {
    GST_WARNING_OBJECT (sink,
        "[output %p] could not write, removing client: %s", output,
        err->message);
    g_clear_error (&err);
    client->status = GST_CLIENT_STATUS_ERROR;
    return FALSE;
  }
}

/* calculate the new position for a client after recovery. This function
 * does not update the client position but merely returns the required
 * position.
 */
static gint
gst_multi_output_sink_recover_client (GstMultiOutputSink * sink,
    GstOutputClient * client)
{
  gint newbufpos;

  GST_WARNING_OBJECT (sink,
      "[output %p] client %p is lagging at %d, recover using policy %d",
      client->output, client, client->bufpos, sink->recover_policy);

  switch (sink->recover_policy) {
    case GST_RECOVER_POLICY_NONE:
      /* do nothing, client will catch up or get kicked out when it reaches
       * the hard max */
      newbufpos = client->bufpos;
      break;
    case GST_RECOVER_POLICY_RESYNC_LATEST:
      /* move to beginning of queue */
      newbufpos = -1;
      break;
    case GST_RECOVER_POLICY_RESYNC_SOFT_LIMIT:
      /* move to beginning of soft max */
      newbufpos = get_buffers_max (sink, sink->units_soft_max);
      break;
    case GST_RECOVER_POLICY_RESYNC_KEYFRAME:
      /* find keyframe in buffers, we search backwards to find the
       * closest keyframe relative to what this client already received. */
      newbufpos = MIN (sink->bufqueue->len - 1,
          get_buffers_max (sink, sink->units_soft_max) - 1);

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
      newbufpos = get_buffers_max (sink, sink->units_soft_max);
      break;
  }
  return newbufpos;
}

/* Queue a buffer on the global queue.
 *
 * This function adds the buffer to the front of a GArray. It removes the
 * tail buffer if the max queue size is exceeded, unreffing the queued buffer.
 * Note that unreffing the buffer is not a problem as clients who
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
 * This is done by adding the client back into the write fd_set and signaling
 * the select thread that the fd_set changed.
 */
static void
gst_multi_output_sink_queue_buffer (GstMultiOutputSink * sink, GstBuffer * buf)
{
  GList *clients, *next;
  gint queuelen;
  gint max_buffer_usage;
  gint i;
  GTimeVal nowtv;
  GstClockTime now;
  gint max_buffers, soft_max_buffers;
  guint cookie;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (sink);
  /* add buffer to queue */
  gst_buffer_ref (buf);
  g_array_prepend_val (sink->bufqueue, buf);
  queuelen = sink->bufqueue->len;

  if (sink->units_max > 0)
    max_buffers = get_buffers_max (sink, sink->units_max);
  else
    max_buffers = -1;

  if (sink->units_soft_max > 0)
    soft_max_buffers = get_buffers_max (sink, sink->units_soft_max);
  else
    soft_max_buffers = -1;
  GST_LOG_OBJECT (sink, "Using max %d, softmax %d", max_buffers,
      soft_max_buffers);

  /* then loop over the clients and update the positions */
  max_buffer_usage = 0;

restart:
  cookie = sink->clients_cookie;
  for (clients = sink->clients; clients; clients = next) {
    GstOutputClient *client;

    if (cookie != sink->clients_cookie) {
      GST_DEBUG_OBJECT (sink, "Clients cookie outdated, restarting");
      goto restart;
    }

    client = clients->data;
    next = g_list_next (clients);

    client->bufpos++;
    GST_LOG_OBJECT (sink, "[output %p] client %p at position %d",
        client->output, client, client->bufpos);
    /* check soft max if needed, recover client */
    if (soft_max_buffers > 0 && client->bufpos >= soft_max_buffers) {
      gint newpos;

      newpos = gst_multi_output_sink_recover_client (sink, client);
      if (newpos != client->bufpos) {
        client->dropped_buffers += client->bufpos - newpos;
        client->bufpos = newpos;
        client->discont = TRUE;
        GST_INFO_OBJECT (sink, "[output %p] client %p position reset to %d",
            client->output, client, client->bufpos);
      } else {
        GST_INFO_OBJECT (sink,
            "[output %p] client %p not recovering position",
            client->output, client);
      }
    }
    /* check hard max and timeout, remove client */
    if ((max_buffers > 0 && client->bufpos >= max_buffers) ||
        (sink->timeout > 0
            && now - client->last_activity_time > sink->timeout)) {
      /* remove client */
      GST_WARNING_OBJECT (sink, "[output %p] client %p is too slow, removing",
          client->output, client);
      /* remove the client, the fd set will be cleared and the select thread
       * will be signaled */
      client->status = GST_CLIENT_STATUS_SLOW;
      /* set client to invalid position while being removed */
      client->bufpos = -1;
      gst_multi_output_sink_remove_client_link (sink, clients);
      continue;
    } else if (client->bufpos == 0 || client->new_connection) {
      /* can send data to this client now. need to signal the select thread that
       * the fd_set changed */
      if (!client->source) {
        client->source =
            g_output_create_source (client->output,
            G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP,
            sink->cancellable);
        g_source_set_callback (client->source,
            (GSourceFunc) gst_multi_output_sink_output_condition,
            gst_object_ref (sink), (GDestroyNotify) gst_object_unref);
        g_source_attach (client->source, sink->main_context);
      }
    }
    /* keep track of maximum buffer usage */
    if (client->bufpos > max_buffer_usage) {
      max_buffer_usage = client->bufpos;
    }
  }

  /* make sure we respect bytes-min, buffers-min and time-min when they are set */
  {
    gint usage, max;

    GST_LOG_OBJECT (sink,
        "extending queue %d to respect time_min %" GST_TIME_FORMAT
        ", bytes_min %d, buffers_min %d", max_buffer_usage,
        GST_TIME_ARGS (sink->time_min), sink->bytes_min, sink->buffers_min);

    /* get index where the limits are ok, we don't really care if all limits
     * are ok, we just queue as much as we need. We also don't compare against
     * the max limits. */
    find_limits (sink, &usage, sink->bytes_min, sink->buffers_min,
        sink->time_min, &max, -1, -1, -1);

    max_buffer_usage = MAX (max_buffer_usage, usage + 1);
    GST_LOG_OBJECT (sink, "extended queue to %d", max_buffer_usage);
  }

  /* now look for sync points and make sure there is at least one
   * sync point in the queue. We only do this if the LATEST_KEYFRAME or 
   * BURST_KEYFRAME mode is selected */
  if (sink->def_sync_method == GST_SYNC_METHOD_LATEST_KEYFRAME ||
      sink->def_sync_method == GST_SYNC_METHOD_BURST_KEYFRAME) {
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

  GST_LOG_OBJECT (sink, "len %d, usage %d", queuelen, max_buffer_usage);

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
}

/* Handle the clients. This is called when a output becomes ready
 * to read or writable. Badly behaving clients are put on a
 * garbage list and removed.
 */
static gboolean
gst_multi_output_sink_output_condition (GstOutput * output,
    GIOCondition condition, GstMultiOutputSink * sink)
{
  GList *clink;
  GstOutputClient *client;
  gboolean ret = TRUE;

  CLIENTS_LOCK (sink);
  clink = g_hash_table_lookup (sink->output_hash, output);
  if (clink == NULL) {
    ret = FALSE;
    goto done;
  }

  client = clink->data;

  if (client->status != GST_CLIENT_STATUS_FLUSHING
      && client->status != GST_CLIENT_STATUS_OK) {
    gst_multi_output_sink_remove_client_link (sink, clink);
    ret = FALSE;
    goto done;
  }

  if ((condition & G_IO_ERR)) {
    GST_WARNING_OBJECT (sink, "Output %p has error", client->output);
    client->status = GST_CLIENT_STATUS_ERROR;
    gst_multi_output_sink_remove_client_link (sink, clink);
    ret = FALSE;
    goto done;
  } else if ((condition & G_IO_HUP)) {
    client->status = GST_CLIENT_STATUS_CLOSED;
    gst_multi_output_sink_remove_client_link (sink, clink);
    ret = FALSE;
    goto done;
  } else if ((condition & G_IO_IN) || (condition & G_IO_PRI)) {
    /* handle client read */
    if (!gst_multi_output_sink_handle_client_read (sink, client)) {
      gst_multi_output_sink_remove_client_link (sink, clink);
      ret = FALSE;
      goto done;
    }
  } else if ((condition & G_IO_OUT)) {
    /* handle client write */
    if (!gst_multi_output_sink_handle_client_write (sink, client)) {
      gst_multi_output_sink_remove_client_link (sink, clink);
      ret = FALSE;
      goto done;
    }
  }

done:
  CLIENTS_UNLOCK (sink);

  return ret;
}

static gboolean
gst_multi_output_sink_timeout (GstMultiOutputSink * sink)
{
  GstClockTime now;
  GTimeVal nowtv;
  GList *clients;

  g_get_current_time (&nowtv);
  now = GST_TIMEVAL_TO_TIME (nowtv);

  CLIENTS_LOCK (sink);
  for (clients = sink->clients; clients; clients = clients->next) {
    GstOutputClient *client;

    client = clients->data;
    if (sink->timeout > 0 && now - client->last_activity_time > sink->timeout) {
      client->status = GST_CLIENT_STATUS_SLOW;
      gst_multi_output_sink_remove_client_link (sink, clients);
    }
  }
  CLIENTS_UNLOCK (sink);

  return FALSE;
}

/* we handle the client communication in another thread so that we do not block
 * the gstreamer thread while we select() on the client fds */
static gpointer
gst_multi_output_sink_thread (GstMultiOutputSink * sink)
{
  GSource *timeout = NULL;

  while (sink->running) {
    if (sink->timeout > 0) {
      timeout = g_timeout_source_new (sink->timeout / GST_MSECOND);

      g_source_set_callback (timeout,
          (GSourceFunc) gst_multi_output_sink_timeout, gst_object_ref (sink),
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

static GstFlowReturn
gst_multi_output_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstMultiOutputSink *sink;
  gboolean in_caps;
#if 0
  GstCaps *bufcaps, *padcaps;
#endif

  sink = GST_MULTI_OUTPUT_SINK (bsink);

  g_return_val_if_fail (GST_OBJECT_FLAG_IS_SET (sink,
          GST_MULTI_OUTPUT_SINK_OPEN), GST_FLOW_WRONG_STATE);

#if 0
  /* since we check every buffer for streamheader caps, we need to make
   * sure every buffer has caps set */
  bufcaps = gst_buffer_get_caps (buf);
  padcaps = GST_PAD_CAPS (GST_BASE_SINK_PAD (bsink));

  /* make sure we have caps on the pad */
  if (!padcaps && !bufcaps)
    goto no_caps;
#endif

  /* get IN_CAPS first, code below might mess with the flags */
  in_caps = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

#if 0
  /* stamp the buffer with previous caps if no caps set */
  if (!bufcaps) {
    if (!gst_buffer_is_writable (buf)) {
      /* metadata is not writable, copy will be made and original buffer
       * will be unreffed so we need to ref so that we don't lose the
       * buffer in the render method. */
      gst_buffer_ref (buf);
      /* the new buffer is ours only, we keep it out of the scope of this
       * function */
      buf = gst_buffer_make_writable (buf);
    } else {
      /* else the metadata is writable, we ref because we keep the buffer
       * out of the scope of this method */
      gst_buffer_ref (buf);
    }
    /* buffer metadata is writable now, set the caps */
    gst_buffer_set_caps (buf, padcaps);
  } else {
    gst_caps_unref (bufcaps);

    /* since we keep this buffer out of the scope of this method */
    gst_buffer_ref (buf);
  }
#endif

  GST_LOG_OBJECT (sink, "received buffer %p, in_caps: %s, offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT
      ", timestamp %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      buf, in_caps ? "yes" : "no", GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* if we get IN_CAPS buffers, but the previous buffer was not IN_CAPS,
   * it means we're getting new streamheader buffers, and we should clear
   * the old ones */
  if (in_caps && sink->previous_buffer_in_caps == FALSE) {
    GST_DEBUG_OBJECT (sink,
        "receiving new IN_CAPS buffers, clearing old streamheader");
    g_slist_foreach (sink->streamheader, (GFunc) gst_mini_object_unref, NULL);
    g_slist_free (sink->streamheader);
    sink->streamheader = NULL;
  }

  /* save the current in_caps */
  sink->previous_buffer_in_caps = in_caps;

  /* if the incoming buffer is marked as IN CAPS, then we assume for now
   * it's a streamheader that needs to be sent to each new client, so we
   * put it on our internal list of streamheader buffers.
   * FIXME: we could check if the buffer's contents are in fact part of the
   * current streamheader.
   *
   * We don't send the buffer to the client, since streamheaders are sent
   * separately when necessary. */
  if (in_caps) {
    GST_DEBUG_OBJECT (sink, "appending IN_CAPS buffer with length %"
        G_GSIZE_FORMAT " to streamheader", gst_buffer_get_size (buf));
    sink->streamheader = g_slist_append (sink->streamheader, buf);
  } else {
    /* queue the buffer, this is a regular data buffer. */
    this->class->queue_buffer (sink, buf);

    sink->bytes_to_serve += gst_buffer_get_size (buf);
  }
  return GST_FLOW_OK;

  /* ERRORS */
#if 0
no_caps:
  {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, (NULL),
        ("Received first buffer without caps set"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
#endif
}

static void
gst_multi_output_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiOutputSink *multioutputsink;

  multioutputsink = GST_MULTI_OUTPUT_SINK (object);

  switch (prop_id) {
    case PROP_BUFFERS_MAX:
      multioutputsink->units_max = g_value_get_int (value);
      break;
    case PROP_BUFFERS_SOFT_MAX:
      multioutputsink->units_soft_max = g_value_get_int (value);
      break;
    case PROP_TIME_MIN:
      multioutputsink->time_min = g_value_get_int64 (value);
      break;
    case PROP_BYTES_MIN:
      multioutputsink->bytes_min = g_value_get_int (value);
      break;
    case PROP_BUFFERS_MIN:
      multioutputsink->buffers_min = g_value_get_int (value);
      break;
    case PROP_UNIT_TYPE:
      multioutputsink->unit_type = g_value_get_enum (value);
      break;
    case PROP_UNITS_MAX:
      multioutputsink->units_max = g_value_get_int64 (value);
      break;
    case PROP_UNITS_SOFT_MAX:
      multioutputsink->units_soft_max = g_value_get_int64 (value);
      break;
    case PROP_RECOVER_POLICY:
      multioutputsink->recover_policy = g_value_get_enum (value);
      break;
    case PROP_TIMEOUT:
      multioutputsink->timeout = g_value_get_uint64 (value);
      break;
    case PROP_SYNC_METHOD:
      multioutputsink->def_sync_method = g_value_get_enum (value);
      break;
    case PROP_BURST_FORMAT:
      multioutputsink->def_burst_format = g_value_get_enum (value);
      break;
    case PROP_BURST_VALUE:
      multioutputsink->def_burst_value = g_value_get_uint64 (value);
      break;
    case PROP_QOS_DSCP:
      multioutputsink->qos_dscp = g_value_get_int (value);
      setup_dscp (multioutputsink);
      break;
    case PROP_HANDLE_READ:
      multioutputsink->handle_read = g_value_get_boolean (value);
      break;
    case PROP_RESEND_STREAMHEADER:
      multioutputsink->resend_streamheader = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_output_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiOutputSink *multioutputsink;

  multioutputsink = GST_MULTI_OUTPUT_SINK (object);

  switch (prop_id) {
    case PROP_BUFFERS_MAX:
      g_value_set_int (value, multioutputsink->units_max);
      break;
    case PROP_BUFFERS_SOFT_MAX:
      g_value_set_int (value, multioutputsink->units_soft_max);
      break;
    case PROP_TIME_MIN:
      g_value_set_int64 (value, multioutputsink->time_min);
      break;
    case PROP_BYTES_MIN:
      g_value_set_int (value, multioutputsink->bytes_min);
      break;
    case PROP_BUFFERS_MIN:
      g_value_set_int (value, multioutputsink->buffers_min);
      break;
    case PROP_BUFFERS_QUEUED:
      g_value_set_uint (value, multioutputsink->buffers_queued);
      break;
    case PROP_BYTES_QUEUED:
      g_value_set_uint (value, multioutputsink->bytes_queued);
      break;
    case PROP_TIME_QUEUED:
      g_value_set_uint64 (value, multioutputsink->time_queued);
      break;
    case PROP_UNIT_TYPE:
      g_value_set_enum (value, multioutputsink->unit_type);
      break;
    case PROP_UNITS_MAX:
      g_value_set_int64 (value, multioutputsink->units_max);
      break;
    case PROP_UNITS_SOFT_MAX:
      g_value_set_int64 (value, multioutputsink->units_soft_max);
      break;
    case PROP_RECOVER_POLICY:
      g_value_set_enum (value, multioutputsink->recover_policy);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, multioutputsink->timeout);
      break;
    case PROP_SYNC_METHOD:
      g_value_set_enum (value, multioutputsink->def_sync_method);
      break;
    case PROP_BYTES_TO_SERVE:
      g_value_set_uint64 (value, multioutputsink->bytes_to_serve);
      break;
    case PROP_BYTES_SERVED:
      g_value_set_uint64 (value, multioutputsink->bytes_served);
      break;
    case PROP_BURST_FORMAT:
      g_value_set_enum (value, multioutputsink->def_burst_format);
      break;
    case PROP_BURST_VALUE:
      g_value_set_uint64 (value, multioutputsink->def_burst_value);
      break;
    case PROP_QOS_DSCP:
      g_value_set_int (value, multioutputsink->qos_dscp);
      break;
    case PROP_HANDLE_READ:
      g_value_set_boolean (value, multioutputsink->handle_read);
      break;
    case PROP_RESEND_STREAMHEADER:
      g_value_set_boolean (value, multioutputsink->resend_streamheader);
      break;
    case PROP_NUM_OUTPUTS:
      g_value_set_uint (value,
          g_hash_table_size (multioutputsink->output_hash));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a output for sending to remote machine */
static gboolean
gst_multi_output_sink_start (GstBaseSink * bsink)
{
  GstMultiOutputSinkClass *fclass;
  GstMultiOutputSink *this;
  GList *clients;

  if (GST_OBJECT_FLAG_IS_SET (bsink, GST_MULTI_OUTPUT_SINK_OPEN))
    return TRUE;

  this = GST_MULTI_OUTPUT_SINK (bsink);
  fclass = GST_MULTI_OUTPUT_SINK_GET_CLASS (this);

  GST_INFO_OBJECT (this, "starting");
  /* FIXME: until here the same */

#if 0
  this->main_context = g_main_context_new ();

  CLIENTS_LOCK (this);
  for (clients = this->clients; clients; clients = clients->next) {
    GstOutputClient *client;

    client = clients->data;
    if (client->source)
      continue;
    client->source =
        g_output_create_source (client->output,
        G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP, this->cancellable);
    g_source_set_callback (client->source,
        (GSourceFunc) gst_multi_output_sink_output_condition,
        gst_object_ref (this), (GDestroyNotify) gst_object_unref);
    g_source_attach (client->source, this->main_context);
  }
  CLIENTS_UNLOCK (this);

  /* FIXME: this bit shared */
#endif
  this->streamheader = NULL;
  this->bytes_to_serve = 0;
  this->bytes_served = 0;

  if (fclass->init) {
    fclass->init (this);
  }

  this->running = TRUE;

  this->thread = g_thread_new ("multioutputsink",
      (GThreadFunc) gst_multi_output_sink_thread, this);

  GST_OBJECT_FLAG_SET (this, GST_MULTI_OUTPUT_SINK_OPEN);

  return TRUE;
}

static gboolean
multioutputsink_hash_remove (gpointer key, gpointer value, gpointer data)
{
  return TRUE;
}

static gboolean
gst_multi_output_sink_stop (GstBaseSink * bsink)
{
  GstMultiOutputSinkClass *fclass;
  GstMultiOutputSink *this;
  GstBuffer *buf;
  gint i;

  this = GST_MULTI_OUTPUT_SINK (bsink);
  fclass = GST_MULTI_OUTPUT_SINK_GET_CLASS (this);

  if (!GST_OBJECT_FLAG_IS_SET (bsink, GST_MULTI_OUTPUT_SINK_OPEN))
    return TRUE;

  this->running = FALSE;

  if (parent_class->wakeup)
    parent_class->wakeup (this);


  if (this->thread) {
    GST_DEBUG_OBJECT (this, "joining thread");
    g_thread_join (this->thread);
    GST_DEBUG_OBJECT (this, "joined thread");
    this->thread = NULL;
  }

  /* free the clients */
  gst_multi_output_sink_clear (this);

  if (this->streamheader) {
    g_slist_foreach (this->streamheader, (GFunc) gst_mini_object_unref, NULL);
    g_slist_free (this->streamheader);
    this->streamheader = NULL;
  }

  if (fclass->close)
    fclass->close (this);

#ifdef 0
  if (this->main_context) {
    g_main_context_unref (this->main_context);
    this->main_context = NULL;
  }
#endif

  g_hash_table_foreach_remove (this->output_hash, multioutputsink_hash_remove,
      this);

  /* remove all queued buffers */
  if (this->bufqueue) {
    GST_DEBUG_OBJECT (this, "Emptying bufqueue with %d buffers",
        this->bufqueue->len);
    for (i = this->bufqueue->len - 1; i >= 0; --i) {
      buf = g_array_index (this->bufqueue, GstBuffer *, i);
      GST_LOG_OBJECT (this, "Removing buffer %p (%d) with refcount %d", buf, i,
          GST_MINI_OBJECT_REFCOUNT (buf));
      gst_buffer_unref (buf);
      this->bufqueue = g_array_remove_index (this->bufqueue, i);
    }
    /* freeing the array is done in _finalize */
  }
  GST_OBJECT_FLAG_UNSET (this, GST_MULTI_OUTPUT_SINK_OPEN);

  return TRUE;
}

static GstStateChangeReturn
gst_multi_output_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMultiOutputSink *sink;
  GstStateChangeReturn ret;

  sink = GST_MULTI_OUTPUT_SINK (element);

  /* we disallow changing the state from the streaming thread */
  if (g_thread_self () == sink->thread)
    return GST_STATE_CHANGE_FAILURE;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!parent_class->start (GST_BASE_SINK (sink)))
        goto start_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_multi_output_sink_stop (GST_BASE_SINK (sink));
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
start_failed:
  {
    /* error message was posted */
    return GST_STATE_CHANGE_FAILURE;
  }
}

#if 0
static gboolean
gst_multi_output_sink_unlock (GstBaseSink * bsink)
{
  GstMultiOutputSink *sink;

  sink = GST_MULTI_OUTPUT_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "set to flushing");
  g_cancellable_cancel (sink->cancellable);
  if (sink->main_context)
    g_main_context_wakeup (sink->main_context);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_multi_output_sink_unlock_stop (GstBaseSink * bsink)
{
  GstMultiOutputSink *sink;

  sink = GST_MULTI_OUTPUT_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "unset flushing");
  g_cancellable_reset (sink->cancellable);

  return TRUE;
}
#endif
