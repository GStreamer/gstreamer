/* GStreamer
 * 
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstinterface.c: Interface functions
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

#include "gstinterface.h"
#include "gstlog.h"

static void 	gst_interface_class_init 	(GstInterfaceClass *ifklass);
static gboolean	gst_interface_supported_default (GstInterface *iface,
						 GType         iface_type);

GType
gst_interface_get_type (void)
{
  static GType gst_interface_type = 0;

  if (!gst_interface_type) {
    static const GTypeInfo gst_interface_info = {
      sizeof (GstInterfaceClass),
      (GBaseInitFunc) gst_interface_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
      NULL
    };

    gst_interface_type = g_type_register_static (G_TYPE_INTERFACE,
						 "GstInterface",
						 &gst_interface_info, 0);
  }

  return gst_interface_type;
}

static void
gst_interface_class_init (GstInterfaceClass *klass)
{
  klass->supported = gst_interface_supported_default;
}

static gboolean
gst_interface_supported_default (GstInterface *interface,
				 GType         iface_type)
{
  /* Well, if someone didn't set the virtual function,
   * then something is clearly wrong. So big no-no here */

  return FALSE;
}

/**
 * gst_element_implements_interface:
 * @element: #GstElement to check for the implementation of the interface
 * @iface_type: (final) type of the interface which we want to be implemented
 *
 * Test whether the given element implements a certain interface of type
 * iface_type, and test whether it is supported for this specific instance.
 */

gboolean
gst_element_implements_interface (GstElement *element,
				  GType       iface_type)
{
  if (G_TYPE_CHECK_INSTANCE_TYPE (G_OBJECT (element),
				  iface_type)) {
    GstInterface *iface;
    GstInterfaceClass *ifclass;

    iface = G_TYPE_CHECK_INSTANCE_CAST (G_OBJECT (element),
					iface_type, GstInterface);
    ifclass = GST_INTERFACE_GET_CLASS (iface);

    if (ifclass->supported != NULL &&
        ifclass->supported (iface, iface_type) == TRUE) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_interface_cast:
 * @from: the object (any sort) from which to cast to the interface
 * @type: the interface type to cast to
 *
 * cast a given object to an interface type, and check whether this
 * interface is supported for this specific instance.
 */

GstInterface *
gst_interface_cast (gpointer from,
		    GType    iface_type)
{
  GstInterface *iface;

  /* check cast, give warning+fail if it's invalid */
  if (!(iface = G_TYPE_CHECK_INSTANCE_CAST (from, iface_type,
					    GstInterface))) {
    return NULL;
  }

  /* if we're an element, take care that this interface
   * is actually implemented */
  if (GST_IS_ELEMENT (from)) {
    gboolean interface_is_implemented =
	gst_element_implements_interface (GST_ELEMENT (from), iface_type);
    g_return_val_if_fail (interface_is_implemented == TRUE, NULL);
  }

  return iface;
}

/**
 * gst_interface_cast:
 * @from: the object (any sort) from which to check from for the interface
 * @type: the interface type to check for
 *
 * check a given object for an interface implementation, and check
 * whether this interface is supported for this specific instance.
 */

gboolean
gst_interface_check (gpointer from,
		     GType    type)
{
  GstInterface *iface;

  /* check cast, return FALSE if it fails, don't give a warning... */
  if (!G_TYPE_CHECK_INSTANCE_TYPE (from, type)) {
    return FALSE;
  }

  iface = G_TYPE_CHECK_INSTANCE_CAST (from, type, GstInterface);

  /* now, if we're an element (or derivative), is this thing
   * actually implemented for real? */
  if (GST_IS_ELEMENT (from)) {
    if (!gst_element_implements_interface (GST_ELEMENT (from), type)) {
      return FALSE;
    }
  }

  return TRUE;
}
