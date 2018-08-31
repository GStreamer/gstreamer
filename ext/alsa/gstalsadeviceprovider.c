/* GStreamer
 * Copyright (C) 2018 Thibault Saunier <tsaunier@igalia.com>
 *
 * alsadeviceprovider.c: alsa device probing and monitoring
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

#include "gstalsadeviceprovider.h"
#include <string.h>
#include <gst/gst.h>


static GstDevice *gst_alsa_device_new (const gchar * device_name,
    GstCaps * caps, const gchar * internal_name, snd_pcm_stream_t stream,
    GstStructure * properties);

G_DEFINE_TYPE (GstAlsaDeviceProvider, gst_alsa_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static void gst_alsa_device_provider_finalize (GObject * object);
static void gst_alsa_device_provider_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_alsa_device_provider_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static GList *gst_alsa_device_provider_probe (GstDeviceProvider * provider);

static gboolean
gst_alsa_device_provider_start (GstDeviceProvider * provider)
{
  g_list_free_full (gst_alsa_device_provider_probe (provider),
      gst_object_unref);
  return TRUE;
}

static void
gst_alsa_device_provider_stop (GstDeviceProvider * provider)
{
  return;
}

enum
{
  PROP_0,
  PROP_LAST
};


static void
gst_alsa_device_provider_class_init (GstAlsaDeviceProviderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);

  gobject_class->set_property = gst_alsa_device_provider_set_property;
  gobject_class->get_property = gst_alsa_device_provider_get_property;
  gobject_class->finalize = gst_alsa_device_provider_finalize;

  dm_class->probe = gst_alsa_device_provider_probe;
  dm_class->start = gst_alsa_device_provider_start;
  dm_class->stop = gst_alsa_device_provider_stop;

  gst_device_provider_class_set_static_metadata (dm_class,
      "Alsa Device Provider", "Sink/Source/Audio",
      "List and provider Alsa source and sink devices",
      "Thibault Saunier <tsaunier@igali.com>");
}

static void
gst_alsa_device_provider_init (GstAlsaDeviceProvider * self)
{
}

static void
gst_alsa_device_provider_finalize (GObject * object)
{
  // GstAlsaDeviceProvider *self = GST_ALSA_DEVICE_PROVIDER (object);

  G_OBJECT_CLASS (gst_alsa_device_provider_parent_class)->finalize (object);
}


static void
gst_alsa_device_provider_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  // GstAlsaDeviceProvider *self = GST_ALSA_DEVICE_PROVIDER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_alsa_device_provider_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  // GstAlsaDeviceProvider *self = GST_ALSA_DEVICE_PROVIDER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstDevice *
add_device (GstDeviceProvider * provider, snd_ctl_t * info,
    snd_pcm_stream_t stream, gchar * internal_name)
{
  GstCaps *caps;
  GstDevice *dev;

  caps = gst_caps_new_simple ("audio/x-raw", NULL, NULL);

  dev = gst_alsa_device_new ("Alsa device",
      caps, internal_name, stream, gst_structure_new_empty ("alsa-proplist"));

  gst_device_provider_device_add (provider, gst_object_ref (dev));

  return dev;
}

static GList *
gst_alsa_device_provider_probe (GstDeviceProvider * provider)
{
  snd_ctl_t *handle;
  int card, dev;
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  GList *list = NULL;
  gint i;
  gint streams[] = { SND_PCM_STREAM_CAPTURE, SND_PCM_STREAM_PLAYBACK };
  snd_pcm_stream_t stream;

  GST_INFO_OBJECT (provider, "Probing alsa devices");
  snd_ctl_card_info_malloc (&info);
  snd_pcm_info_malloc (&pcminfo);
  card = -1;

  if (snd_card_next (&card) < 0 || card < 0) {
    /* no soundcard found */
    GST_WARNING ("No soundcard found");
    goto beach;
  }

  for (i = 0; i < G_N_ELEMENTS (streams); i++) {
    stream = streams[i];

    while (card >= 0) {
      gchar name[32];

      g_snprintf (name, sizeof (name), "hw:%d", card);
      if (snd_ctl_open (&handle, name, 0) < 0) {
        goto next_card;
      }
      if (snd_ctl_card_info (handle, info) < 0) {
        snd_ctl_close (handle);
        goto next_card;
      }

      dev = -1;
      while (1) {
        gchar *gst_device;

        snd_ctl_pcm_next_device (handle, &dev);

        if (dev < 0)
          break;
        snd_pcm_info_set_device (pcminfo, dev);
        snd_pcm_info_set_subdevice (pcminfo, 0);
        snd_pcm_info_set_stream (pcminfo, stream);
        if (snd_ctl_pcm_info (handle, pcminfo) < 0) {
          continue;
        }

        gst_device = g_strdup_printf ("hw:%d,%d", card, dev);
        list =
            g_list_prepend (list, add_device (provider, handle, stream,
                gst_device));
      }
      snd_ctl_close (handle);
    next_card:
      if (snd_card_next (&card) < 0) {
        break;
      }
    }
  }

beach:
  snd_ctl_card_info_free (info);
  snd_pcm_info_free (pcminfo);

  return list;
}

enum
{
  PROP_INTERNAL_NAME = 1,
};


G_DEFINE_TYPE (GstAlsaDevice, gst_alsa_device, GST_TYPE_DEVICE);

static void gst_alsa_device_finalize (GObject * object);
static GstElement *gst_alsa_device_create_element (GstDevice * device,
    const gchar * name);
static gboolean gst_alsa_device_reconfigure_element (GstDevice * device,
    GstElement * element);

static void
gst_alsa_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAlsaDevice *device;

  device = GST_ALSA_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_INTERNAL_NAME:
      g_value_set_string (value, device->internal_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_alsa_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAlsaDevice *device;

  device = GST_ALSA_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_INTERNAL_NAME:
      device->internal_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_alsa_device_class_init (GstAlsaDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_alsa_device_create_element;
  dev_class->reconfigure_element = gst_alsa_device_reconfigure_element;

  object_class->get_property = gst_alsa_device_get_property;
  object_class->set_property = gst_alsa_device_set_property;
  object_class->finalize = gst_alsa_device_finalize;

  g_object_class_install_property (object_class, PROP_INTERNAL_NAME,
      g_param_spec_string ("internal-name", "Internal AlsaAudio device name",
          "The internal name of the AlsaAudio device", "",
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_alsa_device_finalize (GObject * object)
{
  GstAlsaDevice *device = GST_ALSA_DEVICE (object);

  g_free (device->internal_name);

  G_OBJECT_CLASS (gst_alsa_device_parent_class)->finalize (object);
}

static void
gst_alsa_device_init (GstAlsaDevice * device)
{
}

static GstElement *
gst_alsa_device_create_element (GstDevice * device, const gchar * name)
{
  GstAlsaDevice *alsa_dev = GST_ALSA_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (alsa_dev->element, name);
  g_object_set (elem, "device", alsa_dev->internal_name, NULL);

  return elem;
}

static gboolean
gst_alsa_device_reconfigure_element (GstDevice * device, GstElement * element)
{
  GstAlsaDevice *alsa_dev = GST_ALSA_DEVICE (device);

  g_object_set (element, "device", alsa_dev->internal_name, NULL);

  return TRUE;
}

/* Takes ownership of @caps and @props */
static GstDevice *
gst_alsa_device_new (const gchar * device_name,
    GstCaps * caps, const gchar * internal_name, snd_pcm_stream_t stream,
    GstStructure * props)
{
  GstAlsaDevice *gstdev;
  const gchar *element = NULL;
  const gchar *klass = NULL;

  g_return_val_if_fail (device_name, NULL);
  g_return_val_if_fail (internal_name, NULL);
  g_return_val_if_fail (caps, NULL);

  switch (stream) {
    case SND_PCM_STREAM_CAPTURE:
      element = "alsasrc";
      klass = "Audio/Source";
      break;
    case SND_PCM_STREAM_PLAYBACK:
      element = "alsasink";
      klass = "Audio/Sink";
      break;
    default:
      g_assert_not_reached ();
      break;
  }


  gstdev = g_object_new (GST_TYPE_ALSA_DEVICE,
      "display-name", device_name, "caps", caps, "device-class", klass,
      "internal-name", internal_name, "properties", props, NULL);

  gstdev->stream = stream;
  gstdev->element = element;

  gst_structure_free (props);
  gst_caps_unref (caps);

  return GST_DEVICE (gstdev);
}
