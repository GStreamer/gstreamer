/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2004 Wim Taymans <wim@fluendo.com>
 *               2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstdevicemonitorfactory.h: Header for GstDeviceMonitorFactory
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



#ifndef __GST_DEVICE_MONITOR_FACTORY_H__
#define __GST_DEVICE_MONITOR_FACTORY_H__

/**
 * GstDeviceMonitorFactory:
 *
 * The opaque #GstDeviceMonitorFactory data structure.
 *
 * Since: 1.4
 */

/**
 * GstDeviceMonitorFactoryClass:
 *
 * The opaque #GstDeviceMonitorFactoryClass data structure.
 *
 * Since: 1.4
 */
typedef struct _GstDeviceMonitorFactory GstDeviceMonitorFactory;
typedef struct _GstDeviceMonitorFactoryClass GstDeviceMonitorFactoryClass;

#include <gst/gstconfig.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstdevicemonitor.h>

G_BEGIN_DECLS

#define GST_TYPE_DEVICE_MONITOR_FACTORY            (gst_device_monitor_factory_get_type())
#define GST_DEVICE_MONITOR_FACTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEVICE_MONITOR_FACTORY,\
                                                 GstDeviceMonitorFactory))
#define GST_DEVICE_MONITOR_FACTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEVICE_MONITOR_FACTORY,\
                                                 GstDeviceMonitorFactoryClass))
#define GST_IS_DEVICE_MONITOR_FACTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEVICE_MONITOR_FACTORY))
#define GST_IS_DEVICE_MONITOR_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEVICE_MONITOR_FACTORY))
#define GST_DEVICE_MONITOR_FACTORY_CAST(obj)       ((GstDeviceMonitorFactory *)(obj))

GType                   gst_device_monitor_factory_get_type          (void);

GstDeviceMonitorFactory * gst_device_monitor_factory_find            (const gchar *name);

GType                   gst_device_monitor_factory_get_device_monitor_type (GstDeviceMonitorFactory *factory);

const gchar *           gst_device_monitor_factory_get_metadata       (GstDeviceMonitorFactory *factory, const gchar *key);
gchar **                gst_device_monitor_factory_get_metadata_keys  (GstDeviceMonitorFactory *factory);

GstDeviceMonitor*       gst_device_monitor_factory_get                (GstDeviceMonitorFactory *factory) G_GNUC_MALLOC;
GstDeviceMonitor*       gst_device_monitor_factory_get_by_name        (const gchar *factoryname) G_GNUC_MALLOC;

gboolean                gst_device_monitor_register                   (GstPlugin *plugin, const gchar *name,
                                                                       guint rank,
                                                                       GType type);

gboolean      gst_device_monitor_factory_has_classesv (GstDeviceMonitorFactory * factory,
                                                       gchar ** classes);

gboolean      gst_device_monitor_factory_has_classes (GstDeviceMonitorFactory *factory,
                                                      const gchar * classes);

GList *       gst_device_monitor_factory_list_get_device_monitors (const gchar *classes,
                                                                   GstRank minrank) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __GST_DEVICE_MONITOR_FACTORY_H__ */
