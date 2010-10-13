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


#ifndef __GST_MULTI_FD_SINK_H__
#define __GST_MULTI_FD_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#include "gsttcp.h"

#define GST_TYPE_MULTI_FD_SINK \
  (gst_multi_fd_sink_get_type())
#define GST_MULTI_FD_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTI_FD_SINK,GstMultiFdSink))
#define GST_MULTI_FD_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTI_FD_SINK,GstMultiFdSinkClass))
#define GST_IS_MULTI_FD_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTI_FD_SINK))
#define GST_IS_MULTI_FD_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTI_FD_SINK))
#define GST_MULTI_FD_SINK_GET_CLASS(klass) \
  (G_TYPE_INSTANCE_GET_CLASS ((klass), GST_TYPE_MULTI_FD_SINK, GstMultiFdSinkClass))


typedef struct _GstMultiFdSink GstMultiFdSink;
typedef struct _GstMultiFdSinkClass GstMultiFdSinkClass;

typedef enum {
  GST_MULTI_FD_SINK_OPEN             = (GST_ELEMENT_FLAG_LAST << 0),

  GST_MULTI_FD_SINK_FLAG_LAST        = (GST_ELEMENT_FLAG_LAST << 2)
} GstMultiFdSinkFlags;

/**
 * GstRecoverPolicy:
 * @GST_RECOVER_POLICY_NONE             : no recovering is done
 * @GST_RECOVER_POLICY_RESYNC_LATEST    : client is moved to last buffer
 * @GST_RECOVER_POLICY_RESYNC_SOFT_LIMIT: client is moved to the soft limit
 * @GST_RECOVER_POLICY_RESYNC_KEYFRAME  : client is moved to latest keyframe
 *
 * Possible values for the recovery procedure to use when a client consumes
 * data too slow and has a backlag of more that soft-limit buffers.
 */
typedef enum
{
  GST_RECOVER_POLICY_NONE,
  GST_RECOVER_POLICY_RESYNC_LATEST,
  GST_RECOVER_POLICY_RESYNC_SOFT_LIMIT,
  GST_RECOVER_POLICY_RESYNC_KEYFRAME
} GstRecoverPolicy;

/**
 * GstSyncMethod:
 * @GST_SYNC_METHOD_LATEST              : client receives most recent buffer
 * @GST_SYNC_METHOD_NEXT_KEYFRAME       : client receives next keyframe
 * @GST_SYNC_METHOD_LATEST_KEYFRAME     : client receives latest keyframe (burst)
 * @GST_SYNC_METHOD_BURST               : client receives specific amount of data
 * @GST_SYNC_METHOD_BURST_KEYFRAME      : client receives specific amount of data 
 *                                        starting from latest keyframe
 * @GST_SYNC_METHOD_BURST_WITH_KEYFRAME : client receives specific amount of data from
 *                                        a keyframe, or if there is not enough data after
 *                                        the keyframe, starting before the keyframe
 *
 * This enum defines the selection of the first buffer that is sent
 * to a new client.
 */
typedef enum
{
  GST_SYNC_METHOD_LATEST,
  GST_SYNC_METHOD_NEXT_KEYFRAME,
  GST_SYNC_METHOD_LATEST_KEYFRAME,
  GST_SYNC_METHOD_BURST,
  GST_SYNC_METHOD_BURST_KEYFRAME,
  GST_SYNC_METHOD_BURST_WITH_KEYFRAME
} GstSyncMethod;

/**
 * GstTCPUnitType:
 * @GST_TCP_UNIT_TYPE_UNDEFINED: undefined
 * @GST_TCP_UNIT_TYPE_BUFFERS  : buffers
 * @GST_TCP_UNIT_TYPE_TIME     : timeunits (in nanoseconds)
 * @GST_TCP_UNIT_TYPE_BYTES    : bytes
 *
 * The units used to specify limits.
 */
typedef enum
{
  GST_TCP_UNIT_TYPE_UNDEFINED,
  GST_TCP_UNIT_TYPE_BUFFERS,
  GST_TCP_UNIT_TYPE_TIME,
  GST_TCP_UNIT_TYPE_BYTES
} GstTCPUnitType;

/**
 * GstClientStatus:
 * @GST_CLIENT_STATUS_OK       : client is ok
 * @GST_CLIENT_STATUS_CLOSED   : client closed the socket
 * @GST_CLIENT_STATUS_REMOVED  : client is removed
 * @GST_CLIENT_STATUS_SLOW     : client is too slow
 * @GST_CLIENT_STATUS_ERROR    : client is in error
 * @GST_CLIENT_STATUS_DUPLICATE: same client added twice
 * @GST_CLIENT_STATUS_FLUSHING : client is flushing out the remaining buffers.
 *
 * This specifies the reason why a client was removed from
 * multifdsink and is received in the "client-removed" signal.
 */
typedef enum
{
  GST_CLIENT_STATUS_OK          = 0,
  GST_CLIENT_STATUS_CLOSED      = 1,
  GST_CLIENT_STATUS_REMOVED     = 2,
  GST_CLIENT_STATUS_SLOW        = 3,
  GST_CLIENT_STATUS_ERROR       = 4,
  GST_CLIENT_STATUS_DUPLICATE   = 5,
  GST_CLIENT_STATUS_FLUSHING    = 6
} GstClientStatus;

/* structure for a client
 */
typedef struct {
  GstPollFD fd;

  gint bufpos;                  /* position of this client in the global queue */
  gint flushcount;              /* the remaining number of buffers to flush out or -1 if the 
                                   client is not flushing. */

  GstClientStatus status;
  gboolean is_socket;

  GSList *sending;              /* the buffers we need to send */
  gint bufoffset;               /* offset in the first buffer */

  gboolean discont;

  GstTCPProtocol protocol;

  gboolean caps_sent;
  gboolean new_connection;

  gboolean currently_removing;

  /* method to sync client when connecting */
  GstSyncMethod sync_method;
  GstTCPUnitType   burst_min_unit;
  guint64       burst_min_value;
  GstTCPUnitType   burst_max_unit;
  guint64       burst_max_value;

  GstCaps *caps;                /* caps of last queued buffer */

  /* stats */
  guint64 bytes_sent;
  guint64 connect_time;
  guint64 disconnect_time;
  guint64 last_activity_time;
  guint64 dropped_buffers;
  guint64 avg_queue_size;
  guint64 first_buffer_ts;
  guint64 last_buffer_ts;
} GstTCPClient;

#define CLIENTS_LOCK_INIT(fdsink)       (g_static_rec_mutex_init(&fdsink->clientslock))
#define CLIENTS_LOCK_FREE(fdsink)       (g_static_rec_mutex_free(&fdsink->clientslock))
#define CLIENTS_LOCK(fdsink)            (g_static_rec_mutex_lock(&fdsink->clientslock))
#define CLIENTS_UNLOCK(fdsink)          (g_static_rec_mutex_unlock(&fdsink->clientslock))

/**
 * GstMultiFdSink:
 *
 * The multifdsink object structure.
 */
struct _GstMultiFdSink {
  GstBaseSink element;

  /*< private >*/
  guint64 bytes_to_serve; /* how much bytes we must serve */
  guint64 bytes_served; /* how much bytes have we served */

  GStaticRecMutex clientslock;  /* lock to protect the clients list */
  GList *clients;       /* list of clients we are serving */
  GHashTable *fd_hash;  /* index on fd to client */
  guint clients_cookie; /* Cookie to detect changes to the clients list */

  gint mode;
  GstPoll *fdset;

  GSList *streamheader; /* GSList of GstBuffers to use as streamheader */
  gboolean previous_buffer_in_caps;

  GstTCPProtocol protocol;
  guint mtu;
  gint qos_dscp;
  gboolean handle_read;

  GArray *bufqueue;     /* global queue of buffers */

  gboolean running;     /* the thread state */
  GThread *thread;      /* the sender thread */

  /* these values are used to check if a client is reading fast
   * enough and to control receovery */
  GstTCPUnitType unit_type;/* the type of the units */
  gint64 units_max;       /* max units to queue for a client */
  gint64 units_soft_max;  /* max units a client can lag before recovery starts */
  GstRecoverPolicy recover_policy;
  GstClockTime timeout; /* max amount of nanoseconds to remain idle */

  GstSyncMethod def_sync_method;    /* what method to use for connecting clients */
  GstTCPUnitType   def_burst_unit;
  guint64       def_burst_value;

  /* these values are used to control the amount of data
   * kept in the queues. It allows clients to perform a burst
   * on connect. */
  gint   bytes_min;	/* min number of bytes to queue */
  gint64 time_min;	/* min time to queue */
  gint   buffers_min;   /* min number of buffers to queue */

  gboolean resend_streamheader; /* resend streamheader if it changes */

  /* stats */
  gint buffers_queued;  /* number of queued buffers */
  gint bytes_queued;    /* number of queued bytes */
  gint time_queued;     /* number of queued time */

  guint8 header_flags;
};

struct _GstMultiFdSinkClass {
  GstBaseSinkClass parent_class;

  /* element methods */
  void          (*add)          (GstMultiFdSink *sink, int fd);
  void          (*add_full)     (GstMultiFdSink *sink, int fd, GstSyncMethod sync,
		                 GstTCPUnitType format, guint64 value, 
				 GstTCPUnitType max_unit, guint64 max_value);
  void          (*remove)       (GstMultiFdSink *sink, int fd);
  void          (*remove_flush) (GstMultiFdSink *sink, int fd);
  void          (*clear)        (GstMultiFdSink *sink);
  GValueArray*  (*get_stats)    (GstMultiFdSink *sink, int fd);

  /* vtable */
  gboolean (*init)   (GstMultiFdSink *sink);
  gboolean (*wait)   (GstMultiFdSink *sink, GstPoll *set);
  gboolean (*close)  (GstMultiFdSink *sink);
  void (*removed) (GstMultiFdSink *sink, int fd);

  /* signals */
  void (*client_added) (GstElement *element, gint fd);
  void (*client_removed) (GstElement *element, gint fd, GstClientStatus status);
  void (*client_fd_removed) (GstElement *element, gint fd);
};

GType gst_multi_fd_sink_get_type (void);

void          gst_multi_fd_sink_add          (GstMultiFdSink *sink, int fd);
void          gst_multi_fd_sink_add_full     (GstMultiFdSink *sink, int fd, GstSyncMethod sync, 
                                              GstTCPUnitType min_unit, guint64 min_value,
                                              GstTCPUnitType max_unit, guint64 max_value);
void          gst_multi_fd_sink_remove       (GstMultiFdSink *sink, int fd);
void          gst_multi_fd_sink_remove_flush (GstMultiFdSink *sink, int fd);
void          gst_multi_fd_sink_clear        (GstMultiFdSink *sink);
GValueArray*  gst_multi_fd_sink_get_stats    (GstMultiFdSink *sink, int fd);

G_END_DECLS

#endif /* __GST_MULTI_FD_SINK_H__ */
