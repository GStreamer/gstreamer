/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
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

#ifndef __GST_VULKAN_DISPLAY_H__
#define __GST_VULKAN_DISPLAY_H__

#include <gst/gst.h>

#include <vk.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_DISPLAY             (gst_vulkan_display_get_type())
#define GST_VULKAN_DISPLAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VULKAN_DISPLAY,GstVulkanDisplay))
#define GST_VULKAN_DISPLAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VULKAN_DISPLAY,GstVulkanDisplayClass))
#define GST_IS_VULKAN_DISPLAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VULKAN_DISPLAY))
#define GST_IS_VULKAN_DISPLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VULKAN_DISPLAY))
#define GST_VULKAN_DISPLAY_CAST(obj)        ((GstVulkanDisplay*)(obj))
#define GST_VULKAN_DISPLAY_GET_CLASS(o)     (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_DISPLAY, GstVulkanDisplayClass))
GType gst_vulkan_display_get_type (void);

typedef enum
{
  GST_VULKAN_DISPLAY_TYPE_NONE = 0,
  GST_VULKAN_DISPLAY_TYPE_X11 = (1 << 0),
  GST_VULKAN_DISPLAY_TYPE_XCB = (1 << 1),
  GST_VULKAN_DISPLAY_TYPE_WAYLAND = (1 << 2),
  GST_VULKAN_DISPLAY_TYPE_MIR = (1 << 3),
  GST_VULKAN_DISPLAY_TYPE_WIN32 = (1 << 4),

  GST_VULKAN_DISPLAY_TYPE_ANY = G_MAXUINT32
} GstVulkanDisplayType;

/**
 * GstVulkanDisplay:
 *
 * The contents of a #GstVulkanDisplay are private and should only be accessed
 * through the provided API
 */
struct _GstVulkanDisplay
{
  /* <private> */
  GstObject                 object;

  GstVulkanDisplayType      type;

  /* <protected> */
  GList                    *windows;        /* OBJECT lock */

  GstVulkanDisplayPrivate  *priv;
};

struct _GstVulkanDisplayClass
{
  GstObjectClass object_class;

  gpointer          (*get_handle)           (GstVulkanDisplay * display);
  gpointer          (*get_platform_handle)  (GstVulkanDisplay * display); /* default chains up to @get_handle */
  GstVulkanWindow * (*create_window)        (GstVulkanDisplay * display);
};

GstVulkanDisplay *gst_vulkan_display_new (void);

gpointer             gst_vulkan_display_get_handle             (GstVulkanDisplay * display);
GstVulkanDisplayType gst_vulkan_display_get_handle_type        (GstVulkanDisplay * display);
gpointer             gst_vulkan_display_get_platform_handle    (GstVulkanDisplay * display);
GstVulkanWindow *    gst_vulkan_display_create_window          (GstVulkanDisplay * display);

G_END_DECLS

#endif /* __GST_VULKAN_DISPLAY_H__ */
