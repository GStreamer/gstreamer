/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_GL_PROXY_CONTROL_BINDING_H__
#define __GST_GL_PROXY_CONTROL_BINDING_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

GType gst_gl_control_binding_proxy_get_type (void);
#define GST_TYPE_GL_CONTROL_BINDING_PROXY  (gst_gl_control_binding_proxy_get_type())

typedef struct _GstGLControlBindingProxy GstGLControlBindingProxy;
typedef struct _GstGLControlBindingProxyClass GstGLControlBindingProxyClass;

struct _GstGLControlBindingProxy
{
  GstControlBinding parent;

  GstObject *ref_object;
  const gchar *property_name;
};

struct _GstGLControlBindingProxyClass
{
  GstControlBindingClass parent_class;
};

GstControlBinding *     gst_gl_control_binding_proxy_new            (GstObject * object,
                                                                     const gchar * property_name,
                                                                     GstObject * ref_object,
                                                                     const gchar * ref_property_name);

void                    gst_gl_object_add_control_binding_proxy     (GstObject * object,
                                                                     GstObject * ref_object,
                                                                     const gchar * prop);

G_END_DECLS

#endif /* __GST_GL_PROXY_CONTROL_BINDING_H__ */
