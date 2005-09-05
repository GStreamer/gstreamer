/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "gstsunelement.h"
#include  "gstsunmixer.h"

#include <gst/propertyprobe/propertyprobe.h>

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_MIXERDEV,
  ARG_DEVICE_NAME,
};

/* elementfactory information */
static GstElementDetails gst_sunaudioelement_details =
GST_ELEMENT_DETAILS ("SunAudioMixer",
    "Generic/Audio",
    "Audio mixer for Sun Audio devices",
    "Balamurali Viswanathan <balamurali.viswanathan@wipro.com>");

static void gst_sunaudioelement_base_init (GstSunAudioElementClass * klass);
static void gst_sunaudioelement_class_init (GstSunAudioElementClass * klass);

static void gst_sunaudioprobe_interface_init (GstPropertyProbeInterface *
    iface);
static void gst_sunaudioelement_init (GstSunAudioElement * sunaudio);
static void gst_sunaudioelement_dispose (GObject * object);

static void gst_sunaudioelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_sunaudioelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_sunaudioelement_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;

static gboolean gst_sunaudioelement_open_audio (GstSunAudioElement * sunaudio);
static gboolean gst_sunaudioelement_close_audio (GstSunAudioElement * sunaudio);
void gst_sunaudioelement_reset (GstSunAudioElement * sunaudio);

GType
gst_sunaudioelement_get_type (void)
{
  static GType sunaudioelement_type = 0;

  if (!sunaudioelement_type) {
    static const GTypeInfo sunaudioelement_info = {
      sizeof (GstSunAudioElementClass),
      (GBaseInitFunc) gst_sunaudioelement_base_init,
      NULL,
      (GClassInitFunc) gst_sunaudioelement_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioElement),
      0,
      (GInstanceInitFunc) gst_sunaudioelement_init
    };
    static const GInterfaceInfo sunaudioiface_info = {
      (GInterfaceInitFunc) gst_sunaudio_interface_init,
      NULL,
      NULL
    };
    static const GInterfaceInfo sunaudiomixer_info = {
      (GInterfaceInitFunc) gst_sunaudiomixer_interface_init,
      NULL,
      NULL
    };
    static const GInterfaceInfo sunaudioprobe_info = {
      (GInterfaceInitFunc) gst_sunaudioprobe_interface_init,
      NULL,
      NULL
    };

    sunaudioelement_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSunAudioElement", &sunaudioelement_info, 0);
    g_type_add_interface_static (sunaudioelement_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &sunaudioiface_info);
    g_type_add_interface_static (sunaudioelement_type,
        GST_TYPE_MIXER, &sunaudiomixer_info);
    g_type_add_interface_static (sunaudioelement_type,
        GST_TYPE_PROPERTY_PROBE, &sunaudioprobe_info);
  }

  return sunaudioelement_type;
}

static void
gst_sunaudioelement_base_init (GstSunAudioElementClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  klass->device_combinations = NULL;

  gst_element_class_set_details (element_class, &gst_sunaudioelement_details);
}

static void
gst_sunaudioelement_class_init (GstSunAudioElementClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "SunAudio device (/dev/audioctl usually)", "default",
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MIXERDEV,
      g_param_spec_string ("mixerdev", "Mixer device",
          "SunAudio mixer device (/dev/audioctl usually)", "default",
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE_NAME,
      g_param_spec_string ("device_name", "Device name", "Name of the device",
          NULL, G_PARAM_READABLE));

  gobject_class->set_property = gst_sunaudioelement_set_property;
  gobject_class->get_property = gst_sunaudioelement_get_property;
  gobject_class->dispose = gst_sunaudioelement_dispose;

  gstelement_class->change_state = gst_sunaudioelement_change_state;
}

static GList *
device_combination_append (GList * device_combinations,
    GstSunAudioDeviceCombination * combi)
{
  GList *it;

  for (it = device_combinations; it != NULL; it = it->next) {
    GstSunAudioDeviceCombination *cur;

    cur = (GstSunAudioDeviceCombination *) it->data;
    if (cur->dev == combi->dev) {
      return device_combinations;
    }
  }

  return g_list_append (device_combinations, combi);
}

static gboolean
gst_sunaudioelement_class_probe_devices (GstSunAudioElementClass * klass,
    gboolean check)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  gint openmode = O_RDONLY;
  GList *padtempllist;
  static GList *device_combinations;
  static gboolean init = FALSE;
  int fd;

  padtempllist = gst_element_class_get_pad_template_list (eklass);
  if (padtempllist != NULL) {
    GstPadTemplate *firstpadtempl = padtempllist->data;

    if (GST_PAD_TEMPLATE_DIRECTION (firstpadtempl) == GST_PAD_SINK) {
      openmode = O_WRONLY;
    }
  }


  if (!init && !check) {
    if ((fd = open ("/dev/audioctl", openmode | O_NONBLOCK)) > 0
        || errno == EBUSY) {
      GstSunAudioDeviceCombination *combi;

      if (fd > 0)
        close (fd);

      combi = g_new0 (GstSunAudioDeviceCombination, 1);
      combi->mixer = g_strdup ("/dev/audioctl");
      device_combinations =
          device_combination_append (device_combinations, combi);
    }
    init = TRUE;
  }

  klass->device_combinations = device_combinations;

  return init;
}

static void
gst_sunaudioprobe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstSunAudioElementClass *klass = GST_SUNAUDIOELEMENT_GET_CLASS (probe);

  switch (prop_id) {
    case ARG_DEVICE:
      gst_sunaudioelement_class_probe_devices (klass, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sunaudioprobe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstSunAudioElementClass *klass = GST_SUNAUDIOELEMENT_GET_CLASS (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case ARG_DEVICE:
      ret = !gst_sunaudioelement_class_probe_devices (klass, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static const GList *
gst_sunaudioprobe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  return list;
}

static GValueArray *
gst_sunaudioelement_class_list_devices (GstSunAudioElementClass * klass)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!klass->device_combinations) {
    return NULL;
  }

  array = g_value_array_new (g_list_length (klass->device_combinations));
  item = klass->device_combinations;
  g_value_init (&value, G_TYPE_STRING);
  while (item) {
    GstSunAudioDeviceCombination *combi = item->data;

    g_value_set_string (&value, combi->mixer);
    g_value_array_append (array, &value);

    item = item->next;
  }
  g_value_unset (&value);

  return array;
}

static GValueArray *
gst_sunaudioprobe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstSunAudioElementClass *klass = GST_SUNAUDIOELEMENT_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case ARG_DEVICE:
      array = gst_sunaudioelement_class_list_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_sunaudioprobe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_sunaudioprobe_get_properties;
  iface->probe_property = gst_sunaudioprobe_probe_property;
  iface->needs_probe = gst_sunaudioprobe_needs_probe;
  iface->get_values = gst_sunaudioprobe_get_values;
}

static void
gst_sunaudioelement_init (GstSunAudioElement * sunaudio)
{
  sunaudio->device = g_strdup ("/dev/audio");
  sunaudio->mixer_dev = g_strdup ("/dev/audioctl");
  sunaudio->fd = -1;
  sunaudio->mixer_fd = -1;
  sunaudio->tracklist = NULL;
  sunaudio->device_name = NULL;

  gst_sunaudioelement_reset (sunaudio);
}

void
gst_sunaudioelement_reset (GstSunAudioElement * sunaudio)
{
  return;
}

static void
gst_sunaudioelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (gst_element_get_state (GST_ELEMENT (sunaudio)) == GST_STATE_NULL) {
        g_free (sunaudio->device);
        sunaudio->device = g_strdup (g_value_get_string (value));

        if (GST_SUNAUDIOELEMENT_GET_CLASS (sunaudio)->device_combinations !=
            NULL) {
          GList *list =
              GST_SUNAUDIOELEMENT_GET_CLASS (sunaudio)->device_combinations;

          while (list) {
            GstSunAudioDeviceCombination *combi = list->data;

            if (!strcmp (combi->mixer, sunaudio->device)) {
              g_free (sunaudio->mixer_dev);
              sunaudio->mixer_dev = g_strdup (combi->mixer);
              break;
            }

            list = list->next;
          }
        }
      }
      break;
    case ARG_MIXERDEV:
      if (gst_element_get_state (GST_ELEMENT (sunaudio)) == GST_STATE_NULL) {
        g_free (sunaudio->mixer_dev);
        sunaudio->mixer_dev = g_strdup (g_value_get_string (value));
      }
      break;
    default:
      break;
  }
}

static void
gst_sunaudioelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, sunaudio->device);
      break;
    case ARG_MIXERDEV:
      g_value_set_string (value, sunaudio->mixer_dev);
      break;
    case ARG_DEVICE_NAME:
      g_value_set_string (value, sunaudio->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sunaudioelement_dispose (GObject * object)
{
  GstSunAudioElement *sunaudio = (GstSunAudioElement *) object;

  g_free (sunaudio->device);
  g_free (sunaudio->mixer_dev);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_sunaudioelement_change_state (GstElement * element,
    GstStateChange transition)
{
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_sunaudioelement_open_audio (sunaudio)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      GST_INFO ("opened sound device");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_sunaudioelement_close_audio (sunaudio);
      gst_sunaudioelement_reset (sunaudio);
      GST_INFO ("closed sound device");
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_sunaudioelement_open_audio (GstSunAudioElement * sunaudio)
{
  gint caps;
  GstSunAudioOpenMode mode = GST_SUNAUDIOELEMENT_READ;
  const GList *padlist;

  g_return_val_if_fail (sunaudio->fd == -1, FALSE);

  padlist = gst_element_get_pad_list (GST_ELEMENT (sunaudio));

  if (padlist != NULL) {
    GstPad *firstpad = padlist->data;

    if (GST_PAD_IS_SINK (firstpad)) {
      mode = GST_SUNAUDIOELEMENT_WRITE;
    }
  }

  if (mode == GST_SUNAUDIOELEMENT_WRITE) {
    sunaudio->fd = open (sunaudio->device, O_WRONLY | O_NONBLOCK);

    if (sunaudio->fd >= 0) {
      close (sunaudio->fd);

      sunaudio->fd = open (sunaudio->device, O_WRONLY);
    }
  } else {
    sunaudio->fd = open (sunaudio->device, O_RDONLY);
  }

  if (sunaudio->fd < 0) {
    switch (errno) {
      default:
        printf ("could not open device\n");
    }
    return FALSE;
  }

  sunaudio->mode = mode;
  gst_sunaudiomixer_build_list (sunaudio);
  return TRUE;

}

static gboolean
gst_sunaudioelement_close_audio (GstSunAudioElement * sunaudio)
{
  close (sunaudio->fd);
  sunaudio->fd = -1;
}
