/*
 * GStreamer gstv4l2element.c: base class for V4L2 elements Copyright
 * (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net> This library 
 * is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Library General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This library is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Library General Public License for more details.
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA. 
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

#include "v4l2_calls.h"
#include "gstv4l2tuner.h"
#ifdef HAVE_XVIDEO
#include "gstv4l2xoverlay.h"
#endif
#include "gstv4l2colorbalance.h"


enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_FLAGS,
  PROP_STD,
  PROP_INPUT,
  PROP_FREQUENCY
};


static void gst_v4l2element_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstV4l2Element, gst_v4l2element, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_v4l2element_init_interfaces)
     static void gst_v4l2element_dispose (GObject * object);
     static void gst_v4l2element_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_v4l2element_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
     static gboolean gst_v4l2element_start (GstBaseSrc * src);
     static gboolean gst_v4l2element_stop (GstBaseSrc * src);


     static gboolean
         gst_v4l2_iface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (iface);

#ifdef HAVE_XVIDEO
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_X_OVERLAY || iface_type == GST_TYPE_COLOR_BALANCE);
#else
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_COLOR_BALANCE);
#endif

  if (v4l2element->video_fd == -1)
    return FALSE;

#ifdef HAVE_XVIDEO
  if (iface_type == GST_TYPE_X_OVERLAY && !GST_V4L2_IS_OVERLAY (v4l2element))
    return FALSE;
#endif

  return TRUE;
}

static void
gst_v4l2_interface_init (GstImplementsInterfaceClass * klass)
{
  /*
   * default virtual functions 
   */
  klass->supported = gst_v4l2_iface_supported;
}

static const GList *
gst_v4l2_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  return list;
}

static gboolean
gst_v4l2_class_probe_devices (GstV4l2ElementClass * klass, gboolean check)
{
  static gboolean init = FALSE;
  static GList *devices = NULL;

  if (!init && !check) {
    gchar *dev_base[] = { "/dev/video", "/dev/v4l2/video", NULL };
    gint base, n, fd;

    while (devices) {
      GList *item = devices;
      gchar *device = item->data;

      devices = g_list_remove (devices, item);
      g_free (device);
    }

    /*
     * detect /dev entries 
     */
    for (n = 0; n < 64; n++) {
      for (base = 0; dev_base[base] != NULL; base++) {
        struct stat s;
        gchar *device = g_strdup_printf ("%s%d",
            dev_base[base],
            n);

        /*
         * does the /dev/ entry exist at all? 
         */
        if (stat (device, &s) == 0) {
          /*
           * yes: is a device attached? 
           */
          if (S_ISCHR (s.st_mode)) {

            if ((fd = open (device, O_RDWR | O_NONBLOCK)) > 0 || errno == EBUSY) {
              if (fd > 0)
                close (fd);

              devices = g_list_append (devices, device);
              break;
            }
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
gst_v4l2_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4l2ElementClass *klass = GST_V4L2ELEMENT_GET_CLASS (probe);

  switch (prop_id) {
    case PROP_DEVICE:
      gst_v4l2_class_probe_devices (klass, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_v4l2_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4l2ElementClass *klass = GST_V4L2ELEMENT_GET_CLASS (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case PROP_DEVICE:
      ret = !gst_v4l2_class_probe_devices (klass, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static GValueArray *
gst_v4l2_class_list_devices (GstV4l2ElementClass * klass)
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
gst_v4l2_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstV4l2ElementClass *klass = GST_V4L2ELEMENT_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE:
      array = gst_v4l2_class_list_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_v4l2_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_v4l2_probe_get_properties;
  iface->probe_property = gst_v4l2_probe_probe_property;
  iface->needs_probe = gst_v4l2_probe_needs_probe;
  iface->get_values = gst_v4l2_probe_get_values;
}

#define GST_TYPE_V4L2_DEVICE_FLAGS (gst_v4l2_device_get_type ())
GType
gst_v4l2_device_get_type (void)
{
  static GType v4l2_device_type = 0;

  if (v4l2_device_type == 0) {
    static const GFlagsValue values[] = {
      {V4L2_CAP_VIDEO_CAPTURE, "CAPTURE",
          "Device supports video capture"},
      {V4L2_CAP_VIDEO_OUTPUT, "PLAYBACK",
          "Device supports video playback"},
      {V4L2_CAP_VIDEO_OVERLAY, "OVERLAY",
          "Device supports video overlay"},

      {V4L2_CAP_VBI_CAPTURE, "VBI_CAPTURE",
          "Device supports the VBI capture"},
      {V4L2_CAP_VBI_OUTPUT, "VBI_OUTPUT",
          "Device supports the VBI output"},

      {V4L2_CAP_TUNER, "TUNER",
          "Device has a tuner or modulator"},
      {V4L2_CAP_AUDIO, "AUDIO",
          "Device has audio inputs or outputs"},

      {0, NULL, NULL}
    };

    v4l2_device_type =
        g_flags_register_static ("GstV4l2DeviceTypeFlags", values);
  }

  return v4l2_device_type;
}

static void
gst_v4l2element_init_interfaces (GType type)
{
  static const GInterfaceInfo v4l2iface_info = {
    (GInterfaceInitFunc) gst_v4l2_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l2_tuner_info = {
    (GInterfaceInitFunc) gst_v4l2_tuner_interface_init,
    NULL,
    NULL,
  };
#ifdef HAVE_XVIDEO
  static const GInterfaceInfo v4l2_xoverlay_info = {
    (GInterfaceInitFunc) gst_v4l2_xoverlay_interface_init,
    NULL,
    NULL,
  };
#endif
  static const GInterfaceInfo v4l2_colorbalance_info = {
    (GInterfaceInitFunc) gst_v4l2_color_balance_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l2_propertyprobe_info = {
    (GInterfaceInitFunc) gst_v4l2_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &v4l2iface_info);
  g_type_add_interface_static (type, GST_TYPE_TUNER, &v4l2_tuner_info);
#ifdef HAVE_XVIDEO
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &v4l2_xoverlay_info);
#endif
  g_type_add_interface_static (type,
      GST_TYPE_COLOR_BALANCE, &v4l2_colorbalance_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &v4l2_propertyprobe_info);
}


static void
gst_v4l2element_base_init (gpointer g_class)
{
  GstV4l2ElementClass *klass = GST_V4L2ELEMENT_CLASS (g_class);

  klass->devices = NULL;
}

static void
gst_v4l2element_class_init (GstV4l2ElementClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *basesrc_class;

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_v4l2element_set_property;
  gobject_class->get_property = gst_v4l2element_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEVICE,
      g_param_spec_string ("device",
          "Device", "Device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DEVICE_NAME,
      g_param_spec_string ("device_name",
          "Device name", "Name of the device", NULL, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags",
          "Device type flags",
          GST_TYPE_V4L2_DEVICE_FLAGS, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_STD,
      g_param_spec_string ("std", "std",
          "standard (norm) to use", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_INPUT,
      g_param_spec_string ("input",
          "input",
          "input/output (channel) to switch to", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FREQUENCY,
      g_param_spec_ulong ("frequency",
          "frequency",
          "frequency to tune to (in Hz)", 0, G_MAXULONG, 0, G_PARAM_READWRITE));

  basesrc_class->start = gst_v4l2element_start;
  basesrc_class->stop = gst_v4l2element_stop;

  gobject_class->dispose = gst_v4l2element_dispose;
}


static void
gst_v4l2element_init (GstV4l2Element * v4l2element, GstV4l2ElementClass * klass)
{
  /*
   * some default values 
   */
  v4l2element->video_fd = -1;
  v4l2element->buffer = NULL;
  v4l2element->videodev = g_strdup ("/dev/video0");

  v4l2element->stds = NULL;
  v4l2element->inputs = NULL;
  v4l2element->colors = NULL;

  v4l2element->xwindow_id = 0;
}


static void
gst_v4l2element_dispose (GObject * object)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (object);

  if (v4l2element->videodev) {
    g_free (v4l2element->videodev);
    v4l2element->videodev = NULL;
  }

  if (((GObjectClass *) parent_class)->dispose)
    ((GObjectClass *) parent_class)->dispose (object);
}


static void
gst_v4l2element_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      if (v4l2element->videodev)
        g_free (v4l2element->videodev);
      v4l2element->videodev = g_strdup (g_value_get_string (value));
      break;
    case PROP_STD:
      if (GST_V4L2_IS_OPEN (v4l2element)) {
        GstTuner *tuner = GST_TUNER (v4l2element);
        GstTunerNorm *norm = gst_tuner_find_norm_by_name (tuner,
            (gchar *)
            g_value_get_string (value));

        if (norm) {
          gst_tuner_set_norm (tuner, norm);
        }
      } else {
        g_free (v4l2element->std);
        v4l2element->std = g_value_dup_string (value);
        g_object_notify (object, "std");
      }
      break;
    case PROP_INPUT:
      if (GST_V4L2_IS_OPEN (v4l2element)) {
        GstTuner *tuner = GST_TUNER (v4l2element);
        GstTunerChannel *channel = gst_tuner_find_channel_by_name (tuner,
            (gchar *)
            g_value_get_string (value));

        if (channel) {
          gst_tuner_set_channel (tuner, channel);
        }
      } else {
        g_free (v4l2element->input);
        v4l2element->input = g_value_dup_string (value);
        g_object_notify (object, "input");
      }
      break;
    case PROP_FREQUENCY:
      if (GST_V4L2_IS_OPEN (v4l2element)) {
        GstTuner *tuner = GST_TUNER (v4l2element);
        GstTunerChannel *channel = gst_tuner_get_channel (tuner);

        if (channel &&
            GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
          gst_tuner_set_frequency (tuner, channel, g_value_get_ulong (value));
        }
      } else {
        v4l2element->frequency = g_value_get_ulong (value);
        g_object_notify (object, "frequency");
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4l2element_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, v4l2element->videodev);
      break;
    case PROP_DEVICE_NAME:
    {
      gchar *new = NULL;

      if (GST_V4L2_IS_OPEN (v4l2element))
        new = (gchar *) v4l2element->vcap.card;
      g_value_set_string (value, new);
      break;
    }
    case PROP_FLAGS:
    {
      guint flags = 0;

      if (GST_V4L2_IS_OPEN (v4l2element)) {
        flags |= v4l2element->vcap.capabilities &
            (V4L2_CAP_VIDEO_CAPTURE |
            V4L2_CAP_VIDEO_OUTPUT |
            V4L2_CAP_VIDEO_OVERLAY | V4L2_CAP_TUNER | V4L2_CAP_AUDIO);
        if (v4l2element->vcap.capabilities & V4L2_CAP_AUDIO)
          flags |= V4L2_FBUF_CAP_CHROMAKEY;
      }
      g_value_set_flags (value, flags);
      break;
    }
    case PROP_STD:
      g_value_set_string (value, v4l2element->std);
      break;
    case PROP_INPUT:
      g_value_set_string (value, v4l2element->input);
      break;
    case PROP_FREQUENCY:
      g_value_set_ulong (value, v4l2element->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_v4l2element_start (GstBaseSrc * src)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (src);

  if (!gst_v4l2_open (v4l2element))
    return FALSE;

#ifdef HAVE_XVIDEO
  gst_v4l2_xoverlay_start (v4l2element);
#endif

  return TRUE;
}

static gboolean
gst_v4l2element_stop (GstBaseSrc * src)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (src);

#ifdef HAVE_XVIDEO
  gst_v4l2_xoverlay_stop (v4l2element);
#endif

  if (!gst_v4l2_close (v4l2element))
    return FALSE;

  return TRUE;
}
