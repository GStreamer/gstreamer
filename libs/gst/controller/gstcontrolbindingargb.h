/* GStreamer
 *
 * Copyright (C) 2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstcontrolbindingargb.h: Attachment for multiple control sources to gargb
 *                            properties
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

#ifndef __GST_CONTROL_BINDING_ARGB_H__
#define __GST_CONTROL_BINDING_ARGB_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

#include <gst/gstcontrolsource.h>

G_BEGIN_DECLS

#define GST_TYPE_CONTROL_BINDING_ARGB \
  (gst_control_binding_argb_get_type())
#define GST_CONTROL_BINDING_ARGB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CONTROL_BINDING_ARGB,GstControlBindingARGB))
#define GST_CONTROL_BINDING_ARGB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CONTROL_BINDING_ARGB,GstControlBindingARGBClass))
#define GST_IS_CONTROL_BINDING_ARGB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CONTROL_BINDING_ARGB))
#define GST_IS_CONTROL_BINDING_ARGB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CONTROL_BINDING_ARGB))
#define GST_CONTROL_BINDING_ARGB_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CONTOL_SOURCE, GstControlBindingARGBClass))

typedef struct _GstControlBindingARGB GstControlBindingARGB;
typedef struct _GstControlBindingARGBClass GstControlBindingARGBClass;

/**
 * GstControlBindingARGB:
 * @name: name of the property of this binding
 *
 * The instance structure of #GstControlBindingARGB.
 */
struct _GstControlBindingARGB {
  GstControlBinding parent;
  
  /*< private >*/
  GstControlSource *cs_a;       /* GstControlSources for this property */
  GstControlSource *cs_r;
  GstControlSource *cs_g;
  GstControlSource *cs_b;

  GValue cur_value;
  guint32 last_value;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstControlBindingARGBClass:
 * @parent_class: Parent class
 * @convert: Class method to convert control-values
 *
 * The class structure of #GstControlBindingARGB.
 */

struct _GstControlBindingARGBClass
{
  GstControlBindingClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_control_binding_argb_get_type (void);

/* Functions */

GstControlBinding * gst_control_binding_argb_new   (GstObject * object, const gchar * property_name,
                                                            GstControlSource * cs_a, GstControlSource * cs_r,
                                                            GstControlSource * cs_g, GstControlSource * cs_b);

G_END_DECLS

#endif /* __GST_CONTROL_BINDING_ARGB_H__ */
