/* GStreamer PropertyProbe
 * Copyright (C) 2003 David Schleef <ds@schleef.org>
 *
 * property_probe.c: property_probe design virtual class function wrappers
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

#include <gst/propertyprobe/propertyprobe.h>

static void gst_property_probe_iface_init (GstPropertyProbeInterface *iface);

enum {
  NEED_PROBE,
  LAST_SIGNAL
};

static guint gst_property_probe_signals[LAST_SIGNAL] = { 0 };

GType
gst_property_probe_get_type (void)
{
  static GType gst_property_probe_type = 0;

  if (!gst_property_probe_type) {
    static const GTypeInfo gst_property_probe_info = {
      sizeof (GstPropertyProbeInterface),
      (GBaseInitFunc) gst_property_probe_iface_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_property_probe_type = g_type_register_static (G_TYPE_INTERFACE,
					     "GstPropertyProbe",
					     &gst_property_probe_info, 0);

    g_type_interface_add_prerequisite (gst_property_probe_type, G_TYPE_OBJECT);
  }

  return gst_property_probe_type;
}

static void
gst_property_probe_iface_init (GstPropertyProbeInterface *iface)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    gst_property_probe_signals[NEED_PROBE] =
      g_signal_new ("need_probe",
          GST_TYPE_PROPERTY_PROBE,
          G_SIGNAL_RUN_LAST,
          G_STRUCT_OFFSET (GstPropertyProbeInterface, need_probe),
          NULL,
          NULL,
          gst_marshal_VOID__STRING,
          G_TYPE_NONE, 1,
          G_TYPE_STRING);

    initialized = TRUE;
  }

  /* default virtual functions */
  iface->get_list = NULL;
  iface->probe_property = NULL;
  iface->get_property_info = NULL;
  iface->is_probed = NULL;
}

char **
gst_property_probe_get_list (GstElement *element)
{
  GstPropertyProbeInterface *iface;
  
  g_return_val_if_fail (GST_IS_PROPERTY_PROBE (element), NULL);

  iface = GST_PROPERTY_PROBE_GET_IFACE (element);
  if (iface->get_list) {
    return iface->get_list (element);
  }

  return NULL;
}

void
gst_property_probe_probe_property (GstElement *element,
    const char *property)
{
  GstPropertyProbeInterface *iface;
  GParamSpec *ps;
  
  g_return_if_fail (GST_IS_PROPERTY_PROBE (element));
  g_return_if_fail (GST_STATE (element) == GST_STATE_NULL);

  ps = g_object_class_find_property (G_OBJECT_CLASS (element), property);
  if (ps == NULL) return;
  
  iface = GST_PROPERTY_PROBE_GET_IFACE (element);
  if (iface->probe_property) {
    iface->probe_property (element, ps);
  }
}

gchar **
gst_property_probe_get_property_info (GstElement *element,
    const gchar *property)
{
  GstPropertyProbeInterface *iface;
  GParamSpec *ps;
  
  g_return_val_if_fail (GST_IS_PROPERTY_PROBE (element), NULL);

  ps = g_object_class_find_property (G_OBJECT_CLASS (element), property);
  if (ps == NULL) return NULL;
  
  iface = GST_PROPERTY_PROBE_GET_IFACE (element);
  if (iface->get_property_info) {
    return iface->get_property_info (element, ps);
  }
  return NULL;
}

gboolean
gst_property_probe_is_probed (GstElement *element,
    const char *property)
{
  GstPropertyProbeInterface *iface;
  GParamSpec *ps;
  
  g_return_val_if_fail (GST_IS_PROPERTY_PROBE (element), FALSE);

  ps = g_object_class_find_property (G_OBJECT_CLASS (element), property);
  if (ps == NULL) return FALSE;

  iface = GST_PROPERTY_PROBE_GET_IFACE (element);
  if (iface->is_probed) {
    return iface->is_probed (element, ps);
  }
  return FALSE;
}

gchar **
gst_property_probe_get_possibilities (GstElement *element,
    const char *property)
{
  g_return_val_if_fail (GST_IS_PROPERTY_PROBE (element), NULL);

  if (!gst_property_probe_is_probed (element, property)){
    gst_property_probe_probe_property (element, property);
  }

  return gst_property_probe_get_property_info (element, property);
}

