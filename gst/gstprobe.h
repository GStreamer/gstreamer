/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstprobe.h: Header for GstProbe object
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


#ifndef __GST_PROBE_H__
#define __GST_PROBE_H__

#include <glib.h>
#include <gst/gstdata.h>

G_BEGIN_DECLS

typedef struct _GstProbe GstProbe;

/* the callback should return FALSE if the data should be discarded */
typedef gboolean 		(*GstProbeCallback) 		(GstProbe *probe, 
								 GstData **data, 
								 gpointer user_data);

struct _GstProbe {
  gboolean		single_shot;
  
  GstProbeCallback 	callback;
  gpointer 		user_data;
};


GstProbe*		gst_probe_new 			(gboolean single_shot, 
							 GstProbeCallback callback, 
							 gpointer user_data);
void			gst_probe_destroy		(GstProbe *probe);

gboolean		gst_probe_perform 		(GstProbe *probe, GstData **data);

typedef struct _GstProbeDispatcher GstProbeDispatcher;

struct _GstProbeDispatcher {
  gboolean		active;
  
  GSList		*probes;
};

GstProbeDispatcher*	gst_probe_dispatcher_new 		(void);
void			gst_probe_dispatcher_destroy 		(GstProbeDispatcher *disp);
void			gst_probe_dispatcher_init 		(GstProbeDispatcher *disp);

void			gst_probe_dispatcher_set_active		(GstProbeDispatcher *disp, gboolean active);
void			gst_probe_dispatcher_add_probe		(GstProbeDispatcher *disp, GstProbe *probe);
void			gst_probe_dispatcher_remove_probe	(GstProbeDispatcher *disp, GstProbe *probe);

gboolean		gst_probe_dispatcher_dispatch		(GstProbeDispatcher *disp, GstData **data);

G_END_DECLS


#endif /* __GST_PAD_H__ */

