/* GStreamer Navigation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * navigation.c: navigation design virtual class function wrappers
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/navigation/navigation.h>

static void gst_navigation_class_init (GstNavigationInterface * iface);

GType
gst_navigation_get_type (void)
{
  static GType gst_navigation_type = 0;

  if (!gst_navigation_type) {
    static const GTypeInfo gst_navigation_info = {
      sizeof (GstNavigationInterface),
      (GBaseInitFunc) gst_navigation_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_navigation_type = g_type_register_static (G_TYPE_INTERFACE,
	"GstNavigation", &gst_navigation_info, 0);
  }

  return gst_navigation_type;
}

static void
gst_navigation_class_init (GstNavigationInterface * iface)
{
  /* default virtual functions */
  iface->send_event = NULL;
}

void
gst_navigation_send_event (GstNavigation * navigation, GstStructure * structure)
{
  GstNavigationInterface *iface = GST_NAVIGATION_GET_IFACE (navigation);

  if (iface->send_event) {
    iface->send_event (navigation, structure);
  }
}

void
gst_navigation_send_key_event (GstNavigation * navigation, const char *event,
    const char *key)
{
  gst_navigation_send_event (navigation,
      gst_structure_new ("application/x-gst-navigation", "event", G_TYPE_STRING,
	  event, "key", G_TYPE_STRING, key, NULL));
}

void
gst_navigation_send_mouse_event (GstNavigation * navigation, const char *event,
    int button, double x, double y)
{
  gst_navigation_send_event (navigation,
      gst_structure_new ("application/x-gst-navigation", "event", G_TYPE_STRING,
	  event, "button", G_TYPE_INT, button, "pointer_x", G_TYPE_DOUBLE, x,
	  "pointer_y", G_TYPE_DOUBLE, y, NULL));
}
