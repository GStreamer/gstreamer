/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosshelper.c: helper functions for easy OSS device handling.
 * See gstosshelper.h for details.
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

#include "gst/gst-i18n-plugin.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/soundcard.h>

#include <gst/interfaces/propertyprobe.h>

#include "gstosshelper.h"
#include "gstossmixer.h"

static void gst_ossprobe_interface_init (GstPropertyProbeInterface * iface);

static GList *device_combinations = NULL;

void
gst_oss_add_mixer_type (GType type)
{
  static const GInterfaceInfo ossiface_info = {
    (GInterfaceInitFunc) gst_oss_interface_init,
    NULL,
    NULL
  };
  static const GInterfaceInfo ossmixer_info = {
    (GInterfaceInitFunc) gst_ossmixer_interface_init,
    NULL,
    NULL
  };
  static const GInterfaceInfo ossprobe_info = {
    (GInterfaceInitFunc) gst_ossprobe_interface_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &ossiface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &ossmixer_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE, &ossprobe_info);
}

void
gst_oss_add_device_properties (GstElementClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_object_class_install_property (gobject_class, OSS_ARG_DEVICE,
      g_param_spec_string ("device", "Device", "OSS device (/dev/dspN usually)",
          "default", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, OSS_ARG_MIXER_DEVICE,
      g_param_spec_string ("mixerdev", "Mixer device",
          "OSS mixer device (/dev/mixerN usually)",
          "default", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, OSS_ARG_DEVICE_NAME,
      g_param_spec_string ("device_name", "Device name", "Name of the device",
          NULL, G_PARAM_READABLE));
}

void
gst_oss_set_device_property (GstElement * element,
    GstOssDeviceCombination * c, GstOssDevice * oss,
    guint prop_id, GParamSpec * pspec, const GValue * value)
{
  switch (prop_id) {
    case OSS_ARG_DEVICE:
      /* disallow changing the device while it is opened
         get_property("device") should return the right one */
      if (oss->fd == -1) {
        g_free (c->dsp);
        c->dsp = g_strdup (g_value_get_string (value));

        /* let's assume that if we have a device map for the mixer,
         * we're allowed to do all that automagically here */
        if (device_combinations != NULL) {
          GList *list = device_combinations;

          while (list) {
            GstOssDeviceCombination *combi = list->data;

            if (!strcmp (combi->dsp, c->dsp)) {
              g_free (c->mixer);
              c->mixer = g_strdup (combi->mixer);
              break;
            }

            list = list->next;
          }
        }
      }
      break;
    case OSS_ARG_MIXER_DEVICE:
      /* disallow changing the device while it is opened
         get_property("mixerdev") should return the right one */
      if (oss->fd == -1) {
        g_free (c->mixer);
        c->mixer = g_strdup (g_value_get_string (value));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (element, prop_id, pspec);
      break;
  }
}

void
gst_oss_get_device_property (GstElement * element, GstOssDeviceCombination * c,
    GstOssDevice * oss, guint prop_id, GParamSpec * pspec, GValue * value)
{
  switch (prop_id) {
    case OSS_ARG_DEVICE:
      g_value_set_string (value, c->dsp);
      break;
    case OSS_ARG_MIXER_DEVICE:
      g_value_set_string (value, c->mixer);
      break;
    case OSS_ARG_DEVICE_NAME:
      g_value_set_string (value, oss->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (element, prop_id, pspec);
      break;
  }
}

static const GList *
gst_oss_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  return list;
}

/* OSS (without devfs) allows at max. 16 devices */
#define MAX_OSS_DEVICES 16

static void
gst_oss_do_probe (gchar * device_base,
    gint device_num, gchar ** name, dev_t * devno)
{
  gchar *device = NULL;
  struct stat s;

  if ((name == NULL) || (devno == NULL)) {
    goto end;
  }

  *name = NULL;
  *devno = 0;

  if (device_num == -1)
    device = g_strdup (device_base);
  else if ((device_num >= -1) && (device_num <= MAX_OSS_DEVICES)) {
    device = g_strdup_printf ("%s%d", device_base, device_num);
  } else {
    goto end;
  }

  if (stat (device, &s) || !S_ISCHR (s.st_mode))
    goto end;

  *name = device;
  *devno = s.st_rdev;
  return;

end:
  g_free (device);
}

static GList *
device_combination_append (GList * device_combinations,
    GstOssDeviceCombination * combi)
{
  GList *it;

  for (it = device_combinations; it != NULL; it = it->next) {
    GstOssDeviceCombination *cur;

    cur = (GstOssDeviceCombination *) it->data;
    if (cur->dev == combi->dev) {
      return device_combinations;
    }
  }

  return g_list_append (device_combinations, combi);
}

static gboolean
gst_oss_probe_devices (GstOssOpenMode mode, gboolean check)
{
  static gboolean init = FALSE;
  gint openmode = (mode == GST_OSS_MODE_WRITE) ? O_WRONLY : O_RDONLY;
  gboolean do_mixer = (mode == GST_OSS_MODE_VOLUME);

  if (!init && !check) {
#define MIXER 0
#define DSP   1
    gchar *dev_base[][2] = { {"/dev/mixer", "/dev/dsp"}
    ,
    {"/dev/sound/mixer", "/dev/sound/dsp"}
    ,
    {NULL, NULL}
    };
    gint n;
    gint base;

    while (device_combinations) {
      GList *item = device_combinations;
      GstOssDeviceCombination *combi = item->data;

      device_combinations = g_list_remove (device_combinations, item);

      g_free (combi->dsp);
      g_free (combi->mixer);
      g_free (combi);
    }

    /* probe for all /dev entries */
    for (base = 0; dev_base[base][DSP] != NULL; base++) {
      gint fd;

      for (n = -1; n < MAX_OSS_DEVICES; n++) {
        gchar *dsp = NULL;
        gchar *mixer = NULL;
        dev_t dsp_dev;
        dev_t mixer_dev;

        gst_oss_do_probe (dev_base[base][DSP], n, &dsp, &dsp_dev);
        if (dsp == NULL) {
          continue;
        }
        gst_oss_do_probe (dev_base[base][MIXER], n, &mixer, &mixer_dev);
        /* does the device exist (can we open them)? */

        /* we just check the dsp. we assume the mixer always works.
         * we don't need a mixer anyway (says OSS)... If we are a
         * mixer element, we use the mixer anyway. */
        if ((fd = open (do_mixer ? mixer :
                    dsp, openmode | O_NONBLOCK)) > 0 || errno == EBUSY) {
          GstOssDeviceCombination *combi;

          if (fd > 0)
            close (fd);

          /* yay! \o/ */
          combi = g_new0 (GstOssDeviceCombination, 1);
          combi->dsp = dsp;
          combi->mixer = mixer;
          combi->dev = do_mixer ? mixer_dev : dsp_dev;
          device_combinations = device_combination_append (device_combinations,
              combi);
        } else {
          g_free (dsp);
          g_free (mixer);
        }
      }
    }

    init = TRUE;
  }

  return init;
}

static GValueArray *
gst_oss_probe_list_devices (void)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!device_combinations)
    return NULL;

  array = g_value_array_new (g_list_length (device_combinations));
  item = device_combinations;
  g_value_init (&value, G_TYPE_STRING);
  while (item) {
    GstOssDeviceCombination *combi = item->data;

    g_value_set_string (&value, combi->dsp);
    g_value_array_append (array, &value);

    item = item->next;
  }
  g_value_unset (&value);

  return array;
}

static void
gst_oss_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstOssDevice *oss = g_object_get_data (G_OBJECT (probe), "oss-data");

  switch (prop_id) {
    case OSS_ARG_DEVICE:
      gst_oss_probe_devices (oss ? oss->mode : GST_OSS_MODE_READ, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_oss_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstOssDevice *oss = g_object_get_data (G_OBJECT (probe), "oss-data");
  gboolean ret = FALSE;

  switch (prop_id) {
    case OSS_ARG_DEVICE:
      ret = !gst_oss_probe_devices (oss ? oss->mode : GST_OSS_MODE_READ, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static GValueArray *
gst_oss_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GValueArray *array = NULL;

  switch (prop_id) {
    case OSS_ARG_DEVICE:
      array = gst_oss_probe_list_devices ();
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_ossprobe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_oss_probe_get_properties;
  iface->probe_property = gst_oss_probe_property;
  iface->needs_probe = gst_oss_needs_probe;
  iface->get_values = gst_oss_probe_get_values;
}

void
gst_oss_init (GObject * obj, GstOssDeviceCombination * c, GstOssDevice * oss,
    GstOssOpenMode mode)
{
  g_object_set_data (obj, "oss-data", oss);

  c->dsp = g_strdup ("/dev/dsp");
  c->mixer = g_strdup ("/dev/mixer");

  oss->fd = -1;
  oss->mixer_fd = -1;
  oss->mode = mode;

  gst_oss_reset (oss);
}

void
gst_oss_dispose (GstOssDeviceCombination * c, GstOssDevice * oss)
{
  g_free (c->dsp);
  c->dsp = NULL;
  g_free (c->mixer);
  c->mixer = NULL;
}

void
gst_oss_reset (GstOssDevice * oss)
{
  oss->law = 0;
  oss->endianness = G_BYTE_ORDER;
  oss->sign = TRUE;
  oss->width = 16;
  oss->depth = 16;
  oss->channels = 2;
  oss->rate = 44100;
  oss->fragment = 0;
  oss->bps = 0;
  oss->sample_width = 0;

/* AFMT_*_BE not available on all OSS includes (e.g. FBSD) */
#ifdef WORDS_BIGENDIAN
  oss->format = AFMT_S16_BE;
#else
  oss->format = AFMT_S16_LE;
#endif /* WORDS_BIGENDIAN */
}

static gboolean
gst_ossformat_get (gint law, gint endianness, gboolean sign, gint width,
    gint depth, gint * format, gint * bps)
{
  if (width != depth)
    return FALSE;

  *bps = 1;

  if (law == 0) {
    if (width == 16) {
      if (sign == TRUE) {
        if (endianness == G_LITTLE_ENDIAN) {
          *format = AFMT_S16_LE;
          GST_DEBUG ("16 bit signed LE, no law (%d)", *format);
        } else if (endianness == G_BIG_ENDIAN) {
          *format = AFMT_S16_BE;
          GST_DEBUG ("16 bit signed BE, no law (%d)", *format);
        }
      } else {
        if (endianness == G_LITTLE_ENDIAN) {
          *format = AFMT_U16_LE;
          GST_DEBUG ("16 bit unsigned LE, no law (%d)", *format);
        } else if (endianness == G_BIG_ENDIAN) {
          *format = AFMT_U16_BE;
          GST_DEBUG ("16 bit unsigned BE, no law (%d)", *format);
        }
      }
      *bps = 2;
    } else if (width == 8) {
      if (sign == TRUE) {
        *format = AFMT_S8;
        GST_DEBUG ("8 bit signed, no law (%d)", *format);
      } else {
        *format = AFMT_U8;
        GST_DEBUG ("8 bit unsigned, no law (%d)", *format);
      }
      *bps = 1;
    }
  } else if (law == 1) {
    *format = AFMT_MU_LAW;
    GST_DEBUG ("mu law (%d)", *format);
  } else if (law == 2) {
    *format = AFMT_A_LAW;
    GST_DEBUG ("a law (%d)", *format);
  } else {
    g_critical ("unknown law");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_oss_parse_caps (GstOssDevice * oss, const GstCaps * caps)
{
  gint bps, format;
  GstStructure *structure;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  res = gst_structure_get_int (structure, "width", &oss->width);
  res &= gst_structure_get_int (structure, "depth", &oss->depth);

  if (!res || oss->width != oss->depth)
    return FALSE;

  res = gst_structure_get_int (structure, "law", &oss->law);
  res &= gst_structure_get_int (structure, "endianness", &oss->endianness);
  res &= gst_structure_get_boolean (structure, "signed", &oss->sign);

  if (!gst_ossformat_get (oss->law, oss->endianness, oss->sign,
          oss->width, oss->depth, &format, &bps)) {
    GST_DEBUG ("could not get format");
    return FALSE;
  }

  gst_structure_get_int (structure, "channels", &oss->channels);
  gst_structure_get_int (structure, "rate", &oss->rate);

  oss->sample_width = bps * oss->channels;
  oss->bps = bps * oss->channels * oss->rate;
  oss->format = format;

  return TRUE;
}

#define GET_FIXED_INT(caps, name, dest)         \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_structure_get_int  (structure, name, dest);        \
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest)     \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_structure_get_boolean  (structure, name, dest);    \
} G_STMT_END

gboolean
gst_oss_merge_fixed_caps (GstOssDevice * oss, GstCaps * caps)
{
  gint bps, format;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  /* peel off fixed stuff from the caps */
  gst_structure_get_int (structure, "law", &oss->law);
  gst_structure_get_int (structure, "endianness", &oss->endianness);
  gst_structure_get_boolean (structure, "signed", &oss->sign);
  gst_structure_get_int (structure, "width", &oss->width);
  gst_structure_get_int (structure, "depth", &oss->depth);

  if (!gst_ossformat_get (oss->law, oss->endianness, oss->sign,
          oss->width, oss->depth, &format, &bps)) {
    return FALSE;
  }

  gst_structure_get_int (structure, "rate", &oss->rate);
  gst_structure_get_int (structure, "channels", &oss->channels);

  oss->bps = bps * oss->channels * oss->rate;
  oss->format = format;

  return TRUE;
}

gboolean
gst_oss_sync_parms (GstOssDevice * oss)
{
  audio_buf_info space;
  int frag;
  gint target_format;
  gint target_channels;
  gint target_rate;

  /* gint fragscale, frag_ln; */

  if (oss->fd == -1)
    return FALSE;

  if ((oss->fragment & 0xFFFF) == 0) {
    frag = 0;
  } else if (oss->fragment >> 16) {
    frag = oss->fragment;
  } else {
    frag = 0x7FFF0000 | oss->fragment;
  }

  GST_INFO
      ("oss: setting sound card to %dHz %d format %s (%08x fragment)",
      oss->rate, oss->format, (oss->channels == 2) ? "stereo" : "mono", frag);

  if (frag)
    ioctl (oss->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl (oss->fd, SNDCTL_DSP_RESET, 0);

  target_format = oss->format;
  target_channels = oss->channels;
  target_rate = oss->rate;

  ioctl (oss->fd, SNDCTL_DSP_SETFMT, &oss->format);
  ioctl (oss->fd, SNDCTL_DSP_CHANNELS, &oss->channels);
  ioctl (oss->fd, SNDCTL_DSP_SPEED, &oss->rate);

  ioctl (oss->fd, SNDCTL_DSP_GETBLKSIZE, &oss->fragment_size);

  if (oss->mode == GST_OSS_MODE_WRITE) {
    ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &space);
  } else {
    ioctl (oss->fd, SNDCTL_DSP_GETISPACE, &space);
  }

#if 0
  /* FIXME: make the current fragment info available somehow
   * the current way overrides preset values and that sucks */
  /* calculate new fragment using a poor man's logarithm function */
  fragscale = 1;
  frag_ln = 0;
  while (fragscale < space.fragsize) {
    fragscale <<= 1;
    frag_ln++;
  }
  oss->fragment = space.fragstotal << 16 | frag_ln;
#endif

  GST_INFO ("oss: set sound card to %dHz, %d format, %s "
      "(%d bytes buffer, %08x fragment)",
      oss->rate, oss->format,
      (oss->channels == 2) ? "stereo" : "mono", space.bytes, oss->fragment);

  oss->fragment_time = (GST_SECOND * oss->fragment_size) / oss->bps;
  GST_INFO ("fragment time %u %" G_GUINT64_FORMAT,
      oss->bps, oss->fragment_time);

  if (target_format != oss->format ||
      target_channels != oss->channels || target_rate != oss->rate) {
    if (target_channels != oss->channels)
      g_warning
          ("couldn't set the right number of channels (wanted %d, got %d), enjoy the tone difference",
          target_channels, oss->channels);
    if (target_rate < oss->rate - 1 || target_rate > oss->rate + 1)
      g_warning
          ("couldn't set the right sample rate (wanted %d, got %d), enjoy the speed difference",
          target_rate, oss->rate);
    if (target_format != oss->format)
      g_warning ("couldn't set requested OSS format, enjoy the noise :)");
    /* we could eventually return FALSE here, or just do some additional tests
     * to see that the frequencies don't differ too much etc.. */
  }
  return TRUE;
}

gboolean
gst_oss_open (GstElement * element,
    GstOssDeviceCombination * c, GstOssDevice * oss)
{
  gint caps;

  g_return_val_if_fail (oss->fd == -1, FALSE);
  GST_INFO ("oss: attempting to open sound device");

  /* first try to open the sound card */
  if (oss->mode == GST_OSS_MODE_VOLUME) {
    goto do_mixer;
  } else if (oss->mode == GST_OSS_MODE_WRITE) {
    /* open non blocking first so that it returns immediatly with an error
     * when we cannot get to the device */
    oss->fd = open (c->dsp, O_WRONLY | O_NONBLOCK);

    if (oss->fd >= 0) {
      close (oss->fd);

      /* re-open the sound device in blocking mode */
      oss->fd = open (c->dsp, O_WRONLY);
    }
  } else {
    oss->fd = open (c->dsp, O_RDONLY);
  }

  if (oss->fd < 0) {
    switch (errno) {
      case EBUSY:
        GST_ELEMENT_ERROR (element, RESOURCE, BUSY,
            (_("OSS device \"%s\" is already in use by another program."),
                c->dsp), (NULL));
        break;
      case EACCES:
      case ETXTBSY:
        if (oss->mode == GST_OSS_MODE_WRITE)
          GST_ELEMENT_ERROR (element, RESOURCE, OPEN_WRITE,
              (_("Could not access device \"%s\", check its permissions."),
                  c->dsp), GST_ERROR_SYSTEM);
        else
          GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ,
              (_("Could not access device \"%s\", check its permissions."),
                  c->dsp), GST_ERROR_SYSTEM);
        break;
      case ENXIO:
      case ENODEV:
      case ENOENT:
        GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
            (_("Device \"%s\" does not exist."), c->dsp), GST_ERROR_SYSTEM);
        break;
      default:
        if (oss->mode == GST_OSS_MODE_WRITE)
          GST_ELEMENT_ERROR (element, RESOURCE, OPEN_WRITE,
              (_("Could not open device \"%s\" for writing."), c->dsp),
              GST_ERROR_SYSTEM);
        else
          GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ,
              (_("Could not open device \"%s\" for reading."), c->dsp),
              GST_ERROR_SYSTEM);
        break;
    }
    return FALSE;
  }

  /* we have it, set the default parameters and go have fun */
  /* set card state */
  ioctl (oss->fd, SNDCTL_DSP_GETCAPS, &caps);

  GST_INFO ("oss: Capabilities %08x", caps);

  if (caps & DSP_CAP_DUPLEX)
    GST_INFO ("oss:   Full duplex");
  if (caps & DSP_CAP_REALTIME)
    GST_INFO ("oss:   Realtime");
  if (caps & DSP_CAP_BATCH)
    GST_INFO ("oss:   Batch");
  if (caps & DSP_CAP_COPROC)
    GST_INFO ("oss:   Has coprocessor");
  if (caps & DSP_CAP_TRIGGER)
    GST_INFO ("oss:   Trigger");
  if (caps & DSP_CAP_MMAP)
    GST_INFO ("oss:   Direct access");

#ifdef DSP_CAP_MULTI
  if (caps & DSP_CAP_MULTI)
    GST_INFO ("oss:   Multiple open");
#endif /* DSP_CAP_MULTI */

#ifdef DSP_CAP_BIND
  if (caps & DSP_CAP_BIND)
    GST_INFO ("oss:   Channel binding");
#endif /* DSP_CAP_BIND */

  ioctl (oss->fd, SNDCTL_DSP_GETFMTS, &caps);

  GST_INFO ("oss: Formats %08x", caps);
  if (caps & AFMT_MU_LAW)
    GST_INFO ("oss:   MU_LAW");
  if (caps & AFMT_A_LAW)
    GST_INFO ("oss:   A_LAW");
  if (caps & AFMT_IMA_ADPCM)
    GST_INFO ("oss:   IMA_ADPCM");
  if (caps & AFMT_U8)
    GST_INFO ("oss:   U8");
  if (caps & AFMT_S16_LE)
    GST_INFO ("oss:   S16_LE");
  if (caps & AFMT_S16_BE)
    GST_INFO ("oss:   S16_BE");
  if (caps & AFMT_S8)
    GST_INFO ("oss:   S8");
  if (caps & AFMT_U16_LE)
    GST_INFO ("oss:   U16_LE");
  if (caps & AFMT_U16_BE)
    GST_INFO ("oss:   U16_BE");
  if (caps & AFMT_MPEG)
    GST_INFO ("oss:   MPEG");
#ifdef AFMT_AC3
  if (caps & AFMT_AC3)
    GST_INFO ("oss:   AC3");
#endif

  GST_INFO ("oss: opened audio (%s) with fd=%d", c->dsp, oss->fd);

  oss->caps = caps;

do_mixer:
  gst_ossmixer_build_list (c, oss);

  return TRUE;
}

void
gst_oss_close (GstOssDevice * oss)
{
  gst_ossmixer_free_list (oss);

  if (oss->probed_caps) {
    gst_caps_unref (oss->probed_caps);
    oss->probed_caps = NULL;
  }

  if (oss->fd < 0)
    return;

  close (oss->fd);
  oss->fd = -1;
}

gboolean
gst_oss_convert (GstOssDevice * oss,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (oss->bps == 0 || oss->channels == 0 || oss->width == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / oss->bps;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (oss->width * oss->channels / 8);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * oss->bps / GST_SECOND;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * oss->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / oss->rate;
          break;
        case GST_FORMAT_BYTES:
          *dest_value = src_value * oss->width * oss->channels / 8;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

/* rate probing code */


#if 0

#ifdef HAVE_OSS_INCLUDE_IN_SYS
#include <sys/soundcard.h>
#else

#ifdef HAVE_OSS_INCLUDE_IN_ROOT
#include <soundcard.h>
#else

#include <machine/soundcard.h>

#endif /* HAVE_OSS_INCLUDE_IN_ROOT */

#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <glib.h>
#endif

typedef struct _GstOssProbe GstOssProbe;
struct _GstOssProbe
{
  int fd;
  int format;
  int n_channels;
  GArray *rates;
  int min;
  int max;
};

typedef struct _GstOssRange GstOssRange;
struct _GstOssRange
{
  int min;
  int max;
};

static GstStructure *gst_oss_get_format_structure (unsigned int format_bit);
static gboolean gst_oss_rate_probe_check (GstOssProbe * probe);
static int gst_oss_rate_check_rate (GstOssProbe * probe, int irate);
static void gst_oss_rate_add_range (GQueue * queue, int min, int max);
static void gst_oss_rate_add_rate (GArray * array, int rate);
static int gst_oss_rate_int_compare (gconstpointer a, gconstpointer b);

void
gst_oss_probe_caps (GstOssDevice * oss)
{
  GstOssProbe *probe;
  int i;
  gboolean ret;
  GstStructure *structure;
  unsigned int format_bit;
  unsigned int format_mask;
  GstCaps *caps;

  if (oss->probed_caps != NULL)
    return;
  if (oss->fd == -1)
    return;

  /* FIXME test make sure we're not currently playing */
  /* FIXME test both mono and stereo */

  format_mask = AFMT_U8 | AFMT_S16_LE | AFMT_S16_BE | AFMT_S8 |
      AFMT_U16_LE | AFMT_U16_BE;
  format_mask &= oss->caps;

  caps = gst_caps_new_empty ();

  /* assume that the most significant bit of format_mask is 0 */
  for (format_bit = 1; format_bit <= format_mask; format_bit <<= 1) {
    if (format_bit & format_mask) {
      GValue rate_value = { 0 };

      probe = g_new0 (GstOssProbe, 1);
      probe->fd = oss->fd;
      probe->format = format_bit;
      probe->n_channels = 2;

      ret = gst_oss_rate_probe_check (probe);
      if (probe->min == -1 || probe->max == -1) {
        g_array_free (probe->rates, TRUE);
        g_free (probe);
        continue;
      }

      if (ret) {
        GValue value = { 0 };

        g_array_sort (probe->rates, gst_oss_rate_int_compare);

        g_value_init (&rate_value, GST_TYPE_LIST);
        g_value_init (&value, G_TYPE_INT);

        for (i = 0; i < probe->rates->len; i++) {
          g_value_set_int (&value, g_array_index (probe->rates, int, i));

          gst_value_list_append_value (&rate_value, &value);
        }

        g_value_unset (&value);
      } else {
        /* one big range */
        g_value_init (&rate_value, GST_TYPE_INT_RANGE);
        gst_value_set_int_range (&rate_value, probe->min, probe->max);
      }

      g_array_free (probe->rates, TRUE);
      g_free (probe);

      structure = gst_oss_get_format_structure (format_bit);
      gst_structure_set (structure, "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
      gst_structure_set_value (structure, "rate", &rate_value);
      g_value_unset (&rate_value);

      gst_caps_append_structure (caps, structure);
    }
  }

  if (gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (oss, RESOURCE, SETTINGS,
        (_("Your OSS device could not be probed correctly")), (NULL));
    return;
  }
  GST_DEBUG ("probed caps: %" GST_PTR_FORMAT, caps);
  oss->probed_caps = caps;
}

static GstStructure *
gst_oss_get_format_structure (unsigned int format_bit)
{
  GstStructure *structure;
  int endianness;
  gboolean sign;
  int width;

  switch (format_bit) {
    case AFMT_U8:
      endianness = 0;
      sign = FALSE;
      width = 8;
      break;
    case AFMT_S16_LE:
      endianness = G_LITTLE_ENDIAN;
      sign = TRUE;
      width = 16;
      break;
    case AFMT_S16_BE:
      endianness = G_BIG_ENDIAN;
      sign = TRUE;
      width = 16;
      break;
    case AFMT_S8:
      endianness = 0;
      sign = TRUE;
      width = 8;
      break;
    case AFMT_U16_LE:
      endianness = G_LITTLE_ENDIAN;
      sign = FALSE;
      width = 16;
      break;
    case AFMT_U16_BE:
      endianness = G_BIG_ENDIAN;
      sign = FALSE;
      width = 16;
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }

  structure = gst_structure_new ("audio/x-raw-int",
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, width, "signed", G_TYPE_BOOLEAN, sign, NULL);

  if (endianness) {
    gst_structure_set (structure, "endianness", G_TYPE_INT, endianness, NULL);
  }

  return structure;
}

static gboolean
gst_oss_rate_probe_check (GstOssProbe * probe)
{
  GstOssRange *range;
  GQueue *ranges;
  int exact_rates = 0;
  gboolean checking_exact_rates = TRUE;
  int n_checks = 0;
  gboolean result = TRUE;

  ranges = g_queue_new ();

  probe->rates = g_array_new (FALSE, FALSE, sizeof (int));

  probe->min = gst_oss_rate_check_rate (probe, 1000);
  n_checks++;
  probe->max = gst_oss_rate_check_rate (probe, 100000);
  /* a little bug workaround */
  {
    int max;

    max = gst_oss_rate_check_rate (probe, 48000);
    if (max > probe->max) {
      GST_ERROR
          ("Driver bug recognized (driver does not round rates correctly).  Please file a bug report.");
      probe->max = max;
    }
  }
  n_checks++;
  if (probe->min == -1 || probe->max == -1) {
    /* This is a workaround for drivers that return -EINVAL (or another
     * error) for rates outside of [8000,48000].  If this fails, the
     * driver is seriously buggy, and probably doesn't work with other
     * media libraries/apps.  */
    probe->min = gst_oss_rate_check_rate (probe, 8000);
    probe->max = gst_oss_rate_check_rate (probe, 48000);
  }
  if (probe->min == -1 || probe->max == -1) {
    GST_DEBUG ("unexpected check_rate error");
    return FALSE;
  }
  gst_oss_rate_add_range (ranges, probe->min + 1, probe->max - 1);

  while ((range = g_queue_pop_head (ranges))) {
    int min1;
    int max1;
    int mid;
    int mid_ret;

    GST_DEBUG ("checking [%d,%d]", range->min, range->max);

    mid = (range->min + range->max) / 2;
    mid_ret = gst_oss_rate_check_rate (probe, mid);
    if (mid_ret == -1) {
      /* FIXME ioctl returned an error.  do something */
      GST_DEBUG ("unexpected check_rate error");
    }
    n_checks++;

    if (mid == mid_ret && checking_exact_rates) {
      int max_exact_matches = 20;

      exact_rates++;
      if (exact_rates > max_exact_matches) {
        GST_DEBUG ("got %d exact rates, assuming all are exact",
            max_exact_matches);
        result = FALSE;
        g_free (range);
        break;
      }
    } else {
      checking_exact_rates = FALSE;
    }

    /* Assume that the rate is arithmetically rounded to the nearest
     * supported rate. */
    if (mid == mid_ret) {
      min1 = mid - 1;
      max1 = mid + 1;
    } else {
      if (mid < mid_ret) {
        min1 = mid - (mid_ret - mid);
        max1 = mid_ret + 1;
      } else {
        min1 = mid_ret - 1;
        max1 = mid + (mid - mid_ret);
      }
    }

    gst_oss_rate_add_range (ranges, range->min, min1);
    gst_oss_rate_add_range (ranges, max1, range->max);

    g_free (range);
  }

  while ((range = g_queue_pop_head (ranges))) {
    g_free (range);
  }
  g_queue_free (ranges);

  return result;
}

static void
gst_oss_rate_add_range (GQueue * queue, int min, int max)
{
  if (min <= max) {
    GstOssRange *range = g_new0 (GstOssRange, 1);

    range->min = min;
    range->max = max;

    g_queue_push_tail (queue, range);
    /* push_head also works, but has different probing behavior */
    /*g_queue_push_head (queue, range); */
  }
}

static int
gst_oss_rate_check_rate (GstOssProbe * probe, int irate)
{
  int rate;
  int format;
  int n_channels;
  int ret;

  rate = irate;
  format = probe->format;
  n_channels = probe->n_channels;

  GST_LOG ("checking format %d, channels %d, rate %d",
      format, n_channels, rate);
  ret = ioctl (probe->fd, SNDCTL_DSP_SETFMT, &format);
  if (ret < 0)
    return -1;
  ret = ioctl (probe->fd, SNDCTL_DSP_CHANNELS, &n_channels);
  if (ret < 0)
    return -1;
  ret = ioctl (probe->fd, SNDCTL_DSP_SPEED, &rate);
  if (ret < 0)
    return -1;

  GST_DEBUG ("rate %d -> %d", irate, rate);

  if (rate == irate - 1 || rate == irate + 1) {
    rate = irate;
  }
  gst_oss_rate_add_rate (probe->rates, rate);
  return rate;
}

static void
gst_oss_rate_add_rate (GArray * array, int rate)
{
  int i;
  int val;

  for (i = 0; i < array->len; i++) {
    val = g_array_index (array, int, i);

    if (val == rate)
      return;
  }
  GST_DEBUG ("supported rate: %d", rate);
  g_array_append_val (array, rate);
}

static int
gst_oss_rate_int_compare (gconstpointer a, gconstpointer b)
{
  const int *va = (const int *) a;
  const int *vb = (const int *) b;

  if (*va < *vb)
    return -1;
  if (*va > *vb)
    return 1;
  return 0;
}
