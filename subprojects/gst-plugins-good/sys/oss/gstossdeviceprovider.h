/* GStreamer
 * Copyright (C) 2023 Matthieu Volat <mathieu.volat@ensimag.fr>
 *
 * ossdeviceprovider.c: OSS device probing and monitoring
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


#ifndef __GST_OSS_DEVICE_PROVIDER_H__
#define __GST_OSS_DEVICE_PROVIDER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstOssDeviceProvider GstOssDeviceProvider;
typedef struct _GstOssDeviceProviderClass GstOssDeviceProviderClass;

#define GST_TYPE_OSS_DEVICE_PROVIDER                 (gst_oss_device_provider_get_type())
#define GST_IS_OSS_DEVICE_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OSS_DEVICE_PROVIDER))
#define GST_IS_OSS_DEVICE_PROVIDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_OSS_DEVICE_PROVIDER))
#define GST_OSS_DEVICE_PROVIDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OSS_DEVICE_PROVIDER, GstOssDeviceProviderClass))
#define GST_OSS_DEVICE_PROVIDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OSS_DEVICE_PROVIDER, GstOssDeviceProvider))
#define GST_OSS_DEVICE_PROVIDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE_PROVIDER, GstOssDeviceProviderClass))
#define GST_OSS_DEVICE_PROVIDER_CAST(obj)            ((GstOssDeviceProvider *)(obj))

struct _GstOssDeviceProvider {
  GstDeviceProvider         parent;
};

struct _GstOssDeviceProviderClass {
  GstDeviceProviderClass    parent_class;
};

GType        gst_oss_device_provider_get_type (void);


typedef struct _GstOssDevice GstOssDevice;
typedef struct _GstOssDeviceClass GstOssDeviceClass;

#define GST_TYPE_OSS_DEVICE                 (gst_oss_device_get_type())
#define GST_IS_OSS_DEVICE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OSS_DEVICE))
#define GST_IS_OSS_DEVICE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_OSS_DEVICE))
#define GST_OSS_DEVICE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OSS_DEVICE, GstOssDeviceClass))
#define GST_OSS_DEVICE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OSS_DEVICE, GstOssDevice))
#define GST_OSS_DEVICE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE, GstOssDeviceClass))
#define GST_OSS_DEVICE_CAST(obj)            ((GstOssDevice *)(obj))

typedef enum
{
  GST_OSS_DEVICE_TYPE_INVALID = 0,
  GST_OSS_DEVICE_TYPE_SOURCE,
  GST_OSS_DEVICE_TYPE_SINK
} GstOssDeviceType;

struct _GstOssDevice {
  GstDevice         parent;

  gchar            *device_path;
  const gchar      *element;
};

struct _GstOssDeviceClass {
  GstDeviceClass    parent_class;
};

GType        gst_oss_device_get_type (void);

G_END_DECLS
#endif /* __GST_OSS_DEVICE_PROVIDER_H__ */
