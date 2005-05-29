/* GStreamer
 * Copyright (C) 2004-2005 Benjamin Otte <otte@gnome.org>
 *
 * gstaction.h: base class for main actions/loops
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


#ifndef __GST_ACTION_H__
#define __GST_ACTION_H__

#include <glib.h>
#include <gst/gsttypes.h>
#include <gst/gstdata.h>

G_BEGIN_DECLS


#define GST_TYPE_ACTION 		(gst_action_get_type ())
#define GST_IS_ACTION(action) ((action) != NULL && \
    (action)->type > GST_ACTION_INVALID && \
    (action)->type < GST_ACTION_TYPE_COUNT)
#define GST_IS_ACTION_TYPE(action, _type) ((action) != NULL && \
    (action)->type == _type)

typedef enum {
  GST_ACTION_INVALID = 0,
  GST_ACTION_WAKEUP,
  GST_ACTION_SINK_PAD,
  GST_ACTION_SRC_PAD,
  GST_ACTION_FD,
  GST_ACTION_WAIT,
  /* add more */
  GST_ACTION_TYPE_COUNT
} GstActionType;

typedef struct _GstActionAny GstActionAny;
typedef struct _GstActionWakeup GstActionWakeup;
typedef struct _GstActionSinkPad GstActionSinkPad;
typedef struct _GstActionSrcPad GstActionSrcPad;
typedef struct _GstActionFd GstActionFd;
typedef struct _GstActionWait GstActionWait;

typedef void (* GstActionWakeupFunc)	(GstAction *	action,
					 GstElement *	element, 
					 gpointer	user_data);
typedef GstData * (* GstActionSrcPadFunc) (GstAction *	action,
					 GstRealPad *	pad);
typedef void (* GstActionSinkPadFunc)	(GstAction *	action,
					 GstRealPad *	pad, 
					 GstData *	data);
typedef void (* GstActionFdFunc)	(GstAction *	action, 
					 GstElement *	element, 
					 gint		fd,
					 GIOCondition	condition);
typedef void (* GstActionWaitFunc)	(GstAction *	action,
					 GstElement *	element,
					 GstClockTime	time);

#define GST_ACTION_HEAD \
  GstActionType		type; \
  guint			active : 1; \
  guint			initially_active : 1; \
  guint			coupled : 1; \
  guint			padding : 13; \
  GstElement *		element;
struct _GstActionAny {
  GST_ACTION_HEAD
};

struct _GstActionWakeup {
  GST_ACTION_HEAD
  GstActionWakeupFunc	release;
  gpointer		user_data;
};

struct _GstActionSrcPad {
  GST_ACTION_HEAD
  GstRealPad *		pad;
  GstActionSrcPadFunc	release;
};

struct _GstActionSinkPad {
  GST_ACTION_HEAD
  GstRealPad *		pad;
  GstActionSinkPadFunc	release;
};

struct _GstActionFd {
  GST_ACTION_HEAD
  int			fd;
  gushort		condition;
  GstActionFdFunc	release;
};

struct _GstActionWait {
  GST_ACTION_HEAD
  GstClockTime		time;
  GstClockTime		interval;
  GstActionWaitFunc	release;
};

/* FIXME: padding? */
union _GstAction {
  GstActionType		type;
  GstActionAny		any;
  GstActionWakeup	wakeup;
  GstActionSinkPad	sinkpad;
  GstActionSrcPad	srcpad;
  GstActionFd		fd;
  GstActionWait		wait;
};

GType			gst_action_get_type		(void);

GstElement *		gst_action_get_element		(const GstAction *	action);
void			gst_action_set_active		(GstAction *		action,
							 gboolean		active);
gboolean		gst_action_is_active		(GstAction *		action);
void			gst_action_set_initially_active	(GstAction *		action,
							 gboolean		active);
gboolean		gst_action_is_initially_active	(GstAction *		action);
void			gst_action_set_coupled		(GstAction *		action,
							 gboolean		coupled);
gboolean		gst_action_is_coupled		(GstAction *		action);
void			gst_element_add_action		(GstElement *		element,
							 GstAction *		action);
void			gst_element_remove_action	(GstAction *		action);

GstAction *		gst_element_add_wakeup		(GstElement *		element,
							 gboolean		active,
							 GstActionWakeupFunc	release,
							 gpointer		user_data);
void			gst_action_wakeup_release     	(GstAction *		action);

GstRealPad *		gst_action_get_pad		(const GstAction *	action);
void			gst_action_release_sink_pad  	(GstAction *		action,
							 GstData *		data);
GstData *		gst_action_release_src_pad   	(GstAction *		action);
GstAction *		gst_real_pad_get_action		(GstRealPad *		pad);

GstAction *   		gst_element_add_wait		(GstElement *		element, 
							 gboolean		active,
							 GstClockTime		start_time,
							 GstClockTime		interval,
							 GstActionWaitFunc	release);
void			gst_action_wait_change		(GstAction *		action,
			/* FIXME: better name? */	 GstClockTime           start_time,
							 GstClockTime           interval);
void			gst_action_wait_release		(GstAction *		action);

GstAction *	      	gst_element_add_fd		(GstElement *		element,
							 gboolean		active,
							 gint			fd,
							 gushort		condition,
							 GstActionFdFunc	release);
void			gst_action_fd_release		(GstAction *		action,
							 GIOCondition		condition);
void			gst_action_fd_change		(GstAction *		action,
							 gint			fd,
							 gushort		condition);

gchar *			gst_action_to_string		(const GstAction *	action);


G_END_DECLS

#endif /* __GST_ACTION_H__ */
