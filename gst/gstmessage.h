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
 * @GST_MESSAGE_EOS: end-of-stream reached in a pipeline. The application will
 * only receive this message in the PLAYING state and every time it sets a
 * pipeline to PLAYING that is in the EOS state. The application can perform a
 * flushing seek in the pipeline, which will undo the EOS state again. 
 * @GST_MESSAGE_ERROR: an error occured. Whe the application receives an error
 * message it should stop playback of the pipeline and not assume that more
 * data will be played.
 * @GST_MESSAGE_WARNING: a warning occured.
 * @GST_MESSAGE_INFO: an info message occured
 * @GST_MESSAGE_TAG: a tag was found.
 * @GST_MESSAGE_BUFFERING: the pipeline is buffering. When the application
 * receives a buffering message in the PLAYING state for a non-live pipeline it
 * must PAUSE the pipeline until the buffering completes, when the percentage
 * field in the message is 100%. For live pipelines, no action must be
 * performed and the buffering percentage can be used to inform the user about
 * the progress.
 * @GST_MESSAGE_STATE_CHANGED: a state change happened
 * @GST_MESSAGE_STATE_DIRTY: an element changed state in a streaming thread.
 * This message is deprecated.
 * @GST_MESSAGE_STEP_DONE: a framestep finished. This message is not yet
 * implemented.
 * @GST_MESSAGE_CLOCK_PROVIDE: an element notifies its capability of providing
 *                             a clock. This message is used internally and
 *                             never forwarded to the application.
 * @GST_MESSAGE_CLOCK_LOST: The current clock as selected by the pipeline became
 *                          unusable. The pipeline will select a new clock on
 *                          the next PLAYING state change.
 * @GST_MESSAGE_NEW_CLOCK: a new clock was selected in the pipeline.
 * @GST_MESSAGE_STRUCTURE_CHANGE: the structure of the pipeline changed. Not
 * implemented yet.
 * @GST_MESSAGE_STREAM_STATUS: status about a stream, emitted when it starts,
 *                             stops, errors, etc.. Not implemented yet.
 * @GST_MESSAGE_APPLICATION: message posted by the application, possibly
 *                           via an application-specific element.
 * @GST_MESSAGE_ELEMENT: element-specific message, see the specific element's
 *                       documentation
 * @GST_MESSAGE_SEGMENT_START: pipeline started playback of a segment. This
 * message is used internally and never forwarded to the application.
 * @GST_MESSAGE_SEGMENT_DONE: pipeline completed playback of a segment. This
 * message is forwarded to the application after all elements that posted
 * @GST_MESSAGE_SEGMENT_START posted a GST_MESSAGE_SEGMENT_DONE message.
 * @GST_MESSAGE_DURATION: The duration of a pipeline changed. The application
 * can get the new duration with a duration query.
 * @GST_MESSAGE_ASYNC_START: Posted by elements when they start an ASYNC state
 * change. This message is not forwarded to the application but is used
 * internally. Since: 0.10.13. 
 * @GST_MESSAGE_ASYNC_DONE: Posted by elements when they complete an ASYNC state
 * change. The application will only receive this message from the toplevel
 * pipeline. Since: 0.10.13
 * @GST_MESSAGE_LATENCY: Posted by elements when their latency changes. The
 * pipeline will calculate and distribute a new latency. Since: 0.10.12
 * @GST_MESSAGE_ANY: mask for all of the above messages.
 *
 * The different message types that are available.
 */
/* NOTE: keep in sync with quark registration in gstmessage.c 
 * NOTE: keep GST_MESSAGE_ANY a valid gint to avoid compiler warnings.
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
  GST_MESSAGE_STATE_DIRTY       = (1 << 7),
  GST_MESSAGE_STEP_DONE         = (1 << 8),
  GST_MESSAGE_CLOCK_PROVIDE     = (1 << 9),
  GST_MESSAGE_CLOCK_LOST        = (1 << 10),
  GST_MESSAGE_NEW_CLOCK         = (1 << 11),
  GST_MESSAGE_STRUCTURE_CHANGE  = (1 << 12),
  GST_MESSAGE_STREAM_STATUS     = (1 << 13),
  GST_MESSAGE_APPLICATION       = (1 << 14),
  GST_MESSAGE_ELEMENT           = (1 << 15),
  GST_MESSAGE_SEGMENT_START     = (1 << 16),
  GST_MESSAGE_SEGMENT_DONE      = (1 << 17),
  GST_MESSAGE_DURATION          = (1 << 18),
  GST_MESSAGE_LATENCY           = (1 << 19),
  GST_MESSAGE_ASYNC_START       = (1 << 20),
  GST_MESSAGE_ASYNC_DONE        = (1 << 21),
  GST_MESSAGE_ANY               = ~0
} GstMessageType;

#include <gst/gstminiobject.h>
#include <gst/gstobject.h>
#include <gst/gstelement.h>
#include <gst/gsttaglist.h>
#include <gst/gststructure.h>

/**
 * GST_MESSAGE_TRACE_NAME:
 *
 * The name used for memory allocation tracing
 */
#define GST_MESSAGE_TRACE_NAME	"GstMessage"

#define GST_TYPE_MESSAGE			 (gst_message_get_type())
#define GST_IS_MESSAGE(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MESSAGE))
#define GST_IS_MESSAGE_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MESSAGE))
#define GST_MESSAGE_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MESSAGE, GstMessageClass))
#define GST_MESSAGE(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MESSAGE, GstMessage))
#define GST_MESSAGE_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MESSAGE, GstMessageClass))
#define GST_MESSAGE_CAST(obj)                    ((GstMessage*)(obj))

/* the lock is used to handle the synchronous handling of messages,
 * the emiting thread is block until the handling thread processed
 * the message using this mutex/cond pair */
#define GST_MESSAGE_GET_LOCK(message)	(GST_MESSAGE(message)->lock)
#define GST_MESSAGE_LOCK(message)	g_mutex_lock(GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_UNLOCK(message)	g_mutex_unlock(GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_COND(message)	(GST_MESSAGE(message)->cond)
#define GST_MESSAGE_WAIT(message)	g_cond_wait(GST_MESSAGE_COND(message),GST_MESSAGE_GET_LOCK(message))
#define GST_MESSAGE_SIGNAL(message)	g_cond_signal(GST_MESSAGE_COND(message))

/**
 * GST_MESSAGE_TYPE:
 * @message: a #GstMessage
 *
 * Get the #GstMessageType of @message.
 */
#define GST_MESSAGE_TYPE(message)	(GST_MESSAGE(message)->type)
/**
 * GST_MESSAGE_TYPE_NAME:
 * @message: a #GstMessage
 *
 * Get a constant string representation of the #GstMessageType of @message.
 *
 * Since: 0.10.4
 */
#define GST_MESSAGE_TYPE_NAME(message)	gst_message_type_get_name(GST_MESSAGE_TYPE(message))
/**
 * GST_MESSAGE_TIMESTAMP:
 * @message: a #GstMessage
 *
 * Get the timestamp of @message. This is the timestamp when the message
 * was created.
 */
#define GST_MESSAGE_TIMESTAMP(message)	(GST_MESSAGE(message)->timestamp)
/**
 * GST_MESSAGE_SRC:
 * @message: a #GstMessage
 *
 * Get the object that posted @message.
 */
#define GST_MESSAGE_SRC(message)	(GST_MESSAGE(message)->src)

/**
 * GstMessage:
 * @mini_object: the parent structure
 * @type: the #GstMessageType of the message
 * @timestamp: the timestamp of the message
 * @src: the src of the message
 * @structure: the #GstStructure containing the message info.
 *
 * A #GstMessage.
 */
struct _GstMessage
{
  GstMiniObject mini_object;

  /*< private > *//* with MESSAGE_LOCK */
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

GType		gst_message_get_type		(void);

const gchar*	gst_message_type_get_name	(GstMessageType type);
GQuark		gst_message_type_to_quark	(GstMessageType type);

/* refcounting */
/**
 * gst_message_ref:
 * @msg: the message to ref
 *
 * Convenience macro to increase the reference count of the message.
 *
 * Returns: @msg (for convenience when doing assignments)
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstMessage * gst_message_ref (GstMessage * msg);
#endif

static inline GstMessage *
gst_message_ref (GstMessage * msg)
{
  /* not using a macro here because gcc-4.1 will complain
   * if the return value isn't used (because of the cast) */
  return (GstMessage *) gst_mini_object_ref (GST_MINI_OBJECT (msg));
}

/**
 * gst_message_unref:
 * @msg: the message to unref
 *
 * Convenience macro to decrease the reference count of the message, possibly
 * freeing it.
 */
#define         gst_message_unref(msg)		gst_mini_object_unref (GST_MINI_OBJECT (msg))
/* copy message */
/**
 * gst_message_copy:
 * @msg: the message to copy
 *
 * Creates a copy of the message. Returns a copy of the message.
 *
 * MT safe
 */
#define         gst_message_copy(msg)		GST_MESSAGE (gst_mini_object_copy (GST_MINI_OBJECT (msg)))
/**
 * gst_message_make_writable:
 * @msg: the message to make writable
 *
 * Checks if a message is writable. If not, a writable copy is made and
 * returned. Returns a message (possibly a duplicate) that is writable.
 *
 * MT safe
 */
#define         gst_message_make_writable(msg)	GST_MESSAGE (gst_mini_object_make_writable (GST_MINI_OBJECT (msg)))

/* EOS */
GstMessage *	gst_message_new_eos		(GstObject * src);

/* ERROR */

GstMessage *	gst_message_new_error		(GstObject * src, GError * error, const gchar * debug);
void		gst_message_parse_error		(GstMessage *message, GError **gerror, gchar **debug);

/* WARNING */
GstMessage *	gst_message_new_warning		(GstObject * src, GError * error, const gchar * debug);
void		gst_message_parse_warning	(GstMessage *message, GError **gerror, gchar **debug);

/* INFO */
GstMessage *	gst_message_new_info		(GstObject * src, GError * error, const gchar * debug);
void		gst_message_parse_info 		(GstMessage *message, GError **gerror, gchar **debug);

/* TAG */
GstMessage *	gst_message_new_tag		(GstObject * src, GstTagList * tag_list);
void		gst_message_parse_tag		(GstMessage *message, GstTagList **tag_list);

/* BUFFERING */
GstMessage *	gst_message_new_buffering	(GstObject * src, gint percent);
void 		gst_message_parse_buffering	(GstMessage *message, gint *percent);
void            gst_message_set_buffering_stats (GstMessage *message, GstBufferingMode mode,
		                                 gint avg_in, gint avg_out,
						 gint64 buffering_left);
void            gst_message_parse_buffering_stats (GstMessage *message, GstBufferingMode *mode,
                                                   gint *avg_in, gint *avg_out,
                                                   gint64 *buffering_left);

/* STATE_CHANGED */
GstMessage *	gst_message_new_state_changed	(GstObject * src, GstState oldstate,
                                                 GstState newstate, GstState pending);
void		gst_message_parse_state_changed	(GstMessage *message, GstState *oldstate,
                                                 GstState *newstate, GstState *pending);

/* STATE_DIRTY */
GstMessage *	gst_message_new_state_dirty	(GstObject * src);

/* CLOCK_PROVIDE */
GstMessage *	gst_message_new_clock_provide	(GstObject * src, GstClock *clock, gboolean ready);
void		gst_message_parse_clock_provide (GstMessage *message, GstClock **clock,
                                                 gboolean *ready);

/* CLOCK_LOST */
GstMessage *	gst_message_new_clock_lost	(GstObject * src, GstClock *clock);
void		gst_message_parse_clock_lost	(GstMessage *message, GstClock **clock);

/* NEW_CLOCK */
GstMessage *	gst_message_new_new_clock	(GstObject * src, GstClock *clock);
void		gst_message_parse_new_clock	(GstMessage *message, GstClock **clock);

/* APPLICATION */
GstMessage *	gst_message_new_application	(GstObject * src, GstStructure * structure);

/* ELEMENT */
GstMessage *	gst_message_new_element		(GstObject * src, GstStructure * structure);

/* SEGMENT_START */
GstMessage *	gst_message_new_segment_start	(GstObject * src, GstFormat format, gint64 position);
void		gst_message_parse_segment_start (GstMessage *message, GstFormat *format,
                                                 gint64 *position);

/* SEGMENT_DONE */
GstMessage *	gst_message_new_segment_done	(GstObject * src, GstFormat format, gint64 position);
void		gst_message_parse_segment_done	(GstMessage *message, GstFormat *format,
                                                 gint64 *position);

/* DURATION */
GstMessage *	gst_message_new_duration	(GstObject * src, GstFormat format, gint64 duration);
void		gst_message_parse_duration	(GstMessage *message, GstFormat *format,
                                                 gint64 *duration);

/* LATENCY */
GstMessage *	gst_message_new_latency         (GstObject * src);

/* ASYNC_START */
GstMessage *	gst_message_new_async_start	(GstObject * src, gboolean new_base_time);
void		gst_message_parse_async_start	(GstMessage *message, gboolean *new_base_time);

/* ASYNC_DONE */
GstMessage *	gst_message_new_async_done	(GstObject * src);

/* custom messages */
GstMessage *	gst_message_new_custom		(GstMessageType type,
						 GstObject    * src,
						 GstStructure * structure);
const GstStructure *  gst_message_get_structure	(GstMessage *message);

G_END_DECLS

#endif /* __GST_MESSAGE_H__ */
