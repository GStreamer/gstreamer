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


#ifndef __GST_MULTIFDSINK_H__
#define __GST_MULTIFDSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#include "gsttcp.h"
#include "gstfdset.h"

#define GST_TYPE_MULTIFDSINK \
  (gst_multifdsink_get_type())
#define GST_MULTIFDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIFDSINK,GstMultiFdSink))
#define GST_MULTIFDSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTIFDSINK,GstMultiFdSink))
#define GST_IS_MULTIFDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIFDSINK))
#define GST_IS_MULTIFDSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTIFDSINK))
#define GST_MULTIFDSINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MULTIFDSINK, GstMultiFdSinkClass))
	

typedef struct _GstMultiFdSink GstMultiFdSink;
typedef struct _GstMultiFdSinkClass GstMultiFdSinkClass;

typedef enum {
  GST_MULTIFDSINK_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_MULTIFDSINK_FLAG_LAST        = GST_ELEMENT_FLAG_LAST + 2,
} GstMultiFdSinkFlags;

typedef enum
{
  GST_RECOVER_POLICY_NONE,
  GST_RECOVER_POLICY_RESYNC_START,
  GST_RECOVER_POLICY_RESYNC_SOFT,
  GST_RECOVER_POLICY_RESYNC_KEYFRAME,
} GstRecoverPolicy;

typedef enum
{
  GST_SYNC_METHOD_NONE,
  GST_SYNC_METHOD_WAIT,
  GST_SYNC_METHOD_BURST,
} GstSyncMethod;

typedef enum
{
  GST_UNIT_TYPE_BUFFERS,
  GST_UNIT_TYPE_TIME,
  GST_UNIT_TYPE_BYTES,
} GstUnitType;

typedef enum
{
  GST_CLIENT_STATUS_OK		= 0,
  GST_CLIENT_STATUS_CLOSED	= 1,
  GST_CLIENT_STATUS_REMOVED	= 2,
  GST_CLIENT_STATUS_SLOW	= 3,
  GST_CLIENT_STATUS_ERROR	= 4,
  GST_CLIENT_STATUS_DUPLICATE	= 5,
} GstClientStatus;

/* structure for a client
 *  */
typedef struct {
  GstFD fd;

  gint bufpos;                  /* position of this client in the global queue */

  GstClientStatus status;
  gboolean is_socket;

  GSList *sending;              /* the buffers we need to send */
  gint bufoffset;               /* offset in the first buffer */

  gboolean discont;

  GstTCPProtocolType protocol;

  gboolean caps_sent;
  gboolean streamheader_sent;
  gboolean new_connection;

  /* stats */
  guint64 bytes_sent;
  guint64 connect_time;
  guint64 disconnect_time;
  guint64 last_activity_time;
  guint64 dropped_buffers;
  guint64 avg_queue_size;

} GstTCPClient;

#define CLIENTS_LOCK_INIT(fdsink)	(g_static_rec_mutex_init(&fdsink->clientslock))
#define CLIENTS_LOCK_FREE(fdsink)	(g_static_rec_mutex_free(&fdsink->clientslock))
#define CLIENTS_LOCK(fdsink)		(g_static_rec_mutex_lock(&fdsink->clientslock))
#define CLIENTS_UNLOCK(fdsink)		(g_static_rec_mutex_unlock(&fdsink->clientslock))

struct _GstMultiFdSink {
  GstBaseSink element;

  guint64 bytes_to_serve; /* how much bytes we must serve */
  guint64 bytes_served; /* how much bytes have we served */

  GStaticRecMutex clientslock;	/* lock to protect the clients list */
  GList *clients;	/* list of clients we are serving */
  GHashTable *fd_hash;  /* index on fd to client */

  GstFDSetMode mode;
  GstFDSet *fdset;

  GstFD control_sock[2];/* sockets for controlling the select call */

  GSList *streamheader; /* GSList of GstBuffers to use as streamheader */
  gboolean previous_buffer_in_caps;

  GstTCPProtocolType protocol;
  guint mtu;

  GArray *bufqueue;	/* global queue of buffers */

  gboolean running;	/* the thread state */
  GThread *thread;	/* the sender thread */

  GstUnitType unit_type;/* the type of the units */
  gint units_max;	/* max units to queue */
  gint units_soft_max;	/* max units a client can lag before recovery starts */
  GstRecoverPolicy recover_policy;
  GstClockTime timeout;	/* max amount of nanoseconds to remain idle */
  GstSyncMethod sync_method;	/* what method to use for connecting clients */

  /* stats */
  gint buffers_queued;	/* number of queued buffers */
  gint bytes_queued;	/* number of queued bytes */
  gint time_queued;	/* number of queued time */
};

struct _GstMultiFdSinkClass {
  GstBaseSinkClass parent_class;

  /* element methods */
  void 		(*add)    	(GstMultiFdSink *sink, int fd);
  void 		(*remove) 	(GstMultiFdSink *sink, int fd);
  void 		(*clear)  	(GstMultiFdSink *sink);
  GValueArray* 	(*get_stats)  	(GstMultiFdSink *sink, int fd);

  /* vtable */
  gboolean (*init)   (GstMultiFdSink *sink);
  gboolean (*wait)   (GstMultiFdSink *sink, GstFDSet *set);
  gboolean (*close)  (GstMultiFdSink *sink);
  void (*removed) (GstMultiFdSink *sink, int fd);

  /* signals */
  void (*client_added) (GstElement *element, gchar *host, gint fd);
  void (*client_removed) (GstElement *element, gchar *host, gint fd);
};

GType gst_multifdsink_get_type (void);

void gst_multifdsink_add (GstMultiFdSink *sink, int fd);
void gst_multifdsink_remove (GstMultiFdSink *sink, int fd);
void gst_multifdsink_clear (GstMultiFdSink *sink);
GValueArray* gst_multifdsink_get_stats (GstMultiFdSink *sink, int fd);

G_END_DECLS

#endif /* __GST_MULTIFDSINK_H__ */
