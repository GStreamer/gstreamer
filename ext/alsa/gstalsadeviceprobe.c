/* Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 * Copyright (C) 2005 Tim-Philipp Müller <tim centricular net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsadeviceprobe.h"
#include "gst/interfaces/propertyprobe.h"

#define DATA_OFFSET_QUARK \
    g_quark_from_static_string ("alsa-device-probe-data-offset-quark")

#define DEVICE_PROPID_QUARK \
    g_quark_from_static_string ("alsa-device-probe-device-propid-quark")

static const GList *
gst_alsa_device_property_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  /* well, not perfect, but better than no locking at all.
   * In the worst case we leak a list node, so who cares? */
  GST_CLASS_LOCK (GST_OBJECT_CLASS (klass));

  if (!list) {
    GParamSpec *pspec;

    pspec = g_object_class_find_property (klass, "device");
    list = g_list_append (NULL, pspec);
  }

  GST_CLASS_UNLOCK (GST_OBJECT_CLASS (klass));

  return list;
}

/* yes, this is evil, but hey, it works */

static gboolean
gst_alsa_device_probe_get_data (GObject * obj, GstAlsaDeviceProbeData ** p_data,
    guint * p_device_propid)
{
  gpointer klass;
  guint devpropid;
  guint offset;
  GType type;

  type = G_TYPE_FROM_INSTANCE (obj);

  /* in case this is a derived class ... */
  while (g_type_get_qdata (type, DATA_OFFSET_QUARK) == NULL) {
    type = g_type_parent (type);
    g_return_val_if_fail (G_TYPE_IS_FUNDAMENTAL (type) == FALSE, FALSE);
  }

  offset = GPOINTER_TO_UINT (g_type_get_qdata (type, DATA_OFFSET_QUARK));
  devpropid = GPOINTER_TO_UINT (g_type_get_qdata (type, DEVICE_PROPID_QUARK));

  g_return_val_if_fail (offset != 0, FALSE);
  g_return_val_if_fail (devpropid != 0, FALSE);

  klass = G_OBJECT_GET_CLASS (obj);

  *p_data = (GstAlsaDeviceProbeData *) G_STRUCT_MEMBER_P (klass, offset);
  *p_device_propid = devpropid;

  return TRUE;
}

static void
gst_alsa_add_device_list (GstAlsaDeviceProbeData * probe_data,
    snd_pcm_stream_t stream)
{
  snd_ctl_t *handle;
  int card, err, dev;
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  gboolean mixer = (stream == -1);

  if (stream == -1)
    stream = 0;

  snd_ctl_card_info_alloca (&info);
  snd_pcm_info_alloca (&pcminfo);
  card = -1;

  if (snd_card_next (&card) < 0 || card < 0) {
    /* no soundcard found */
    return;
  }
  while (card >= 0) {
    gchar name[32];

    g_snprintf (name, sizeof (name), "hw:%d", card);
    if ((err = snd_ctl_open (&handle, name, 0)) < 0) {
      goto next_card;
    }
    if ((err = snd_ctl_card_info (handle, info)) < 0) {
      snd_ctl_close (handle);
      goto next_card;
    }

    if (mixer) {
      probe_data->devices = g_list_append (probe_data->devices,
          g_strdup (name));
    } else {
      dev = -1;
      while (1) {
        gchar *gst_device;

        snd_ctl_pcm_next_device (handle, &dev);

        if (dev < 0)
          break;
        snd_pcm_info_set_device (pcminfo, dev);
        snd_pcm_info_set_subdevice (pcminfo, 0);
        snd_pcm_info_set_stream (pcminfo, stream);
        if ((err = snd_ctl_pcm_info (handle, pcminfo)) < 0) {
          continue;
        }

        gst_device = g_strdup_printf ("hw:%d,%d", card, dev);
        probe_data->devices = g_list_append (probe_data->devices, gst_device);
      }
    }
    snd_ctl_close (handle);
  next_card:
    if (snd_card_next (&card) < 0) {
      break;
    }
  }
}

static gboolean
gst_alsa_probe_devices (GstElementClass * klass,
    GstAlsaDeviceProbeData * probe_data, gboolean check)
{
  static gboolean init = FALSE;

  /* I'm pretty sure ALSA has a good way to do this. However, their cool
   * auto-generated documentation is pretty much useless if you try to
   * do function-wise look-ups. */

  if (!init && !check) {
    snd_pcm_stream_t mode = -1;
    const GList *templates;

    /* we assume one pad template at max [zero=mixer] */
    templates = gst_element_class_get_pad_template_list (klass);
    if (templates) {
      if (GST_PAD_TEMPLATE_DIRECTION (templates->data) == GST_PAD_SRC)
        mode = SND_PCM_STREAM_CAPTURE;
      else
        mode = SND_PCM_STREAM_PLAYBACK;
    }

    gst_alsa_add_device_list (probe_data, mode);

    init = TRUE;
  }

  return init;
}

static void
gst_alsa_device_property_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaDeviceProbeData *probe_data;
  guint devid;

  if (!gst_alsa_device_probe_get_data (G_OBJECT (probe), &probe_data, &devid))
    g_return_if_reached ();

  if (prop_id == devid) {
    gst_alsa_probe_devices (GST_ELEMENT_GET_CLASS (probe), probe_data, FALSE);
  } else {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
  }
}

static gboolean
gst_alsa_device_property_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaDeviceProbeData *probe_data;
  GstElementClass *klass;
  guint devid;

  if (!gst_alsa_device_probe_get_data (G_OBJECT (probe), &probe_data, &devid))
    g_return_val_if_reached (FALSE);

  if (prop_id != devid) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
    return FALSE;
  }

  klass = GST_ELEMENT_GET_CLASS (probe);
  return !gst_alsa_probe_devices (klass, probe_data, TRUE);
}

static GValueArray *
gst_alsa_device_probe_list_devices (GstAlsaDeviceProbeData * probe_data)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!probe_data->devices)
    return NULL;

  array = g_value_array_new (g_list_length (probe_data->devices));
  g_value_init (&value, G_TYPE_STRING);
  for (item = probe_data->devices; item != NULL; item = item->next) {
    g_value_set_string (&value, (const gchar *) item->data);
    g_value_array_append (array, &value);
  }
  g_value_unset (&value);

  return array;
}

static GValueArray *
gst_alsa_device_property_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaDeviceProbeData *probe_data;
  guint devid;

  if (!gst_alsa_device_probe_get_data (G_OBJECT (probe), &probe_data, &devid))
    g_return_val_if_reached (NULL);

  if (prop_id != devid) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
    return NULL;
  }

  return gst_alsa_device_probe_list_devices (probe_data);
}

static void
gst_alsa_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_alsa_device_property_probe_get_properties;
  iface->probe_property = gst_alsa_device_property_probe_probe_property;
  iface->needs_probe = gst_alsa_device_property_probe_needs_probe;
  iface->get_values = gst_alsa_device_property_probe_get_values;
}

void
gst_alsa_type_add_device_property_probe_interface (GType type,
    guint probe_data_klass_offset, guint device_prop_id)
{
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_alsa_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_assert (probe_data_klass_offset != 0);
  g_assert (device_prop_id != 0);

  g_type_set_qdata (type, DATA_OFFSET_QUARK,
      GUINT_TO_POINTER (probe_data_klass_offset));

  g_type_set_qdata (type, DEVICE_PROPID_QUARK,
      GUINT_TO_POINTER (device_prop_id));

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}
