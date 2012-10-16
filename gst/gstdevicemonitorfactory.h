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

/* Factory list functions */

/**
 * GstDeviceMonitorFactoryListType:
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_SINK: Sink elements
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_SRC: Source elements
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MAX_DEVICE_MONITORS: Private, do not use
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_VIDEO: Elements handling video media types
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_AUDIO: Elements handling audio media types
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_IMAGE: Elements handling image media types
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_SUBTITLE: Elements handling subtitle media types
 * @GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_METADATA: Elements handling metadata media types
 *
 * The type of #GstDeviceMonitorFactory to filter.
 *
 * All @GstDeviceMonitorFactoryListType up to @GST_DEVICE_MONITOR_FACTORY_TYPE_MAX_DEVICE_MONITORS are exclusive.
 *
 * If one or more of the MEDIA types are specified, then only elements
 * matching the specified media types will be selected.
 *
 * Since: 1.4
 */

typedef guint64 GstDeviceMonitorFactoryListType;

#define  GST_DEVICE_MONITOR_FACTORY_TYPE_SINK           (G_GUINT64_CONSTANT (1) << 0)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_SRC            (G_GUINT64_CONSTANT (1) << 1)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MAX_DEVICE_MONITORS   (G_GUINT64_CONSTANT (1) << 48)

#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_VIDEO    (G_GUINT64_CONSTANT (1) << 49)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_AUDIO    (G_GUINT64_CONSTANT (1) << 50)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_IMAGE    (G_GUINT64_CONSTANT (1) << 51)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_SUBTITLE (G_GUINT64_CONSTANT (1) << 52)
#define  GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_METADATA (G_GUINT64_CONSTANT (1) << 53)

/* Element klass defines */
#define GST_DEVICE_MONITOR_FACTORY_KLASS_DECODER               "Decoder"
#define GST_DEVICE_MONITOR_FACTORY_KLASS_ENCODER               "Encoder"

#define GST_DEVICE_MONITOR_FACTORY_KLASS_MEDIA_VIDEO           "Video"
#define GST_DEVICE_MONITOR_FACTORY_KLASS_MEDIA_AUDIO           "Audio"
#define GST_DEVICE_MONITOR_FACTORY_KLASS_MEDIA_IMAGE           "Image"
#define GST_DEVICE_MONITOR_FACTORY_KLASS_MEDIA_SUBTITLE        "Subtitle"
#define GST_DEVICE_MONITOR_FACTORY_KLASS_MEDIA_METADATA        "Metadata"

gboolean      gst_device_monitor_factory_list_is_type (GstDeviceMonitorFactory *factory,
                                                       GstDeviceMonitorFactoryListType type);

GList *       gst_device_monitor_factory_list_get_device_monitors (GstDeviceMonitorFactoryListType type,
                                                                   GstRank minrank) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __GST_DEVICE_MONITOR_FACTORY_H__ */
