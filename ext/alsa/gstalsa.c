/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include <sys/time.h>

#include "gst/gst-i18n-plugin.h"
#include "gst/propertyprobe/propertyprobe.h"
#include "gstalsa.h"
#include "gstalsaclock.h"
#include "gstalsamixer.h"

#define ALSA_DEBUG_FLUSH(this) G_STMT_START{ \
  gchar *__str; \
  ssize_t __size; \
  __size = snd_output_buffer_string (this->out, &__str); \
  if (__size > 0) { \
    GST_DEBUG_OBJECT (this, "%*s", __size, __str); \
    if (snd_output_flush (this->out) != 0) \
      GST_ERROR_OBJECT (this, "error flushing output buffer"); \
  } \
}G_STMT_END

/* GObject functions */
static void gst_alsa_class_init (gpointer g_class, gpointer class_data);
static void gst_alsa_init (GstAlsa * this);
static void gst_alsa_dispose (GObject * object);
static void gst_alsa_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_alsa_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* interface */
static void gst_alsa_probe_interface_init (GstPropertyProbeInterface * iface);

/* GStreamer functions for pads and state changing */
static GstPad *gst_alsa_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstElementStateReturn gst_alsa_change_state (GstElement * element);
static GstClock *gst_alsa_get_clock (GstElement * element);
static void gst_alsa_set_clock (GstElement * element, GstClock * clock);

/* ALSA setup / start / stop functions */
static gboolean gst_alsa_probe_hw_params (GstAlsa * this,
    GstAlsaFormat * format);
static gboolean gst_alsa_set_hw_params (GstAlsa * this);
static gboolean gst_alsa_set_sw_params (GstAlsa * this);

static gboolean gst_alsa_open_audio (GstAlsa * this);
static gboolean gst_alsa_start_audio (GstAlsa * this);
static gboolean gst_alsa_drain_audio (GstAlsa * this);
static gboolean gst_alsa_stop_audio (GstAlsa * this);
static gboolean gst_alsa_close_audio (GstAlsa * this);

/* GStreamer querying, conversion, and format functions */
static const GstFormat *gst_alsa_get_formats (GstPad * pad);
static gboolean gst_alsa_convert (GstAlsa * this,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_alsa_pad_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static const GstQueryType *gst_alsa_get_query_types (GstPad * pad);
static gboolean gst_alsa_query_func (GstElement * element,
    GstQueryType type, GstFormat * format, gint64 * value);
static gboolean gst_alsa_query (GstElement * element,
    GstQueryType type, GstFormat * format, gint64 * value);
static gboolean gst_alsa_pad_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

/*** TYPE FUNCTIONS ***********************************************************/

GType
gst_alsa_get_type (void)
{
  static GType alsa_type = 0;

  if (!alsa_type) {
    static const GTypeInfo alsa_info = {
      sizeof (GstAlsaClass),
      NULL,
      NULL,
      gst_alsa_class_init,
      NULL,
      NULL,
      sizeof (GstAlsa),
      0,
      (GInstanceInitFunc) gst_alsa_init,
    };
    static const GInterfaceInfo alsa_probe_info = {
      (GInterfaceInitFunc) gst_alsa_probe_interface_init,
      NULL,
      NULL
    };

    alsa_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAlsa", &alsa_info, 0);

    g_type_add_interface_static (alsa_type,
        GST_TYPE_PROPERTY_PROBE, &alsa_probe_info);
  }

  return alsa_type;
}

/*** GOBJECT FUNCTIONS ********************************************************/

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_DEVICE_NAME,
  ARG_PERIODCOUNT,
  ARG_PERIODSIZE,
  ARG_BUFFERSIZE,
  ARG_AUTORECOVER,
  ARG_MMAP,
  ARG_MAXDISCONT
};

static GstElement *parent_class = NULL;

static void
gst_alsa_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstAlsaClass *klass;

  klass = (GstAlsaClass *) g_class;
  object_class = (GObjectClass *) g_class;
  element_class = (GstElementClass *) g_class;

  if (parent_class == NULL)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->dispose = gst_alsa_dispose;
  object_class->get_property = gst_alsa_get_property;
  object_class->set_property = gst_alsa_set_property;

  g_object_class_install_property (object_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "ALSA device, as defined in an asoundrc",
          "default", G_PARAM_READWRITE));
  g_object_class_install_property (object_class, ARG_DEVICE_NAME,
      g_param_spec_string ("device_name", "Device name",
          "Name of the device", NULL, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_PERIODCOUNT,
      g_param_spec_int ("period-count", "Period count",
          "Number of hardware buffers to use",
          2, 64, 2, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class, ARG_PERIODSIZE,
      g_param_spec_int ("period-size", "Period size",
          "Number of frames (samples on each channel) in one hardware period",
          2, 8192, 8192, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class, ARG_BUFFERSIZE,
      g_param_spec_int ("buffer-size", "Buffer size",
          "Number of frames the hardware buffer can hold",
          4, 65536, 16384, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, ARG_AUTORECOVER,
      g_param_spec_boolean ("autorecover", "Automatic xrun recovery",
          "When TRUE tries to reduce processor load on xruns",
          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class, ARG_MMAP,
      g_param_spec_boolean ("mmap", "Use mmap'ed access",
          "Wether to use mmap (faster) or standard read/write (more compatible)",
          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class, ARG_MAXDISCONT,
      g_param_spec_uint64 ("max-discont", "Maximum Discontinuity",
          "GStreamer timeunits before the timestamp syncing starts dropping/inserting samples",
          /* rounding errors */ 1000, GST_SECOND, GST_ALSA_DEFAULT_DISCONT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_alsa_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_alsa_query);
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_alsa_request_new_pad);
  element_class->set_clock = GST_DEBUG_FUNCPTR (gst_alsa_set_clock);
  element_class->get_clock = GST_DEBUG_FUNCPTR (gst_alsa_get_clock);
}

static void
gst_alsa_init (GstAlsa * this)
{
  this->device = g_strdup ("default");

  GST_FLAG_SET (this, GST_ELEMENT_EVENT_AWARE);
  GST_FLAG_SET (this, GST_ELEMENT_THREAD_SUGGESTED);
}

static void
gst_alsa_dispose (GObject * object)
{
  GstAlsa *this = GST_ALSA (object);

  g_free (this->device);

  if (this->clock)
    gst_object_unparent (GST_OBJECT (this->clock));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_alsa_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstAlsa *this;
  gint buffer_size;

  this = (GstAlsa *) object;
  switch (prop_id) {
    case ARG_DEVICE:
      if (this->device)
        g_free (this->device);
      this->device = g_strdup (g_value_get_string (value));
      break;
    case ARG_PERIODCOUNT:
      g_return_if_fail (!GST_FLAG_IS_SET (this, GST_ALSA_RUNNING));
      this->period_count = g_value_get_int (value);
      break;
    case ARG_PERIODSIZE:
      g_return_if_fail (!GST_FLAG_IS_SET (this, GST_ALSA_RUNNING));
      this->period_size = g_value_get_int (value);
      break;
    case ARG_BUFFERSIZE:
      g_return_if_fail (!GST_FLAG_IS_SET (this, GST_ALSA_RUNNING));
      buffer_size = g_value_get_int (value);
      this->period_count = buffer_size / this->period_size;
      break;
    case ARG_AUTORECOVER:
      this->autorecover = g_value_get_boolean (value);
      return;
    case ARG_MMAP:
      this->mmap = g_value_get_boolean (value);
      return;
    case ARG_MAXDISCONT:
      this->max_discont = (GstClockTime) g_value_get_uint64 (value);
      return;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }

  if (GST_STATE (this) == GST_STATE_NULL)
    return;

  if (GST_FLAG_IS_SET (this, GST_ALSA_RUNNING)) {
    gst_alsa_stop_audio (this);
    gst_alsa_start_audio (this);
  }
}

static void
gst_alsa_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAlsa *this;

  this = (GstAlsa *) object;

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, this->device);
      break;
    case ARG_DEVICE_NAME:
      g_value_set_string (value, this->cardname);
      break;
    case ARG_PERIODCOUNT:
      g_value_set_int (value, this->period_count);
      break;
    case ARG_PERIODSIZE:
      g_value_set_int (value, this->period_size);
      break;
    case ARG_BUFFERSIZE:
      g_value_set_int (value, this->period_size * this->period_count);
      break;
    case ARG_AUTORECOVER:
      g_value_set_boolean (value, this->autorecover);
      break;
    case ARG_MMAP:
      g_value_set_boolean (value, this->mmap);
      break;
    case ARG_MAXDISCONT:
      g_value_set_uint64 (value, (guint64) this->max_discont);
      return;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const GList *
gst_alsa_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
  }

  return list;
}

static void
device_list (snd_pcm_stream_t stream, GstAlsaClass * klass)
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
    char name[32];

    sprintf (name, "hw:%d", card);
    if ((err = snd_ctl_open (&handle, name, 0)) < 0) {
      goto next_card;
    }
    if ((err = snd_ctl_card_info (handle, info)) < 0) {
      snd_ctl_close (handle);
      goto next_card;
    }

    if (mixer) {
      klass->devices = g_list_append (klass->devices, g_strdup (name));
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
        klass->devices = g_list_append (klass->devices, gst_device);
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
gst_alsa_class_probe_devices (GstAlsaClass * klass, gboolean check)
{
  static gboolean init = FALSE;

  /* I'm pretty sure ALSA has a good way to do this. However, their cool
   * auto-generated documentation is pretty much useless if you try to
   * do function-wise look-ups. */

  if (!init && !check) {
    snd_pcm_stream_t mode = -1;
    const GList *templates;

    /* we assume one pad template at max [zero=mixer] */
    templates =
        gst_element_class_get_pad_template_list (GST_ELEMENT_CLASS (klass));
    if (templates) {
      if (GST_PAD_TEMPLATE_DIRECTION (templates->data) == GST_PAD_SRC)
        mode = SND_PCM_STREAM_CAPTURE;
      else
        mode = SND_PCM_STREAM_PLAYBACK;
    }

    device_list (mode, klass);

    init = TRUE;
  }

  return init;
}

static GValueArray *
gst_alsa_class_list_devices (GstAlsaClass * klass)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (!klass->devices)
    return NULL;

  array = g_value_array_new (g_list_length (klass->devices));
  g_value_init (&value, G_TYPE_STRING);
  for (item = klass->devices; item != NULL; item = item->next) {
    g_value_set_string (&value, item->data);
    g_value_array_append (array, &value);
  }
  g_value_unset (&value);

  return array;

}

static void
gst_alsa_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaClass *klass = GST_ALSA_GET_CLASS (probe);

  switch (prop_id) {
    case ARG_DEVICE:
      gst_alsa_class_probe_devices (klass, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_alsa_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaClass *klass = GST_ALSA_GET_CLASS (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case ARG_DEVICE:
      ret = !gst_alsa_class_probe_devices (klass, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static GValueArray *
gst_alsa_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAlsaClass *klass = GST_ALSA_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case ARG_DEVICE:
      array = gst_alsa_class_list_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_alsa_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_alsa_probe_get_properties;
  iface->probe_property = gst_alsa_probe_probe_property;
  iface->needs_probe = gst_alsa_probe_needs_probe;
  iface->get_values = gst_alsa_probe_get_values;
}

/*** GSTREAMER PAD / QUERY / CONVERSION / STATE FUNCTIONS *********************/

static GstPad *
gst_alsa_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstAlsa *this;
  gint track = 0;

  g_return_val_if_fail ((this = GST_ALSA (element)), NULL);
  g_return_val_if_fail (!GST_FLAG_IS_SET (element, GST_ALSA_RUNNING), NULL);

  if (name) {
    /* locate the track number in the requested pad name. */
    track = (gint) strtol (name + (strchr (templ->name_template, '%') -
            templ->name_template), NULL, 0);
    if (track < 1 || track >= GST_ALSA_MAX_TRACKS) {
      GST_INFO_OBJECT (this, "invalid track requested. (%d)", track);
      return NULL;
    }
  }

  /* make sure the requested track is free. */
  if (track > 0 || this->pad[track] != NULL) {
    GST_INFO_OBJECT (this, "requested track %d already in use.", track);
    return NULL;
  }

  /* if the user doesn't care, use the lowest available track number */
  if (track == 0) {
    for (track = 1; track < GST_ALSA_MAX_TRACKS; track++) {
      if (this->pad[track] != NULL)
        goto found_track;
    }
    return NULL;
  }

found_track:
  this->pad[track] = gst_pad_new_from_template (templ, name);

  gst_pad_set_link_function (this->pad[track], gst_alsa_link);
  gst_pad_set_getcaps_function (this->pad[track], gst_alsa_get_caps);
  gst_pad_set_fixate_function (this->pad[track], gst_alsa_fixate);

  gst_element_add_pad (GST_ELEMENT (this), this->pad[track]);

  gst_pad_set_convert_function (this->pad[track], gst_alsa_pad_convert);
  gst_pad_set_query_function (this->pad[track], gst_alsa_pad_query);
  gst_pad_set_query_type_function (this->pad[track], gst_alsa_get_query_types);
  gst_pad_set_formats_function (this->pad[track], gst_alsa_get_formats);

  return this->pad[track];
}

/* gets the matching alsa format or NULL if none matches */
static GstAlsaFormat *
gst_alsa_get_format (const GstStructure * structure)
{
  const gchar *mimetype;
  GstAlsaFormat *ret;

  if (!(ret = g_new (GstAlsaFormat, 1)))
    return NULL;

  /* we have to differentiate between int and float formats */
  mimetype = gst_structure_get_name (structure);

  if (!strncmp (mimetype, "audio/x-raw-int", 15)) {
    gboolean sign;
    gint width, depth, endianness;

    /* extract the needed information from the cap */
    if (!(gst_structure_get_int (structure, "width", &width) &&
            gst_structure_get_int (structure, "depth", &depth) &&
            gst_structure_get_boolean (structure, "signed", &sign)))
      goto error;

    /* extract endianness if needed */
    if (width > 8) {
      if (!gst_structure_get_int (structure, "endianness", &endianness))
        goto error;
    } else {
      endianness = G_BYTE_ORDER;
    }

    ret->format =
        snd_pcm_build_linear_format (depth, width, sign ? 0 : 1,
        endianness == G_LITTLE_ENDIAN ? 0 : 1);

  } else if (!strncmp (mimetype, "audio/x-raw-float", 17)) {
    gint width;

    /* get layout */
    if (!gst_structure_get_int (structure, "width", &width))
      goto error;

    /* match layout to format wrt to endianness */
    if (width == 32) {
      if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
        ret->format = SND_PCM_FORMAT_FLOAT_LE;
      } else if (G_BYTE_ORDER == G_BIG_ENDIAN) {
        ret->format = SND_PCM_FORMAT_FLOAT_BE;
      } else {
        ret->format = SND_PCM_FORMAT_FLOAT;
      }
    } else if (width == 64) {
      if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
        ret->format = SND_PCM_FORMAT_FLOAT64_LE;
      } else if (G_BYTE_ORDER == G_BIG_ENDIAN) {
        ret->format = SND_PCM_FORMAT_FLOAT64_BE;
      } else {
        ret->format = SND_PCM_FORMAT_FLOAT64;
      }
    } else {
      goto error;
    }
  } else if (!strncmp (mimetype, "audio/x-alaw", 12)) {
    ret->format = SND_PCM_FORMAT_A_LAW;
  } else if (!strncmp (mimetype, "audio/x-mulaw", 13)) {
    ret->format = SND_PCM_FORMAT_MU_LAW;
  }

  /* get rate and channels */
  if (!(gst_structure_get_int (structure, "rate", &ret->rate) &&
          gst_structure_get_int (structure, "channels", &ret->channels)))
    goto error;

  return ret;

error:
  g_free (ret);
  return NULL;
}

static inline gboolean
gst_alsa_formats_match (GstAlsaFormat * one, GstAlsaFormat * two)
{
  if (one == two)
    return TRUE;
  if (one == NULL || two == NULL)
    return FALSE;
  return (one->format == two->format) &&
      (one->rate == two->rate) && (one->channels == two->channels);
}

/* get props for a spec */
static GstCaps *
gst_alsa_get_caps_internal (snd_pcm_format_t format)
{
  if (format == SND_PCM_FORMAT_A_LAW) {
    return gst_caps_new_simple ("audio/x-alaw", NULL);
  } else if (format == SND_PCM_FORMAT_MU_LAW) {
    return gst_caps_new_simple ("audio/x-mulaw", NULL);
  } else if (snd_pcm_format_linear (format)) {
    /* int */
    GstStructure *structure = gst_structure_new ("audio/x-raw-int",
        "width", G_TYPE_INT, (gint) snd_pcm_format_physical_width (format),
        "depth", G_TYPE_INT, (gint) snd_pcm_format_width (format),
        "signed", G_TYPE_BOOLEAN,
        snd_pcm_format_signed (format) == 1 ? TRUE : FALSE,
        NULL);

    /* endianness */
    if (snd_pcm_format_physical_width (format) > 8) {
      switch (snd_pcm_format_little_endian (format)) {
        case 0:
          gst_structure_set (structure, "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              NULL);
          break;
        case 1:
          gst_structure_set (structure, "endianness", G_TYPE_INT,
              G_LITTLE_ENDIAN, NULL);
          break;
        default:
          GST_WARNING
              ("Unknown byte order in sound driver. Continuing by assuming system byte order.");
          gst_structure_set (structure, "endianness", G_TYPE_INT, G_BYTE_ORDER,
              NULL);
          break;
      }
    }
    return gst_caps_new_full (structure, NULL);
  } else if (snd_pcm_format_float (format)) {
    /* no float with non-platform endianness */
    if (!snd_pcm_format_cpu_endian (format))
      return NULL;

    return gst_caps_new_simple ("audio/x-raw-float",
        "width", G_TYPE_INT, (gint) snd_pcm_format_width (format),
        "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
  }
  return NULL;
}

static inline void
add_channels (GstStructure * structure, gint min_rate, gint max_rate,
    gint min_channels, gint max_channels)
{
  if (min_rate < 0) {
    min_rate = GST_ALSA_MIN_RATE;
    max_rate = GST_ALSA_MAX_RATE;
  }
  if (max_rate < 0 || min_rate == max_rate) {
    gst_structure_set (structure, "rate", G_TYPE_INT, min_rate, NULL);
  } else {
    /* just to be sure */
    if (min_rate > max_rate) {
      gint temp;

      GST_ERROR
          ("minimum rate > maximum rate (%d > %d), please fix your soundcard drivers",
          min_rate, max_rate);
      temp = min_rate;
      min_rate = max_rate;
      max_rate = temp;
    }
    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, min_rate,
        max_rate, NULL);
  }
  if (min_channels < 0) {
    min_channels = 1;
    max_channels = GST_ALSA_MAX_CHANNELS;
  }
  if (max_channels < 0 || min_channels == max_channels) {
    gst_structure_set (structure, "channels", G_TYPE_INT, min_channels, NULL);
  } else {
    /* just to be sure */
    if (min_channels > max_channels) {
      gint temp;

      GST_ERROR
          ("minimum channels > maximum channels (%d > %d), please fix your soundcard drivers",
          min_channels, max_channels);
      temp = min_channels;
      min_channels = max_channels;
      max_channels = temp;
    }
    gst_structure_set (structure, "channels", GST_TYPE_INT_RANGE,
        min_channels, max_channels, NULL);
  }
}

/**
 * Get all available caps.
 * @format: SND_PCM_FORMAT_UNKNOWN for all formats, desired format else
 * @rate: allowed rates if < 0, else desired rate
 * @channels: all allowed values for channels if < 0, else desired channels
 */
GstCaps *
gst_alsa_caps (snd_pcm_format_t format, gint rate, gint channels)
{
  GstCaps *ret_caps;

  if (format != SND_PCM_FORMAT_UNKNOWN) {
    /* there are some caps set already */
    ret_caps = gst_alsa_get_caps_internal (format);

    /* we can never use a format we can't set caps for */
    g_assert (ret_caps != NULL);
    g_assert (gst_caps_get_size (ret_caps) == 1);

    add_channels (gst_caps_get_structure (ret_caps, 0), rate, -1, channels, -1);
  } else {
    int i;
    GstCaps *temp;

    ret_caps = gst_caps_new_empty ();
    for (i = 0; i <= SND_PCM_FORMAT_LAST; i++) {
      temp = gst_alsa_get_caps_internal (i);

      /* can be NULL, because not all alsa formats can be specified as caps */
      if (temp != NULL) {
        g_assert (gst_caps_get_size (temp) == 1);
        add_channels (gst_caps_get_structure (temp, 0), rate, -1, channels, -1);
        gst_caps_append (ret_caps, temp);
      }
    }
  }

  gst_caps_do_simplify (ret_caps);
  return ret_caps;
}

/* Return better caps when device is open */
GstCaps *
gst_alsa_get_caps (GstPad * pad)
{
  GstAlsa *this;
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_format_mask_t *mask;
  int i;
  unsigned int min_rate, max_rate;
  gint min_channels, max_channels;
  GstCaps *ret = NULL;

  g_return_val_if_fail (pad != NULL, NULL);

  this = GST_ALSA (gst_pad_get_parent (pad));

  if (!GST_FLAG_IS_SET (this, GST_ALSA_OPEN))
    return gst_caps_copy (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad)));

  snd_pcm_hw_params_alloca (&hw_params);
  ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params),
      "Broken configuration for this PCM: %s");

  if (((GstElement *) this)->numpads > 1) {
    min_channels = 1;
    max_channels = -1;
  } else {
    ERROR_CHECK (snd_pcm_hw_params_get_channels_min (hw_params, &min_rate),
        "Couldn't get minimum channel count for device %s: %s", this->device);
    ERROR_CHECK (snd_pcm_hw_params_get_channels_max (hw_params, &max_rate),
        "Couldn't get maximum channel count for device %s: %s", this->device);
    min_channels = min_rate;
    max_channels =
        max_rate > GST_ALSA_MAX_CHANNELS ? GST_ALSA_MAX_CHANNELS : max_rate;
  }

  ERROR_CHECK (snd_pcm_hw_params_get_rate_min (hw_params, &min_rate, &i),
      "Couldn't get minimum rate for device %s: %s", this->device);
  min_rate = min_rate < GST_ALSA_MIN_RATE ? GST_ALSA_MIN_RATE : min_rate + i;
  ERROR_CHECK (snd_pcm_hw_params_get_rate_max (hw_params, &max_rate, &i),
      "Couldn't get maximum rate for device %s: %s", this->device);
  max_rate = max_rate > GST_ALSA_MAX_RATE ? GST_ALSA_MAX_RATE : max_rate + i;

  snd_pcm_format_mask_alloca (&mask);
  snd_pcm_hw_params_get_format_mask (hw_params, mask);
  for (i = 0; i <= SND_PCM_FORMAT_LAST; i++) {
    if (snd_pcm_format_mask_test (mask, i)) {
      GstCaps *caps = gst_alsa_get_caps_internal (i);

      /* we can never use a format we can't set caps for */
      if (caps != NULL) {
        g_assert (gst_caps_get_size (caps) == 1);
        add_channels (gst_caps_get_structure (caps, 0), min_rate, max_rate,
            min_channels, max_channels);
        if (ret) {
          gst_caps_append (ret, caps);
        } else {
          ret = caps;
        }
      }
    }
  }

  if (ret == NULL) {
    GST_WARNING_OBJECT (this, "no supported caps found, returning empty caps");
    return gst_caps_new_empty ();
  } else {
    G_GNUC_UNUSED gchar *str;

    gst_caps_do_simplify (ret);
    str = gst_caps_to_string (ret);
    GST_LOG_OBJECT (this, "get_caps returns %s", str);
    g_free (str);
    return ret;
  }
}

static GstCaps *
gst_alsa_fixate_to_mimetype (const GstCaps * caps, const gchar * mime)
{
  GstCaps *try, *result;

  try = gst_caps_new_simple (mime, NULL);
  result = gst_caps_intersect (try, caps);
  gst_caps_free (try);
  if (gst_caps_is_empty (result)) {
    gst_caps_free (result);
    return NULL;
  }
  if (gst_caps_is_subset (caps, result)) {
    /* we didn't reduce caps */
    gst_caps_free (result);
    return NULL;
  }
  return result;
}

static GstCaps *
gst_alsa_fixate_field_nearest_int (const GstCaps * caps,
    const gchar * field_name, gint target)
{
  guint i;
  GstCaps *result;
  GstCaps *smaller = gst_caps_new_empty ();
  GstCaps *equal = gst_caps_new_empty ();
  GstCaps *bigger = gst_caps_new_empty ();

  /* works like this: we fixate every structure and put them into one of those 
   * caps depending on what we fixated to. We then return the best caps that is 
   * not empty in the following order: equal, bigger, smaller 
   * We also make sure the caps were really reduced.
   */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    gint fixated_to;
    GstStructure *copy = gst_structure_copy (gst_caps_get_structure (caps, i));

    gst_caps_structure_fixate_field_nearest_int (copy, field_name, target);
    if (gst_structure_get_int (copy, field_name, &fixated_to)) {
      if (fixated_to == target) {
        gst_caps_append_structure (equal, copy);
      } else if (fixated_to > target) {
        gst_caps_append_structure (bigger, copy);
      } else {
        gst_caps_append_structure (smaller, copy);
      }
    } else {
      /* FIXME: what do we do here? Add to all or throw an error? */
      g_return_val_if_reached (NULL);
    }
  }
  if (!gst_caps_is_empty (equal)) {
    gst_caps_free (bigger);
    gst_caps_free (smaller);
    result = equal;
  } else {
    gst_caps_free (equal);
    if (!gst_caps_is_empty (bigger)) {
      gst_caps_free (smaller);
      result = bigger;
    } else {
      gst_caps_free (bigger);
      if (gst_caps_is_empty (smaller)) {
        gst_caps_free (smaller);
        return NULL;
      }
      result = smaller;
    }
  }
  if (gst_caps_is_subset (caps, result)) {
    /* we didn't reduce caps */
    gst_caps_free (result);
    return NULL;
  }
  return result;
}

GstCaps *
gst_alsa_fixate (GstPad * pad, const GstCaps * caps)
{
  GstCaps *result;
  const gchar *mime;

  if ((result = gst_alsa_fixate_to_mimetype (caps, "audio/x-raw-int")))
    return result;
  if ((result = gst_alsa_fixate_to_mimetype (caps, "audio/x-raw-float")))
    return result;
  if ((result = gst_alsa_fixate_to_mimetype (caps, "audio/x-alaw")))
    return result;
  if ((result = gst_alsa_fixate_to_mimetype (caps, "audio/x-mulaw")))
    return result;

  /* now we know there's only one mimetype in the caps */
  /* FIXME: I should check this to be really sure I didn't mess up somewhere */

  if ((result = gst_alsa_fixate_field_nearest_int (caps, "rate", 44100)))
    return result;
  if ((result = gst_alsa_fixate_field_nearest_int (caps, "channels", 2)))
    return result;

  mime = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  if (g_str_equal (mime, "audio/x-raw-int")) {
    if ((result = gst_alsa_fixate_field_nearest_int (caps, "width", 16)))
      return result;
    if ((result = gst_alsa_fixate_field_nearest_int (caps, "depth", 16)))
      return result;
  } else if (g_str_equal (mime, "audio/x-raw-float")) {
    if ((result = gst_alsa_fixate_field_nearest_int (caps, "width", 32)))
      return result;
  }

  return NULL;
}

/* Negotiates the caps */
GstPadLinkReturn
gst_alsa_link (GstPad * pad, const GstCaps * caps)
{
  GstAlsa *this;
  GstAlsaFormat *format;
  GstPadLinkReturn ret;

  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (pad != NULL, GST_PAD_LINK_REFUSED);

  this = GST_ALSA (gst_pad_get_parent (pad));

  if (this->handle == NULL)
    if (!gst_alsa_open_audio (this))
      return GST_PAD_LINK_REFUSED;

  format = gst_alsa_get_format (gst_caps_get_structure (caps, 0));
  if (format == NULL)
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG ("found format %s", snd_pcm_format_name (format->format));

  if (!GST_FLAG_IS_SET (this, GST_ALSA_CAPS_NEGO)) {
    gint i;

    GST_FLAG_SET (this, GST_ALSA_CAPS_NEGO);

    if (gst_alsa_formats_match (this->format, format)) {
      ret = GST_PAD_LINK_OK;
      goto out;
    }

    if (!gst_alsa_probe_hw_params (this, format)) {
      ret = GST_PAD_LINK_REFUSED;
      goto out;
    }

    for (i = 0; i < ((GstElement *) this)->numpads; i++) {
      g_assert (this->pad[i] != NULL);
      if (this->pad[i] == pad)
        continue;
      if (gst_pad_try_set_caps (this->pad[i], caps) == GST_PAD_LINK_REFUSED) {
        if (this->format) {
          GstCaps *old =
              gst_alsa_caps (this->format->format, this->format->rate,
              this->format->channels);

          for (--i; i >= 0; i--) {
            if (gst_pad_try_set_caps (this->pad[i],
                    old) == GST_PAD_LINK_REFUSED) {
              GST_ELEMENT_ERROR (this, CORE, NEGOTIATION, (NULL),
                  ("could not reset caps to a sane value"));
              gst_caps_free (old);
              break;
            } else {
              /* FIXME: unset caps on pads somehow */
            }
          }
          gst_caps_free (old);
          ret = GST_PAD_LINK_REFUSED;
          goto out;
        }
      }
    }

    GST_FLAG_UNSET (this, GST_ALSA_CAPS_NEGO);

    /* sync the params */
    if (GST_FLAG_IS_SET (this, GST_ALSA_RUNNING))
      gst_alsa_stop_audio (this);
    g_free (this->format);
    this->format = format;
    if (!gst_alsa_start_audio (this)) {
      GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL), (NULL));
      return GST_PAD_LINK_REFUSED;
    }

    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_DELAYED;

out:
  g_free (format);
  GST_FLAG_UNSET (this, GST_ALSA_CAPS_NEGO);
  return ret;
}

static GstElementStateReturn
gst_alsa_change_state (GstElement * element)
{
  int err = 0;
  GstAlsa *this;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_ALSA (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(GST_FLAG_IS_SET (element, GST_ALSA_OPEN) ||
              gst_alsa_open_audio (this)))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (!(GST_FLAG_IS_SET (element, GST_ALSA_RUNNING) ||
              gst_alsa_start_audio (this)))
        return GST_STATE_FAILURE;
      this->transmitted = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (snd_pcm_state (this->handle) == SND_PCM_STATE_PAUSED) {
        if ((err = snd_pcm_pause (this->handle, 0)) < 0) {
          GST_ERROR_OBJECT (this, "Error unpausing sound: %s",
              snd_strerror (err));
          return GST_STATE_FAILURE;
        }
      } else if (!(GST_FLAG_IS_SET (element, GST_ALSA_RUNNING) ||
              gst_alsa_start_audio (this))) {
        return GST_STATE_FAILURE;
      }
      gst_alsa_clock_start (this->clock);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (GST_ALSA_CAPS_IS_SET (this, GST_ALSA_CAPS_PAUSE)) {
        if (snd_pcm_state (this->handle) == SND_PCM_STATE_RUNNING) {
          if ((err = snd_pcm_pause (this->handle, 1)) < 0) {
            GST_ERROR_OBJECT (this, "Error pausing sound: %s",
                snd_strerror (err));
            GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_PAUSE, 0);
            goto cant_pause;
          }
        }
      } else {
      cant_pause:
        /* if device doesn't know how to pause, we just stop */
        if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING))
          gst_alsa_stop_audio (this);
      }
      gst_alsa_clock_stop (this->clock);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING))
        gst_alsa_stop_audio (this);
      g_free (this->format);
      this->format = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      if (GST_FLAG_IS_SET (element, GST_ALSA_OPEN))
        gst_alsa_close_audio (this);
      break;

    default:
      g_assert_not_reached ();
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstClock *
gst_alsa_get_clock (GstElement * element)
{
  return GST_CLOCK (GST_ALSA (element)->clock);
}

static void
gst_alsa_set_clock (GstElement * element, GstClock * clock)
{                               /* we need this function just so everybody knows we use a clock */
}

/*** AUDIO PROCESSING *********************************************************/

inline snd_pcm_sframes_t
gst_alsa_update_avail (GstAlsa * this)
{
  snd_pcm_sframes_t avail = snd_pcm_avail_update (this->handle);

  if (avail < 0) {
    if (avail == -EPIPE) {
      gst_alsa_xrun_recovery (this);
    } else {
      GST_WARNING_OBJECT (this, "unknown ALSA avail_update return value (%d)",
          (int) avail);
    }
  }
  return avail;
}

/* returns TRUE, if the loop should go on */
inline gboolean
gst_alsa_pcm_wait (GstAlsa * this)
{
  int err;

  if (snd_pcm_state (this->handle) == SND_PCM_STATE_RUNNING) {
    if ((err = snd_pcm_wait (this->handle, 1000)) < 0) {
      if (err == EINTR) {
        /* happens mostly when run under gdb, or when exiting due to a signal */
        GST_DEBUG ("got interrupted while waiting");
        if (gst_element_interrupt (GST_ELEMENT (this))) {
          return TRUE;
        } else {
          return FALSE;
        }
      }
      if (!gst_alsa_xrun_recovery (this)) {
        GST_ERROR_OBJECT (this, "error waiting for alsa pcm: (%d: %s)", err,
            snd_strerror (err));
        return FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * error out or make sure we're in SND_PCM_STATE_RUNNING afterwards 
 * return FALSE if we're not
 */
inline gboolean
gst_alsa_start (GstAlsa * this)
{
  GST_DEBUG ("Setting state to RUNNING");

  switch (snd_pcm_state (this->handle)) {
    case SND_PCM_STATE_XRUN:
      gst_alsa_xrun_recovery (this);
      return gst_alsa_start (this);
    case SND_PCM_STATE_SETUP:
      ERROR_CHECK (snd_pcm_prepare (this->handle), "error preparing: %s");
    case SND_PCM_STATE_SUSPENDED:
    case SND_PCM_STATE_PREPARED:
      ERROR_CHECK (snd_pcm_start (this->handle), "error starting playback: %s");
      break;
    case SND_PCM_STATE_PAUSED:
      ERROR_CHECK (snd_pcm_pause (this->handle, 0), "error unpausing: %s");
      break;
    case SND_PCM_STATE_RUNNING:
      break;
    case SND_PCM_STATE_DRAINING:
    case SND_PCM_STATE_OPEN:
      /* this probably happens when someone replugged a pipeline and we're in a
         really weird state because our cothread wasn't busted */
      return FALSE;
    default:
      /* it's a bug when we get here */
      g_assert_not_reached ();
      break;
  }
  return TRUE;
}

gboolean
gst_alsa_xrun_recovery (GstAlsa * this)
{
  snd_pcm_status_t *status;
  gint err;

  snd_pcm_status_alloca (&status);

  if ((err = snd_pcm_status (this->handle, status)) < 0)
    GST_ERROR_OBJECT (this, "status error: %s", snd_strerror (err));

  if (snd_pcm_status_get_state (status) == SND_PCM_STATE_XRUN) {
    struct timeval now, diff, tstamp;

    gettimeofday (&now, 0);
    snd_pcm_status_get_trigger_tstamp (status, &tstamp);
    timersub (&now, &tstamp, &diff);
    GST_INFO_OBJECT (this, "alsa: xrun of at least %.3f msecs",
        diff.tv_sec * 1000 + diff.tv_usec / 1000.0);

    /* if we're allowed to recover, ... */
    if (this->autorecover) {
      /* ... then increase the period size or buffer size / period count to
         prevent further xruns (at the cost of increased latency and memory
         usage). */
      if (this->period_count >= 4) {
        this->period_size *= 2;
        this->period_count /= 2;
      } else {
        this->period_count *= 2;
      }
    }
  }

  if (!(gst_alsa_stop_audio (this) && gst_alsa_start_audio (this))) {
    GST_ELEMENT_ERROR (this, RESOURCE, FAILED, (NULL),
        ("Error restarting audio after xrun"));
    return FALSE;
  }

  return TRUE;
}

/*** AUDIO SETUP / START / STOP ***********************************************/

void
gst_alsa_set_eos (GstAlsa * this)
{
  gst_alsa_drain_audio (this);
  gst_element_set_eos (GST_ELEMENT (this));
}

static gboolean
gst_alsa_open_audio (GstAlsa * this)
{
  snd_pcm_info_t *info;
  int ret;

  g_assert (this != NULL);
  g_assert (this->handle == NULL);

  /* If we have no pads, then we're apparently a mixer object,
   * and that doesn't need a handle to the actual audio device. */
  if (!gst_element_get_pad_list (GST_ELEMENT (this)))
    return TRUE;

  GST_INFO ("Opening alsa device \"%s\"...", this->device);

  ERROR_CHECK (snd_output_buffer_open (&this->out),
      "error opening log output: %s");

  if ((ret = snd_pcm_open (&this->handle, this->device,
              GST_ALSA_GET_CLASS (this)->stream, SND_PCM_NONBLOCK)) < 0) {
    /* ALSA inverts standard errno.h error codes */
    switch (-ret) {
      case EBUSY:
        GST_ELEMENT_ERROR (GST_ELEMENT (this), RESOURCE, BUSY,
            (_("ALSA device \"%s\" is already in use by another program."),
                this->device), (NULL));
        break;
      case EACCES:
      case ETXTBSY:
        GST_ELEMENT_ERROR (GST_ELEMENT (this), RESOURCE, OPEN_READ_WRITE,
            (_("Could not access ALSA device \"%s\", check its permissions."),
                this->device), GST_ERROR_SYSTEM);
        break;

      case ENXIO:
      case ENODEV:
      case ENOENT:
        GST_ELEMENT_ERROR (GST_ELEMENT (this), RESOURCE, BUSY,
            (_("ALSA device \"%s\" does not exist."), this->device), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (GST_ELEMENT (this), RESOURCE, BUSY,
            (_("ALSA device \"%s\" had an error."),
                this->device), ("ALSA error %d: %s", ret, snd_strerror (ret)));
        break;
    }
    return FALSE;
  }

  snd_pcm_info_malloc (&info);
  snd_pcm_info (this->handle, info);
  this->cardname = g_strdup (snd_pcm_info_get_name (info));
  snd_pcm_info_free (info);

  GST_FLAG_SET (this, GST_ALSA_OPEN);
  return TRUE;
}

void
gst_alsa_sw_params_dump (GstAlsa * this, snd_pcm_sw_params_t * sw_params)
{
  snd_pcm_sw_params_dump (sw_params, this->out);
  ALSA_DEBUG_FLUSH (this);
}

void
gst_alsa_hw_params_dump (GstAlsa * this, snd_pcm_hw_params_t * hw_params)
{
  snd_pcm_hw_params_dump (hw_params, this->out);
  ALSA_DEBUG_FLUSH (this);
}

/* if someone finds an easy way to merge this with _set_hw_params, go ahead */
static gboolean
gst_alsa_probe_hw_params (GstAlsa * this, GstAlsaFormat * format)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_uframes_t period_size;
  unsigned int period_count;
  unsigned int rate;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  GST_INFO ("Probing format: %s %dHz, %d channels",
      snd_pcm_format_name (format->format), format->rate, format->channels);

  snd_pcm_hw_params_alloca (&hw_params);
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params));

  gst_alsa_hw_params_dump (this, hw_params);

  if (GST_ELEMENT (this)->numpads == 1) {
    SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_access (this->handle,
            hw_params, this->
            mmap ? SND_PCM_ACCESS_MMAP_INTERLEAVED :
            SND_PCM_ACCESS_RW_INTERLEAVED));
  } else {
    SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_access (this->handle,
            hw_params, this->
            mmap ? SND_PCM_ACCESS_MMAP_NONINTERLEAVED :
            SND_PCM_ACCESS_RW_NONINTERLEAVED));
  }

  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_format (this->handle, hw_params,
          format->format));
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_channels (this->handle, hw_params,
          format->channels));
  /* FIXME: We should use snd_pcm_hw_params_set_rate instead of 
   * snd_pcm_hw_params_set_rate_near here. Unfortunately alsa fails in that case
   * far more often and seems to handle this quite well. (Example: ENS1371 
   * driver on alsalib 1.0.5, kernel 2.6.6-mm5). If it sets far too wrong sample
   * rates, we need to revert back to snd_pcm_hw_params_set_rate or check the
   * rate that was set.
   */
  rate = format->rate;
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_rate_near (this->handle, hw_params,
          &rate, 0));
  if (rate != format->rate)
    GST_WARNING_OBJECT (this, "set rate (%u) differs from desired rate (%u)",
        rate, format->rate);

  period_count = this->period_count;
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_periods_near (this->handle,
          hw_params, &period_count, 0));
  period_size = this->period_size;
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_period_size_near (this->handle,
          hw_params, &period_size, 0));

  return TRUE;
}

/**
 * You must set all hw parameters at once and can't use already set params and
 * change them.
 * Thx ALSA for not documenting this
 */
static gboolean
gst_alsa_set_hw_params (GstAlsa * this)
{
  snd_pcm_hw_params_t *hw_params;
  unsigned int rate;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  snd_pcm_hw_params_alloca (&hw_params);
  ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params),
      "Broken configuration for this PCM: %s");

  if (this->format) {
    GST_INFO ("Preparing format: %s %dHz, %d channels",
        snd_pcm_format_name (this->format->format), this->format->rate,
        this->format->channels);

    if (GST_ELEMENT (this)->numpads == 1) {
      ERROR_CHECK (snd_pcm_hw_params_set_access (this->handle, hw_params, this->
              mmap ? SND_PCM_ACCESS_MMAP_INTERLEAVED :
              SND_PCM_ACCESS_RW_INTERLEAVED),
          "This plugin does not support your harware: %s");
    } else {
      ERROR_CHECK (snd_pcm_hw_params_set_access (this->handle, hw_params, this->
              mmap ? SND_PCM_ACCESS_MMAP_NONINTERLEAVED :
              SND_PCM_ACCESS_RW_NONINTERLEAVED),
          "This plugin does not support your harware: %s");
    }

    ERROR_CHECK (snd_pcm_hw_params_set_format (this->handle, hw_params,
            this->format->format), "Sample format (%s) not available: %s",
        snd_pcm_format_name (this->format->format));
    ERROR_CHECK (snd_pcm_hw_params_set_channels (this->handle, hw_params,
            this->format->channels), "Channels count (%d) not available: %s",
        this->format->channels);
    /* FIXME: We should use snd_pcm_hw_params_set_rate instead of 
     * snd_pcm_hw_params_set_rate_near here. Unfortunately alsa fails in that case
     * far more often and seems to handle this quite well. (Example: ENS1371 
     * driver on alsalib 1.0.5, kernel 2.6.6-mm5). If it sets far too wrong sample
     * rates, we need to revert back to snd_pcm_hw_params_set_rate or check the
     * rate that was set.
     */
    rate = this->format->rate;
    ERROR_CHECK (snd_pcm_hw_params_set_rate_near (this->handle, hw_params,
            &rate, 0), "error setting rate (%d): %s", this->format->rate);
    if (rate != this->format->rate)
      GST_WARNING_OBJECT (this, "set rate (%u) differs from desired rate (%u)",
          rate, this->format->rate);
    ERROR_CHECK (snd_pcm_hw_params_set_periods_near (this->handle, hw_params,
            &this->period_count, 0), "error setting period count to %u: %s",
        (guint) this->period_count);
    ERROR_CHECK (snd_pcm_hw_params_set_period_size_near (this->handle,
            hw_params, &this->period_size, 0),
        "error setting period size to %u frames: %s",
        (guint) this->period_size);
  } else {
    GST_INFO_OBJECT (this, "Preparing format: (none)");
  }
  gst_alsa_hw_params_dump (this, hw_params);


  ERROR_CHECK (snd_pcm_hw_params (this->handle, hw_params),
      "Could not set hardware parameters: %s");

  /* now get the pcm caps */
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_PAUSE,
      snd_pcm_hw_params_can_pause (hw_params));
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_RESUME,
      snd_pcm_hw_params_can_resume (hw_params));
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_SYNC_START,
      snd_pcm_hw_params_can_sync_start (hw_params));

  if (this->mmap) {
    this->transmit = GST_ALSA_GET_CLASS (this)->transmit_mmap;
  } else {
    this->transmit = GST_ALSA_GET_CLASS (this)->transmit_rw;
  }

  return TRUE;
}

static gboolean
gst_alsa_set_sw_params (GstAlsa * this)
{
  snd_pcm_sw_params_t *sw_params;

  if (!this->format) {
    GST_LOG_OBJECT (this, "not setting sw params, we're not negotiated yet");
    return TRUE;
  }

  snd_pcm_sw_params_alloca (&sw_params);
  ERROR_CHECK (snd_pcm_sw_params_current (this->handle, sw_params),
      "Could not get current software parameters: %s");

  gst_alsa_sw_params_dump (this, sw_params);

  ERROR_CHECK (snd_pcm_sw_params_set_silence_size (this->handle, sw_params, 0),
      "could not set silence size: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_silence_threshold (this->handle, sw_params,
          0), "could not set silence threshold: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_avail_min (this->handle, sw_params,
          this->period_size), "could not set avail min: %s");
  /* we start explicitly */
  ERROR_CHECK (snd_pcm_sw_params_set_start_threshold (this->handle, sw_params,
          this->period_size * this->period_count + 1),
      "could not set start mode: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_stop_threshold (this->handle, sw_params,
          this->period_size * this->period_count),
      "could not set stop mode: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_xfer_align (this->handle, sw_params, 1),
      "Unable to set transfer align for playback: %s");
  ERROR_CHECK (snd_pcm_sw_params (this->handle, sw_params),
      "could not set sw_params: %s");
  return TRUE;
}

static gboolean
gst_alsa_start_audio (GstAlsa * this)
{
  g_assert (GST_FLAG_IS_SET (this, GST_ALSA_OPEN));

  if (!gst_alsa_set_hw_params (this))
    return FALSE;
  if (!gst_alsa_set_sw_params (this))
    GST_WARNING_OBJECT (this,
        "setting software parameters failed, we'll trust the defaults");

  GST_FLAG_SET (this, GST_ALSA_RUNNING);
  return TRUE;
}

static gboolean
gst_alsa_drain_audio (GstAlsa * this)
{
  g_assert (this != NULL);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  GST_DEBUG ("stopping alsa");

  switch (snd_pcm_state (this->handle)) {
    case SND_PCM_STATE_XRUN:
    case SND_PCM_STATE_RUNNING:
      /* fall through - clock is already stopped when paused */
    case SND_PCM_STATE_PAUSED:
      /* snd_pcm_drain only works in blocking mode */
      ERROR_CHECK (snd_pcm_nonblock (this->handle, 0),
          "couldn't set blocking mode: %s");
      ERROR_CHECK (snd_pcm_drain (this->handle),
          "couldn't stop and drain buffer: %s");
      ERROR_CHECK (snd_pcm_nonblock (this->handle, 1),
          "couldn't set non-blocking mode: %s");
      break;
    default:
      break;
  }

  GST_FLAG_UNSET (this, GST_ALSA_RUNNING);
  return TRUE;
}

static gboolean
gst_alsa_stop_audio (GstAlsa * this)
{
  g_assert (this != NULL);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  GST_DEBUG ("stopping alsa, skipping pending frames");

  switch (snd_pcm_state (this->handle)) {
    case SND_PCM_STATE_XRUN:
    case SND_PCM_STATE_RUNNING:
      /* fall through - clock is already stopped when paused */
    case SND_PCM_STATE_PAUSED:
      ERROR_CHECK (snd_pcm_drop (this->handle),
          "couldn't stop (dropping frames): %s");
      break;
    default:
      break;
  }

  GST_FLAG_UNSET (this, GST_ALSA_RUNNING);
  return TRUE;
}

static gboolean
gst_alsa_close_audio (GstAlsa * this)
{
  gint err;

  /* if there's no pads, we never open. So we don't close either. */
  if (!gst_element_get_pad_list (GST_ELEMENT (this)))
    return TRUE;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  ALSA_DEBUG_FLUSH (this);
  err = snd_output_close (this->out);
  if (err != 0)
    GST_ERROR_OBJECT (this, "failed to close debugging output: %s",
        snd_strerror (err));
  ERROR_CHECK (snd_pcm_close (this->handle), "Error closing device: %s");

  this->handle = NULL;
  if (this->cardname) {
    g_free (this->cardname);
    this->cardname = NULL;
  }
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_PAUSE, 0);
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_RESUME, 0);
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_SYNC_START, 0);
  GST_FLAG_UNSET (this, GST_ALSA_OPEN);

  return TRUE;
}

/*** QUERYING/FORMAT/CONVERSION FUNCTIONS *************************************/

static const GstFormat *
gst_alsa_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_BYTES,
    0
  };

  return formats;
}

static gboolean
gst_alsa_pad_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  return gst_alsa_convert (GST_ALSA (GST_PAD_PARENT (pad)), src_format,
      src_value, dest_format, dest_value);
}

static gboolean
gst_alsa_convert (GstAlsa * this, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }
  if (this->format == NULL)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_alsa_bytes_to_samples (this, (guint) src_value);
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_alsa_bytes_to_timestamp (this, (guint) src_value);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_alsa_timestamp_to_samples (this, (GstClockTime) src_value);
          break;
        case GST_FORMAT_BYTES:
          *dest_value =
              gst_alsa_timestamp_to_bytes (this, (GstClockTime) src_value);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_alsa_samples_to_timestamp (this, (guint) src_value);
          break;
        case GST_FORMAT_BYTES:
          *dest_value = gst_alsa_samples_to_bytes (this, (guint) src_value);
          break;
        case GST_FORMAT_DEFAULT:
          g_assert_not_reached ();
          /* fall through */
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static const GstQueryType *
gst_alsa_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
}

static gboolean
gst_alsa_query_func (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;
  GstAlsa *this = GST_ALSA (element);

  switch (type) {
    case GST_QUERY_LATENCY:{
      snd_pcm_sframes_t delay;

      ERROR_CHECK (snd_pcm_delay (this->handle, &delay),
          "Error getting delay: %s");
      res =
          gst_alsa_convert (this, GST_FORMAT_DEFAULT, (gint64) delay, format,
          value);
      break;
    }
    case GST_QUERY_POSITION:
      res =
          gst_alsa_convert (this, GST_FORMAT_TIME,
          gst_element_get_time (GST_ELEMENT (this)), format, value);
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_alsa_query (GstElement * element, GstQueryType type, GstFormat * format,
    gint64 * value)
{
  return gst_alsa_pad_query (GST_ALSA (element)->pad[0], type, format, value);
}

static gboolean
gst_alsa_pad_query (GstPad * pad, GstQueryType type, GstFormat * format,
    gint64 * value)
{
  if (gst_alsa_query_func (GST_PAD_PARENT (pad), type, format, value))
    return TRUE;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK
      && gst_pad_query (gst_pad_get_peer (pad), type, format, value))
    return TRUE;

  return FALSE;
}

inline snd_pcm_uframes_t
gst_alsa_timestamp_to_samples (GstAlsa * this, GstClockTime time)
{
  return (snd_pcm_uframes_t) ((time * this->format->rate +
          this->format->rate / 2) / GST_SECOND);
}

inline GstClockTime
gst_alsa_samples_to_timestamp (GstAlsa * this, snd_pcm_uframes_t samples)
{
  return ((GstClockTime) samples) * GST_SECOND / this->format->rate;
}

inline snd_pcm_uframes_t
gst_alsa_bytes_to_samples (GstAlsa * this, guint bytes)
{
  return bytes / (snd_pcm_format_physical_width (this->format->format) / 8) /
      (GST_ELEMENT (this)->numpads == 1 ? this->format->channels : 1);
}

inline guint
gst_alsa_samples_to_bytes (GstAlsa * this, snd_pcm_uframes_t samples)
{
  return samples * snd_pcm_format_physical_width (this->format->format) / 8 *
      (GST_ELEMENT (this)->numpads == 1 ? this->format->channels : 1);
}

inline GstClockTime
gst_alsa_bytes_to_timestamp (GstAlsa * this, guint bytes)
{
  return gst_alsa_samples_to_timestamp (this, gst_alsa_bytes_to_samples (this,
          bytes));
}

inline guint
gst_alsa_timestamp_to_bytes (GstAlsa * this, GstClockTime time)
{
  return gst_alsa_samples_to_bytes (this, gst_alsa_timestamp_to_samples (this,
          time));
}
