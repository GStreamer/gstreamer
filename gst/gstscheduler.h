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


#define GST_TYPE_SCHEDULE \
  (gst_schedule_get_type())
#define GST_SCHEDULE(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_SCHEDULE,GstSchedule))
#define GST_SCHEDULE_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_SCHEDULE,GstScheduleClass))
#define GST_IS_SCHEDULE(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_SCHEDULE))
#define GST_IS_SCHEDULE_CLASS(klass) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_SCHEDULE))


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
  void (*pad_connect)		(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
  void (*pad_disconnect)	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
};

struct _GstScheduleClass {
  GstObjectClass parent_class;
};

#define GST_SCHEDULE_ADD_ELEMENT(sched,element) \
  ((sched)->add_element((sched),(element)))
#define GST_SCHEDULE_REMOVE_ELEMENT(sched,element) \
  ((sched)->remove_element((sched),(element)))
#define GST_SCHEDULE_PAD_CONNECT(sched,srcpad,sinkpad) \
  ((sched)->pad_connect((sched),(srcpad),(sinkpad)))
#define GST_SCHEDULE_PAD_DISCONNECT(sched,srcpad,sinkpad) \
  ((sched)->pad_disconnect((sched),(srcpad),(sinkpad)))



struct _GstScheduleChain {
  GstSchedule *sched;

  GList *elements;
  gint num_elements;

  GstElement *entry;

  gint cothreaded_elements;
  gboolean schedule;
};


void gst_bin_schedule_func(GstBin *bin);

GtkType		gst_schedule_get_type		(void);
GstSchedule *	gst_schedule_new		(GstElement *parent);

void	gst_schedule_add_element	(GstSchedule *sched, GstElement *element);
void	gst_schedule_remove_element	(GstSchedule *sched, GstElement *element);
void	gst_schedule_pad_connect	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);
void	gst_schedule_pad_disconnect	(GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad);

void	gst_schedule_show		(GstSchedule *sched);


#ifdef __cplusplus   
}
#endif /* __cplusplus */

#endif /* __GST_SCHEDULER_H__ */
