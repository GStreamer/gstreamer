/* GStreamer Navigation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
 *
 * navigation.h: navigation interface design
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

#ifndef __GST_NAVIGATION_H__
#define __GST_NAVIGATION_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_NAVIGATION \
  (gst_navigation_get_type ())
#define GST_NAVIGATION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_NAVIGATION, GstNavigation))
#define GST_IS_NAVIGATION(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_NAVIGATION))
#define GST_NAVIGATION_GET_IFACE(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_NAVIGATION, GstNavigationInterface))

typedef struct _GstNavigation GstNavigation;

typedef struct _GstNavigationInterface {
  GTypeInterface g_iface;

  /* virtual functions */
  void (*send_event) (GstNavigation *navigation, GstStructure *structure);
  
  gpointer _gst_reserved[GST_PADDING];
} GstNavigationInterface;

GType		gst_navigation_get_type	(void);

/* virtual class function wrappers */
void gst_navigation_send_event (GstNavigation *navigation, GstStructure *structure);

void gst_navigation_send_key_event (GstNavigation *navigation, 
	const char *event, const char *key);
void gst_navigation_send_mouse_event (GstNavigation *navigation, 
	const char *event, int button, double x, double y);

G_END_DECLS

#endif /* __GST_NAVIGATION_H__ */
