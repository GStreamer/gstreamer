/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sourceforge.net>
 *
 * gstcollectpads2.h:
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

#ifndef __GST_COLLECT_PADS2_H__
#define __GST_COLLECT_PADS2_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_COLLECT_PADS2  		 (gst_collect_pads2_get_type())
#define GST_COLLECT_PADS2(obj)  		 (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLLECT_PADS2,GstCollectPads2))
#define GST_COLLECT_PADS2_CLASS(klass) 	 (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COLLECT_PADS2,GstCollectPads2Class))
#define GST_COLLECT_PADS2_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_COLLECT_PADS2,GstCollectPads2Class))
#define GST_IS_COLLECT_PADS2(obj)  	 (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLLECT_PADS2))
#define GST_IS_COLLECT_PADS2_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLLECT_PADS2))

typedef struct _GstCollectData2 GstCollectData2;
typedef struct _GstCollectData2Private GstCollectData2Private;
typedef struct _GstCollectPads2 GstCollectPads2;
typedef struct _GstCollectPads2Private GstCollectPads2Private;
typedef struct _GstCollectPads2Class GstCollectPads2Class;

/**
 * GstCollectData2DestroyNotify:
 * @data: the #GstCollectData2 that will be freed
 *
 * A function that will be called when the #GstCollectData2 will be freed.
 * It is passed the pointer to the structure and should free any custom
 * memory and resources allocated for it.
 *
 * Since: 0.10.36
 */
typedef void (*GstCollectData2DestroyNotify) (GstCollectData2 *data);

/**
 * GstCollectPads2StateFlags:
 * @GST_COLLECT_PADS2_STATE_EOS:         Set if collectdata's pad is EOS.
 * @GST_COLLECT_PADS2_STATE_FLUSHING:    Set if collectdata's pad is flushing.
 * @GST_COLLECT_PADS2_STATE_NEW_SEGMENT: Set if collectdata's pad received a
 *                                      new_segment event.
 * @GST_COLLECT_PADS2_STATE_WAITING:     Set if collectdata's pad must be waited
 *                                      for when collecting.
 * @GST_COLLECT_PADS2_STATE_LOCKED:      Set collectdata's pad WAITING state must
 *                                      not be changed.
 * #GstCollectPads2StateFlags indicate private state of a collectdata('s pad).
 *
 * Since: 0.10.36
 */
typedef enum {
  GST_COLLECT_PADS2_STATE_EOS = 1 << 0,
  GST_COLLECT_PADS2_STATE_FLUSHING = 1 << 1,
  GST_COLLECT_PADS2_STATE_NEW_SEGMENT = 1 << 2,
  GST_COLLECT_PADS2_STATE_WAITING = 1 << 3,
  GST_COLLECT_PADS2_STATE_LOCKED = 1 << 4
} GstCollectPads2StateFlags;

/**
 * GST_COLLECT_PADS2_STATE:
 * @data: a #GstCollectData2.
 *
 * A flags word containing #GstCollectPads2StateFlags flags set
 * on this collected pad.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STATE(data)                 (((GstCollectData2 *) data)->state)
/**
 * GST_COLLECT_PADS2_STATE_IS_SET:
 * @data: a #GstCollectData2.
 * @flag: the #GstCollectPads2StateFlags to check.
 *
 * Gives the status of a specific flag on a collected pad.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STATE_IS_SET(data,flag)     !!(GST_COLLECT_PADS2_STATE (data) & flag)
/**
 * GST_COLLECT_PADS2_STATE_SET:
 * @data: a #GstCollectData2.
 * @flag: the #GstCollectPads2StateFlags to set.
 *
 * Sets a state flag on a collected pad.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STATE_SET(data,flag)        (GST_COLLECT_PADS2_STATE (data) |= flag)
/**
 * GST_COLLECT_PADS2_STATE_UNSET:
 * @data: a #GstCollectData2.
 * @flag: the #GstCollectPads2StateFlags to clear.
 *
 * Clears a state flag on a collected pad.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STATE_UNSET(data,flag)      (GST_COLLECT_PADS2_STATE (data) &= ~(flag))

/**
 * GstCollectData2:
 * @collect: owner #GstCollectPads2
 * @pad: #GstPad managed by this data
 * @buffer: currently queued buffer.
 * @pos: position in the buffer
 * @segment: last segment received.
 *
 * Structure used by the collect_pads2.
 *
 * Since: 0.10.36
 */
struct _GstCollectData2
{
  /* with STREAM_LOCK of @collect */
  GstCollectPads2 	*collect;
  GstPad		*pad;
  GstBuffer		*buffer;
  guint			 pos;
  GstSegment             segment;

  /*< private >*/
  /* state: bitfield for easier extension;
   * eos, flushing, new_segment, waiting */
  GstCollectPads2StateFlags    state;

  GstCollectData2Private *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstCollectPads2Function:
 * @pads: the #GstCollectPads2 that trigered the callback
 * @user_data: user data passed to gst_collect_pads2_set_function()
 *
 * A function that will be called when all pads have received data.
 *
 * Returns: #GST_FLOW_OK for success
 *
 * Since: 0.10.36
 */
typedef GstFlowReturn (*GstCollectPads2Function) (GstCollectPads2 *pads, gpointer user_data);

/**
 * GstCollectPads2BufferFunction:
 * @pads: the #GstCollectPads2 that trigered the callback
 * @data: the #GstCollectData2 of pad that has received the buffer
 * @buffer: the #GstBuffer
 * @user_data: user data passed to gst_collect_pads2_set_buffer_function()
 *
 * A function that will be called when a (considered oldest) buffer can be muxed.
 * If all pads have reached EOS, this function is called with NULL @buffer
 * and NULL @data.
 *
 * Returns: #GST_FLOW_OK for success
 *
 * Since: 0.10.36
 */
typedef GstFlowReturn (*GstCollectPads2BufferFunction) (GstCollectPads2 *pads, GstCollectData2 *data,
							GstBuffer *buffer, gpointer user_data);

/**
 * GstCollectPads2CompareFunction:
 * @pads: the #GstCollectPads that is comparing the timestamps
 * @data1: the first #GstCollectData2
 * @timestamp1: the first timestamp
 * @data2: the second #GstCollectData2
 * @timestamp2: the second timestamp
 * @user_data: user data passed to gst_collect_pads2_set_compare_function()
 *
 * A function for comparing two timestamps of buffers or newsegments collected on one pad.
 *
 * Returns: Integer less than zero when first timestamp is deemed older than the second one.
 *          Zero if the timestamps are deemed equally old.
 *          Integer greate than zero when second timestamp is deemed older than the first one.
 *
 * Since: 0.10.36
 */
typedef gint (*GstCollectPads2CompareFunction) (GstCollectPads2 *pads,
						GstCollectData2 * data1, GstClockTime timestamp1,
						GstCollectData2 * data2, GstClockTime timestamp2,
						gpointer user_data);

/**
 * GstCollectPads2EventFunction:
 * @pads: the #GstCollectPads2 that trigered the callback
 * @pad: the #GstPad that received an event
 * @event: the #GstEvent received
 * @user_data: user data passed to gst_collect_pads2_set_event_function()
 *
 * A function that will be called after collectpads has processed the event.
 *
 * Returns: %TRUE if the pad could handle the event
 *
 * Since: 0.10.36
 */
typedef gboolean (*GstCollectPads2EventFunction)	(GstCollectPads2 *pads, GstCollectData2 * pad,
							 GstEvent * event, gpointer user_data);


/**
 * GstCollectPads2ClipFunction:
 * @pads: a #GstCollectPads2 
 * @data: a #GstCollectData2
 * @inbuffer: the input #GstBuffer 
 * @outbuffer: the output #GstBuffer
 * @user_data: user data 
 *
 * A function that will be called when @inbuffer is received on the pad managed
 * by @data in the collecpad object @pads.
 *
 * The function should use the segment of @data and the negotiated media type on
 * the pad to perform clipping of @inbuffer. 
 *
 * This function takes ownership of @inbuffer and should output a buffer in
 * @outbuffer or return %NULL in @outbuffer if the buffer should be dropped.
 *
 * Returns: a #GstFlowReturn that corresponds to the result of clipping.
 *
 * Since: 0.10.36
 */
typedef GstFlowReturn (*GstCollectPads2ClipFunction) (GstCollectPads2 *pads, GstCollectData2 *data,
                                                   GstBuffer *inbuffer, GstBuffer **outbuffer,
                                                   gpointer user_data);

/**
 * GST_COLLECT_PADS2_GET_STREAM_LOCK:
 * @pads: a #GstCollectPads2
 *
 * Get the stream lock of @pads. The stream lock is used to coordinate and
 * serialize execution among the various streams being collected, and in
 * protecting the resources used to accomplish this.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_GET_STREAM_LOCK(pads) (&((GstCollectPads2 *)pads)->stream_lock)
/**
 * GST_COLLECT_PADS2_STREAM_LOCK:
 * @pads: a #GstCollectPads2
 *
 * Lock the stream lock of @pads.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STREAM_LOCK(pads)     (g_static_rec_mutex_lock(GST_COLLECT_PADS2_GET_STREAM_LOCK (pads)))
/**
 * GST_COLLECT_PADS2_STREAM_UNLOCK:
 * @pads: a #GstCollectPads2
 *
 * Unlock the stream lock of @pads.
 *
 * Since: 0.10.36
 */
#define GST_COLLECT_PADS2_STREAM_UNLOCK(pads)   (g_static_rec_mutex_unlock(GST_COLLECT_PADS2_GET_STREAM_LOCK (pads)))

/**
 * GstCollectPads2:
 * @data: #GList of #GstCollectData2 managed by this #GstCollectPads2.
 *
 * Collectpads object.
 *
 * Since: 0.10.36
 */
struct _GstCollectPads2 {
  GstObject      object;

  /*< public >*/ /* with LOCK and/or STREAM_LOCK */
  GSList	*data;                  /* list of CollectData items */

  /*< private >*/
  GStaticRecMutex stream_lock;		/* used to serialize collection among several streams */

  GstCollectPads2Private *priv;

  gpointer _gst_reserved[GST_PADDING];

};

struct _GstCollectPads2Class {
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_collect_pads2_get_type(void);

/* creating the object */
GstCollectPads2*	gst_collect_pads2_new	 	(void);

/* set the callbacks */
void		gst_collect_pads2_set_function 	(GstCollectPads2 *pads, GstCollectPads2Function func,
						 gpointer user_data);
void		gst_collect_pads2_set_buffer_function (GstCollectPads2 *pads,
     						 GstCollectPads2BufferFunction func, gpointer user_data);
void            gst_collect_pads2_set_event_function (GstCollectPads2 *pads,
    						 GstCollectPads2EventFunction func, gpointer user_data);
void		gst_collect_pads2_set_compare_function (GstCollectPads2 *pads,
    						 GstCollectPads2CompareFunction func, gpointer user_data);
void            gst_collect_pads2_set_clip_function (GstCollectPads2 *pads, GstCollectPads2ClipFunction clipfunc,
                                                 gpointer user_data);

/* pad management */
GstCollectData2* gst_collect_pads2_add_pad	(GstCollectPads2 *pads, GstPad *pad, guint size);
GstCollectData2* gst_collect_pads2_add_pad_full (GstCollectPads2 *pads, GstPad *pad, guint size,							 GstCollectData2DestroyNotify destroy_notify,
						 gboolean lock);
gboolean	gst_collect_pads2_remove_pad	(GstCollectPads2 *pads, GstPad *pad);
gboolean	gst_collect_pads2_is_active 	(GstCollectPads2 *pads, GstPad *pad);

/* start/stop collection */
GstFlowReturn	gst_collect_pads2_collect 	(GstCollectPads2 *pads);
GstFlowReturn	gst_collect_pads2_collect_range (GstCollectPads2 *pads, guint64 offset, guint length);

void		gst_collect_pads2_start 	(GstCollectPads2 *pads);
void		gst_collect_pads2_stop 		(GstCollectPads2 *pads);
void		gst_collect_pads2_set_flushing	(GstCollectPads2 *pads, gboolean flushing);

/* get collected buffers */
GstBuffer*	gst_collect_pads2_peek 		(GstCollectPads2 *pads, GstCollectData2 *data);
GstBuffer*	gst_collect_pads2_pop		(GstCollectPads2 *pads, GstCollectData2 *data);

/* get collected bytes */
guint 		gst_collect_pads2_available 	(GstCollectPads2 *pads);
guint		gst_collect_pads2_read		(GstCollectPads2 *pads, GstCollectData2 *data,
						 guint8 **bytes, guint size);
guint 		gst_collect_pads2_flush 	(GstCollectPads2 *pads, GstCollectData2 *data,
						 guint size);
GstBuffer*	gst_collect_pads2_read_buffer	(GstCollectPads2 * pads, GstCollectData2 * data,
						 guint size);
GstBuffer*	gst_collect_pads2_take_buffer	(GstCollectPads2 * pads, GstCollectData2 * data,
						 guint size);

/* setting and unsetting waiting mode */
void		gst_collect_pads2_set_waiting	(GstCollectPads2 *pads, GstCollectData2 *data,
						 gboolean waiting);


G_END_DECLS

#endif /* __GST_COLLECT_PADS22_H__ */
