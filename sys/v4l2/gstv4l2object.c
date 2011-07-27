/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * gstv4l2object.c: base class for V4L2 elements
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. This library is distributed in the hope
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

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

#include "v4l2_calls.h"
#include "gstv4l2tuner.h"
#ifdef HAVE_XVIDEO
#include "gstv4l2xoverlay.h"
#endif
#include "gstv4l2colorbalance.h"

#include "gst/gst-i18n-plugin.h"

#include <gst/video/video.h>

/* videodev2.h is not versioned and we can't easily check for the presence
 * of enum values at compile time, but the V4L2_CAP_VIDEO_OUTPUT_OVERLAY define
 * was added in the same commit as V4L2_FIELD_INTERLACED_{TB,BT} (b2787845) */
#ifndef V4L2_CAP_VIDEO_OUTPUT_OVERLAY
#define V4L2_FIELD_INTERLACED_TB 8
#define V4L2_FIELD_INTERLACED_BT 9
#endif

GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);
#define GST_CAT_DEFAULT v4l2_debug

#define DEFAULT_PROP_DEVICE_NAME 	NULL
#define DEFAULT_PROP_DEVICE_FD          -1
#define DEFAULT_PROP_FLAGS              0
#define DEFAULT_PROP_TV_NORM            0
#define DEFAULT_PROP_CHANNEL            NULL
#define DEFAULT_PROP_FREQUENCY          0
#define DEFAULT_PROP_IO_MODE            GST_V4L2_IO_AUTO

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
};

G_LOCK_DEFINE_STATIC (probe_lock);

const GList *
gst_v4l2_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  G_LOCK (probe_lock);

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  G_UNLOCK (probe_lock);

  return list;
}

static gboolean init = FALSE;
static GList *devices = NULL;

#ifdef HAVE_GUDEV
static gboolean
gst_v4l2_class_probe_devices_with_udev (GstElementClass * klass, gboolean check,
    GList ** klass_devices)
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
      /* Append v4l2 devices only. If api is 0 probably v4l_id has
         been stripped out of the current udev installation, append
         anyway */
      if (api == 0) {
        GST_WARNING
            ("Couldn't retrieve ID_V4L_VERSION, silly udev installation?");
      }
      if ((api == 2 || api == 0)) {
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

  *klass_devices = devices;

  return init;
}
#endif /* HAVE_GUDEV */

static gboolean
gst_v4l2_class_probe_devices (GstElementClass * klass, gboolean check,
    GList ** klass_devices)
{
  if (!check) {
    const gchar *dev_base[] = { "/dev/video", "/dev/v4l2/video", NULL };
    gint base, n, fd;

    while (devices) {
      gchar *device = devices->data;
      devices = g_list_remove (devices, device);
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

  *klass_devices = devices;

  return init;
}

void
gst_v4l2_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec, GList ** klass_devices)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (probe);

  switch (prop_id) {
    case PROP_DEVICE:
#ifdef HAVE_GUDEV
      if (!gst_v4l2_class_probe_devices_with_udev (klass, FALSE, klass_devices))
        gst_v4l2_class_probe_devices (klass, FALSE, klass_devices);
#else /* !HAVE_GUDEV */
      gst_v4l2_class_probe_devices (klass, FALSE, klass_devices);
#endif /* HAVE_GUDEV */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

gboolean
gst_v4l2_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec, GList ** klass_devices)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case PROP_DEVICE:
#ifdef HAVE_GUDEV
      ret =
          !gst_v4l2_class_probe_devices_with_udev (klass, FALSE, klass_devices);
#else /* !HAVE_GUDEV */
      ret = !gst_v4l2_class_probe_devices (klass, TRUE, klass_devices);
#endif /* HAVE_GUDEV */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
  return ret;
}

static GValueArray *
gst_v4l2_class_list_devices (GstElementClass * klass, GList ** klass_devices)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!*klass_devices)
    return NULL;

  array = g_value_array_new (g_list_length (*klass_devices));
  item = *klass_devices;
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

GValueArray *
gst_v4l2_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec, GList ** klass_devices)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE:
      array = gst_v4l2_class_list_devices (klass, klass_devices);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

#define GST_TYPE_V4L2_DEVICE_FLAGS (gst_v4l2_device_get_type ())
static GType
gst_v4l2_device_get_type (void)
{
  static GType v4l2_device_type = 0;

  if (v4l2_device_type == 0) {
    static const GFlagsValue values[] = {
      {V4L2_CAP_VIDEO_CAPTURE, "Device supports video capture", "capture"},
      {V4L2_CAP_VIDEO_OUTPUT, "Device supports video playback", "output"},
      {V4L2_CAP_VIDEO_OVERLAY, "Device supports video overlay", "overlay"},

      {V4L2_CAP_VBI_CAPTURE, "Device supports the VBI capture", "vbi-capture"},
      {V4L2_CAP_VBI_OUTPUT, "Device supports the VBI output", "vbi-output"},

      {V4L2_CAP_TUNER, "Device has a tuner or modulator", "tuner"},
      {V4L2_CAP_AUDIO, "Device has audio inputs or outputs", "audio"},

      {0, NULL, NULL}
    };

    v4l2_device_type =
        g_flags_register_static ("GstV4l2DeviceTypeFlags", values);
  }

  return v4l2_device_type;
}

#define GST_TYPE_V4L2_TV_NORM (gst_v4l2_tv_norm_get_type ())
static GType
gst_v4l2_tv_norm_get_type (void)
{
  static GType v4l2_tv_norm = 0;

  if (!v4l2_tv_norm) {
    static const GEnumValue tv_norms[] = {
      {0, "none", "none"},

      {V4L2_STD_NTSC, "NTSC", "NTSC"},
      {V4L2_STD_NTSC_M, "NTSC-M", "NTSC-M"},
      {V4L2_STD_NTSC_M_JP, "NTSC-M-JP", "NTSC-M-JP"},
      {V4L2_STD_NTSC_M_KR, "NTSC-M-KR", "NTSC-M-KR"},
      {V4L2_STD_NTSC_443, "NTSC-443", "NTSC-443"},

      {V4L2_STD_PAL, "PAL", "PAL"},
      {V4L2_STD_PAL_BG, "PAL-BG", "PAL-BG"},
      {V4L2_STD_PAL_B, "PAL-B", "PAL-B"},
      {V4L2_STD_PAL_B1, "PAL-B1", "PAL-B1"},
      {V4L2_STD_PAL_G, "PAL-G", "PAL-G"},
      {V4L2_STD_PAL_H, "PAL-H", "PAL-H"},
      {V4L2_STD_PAL_I, "PAL-I", "PAL-I"},
      {V4L2_STD_PAL_DK, "PAL-DK", "PAL-DK"},
      {V4L2_STD_PAL_D, "PAL-D", "PAL-D"},
      {V4L2_STD_PAL_D1, "PAL-D1", "PAL-D1"},
      {V4L2_STD_PAL_K, "PAL-K", "PAL-K"},
      {V4L2_STD_PAL_M, "PAL-M", "PAL-M"},
      {V4L2_STD_PAL_N, "PAL-N", "PAL-N"},
      {V4L2_STD_PAL_Nc, "PAL-Nc", "PAL-Nc"},
      {V4L2_STD_PAL_60, "PAL-60", "PAL-60"},

      {V4L2_STD_SECAM, "SECAM", "SECAM"},
      {V4L2_STD_SECAM_B, "SECAM-B", "SECAM-B"},
      {V4L2_STD_SECAM_G, "SECAM-G", "SECAM-G"},
      {V4L2_STD_SECAM_H, "SECAM-H", "SECAM-H"},
      {V4L2_STD_SECAM_DK, "SECAM-DK", "SECAM-DK"},
      {V4L2_STD_SECAM_D, "SECAM-D", "SECAM-D"},
      {V4L2_STD_SECAM_K, "SECAM-K", "SECAM-K"},
      {V4L2_STD_SECAM_K1, "SECAM-K1", "SECAM-K1"},
      {V4L2_STD_SECAM_L, "SECAM-L", "SECAM-L"},
      {V4L2_STD_SECAM_LC, "SECAM-Lc", "SECAM-Lc"},

      {0, NULL, NULL}
    };

    v4l2_tv_norm = g_enum_register_static ("V4L2_TV_norms", tv_norms);
  }

  return v4l2_tv_norm;
}

#define GST_TYPE_V4L2_IO_MODE (gst_v4l2_io_mode_get_type ())
static GType
gst_v4l2_io_mode_get_type (void)
{
  static GType v4l2_io_mode = 0;

  if (!v4l2_io_mode) {
    static const GEnumValue io_modes[] = {
      {GST_V4L2_IO_AUTO, "GST_V4L2_IO_AUTO", "auto"},
      {GST_V4L2_IO_RW, "GST_V4L2_IO_RW", "rw"},
      {GST_V4L2_IO_MMAP, "GST_V4L2_IO_MMAP", "mmap"},
      {GST_V4L2_IO_USERPTR, "GST_V4L2_IO_USERPTR", "userptr"},

      {0, NULL, NULL}
    };
    v4l2_io_mode = g_enum_register_static ("GstV4l2IOMode", io_modes);
  }
  return v4l2_io_mode;
}

void
gst_v4l2_object_install_properties_helper (GObjectClass * gobject_class,
    const char *default_device)
{
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Device location",
          default_device, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Name of the device", DEFAULT_PROP_DEVICE_NAME,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_FD,
      g_param_spec_int ("device-fd", "File descriptor",
          "File descriptor of the device", -1, G_MAXINT, DEFAULT_PROP_DEVICE_FD,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Device type flags",
          GST_TYPE_V4L2_DEVICE_FLAGS, DEFAULT_PROP_FLAGS,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstV4l2Src:brightness
   *
   * Picture brightness, or more precisely, the black level
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
      g_param_spec_int ("brightness", "Brightness",
          "Picture brightness, or more precisely, the black level", G_MININT,
          G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  /**
   * GstV4l2Src:contrast
   *
   * Picture contrast or luma gain
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_CONTRAST,
      g_param_spec_int ("contrast", "Contrast",
          "Picture contrast or luma gain", G_MININT,
          G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  /**
   * GstV4l2Src:saturation
   *
   * Picture color saturation or chroma gain
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_int ("saturation", "Saturation",
          "Picture color saturation or chroma gain", G_MININT,
          G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  /**
   * GstV4l2Src:hue
   *
   * Hue or color balance
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_HUE,
      g_param_spec_int ("hue", "Hue",
          "Hue or color balance", G_MININT,
          G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  /**
   * GstV4l2Src:norm
   *
   * TV norm
   *
   * Since: 0.10.30
   */
  g_object_class_install_property (gobject_class, PROP_TV_NORM,
      g_param_spec_enum ("norm", "TV norm",
          "video standard",
          GST_TYPE_V4L2_TV_NORM, DEFAULT_PROP_TV_NORM,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstV4l2Src:io-mode
   *
   * IO Mode
   */
  g_object_class_install_property (gobject_class, PROP_IO_MODE,
      g_param_spec_enum ("io-mode", "IO mode",
          "I/O mode",
          GST_TYPE_V4L2_IO_MODE, DEFAULT_PROP_IO_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

GstV4l2Object *
gst_v4l2_object_new (GstElement * element,
    enum v4l2_buf_type type,
    const char *default_device,
    GstV4l2GetInOutFunction get_in_out_func,
    GstV4l2SetInOutFunction set_in_out_func,
    GstV4l2UpdateFpsFunction update_fps_func)
{
  GstV4l2Object *v4l2object;

  /*
   * some default values
   */
  v4l2object = g_new0 (GstV4l2Object, 1);

  v4l2object->type = type;
  v4l2object->formats = NULL;

  v4l2object->element = element;
  v4l2object->get_in_out_func = get_in_out_func;
  v4l2object->set_in_out_func = set_in_out_func;
  v4l2object->update_fps_func = update_fps_func;

  v4l2object->video_fd = -1;
  v4l2object->poll = gst_poll_new (TRUE);
  v4l2object->active = FALSE;
  v4l2object->videodev = g_strdup (default_device);

  v4l2object->norms = NULL;
  v4l2object->channels = NULL;
  v4l2object->colors = NULL;

  v4l2object->xwindow_id = 0;

  return v4l2object;
}

static gboolean gst_v4l2_object_clear_format_list (GstV4l2Object * v4l2object);


void
gst_v4l2_object_destroy (GstV4l2Object * v4l2object)
{
  g_return_if_fail (v4l2object != NULL);

  if (v4l2object->videodev)
    g_free (v4l2object->videodev);

  if (v4l2object->poll)
    gst_poll_free (v4l2object->poll);

  if (v4l2object->channel)
    g_free (v4l2object->channel);

  if (v4l2object->formats) {
    gst_v4l2_object_clear_format_list (v4l2object);
  }

  g_free (v4l2object);
}


static gboolean
gst_v4l2_object_clear_format_list (GstV4l2Object * v4l2object)
{
  g_slist_foreach (v4l2object->formats, (GFunc) g_free, NULL);
  g_slist_free (v4l2object->formats);
  v4l2object->formats = NULL;

  return TRUE;
}

static gint
gst_v4l2_object_prop_to_cid (guint prop_id)
{
  gint cid = -1;

  switch (prop_id) {
    case PROP_BRIGHTNESS:
      cid = V4L2_CID_BRIGHTNESS;
      break;
    case PROP_CONTRAST:
      cid = V4L2_CID_CONTRAST;
      break;
    case PROP_SATURATION:
      cid = V4L2_CID_SATURATION;
      break;
    case PROP_HUE:
      cid = V4L2_CID_HUE;
      break;
    default:
      GST_WARNING ("unmapped property id: %d", prop_id);
  }
  return cid;
}


gboolean
gst_v4l2_object_set_property_helper (GstV4l2Object * v4l2object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_DEVICE:
      g_free (v4l2object->videodev);
      v4l2object->videodev = g_value_dup_string (value);
      break;
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST:
    case PROP_SATURATION:
    case PROP_HUE:
    {
      gint cid = gst_v4l2_object_prop_to_cid (prop_id);

      if (cid != -1) {
        if (GST_V4L2_IS_OPEN (v4l2object)) {
          gst_v4l2_set_attribute (v4l2object, cid, g_value_get_int (value));
        }
      }
      return TRUE;
    }
      break;
    case PROP_TV_NORM:
      v4l2object->tv_norm = g_value_get_enum (value);
      break;
#if 0
    case PROP_CHANNEL:
      if (GST_V4L2_IS_OPEN (v4l2object)) {
        GstTuner *tuner = GST_TUNER (v4l2object->element);
        GstTunerChannel *channel = gst_tuner_find_channel_by_name (tuner,
            (gchar *) g_value_get_string (value));

        if (channel) {
          /* like gst_tuner_set_channel (tuner, channel)
             without g_object_notify */
          gst_v4l2_tuner_set_channel (v4l2object, channel);
        }
      } else {
        g_free (v4l2object->channel);
        v4l2object->channel = g_value_dup_string (value);
      }
      break;
    case PROP_FREQUENCY:
      if (GST_V4L2_IS_OPEN (v4l2object)) {
        GstTuner *tuner = GST_TUNER (v4l2object->element);
        GstTunerChannel *channel = gst_tuner_get_channel (tuner);

        if (channel &&
            GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
          /* like
             gst_tuner_set_frequency (tuner, channel, g_value_get_ulong (value))
             without g_object_notify */
          gst_v4l2_tuner_set_frequency (v4l2object, channel,
              g_value_get_ulong (value));
        }
      } else {
        v4l2object->frequency = g_value_get_ulong (value);
      }
      break;
#endif
    case PROP_IO_MODE:
      v4l2object->req_mode = g_value_get_enum (value);
      break;
    default:
      return FALSE;
      break;
  }
  return TRUE;
}


gboolean
gst_v4l2_object_get_property_helper (GstV4l2Object * v4l2object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, v4l2object->videodev);
      break;
    case PROP_DEVICE_NAME:
    {
      const guchar *new = NULL;

      if (GST_V4L2_IS_OPEN (v4l2object)) {
        new = v4l2object->vcap.card;
      } else if (gst_v4l2_open (v4l2object)) {
        new = v4l2object->vcap.card;
        gst_v4l2_close (v4l2object);
      }
      g_value_set_string (value, (gchar *) new);
      break;
    }
    case PROP_DEVICE_FD:
    {
      if (GST_V4L2_IS_OPEN (v4l2object))
        g_value_set_int (value, v4l2object->video_fd);
      else
        g_value_set_int (value, DEFAULT_PROP_DEVICE_FD);
      break;
    }
    case PROP_FLAGS:
    {
      guint flags = 0;

      if (GST_V4L2_IS_OPEN (v4l2object)) {
        flags |= v4l2object->vcap.capabilities &
            (V4L2_CAP_VIDEO_CAPTURE |
            V4L2_CAP_VIDEO_OUTPUT |
            V4L2_CAP_VIDEO_OVERLAY |
            V4L2_CAP_VBI_CAPTURE |
            V4L2_CAP_VBI_OUTPUT | V4L2_CAP_TUNER | V4L2_CAP_AUDIO);
      }
      g_value_set_flags (value, flags);
      break;
    }
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST:
    case PROP_SATURATION:
    case PROP_HUE:
    {
      gint cid = gst_v4l2_object_prop_to_cid (prop_id);

      if (cid != -1) {
        if (GST_V4L2_IS_OPEN (v4l2object)) {
          gint v;
          if (gst_v4l2_get_attribute (v4l2object, cid, &v)) {
            g_value_set_int (value, v);
          }
        }
      }
      return TRUE;
    }
      break;
    case PROP_TV_NORM:
      g_value_set_enum (value, v4l2object->tv_norm);
      break;
    case PROP_IO_MODE:
      g_value_set_enum (value, v4l2object->req_mode);
      break;
    default:
      return FALSE;
      break;
  }
  return TRUE;
}

static void
gst_v4l2_set_defaults (GstV4l2Object * v4l2object)
{
  GstTunerNorm *norm = NULL;
  GstTunerChannel *channel = NULL;
  GstTuner *tuner;

  if (!GST_IS_TUNER (v4l2object->element))
    return;

  tuner = GST_TUNER (v4l2object->element);

  if (v4l2object->tv_norm)
    norm = gst_v4l2_tuner_get_norm_by_std_id (v4l2object, v4l2object->tv_norm);
  GST_DEBUG_OBJECT (v4l2object->element, "tv_norm=%d, norm=%p",
      v4l2object->tv_norm, norm);
  if (norm) {
    gst_tuner_set_norm (tuner, norm);
  } else {
    norm =
        GST_TUNER_NORM (gst_tuner_get_norm (GST_TUNER (v4l2object->element)));
    if (norm) {
      v4l2object->tv_norm =
          gst_v4l2_tuner_get_std_id_by_norm (v4l2object, norm);
      gst_tuner_norm_changed (tuner, norm);
    }
  }

  if (v4l2object->channel)
    channel = gst_tuner_find_channel_by_name (tuner, v4l2object->channel);
  if (channel) {
    gst_tuner_set_channel (tuner, channel);
  } else {
    channel =
        GST_TUNER_CHANNEL (gst_tuner_get_channel (GST_TUNER
            (v4l2object->element)));
    if (channel) {
      g_free (v4l2object->channel);
      v4l2object->channel = g_strdup (channel->label);
      gst_tuner_channel_changed (tuner, channel);
    }
  }

  if (channel
      && GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
    if (v4l2object->frequency != 0) {
      gst_tuner_set_frequency (tuner, channel, v4l2object->frequency);
    } else {
      v4l2object->frequency = gst_tuner_get_frequency (tuner, channel);
      if (v4l2object->frequency == 0) {
        /* guess */
        gst_tuner_set_frequency (tuner, channel, 1000);
      } else {
      }
    }
  }
}

gboolean
gst_v4l2_object_open (GstV4l2Object * v4l2object)
{
  if (gst_v4l2_open (v4l2object))
    gst_v4l2_set_defaults (v4l2object);
  else
    return FALSE;

#ifdef HAVE_XVIDEO
  gst_v4l2_xoverlay_start (v4l2object);
#endif

  return TRUE;
}

gboolean
gst_v4l2_object_close (GstV4l2Object * v4l2object)
{
#ifdef HAVE_XVIDEO
  gst_v4l2_xoverlay_stop (v4l2object);
#endif

  if (!gst_v4l2_close (v4l2object))
    return FALSE;

  if (v4l2object->formats) {
    gst_v4l2_object_clear_format_list (v4l2object);
  }

  return TRUE;
}


/*
 * common format / caps utilities:
 */
typedef struct
{
  guint32 format;
  gboolean dimensions;
} GstV4L2FormatDesc;

static const GstV4L2FormatDesc gst_v4l2_formats[] = {
  /* from Linux 2.6.15 videodev2.h */
  {V4L2_PIX_FMT_RGB332, TRUE},
  {V4L2_PIX_FMT_RGB555, TRUE},
  {V4L2_PIX_FMT_RGB565, TRUE},
  {V4L2_PIX_FMT_RGB555X, TRUE},
  {V4L2_PIX_FMT_RGB565X, TRUE},
  {V4L2_PIX_FMT_BGR24, TRUE},
  {V4L2_PIX_FMT_RGB24, TRUE},
  {V4L2_PIX_FMT_BGR32, TRUE},
  {V4L2_PIX_FMT_RGB32, TRUE},
  {V4L2_PIX_FMT_GREY, TRUE},
  {V4L2_PIX_FMT_YVU410, TRUE},
  {V4L2_PIX_FMT_YVU420, TRUE},
  {V4L2_PIX_FMT_YUYV, TRUE},
  {V4L2_PIX_FMT_UYVY, TRUE},
  {V4L2_PIX_FMT_YUV422P, TRUE},
  {V4L2_PIX_FMT_YUV411P, TRUE},
  {V4L2_PIX_FMT_Y41P, TRUE},

  /* two planes -- one Y, one Cr + Cb interleaved  */
  {V4L2_PIX_FMT_NV12, TRUE},
  {V4L2_PIX_FMT_NV21, TRUE},

  /*  The following formats are not defined in the V4L2 specification */
  {V4L2_PIX_FMT_YUV410, TRUE},
  {V4L2_PIX_FMT_YUV420, TRUE},
  {V4L2_PIX_FMT_YYUV, TRUE},
  {V4L2_PIX_FMT_HI240, TRUE},

  /* see http://www.siliconimaging.com/RGB%20Bayer.htm */
#ifdef V4L2_PIX_FMT_SBGGR8
  {V4L2_PIX_FMT_SBGGR8, TRUE},
#endif

  /* compressed formats */
  {V4L2_PIX_FMT_MJPEG, TRUE},
  {V4L2_PIX_FMT_JPEG, TRUE},
#ifdef V4L2_PIX_FMT_PJPG
  {V4L2_PIX_FMT_PJPG, TRUE},
#endif
  {V4L2_PIX_FMT_DV, TRUE},
  {V4L2_PIX_FMT_MPEG, FALSE},

  /*  Vendor-specific formats   */
  {V4L2_PIX_FMT_WNVA, TRUE},

#ifdef V4L2_PIX_FMT_SN9C10X
  {V4L2_PIX_FMT_SN9C10X, TRUE},
#endif
#ifdef V4L2_PIX_FMT_PWC1
  {V4L2_PIX_FMT_PWC1, TRUE},
#endif
#ifdef V4L2_PIX_FMT_PWC2
  {V4L2_PIX_FMT_PWC2, TRUE},
#endif
#ifdef V4L2_PIX_FMT_YVYU
  {V4L2_PIX_FMT_YVYU, TRUE},
#endif
};

#define GST_V4L2_FORMAT_COUNT (G_N_ELEMENTS (gst_v4l2_formats))


static struct v4l2_fmtdesc *
gst_v4l2_object_get_format_from_fourcc (GstV4l2Object * v4l2object,
    guint32 fourcc)
{
  struct v4l2_fmtdesc *fmt;
  GSList *walk;

  if (fourcc == 0)
    return NULL;

  walk = gst_v4l2_object_get_format_list (v4l2object);
  while (walk) {
    fmt = (struct v4l2_fmtdesc *) walk->data;
    if (fmt->pixelformat == fourcc)
      return fmt;
    /* special case for jpeg */
    if (fmt->pixelformat == V4L2_PIX_FMT_MJPEG ||
        fmt->pixelformat == V4L2_PIX_FMT_JPEG
#ifdef V4L2_PIX_FMT_PJPG
        || fmt->pixelformat == V4L2_PIX_FMT_PJPG
#endif
        ) {
      if (fourcc == V4L2_PIX_FMT_JPEG || fourcc == V4L2_PIX_FMT_MJPEG
#ifdef V4L2_PIX_FMT_PJPG
          || fourcc == V4L2_PIX_FMT_PJPG
#endif
          ) {
        return fmt;
      }
    }
    walk = g_slist_next (walk);
  }

  return NULL;
}



/* complete made up ranking, the values themselves are meaningless */
#define YUV_BASE_RANK     1000
#define JPEG_BASE_RANK     500
#define DV_BASE_RANK       200
#define RGB_BASE_RANK      100
#define YUV_ODD_BASE_RANK   50
#define RGB_ODD_BASE_RANK   25
#define BAYER_BASE_RANK     15
#define S910_BASE_RANK      10
#define GREY_BASE_RANK       5
#define PWC_BASE_RANK        1

/* This flag is already used by libv4l2 although
 * it was added to the Linux kernel in 2.6.32
 */
#ifndef V4L2_FMT_FLAG_EMULATED
#define V4L2_FMT_FLAG_EMULATED 0x0002
#endif

static gint
gst_v4l2_object_format_get_rank (const struct v4l2_fmtdesc *fmt)
{
  guint32 fourcc = fmt->pixelformat;
  gboolean emulated = ((fmt->flags & V4L2_FMT_FLAG_EMULATED) != 0);
  gint rank = 0;

  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:
#ifdef V4L2_PIX_FMT_PJPG
    case V4L2_PIX_FMT_PJPG:
      rank = JPEG_BASE_RANK;
      break;
#endif
    case V4L2_PIX_FMT_JPEG:
      rank = JPEG_BASE_RANK + 1;
      break;
    case V4L2_PIX_FMT_MPEG:    /* MPEG          */
      rank = JPEG_BASE_RANK + 2;
      break;

    case V4L2_PIX_FMT_RGB332:
    case V4L2_PIX_FMT_RGB555:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB565X:
      rank = RGB_ODD_BASE_RANK;
      break;

    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_BGR24:
      rank = RGB_BASE_RANK - 1;
      break;

    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:
      rank = RGB_BASE_RANK;
      break;

    case V4L2_PIX_FMT_GREY:    /*  8  Greyscale     */
      rank = GREY_BASE_RANK;
      break;

    case V4L2_PIX_FMT_NV12:    /* 12  Y/CbCr 4:2:0  */
    case V4L2_PIX_FMT_NV21:    /* 12  Y/CrCb 4:2:0  */
    case V4L2_PIX_FMT_YYUV:    /* 16  YUV 4:2:2     */
    case V4L2_PIX_FMT_HI240:   /*  8  8-bit color   */
      rank = YUV_ODD_BASE_RANK;
      break;

    case V4L2_PIX_FMT_YVU410:  /* YVU9,  9 bits per pixel */
      rank = YUV_BASE_RANK + 3;
      break;
    case V4L2_PIX_FMT_YUV410:  /* YUV9,  9 bits per pixel */
      rank = YUV_BASE_RANK + 2;
      break;
    case V4L2_PIX_FMT_YUV420:  /* I420, 12 bits per pixel */
      rank = YUV_BASE_RANK + 7;
      break;
    case V4L2_PIX_FMT_YUYV:    /* YUY2, 16 bits per pixel */
      rank = YUV_BASE_RANK + 10;
      break;
    case V4L2_PIX_FMT_YVU420:  /* YV12, 12 bits per pixel */
      rank = YUV_BASE_RANK + 6;
      break;
    case V4L2_PIX_FMT_UYVY:    /* UYVY, 16 bits per pixel */
      rank = YUV_BASE_RANK + 9;
      break;
    case V4L2_PIX_FMT_Y41P:    /* Y41P, 12 bits per pixel */
      rank = YUV_BASE_RANK + 5;
      break;
    case V4L2_PIX_FMT_YUV411P: /* Y41B, 12 bits per pixel */
      rank = YUV_BASE_RANK + 4;
      break;
    case V4L2_PIX_FMT_YUV422P: /* Y42B, 16 bits per pixel */
      rank = YUV_BASE_RANK + 8;
      break;

    case V4L2_PIX_FMT_DV:
      rank = DV_BASE_RANK;
      break;

    case V4L2_PIX_FMT_WNVA:    /* Winnov hw compres */
      rank = 0;
      break;

#ifdef V4L2_PIX_FMT_SBGGR8
    case V4L2_PIX_FMT_SBGGR8:
      rank = BAYER_BASE_RANK;
      break;
#endif

#ifdef V4L2_PIX_FMT_SN9C10X
    case V4L2_PIX_FMT_SN9C10X:
      rank = S910_BASE_RANK;
      break;
#endif

#ifdef V4L2_PIX_FMT_PWC1
    case V4L2_PIX_FMT_PWC1:
      rank = PWC_BASE_RANK;
      break;
#endif
#ifdef V4L2_PIX_FMT_PWC2
    case V4L2_PIX_FMT_PWC2:
      rank = PWC_BASE_RANK;
      break;
#endif

    default:
      rank = 0;
      break;
  }

  /* All ranks are below 1<<15 so a shift by 15
   * will a) make all non-emulated formats larger
   * than emulated and b) will not overflow
   */
  if (!emulated)
    rank <<= 15;

  return rank;
}



static gint
format_cmp_func (gconstpointer a, gconstpointer b)
{
  const struct v4l2_fmtdesc *fa = a;
  const struct v4l2_fmtdesc *fb = b;

  if (fa->pixelformat == fb->pixelformat)
    return 0;

  return gst_v4l2_object_format_get_rank (fb) -
      gst_v4l2_object_format_get_rank (fa);
}

/******************************************************
 * gst_v4l2_object_fill_format_list():
 *   create list of supported capture formats
 * return value: TRUE on success, FALSE on error
 ******************************************************/
static gboolean
gst_v4l2_object_fill_format_list (GstV4l2Object * v4l2object)
{
  gint n;
  struct v4l2_fmtdesc *format;

  GST_DEBUG_OBJECT (v4l2object->element, "getting src format enumerations");

  /* format enumeration */
  for (n = 0;; n++) {
    format = g_new0 (struct v4l2_fmtdesc, 1);

    format->index = n;
    format->type = v4l2object->type;

    if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_ENUM_FMT, format) < 0) {
      if (errno == EINVAL) {
        g_free (format);
        break;                  /* end of enumeration */
      } else {
        goto failed;
      }
    }

    GST_LOG_OBJECT (v4l2object->element, "index:       %u", format->index);
    GST_LOG_OBJECT (v4l2object->element, "type:        %d", format->type);
    GST_LOG_OBJECT (v4l2object->element, "flags:       %08x", format->flags);
    GST_LOG_OBJECT (v4l2object->element, "description: '%s'",
        format->description);
    GST_LOG_OBJECT (v4l2object->element, "pixelformat: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (format->pixelformat));

    /* sort formats according to our preference;  we do this, because caps
     * are probed in the order the formats are in the list, and the order of
     * formats in the final probed caps matters for things like fixation */
    v4l2object->formats = g_slist_insert_sorted (v4l2object->formats, format,
        (GCompareFunc) format_cmp_func);
  }

#ifndef GST_DISABLE_GST_DEBUG
  {
    GSList *l;

    GST_INFO_OBJECT (v4l2object->element, "got %d format(s):", n);
    for (l = v4l2object->formats; l != NULL; l = l->next) {
      format = l->data;

      GST_INFO_OBJECT (v4l2object->element,
          "  %" GST_FOURCC_FORMAT "%s", GST_FOURCC_ARGS (format->pixelformat),
          ((format->flags & V4L2_FMT_FLAG_EMULATED)) ? " (emulated)" : "");
    }
  }
#endif

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Failed to enumerate possible video formats device '%s' can work with"), v4l2object->videodev), ("Failed to get number %d in pixelformat enumeration for %s. (%d - %s)", n, v4l2object->videodev, errno, g_strerror (errno)));
    g_free (format);
    return FALSE;
  }
}

/*
 * Get the list of supported capture formats, a list of
 * <code>struct v4l2_fmtdesc</code>.
 */
GSList *
gst_v4l2_object_get_format_list (GstV4l2Object * v4l2object)
{
  if (!v4l2object->formats)
    gst_v4l2_object_fill_format_list (v4l2object);
  return v4l2object->formats;
}


GstStructure *
gst_v4l2_object_v4l2fourcc_to_structure (guint32 fourcc)
{
  GstStructure *structure = NULL;

  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:   /* Motion-JPEG */
#ifdef V4L2_PIX_FMT_PJPG
    case V4L2_PIX_FMT_PJPG:    /* Progressive-JPEG */
#endif
    case V4L2_PIX_FMT_JPEG:    /* JFIF JPEG */
      structure = gst_structure_new ("image/jpeg", NULL);
      break;
    case V4L2_PIX_FMT_YYUV:    /* 16  YUV 4:2:2     */
    case V4L2_PIX_FMT_HI240:   /*  8  8-bit color   */
      /* FIXME: get correct fourccs here */
      break;
    case V4L2_PIX_FMT_RGB332:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB565X:
      /* FIXME: get correct fourccs here */
      break;
    case V4L2_PIX_FMT_GREY:    /*  8  Greyscale     */
    case V4L2_PIX_FMT_RGB555:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_BGR24:
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:
    case V4L2_PIX_FMT_NV12:    /* 12  Y/CbCr 4:2:0  */
    case V4L2_PIX_FMT_NV21:    /* 12  Y/CrCb 4:2:0  */
    case V4L2_PIX_FMT_YVU410:
    case V4L2_PIX_FMT_YUV410:
    case V4L2_PIX_FMT_YUV420:  /* I420/IYUV */
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_UYVY:
#if 0
    case V4L2_PIX_FMT_Y41P:
#endif
    case V4L2_PIX_FMT_YUV422P:
#ifdef V4L2_PIX_FMT_YVYU
    case V4L2_PIX_FMT_YVYU:
#endif
    case V4L2_PIX_FMT_YUV411P:{
      GstVideoFormat format;

      switch (fourcc) {
        case V4L2_PIX_FMT_GREY:        /*  8  Greyscale     */
          format = GST_VIDEO_FORMAT_GRAY8;
          break;
        case V4L2_PIX_FMT_RGB555:
          format = GST_VIDEO_FORMAT_RGB15;
          break;
        case V4L2_PIX_FMT_RGB565:
          format = GST_VIDEO_FORMAT_RGB16;
          break;
        case V4L2_PIX_FMT_RGB24:
          format = GST_VIDEO_FORMAT_RGB;
          break;
        case V4L2_PIX_FMT_BGR24:
          format = GST_VIDEO_FORMAT_BGR;
          break;
        case V4L2_PIX_FMT_RGB32:
          format = GST_VIDEO_FORMAT_RGBx;
          break;
        case V4L2_PIX_FMT_BGR32:
          format = GST_VIDEO_FORMAT_BGRx;
          break;
        case V4L2_PIX_FMT_NV12:
          format = GST_VIDEO_FORMAT_NV12;
          break;
        case V4L2_PIX_FMT_NV21:
          format = GST_VIDEO_FORMAT_NV21;
          break;
        case V4L2_PIX_FMT_YVU410:
          format = GST_VIDEO_FORMAT_YVU9;
          break;
        case V4L2_PIX_FMT_YUV410:
          format = GST_VIDEO_FORMAT_YUV9;
          break;
        case V4L2_PIX_FMT_YUV420:
          format = GST_VIDEO_FORMAT_I420;
          break;
        case V4L2_PIX_FMT_YUYV:
          format = GST_VIDEO_FORMAT_YUY2;
          break;
        case V4L2_PIX_FMT_YVU420:
          format = GST_VIDEO_FORMAT_YV12;
          break;
        case V4L2_PIX_FMT_UYVY:
          format = GST_VIDEO_FORMAT_UYVY;
          break;
#if 0
        case V4L2_PIX_FMT_Y41P:
          format = GST_VIDEO_FORMAT_Y41P;
          break;
#endif
        case V4L2_PIX_FMT_YUV411P:
          format = GST_VIDEO_FORMAT_Y41B;
          break;
        case V4L2_PIX_FMT_YUV422P:
          format = GST_VIDEO_FORMAT_Y42B;
          break;
#ifdef V4L2_PIX_FMT_YVYU
        case V4L2_PIX_FMT_YVYU:
          format = GST_VIDEO_FORMAT_YVYU;
          break;
#endif
        default:
          g_assert_not_reached ();
          break;
      }
      structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
      break;
    }
    case V4L2_PIX_FMT_DV:
      structure =
          gst_structure_new ("video/x-dv", "systemstream", G_TYPE_BOOLEAN, TRUE,
          NULL);
      break;
    case V4L2_PIX_FMT_MPEG:    /* MPEG          */
      structure = gst_structure_new ("video/mpegts", NULL);
      break;
    case V4L2_PIX_FMT_WNVA:    /* Winnov hw compres */
      break;
#ifdef V4L2_PIX_FMT_SBGGR8
    case V4L2_PIX_FMT_SBGGR8:
      structure = gst_structure_new ("video/x-raw-bayer", NULL);
      break;
#endif
#ifdef V4L2_PIX_FMT_SN9C10X
    case V4L2_PIX_FMT_SN9C10X:
      structure = gst_structure_new ("video/x-sonix", NULL);
      break;
#endif
#ifdef V4L2_PIX_FMT_PWC1
    case V4L2_PIX_FMT_PWC1:
      structure = gst_structure_new ("video/x-pwc1", NULL);
      break;
#endif
#ifdef V4L2_PIX_FMT_PWC2
    case V4L2_PIX_FMT_PWC2:
      structure = gst_structure_new ("video/x-pwc2", NULL);
      break;
#endif
    default:
      GST_DEBUG ("Unknown fourcc 0x%08x %" GST_FOURCC_FORMAT,
          fourcc, GST_FOURCC_ARGS (fourcc));
      break;
  }

  return structure;
}



GstCaps *
gst_v4l2_object_get_all_caps (void)
{
  static GstCaps *caps = NULL;

  if (caps == NULL) {
    GstStructure *structure;

    guint i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < GST_V4L2_FORMAT_COUNT; i++) {
      structure =
          gst_v4l2_object_v4l2fourcc_to_structure (gst_v4l2_formats[i].format);
      if (structure) {
        if (gst_v4l2_formats[i].dimensions) {
          gst_structure_set (structure,
              "width", GST_TYPE_INT_RANGE, 1, GST_V4L2_MAX_SIZE,
              "height", GST_TYPE_INT_RANGE, 1, GST_V4L2_MAX_SIZE,
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);
        }
        gst_caps_append_structure (caps, structure);
      }
    }
  }

  return gst_caps_ref (caps);
}


/* collect data for the given caps
 * @caps: given input caps
 * @format: location for the v4l format
 * @w/@h: location for width and height
 * @fps_n/@fps_d: location for framerate
 * @size: location for expected size of the frame or 0 if unknown
 */
static gboolean
gst_v4l2_object_get_caps_info (GstV4l2Object * v4l2object, GstCaps * caps,
    struct v4l2_fmtdesc **format, GstVideoInfo * info)
{
  GstStructure *structure;
  guint32 fourcc;
  const gchar *mimetype;
  struct v4l2_fmtdesc *fmt;

  /* default unknown values */
  fourcc = 0;

  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (g_str_equal (mimetype, "video/x-raw")) {
    /* raw caps, parse into video info */
    if (!gst_video_info_from_caps (info, caps))
      goto invalid_format;

    switch (GST_VIDEO_INFO_FORMAT (info)) {
      case GST_VIDEO_FORMAT_I420:
        fourcc = V4L2_PIX_FMT_YUV420;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        fourcc = V4L2_PIX_FMT_YUYV;
        break;
#if 0
      case GST_VIDEO_FORMAT_Y41P:
        fourcc = V4L2_PIX_FMT_Y41P;
        break;
#endif
      case GST_VIDEO_FORMAT_UYVY:
        fourcc = V4L2_PIX_FMT_UYVY;
        break;
      case GST_VIDEO_FORMAT_YV12:
        fourcc = V4L2_PIX_FMT_YVU420;
        break;
      case GST_VIDEO_FORMAT_Y41B:
        fourcc = V4L2_PIX_FMT_YUV411P;
        break;
      case GST_VIDEO_FORMAT_Y42B:
        fourcc = V4L2_PIX_FMT_YUV422P;
        break;
      case GST_VIDEO_FORMAT_NV12:
        fourcc = V4L2_PIX_FMT_NV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        fourcc = V4L2_PIX_FMT_NV21;
        break;
#ifdef V4L2_PIX_FMT_YVYU
      case GST_VIDEO_FORMAT_YVYU:
        fourcc = V4L2_PIX_FMT_YVYU;
        break;
#endif
      case GST_VIDEO_FORMAT_RGB15:
        fourcc = V4L2_PIX_FMT_RGB555;
        break;
      case GST_VIDEO_FORMAT_RGB16:
        fourcc = V4L2_PIX_FMT_RGB565;
        break;
      case GST_VIDEO_FORMAT_RGB:
        fourcc = V4L2_PIX_FMT_RGB24;
        break;
      case GST_VIDEO_FORMAT_BGR:
        fourcc = V4L2_PIX_FMT_BGR24;
        break;
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_RGBA:
        fourcc = V4L2_PIX_FMT_RGB32;
        break;
      case GST_VIDEO_FORMAT_BGRx:
      case GST_VIDEO_FORMAT_BGRA:
        fourcc = V4L2_PIX_FMT_BGR32;
        break;
      case GST_VIDEO_FORMAT_GRAY8:
        fourcc = V4L2_PIX_FMT_GREY;
      default:
        break;
    }
  } else {
    gboolean dimensions = TRUE;

    /* no video caps, construct videoinfo ourselves */
    gst_video_info_init (info);

    if (g_str_equal (mimetype, "video/mpegts")) {
      fourcc = V4L2_PIX_FMT_MPEG;
      dimensions = FALSE;
    } else if (g_str_equal (mimetype, "video/x-dv")) {
      fourcc = V4L2_PIX_FMT_DV;
    } else if (g_str_equal (mimetype, "image/jpeg")) {
      fourcc = V4L2_PIX_FMT_JPEG;
#ifdef V4L2_PIX_FMT_SBGGR8
    } else if (g_str_equal (mimetype, "video/x-raw-bayer")) {
      fourcc = V4L2_PIX_FMT_SBGGR8;
#endif
#ifdef V4L2_PIX_FMT_SN9C10X
    } else if (g_str_equal (mimetype, "video/x-sonix")) {
      fourcc = V4L2_PIX_FMT_SN9C10X;
#endif
#ifdef V4L2_PIX_FMT_PWC1
    } else if (g_str_equal (mimetype, "video/x-pwc1")) {
      fourcc = V4L2_PIX_FMT_PWC1;
#endif
#ifdef V4L2_PIX_FMT_PWC2
    } else if (g_str_equal (mimetype, "video/x-pwc2")) {
      fourcc = V4L2_PIX_FMT_PWC2;
    }
#endif

    if (dimensions) {
      gboolean interlaced;

      if (!gst_structure_get_int (structure, "width", &info->width))
        goto no_width;

      if (!gst_structure_get_int (structure, "height", &info->height))
        goto no_height;

      if (!gst_structure_get_boolean (structure, "interlaced", &interlaced))
        interlaced = FALSE;
      if (interlaced)
        info->flags |= GST_VIDEO_FLAG_INTERLACED;

      if (!gst_structure_get_fraction (structure, "framerate", &info->fps_n,
              &info->fps_d))
        goto no_framerate;
    }
  }

  if (fourcc == 0)
    goto unhandled_format;

  fmt = gst_v4l2_object_get_format_from_fourcc (v4l2object, fourcc);
  if (fmt == NULL)
    goto unsupported_format;

  *format = fmt;

  return TRUE;

  /* ERRORS */
no_width:
  {
    GST_DEBUG_OBJECT (v4l2object, "no width");
    return FALSE;
  }
no_height:
  {
    GST_DEBUG_OBJECT (v4l2object, "no height");
    return FALSE;
  }
no_framerate:
  {
    GST_DEBUG_OBJECT (v4l2object, "no framerate");
    return FALSE;
  }
invalid_format:
  {
    GST_DEBUG_OBJECT (v4l2object, "invalid format");
    return FALSE;
  }
unhandled_format:
  {
    GST_DEBUG_OBJECT (v4l2object, "unhandled format");
    return FALSE;
  }
unsupported_format:
  {
    GST_DEBUG_OBJECT (v4l2object, "unsupported format");
    return FALSE;
  }
}


static gboolean
gst_v4l2_object_get_nearest_size (GstV4l2Object * v4l2object,
    guint32 pixelformat, gint * width, gint * height, gboolean * interlaced);


/* The frame interval enumeration code first appeared in Linux 2.6.19. */
#ifdef VIDIOC_ENUM_FRAMEINTERVALS
static GstStructure *
gst_v4l2_object_probe_caps_for_format_and_size (GstV4l2Object * v4l2object,
    guint32 pixelformat,
    guint32 width, guint32 height, const GstStructure * template)
{
  gint fd = v4l2object->video_fd;
  struct v4l2_frmivalenum ival;
  guint32 num, denom;
  GstStructure *s;
  GValue rates = { 0, };
  gboolean interlaced;
  gint int_width = width;
  gint int_height = height;

  /* interlaced detection using VIDIOC_TRY/S_FMT */
  if (!gst_v4l2_object_get_nearest_size (v4l2object, pixelformat,
          &int_width, &int_height, &interlaced))
    return NULL;

  memset (&ival, 0, sizeof (struct v4l2_frmivalenum));
  ival.index = 0;
  ival.pixel_format = pixelformat;
  ival.width = width;
  ival.height = height;

  GST_LOG_OBJECT (v4l2object->element,
      "get frame interval for %ux%u, %" GST_FOURCC_FORMAT, width, height,
      GST_FOURCC_ARGS (pixelformat));

  /* keep in mind that v4l2 gives us frame intervals (durations); we invert the
   * fraction to get framerate */
  if (v4l2_ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) < 0)
    goto enum_frameintervals_failed;

  if (ival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
    GValue rate = { 0, };

    g_value_init (&rates, GST_TYPE_LIST);
    g_value_init (&rate, GST_TYPE_FRACTION);

    do {
      num = ival.discrete.numerator;
      denom = ival.discrete.denominator;

      if (num > G_MAXINT || denom > G_MAXINT) {
        /* let us hope we don't get here... */
        num >>= 1;
        denom >>= 1;
      }

      GST_LOG_OBJECT (v4l2object->element, "adding discrete framerate: %d/%d",
          denom, num);

      /* swap to get the framerate */
      gst_value_set_fraction (&rate, denom, num);
      gst_value_list_append_value (&rates, &rate);

      ival.index++;
    } while (v4l2_ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) >= 0);
  } else if (ival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
    GValue min = { 0, };
    GValue step = { 0, };
    GValue max = { 0, };
    gboolean added = FALSE;
    guint32 minnum, mindenom;
    guint32 maxnum, maxdenom;

    g_value_init (&rates, GST_TYPE_LIST);

    g_value_init (&min, GST_TYPE_FRACTION);
    g_value_init (&step, GST_TYPE_FRACTION);
    g_value_init (&max, GST_TYPE_FRACTION);

    /* get the min */
    minnum = ival.stepwise.min.numerator;
    mindenom = ival.stepwise.min.denominator;
    if (minnum > G_MAXINT || mindenom > G_MAXINT) {
      minnum >>= 1;
      mindenom >>= 1;
    }
    GST_LOG_OBJECT (v4l2object->element, "stepwise min frame interval: %d/%d",
        minnum, mindenom);
    gst_value_set_fraction (&min, minnum, mindenom);

    /* get the max */
    maxnum = ival.stepwise.max.numerator;
    maxdenom = ival.stepwise.max.denominator;
    if (maxnum > G_MAXINT || maxdenom > G_MAXINT) {
      maxnum >>= 1;
      maxdenom >>= 1;
    }

    GST_LOG_OBJECT (v4l2object->element, "stepwise max frame interval: %d/%d",
        maxnum, maxdenom);
    gst_value_set_fraction (&max, maxnum, maxdenom);

    /* get the step */
    num = ival.stepwise.step.numerator;
    denom = ival.stepwise.step.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }

    if (num == 0 || denom == 0) {
      /* in this case we have a wrong fraction or no step, set the step to max
       * so that we only add the min value in the loop below */
      num = maxnum;
      denom = maxdenom;
    }

    /* since we only have gst_value_fraction_subtract and not add, negate the
     * numerator */
    GST_LOG_OBJECT (v4l2object->element, "stepwise step frame interval: %d/%d",
        num, denom);
    gst_value_set_fraction (&step, -num, denom);

    while (gst_value_compare (&min, &max) <= 0) {
      GValue rate = { 0, };

      num = gst_value_get_fraction_numerator (&min);
      denom = gst_value_get_fraction_denominator (&min);
      GST_LOG_OBJECT (v4l2object->element, "adding stepwise framerate: %d/%d",
          denom, num);

      /* invert to get the framerate */
      g_value_init (&rate, GST_TYPE_FRACTION);
      gst_value_set_fraction (&rate, denom, num);
      gst_value_list_append_value (&rates, &rate);
      added = TRUE;

      /* we're actually adding because step was negated above. This is because
       * there is no _add function... */
      if (!gst_value_fraction_subtract (&min, &min, &step)) {
        GST_WARNING_OBJECT (v4l2object->element, "could not step fraction!");
        break;
      }
    }
    if (!added) {
      /* no range was added, leave the default range from the template */
      GST_WARNING_OBJECT (v4l2object->element,
          "no range added, leaving default");
      g_value_unset (&rates);
    }
  } else if (ival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
    guint32 maxnum, maxdenom;

    g_value_init (&rates, GST_TYPE_FRACTION_RANGE);

    num = ival.stepwise.min.numerator;
    denom = ival.stepwise.min.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }

    maxnum = ival.stepwise.max.numerator;
    maxdenom = ival.stepwise.max.denominator;
    if (maxnum > G_MAXINT || maxdenom > G_MAXINT) {
      maxnum >>= 1;
      maxdenom >>= 1;
    }

    GST_LOG_OBJECT (v4l2object->element,
        "continuous frame interval %d/%d to %d/%d", maxdenom, maxnum, denom,
        num);

    gst_value_set_fraction_range_full (&rates, maxdenom, maxnum, denom, num);
  } else {
    goto unknown_type;
  }

return_data:
  s = gst_structure_copy (template);
  gst_structure_set (s, "width", G_TYPE_INT, (gint) width,
      "height", G_TYPE_INT, (gint) height,
      "interlaced", G_TYPE_BOOLEAN, interlaced, NULL);

  if (G_IS_VALUE (&rates)) {
    /* only change the framerate on the template when we have a valid probed new
     * value */
    gst_structure_set_value (s, "framerate", &rates);
    g_value_unset (&rates);
  } else {
    gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1,
        NULL);
  }
  return s;

  /* ERRORS */
enum_frameintervals_failed:
  {
    GST_DEBUG_OBJECT (v4l2object->element,
        "Unable to enumerate intervals for %" GST_FOURCC_FORMAT "@%ux%u",
        GST_FOURCC_ARGS (pixelformat), width, height);
    goto return_data;
  }
unknown_type:
  {
    /* I don't see how this is actually an error, we ignore the format then */
    GST_WARNING_OBJECT (v4l2object->element,
        "Unknown frame interval type at %" GST_FOURCC_FORMAT "@%ux%u: %u",
        GST_FOURCC_ARGS (pixelformat), width, height, ival.type);
    return NULL;
  }
}
#endif /* defined VIDIOC_ENUM_FRAMEINTERVALS */

#ifdef VIDIOC_ENUM_FRAMESIZES
static gint
sort_by_frame_size (GstStructure * s1, GstStructure * s2)
{
  int w1, h1, w2, h2;

  gst_structure_get_int (s1, "width", &w1);
  gst_structure_get_int (s1, "height", &h1);
  gst_structure_get_int (s2, "width", &w2);
  gst_structure_get_int (s2, "height", &h2);

  /* I think it's safe to assume that this won't overflow for a while */
  return ((w2 * h2) - (w1 * h1));
}
#endif

GstCaps *
gst_v4l2_object_probe_caps_for_format (GstV4l2Object * v4l2object,
    guint32 pixelformat, const GstStructure * template)
{
  GstCaps *ret = gst_caps_new_empty ();
  GstStructure *tmp;

#ifdef VIDIOC_ENUM_FRAMESIZES
  gint fd = v4l2object->video_fd;
  struct v4l2_frmsizeenum size;
  GList *results = NULL;
  guint32 w, h;

  if (pixelformat == GST_MAKE_FOURCC ('M', 'P', 'E', 'G'))
    return gst_caps_new_simple ("video/mpegts", NULL);

  memset (&size, 0, sizeof (struct v4l2_frmsizeenum));
  size.index = 0;
  size.pixel_format = pixelformat;

  GST_DEBUG_OBJECT (v4l2object->element, "Enumerating frame sizes");

  if (v4l2_ioctl (fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0)
    goto enum_framesizes_failed;

  if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    do {
      GST_LOG_OBJECT (v4l2object->element, "got discrete frame size %dx%d",
          size.discrete.width, size.discrete.height);

      w = MIN (size.discrete.width, G_MAXINT);
      h = MIN (size.discrete.height, G_MAXINT);

      if (w && h) {
        tmp =
            gst_v4l2_object_probe_caps_for_format_and_size (v4l2object,
            pixelformat, w, h, template);

        if (tmp)
          results = g_list_prepend (results, tmp);
      }

      size.index++;
    } while (v4l2_ioctl (fd, VIDIOC_ENUM_FRAMESIZES, &size) >= 0);
    GST_DEBUG_OBJECT (v4l2object->element,
        "done iterating discrete frame sizes");
  } else if (size.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
    GST_DEBUG_OBJECT (v4l2object->element, "we have stepwise frame sizes:");
    GST_DEBUG_OBJECT (v4l2object->element, "min width:   %d",
        size.stepwise.min_width);
    GST_DEBUG_OBJECT (v4l2object->element, "min height:  %d",
        size.stepwise.min_height);
    GST_DEBUG_OBJECT (v4l2object->element, "max width:   %d",
        size.stepwise.max_width);
    GST_DEBUG_OBJECT (v4l2object->element, "min height:  %d",
        size.stepwise.max_height);
    GST_DEBUG_OBJECT (v4l2object->element, "step width:  %d",
        size.stepwise.step_width);
    GST_DEBUG_OBJECT (v4l2object->element, "step height: %d",
        size.stepwise.step_height);

    for (w = size.stepwise.min_width, h = size.stepwise.min_height;
        w < size.stepwise.max_width && h < size.stepwise.max_height;
        w += size.stepwise.step_width, h += size.stepwise.step_height) {
      if (w == 0 || h == 0)
        continue;

      tmp =
          gst_v4l2_object_probe_caps_for_format_and_size (v4l2object,
          pixelformat, w, h, template);

      if (tmp)
        results = g_list_prepend (results, tmp);
    }
    GST_DEBUG_OBJECT (v4l2object->element,
        "done iterating stepwise frame sizes");
  } else if (size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
    guint32 maxw, maxh;

    GST_DEBUG_OBJECT (v4l2object->element, "we have continuous frame sizes:");
    GST_DEBUG_OBJECT (v4l2object->element, "min width:   %d",
        size.stepwise.min_width);
    GST_DEBUG_OBJECT (v4l2object->element, "min height:  %d",
        size.stepwise.min_height);
    GST_DEBUG_OBJECT (v4l2object->element, "max width:   %d",
        size.stepwise.max_width);
    GST_DEBUG_OBJECT (v4l2object->element, "min height:  %d",
        size.stepwise.max_height);

    w = MAX (size.stepwise.min_width, 1);
    h = MAX (size.stepwise.min_height, 1);
    maxw = MIN (size.stepwise.max_width, G_MAXINT);
    maxh = MIN (size.stepwise.max_height, G_MAXINT);

    tmp =
        gst_v4l2_object_probe_caps_for_format_and_size (v4l2object, pixelformat,
        w, h, template);
    if (tmp) {
      gst_structure_set (tmp, "width", GST_TYPE_INT_RANGE, (gint) w,
          (gint) maxw, "height", GST_TYPE_INT_RANGE, (gint) h, (gint) maxh,
          NULL);

      /* no point using the results list here, since there's only one struct */
      gst_caps_append_structure (ret, tmp);
    }
  } else {
    goto unknown_type;
  }

  /* we use an intermediary list to store and then sort the results of the
   * probing because we can't make any assumptions about the order in which
   * the driver will give us the sizes, but we want the final caps to contain
   * the results starting with the highest resolution and having the lowest
   * resolution last, since order in caps matters for things like fixation. */
  results = g_list_sort (results, (GCompareFunc) sort_by_frame_size);
  while (results != NULL) {
    gst_caps_append_structure (ret, GST_STRUCTURE (results->data));
    results = g_list_delete_link (results, results);
  }

  if (gst_caps_is_empty (ret))
    goto enum_framesizes_no_results;

  return ret;

  /* ERRORS */
enum_framesizes_failed:
  {
    /* I don't see how this is actually an error */
    GST_DEBUG_OBJECT (v4l2object->element,
        "Failed to enumerate frame sizes for pixelformat %" GST_FOURCC_FORMAT
        " (%s)", GST_FOURCC_ARGS (pixelformat), g_strerror (errno));
    goto default_frame_sizes;
  }
enum_framesizes_no_results:
  {
    /* it's possible that VIDIOC_ENUM_FRAMESIZES is defined but the driver in
     * question doesn't actually support it yet */
    GST_DEBUG_OBJECT (v4l2object->element,
        "No results for pixelformat %" GST_FOURCC_FORMAT
        " enumerating frame sizes, trying fallback",
        GST_FOURCC_ARGS (pixelformat));
    goto default_frame_sizes;
  }
unknown_type:
  {
    GST_WARNING_OBJECT (v4l2object->element,
        "Unknown frame sizeenum type for pixelformat %" GST_FOURCC_FORMAT
        ": %u", GST_FOURCC_ARGS (pixelformat), size.type);
    goto default_frame_sizes;
  }
default_frame_sizes:
#endif /* defined VIDIOC_ENUM_FRAMESIZES */
  {
    gint min_w, max_w, min_h, max_h, fix_num = 0, fix_denom = 0;
    gboolean interlaced;

    /* This code is for Linux < 2.6.19 */
    min_w = min_h = 1;
    max_w = max_h = GST_V4L2_MAX_SIZE;
    if (!gst_v4l2_object_get_nearest_size (v4l2object, pixelformat, &min_w,
            &min_h, &interlaced)) {
      GST_WARNING_OBJECT (v4l2object->element,
          "Could not probe minimum capture size for pixelformat %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (pixelformat));
    }
    if (!gst_v4l2_object_get_nearest_size (v4l2object, pixelformat, &max_w,
            &max_h, &interlaced)) {
      GST_WARNING_OBJECT (v4l2object->element,
          "Could not probe maximum capture size for pixelformat %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (pixelformat));
    }

    /* Since we can't get framerate directly, try to use the current norm */
    if (v4l2object->tv_norm && v4l2object->norms) {
      GList *norms;
      GstTunerNorm *norm = NULL;
      GstTunerNorm *current =
          gst_v4l2_tuner_get_norm_by_std_id (v4l2object, v4l2object->tv_norm);

      for (norms = v4l2object->norms; norms != NULL; norms = norms->next) {
        norm = (GstTunerNorm *) norms->data;
        if (!strcmp (norm->label, current->label))
          break;
      }
      /* If it's possible, set framerate to that (discrete) value */
      if (norm) {
        fix_num = gst_value_get_fraction_numerator (&norm->framerate);
        fix_denom = gst_value_get_fraction_denominator (&norm->framerate);
      }
    }

    tmp = gst_structure_copy (template);
    if (fix_num) {
      gst_structure_set (tmp, "framerate", GST_TYPE_FRACTION, fix_num,
          fix_denom, NULL);
    } else {
      /* if norm can't be used, copy the template framerate */
      gst_structure_set (tmp, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1,
          100, 1, NULL);
    }

    if (min_w == max_w)
      gst_structure_set (tmp, "width", G_TYPE_INT, max_w, NULL);
    else
      gst_structure_set (tmp, "width", GST_TYPE_INT_RANGE, min_w, max_w, NULL);

    if (min_h == max_h)
      gst_structure_set (tmp, "height", G_TYPE_INT, max_h, NULL);
    else
      gst_structure_set (tmp, "height", GST_TYPE_INT_RANGE, min_h, max_h, NULL);

    gst_structure_set (tmp, "interlaced", G_TYPE_BOOLEAN, interlaced, NULL);

    gst_caps_append_structure (ret, tmp);

    return ret;
  }
}

static gboolean
gst_v4l2_object_get_nearest_size (GstV4l2Object * v4l2object,
    guint32 pixelformat, gint * width, gint * height, gboolean * interlaced)
{
  struct v4l2_format fmt;
  int fd;
  int r;

  g_return_val_if_fail (width != NULL, FALSE);
  g_return_val_if_fail (height != NULL, FALSE);

  GST_LOG_OBJECT (v4l2object->element,
      "getting nearest size to %dx%d with format %" GST_FOURCC_FORMAT,
      *width, *height, GST_FOURCC_ARGS (pixelformat));

  fd = v4l2object->video_fd;

  /* get size delimiters */
  memset (&fmt, 0, sizeof (fmt));
  fmt.type = v4l2object->type;
  fmt.fmt.pix.width = *width;
  fmt.fmt.pix.height = *height;
  fmt.fmt.pix.pixelformat = pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  r = v4l2_ioctl (fd, VIDIOC_TRY_FMT, &fmt);
  if (r < 0 && errno == EINVAL) {
    /* try again with interlaced video */
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    r = v4l2_ioctl (fd, VIDIOC_TRY_FMT, &fmt);
  }

  if (r < 0) {
    /* The driver might not implement TRY_FMT, in which case we will try
       S_FMT to probe */
    if (errno != ENOTTY)
      return FALSE;

    /* Only try S_FMT if we're not actively capturing yet, which we shouldn't
       be, because we're still probing */
    if (GST_V4L2_IS_ACTIVE (v4l2object))
      return FALSE;

    GST_LOG_OBJECT (v4l2object->element,
        "Failed to probe size limit with VIDIOC_TRY_FMT, trying VIDIOC_S_FMT");

    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;

    r = v4l2_ioctl (fd, VIDIOC_S_FMT, &fmt);
    if (r < 0 && errno == EINVAL) {
      /* try again with progressive video */
      fmt.fmt.pix.width = *width;
      fmt.fmt.pix.height = *height;
      fmt.fmt.pix.pixelformat = pixelformat;
      fmt.fmt.pix.field = V4L2_FIELD_NONE;
      r = v4l2_ioctl (fd, VIDIOC_S_FMT, &fmt);
    }

    if (r < 0)
      return FALSE;
  }

  GST_LOG_OBJECT (v4l2object->element,
      "got nearest size %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);

  *width = fmt.fmt.pix.width;
  *height = fmt.fmt.pix.height;

  switch (fmt.fmt.pix.field) {
    case V4L2_FIELD_ANY:
    case V4L2_FIELD_NONE:
      *interlaced = FALSE;
      break;
    case V4L2_FIELD_INTERLACED:
    case V4L2_FIELD_INTERLACED_TB:
    case V4L2_FIELD_INTERLACED_BT:
      *interlaced = TRUE;
      break;
    default:
      GST_WARNING_OBJECT (v4l2object->element,
          "Unsupported field type for %" GST_FOURCC_FORMAT "@%ux%u",
          GST_FOURCC_ARGS (pixelformat), *width, *height);
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_v4l2_object_setup_pool (GstV4l2Object * v4l2object, GstCaps * caps)
{
  guint num_buffers;
  GstStructure *config;
  GstV4l2IOMode mode;

  GST_DEBUG_OBJECT (v4l2object->element, "initializing the capture system");

  GST_V4L2_CHECK_OPEN (v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2object);

  /* find transport */
  mode = v4l2object->req_mode;

  if (v4l2object->vcap.capabilities & V4L2_CAP_READWRITE) {
    if (v4l2object->req_mode == GST_V4L2_IO_AUTO)
      mode = GST_V4L2_IO_RW;
  } else if (v4l2object->req_mode == GST_V4L2_IO_RW)
    goto method_not_supported;

  if (v4l2object->vcap.capabilities & V4L2_CAP_STREAMING) {
    if (v4l2object->req_mode == GST_V4L2_IO_AUTO)
      mode = GST_V4L2_IO_MMAP;
  } else if (v4l2object->req_mode == GST_V4L2_IO_MMAP)
    goto method_not_supported;

  /* if still no transport selected, error out */
  if (mode == GST_V4L2_IO_AUTO)
    goto no_supported_capture_method;

  GST_INFO_OBJECT (v4l2object->element, "accessing buffers via mode %d", mode);
  v4l2object->mode = mode;

  /* keep track of current number of buffers */
  num_buffers = v4l2object->num_buffers;

  /* Map the buffers */
  GST_LOG_OBJECT (v4l2object->element, "initiating buffer pool");

  if (!(v4l2object->pool = gst_v4l2_buffer_pool_new (v4l2object)))
    goto buffer_pool_new_failed;

  config = gst_buffer_pool_get_config (v4l2object->pool);
  gst_buffer_pool_config_set (config, caps, v4l2object->info.size,
      num_buffers, num_buffers, 0, 0);
  gst_buffer_pool_set_config (v4l2object->pool, config);

  GST_V4L2_SET_ACTIVE (v4l2object);

  return TRUE;

  /* ERRORS */
buffer_pool_new_failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("Could not map buffers from device '%s'"),
            v4l2object->videodev),
        ("Failed to create buffer pool: %s", g_strerror (errno)));
    return FALSE;
  }
method_not_supported:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("The driver of device '%s' does not support the IO method %d"),
            v4l2object->videodev, mode), (NULL));
    return FALSE;
  }
no_supported_capture_method:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("The driver of device '%s' does not support any known IO "
                "method."), v4l2object->videodev), (NULL));
    return FALSE;
  }
}


/* Note about fraction simplification
 *  * n1/d1 == n2/d2  is also written as  n1 == ( n2 * d1 ) / d2
 *   */
#define fractions_are_equal(n1,d1,n2,d2) ((n1) == gst_util_uint64_scale_int((n2), (d1), (d2)))

gboolean
gst_v4l2_object_set_format (GstV4l2Object * v4l2object, GstCaps * caps)
{
  gint fd = v4l2object->video_fd;
  struct v4l2_format format;
  struct v4l2_streamparm streamparm;
  enum v4l2_field field;
  guint32 pixelformat;
  struct v4l2_fmtdesc *fmtdesc;
  GstVideoInfo info;
  gint width, height, fps_n, fps_d, stride;

  if (!gst_v4l2_object_get_caps_info (v4l2object, caps, &fmtdesc, &info))
    goto invalid_caps;

  pixelformat = fmtdesc->pixelformat;
  width = GST_VIDEO_INFO_WIDTH (&info);
  height = GST_VIDEO_INFO_HEIGHT (&info);
  fps_n = GST_VIDEO_INFO_FPS_N (&info);
  fps_d = GST_VIDEO_INFO_FPS_D (&info);
  stride = GST_VIDEO_INFO_PLANE_STRIDE (&info, 0);

  if (info.flags & GST_VIDEO_FLAG_INTERLACED) {
    GST_DEBUG_OBJECT (v4l2object->element, "interlaced video");
    /* ideally we would differentiate between types of interlaced video
     * but there is not sufficient information in the caps..
     */
    field = V4L2_FIELD_INTERLACED;
  } else {
    GST_DEBUG_OBJECT (v4l2object->element, "progressive video");
    field = V4L2_FIELD_NONE;
  }

  GST_DEBUG_OBJECT (v4l2object->element, "Desired format %dx%d, format "
      "%" GST_FOURCC_FORMAT " stride: %d", width, height,
      GST_FOURCC_ARGS (pixelformat), stride);

  GST_V4L2_CHECK_OPEN (v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2object);

  /* Only unconditionally accept mpegts for sources */
  if ((v4l2object->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
      (pixelformat == GST_MAKE_FOURCC ('M', 'P', 'E', 'G')))
    goto done;

  memset (&format, 0x00, sizeof (struct v4l2_format));
  format.type = v4l2object->type;

  if (v4l2_ioctl (fd, VIDIOC_G_FMT, &format) < 0)
    goto get_fmt_failed;

  GST_DEBUG_OBJECT (v4l2object->element, "Got format to %dx%d, format "
      "%" GST_FOURCC_FORMAT " bytesperline %d, colorspace %d",
      format.fmt.pix.width, format.fmt.pix.height,
      GST_FOURCC_ARGS (format.fmt.pix.pixelformat), format.fmt.pix.bytesperline,
      format.fmt.pix.colorspace);

  if (format.type != v4l2object->type ||
      format.fmt.pix.width != width ||
      format.fmt.pix.height != height ||
      format.fmt.pix.pixelformat != pixelformat ||
      format.fmt.pix.field != field || format.fmt.pix.bytesperline != stride) {
    /* something different, set the format */
    GST_DEBUG_OBJECT (v4l2object->element, "Setting format to %dx%d, format "
        "%" GST_FOURCC_FORMAT " bytesperline %d", width, height,
        GST_FOURCC_ARGS (pixelformat), stride);

    format.type = v4l2object->type;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    format.fmt.pix.field = field;
    /* try to ask our prefered stride */
    format.fmt.pix.bytesperline = stride;

    if (v4l2_ioctl (fd, VIDIOC_S_FMT, &format) < 0)
      goto set_fmt_failed;

    GST_DEBUG_OBJECT (v4l2object->element, "Got format to %dx%d, format "
        "%" GST_FOURCC_FORMAT " stride %d", format.fmt.pix.width,
        format.fmt.pix.height, GST_FOURCC_ARGS (format.fmt.pix.pixelformat),
        format.fmt.pix.bytesperline);

    if (format.fmt.pix.width != width || format.fmt.pix.height != height)
      goto invalid_dimensions;

    if (format.fmt.pix.pixelformat != pixelformat)
      goto invalid_pixelformat;
  }

  /* figure out the frame layout */
  v4l2object->bytesperline = format.fmt.pix.bytesperline;
  v4l2object->sizeimage = format.fmt.pix.sizeimage;


  /* Is there a reason we require the caller to always specify a framerate? */
  GST_DEBUG_OBJECT (v4l2object->element, "Desired framerate: %u/%u", fps_n,
      fps_d);

  memset (&streamparm, 0x00, sizeof (struct v4l2_streamparm));
  streamparm.type = v4l2object->type;

  if (v4l2_ioctl (fd, VIDIOC_G_PARM, &streamparm) < 0)
    goto get_parm_failed;

  GST_VIDEO_INFO_FPS_N (&info) =
      streamparm.parm.capture.timeperframe.denominator;
  GST_VIDEO_INFO_FPS_D (&info) = streamparm.parm.capture.timeperframe.numerator;

  if (v4l2object->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
    GST_DEBUG_OBJECT (v4l2object->element, "Got framerate: %u/%u",
        streamparm.parm.capture.timeperframe.denominator,
        streamparm.parm.capture.timeperframe.numerator);

    /* Note: V4L2 provides the frame interval, we have the frame rate */
    if (!fractions_are_equal (streamparm.parm.capture.timeperframe.numerator,
            streamparm.parm.capture.timeperframe.denominator, fps_d, fps_n)) {
      GST_LOG_OBJECT (v4l2object->element, "Setting framerate to %u/%u", fps_n,
          fps_d);
      /* We want to change the frame rate, so check whether we can. Some cheap USB
       * cameras don't have the capability */
      if ((streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0) {
        GST_DEBUG_OBJECT (v4l2object->element,
            "Not setting framerate (not supported)");
        goto done;
      }

      /* Note: V4L2 wants the frame interval, we have the frame rate */
      streamparm.parm.capture.timeperframe.numerator = fps_d;
      streamparm.parm.capture.timeperframe.denominator = fps_n;

      /* some cheap USB cam's won't accept any change */
      if (v4l2_ioctl (fd, VIDIOC_S_PARM, &streamparm) < 0)
        goto set_parm_failed;

      /* get new values */
      fps_d = streamparm.parm.capture.timeperframe.numerator;
      fps_n = streamparm.parm.capture.timeperframe.denominator;

      GST_INFO_OBJECT (v4l2object->element, "Set framerate to %u/%u", fps_n,
          fps_d);

      GST_VIDEO_INFO_FPS_N (&info) = fps_n;
      GST_VIDEO_INFO_FPS_D (&info) = fps_d;
    }
  }

done:
  /* if we have a framerate pre-calculate duration */
  if (fps_n > 0 && fps_d > 0) {
    v4l2object->duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  } else {
    v4l2object->duration = GST_CLOCK_TIME_NONE;
  }
  v4l2object->info = info;
  v4l2object->fmtdesc = fmtdesc;

  /* now configure ther pools */
  if (!gst_v4l2_object_setup_pool (v4l2object, caps))
    goto pool_failed;

  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (v4l2object->element, "can't parse caps %" GST_PTR_FORMAT,
        caps);
    return FALSE;
  }
get_fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Device '%s' does not support video capture"),
            v4l2object->videodev),
        ("Call to G_FMT failed: (%s)", g_strerror (errno)));
    return FALSE;
  }
set_fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture at %dx%d"),
            v4l2object->videodev, width, height),
        ("Call to S_FMT failed for %" GST_FOURCC_FORMAT " @ %dx%d: %s",
            GST_FOURCC_ARGS (pixelformat), width, height, g_strerror (errno)));
    return FALSE;
  }
invalid_dimensions:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture at %dx%d"),
            v4l2object->videodev, width, height),
        ("Tried to capture at %dx%d, but device returned size %dx%d",
            width, height, format.fmt.pix.width, format.fmt.pix.height));
    return FALSE;
  }
invalid_pixelformat:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture in the specified format"),
            v4l2object->videodev),
        ("Tried to capture in %" GST_FOURCC_FORMAT
            ", but device returned format" " %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (pixelformat),
            GST_FOURCC_ARGS (format.fmt.pix.pixelformat)));
    return FALSE;
  }
get_parm_failed:
  {
    /* it's possible that this call is not supported */
    if (errno != EINVAL) {
      GST_ELEMENT_WARNING (v4l2object->element, RESOURCE, SETTINGS,
          (_("Could not get parameters on device '%s'"),
              v4l2object->videodev), GST_ERROR_SYSTEM);
    }
    goto done;
  }
set_parm_failed:
  {
    GST_ELEMENT_WARNING (v4l2object->element, RESOURCE, SETTINGS,
        (_("Video device did not accept new frame rate setting.")),
        GST_ERROR_SYSTEM);
    goto done;
  }
pool_failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,
        (_("Video device could not create buffer pool.")), GST_ERROR_SYSTEM);
    return FALSE;
  }
}

gboolean
gst_v4l2_object_unlock (GstV4l2Object * v4l2object)
{
  GST_LOG_OBJECT (v4l2object->element, "flush poll");
  gst_poll_set_flushing (v4l2object->poll, TRUE);

  return TRUE;
}

gboolean
gst_v4l2_object_unlock_stop (GstV4l2Object * v4l2object)
{
  GST_LOG_OBJECT (v4l2object->element, "flush stop poll");
  gst_poll_set_flushing (v4l2object->poll, FALSE);

  return TRUE;
}

gboolean
gst_v4l2_object_stop (GstV4l2Object * v4l2object)
{
  GST_DEBUG_OBJECT (v4l2object->element, "stopping");

  if (!GST_V4L2_IS_OPEN (v4l2object))
    goto done;
  if (!GST_V4L2_IS_ACTIVE (v4l2object))
    goto done;

  if (v4l2object->pool) {
    GST_DEBUG_OBJECT (v4l2object->element, "deactivating pool");
    gst_buffer_pool_set_active (v4l2object->pool, FALSE);
    gst_object_unref (v4l2object->pool);
    v4l2object->pool = NULL;
  }

  GST_V4L2_SET_INACTIVE (v4l2object);

done:
  return TRUE;
}

#if 0
static GstFlowReturn
gst_v4l2_object_get_mmap (GstV4l2Object * v4l2object, GstBuffer ** buf)
{
  GstFlowReturn res;
#define NUM_TRIALS 50
  GstBufferPool *pool;
  gint32 trials = NUM_TRIALS;
  GstBuffer *pool_buffer;
  gboolean need_copy;

  pool = v4l2object->pool;
  if (!pool)
    goto no_buffer_pool;

  GST_DEBUG_OBJECT (v4l2object->element, "grab frame");

  for (;;) {
    if ((res = gst_v4l2_object_poll (v4l2object)) != GST_FLOW_OK)
      goto poll_error;

    res = gst_buffer_pool_acquire_buffer (pool, &pool_buffer, NULL);
    if (res != GST_FLOW_OK)
      goto no_buffer;

    if (v4l2object->size > 0) {
      gsize size = gst_buffer_get_size (pool_buffer);

      /* if size does not match what we expected, try again */
      if (size != v4l2object->size) {
        GST_ELEMENT_WARNING (v4l2object->element, RESOURCE, READ,
            (_("Got unexpected frame size of %u instead of %u."),
                size, v4l2object->size), (NULL));
        gst_buffer_unref (pool_buffer);
        goto no_buffer;
      }
    }
    /* when we get here all is fine */
    break;

  no_buffer:
    GST_WARNING_OBJECT (v4l2object->element, "trials=%d", trials);

    /* if the sync() got interrupted, we can retry */
    switch (errno) {
      case EINVAL:
      case ENOMEM:
        /* fatal */
        return GST_FLOW_ERROR;

      case EAGAIN:
      case EIO:
      case EINTR:
      default:
        /* try again, until too many trials */
        break;
    }

    /* check nr. of attempts to capture */
    if (--trials == -1) {
      goto too_many_trials;
    }
  }


  /* if we are handing out the last buffer in the pool, we need to make a
   * copy and bring the buffer back in the pool. */
  need_copy = v4l2object->always_copy
      || !gst_v4l2_buffer_pool_available_buffers (pool);

  if (G_UNLIKELY (need_copy)) {
    if (!v4l2object->always_copy) {
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, v4l2object->element,
          "running out of buffers, making a copy to reuse current one");
    }
    *buf = gst_buffer_copy (pool_buffer);
    /* this will requeue */
    gst_buffer_unref (pool_buffer);
  } else {
    *buf = pool_buffer;
  }

  return GST_FLOW_OK;

  /* ERRORS */
no_buffer_pool:
  {
    GST_DEBUG_OBJECT (v4l2object->element, "no buffer pool");
    return GST_FLOW_WRONG_STATE;
  }
poll_error:
  {
    return res;
  }
too_many_trials:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2object->videodev),
        (_("Failed after %d tries. device %s. system error: %s"),
            NUM_TRIALS, v4l2object->videodev, g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
}
#endif

gboolean
gst_v4l2_object_copy (GstV4l2Object * v4l2object, GstBuffer * dest,
    GstBuffer * src)
{
  guint8 *data;
  gsize size;

  if (v4l2object->info.finfo) {
    GstVideoFrame src_frame, dest_frame;

    GST_DEBUG_OBJECT (v4l2object->element, "copy video frame");

    /* we have raw video, use videoframe copy to get strides right */
    if (!gst_video_frame_map (&src_frame, &v4l2object->info, src, GST_MAP_READ))
      goto invalid_buffer;

    if (!gst_video_frame_map (&dest_frame, &v4l2object->info, dest,
            GST_MAP_WRITE)) {
      gst_video_frame_unmap (&src_frame);
      goto invalid_buffer;
    }

    gst_video_frame_copy (&dest_frame, &src_frame);

    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
  } else {
    GST_DEBUG_OBJECT (v4l2object->element, "copy raw bytes");
    data = gst_buffer_map (src, &size, NULL, GST_MAP_READ);
    gst_buffer_fill (dest, 0, data, size);
    gst_buffer_unmap (src, data, size);
  }
  GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, v4l2object->element,
      "slow copy into buffer %p", dest);

  return TRUE;

  /* ERRORS */
invalid_buffer:
  {
    /* No Window available to put our image into */
    GST_WARNING_OBJECT (v4l2object->element, "could not map image");
    return FALSE;
  }
}
