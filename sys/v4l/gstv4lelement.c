/* GStreamer
 *
 * gstv4lelement.c: base class for V4L elements
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <gst/interfaces/propertyprobe.h>

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

#include "v4l_calls.h"
#include "gstv4ltuner.h"
#ifdef HAVE_XVIDEO
#include "gstv4lxoverlay.h"
#endif
#include "gstv4lcolorbalance.h"


enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_FLAGS
};

GST_DEBUG_CATEGORY (v4lelement_debug);
#define GST_CAT_DEFAULT v4lelement_debug

static void gst_v4lelement_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstV4lElement, gst_v4lelement, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_v4lelement_init_interfaces);

static void gst_v4lelement_dispose (GObject * object);
static void gst_v4lelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* element methods */
static GstStateChangeReturn gst_v4lelement_change_state (GstElement * element,
    GstStateChange transition);

static gboolean
gst_v4l_iface_supported (GstImplementsInterface * iface, GType iface_type)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (iface);

#ifdef HAVE_XVIDEO
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_X_OVERLAY || iface_type == GST_TYPE_COLOR_BALANCE);
#else
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_COLOR_BALANCE);
#endif

  if (v4lelement->video_fd == -1)
    return FALSE;

#ifdef HAVE_XVIDEO
  if (iface_type == GST_TYPE_X_OVERLAY && !GST_V4L_IS_OVERLAY (v4lelement))
    return FALSE;
#endif

  return TRUE;
}

static void
gst_v4l_interface_init (GstImplementsInterfaceClass * klass)
{
  /* default virtual functions */
  klass->supported = gst_v4l_iface_supported;
}

static const GList *
gst_v4l_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  return list;
}

static gboolean init = FALSE;
static GList *devices = NULL;

#ifdef HAVE_GUDEV
static gboolean
gst_v4l_class_probe_devices_with_udev (GstV4lElementClass * klass,
    gboolean check)
{
  GUdevClient *client = NULL;
  GList *item;

  if (!check) {
    while (devices) {
      gchar *device = devices->data;
      devices = g_list_remove (devices, device);
      g_free (device);
    }

    GST_INFO ("Enumerating video4linux devices from udev");
    client = g_udev_client_new (NULL);
    if (!client) {
      GST_WARNING ("Failed to initialize gudev client");
      goto finish;
    }

    item = g_udev_client_query_by_subsystem (client, "video4linux");
    while (item) {
      GUdevDevice *device = item->data;
      gchar *devnode = g_strdup (g_udev_device_get_device_file (device));
      gint api = g_udev_device_get_property_as_int (device, "ID_V4L_VERSION");
      GST_INFO ("Found new device: %s, API: %d", devnode, api);
      /* Append v4l1 devices only. If api is 0 probably v4l_id has
         been stripped out of the current udev installation, append
         anyway */
      if (api == 0) {
        GST_WARNING
            ("Couldn't retrieve ID_V4L_VERSION, silly udev installation?");
      }
      if ((api == 1 || api == 0)) {
        devices = g_list_append (devices, devnode);
      } else {
        g_free (devnode);
      }
      g_object_unref (device);
      item = item->next;
    }
    g_list_free (item);
    init = TRUE;
  }

finish:
  if (client) {
    g_object_unref (client);
  }

  klass->devices = devices;

  return init;
}
#endif /* HAVE_GUDEV */

static gboolean
gst_v4l_class_probe_devices (GstV4lElementClass * klass, gboolean check)
{
  if (!check) {
    const gchar *dev_base[] = { "/dev/video", "/dev/v4l/video", NULL };
    gint base, n, fd;

    while (devices) {
      gchar *device = devices->data;
      devices = g_list_remove (devices, device);
      g_free (device);
    }

    /* detect /dev entries */
    for (n = 0; n < 64; n++) {
      for (base = 0; dev_base[base] != NULL; base++) {
        struct stat s;
        gchar *device = g_strdup_printf ("%s%d", dev_base[base], n);

        /* does the /dev/ entry exist at all? */
        if (stat (device, &s) == 0) {
          /* yes: is a device attached? */
          if ((fd = open (device, O_RDONLY)) > 0 || errno == EBUSY) {
            if (fd > 0)
              close (fd);

            devices = g_list_append (devices, device);
            break;
          }
        }
        g_free (device);
      }
    }

    init = TRUE;
  }

  klass->devices = devices;

  return init;
}

static void
gst_v4l_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4lElementClass *klass = GST_V4LELEMENT_GET_CLASS (probe);

  switch (prop_id) {
    case PROP_DEVICE:
#ifdef HAVE_GUDEV
      if (!gst_v4l_class_probe_devices_with_udev (klass, FALSE))
        gst_v4l_class_probe_devices (klass, FALSE);
#else /* !HAVE_GUDEV */
      gst_v4l_class_probe_devices (klass, FALSE);
#endif /* HAVE_GUDEV */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_v4l_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4lElementClass *klass = GST_V4LELEMENT_GET_CLASS (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case PROP_DEVICE:
#ifdef HAVE_GUDEV
      ret = !gst_v4l_class_probe_devices_with_udev (klass, FALSE);
#else /* !HAVE_GUDEV */
      ret = !gst_v4l_class_probe_devices (klass, TRUE);
#endif /* HAVE_GUDEV */
      ret = !gst_v4l_class_probe_devices (klass, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static GValueArray *
gst_v4l_class_list_devices (GstV4lElementClass * klass)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!klass->devices)
    return NULL;

  array = g_value_array_new (g_list_length (klass->devices));
  item = klass->devices;
  g_value_init (&value, G_TYPE_STRING);
  while (item) {
    gchar *device = item->data;

    g_value_set_string (&value, device);
    g_value_array_append (array, &value);

    item = item->next;
  }
  g_value_unset (&value);

  return array;
}

static GValueArray *
gst_v4l_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4lElementClass *klass = GST_V4LELEMENT_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE:
      array = gst_v4l_class_list_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_v4l_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_v4l_probe_get_properties;
  iface->probe_property = gst_v4l_probe_probe_property;
  iface->needs_probe = gst_v4l_probe_needs_probe;
  iface->get_values = gst_v4l_probe_get_values;
}

#define GST_TYPE_V4L_DEVICE_FLAGS (gst_v4l_device_get_type ())
static GType
gst_v4l_device_get_type (void)
{
  static GType v4l_device_type = 0;

  if (v4l_device_type == 0) {
    static const GFlagsValue values[] = {
      {VID_TYPE_CAPTURE, "CAPTURE", "Device can capture"},
      {VID_TYPE_TUNER, "TUNER", "Device has a tuner"},
      {VID_TYPE_OVERLAY, "OVERLAY", "Device can do overlay"},
      {VID_TYPE_MPEG_DECODER, "MPEG_DECODER", "Device can decode MPEG"},
      {VID_TYPE_MPEG_ENCODER, "MPEG_ENCODER", "Device can encode MPEG"},
      {VID_TYPE_MJPEG_DECODER, "MJPEG_DECODER", "Device can decode MJPEG"},
      {VID_TYPE_MJPEG_ENCODER, "MJPEG_ENCODER", "Device can encode MJPEG"},
      {0x10000, "AUDIO", "Device handles audio"},
      {0, NULL, NULL}
    };

    v4l_device_type = g_flags_register_static ("GstV4lDeviceTypeFlags", values);
  }

  return v4l_device_type;
}

static void
gst_v4lelement_init_interfaces (GType type)
{
  static const GInterfaceInfo v4liface_info = {
    (GInterfaceInitFunc) gst_v4l_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l_tuner_info = {
    (GInterfaceInitFunc) gst_v4l_tuner_interface_init,
    NULL,
    NULL,
  };
#ifdef HAVE_XVIDEO
  static const GInterfaceInfo v4l_xoverlay_info = {
    (GInterfaceInitFunc) gst_v4l_xoverlay_interface_init,
    NULL,
    NULL,
  };
#endif
  static const GInterfaceInfo v4l_colorbalance_info = {
    (GInterfaceInitFunc) gst_v4l_color_balance_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l_propertyprobe_info = {
    (GInterfaceInitFunc) gst_v4l_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &v4liface_info);
  g_type_add_interface_static (type, GST_TYPE_TUNER, &v4l_tuner_info);
#ifdef HAVE_XVIDEO
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &v4l_xoverlay_info);
#endif
  g_type_add_interface_static (type,
      GST_TYPE_COLOR_BALANCE, &v4l_colorbalance_info);
  g_type_add_interface_static (type,
      GST_TYPE_PROPERTY_PROBE, &v4l_propertyprobe_info);
}


static void
gst_v4lelement_base_init (gpointer g_class)
{
  GstV4lElementClass *klass = GST_V4LELEMENT_CLASS (g_class);

  klass->devices = NULL;

  GST_DEBUG_CATEGORY_INIT (v4lelement_debug, "v4lelement", 0,
      "V4L Base Class debug");
}

static void
gst_v4lelement_class_init (GstV4lElementClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_v4lelement_set_property;
  gobject_class->get_property = gst_v4lelement_get_property;
  gobject_class->dispose = gst_v4lelement_dispose;

  element_class->change_state = gst_v4lelement_change_state;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Device location",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name", "Name of the device",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Device type flags",
          GST_TYPE_V4L_DEVICE_FLAGS, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

}


static void
gst_v4lelement_init (GstV4lElement * v4lelement, GstV4lElementClass * klass)
{
  /* some default values */
  v4lelement->video_fd = -1;
  v4lelement->buffer = NULL;
  v4lelement->videodev = g_strdup ("/dev/video0");

  v4lelement->norms = NULL;
  v4lelement->channels = NULL;
  v4lelement->colors = NULL;

  v4lelement->xwindow_id = 0;
}


static void
gst_v4lelement_dispose (GObject * object)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (object);

  if (v4lelement->videodev) {
    g_free (v4lelement->videodev);
    v4lelement->videodev = NULL;
  }

  if (((GObjectClass *) parent_class)->dispose)
    ((GObjectClass *) parent_class)->dispose (object);
}


static void
gst_v4lelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      if (v4lelement->videodev)
        g_free (v4lelement->videodev);
      v4lelement->videodev = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, v4lelement->videodev);
      break;
    case PROP_DEVICE_NAME:{
      gchar *new = NULL;

      if (GST_V4L_IS_OPEN (v4lelement)) {
        new = v4lelement->vcap.name;
      } else if (gst_v4l_open (v4lelement)) {
        new = v4lelement->vcap.name;
        gst_v4l_close (v4lelement);
      }
      g_value_set_string (value, new);
      break;
    }
    case PROP_FLAGS:{
      guint flags = 0;

      if (GST_V4L_IS_OPEN (v4lelement)) {
        flags |= v4lelement->vcap.type & 0x3C0B;
        if (v4lelement->vcap.audios)
          flags |= 0x10000;
      }
      g_value_set_flags (value, flags);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_v4lelement_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstV4lElement *v4lelement = GST_V4LELEMENT (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open the device */
      if (!gst_v4l_open (v4lelement))
        return GST_STATE_CHANGE_FAILURE;
#ifdef HAVE_XVIDEO
      gst_v4l_xoverlay_start (v4lelement);
#endif
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close the device */
#ifdef HAVE_XVIDEO
      gst_v4l_xoverlay_stop (v4lelement);
#endif
      if (!gst_v4l_close (v4lelement))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}
