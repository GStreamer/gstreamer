/* GStreamer
 * Copyright (C) 1999-2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000-2002 Wim Taymans <wtay@chello.be>
 *
 * gstsearchfuncs.h: Header for gstsearchfuncs.c
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

#ifndef __GST_SEARCHFUNCS_H__
#define __GST_SEARCHFUNCS_H__

#include <gst/gst.h>

/* placeholder for maximum cost when plugging */
#define GST_AUTOPLUG_MAX_COST 999999

/* struct for a node, in the search tree */
typedef struct _GstAutoplugNode GstAutoplugNode;
	
struct _GstAutoplugNode {
	GstAutoplugNode    *prev;      /* previous node */
	GstElementFactory  *fac;       /* factory of element to connect to */
	GstPadTemplate     *templ;     /* template which can connect */
	guint               cost;      /* total cost to get here */
	GstPadTemplate     *endpoint;  /* pad template that can connect to sink caps */
};

/* helper functions */
gboolean				gst_autoplug_caps_intersect		(const GstCaps *src, const GstCaps *sink);
GstPadTemplate * 			gst_autoplug_can_connect_src            (GstElementFactory *fac, const GstCaps *src);
GstPadTemplate * 			gst_autoplug_can_connect_sink           (GstElementFactory *fac, const GstCaps *sink);
GstPadTemplate *                    	gst_autoplug_can_match                  (GstElementFactory *src, GstElementFactory *dest);
gboolean                            	gst_autoplug_factory_has_direction      (GstElementFactory *fac, GstPadDirection dir);
#define gst_autoplug_factory_has_sink(fac) gst_autoplug_factory_has_direction((fac), GST_PAD_SINK)
#define gst_autoplug_factory_has_src(fac) gst_autoplug_factory_has_direction((fac), GST_PAD_SRC)

/* cost functions */
#define gst_autoplug_get_cost(fac) 1

/* factory selections */
GList *					gst_autoplug_factories_sinks            (GList *factories);
GList *					gst_autoplug_factories_srcs             (GList *factories);
GList *					gst_autoplug_factories_filters          (GList *factories);
GList *					gst_autoplug_factories_filters_with_sink_caps(GList *factories);
GList *                             	gst_autoplug_factories_at_most_templates(GList *factories, GstPadDirection dir, guint maxtemplates);

/* shortest path algorithm */
GList *                             	gst_autoplug_sp                         (const GstCaps *src_caps, const GstCaps *sink_caps, GList *factories);

#endif /* __GST_SEARCHFUNCS_H__ */
