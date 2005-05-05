/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstcollectpads.h: 
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

#ifndef __GST_COLLECTPADS_H__
#define __GST_COLLECTPADS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_COLLECTPADS  		(gst_collectpads_get_type())
#define GST_COLLECTPADS(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLLECTPADS,GstCollectPads))
#define GST_COLLECTPADS_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COLLECTPADS,GstCollectPadsClass))
#define GST_COLLECTPADS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_COLLECTPADS, GstCollectPadsClass))
#define GST_IS_COLLECTPADS(obj)  	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLLECTPADS))
#define GST_IS_COLLECTPADS_CLASS(obj)  	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLLECTPADS))

/* manages a set of pads that operate in collect mode. This means
 * that control is given to the manager of this object when all
 * pads have data.
 *
 * - pads are added to the collection with add/remove_pad. The pad
 *   has to be a sinkpad. The chain function of the pad is
 *   overridden. The element_private of the pad is used to store
 *   private information.
 * - For each pad, data is queued in the chain function or by
 *   performing a pull_range.
 * - When data is queued on all pads, a callback function is
 *   called.
 * - Data can be dequeued from the pad with the _pop() method. 
 *   One can _peek() at the data with the peek function.
 * - Data can also be dequeued with the available/read/flush
 *   calls.
 */

typedef struct _GstCollectPads GstCollectPads;
typedef struct _GstCollectPadsClass GstCollectPadsClass;

typedef struct _GstCollectData
{
  GstCollectPads 	*collect;
  GstPad		*pad;
  GstBuffer		*buffer;
  guint			 pos;
  gboolean		 eos;
} GstCollectData;

/* function will be called when all pads have data */
typedef GstFlowReturn (*GstCollectPadsFunction) (GstCollectPads *pads, gpointer user_data);

#define GST_COLLECTPADS_GET_COND(pads) (((GstCollectPads *)pads)->cond)
#define GST_COLLECTPADS_WAIT(pads)     (g_cond_wait (GST_COLLECTPADS_GET_COND (pads), GST_GET_LOCK (pads)))
#define GST_COLLECTPADS_SIGNAL(pads)   (g_cond_signal (GST_COLLECTPADS_GET_COND (pads)))
#define GST_COLLECTPADS_BROADCAST(pads)(g_cond_broadcast (GST_COLLECTPADS_GET_COND (pads)))

struct _GstCollectPads {
  GstObject      object;

  /*< public >*/ /* with LOCK */
  GSList	*data;			/* GstCollectData in this collection */
  guint32	 cookie;

  GCond		*cond;			/* to signal removal of data */

  /*< private >*/
  GstCollectPadsFunction func;		/* function and user_data for callback */
  gpointer	 user_data;

  guint		 numpads;		/* number of pads */
  guint		 queuedpads;		/* number of pads with a buffer */

  gboolean	 started;
};

struct _GstCollectPadsClass {
  GstObjectClass parent_class;

};

GType gst_collectpads_get_type(void);

/* creating the object */
GstCollectPads*	gst_collectpads_new	 	(void);

/* set the callback */
void		gst_collectpads_set_function 	(GstCollectPads *pads, GstCollectPadsFunction func,
						 gpointer user_data);

/* pad management */
GstCollectData*	gst_collectpads_add_pad		(GstCollectPads *pads, GstPad *pad, guint size);
gboolean	gst_collectpads_remove_pad	(GstCollectPads *pads, GstPad *pad);
gboolean	gst_collectpads_is_active 	(GstCollectPads *pads, GstPad *pad);

/* start/stop collection */
GstFlowReturn	gst_collectpads_collect 	(GstCollectPads *pads);
GstFlowReturn	gst_collectpads_collect_range 	(GstCollectPads *pads, guint64 offset, guint length);

void		gst_collectpads_start 		(GstCollectPads *pads);
void		gst_collectpads_stop 		(GstCollectPads *pads);

/* get collected buffers */
GstBuffer*	gst_collectpads_peek 		(GstCollectPads *pads, GstCollectData *data);
GstBuffer*	gst_collectpads_pop		(GstCollectPads *pads, GstCollectData *data);

/* get collected bytes */
guint 		gst_collectpads_available 	(GstCollectPads *pads);
guint		gst_collectpads_read		(GstCollectPads *pads, GstCollectData *data, 
						 guint8 **bytes, guint size);
guint 		gst_collectpads_flush 		(GstCollectPads *pads, GstCollectData *data, 
						 guint size);

G_END_DECLS

#endif /* __GST_COLLECTPADS_H__ */
