/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstscheduler.h: Header for default scheduler code
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


#ifndef __GST_SCHEDULER_H__
#define __GST_SCHEDULER_H__

#include <gst/gstelement.h>
#include <gst/gstbin.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


extern GType _gst_schedule_type;

#define GST_TYPE_SCHEDULE                 (_gst_schedule_type)
# define GST_IS_SCHEDULE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SCHEDULE))
# define GST_IS_SCHEDULE_CLASS(obj)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SCHEDULE))

#define GST_SCHEDULE_FAST(obj)            ((GstSchedule*)(obj))
#define GST_SCHEDULE_CLASS_FAST(klass)    ((GstScheduleClass*)(klass))

#ifdef GST_TYPE_PARANOID
# define GST_SCHEDULE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SCHEDULE, GstSchedule))
# define GST_SCHEDULE_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SCHEDULE, GstScheduleClass))
#else
# define GST_SCHEDULE                     GST_SCHEDULE_FAST
# define GST_SCHEDULE_CLASS               GST_SCHEDULE_CLASS_FAST
#endif


#define GST_SCHED_PARENT(sched)		((sched)->parent)

//typedef struct _GstSchedule GstSchedule;
//typedef struct _GstScheduleClass GstScheduleClass;
typedef struct _GstScheduleChain GstScheduleChain;

struct _GstSchedule {
  GstObject object;

  GstElement *parent;

  GList *elements;
  gint num_elements;

  GList *chains;
  gint num_chains;

  void (*add_element)		(GstSchedule *sched, GstElement *element);
  void (*remove_element)	(GstSchedule *sched, GstElement *element);
  void (*enable_element)	(GstSchedule *sched, GstElement *element);
  void (*disable_element)	(GstSchedule *sched, GstElement *element);
  void (*lock_element)		(GstSchedule *sched, GstElement *element);
  void (*unlock_element)	(GstSchedule *sched, GstElement *element);
  void (*pad_connect)		(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
  void (*pad_disconnect)	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
  void (*pad_select)		(GstSchedule *sched, GList *padlist);
  gboolean (*iterate)		(GstSchedule *sched);
};

struct _GstScheduleClass {
  GstObjectClass parent_class;
};

#define GST_SCHEDULE_SAFETY(sched) if (sched)
//#define GST_SCHEDULE_SAFETY(sched)

#define GST_SCHEDULE_ADD_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->add_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_REMOVE_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->remove_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_ENABLE_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->enable_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_DISABLE_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->disable_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_LOCK_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { if ((sched)->lock_element != NULL) \
    ((sched)->lock_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_UNLOCK_ELEMENT(sched,element) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { if ((sched)->unlock_element != NULL) \
    ((sched)->unlock_element((sched),(element))); } \
}G_STMT_END
#define GST_SCHEDULE_PAD_CONNECT(sched,srcpad,sinkpad) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->pad_connect((sched),(srcpad),(sinkpad))); } \
}G_STMT_END
#define GST_SCHEDULE_PAD_DISCONNECT(sched,srcpad,sinkpad) G_STMT_START{ \
  GST_SCHEDULE_SAFETY(sched) { ((sched)->pad_disconnect((sched),(srcpad),(sinkpad))); } \
}G_STMT_END
#define GST_SCHEDULE_ITERATE(sched) \
  ((sched)->iterate((sched)))



struct _GstScheduleChain {
  GstSchedule *sched;

  GList *disabled;

  GList *elements;
  gint num_elements;

  GstElement *entry;

  gint cothreaded_elements;
  gboolean schedule;
};


GType			gst_schedule_get_type		(void);
GstSchedule*		gst_schedule_new		(GstElement *parent);

void			gst_schedule_add_element	(GstSchedule *sched, GstElement *element);
void			gst_schedule_remove_element	(GstSchedule *sched, GstElement *element);
void			gst_schedule_enable_element	(GstSchedule *sched, GstElement *element);
void			gst_schedule_disable_element	(GstSchedule *sched, GstElement *element);
void			gst_schedule_pad_connect	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
void			gst_schedule_pad_disconnect	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
GstPad*                 gst_schedule_pad_select         (GstSchedule *sched, GList *padlist);
gboolean		gst_schedule_iterate		(GstSchedule *sched);

void			gst_schedule_show		(GstSchedule *sched);


#ifdef __cplusplus   
}
#endif /* __cplusplus */

#endif /* __GST_SCHEDULER_H__ */
