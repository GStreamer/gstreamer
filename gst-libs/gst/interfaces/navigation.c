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

static void gst_navigation_class_init (GstNavigationIface *iface);

GType
gst_navigation_get_type (void)
{
  static GType gst_navigation_type = 0;

  if (!gst_navigation_type) {
    static const GTypeInfo gst_navigation_info = {
      sizeof (GstNavigationIface),
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
					     "GstNavigation",
					     &gst_navigation_info, 0);
  }

  return gst_navigation_type;
}

static void
gst_navigation_class_init (GstNavigationIface *iface)
{
  /* default virtual functions */
  iface->send_event = NULL;
}

void
gst_navigation_send_event (GstNavigation *navigation, GstCaps *caps)
{
  GstNavigationIface *iface = GST_NAVIGATION_GET_IFACE (navigation);

  if (iface->send_event) {
    iface->send_event (navigation, caps);
  }
}

void
gst_navigation_send_key_event (GstNavigation *navigation, const char *key)
{
  gst_navigation_send_event (navigation, GST_CAPS_NEW ("key_event",
	"application/x-gst-navigation",
	"key", GST_PROPS_STRING (key)));
}

void
gst_navigation_send_mouse_event (GstNavigation *navigation, double x,
        double y)
{
  gst_navigation_send_event (navigation, GST_CAPS_NEW ("mouse_event",
	"application/x-gst-navigation",
	"pointer_x", GST_PROPS_FLOAT (x),
	"pointer_y", GST_PROPS_FLOAT (y)));
}


