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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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
} GstClientStatus;

/* structure for a client
 *  */
typedef struct {
  GstFD fd;

  gint bufpos;                  /* position of this client in the global queue */

  GstClientStatus status;

  GSList *sending;              /* the buffers we need to send */
  gint bufoffset;               /* offset in the first buffer */

  gboolean discont;

  GstTCPProtocolType protocol;

  gboolean caps_sent;
  gboolean streamheader_sent;

  /* stats */
  guint64 bytes_sent;
  guint64 connect_time;
  guint64 disconnect_time;
  guint64 last_activity_time;
  guint64 dropped_buffers;
  guint64 avg_queue_size;
  
} GstTCPClient;

struct _GstMultiFdSink {
  GstElement element;

  /* pad */
  GstPad *sinkpad;

  guint64 bytes_to_serve; /* how much bytes we must serve */
  guint64 bytes_served; /* how much bytes have we served */

  GMutex *clientslock;	/* lock to protect the clients list */
  GList *clients;	/* list of clients we are serving */
  
  GstFDSetMode mode;
  GstFDSet *fdset;

  //fd_set readfds; /* all the client file descriptors that we can read from */
  //fd_set writefds; /* all the client file descriptors that we can write to */

  GstFD control_sock[2];/* sockets for controlling the select call */

  GSList *streamheader; /* GSList of GstBuffers to use as streamheader */
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

  /* stats */
  gint buffers_queued;	/* number of queued buffers */
  gint bytes_queued;	/* number of queued bytes */
  gint time_queued;	/* number of queued time */
};

struct _GstMultiFdSinkClass {
  GstElementClass parent_class;

  /* element methods */
  void 		(*add)    	(GstMultiFdSink *sink, int fd);
  void 		(*remove) 	(GstMultiFdSink *sink, int fd);
  void 		(*clear)  	(GstMultiFdSink *sink);
  GValueArray* 	(*get_stats)  	(GstMultiFdSink *sink, int fd);

  /* vtable */
  gboolean (*init)   (GstMultiFdSink *sink);
  gboolean (*wait)   (GstMultiFdSink *sink, GstFDSet *set);
  gboolean (*close)  (GstMultiFdSink *sink);

  /* signals */
  void (*client_added) (GstElement *element, gchar *host, gint fd);
  void (*client_removed) (GstElement *element, gchar *host, gint fd);
};

GType gst_multifdsink_get_type (void);

void gst_multifdsink_add (GstMultiFdSink *sink, int fd);
void gst_multifdsink_remove (GstMultiFdSink *sink, int fd);
void gst_multifdsink_clear (GstMultiFdSink *sink);
GValueArray* gst_multifdsink_get_stats (GstMultiFdSink *sink, int fd);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_MULTIFDSINK_H__ */
