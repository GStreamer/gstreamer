/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <sys/time.h>
#include "gstalsa.h"

/* elementfactory information */
static GstElementDetails gst_alsa_sink_details = {
  "Alsa Sink",
  "Sink/Audio",
  "LGPL",
  "Output to a sound card via ALSA",
  VERSION,
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>",
  "(C) 2001 "
};

/* elementfactory information */
static GstElementDetails gst_alsa_src_details = {
  "Alsa Src",
  "Source/Audio",
  "LGPL",
  "Read from a sound card via ALSA",
  VERSION,
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>",
  "(C) 2001"
};

/* GObject functions */
static void gst_alsa_class_init (GstAlsaClass *klass);
static void gst_alsa_init (GstAlsa *this);
static void gst_alsa_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_alsa_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* GStreamer functions for pads and state changing */
static GstPadTemplate   *gst_alsa_src_pad_factory ();
static GstPadTemplate   *gst_alsa_src_request_pad_factory ();
static GstPadTemplate   *gst_alsa_sink_pad_factory ();
static GstPadTemplate   *gst_alsa_sink_request_pad_factory ();

static GstPad           *gst_alsa_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *name);
static GstPadLinkReturn  gst_alsa_link (GstPad *pad, GstCaps *caps);
static GstCaps          *gst_alsa_caps (GstAlsa *this);

static GstElementStateReturn gst_alsa_change_state (GstElement *element);

/* audio processing functions */
static void     gst_alsa_loop (GstElement *element);
static void     gst_alsa_xrun_recovery (GstAlsa *this);

static gboolean gst_alsa_src_process (GstAlsa *this, snd_pcm_uframes_t frames);
static gboolean gst_alsa_sink_process (GstAlsa *this, snd_pcm_uframes_t frames);
static void     gst_alsa_sink_check_event (GstAlsa *this, GstAlsaPad *pad);

/* alsa setup / start / stop functions */
static gboolean gst_alsa_set_params (GstAlsa *this);

static gboolean gst_alsa_open_audio (GstAlsa *this);
static gboolean gst_alsa_start_audio (GstAlsa *this);
static void     gst_alsa_stop_audio (GstAlsa *this);
static void     gst_alsa_close_audio (GstAlsa *this);

static gboolean gst_alsa_get_channel_addresses (GstAlsa *this);
static void     gst_alsa_release_channel_addresses (GstAlsa *this);

/* #define _DEBUG */
#ifdef _DEBUG
#define DEBUG(text, args...) g_message(text, ##args)
#else
#define DEBUG(text, args...)
#endif

/*** TYPE FUNCTIONS ***********************************************************/

#define GST_TYPE_ALSA_FORMAT (gst_alsa_format_get_type())
static GType
gst_alsa_format_get_type (void)
{
  static GType type = 0;
  static GEnumValue *values = NULL;
  gint i, len = SND_PCM_FORMAT_GSM + 3;

  if (values == NULL) {
    /* the three: for -1, 0, and the terminating NULL */
    values = g_new0 (GEnumValue, len);

    for (i = 0; i < len - 1; i++) {
      values[i].value = i - 1;        /* UNKNOWN is -1 */
      values[i].value_name = g_strdup_printf ("%d", i - 1);
      values[i].value_nick = g_strdup (snd_pcm_format_name ((snd_pcm_format_t) i - 1));
    }
  }

  if (!type)
    type = g_enum_register_static ("GstAlsaFormat", values);

  return type;
}

GType
gst_alsa_get_type (void)
{
  static GType alsa_type = 0;

  if (!alsa_type) {
    static const GTypeInfo alsa_info = {
      sizeof (GstAlsaClass),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof (GstAlsa),
      0,
      NULL,
    };

    alsa_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAlsa", &alsa_info, 0);
  }
  return alsa_type;
}

GType
gst_alsa_sink_get_type (void)
{
  static GType alsa_type = 0;

  if (!alsa_type) {
    static const GTypeInfo alsa_info = {
      sizeof (GstAlsaClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_alsa_class_init,
      NULL,
      NULL,
      sizeof (GstAlsa),
      0,
      (GInstanceInitFunc) gst_alsa_init,
    };

    alsa_type = g_type_register_static (GST_TYPE_ALSA, "GstAlsaSink", &alsa_info, 0);
  }
  return alsa_type;
}

GType
gst_alsa_src_get_type (void)
{
  static GType alsa_type = 0;

  if (!alsa_type) {
    static const GTypeInfo alsa_info = {
      sizeof (GstAlsaClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_alsa_class_init,
      NULL,
      NULL,
      sizeof (GstAlsa),
      0,
      (GInstanceInitFunc) gst_alsa_init,
    };

    alsa_type = g_type_register_static (GST_TYPE_ALSA, "GstAlsaSrc", &alsa_info, 0);
  }
  return alsa_type;
}

/*** GOBJECT FUNCTIONS ********************************************************/

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_PERIODCOUNT,
  ARG_PERIODFRAMES,
  ARG_DEBUG,
  ARG_AUTORECOVER
};

static GstElement *parent_class = NULL;

static void
gst_alsa_class_init (GstAlsaClass *klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  object_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  if (parent_class == NULL)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->get_property = gst_alsa_get_property;
  object_class->set_property = gst_alsa_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
    g_param_spec_string ("device", "Device", "Alsa device, as defined in an asoundrc",
                         "default", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FORMAT,
    g_param_spec_enum ("format", "Format", "PCM audio format",
                       GST_TYPE_ALSA_FORMAT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHANNELS,
    g_param_spec_int ("channels", "Channels", "Number of channels",
                      1, 64, 2, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE,
    g_param_spec_int ("rate", "Rate", "Sample rate, in Hz",
                      8000, 192000, 44100, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PERIODCOUNT,
    g_param_spec_int ("period-count", "Period count", "Number of hardware buffers to use",
                      2, 64, 2, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PERIODFRAMES,
    g_param_spec_int ("period-frames", "Period frames", "Number of frames (samples on each channel) in one hardware period",
                      64, 8192, 8192, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEBUG,
    g_param_spec_boolean ("debug", "Debug", "Set to TRUE to output PCM state info",
                          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_AUTORECOVER,
    g_param_spec_boolean ("autorecover", "Automatic xrun recovery", "Set to TRUE to increase the period count on xruns",
                          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  element_class->change_state = gst_alsa_change_state;
  element_class->request_new_pad = gst_alsa_request_new_pad;
}

static void
gst_alsa_init (GstAlsa *this)
{
  /* init values */
  this->handle = NULL;

  GST_FLAG_SET (this, GST_ELEMENT_THREAD_SUGGESTED);

  if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC) {
    this->stream = SND_PCM_STREAM_CAPTURE;
    this->format = SND_PCM_FORMAT_S16;        /* native endian */
    this->process = gst_alsa_src_process;
    this->pads = g_list_append (NULL, g_new0 (GstAlsaPad, 1));
    GST_ALSA_PAD (this->pads)->pad =
      gst_pad_new_from_template (gst_alsa_src_pad_factory (), "src");

  } else if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SINK) {
    this->stream = SND_PCM_STREAM_PLAYBACK;
    this->format = SND_PCM_FORMAT_UNKNOWN; /* we don't know until caps are set */
    this->process = gst_alsa_sink_process;
    this->pads = g_list_append (NULL, g_new0 (GstAlsaPad, 1));
    GST_ALSA_PAD (this->pads)->pad =
      gst_pad_new_from_template (gst_alsa_sink_pad_factory (), "sink");

  }

  GST_ALSA_PAD (this->pads)->channel = -1;

  /* data is interleaved by default, because there's only one default pad */
  this->data_interleaved = TRUE;

  gst_element_add_pad (GST_ELEMENT (this), GST_ALSA_PAD (this->pads)->pad);

  gst_pad_set_link_function (GST_ALSA_PAD (this->pads)->pad, gst_alsa_link);
  gst_element_set_loop_function (GST_ELEMENT (this), gst_alsa_loop);
}

static void
gst_alsa_set_property (GObject *object, guint prop_id, const GValue *value,
                       GParamSpec *pspec)
{
  GstAlsa *this;

  this = (GstAlsa *) object;
  switch (prop_id) {
  case ARG_DEVICE:
    if (this->device)
      g_free (this->device);
    this->device = g_strdup (g_value_get_string (value));
    break;
  case ARG_FORMAT:
    this->format = g_value_get_enum (value);
    break;
  case ARG_CHANNELS:
    this->channels = g_value_get_int (value);
    break;
  case ARG_RATE:
    this->rate = g_value_get_int (value);
    break;
  case ARG_PERIODCOUNT:
    this->period_count = g_value_get_int (value);
    this->buffer_frames = this->period_count * this->period_frames;
    break;
  case ARG_PERIODFRAMES:
    this->period_frames = g_value_get_int (value);
    this->buffer_frames = this->period_count * this->period_frames;
    break;
  case ARG_DEBUG:
    this->debug = g_value_get_boolean (value);
    return;
  case ARG_AUTORECOVER:
    this->autorecover = g_value_get_boolean (value);
    return;
  default:
    GST_DEBUG (0, "Unknown arg");
    return;
  }

  if (GST_STATE (this) == GST_STATE_NULL)
    return;

  if (GST_FLAG_IS_SET (this, GST_ALSA_RUNNING)) {
    gst_alsa_stop_audio (this);
    gst_alsa_set_params (this);
    gst_alsa_start_audio (this);
  } else {
    gst_alsa_set_params (this);
  }
}

static void
gst_alsa_get_property (GObject *object, guint prop_id, GValue *value,
                       GParamSpec *pspec)
{
  GstAlsa *this;

  this = (GstAlsa *) object;

  switch (prop_id) {
  case ARG_DEVICE:
    g_value_set_string (value, this->device);
    break;
  case ARG_FORMAT:
    g_value_set_enum (value, this->format);
    break;
  case ARG_CHANNELS:
    g_value_set_int (value, this->channels);
    break;
  case ARG_RATE:
    g_value_set_int (value, this->rate);
    break;
  case ARG_PERIODCOUNT:
    g_value_set_int (value, this->period_count);
    break;
  case ARG_PERIODFRAMES:
    g_value_set_int (value, this->period_frames);
    break;
  case ARG_DEBUG:
    g_value_set_boolean (value, this->debug);
    break;
  case ARG_AUTORECOVER:
    g_value_set_boolean (value, this->autorecover);
    break;
  default:
    GST_DEBUG (0, "Unknown arg");
    break;
  }
}

/*** GSTREAMER PAD / STATE FUNCTIONS*******************************************/

static GstPadTemplate *
gst_alsa_src_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_SOMETIMES,
                                     gst_caps_new ("src", "audio/raw", NULL),
                                     NULL);

  return template;
}

static GstPadTemplate *
gst_alsa_src_request_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template =
      gst_pad_template_new ("src%d", GST_PAD_SRC, GST_PAD_REQUEST,
                            gst_caps_new ("src-request", "audio/raw",
                              gst_props_new ("channels", GST_PROPS_INT (1), NULL)),
                            NULL);

  return template;
}

static GstPadTemplate *
gst_alsa_sink_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_SOMETIMES,
                                     gst_caps_new ("sink", "audio/raw", NULL),
                                     NULL);

  return template;
}

static GstPadTemplate *
gst_alsa_sink_request_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template =
      gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_REQUEST,
                            gst_caps_new ("sink-request", "audio/raw",
                              gst_props_new ("channels", GST_PROPS_INT (1), NULL)),
                            NULL);

  return template;
}

static GstPad *
gst_alsa_request_new_pad (GstElement *element, GstPadTemplate *templ,
                          const gchar *name)
{
  GstAlsa *this;
  GstAlsaPad *pad;
  gint channel;
  gchar *newname;
  GList *l;

  g_return_val_if_fail ((this = GST_ALSA (element)), NULL);

  /* you can't request a pad if the non-request pad is connected */
  g_return_val_if_fail (this->data_interleaved == FALSE ||
                        this->pads == NULL ||
                        GST_ALSA_PAD (this->pads) == NULL ||
                        GST_ALSA_PAD (this->pads)->pad == NULL ||
                        GST_PAD_PEER (GST_ALSA_PAD (this->pads)->pad) == NULL,
                        NULL);

  if (name) {
    /* locate the channel number in the requested pad name. to do so look at
       where the % (which begins the %d) is in the template name. */
    channel = atoi (name + (strchr (templ->name_template, '%') -
                            templ->name_template));

    /* make sure the requested channel is free. */
    l = this->pads;
    while (l) {
      if (GST_ALSA_PAD (l)->channel == channel) {
        g_warning ("requested channel %d already in use.", channel);
        return NULL;
      }
      l = l->next;
    }
    newname = g_strdup (name);
  } else {
    channel = 0;

    /* if the user doesn't care which channel, find the lowest channel number
       that's free. */
    l = this->pads;
    while (l) {
      if (GST_ALSA_PAD (l)->channel >= channel)
        channel = GST_ALSA_PAD (l)->channel + 1;
      l = l->next;
    }
    newname = g_strdup_printf (templ->name_template, channel);
  }

  /* set up a new GstAlsaPad struct to hold this channel's info. */
  pad = g_new0 (GstAlsaPad, 1);
  pad->channel = channel;
  pad->pad = gst_pad_new_from_template (templ, newname);

  gst_element_add_pad (GST_ELEMENT (this), pad->pad);
  gst_pad_set_link_function (pad->pad, gst_alsa_link);

  /* if the only pad is the default (nonrequest) pad, then remove the current
     pad (we know it's free from the above g_return_val_if_fail) */
  if (this->data_interleaved && this->pads) {
    gst_element_remove_pad (GST_ELEMENT (this), GST_ALSA_PAD (this->pads)->pad);
    g_free (GST_ALSA_PAD (this->pads));
    g_list_free (this->pads);
    this->pads = NULL;
  }

  this->pads = g_list_append (this->pads, pad);

  /* all request pads are mono (non-interleaved). FIXME: allow interleaved
     access (for hw:N,M access on consumer hardware) */
  if (this->data_interleaved) {
    this->channels = pad->channel + 1;
    this->data_interleaved = FALSE;
  } else {
    this->channels = MAX (this->channels, pad->channel + 1);
  }

  return pad->pad;
}

static gboolean
gst_alsa_parse_caps (GstAlsa *this, GstCaps *caps)
{
  gint law, endianness, width, depth, channels;
  gboolean sign;
  gint format = -1;
  const gchar *format_name;

  if (!gst_caps_get_string (caps, "format", &format_name))
    return FALSE;

  if (format_name == NULL) {
    return FALSE;
  } else if (strcmp (format_name, "int") == 0) {
    if (!gst_caps_get (caps,
                       "width", &width, "depth", &depth, "law", &law,
                       "endianness", &endianness, "signed", &sign, NULL))
      return FALSE;

    if (law == 0) {
      if (width == 8) {
        if (sign == TRUE) {
          format = SND_PCM_FORMAT_S8;
        } else {
          format = SND_PCM_FORMAT_U8;
        }
      } else if (width == 16) {
        if (sign == TRUE) {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_S16_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_S16_BE;
        } else {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_U16_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_U16_BE;
        }
      } else if (width == 24) {
        if (sign == TRUE) {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_S24_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_S24_BE;
        } else {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_U24_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_U24_BE;
        }
      } else if (width == 32) {
        if (sign == TRUE) {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_S32_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_S32_BE;
        } else {
          if (endianness == G_LITTLE_ENDIAN)   format = SND_PCM_FORMAT_U32_LE;
          else if (endianness == G_BIG_ENDIAN) format = SND_PCM_FORMAT_U32_BE;
        }
      }
    } else if (law == 1) { /* mu law */
      if (width == depth && width == 8 && sign == FALSE) {
        format = SND_PCM_FORMAT_MU_LAW;
      } else {
        return FALSE;
      }
    } else if (law == 2) { /* a law, ug. */
      if (width == depth && width == 8 && sign == FALSE) {
        format = SND_PCM_FORMAT_A_LAW;
      }
    } else {
      return FALSE;
    }
  } else if (strcmp (format_name, "float") == 0) {
    const gchar *layout;

    if (!gst_caps_get_string (caps, "layout", &layout))
      return FALSE;

    if (strcmp (layout, "gfloat") == 0) {
      format = SND_PCM_FORMAT_FLOAT;
    } else {
      return FALSE;
      /* you need doubles? jeez... */
    }
  } else {
    return FALSE;
  }

  this->format = format;
  if (!gst_caps_get (caps, "rate", &this->rate, "channels", &channels, NULL))
    return FALSE;

  if (this->data_interleaved)
    this->channels = channels;
  else if (channels != 1)
    return FALSE;

  return TRUE;
}

/* caps are so painful sometimes. */
static GstCaps *
gst_alsa_caps (GstAlsa *this)
{
  gint law, endianness, width, depth;
  gboolean sign;
  GstProps *props;

  g_return_val_if_fail (this != NULL && this->handle != NULL, NULL);

  if (this->format == SND_PCM_FORMAT_FLOAT) {
    props = gst_props_new ("format",   GST_PROPS_STRING ("float"),
                           "layout",   GST_PROPS_STRING ("gfloat"),
                           "rate",     GST_PROPS_INT (this->rate),
                           "channels", GST_PROPS_INT ((this->data_interleaved ? this->channels : 1)),
                           NULL);
  } else {
    /* we'll just have to assume int, i don't feel like checking */
    if (this->format == SND_PCM_FORMAT_MU_LAW) {
      law = 1;
      width = 8; sign = FALSE; endianness = 0;
    } else if (this->format == SND_PCM_FORMAT_A_LAW) {
      law = 2;
      width = 8; sign = FALSE; endianness = 0;
    } else {
      law = 0;
      if (this->format == SND_PCM_FORMAT_S8) {
        width = 8; sign = TRUE; endianness = 0;
      } else if (this->format == SND_PCM_FORMAT_U8) {
        width = 8; sign = FALSE; endianness = 0;
      } else if (this->format == SND_PCM_FORMAT_S16_LE) {
        width = 16; sign = TRUE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_S16_BE) {
        width = 16; sign = TRUE; endianness = G_BIG_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U16_LE) {
        width = 16; sign = FALSE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U16_BE) {
        width = 16; sign = FALSE; endianness = G_BIG_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_S24_LE) {
        width = 24; sign = TRUE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_S24_BE) {
        width = 24; sign = TRUE; endianness = G_BIG_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U24_LE) {
        width = 24; sign = FALSE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U24_BE) {
        width = 24; sign = FALSE; endianness = G_BIG_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_S32_LE) {
        width = 32; sign = TRUE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_S32_BE) {
        width = 32; sign = TRUE; endianness = G_BIG_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U32_LE) {
        width = 32; sign = FALSE; endianness = G_LITTLE_ENDIAN;
      } else if (this->format == SND_PCM_FORMAT_U32_BE) {
        width = 32; sign = FALSE; endianness = G_BIG_ENDIAN;
      } else {
        g_error ("Unknown audio format %u", this->format);
        return NULL;
      }
    }
    depth = width;
    props = gst_props_new ("format",     GST_PROPS_STRING ("int"),
                           "rate",       GST_PROPS_INT (this->rate),
                           "channels",   GST_PROPS_INT ((this->data_interleaved ? this->channels : 1)),
                           "law",        GST_PROPS_INT (law),
                           "endianness", GST_PROPS_INT (endianness),
                           "signed",     GST_PROPS_BOOLEAN (sign),
                           "width",      GST_PROPS_INT (width),
                           "depth",      GST_PROPS_INT (depth),
                           NULL);
  }

  return gst_caps_new ("alsasrc", "audio/raw", props);
}

/* Negotiates the caps, "borrowed" from gstosssink.c */
GstPadLinkReturn
gst_alsa_link (GstPad *pad, GstCaps *caps)
{
  GstAlsa *this;
  gboolean need_mmap;

  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (pad != NULL, GST_PAD_LINK_REFUSED);

  this = GST_ALSA (gst_pad_get_parent (pad));

  if (GST_CAPS_IS_FIXED (caps)) {
    if (this->handle == NULL)
      if (!gst_alsa_open_audio (this))
        return GST_PAD_LINK_REFUSED;

    if (gst_alsa_parse_caps (this, caps)) {
      need_mmap = this->mmap_open;

      /* sync the params */
      if (GST_FLAG_IS_SET (this, GST_ALSA_RUNNING)) gst_alsa_stop_audio (this);
      if (GST_FLAG_IS_SET (this, GST_ALSA_OPEN))    gst_alsa_close_audio (this);

      /* FIXME send out another caps if nego fails */

      if (!gst_alsa_open_audio (this))  return GST_PAD_LINK_REFUSED;
      if (!gst_alsa_start_audio (this)) return GST_PAD_LINK_REFUSED;

      if (need_mmap && !gst_alsa_get_channel_addresses (this))
        return GST_PAD_LINK_REFUSED;

      return GST_PAD_LINK_OK;
    }

    return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_DELAYED;
}

static GstElementStateReturn
gst_alsa_change_state (GstElement *element)
{
  GstAlsa *this;
  GList *l;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_ALSA (element);

  switch (GST_STATE_PENDING (element)) {
  case GST_STATE_NULL:
    if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING)) gst_alsa_stop_audio (this);
    if (GST_FLAG_IS_SET (element, GST_ALSA_OPEN))    gst_alsa_close_audio (this);

    /* clear out bytestreams as well. */
    l = this->pads;
    while (l) {
      if (GST_ALSA_PAD (l)->bs)
        gst_bytestream_destroy (GST_ALSA_PAD (l)->bs);
      l = l->next;
    }

    break;

  case GST_STATE_READY:
    break;

  case GST_STATE_PAUSED:
    if (GST_FLAG_IS_SET (element, GST_ALSA_OPEN) == FALSE)
      if (!gst_alsa_open_audio (this))
        return GST_STATE_FAILURE;

    if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING))
      gst_alsa_stop_audio (this);

    break;

  case GST_STATE_PLAYING:
    if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING) == FALSE)
      if (!gst_alsa_start_audio (this))
        return GST_STATE_FAILURE;

    break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*** AUDIO PROCESSING *********************************************************/

/* shamelessly stolen from pbd's audioengine and jack alsa_driver. thanks,
   paul! */
static void
gst_alsa_loop (GstElement *element)
{
  GstAlsa *this = GST_ALSA (element);

  g_return_if_fail (this != NULL);

  while (1) {
    if (snd_pcm_wait (this->handle, 1000) < 0) {
      if (errno == EINTR) {
        /* this happens mostly when run
         * under gdb, or when exiting due to a signal */
        g_print ("EINTR\n");
        if (gst_element_interrupt (element))
          break;
        else
          continue;
      }

      g_warning ("error waiting for alsa pcm: (%d: %s)", errno, strerror (errno));
      return;
    }

    this->avail = snd_pcm_avail_update (this->handle);
    DEBUG ("snd_pcm_avail_update() = %d", (int) this->avail);

    if (this->avail < 0) {
      if (this->avail == -EPIPE) {
        gst_alsa_xrun_recovery (this);
        this->avail = 0;
      } else {
        g_warning ("unknown ALSA avail_update return value (%d)", (int) this->avail);
        return;
      }
    }

    /* round down to nearest period_frames avail */
    this->avail -= this->avail % this->period_frames;

    DEBUG ("snd_pcm_avail_update(), rounded down = %d", (int) this->avail);

    /* the available bytes might not be contiguous */
    while (this->avail > 0) {
      if (!gst_alsa_get_channel_addresses (this)) {
        g_error ("could not get channels");
        return;
      }

      if (! (this->mute && this->stream == SND_PCM_STREAM_PLAYBACK))
        if (!this->process (this, this->avail)) {
          g_warning ("alsa: something happened while processing audio");
          return;
        }

      /* releasing channel addresses sets this->avail to 0. */
      if (this->mmap_open)
        gst_alsa_release_channel_addresses (this);
    }

    if (gst_element_interrupt (element))
      break;
  }
}

static void
gst_alsa_xrun_recovery (GstAlsa *this)
{
  snd_pcm_status_t *status;
  gint err;

  snd_pcm_status_alloca (&status);

  if ((err = snd_pcm_status (this->handle, status)) < 0)
    g_warning ("status error: %s", snd_strerror (err));

  if (snd_pcm_status_get_state (status) == SND_PCM_STATE_XRUN) {
    struct timeval now, diff, tstamp;

    gettimeofday (&now, 0);
    snd_pcm_status_get_trigger_tstamp (status, &tstamp);
    timersub (&now, &tstamp, &diff);
    g_warning ("alsa: xrun of at least %.3f msecs", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
  }

  gst_alsa_stop_audio (this);

  /* increase the period count to prevent further xruns (at the cost of
     increased latency and memory usage). only do this if it's allowed. */
  if ((this->period_count <= 32) && this->autorecover) {
    this->period_count *= 2;
    this->buffer_frames = this->period_count * this->period_frames;
    gst_alsa_set_params(this);
  }

  gst_alsa_start_audio (this);
}

static gboolean
gst_alsa_src_process (GstAlsa *this, snd_pcm_uframes_t frames)
{
  GstAlsaPad *pad = NULL;
  GstBuffer *buf;
  GstCaps *caps;
  GList *l;
  guint32 channel;
  gint unit;

  static gboolean caps_set = FALSE;

  /* let's get on the caps-setting merry-go-round! */
  if (!caps_set) {
    caps = gst_alsa_caps (this);
    l = this->pads;
    while (l) {
      if (gst_pad_try_set_caps (GST_ALSA_PAD (l)->pad, caps) <= 0) {
        g_print ("setting caps (%p) in source (%p) failed\n", caps, this);
        sleep (1);
        return FALSE;
      }

      l = l->next;
    }
    caps_set = TRUE;
  }

  unit = this->sample_bytes * (this->data_interleaved ? this->channels : 1);

  /* pull the same amount of data into each pad. create an outgoing buffer with
     the data, and push the buffer. lather, rinse, repeat. */
  while (frames) {
    l = this->pads;
    while (l) {
      pad = GST_ALSA_PAD (l);

      if (!pad->buf)
        pad->buf = g_malloc (this->period_frames * unit);

      channel = (pad->channel > 0) ? pad->channel - 1 : 0;
      memcpy (pad->buf + pad->offset * unit, this->access_addr[channel],
              MIN (frames, this->period_frames - pad->offset) * unit);

      pad->offset += MIN (frames, this->period_frames - pad->offset);

      if (pad->offset >= this->period_frames) {
        g_assert (pad->offset <= this->period_frames);
        buf = gst_buffer_new ();
        GST_BUFFER_DATA (buf) = pad->buf;
        GST_BUFFER_SIZE (buf) = this->period_frames * unit;
        GST_BUFFER_MAXSIZE (buf) = this->period_frames * unit;
        gst_pad_push (pad->pad, buf);
        pad->buf = NULL;
        pad->offset = 0;
      }
      l = l->next;
    }
    /* all pads should have the same amount of data, shouldn't matter which one
       we use for updating output frames ... */
    frames -= MIN (frames, this->period_frames - pad->offset);
  }

  return TRUE;
}

static gboolean
gst_alsa_sink_process (GstAlsa *this, snd_pcm_uframes_t frames)
{
  guint8 *peeked;
  guint32 len, channel, num_peeked = 0;
  gboolean muted;
  GstAlsaPad *pad;
  GList *l;

  /* we need to check sample bytes here because the sample_bytes will change,
     probably, when caps are set, which will occur after the first
     bytestream_peek. we underestimate the amount of data we will need by
     peeking "frames" bytes only. */

  if (!this->sample_bytes) {
    pad = GST_ALSA_PAD (this->pads);

    if (!pad->bs)
      pad->bs = gst_bytestream_new (pad->pad);

    while (num_peeked == 0) {
      num_peeked = gst_bytestream_peek_bytes (pad->bs, &peeked, frames);
      if (num_peeked == 0) {
        gst_alsa_sink_check_event (this, pad);
      } else if (num_peeked < frames) {
        g_warning ("could not make initial pull of %d bytes on pad %s:%s",
                   (int) frames, GST_DEBUG_PAD_NAME (pad->pad));
        gst_element_set_eos (GST_ELEMENT (this));
        return FALSE;
      }
    }

    if (!this->sample_bytes) {
      g_critical ("alsa plugin requires a pipeline that can adequately set caps.");
      return FALSE;
    }
  }

  len = frames * this->channels * this->sample_bytes;

  /* write len bytes out to each of our pads' mmap_area pointers. */
  l = this->pads;
  while (l) {
    pad = GST_ALSA_PAD (l);

    channel = (pad->channel > 0) ? pad->channel - 1 : 0;
    muted = (1 << channel) && this->mute;

    if (!pad->bs)
      pad->bs = gst_bytestream_new (pad->pad);

    num_peeked = gst_bytestream_peek_bytes (pad->bs, &peeked, len);
    if (num_peeked == 0) {
      gst_alsa_sink_check_event (this, pad);
    } else if (peeked && this->access_addr[channel] && (! muted)) {
      memcpy (this->access_addr[channel], peeked, num_peeked);
      gst_bytestream_flush (pad->bs, num_peeked);
    } else {
      g_warning ("error while writing %u bytes (peeked into %p) to %p",
                 num_peeked, peeked, this->access_addr[channel]);
    }

    l = l->next;
  }

  return TRUE;
}

static void
gst_alsa_sink_check_event (GstAlsa *this, GstAlsaPad *pad)
{
  GstEvent *event = NULL;
  guint8 *peeked;
  guint32 avail, channel, num_peeked = 0;

  gst_bytestream_get_status (pad->bs, &avail, &event);

  if (event) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      channel = (pad->channel > 0) ? pad->channel - 1 : 0;
      num_peeked = gst_bytestream_peek_bytes (pad->bs, &peeked, avail);
      if (num_peeked && peeked)
        memcpy (this->access_addr[channel], peeked, num_peeked);

      gst_element_set_eos (GST_ELEMENT (this));
      gst_event_unref (event);

      /* FIXME: remove this alsa pad from the element */
    } else {
      g_warning ("GstAlsaSink: got an unknown event (Type: %d)", GST_EVENT_TYPE (event));
    }
  } else {
    /* the element at the top of the chain did not emit an eos event. */
    g_assert_not_reached ();
  }
}

/*** AUDIO SETUP / START / STOP ***********************************************/

/* taken more or less from pbd's audioengine code */
static gboolean
gst_alsa_set_params (GstAlsa *this)
{
  snd_pcm_sw_params_t *sw_param;
  snd_pcm_hw_params_t *hw_param;
  snd_pcm_access_mask_t *mask;
  gint ret;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  g_print ("Preparing channel: %s %dHz, %d channels\n",
           snd_pcm_format_name (this->format), this->rate, this->channels);

  snd_pcm_hw_params_alloca (&hw_param);
  snd_pcm_sw_params_alloca (&sw_param);

  ret = snd_pcm_hw_params_any (this->handle, hw_param);
  if (ret < 0) {
    g_warning ("Broken configuration for this PCM: no configurations available");
    return FALSE;
  }

  if ((ret = snd_pcm_hw_params_set_periods_integer (this->handle, hw_param)) < 0) {
    g_warning ("cannot restrict period size to integral value.");
    return FALSE;
  }

  mask = alloca (snd_pcm_access_mask_sizeof ());
  snd_pcm_access_mask_none (mask);

  if (this->data_interleaved)
    snd_pcm_access_mask_set (mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);

  snd_pcm_access_mask_set (mask, SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
  ret = snd_pcm_hw_params_set_access_mask (this->handle, hw_param, mask);
  if (ret < 0) {
    g_warning ("the gstreamer alsa plugin does not support your hardware.");
    return FALSE;
  }

  if (this->format != SND_PCM_FORMAT_UNKNOWN) {
    ret = snd_pcm_hw_params_set_format (this->handle, hw_param, this->format);
    if (ret < 0) {
      g_warning ("Sample format (%s) not available: %s",
                 snd_pcm_format_name (this->format), snd_strerror (ret));
      return FALSE;
    }
    this->sample_bytes = snd_pcm_format_physical_width (this->format) / 8;
  }

  ret = snd_pcm_hw_params_set_channels (this->handle, hw_param, this->channels);
  if (ret < 0) {
    g_warning ("Channels count (%d) not available: %s", this->channels, snd_strerror (ret));
    return FALSE;
  }
  this->channels = snd_pcm_hw_params_get_channels (hw_param);

  if (this->rate) {
    ret = snd_pcm_hw_params_set_rate (this->handle, hw_param, this->rate, 0);
    if (ret < 0) {
      g_warning ("error setting rate (%d): %s", this->rate, snd_strerror (ret));
      return FALSE;
    }
  }

  if (this->period_count) {
    ret = snd_pcm_hw_params_set_periods (this->handle, hw_param, this->period_count, 0);
    if (ret < 0) {
      g_warning ("error setting period count minimum (%d): %s",
                 this->period_count, snd_strerror (ret));
      return FALSE;
    }
  }

  if (this->period_frames) {
    ret = snd_pcm_hw_params_set_period_size (this->handle, hw_param, this->period_frames, 0);
    if (ret < 0) {
      g_warning ("error setting period in frames (%d): %s",
                 this->period_frames, snd_strerror (ret));
      return FALSE;
    }
  }

  if (this->buffer_frames) {
    ret = snd_pcm_hw_params_set_buffer_size (this->handle, hw_param, this->buffer_frames);
    if (ret < 0) {
      g_warning ("error setting buffer size (%d): %s", this->buffer_frames, snd_strerror (ret));
      return FALSE;
    }
  }

  ret = snd_pcm_hw_params (this->handle, hw_param);
  if (ret < 0) {
    g_warning ("could not set hw params: %s", snd_strerror (ret));
    snd_pcm_hw_params_dump (hw_param, this->out);
    return FALSE;
  }

  if (!this->rate)
    this->rate = snd_pcm_hw_params_get_rate (hw_param, 0);
  if (!this->format)
    this->format = snd_pcm_hw_params_get_format (hw_param);
  if (!this->period_count)
    this->period_count = snd_pcm_hw_params_get_periods (hw_param, 0);
  if (!this->period_frames)
    this->period_frames = snd_pcm_hw_params_get_period_size (hw_param, 0);
  if (!this->buffer_frames)
    this->buffer_frames = snd_pcm_hw_params_get_buffer_size (hw_param);
  if (this->buffer_frames != this->period_count * this->period_frames)
    g_critical ("buffer size != period size * number of periods, unexpected things may happen!");

  snd_pcm_sw_params_current (this->handle, sw_param);

  ret = snd_pcm_sw_params_set_start_threshold (this->handle, sw_param, ~0U);
  if (ret < 0) {
    g_warning ("could not set start mode: %s", snd_strerror (ret));
    return FALSE;
  }

  ret = snd_pcm_sw_params_set_stop_threshold (this->handle, sw_param, this->buffer_frames);
  if (ret < 0) {
    g_warning ("could not set stop mode: %s", snd_strerror (ret));
    return FALSE;
  }

  ret = snd_pcm_sw_params_set_silence_threshold (this->handle, sw_param, 0);
  if (ret < 0) {
    g_warning ("could not set silence threshold: %s", snd_strerror (ret));
    return FALSE;
  }

  ret = snd_pcm_sw_params_set_silence_size (this->handle, sw_param, this->buffer_frames);
  if (ret < 0) {
    g_warning ("could not set silence size: %s", snd_strerror (ret));
    return FALSE;
  }

  ret = snd_pcm_sw_params_set_avail_min (this->handle, sw_param, this->period_frames);
  if (ret < 0) {
    g_warning ("could not set avail min: %s", snd_strerror (ret));
    return FALSE;
  }

  ret = snd_pcm_sw_params (this->handle, sw_param);
  if (ret < 0) {
    g_warning ("could not set sw_params: %s", snd_strerror (ret));
    return FALSE;
  }

  if (this->debug)
    snd_pcm_dump (this->handle, this->out);

  this->access_interleaved = !(snd_pcm_hw_params_get_access (hw_param) ==
                               SND_PCM_ACCESS_MMAP_NONINTERLEAVED);

  if (this->access_interleaved) {
    this->interleave_unit = this->sample_bytes;
    this->interleave_skip = this->interleave_unit * this->channels;
  } else {
    this->interleave_unit = 0;        /* not used */
    this->interleave_skip = this->sample_bytes;
  }

  if (this->access_addr)
    g_free (this->access_addr);
  this->access_addr = g_new0 (char *, this->channels);

  return TRUE;
}

static gboolean
gst_alsa_open_audio (GstAlsa *this)
{
  gint ret;

  g_assert (this != NULL);

  if (this->handle)
    gst_alsa_close_audio (this);

  g_print ("Opening alsa device \"%s\" for %s...\n", this->device,
           this->stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture");

  ret = snd_output_stdio_attach (&this->out, stdout, 0);
  if (ret < 0) {
    g_print ("error opening log output: %s\n", snd_strerror (ret));
    return FALSE;
  }

  /* blocking i/o */
  if ((ret = snd_pcm_open (&this->handle, this->device, this->stream, 0))) {
    g_print ("error opening pcm device %s: %s\n", this->device, snd_strerror (ret));
    return FALSE;
  }

  if (gst_alsa_set_params (this) == FALSE) {
    gst_alsa_close_audio (this);
    return FALSE;
  }

  GST_FLAG_SET (this, GST_ALSA_OPEN);
  return TRUE;
}


static gboolean
gst_alsa_start_audio (GstAlsa *this)
{
  gint err;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  if ((err = snd_pcm_prepare (this->handle)) < 0) {
    g_warning ("channel prepare failed: %s", snd_strerror (err));
    return FALSE;
  }

  this->avail = snd_pcm_avail_update (this->handle);

  if (this->stream == SND_PCM_STREAM_PLAYBACK && this->avail != this->buffer_frames) {
    g_warning ("full buffer not available at start");
    return FALSE;
  }

  if (!gst_alsa_get_channel_addresses (this))
    return FALSE;

  gst_alsa_release_channel_addresses (this);

  if ((err = snd_pcm_start (this->handle)) < 0) {
    g_warning ("could not start audio: %s", snd_strerror (err));
    return FALSE;
  }

  GST_FLAG_SET (this, GST_ALSA_RUNNING);
  return TRUE;
}

static void
gst_alsa_stop_audio (GstAlsa *this)
{
  gint err;

  g_assert (this != NULL);

  g_return_if_fail (this != NULL);
  g_return_if_fail (this->handle != NULL);

  if (this->mmap_open)
    gst_alsa_release_channel_addresses (this);

  if (this->stream == SND_PCM_STREAM_PLAYBACK && (err = snd_pcm_drop (this->handle)) < 0) {
    g_warning ("channel flush failed: %s", snd_strerror (err));
    return;
  }

  GST_FLAG_UNSET (this, GST_ALSA_RUNNING);
}

static void
gst_alsa_close_audio (GstAlsa *this)
{
  g_return_if_fail (this != NULL);
  g_return_if_fail (this->handle != NULL);

  snd_pcm_close (this->handle);

  this->handle = NULL;

  GST_FLAG_UNSET (this, GST_ALSA_OPEN);
}

static gboolean
gst_alsa_get_channel_addresses (GstAlsa *this)
{
  guint32 err, i;
  const snd_pcm_channel_area_t *a;

  g_return_val_if_fail (this->mmap_open == FALSE, FALSE);

  if ((err = snd_pcm_mmap_begin (this->handle, &this->mmap_areas, &this->offset, &this->avail)) < 0) {
    g_warning ("gstalsa: mmap failed: %s", snd_strerror (err));
    return FALSE;
  }

  GST_DEBUG (0, "got %d mmap'd frames", (int) this->avail);

  for (i = 0; i < this->channels; i++) {
    a = &this->mmap_areas[i];
    this->access_addr[i] =
      (char *) a->addr + ((a->first + a->step * this->offset) / 8);
  }

  this->mmap_open = TRUE;

  return TRUE;
}

static void
gst_alsa_release_channel_addresses (GstAlsa * this)
{
  guint32 err, i;

  g_return_if_fail (this->mmap_open == TRUE);

  GST_DEBUG (0, "releasing mmap'd data region: %d frames", (int) this->avail);

  if ((err = snd_pcm_mmap_commit (this->handle, this->offset, this->avail)) < 0) {
    g_warning ("gstalsa: mmap commit failed: %s", snd_strerror (err));
    return;
  }

  for (i = 0; i < this->channels; i++)
    this->access_addr[i] = NULL;

  this->mmap_open = FALSE;
  this->avail = 0;
}

/*** GSTREAMER PLUGIN *********************************************************/

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;

  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  factory = gst_element_factory_new ("alsasrc", GST_TYPE_ALSA_SRC, &gst_alsa_src_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_add_pad_template (factory, gst_alsa_src_pad_factory ());
  gst_element_factory_add_pad_template (factory, gst_alsa_src_request_pad_factory ());
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  factory = gst_element_factory_new ("alsasink", GST_TYPE_ALSA_SINK, &gst_alsa_sink_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_add_pad_template (factory, gst_alsa_sink_pad_factory ());
  gst_element_factory_add_pad_template (factory, gst_alsa_sink_request_pad_factory ());
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  gst_plugin_set_longname (plugin, "ALSA plugin library");

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "alsa",
  plugin_init
};
