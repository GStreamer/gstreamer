/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmessage.h: Header for GstMessage subsystem
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

#ifndef __GST_MESSAGE_H__
#define __GST_MESSAGE_H__

G_BEGIN_DECLS

typedef struct _GstMessage GstMessage;
typedef struct _GstMessageClass GstMessageClass;

/**
 * GstMessageType:
 * @GST_MESSAGE_UNKNOWN: an undefined message
 * @GST_MESSAGE_EOS: end-of-stream reached in a pipeline
 * @GST_MESSAGE_ERROR: an error occured
 * @GST_MESSAGE_WARNING: a warning occured.
 * @GST_MESSAGE_INFO: an info message occured
 * @GST_MESSAGE_TAG: a tag was found.
 * @GST_MESSAGE_BUFFERING: the pipeline is buffering
 * @GST_MESSAGE_STATE_CHANGED: a state change happened
 * @GST_MESSAGE_STEP_DONE: a framestep finished.
 * @GST_MESSAGE_NEW_CLOCK: a new clock was selected in the pipeline
 * @GST_MESSAGE_STRUCTURE_CHANGE: the structure of the pipeline changed.
 * @GST_MESSAGE_STREAM_STATUS: status about a stream, emitted when it starts,
 *                             stops, errors, etc..
 * @GST_MESSAGE_APPLICATION: message posted by the application, possibly
 *                           via an application-specific element.
 * @GST_MESSAGE_SEGMENT_START: pipeline started playback of a segment.
 * @GST_MESSAGE_SEGMENT_DONE: pipeline completed playback of a segment.
 * @GST_MESSAGE_ANY: mask for all of the above messages.
 */
typedef enum
{
  GST_MESSAGE_UNKNOWN           = 0,
  GST_MESSAGE_EOS               = (1 << 0),
  GST_MESSAGE_ERROR             = (1 << 1),
  GST_MESSAGE_WARNING           = (1 << 2),
  GST_MESSAGE_INFO              = (1 << 3),
  GST_MESSAGE_TAG               = (1 << 4),
  GST_MESSAGE_BUFFERING         = (1 << 5),
  GST_MESSAGE_STATE_CHANGED     = (1 << 6),
  GST_MESSAGE_STEP_DONE         = (1 << 7),
  GST_MESSAGE_NEW_CLOCK         = (1 << 8),
  GST_MESSAGE_STRUCTURE_CHANGE  = (1 << 9),
  GST_MESSAGE_STREAM_STATUS     = (1 << 10),
  GST_MESSAGE_APPLICATION       = (1 << 11),
  GST_MESSAGE_SEGMENT_START     = (1 << 12),
  GST_MESSAGE_SEGMENT_DONE      = (1 << 13),
  GST_MESSAGE_ANY               = 0xffffffff
} GstMessageType;

#include <gst/gstminiobject.h>
#include <gst/gstobject.h>
#include <gst/gstelement.h>
#include <gst/gsttag.h>
#include <gst/gststructure.h>

#define GST_MESSAGE_TRACE_NAME	"GstMessage"

#define GST_TYPE_MESSAGE				(gst_message_get_type())
#define GST_IS_MESSAGE(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MESSAGE))
#define GST_IS_MESSAGE_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MESSAGE))
#define GST_MESSAGE_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MESSAGE, GstMessageClass))
#define GST_MESSAGE(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MESSAGE, GstMessage))
#define GST_MESSAGE_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MESSAGE, GstMessageClass))

/* the lock is used to handle the synchronous handling of messages,
 * the emiting thread is block until the handling thread processed
 * the message using this mutex/cond pair */
#define GST_MESSAGE_GET_LOCK(message)	(GST_MESSAGE(message)->lock)
#define GST_MESSAGE_LOCK(message)	g_mutex_lock(GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_UNLOCK(message)	g_mutex_unlock(GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_COND(message)	(GST_MESSAGE(message)->cond)
#define GST_MESSAGE_WAIT(message)	g_cond_wait(GST_MESSAGE_COND(message),GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_SIGNAL(message)	g_cond_signal(GST_MESSAGE_COND(message))

#define GST_MESSAGE_TYPE(message)	(GST_MESSAGE(message)->type)
#define GST_MESSAGE_TIMESTAMP(message)	(GST_MESSAGE(message)->timestamp)
#define GST_MESSAGE_SRC(message)	(GST_MESSAGE(message)->src)

struct _GstMessage
{
  GstMiniObject mini_object;

  /*< public > *//* with MESSAGE_LOCK */
  GMutex *lock;                 /* lock and cond for async delivery */
  GCond *cond;

  /*< public > *//* with COW */
  GstMessageType type;
  guint64 timestamp;
  GstObject *src;

  GstStructure *structure;
  
  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMessageClass {
  GstMiniObjectClass mini_object_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

void 		_gst_message_initialize 	(void);

GType 		gst_message_get_type 		(void);

/* refcounting */
#define         gst_message_ref(msg)		GST_MESSAGE (gst_mini_object_ref (GST_MINI_OBJECT (msg)))
#define         gst_message_unref(msg)		gst_mini_object_unref (GST_MINI_OBJECT (msg))
/* copy message */
#define         gst_message_copy(msg)		GST_MESSAGE (gst_mini_object_copy (GST_MINI_OBJECT (msg)))
#define         gst_message_make_writable(msg)	GST_MESSAGE (gst_mini_object_make_writable (GST_MINI_OBJECT (msg)))

GstMessage *	gst_message_new_eos 		(GstObject * src);
GstMessage *	gst_message_new_error 		(GstObject * src, GError * error, gchar * debug);
GstMessage *	gst_message_new_warning 	(GstObject * src, GError * error, gchar * debug);
GstMessage *	gst_message_new_tag 		(GstObject * src, GstTagList * tag_list);
GstMessage *	gst_message_new_state_changed 	(GstObject * src, GstState old_state,
                                                 GstState new_state);
GstMessage *	gst_message_new_segment_start 	(GstObject * src, GstClockTime timestamp);
GstMessage *	gst_message_new_segment_done 	(GstObject * src, GstClockTime timestamp);
GstMessage *	gst_message_new_custom 		(GstMessageType type,
						 GstObject    * src,
						 GstStructure * structure);

/**
 * gst_message_new_application:
 * @src: The object originating the message.
 * @str: The structure for the message. The message will take ownership of
 * the structure.
 *
 * Create a new application-typed message. This can be used for anything not
 * handled by other message-specific functions to pass a message to the
 * app. The structure field can be NULL.
 *
 * Returns: The new message.
 *
 * MT safe.
 */
#define		gst_message_new_application(src, str) \
  gst_message_new_custom (GST_MESSAGE_APPLICATION, src, str)

void		gst_message_parse_error		(GstMessage *message, GError **gerror, gchar **debug);
void		gst_message_parse_warning	(GstMessage *message, GError **gerror, gchar **debug);
void		gst_message_parse_tag		(GstMessage *message, GstTagList **tag_list);
void		gst_message_parse_state_changed	(GstMessage *message, GstState *old_state,
                                                 GstState *new_state);
void 		gst_message_parse_segment_start (GstMessage *message, GstClockTime *timestamp);
void		gst_message_parse_segment_done 	(GstMessage *message, GstClockTime *timestamp);

const GstStructure *  gst_message_get_structure	(GstMessage *message);

G_END_DECLS

#endif /* __GST_MESSAGE_H__ */
