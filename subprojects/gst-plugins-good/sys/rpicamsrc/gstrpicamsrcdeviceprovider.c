/* GStreamer Raspberry Pi Camera Source Device Provider
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
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

#include "gstrpicamsrcdeviceprovider.h"

#include <string.h>

#include "RaspiCapture.h"

#include <glib/gi18n-lib.h>

static GstRpiCamSrcDevice *gst_rpi_cam_src_device_new (void);

G_DEFINE_TYPE (GstRpiCamSrcDeviceProvider, gst_rpi_cam_src_device_provider,
    GST_TYPE_DEVICE_PROVIDER);


static GList *gst_rpi_cam_src_device_provider_probe (GstDeviceProvider *
    provider);

static void
gst_rpi_cam_src_device_provider_class_init (GstRpiCamSrcDeviceProviderClass *
    klass)
{
  GstDeviceProviderClass *dprovider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  dprovider_class->probe = gst_rpi_cam_src_device_provider_probe;

  gst_device_provider_class_set_static_metadata (dprovider_class,
      "Raspberry Pi Camera Source Device Provider", "Source/Video",
      "Lists Raspberry Pi camera devices",
      "Tim-Philipp Müller <tim@centricular.com>");
}

static void
gst_rpi_cam_src_device_provider_init (GstRpiCamSrcDeviceProvider * provider)
{
  raspicapture_init ();
}

static GList *
gst_rpi_cam_src_device_provider_probe (GstDeviceProvider * provider)
{
  GstRpiCamSrcDevice *device;
  int supported = 0, detected = 0;

  raspicamcontrol_get_camera (&supported, &detected);

  if (!detected) {
    GST_INFO ("No Raspberry Pi camera module detected.");
    return NULL;
  } else if (!supported) {
    GST_WARNING
        ("Raspberry Pi camera module not supported, make sure to enable it.");
    return NULL;
  }

  GST_INFO ("Raspberry Pi camera module detected and supported.");

  device = gst_rpi_cam_src_device_new ();

  return g_list_append (NULL, device);
}

G_DEFINE_TYPE (GstRpiCamSrcDevice, gst_rpi_cam_src_device, GST_TYPE_DEVICE);

static GstElement *gst_rpi_cam_src_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_rpi_cam_src_device_class_init (GstRpiCamSrcDeviceClass * klass)
{
  GstDeviceClass *device_class = GST_DEVICE_CLASS (klass);

  device_class->create_element = gst_rpi_cam_src_device_create_element;
}

static void
gst_rpi_cam_src_device_init (GstRpiCamSrcDevice * device)
{
  /* nothing to do here */
}

static GstElement *
gst_rpi_cam_src_device_create_element (GstDevice * device, const gchar * name)
{
  return gst_element_factory_make ("rpicamsrc", name);
}

static GstRpiCamSrcDevice *
gst_rpi_cam_src_device_new (void)
{
  GstRpiCamSrcDevice *device;
  GValue profiles = G_VALUE_INIT;
  GValue val = G_VALUE_INIT;
  GstStructure *s, *jpeg_s;
  GstCaps *caps;

  /* FIXME: retrieve limits from the camera module, max width/height/fps etc. */
  s = gst_structure_new ("video/x-h264",
      "width", GST_TYPE_INT_RANGE, 1, 1920,
      "height", GST_TYPE_INT_RANGE, 1, 1080,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, RPICAMSRC_MAX_FPS, 1,
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

  g_value_init (&profiles, GST_TYPE_LIST);
  g_value_init (&val, G_TYPE_STRING);
  g_value_set_static_string (&val, "high");
  gst_value_list_append_value (&profiles, &val);
  g_value_set_static_string (&val, "main");
  gst_value_list_append_value (&profiles, &val);
  g_value_set_static_string (&val, "constrained-baseline");
  gst_value_list_append_value (&profiles, &val);
  g_value_set_static_string (&val, "baseline");
  gst_value_list_append_and_take_value (&profiles, &val);
  gst_structure_take_value (s, "profiles", &profiles);

  jpeg_s = gst_structure_new ("image/jpeg",
      "width", GST_TYPE_INT_RANGE, 1, 1920,
      "height", GST_TYPE_INT_RANGE, 1, 1080,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, RPICAMSRC_MAX_FPS, 1,
      "parsed", G_TYPE_BOOLEAN, "true", NULL);

  caps = gst_caps_new_full (s, jpeg_s, NULL);

  device = g_object_new (GST_TYPE_RPICAMSRC_DEVICE,
      "display-name", _("Raspberry Pi Camera Module"),
      "device-class", "Video/Source", "caps", caps, NULL);

  gst_caps_unref (caps);

  return device;
}
