/* GStreamer
 * Copyright (C) 2023 Matthieu Volat <matthieu.volat@ensimag.fr>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstossdeviceprovider.h"
#include "gstosshelper.h"
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <fcntl.h>
#include <unistd.h>


static GstDevice *gst_oss_device_new (const gchar * device_name, GstCaps * caps,
    const gchar * device, GstOssDeviceType type);

G_DEFINE_TYPE (GstOssDeviceProvider, gst_oss_device_provider,
    GST_TYPE_DEVICE_PROVIDER);


static GstDevice *
add_device (GstDeviceProvider * provider, GstOssDeviceType type, gint devno)
{
  gchar devpath[64];
  gchar mixpath[64];
  gint fd;
  GstCaps *caps;
  gchar *name;
  GstDevice *device;

  snprintf (devpath, sizeof (devpath), "/dev/dsp%u", devno);
  snprintf (mixpath, sizeof (mixpath), "/dev/mixer%u", devno);

  switch (type) {
    case GST_OSS_DEVICE_TYPE_SOURCE:
      fd = open (devpath, O_RDONLY);
      break;
    case GST_OSS_DEVICE_TYPE_SINK:
      fd = open (devpath, O_WRONLY);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  if (fd == -1) {
    GST_WARNING_OBJECT (provider, "Could open device %s for introspection",
        devpath);
    return NULL;
  }
  caps = gst_oss_helper_probe_caps (fd);
  close (fd);
  name = gst_oss_helper_get_card_name (mixpath);

  device = gst_oss_device_new (name, caps, devpath, type);
  g_free (name);
  return device;
}

static GList *
gst_oss_device_provider_probe (GstDeviceProvider * provider)
{
  FILE *sndstat_handle;
  gchar *sndstat_line = NULL;
  size_t sndstat_line_len = 0;
  gint sndstat_device_section = 0;
  gint ossdevno;
  gboolean play, rec;
  GstDevice *device;
  GList *list = NULL;

  GST_INFO_OBJECT (provider, "Probing OSS devices");
  if (((sndstat_handle = fopen ("/dev/sndstat", "r")) == NULL)
      && ((sndstat_handle = fopen ("/proc/sndstat", "r")) == NULL)
      && ((sndstat_handle = fopen ("/proc/asound/sndstat", "r")) == NULL)) {
    /* Cannot evaluate OSS devices without this file */
    GST_WARNING_OBJECT (provider, "No sndstat file found");
    goto beach;
  }

  while (!feof (sndstat_handle)) {
    if (getline (&sndstat_line, &sndstat_line_len, sndstat_handle) == -1) {
      break;
    }
    g_strstrip (sndstat_line);

    if (!sndstat_device_section) {
      sndstat_device_section = g_str_equal (sndstat_line, "Audio devices:")
          || g_str_equal (sndstat_line, "Installed devices:")
          || g_str_equal (sndstat_line, "Installed devices from userspace:");
      continue;
    }

    if ((sscanf (sndstat_line, "pcm%u:", &ossdevno) == 1)
        || (sscanf (sndstat_line, "%u:", &ossdevno) == 1)) {
      /* At least on FreeBSD, these keywords can be different if hw.snd.verbose is not 0 */
      if (strstr (sndstat_line, "(play/rec)") != NULL) {
        play = rec = TRUE;
      } else if (strstr (sndstat_line, "(play)") != NULL) {
        play = TRUE, rec = FALSE;
      } else if (strstr (sndstat_line, "(rec)") != NULL) {
        play = FALSE, rec = TRUE;
      } else {
        play = rec = FALSE;     /* Or should we assume play/rec? */
      }

      if (play) {
        device = add_device (provider, GST_OSS_DEVICE_TYPE_SINK, ossdevno);
        if (device != NULL) {
          list = g_list_append (list, device);
        }
      }
      if (rec) {
        device = add_device (provider, GST_OSS_DEVICE_TYPE_SOURCE, ossdevno);
        if (device != NULL) {
          list = g_list_append (list, device);
        }
      }
    }
  }

  free (sndstat_line);
  fclose (sndstat_handle);

beach:
  return list;
}


static void
gst_oss_device_provider_class_init (GstOssDeviceProviderClass * klass)
{
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);

  dm_class->probe = gst_oss_device_provider_probe;

  gst_device_provider_class_set_static_metadata (dm_class,
      "OSS Device Provider", "Sink/Source/Audio",
      "List and provides OSS source and sink devices",
      "Matthieu Volat <matthieu.volat@ensimag.fr>");
}

static void
gst_oss_device_provider_init (GstOssDeviceProvider * self)
{
}

/*** GstOssDevice implementation ******/
enum
{
  PROP_DEVICE_PATH = 1,
};


G_DEFINE_TYPE (GstOssDevice, gst_oss_device, GST_TYPE_DEVICE);

static GstElement *
gst_oss_device_create_element (GstDevice * device, const gchar * name)
{
  GstOssDevice *oss_dev = GST_OSS_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (oss_dev->element, name);
  g_object_set (elem, "device", oss_dev->device_path, NULL);

  return elem;
}

static gboolean
gst_oss_device_reconfigure_element (GstDevice * device, GstElement * element)
{
  GstOssDevice *oss_dev = GST_OSS_DEVICE (device);

  g_object_set (element, "device", oss_dev->device_path, NULL);

  return TRUE;
}

static void
gst_oss_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOssDevice *device;

  device = GST_OSS_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_value_set_string (value, device->device_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOssDevice *device;

  device = GST_OSS_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      device->device_path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss_device_finalize (GObject * object)
{
  GstOssDevice *device = GST_OSS_DEVICE (object);

  g_free (device->device_path);

  G_OBJECT_CLASS (gst_oss_device_parent_class)->finalize (object);
}

static void
gst_oss_device_class_init (GstOssDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_oss_device_create_element;
  dev_class->reconfigure_element = gst_oss_device_reconfigure_element;

  object_class->get_property = gst_oss_device_get_property;
  object_class->set_property = gst_oss_device_set_property;
  object_class->finalize = gst_oss_device_finalize;

  g_object_class_install_property (object_class, PROP_DEVICE_PATH,
      g_param_spec_string ("device-path", "OSS device path",
          "The path of the OSS device", "",
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_oss_device_init (GstOssDevice * device)
{
}

static GstDevice *
gst_oss_device_new (const gchar * device_name,
    GstCaps * caps, const gchar * device_path, GstOssDeviceType type)
{
  GstOssDevice *gstdev;
  const gchar *element = NULL;
  const gchar *klass = NULL;

  g_return_val_if_fail (device_name, NULL);
  g_return_val_if_fail (device_path, NULL);
  g_return_val_if_fail (caps, NULL);

  switch (type) {
    case GST_OSS_DEVICE_TYPE_SOURCE:
      element = "osssrc";
      klass = "Audio/Source";
      break;
    case GST_OSS_DEVICE_TYPE_SINK:
      element = "osssink";
      klass = "Audio/Sink";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  gstdev = g_object_new (GST_TYPE_OSS_DEVICE,
      "display-name", device_name, "caps", caps, "device-class", klass,
      "device-path", device_path, NULL);

  gstdev->element = element;

  gst_caps_unref (caps);

  return GST_DEVICE (gstdev);
}
