/* G-Streamer generic V4L element - generic V4L calls handling
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
#include "v4l_calls.h"
#include "gstv4ltuner.h"
#include "gstv4lxoverlay.h"
#include "gstv4lcolorbalance.h"

#include <gst/propertyprobe/propertyprobe.h>

/* elementfactory information */
static GstElementDetails gst_v4lelement_details =
GST_ELEMENT_DETAILS ("Generic video4linux Element",
    "Generic/Video",
    "Generic plugin for handling common video4linux calls",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

/* V4lElement signals and args */
enum
{
  /* FILL ME */
  SIGNAL_OPEN,
  SIGNAL_CLOSE,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_DEVICE_NAME,
  ARG_FLAGS,
};


static void gst_v4lelement_base_init (gpointer g_class);
static void gst_v4lelement_class_init (GstV4lElementClass * klass);
static void gst_v4lelement_init (GstV4lElement * v4lelement);
static void gst_v4lelement_dispose (GObject * object);
static void gst_v4lelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_v4lelement_change_state (GstElement * element);


static GstElementClass *parent_class = NULL;
static guint gst_v4lelement_signals[LAST_SIGNAL] = { 0 };

static gboolean
gst_v4l_iface_supported (GstImplementsInterface * iface, GType iface_type)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (iface);

  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_X_OVERLAY || iface_type == GST_TYPE_COLOR_BALANCE);

  if (v4lelement->video_fd == -1)
    return FALSE;

  if (iface_type == GST_TYPE_X_OVERLAY && !GST_V4L_IS_OVERLAY (v4lelement))
    return FALSE;

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

static gboolean
gst_v4l_class_probe_devices (GstV4lElementClass * klass, gboolean check)
{
  static gboolean init = FALSE;
  static GList *devices = NULL;

  if (!init && !check) {
    gchar *dev_base[] = { "/dev/video", "/dev/v4l/video", NULL };
    gint base, n, fd;

    while (devices) {
      GList *item = devices;
      gchar *device = item->data;

      devices = g_list_remove (devices, item);
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
    case ARG_DEVICE:
      gst_v4l_class_probe_devices (klass, FALSE);
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
    case ARG_DEVICE:
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
    case ARG_DEVICE:
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
GType
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


GType
gst_v4lelement_get_type (void)
{
  static GType v4lelement_type = 0;

  if (!v4lelement_type) {
    static const GTypeInfo v4lelement_info = {
      sizeof (GstV4lElementClass),
      gst_v4lelement_base_init,
      NULL,
      (GClassInitFunc) gst_v4lelement_class_init,
      NULL,
      NULL,
      sizeof (GstV4lElement),
      0,
      (GInstanceInitFunc) gst_v4lelement_init,
      NULL
    };
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
    static const GInterfaceInfo v4l_xoverlay_info = {
      (GInterfaceInitFunc) gst_v4l_xoverlay_interface_init,
      NULL,
      NULL,
    };
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

    v4lelement_type = g_type_register_static (GST_TYPE_ELEMENT,
	"GstV4lElement", &v4lelement_info, 0);

    g_type_add_interface_static (v4lelement_type,
	GST_TYPE_IMPLEMENTS_INTERFACE, &v4liface_info);
    g_type_add_interface_static (v4lelement_type,
	GST_TYPE_TUNER, &v4l_tuner_info);
    g_type_add_interface_static (v4lelement_type,
	GST_TYPE_X_OVERLAY, &v4l_xoverlay_info);
    g_type_add_interface_static (v4lelement_type,
	GST_TYPE_COLOR_BALANCE, &v4l_colorbalance_info);
    g_type_add_interface_static (v4lelement_type,
	GST_TYPE_PROPERTY_PROBE, &v4l_propertyprobe_info);
  }

  return v4lelement_type;
}


static void
gst_v4lelement_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstV4lElementClass *klass = GST_V4LELEMENT_CLASS (g_class);

  klass->devices = NULL;

  gst_element_class_set_details (gstelement_class, &gst_v4lelement_details);
}

static void
gst_v4lelement_class_init (GstV4lElementClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device", "Device location",
	  NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE_NAME,
      g_param_spec_string ("device_name", "Device name", "Name of the device",
	  NULL, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Device type flags",
	  GST_TYPE_V4L_DEVICE_FLAGS, 0, G_PARAM_READABLE));

  /* signals */
  gst_v4lelement_signals[SIGNAL_OPEN] =
      g_signal_new ("open", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstV4lElementClass, open),
      NULL, NULL, g_cclosure_marshal_VOID__STRING,
      G_TYPE_NONE, 1, G_TYPE_STRING);
  gst_v4lelement_signals[SIGNAL_CLOSE] =
      g_signal_new ("close", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstV4lElementClass, close),
      NULL, NULL, g_cclosure_marshal_VOID__STRING,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  gobject_class->set_property = gst_v4lelement_set_property;
  gobject_class->get_property = gst_v4lelement_get_property;

  gstelement_class->change_state = gst_v4lelement_change_state;

  gobject_class->dispose = gst_v4lelement_dispose;
}


static void
gst_v4lelement_init (GstV4lElement * v4lelement)
{
  /* some default values */
  v4lelement->video_fd = -1;
  v4lelement->buffer = NULL;
  v4lelement->videodev = g_strdup ("/dev/video");
  v4lelement->display = g_strdup (g_getenv ("DISPLAY"));

  v4lelement->norms = NULL;
  v4lelement->channels = NULL;
  v4lelement->colors = NULL;

  v4lelement->overlay = gst_v4l_xoverlay_new (v4lelement);
}


static void
gst_v4lelement_dispose (GObject * object)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (object);

  gst_v4l_xoverlay_free (v4lelement);

  if (v4lelement->display) {
    g_free (v4lelement->display);
  }

  if (v4lelement->videodev) {
    g_free (v4lelement->videodev);
  }

  if (((GObjectClass *) parent_class)->dispose)
    ((GObjectClass *) parent_class)->dispose (object);
}


static void
gst_v4lelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lElement *v4lelement;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_V4LELEMENT (object));
  v4lelement = GST_V4LELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
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
  GstV4lElement *v4lelement;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_V4LELEMENT (object));
  v4lelement = GST_V4LELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, v4lelement->videodev);
      break;
    case ARG_DEVICE_NAME:{
      gchar *new = NULL;

      if (GST_V4L_IS_OPEN (v4lelement))
	new = v4lelement->vcap.name;
      g_value_set_string (value, new);
      break;
    }
    case ARG_FLAGS:{
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


static GstElementStateReturn
gst_v4lelement_change_state (GstElement * element)
{
  GstV4lElement *v4lelement;

  g_return_val_if_fail (GST_IS_V4LELEMENT (element), GST_STATE_FAILURE);

  v4lelement = GST_V4LELEMENT (element);

  /* if going down into NULL state, close the device if it's open
   * if going to READY, open the device (and set some options)
   */
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_v4l_set_overlay (v4lelement);

      if (!gst_v4l_open (v4lelement))
	return GST_STATE_FAILURE;

      gst_v4l_xoverlay_open (v4lelement);

      g_signal_emit (G_OBJECT (v4lelement),
	  gst_v4lelement_signals[SIGNAL_OPEN], 0, v4lelement->videodev);
      break;

    case GST_STATE_READY_TO_NULL:
      gst_v4l_xoverlay_close (v4lelement);

      if (!gst_v4l_close (v4lelement))
	return GST_STATE_FAILURE;

      g_signal_emit (G_OBJECT (v4lelement),
	  gst_v4lelement_signals[SIGNAL_CLOSE], 0, v4lelement->videodev);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
