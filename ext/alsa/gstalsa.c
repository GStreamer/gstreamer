/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

/* error checking for standard alsa functions */
#define SIMPLE_ERROR_CHECK(value) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    return FALSE; \
  } \
}G_STMT_END
#ifdef G_HAVE_ISO_VARARGS
#define ERROR_CHECK(value, ...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    g_warning ( __VA_ARGS__, snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END
#elif defined(G_HAVE_GNUC_VARARGS)
#define ERROR_CHECK(value, args...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    g_warning ( ## args, snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END
#else
#define ERROR_CHECK(value, args...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    g_warning (snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END
#endif
/* elementfactory information */
static GstElementDetails gst_alsa_sink_details = {
  "Alsa Sink",
  "Sink/Audio",
  "LGPL",
  "Output to a sound card via ALSA",
  VERSION,
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>, "
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
  "(C) 2001-2003"
};

/* elementfactory information */
static GstElementDetails gst_alsa_src_details = {
  "Alsa Src",
  "Source/Audio",
  "LGPL",
  "Read from a sound card via ALSA",
  VERSION,
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>, "
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
  "(C) 2001-2003"
};

/* GObject functions */
static void gst_alsa_class_init (GstAlsaClass *klass);
static void gst_alsa_init (GstAlsa *this);
static void gst_alsa_dispose (GObject *object);
static void gst_alsa_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_alsa_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* GStreamer functions for pads and state changing */
static GstPadTemplate   *gst_alsa_src_pad_factory ();
static GstPadTemplate   *gst_alsa_src_request_pad_factory ();
static GstPadTemplate   *gst_alsa_sink_pad_factory ();
static GstPadTemplate   *gst_alsa_sink_request_pad_factory ();

static GstPad           *gst_alsa_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *name);
static GstPadLinkReturn  gst_alsa_link (GstPad *pad, GstCaps *caps);
static GstCaps          *gst_alsa_get_caps (GstPad *pad, GstCaps *caps);
static GstCaps          *gst_alsa_caps (snd_pcm_format_t format, gint rate, gint channels);

static GstBufferPool    *gst_alsa_src_get_buffer_pool (GstPad *pad);

static GstElementStateReturn gst_alsa_change_state (GstElement *element);

/* audio processing functions */
static int      gst_alsa_do_mmap (GstAlsa *this, snd_pcm_sframes_t *avail);

static void     gst_alsa_sink_loop (GstElement *element);
static void     gst_alsa_src_loop (GstElement *element);
static void     gst_alsa_xrun_recovery (GstAlsa *this);

static gboolean gst_alsa_sink_check_event (GstAlsa *this, gint pad_nr);

/* alsa setup / start / stop functions */
static gboolean gst_alsa_probe_hw_params (GstAlsa *this, GstAlsaFormat *format);
static gboolean gst_alsa_set_hw_params (GstAlsa *this);
static gboolean gst_alsa_set_sw_params (GstAlsa *this);

static gboolean gst_alsa_open_audio (GstAlsa *this);
static gboolean gst_alsa_start_audio (GstAlsa *this);
static gboolean gst_alsa_drain_audio (GstAlsa *this);
static gboolean gst_alsa_stop_audio (GstAlsa *this);
static gboolean gst_alsa_close_audio (GstAlsa *this);

/*** TYPE FUNCTIONS ***********************************************************/

#define GST_TYPE_ALSA_FORMAT (gst_alsa_format_get_type())
static GType
gst_alsa_format_get_type (void)
{
  static GType type = 0;
  static GEnumValue *values = NULL;
  gint i;

  if (values == NULL) {
    /* the three: for -1, 0, and the terminating NULL */
    values = g_new0 (GEnumValue, SND_PCM_FORMAT_LAST + 1);
 
    for (i = -1; i <= SND_PCM_FORMAT_LAST; i++) {
      values[i + 1].value = i; /* UNKNOWN is -1 */
      values[i + 1].value_name = g_strdup_printf ("%d", i);
      values[i + 1].value_nick = g_strdup (snd_pcm_format_name ((snd_pcm_format_t) i));
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
  ARG_PERIODSIZE,
  ARG_BUFFERSIZE,
  ARG_DEBUG,
  ARG_AUTORECOVER,
  ARG_MMAP
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

  object_class->dispose = gst_alsa_dispose;
  object_class->get_property = gst_alsa_get_property;
  object_class->set_property = gst_alsa_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
    g_param_spec_string ("device", "Device", "Alsa device, as defined in an asoundrc",
                         "default", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  /* the next 3 are only settable on srcs */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FORMAT,
    g_param_spec_enum ("format", "Format", "PCM audio format",
                       GST_TYPE_ALSA_FORMAT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHANNELS,
    g_param_spec_int ("channels", "Channels", "Number of channels",
                      1, GST_ALSA_MAX_CHANNELS, 2, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE,
    g_param_spec_int ("rate", "Rate", "Sample rate, in Hz",
                      GST_ALSA_MIN_RATE, GST_ALSA_MAX_RATE, 44100, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PERIODCOUNT,
    g_param_spec_int ("period-count", "Period count", "Number of hardware buffers to use",
                      2, 64, 2, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PERIODSIZE,
    g_param_spec_int ("period-size", "Period size", "Number of frames (samples on each channel) in one hardware period",
                      64, 8192, 8192, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERSIZE,
    g_param_spec_int ("buffer-size", "Buffer size", "Number of frames the hardware buffer can hold",
                      128, 65536, 16384, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_AUTORECOVER,
    g_param_spec_boolean ("autorecover", "Automatic xrun recovery", "When TRUE tries to reduce processor load on xruns",
                          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MMAP,
    g_param_spec_boolean ("mmap", "Use mmap'ed access", "Wether to use mmap (faster) or standard read/write (more compatible)",
                          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  element_class->change_state = gst_alsa_change_state;
  element_class->request_new_pad = gst_alsa_request_new_pad;
}

static void
gst_alsa_init (GstAlsa *this)
{
  gint i;
  /* init values */
  this->handle = NULL;
  this->transmit = NULL;

  GST_FLAG_SET (this, GST_ELEMENT_THREAD_SUGGESTED);

  if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC) {
    this->stream = SND_PCM_STREAM_CAPTURE;
    gst_element_set_loop_function (GST_ELEMENT (this), gst_alsa_src_loop);
    this->pads[0].pad = gst_pad_new_from_template (gst_alsa_src_pad_factory (), "src");
    this->pads[0].bs = NULL;
    gst_pad_set_bufferpool_function(this->pads[0].pad, gst_alsa_src_get_buffer_pool);

    /* set the rate to a sensible value. we can't have gobject construct this
       manually since it only really makes sense on src elements. the rate can
       be changed later through the gobject set property function. */
  } else if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SINK) {
    this->stream = SND_PCM_STREAM_PLAYBACK;
    gst_element_set_loop_function (GST_ELEMENT (this), gst_alsa_sink_loop);
    this->pads[0].pad = gst_pad_new_from_template (gst_alsa_sink_pad_factory (), "sink");
    this->pads[0].bs = gst_bytestream_new (this->pads[0].pad);
  }

  gst_element_add_pad (GST_ELEMENT (this), this->pads[0].pad);
  for (i = 1; i < GST_ALSA_MAX_CHANNELS; i++) {
    this->pads[i].pad = NULL;
  }

  gst_pad_set_link_function (this->pads[0].pad, gst_alsa_link);
  gst_pad_set_getcaps_function (this->pads[0].pad, gst_alsa_get_caps);
}

static void
gst_alsa_dispose (GObject *object)
{
  gint i;
  GstAlsa *this = GST_ALSA (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  for (i = 0; i < ((GstElement *) this)->numpads; i++) {
    if (this->pads[i].bs)
      gst_bytestream_destroy (this->pads[i].bs);
  }
}
static void
gst_alsa_set_property (GObject *object, guint prop_id, const GValue *value,
                       GParamSpec *pspec)
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
  case ARG_FORMAT:
    /* setting this property only makes sense on sources */
    if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC)
      this->format->format = g_value_get_enum (value);
    break;
  case ARG_CHANNELS:
    /* setting this property only makes sense on sources */
    if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC)
      this->format->channels = g_value_get_int (value);
    break;
  case ARG_RATE:
    /* setting this property only makes sense on sources */
    if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC)
      this->format->rate = g_value_get_int (value);
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
  default:
    GST_DEBUG (0, "Unknown arg");
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
    g_value_set_enum (value, this->format ? this->format->format : SND_PCM_FORMAT_UNKNOWN);
    break;
  case ARG_CHANNELS:
    g_value_set_int (value, this->format ? this->format->channels : 2);
    break;
  case ARG_RATE:
    g_value_set_int (value, this->format ? this->format->rate : 44100);
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
    template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, -1),
                                     NULL);

  return template;
}

static GstPadTemplate *
gst_alsa_src_request_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, 1),
                                     NULL);

  return template;
}

static GstPadTemplate *
gst_alsa_sink_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, -1),
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
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, 1),
                            NULL);

  return template;
}

static GstPad *
gst_alsa_request_new_pad (GstElement *element, GstPadTemplate *templ,
                          const gchar *name)
{
  GstAlsa *this;
  gint channel = 0;

  g_return_val_if_fail ((this = GST_ALSA (element)), NULL);
  g_return_val_if_fail (!GST_FLAG_IS_SET (element, GST_ALSA_RUNNING), NULL);

  if (name) {
    /* locate the channel number in the requested pad name. to do so look at
       where the % (which begins the %d) is in the template name. */
    channel = (gint) strtol (name + (strchr (templ->name_template, '%') -
                            templ->name_template), NULL, 0);
    if (channel < 1 || channel >= GST_ALSA_MAX_CHANNELS) {
      g_warning ("invalid channel requested. (%d)", channel);
      return NULL;
    }
  }

  /* make sure the requested channel is free. */
  if (channel > 0 || this->pads[channel].pad != NULL) {
    g_warning ("requested channel %d already in use.", channel);
    return NULL;
  }

  /* if the user doesn't care which channel, find the lowest channel number
     that's free. */
  if (channel == 0) {
    for (channel = 1; channel < GST_ALSA_MAX_CHANNELS; channel++) {
      if (this->pads[channel].pad != NULL)
        goto found_channel;
    }
    return NULL;
  }

found_channel:
  this->pads[channel].pad = gst_pad_new_from_template (templ, name);
  gst_pad_set_link_function (this->pads[channel].pad, gst_alsa_link);
  gst_pad_set_getcaps_function (this->pads[channel].pad, gst_alsa_get_caps);
  gst_element_add_pad (GST_ELEMENT (this), this->pads[channel].pad);
  if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SINK)
    this->pads[channel].bs = gst_bytestream_new (this->pads[channel].pad);
  else
    gst_pad_set_bufferpool_function(this->pads[channel].pad, gst_alsa_src_get_buffer_pool);

  return this->pads[channel].pad;
}

/* gets the matching alsa format or NULL if none matches */
static GstAlsaFormat *
gst_alsa_get_format (GstCaps *caps)
{
  const gchar *format_name;
  GstAlsaFormat *ret;

  if (!(ret = g_new (GstAlsaFormat, 1)))
    return NULL;

  /* we have to differentiate between int and float formats */
  if (!gst_caps_get_string (caps, "format", &format_name))
    goto error;

  if (strncmp (format_name, "int", 3) == 0) {
    gboolean sign;
    gint width, depth, endianness, law;

    /* extract the needed information from the caps */
    if (!gst_caps_get (caps,
                       "width", &width,
                       "depth", &depth,
                       "law", &law,
                       "signed", &sign,
                       NULL))
        goto error;
	
    /* extract endianness if needed */
    if (width > 8) {
      if (!gst_caps_get (caps,
                         "endianness", &endianness,
                         NULL))
        goto error;
      } else {
      endianness = G_BYTE_ORDER;
    }
    
    /* find corresponding alsa format */
    switch (law) {
      case 0: 
	ret->format = snd_pcm_build_linear_format (depth, width, sign ? 0 : 1, endianness == G_LITTLE_ENDIAN ? 0 : 1);
        break;
      case 1: 
        if (width == 8 && depth == 8 && sign == FALSE) {
          ret->format = SND_PCM_FORMAT_MU_LAW;
	  break;
        } else {
          goto error;
        }
      case 2: 
        if (width == 8 && depth == 8 && sign == FALSE) {
          ret->format = SND_PCM_FORMAT_A_LAW;
	  break;
        } else {
          goto error;
        }
      default: 
        goto error;
    }
  } else if (strncmp (format_name, "float", 5) == 0) {
    gchar *layout;
    gfloat intercept, slope;

    /* get layout */
    if (!gst_caps_get (caps, "layout", &layout,
                             "intercept", &intercept,
                             "slope", &slope,
                             NULL))
      goto error;
    if (intercept != 0.0f || slope != 1.0f) {
      goto error;
    }
    /* match layout to format wrt to endianness */
    if (strncmp (layout, "gfloat", 6) == 0) {
      if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
	ret->format = SND_PCM_FORMAT_FLOAT_LE;
      } else if (G_BYTE_ORDER == G_BIG_ENDIAN) {
        ret->format = SND_PCM_FORMAT_FLOAT_BE;
      } else {
        ret->format = SND_PCM_FORMAT_FLOAT;
      }
    } else if (strncmp (layout, "gdouble", 7) == 0) {
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
  }

  /* get rate and channels */
  if (!gst_caps_get (caps, "rate", &ret->rate,
                           "channels", &ret->channels,
                           NULL))
    goto error;
  
  return ret;
  
error:
  g_free (ret);
  return NULL;
}

static inline gboolean
gst_alsa_formats_match (GstAlsaFormat *one, GstAlsaFormat *two)
{
  if (one == two) return TRUE;
  if (one == NULL || two == NULL) return FALSE;
  return (one->format == two->format) && 
         (one->rate == two->rate) && 
         (one->channels == two->channels);
}
/* get props for a spec */
static GstProps *
gst_alsa_get_props (snd_pcm_format_t format)
{
  if (format == SND_PCM_FORMAT_A_LAW) {
    return gst_props_new ("format", GST_PROPS_STRING ("int"),
                          "law", GST_PROPS_INT(2),
                          "width", GST_PROPS_INT(8),
                          "depth", GST_PROPS_INT(8),
                          "signed", GST_PROPS_BOOLEAN (FALSE),
                          NULL);
  } else if (format == SND_PCM_FORMAT_MU_LAW) {
    return gst_props_new ("format", GST_PROPS_STRING ("int"),
                          "law", GST_PROPS_INT(1),
                          "width", GST_PROPS_INT(8),
                          "depth", GST_PROPS_INT(8),
                          "signed", GST_PROPS_BOOLEAN (FALSE),
                          NULL);
  } else if (snd_pcm_format_linear (format)) {
    /* int */
    GstProps *props = gst_props_new ("format", GST_PROPS_STRING ("int"),
                          "width", GST_PROPS_INT(snd_pcm_format_physical_width (format)),
                          "depth", GST_PROPS_INT(snd_pcm_format_width (format)),
                          "law", GST_PROPS_INT(0),
                          "signed", GST_PROPS_BOOLEAN (snd_pcm_format_signed (format) == 1 ? TRUE : FALSE),
                          NULL);
    /* endianness */
    if (snd_pcm_format_physical_width (format) > 8) {
      switch (snd_pcm_format_little_endian (format)) {
      case 0:
        gst_props_add_entry (props, gst_props_entry_new ("endianness", GST_PROPS_INT (G_BIG_ENDIAN)));
        break;
      case 1:
        gst_props_add_entry (props, gst_props_entry_new ("endianness", GST_PROPS_INT (G_LITTLE_ENDIAN)));
        break;
      default:
        g_warning("ALSA: Unknown byte order in sound driver. Continuing by assuming system byte order.");
        gst_props_add_entry (props, gst_props_entry_new ("endianness", GST_PROPS_INT (G_BYTE_ORDER)));
        break;
      }
    }
    return props;
  } else if (snd_pcm_format_float (format)) {
    /* no float with non-platform endianness */
    if (!snd_pcm_format_cpu_endian (format))
      return NULL;

    return gst_props_new ("format", GST_PROPS_STRING ("float"),
                          "layout", GST_PROPS_STRING (snd_pcm_format_width (format) == 64 ? "gdouble" : "gfloat"),
                          "intercept", GST_PROPS_FLOAT (0),
                          "slope", GST_PROPS_FLOAT (1),
                          NULL);
  }
  return NULL;
}

static inline void
add_channels (GstProps *props, gint min_rate, gint max_rate, gint min_channels, gint max_channels) {
  if (min_rate < 0) {
    gst_props_add_entry (props, gst_props_entry_new ("rate", GST_PROPS_INT_RANGE (GST_ALSA_MIN_RATE, GST_ALSA_MAX_RATE)));
  } else if (max_rate < 0) {
    gst_props_add_entry (props, gst_props_entry_new ("rate", GST_PROPS_INT (min_rate)));
  } else {
    gst_props_add_entry (props, gst_props_entry_new ("rate", GST_PROPS_INT_RANGE (min_rate, max_rate)));
  }
  if (min_channels < 0) {
    gst_props_add_entry (props, gst_props_entry_new ("channels", GST_PROPS_INT_RANGE (1, GST_ALSA_MAX_CHANNELS)));
  } else if (max_channels < 0) {
    gst_props_add_entry (props, gst_props_entry_new ("channels", GST_PROPS_INT (min_channels)));
  } else {
    gst_props_add_entry (props, gst_props_entry_new ("channels", GST_PROPS_INT_RANGE (min_channels, max_channels)));
  }
}

/**
 * Get all available caps.
 * @format: SND_PCM_FORMAT_UNKNOWN for all formats, desired format else
 * @rate: allowed rates if < 0, else desired rate
 * @channels: all allowed values for channels if < 0, else desired channels
 */
static GstCaps *
gst_alsa_caps (snd_pcm_format_t format, gint rate, gint channels)
{
  GstCaps *ret_caps = NULL;

  if (format != SND_PCM_FORMAT_UNKNOWN) {
    /* there are some caps set already */
    GstProps *props = gst_alsa_get_props (format);
    /* we can never use a format we can't set caps for */
    g_assert (props != NULL);

    add_channels (props, rate, -1, channels, -1);
    ret_caps = gst_caps_new (g_strdup (snd_pcm_format_name (format)), "audio/raw", props);
  } else {
    int i;
    GstProps *props;

    for (i = 0; i <= SND_PCM_FORMAT_LAST; i++) {
      props = gst_alsa_get_props (i);
      /* can be NULL, because not all alsa formats can be specified as caps */
      if (props != NULL) {
        add_channels (props, rate, -1, channels, -1);
        ret_caps = gst_caps_append (ret_caps, gst_caps_new (g_strdup (snd_pcm_format_name (i)),
                                    "audio/raw", props));
      }
    }
  }

  return ret_caps;
}

/* Return better caps when device is open */
static GstCaps *
gst_alsa_get_caps (GstPad *pad, GstCaps *caps)
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
    return gst_pad_get_pad_template_caps (pad);
  
  snd_pcm_hw_params_alloca (&hw_params);
  ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params),
               "Broken configuration for this PCM: %s");

  if (((GstElement *) this)->numpads > 1) {
    min_channels = 1;
    max_channels = -1;
  } else {
    ERROR_CHECK (snd_pcm_hw_params_get_channels_min (hw_params, &min_rate),
                 "Coulödn't get minimum channel count for device %s: %s", this->device);
    ERROR_CHECK (snd_pcm_hw_params_get_channels_max (hw_params, &max_rate),
                 "Coulödn't get maximum channel count for device %s: %s", this->device);
    min_channels = min_rate;
    max_channels = max_rate > GST_ALSA_MAX_CHANNELS ? GST_ALSA_MAX_CHANNELS : max_rate;
  }

  ERROR_CHECK (snd_pcm_hw_params_get_rate_min (hw_params, &min_rate, &i),
               "Coulödn't get minimum rate for device %s: %s", this->device);
  min_rate = min_rate < GST_ALSA_MIN_RATE ? GST_ALSA_MIN_RATE : min_rate + i;
  ERROR_CHECK (snd_pcm_hw_params_get_rate_max (hw_params, &max_rate, &i),
               "Coulödn't get maximum rate for device %s: %s", this->device);
  max_rate = max_rate > GST_ALSA_MAX_RATE ? GST_ALSA_MAX_RATE : max_rate + i;
  
  snd_pcm_format_mask_alloca (&mask);
  snd_pcm_hw_params_get_format_mask (hw_params, mask);
  for (i = 0; i <= SND_PCM_FORMAT_LAST; i++) {
    if (snd_pcm_format_mask_test (mask, i)) {
      GstProps *props = gst_alsa_get_props (i);
      /* we can never use a format we can't set caps for */
      if (props != NULL) {
        add_channels (props, min_rate, max_rate, min_channels, max_channels);
        ret = gst_caps_append (ret, gst_caps_new (g_strdup (snd_pcm_format_name (i)),
                               "audio/raw", props));
      }
    }
  }

  return ret;
}
/* Negotiates the caps */
GstPadLinkReturn
gst_alsa_link (GstPad *pad, GstCaps *caps)
{
  GstAlsa *this;
  GstAlsaFormat *format;
  GstPadLinkReturn ret;

  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (pad != NULL, GST_PAD_LINK_REFUSED);

  this = GST_ALSA (gst_pad_get_parent (pad));

  if (GST_CAPS_IS_FIXED (caps)) {
    if (this->handle == NULL)
      if (!gst_alsa_open_audio (this))
        return GST_PAD_LINK_REFUSED;

    format = gst_alsa_get_format (caps);
    if (format == NULL)
      return GST_PAD_LINK_DELAYED;
    
    GST_DEBUG (GST_CAT_CAPS, "found format %s\n", snd_pcm_format_name (format->format));
    
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
        g_assert (this->pads[i].pad != NULL);
        if (this->pads[i].pad == pad)
	  continue;
        if (gst_pad_try_set_caps (this->pads[i].pad, gst_caps_ref (caps)) == GST_PAD_LINK_REFUSED) {
	  if (this->format) {
	    GstCaps *old = gst_alsa_caps (this->format->format, this->format->rate, this->format->channels);
	    for (--i; i >= 0; i--) {
              if (gst_pad_try_set_caps (this->pads[i].pad, gst_caps_ref (old)) == GST_PAD_LINK_REFUSED) {
	        gst_element_error (GST_ELEMENT (this), "error resetting caps to sane value");
	        gst_caps_unref (old);
                break;
	      }
	    }
            gst_caps_unref (old);
	  } else {
	    /* FIXME: unset caps on pads somehow */
	  }
          ret = GST_PAD_LINK_REFUSED;
	  goto out;
        }
      }
    
      GST_FLAG_UNSET (this, GST_ALSA_CAPS_NEGO);

      /* sync the params */
      if (GST_FLAG_IS_SET (this, GST_ALSA_RUNNING)) gst_alsa_stop_audio (this);
      g_free (this->format);
      this->format = format;
      if (!gst_alsa_start_audio (this)) {
        gst_element_error (GST_ELEMENT (this), "Probed format doesn't work");
        return GST_PAD_LINK_REFUSED;
      }
    }

    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_DELAYED;

out:
  g_free (format);
  GST_FLAG_UNSET (this, GST_ALSA_CAPS_NEGO);
  return ret;
}

static GstBufferPool *
gst_alsa_src_get_buffer_pool (GstPad *pad)
{
  int width, bytes_per_frame;

  GstAlsa *this = GST_ALSA (gst_pad_get_parent (pad));

  width = snd_pcm_format_physical_width (this->format->format);
  bytes_per_frame = ( width / 8 ) * (GST_ELEMENT (this)->numpads == 1 ? this->format->channels : 1);

  /* FIXME : is this right ? constant size buffers are probably a good thing,
     but what if the size changes (e.g. during xrun autorecovery) ? */
  return gst_buffer_pool_get_default (this->period_size * bytes_per_frame,
                                      this->period_count);
}

static GstElementStateReturn
gst_alsa_change_state (GstElement *element)
{
  GstAlsa *this;
  gint i;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_ALSA (element);

  switch (GST_STATE_TRANSITION (element)) {
  case GST_STATE_NULL_TO_READY:
    if (!GST_FLAG_IS_SET (element, GST_ALSA_OPEN))
      if (!gst_alsa_open_audio (this))
        return GST_STATE_FAILURE;
    break;
  case GST_STATE_READY_TO_PAUSED:
    if (!GST_FLAG_IS_SET (element, GST_ALSA_RUNNING))
      if (!gst_alsa_start_audio (this))
        return GST_STATE_FAILURE;
    break;
  case GST_STATE_PAUSED_TO_PLAYING:
    if (snd_pcm_state (this->handle) == SND_PCM_STATE_PAUSED)
      snd_pcm_pause (this->handle, 0);
    break;
  case GST_STATE_PLAYING_TO_PAUSED:
    if (GST_ALSA_CAPS_IS_SET(this, GST_ALSA_CAPS_PAUSE)) {
      if (snd_pcm_state (this->handle) == SND_PCM_STATE_RUNNING)
        snd_pcm_pause (this->handle, 1);
      break;
    }
    /* if device doesn't know how to pause, we just stop */
  case GST_STATE_PAUSED_TO_READY:
    if (GST_FLAG_IS_SET (element, GST_ALSA_RUNNING))
      gst_alsa_drain_audio (this);
    /* clear format and pads */
    g_free (this->format);
    this->format = NULL;
    for (i = 0; i < element->numpads; i++) {
      if (this->pads[i].bs)
	gst_bytestream_reset (this->pads[i].bs);
    }
    break;
  case GST_STATE_READY_TO_NULL:
    if (GST_FLAG_IS_SET (element, GST_ALSA_OPEN))
      gst_alsa_close_audio (this);
    break;

  default:
    g_assert_not_reached();
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*** AUDIO PROCESSING *********************************************************/

static int
gst_alsa_do_mmap (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  snd_pcm_uframes_t offset;
  snd_pcm_channel_area_t *dst, *src, *areas;
  int i, err, width = snd_pcm_format_physical_width (this->format->format);

  /* areas points to the memory areas that belong to gstreamer. */
  areas = src = dst = calloc(this->format->channels, sizeof(snd_pcm_channel_area_t));

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    for (i = 0; i < this->format->channels; i++) {
      areas[i].addr = this->pads[0].data;
      areas[i].first = i * width;
      areas[i].step = this->format->channels * width;
    }
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      areas[i].addr = this->pads[i].data;
      areas[i].first = 0;
      areas[i].step = width;
    }
  }

  if ((err = snd_pcm_mmap_begin (this->handle, (
             const snd_pcm_channel_area_t **) (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC ? &src : &dst),
             &offset, avail)) < 0) {
    g_warning ("gstalsa: mmap failed: %s", snd_strerror (err));
    return -1;
  }

  if ((err = snd_pcm_areas_copy (dst, offset, src, 0, this->format->channels, *avail, this->format->format)) < 0) {
    snd_pcm_mmap_commit (this->handle, offset, 0);
    g_warning ("gstalsa: data copy failed: %s", snd_strerror (err));
    return -1;
  }
  if ((err = snd_pcm_mmap_commit (this->handle, offset, *avail)) < 0) {
    g_warning ("gstalsa: mmap commit failed: %s", snd_strerror (err));
    return -1;
  }

  return err;
}
static int
gst_alsa_do_read_write (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  void *channels[this->format->channels];
  int err, i;

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC) {
      err = snd_pcm_readi (this->handle, this->pads[0].data, *avail);
    } else {
      err = snd_pcm_writei (this->handle, this->pads[0].data, *avail);
    }
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      channels[i] = this->pads[i].data;
    }
    if (G_OBJECT_TYPE (this) == GST_TYPE_ALSA_SRC) {
      err = snd_pcm_readn (this->handle, channels, *avail);
    } else {
      err = snd_pcm_writen (this->handle, channels, *avail);
    }
  }
  /* error handling */
  if (err < 0) {
    if (err == -EPIPE) {
      gst_alsa_xrun_recovery (this);
      return 0;
    }
    g_warning ("error on data access: %s", snd_strerror (err));
  }
  return err;
}
inline static snd_pcm_sframes_t
gst_alsa_update_avail (GstAlsa *this)
{
  snd_pcm_sframes_t avail = snd_pcm_avail_update (this->handle);
  if (avail < 0) {
    if (avail == -EPIPE) {
      gst_alsa_xrun_recovery (this);
    } else {
      g_warning ("unknown ALSA avail_update return value (%d)", (int) avail);
    }
  }
  return avail;
}
/* returns TRUE, if the loop should go on */
inline static gboolean
gst_alsa_pcm_wait (GstAlsa *this)
{
  if (snd_pcm_state (this->handle) == SND_PCM_STATE_RUNNING) {
    if (snd_pcm_wait (this->handle, 1000) < 0) {
      if (errno == EINTR) {
        /* happens mostly when run under gdb, or when exiting due to a signal */
        GST_DEBUG (GST_CAT_PLUGIN_INFO, "got interrupted while waiting");
        if (gst_element_interrupt (GST_ELEMENT (this))) {
          return TRUE;
        } else {
          return FALSE;
        }
      }
      g_warning ("error waiting for alsa pcm: (%d: %s)", errno, strerror (errno));
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * error out or make sure we're in SND_PCM_STATE_RUNNING afterwards 
 * return FALSE if we're not
 */
inline static gboolean
gst_alsa_start (GstAlsa *this)
{
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "Starting playback");

  switch (snd_pcm_state(this->handle)) {
    case SND_PCM_STATE_XRUN:
      gst_alsa_xrun_recovery (this);
      return gst_alsa_start (this);
    case SND_PCM_STATE_PREPARED:
      ERROR_CHECK (snd_pcm_start(this->handle), "error starting playback: %s");
      break;
    case SND_PCM_STATE_PAUSED:
      ERROR_CHECK (snd_pcm_pause (this->handle, 0), "error unpausing: %s");
      break;
    case SND_PCM_STATE_RUNNING:
      break;
    default:
      /* it's a bug when we get here */
      g_assert_not_reached ();
      break;
  }
  return TRUE;
}
static void
gst_alsa_sink_loop (GstElement *element)
{
  snd_pcm_sframes_t avail, avail2, copied;
  gint i;
  gint bytes, num_bytes; /* per channel */
  GstAlsa *this = GST_ALSA (element);

  g_return_if_fail (this != NULL);

  /* caps nego: fetch 1 byte from every pad */
  if (this->format == NULL) {
    GST_DEBUG (GST_CAT_NEGOTIATION, "starting caps negotiation");
    for (i = 0; i < element->numpads; i++) {
      g_assert (this->pads[i].pad != NULL);
      do {
        num_bytes = gst_bytestream_peek_bytes (this->pads[i].bs, &this->pads[i].data, 1);
      } while (num_bytes == 0 && gst_alsa_sink_check_event (this, i));
      if (num_bytes == 0)
        return;
    }
    if (this->format == NULL) {
      gst_element_error (GST_ELEMENT (this), "alsasink: No caps available");
      return;
    }
  }

sink_restart:

  avail = gst_alsa_update_avail (this);
  if (avail == -EPIPE) goto sink_restart;
  if (avail < 0) return;
  if (avail > 0) {
    int width = snd_pcm_format_physical_width (this->format->format);
    
    /* Not enough space. We grab data nonetheless and sleep afterwards */
    if (avail < this->period_size) {
      avail = this->period_size;
    }
    
    /* check how many bytes we still have in all our bytestreams */
    bytes = avail * ( width / 8 ) * (element->numpads == 1 ? this->format->channels : 1);
    for (i = 0; i < element->numpads; i++) {
      g_assert (this->pads[i].pad != NULL);
      do {
        num_bytes = gst_bytestream_peek_bytes (this->pads[i].bs, &this->pads[i].data, bytes);
      } while (num_bytes == 0 && gst_alsa_sink_check_event (this, i));
      if (num_bytes == 0)
        return;
      bytes = MIN (bytes, num_bytes);
    }
    avail = bytes / (width / 8 ) / (element->numpads == 1 ? this->format->channels : 1);

    /* wait until the hw buffer has enough space */
    while (gst_element_get_state (element) == GST_STATE_PLAYING && (avail2 = gst_alsa_update_avail (this)) < avail) {
      if (avail2 <= -EPIPE) goto sink_restart;
      if (avail2 < 0) return;
      if (avail2 < avail && snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING)
	if (!gst_alsa_start (this)) return;
      if (gst_alsa_pcm_wait (this) == FALSE)
        return;
    }

    /* FIXME: lotsa stuff can have happened while fetching data. Do we need to check something? */
  
    /* put this data into alsa */
    if ((copied = this->transmit (this, &avail)) < 0)
      return;

    /* flush the data */
    bytes = copied * ( width / 8 ) * (element->numpads == 1 ? this->format->channels : 1);
    for (i = 0; i < element->numpads; i++)
      gst_bytestream_flush (this->pads[i].bs, bytes);
  }

  if (snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING && snd_pcm_avail_update (this->handle) == 0) {
    gst_alsa_start (this);
  }

}

static void
gst_alsa_src_loop (GstElement *element)
{
  snd_pcm_sframes_t avail, copied;
  GstBufferPool *pool = NULL;
  GstBuffer *buf;
  GstCaps *caps;
  gint i;
  GstAlsaPad *pad;
  GstAlsa *this = GST_ALSA (element);

  g_return_if_fail (this != NULL);

  /* set the caps on all pads */
  if (!this->format) {
    if (!(this->format = g_new (GstAlsaFormat, 1))) {
      gst_element_error (element, "No more memory");
    }
    /* FIXME: make this settable */
    this->format->format = SND_PCM_FORMAT_S16;
    this->format->rate = 44100;
    this->format->channels = (element->numpads == 1) ? 2 : element->numpads;
    GST_DEBUG (GST_CAT_NEGOTIATION, "starting caps negotiationgst_alsa_pcm_wait");
    caps = gst_alsa_caps (this->format->format, this->format->rate, this->format->channels);
    for (i = 0; i < element->numpads; i++) {
      if (gst_pad_try_set_caps (this->pads[i].pad, caps) <= 0) {
        GST_DEBUG (GST_CAT_NEGOTIATION, "setting caps (%p) in alsasrc (%p) on pad %d failed", caps, this, i);
        return;
      }
    }
  }

src_restart:

  while ((avail = gst_alsa_update_avail (this)) < this->period_size) {
    if (avail == -EPIPE) goto src_restart;
    if (avail < 0) return;
    if (snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING) break;
    /* wait */
    if (gst_alsa_pcm_wait (this) == FALSE)
      return;
  }
  if (avail > 0) {
    int width = snd_pcm_format_physical_width (this->format->format);
    int bytes_per_frame = ( width / 8 ) * (element->numpads == 1 ? this->format->channels : 1);
    if ((copied = this->transmit (this, &avail)) < 0)
      return;
      
    /* we get the buffer pool once per go round */
    if (! pool) pool = gst_alsa_src_get_buffer_pool (this->pads[0].pad);

    /* push the data to gstreamer if it's big enough to fill up a buffer. */
    for (i = 0; i < element->numpads; i++) {
      pad = &this->pads[i];
      pad->offset += MIN (copied, this->period_size - pad->offset);

      if (pad->offset >= this->period_size) {
        g_assert (pad->offset <= this->period_size);

        buf = gst_buffer_new_from_pool (pool, 0, 0);

        GST_BUFFER_DATA (buf) = pad->data;
        GST_BUFFER_SIZE (buf) = this->period_size * bytes_per_frame;
        GST_BUFFER_MAXSIZE (buf) = this->period_size * bytes_per_frame;

        gst_pad_push (pad->pad, buf);

        pad->data = NULL;
        pad->offset = 0;
      }
    }

    pool = NULL;
  }

  /* BUG: we start the stream explicitly, autostart doesn't work correctly (alsa 0.9.0rc7) */
  if (snd_pcm_state(this->handle) == SND_PCM_STATE_PREPARED && snd_pcm_avail_update (this->handle) == 0) {
    GST_DEBUG (GST_CAT_PLUGIN_INFO, "Explicitly starting capture");
    snd_pcm_start(this->handle);
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
    gst_element_error (GST_ELEMENT (this), "alsasink: Error restarting audio after xrun");
  }
}

/* TRUE, if everything should continue */
static gboolean
gst_alsa_sink_check_event (GstAlsa *this, gint pad_nr)
{
  GstEvent *event = NULL;
  guint32 avail;
  gboolean cont = TRUE;

  gst_bytestream_get_status (this->pads[pad_nr].bs, &avail, &event);

  if (event) {
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_element_set_eos (GST_ELEMENT (this));
        cont = FALSE;
        break;
      case GST_EVENT_INTERRUPT:
	cont = FALSE;
        break;
      default:
        g_warning ("GstAlsaSink: got an unknown event (Type: %d)", GST_EVENT_TYPE (event));
        break;
    }
    gst_event_unref (event);
  } else {
    /* the element at the top of the chain did not emit an event. */
    g_assert_not_reached ();
  }
  return cont;
}

/*** AUDIO SETUP / START / STOP ***********************************************/

static gboolean
gst_alsa_open_audio (GstAlsa *this)
{
  g_assert (this != NULL);
  g_assert (this->handle == NULL);

  GST_INFO (GST_CAT_PLUGIN_INFO, "Opening alsa device \"%s\" for %s...\n", this->device,
            this->stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture");

  ERROR_CHECK (snd_output_stdio_attach (&this->out, stderr, 0),
               "error opening log output: %s");
  /* blocking i/o */
  ERROR_CHECK (snd_pcm_open (&this->handle, this->device, this->stream, 0),
               "error opening pcm device %s: %s\n", this->device);

  GST_FLAG_SET (this, GST_ALSA_OPEN);
  return TRUE;
}
/* if someone finds an easy way to merge this with _set_hw_params, go ahead */
static gboolean
gst_alsa_probe_hw_params (GstAlsa *this, GstAlsaFormat *format)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_access_mask_t *mask;
  snd_pcm_uframes_t period_size;
  unsigned int period_count;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  
  GST_INFO (GST_CAT_PLUGIN_INFO, "Probing format: %s %dHz, %d channels\n",
            snd_pcm_format_name (format->format), format->rate, format->channels);

  snd_pcm_hw_params_alloca (&hw_params);
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params));
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_periods_integer (this->handle, hw_params));

  /* enable this for soundcard specific debugging */
  /* snd_pcm_hw_params_dump (hw_params, this->out); */
  
  mask = alloca (snd_pcm_access_mask_sizeof ());
  snd_pcm_access_mask_none (mask);
  if (GST_ELEMENT (this)->numpads == 1) {
    snd_pcm_access_mask_set (mask, this->mmap ? SND_PCM_ACCESS_MMAP_INTERLEAVED : SND_PCM_ACCESS_RW_INTERLEAVED);
  } else {
    snd_pcm_access_mask_set (mask, this->mmap ? SND_PCM_ACCESS_MMAP_NONINTERLEAVED : SND_PCM_ACCESS_RW_NONINTERLEAVED);
  }
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_access_mask (this->handle, hw_params, mask));
  
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_format (this->handle, hw_params, format->format));
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_channels (this->handle, hw_params, format->channels));
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_rate (this->handle, hw_params, format->rate, 0));

  period_count = this->period_count;
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_periods_near (this->handle, hw_params, &period_count, 0));
  period_size = this->period_size;
  SIMPLE_ERROR_CHECK (snd_pcm_hw_params_set_period_size_near (this->handle, hw_params, &period_size, 0));

  return TRUE;
}
/**
 * You must set all hw parameters at once and can't use already set params and
 * change them.
 * Thx ALSA for not documenting this
 */
static gboolean
gst_alsa_set_hw_params (GstAlsa *this)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_access_mask_t *mask;

  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  GST_INFO (GST_CAT_PLUGIN_INFO, "Preparing format: %s %dHz, %d channels\n",
            snd_pcm_format_name (this->format->format), this->format->rate, this->format->channels);

  snd_pcm_hw_params_alloca (&hw_params);
  ERROR_CHECK (snd_pcm_hw_params_any (this->handle, hw_params),
               "Broken configuration for this PCM: %s");
  ERROR_CHECK (snd_pcm_hw_params_set_periods_integer (this->handle, hw_params), 
               "cannot restrict period size to integral value: %s");

  /* enable this for soundcard specific debugging */
  /* snd_pcm_hw_params_dump (hw_params, this->out); */
  
  mask = alloca (snd_pcm_access_mask_sizeof ());
  snd_pcm_access_mask_none (mask);
  if (GST_ELEMENT (this)->numpads == 1) {
    snd_pcm_access_mask_set (mask, this->mmap ? SND_PCM_ACCESS_MMAP_INTERLEAVED : SND_PCM_ACCESS_RW_INTERLEAVED);
  } else {
    snd_pcm_access_mask_set (mask, this->mmap ? SND_PCM_ACCESS_MMAP_NONINTERLEAVED : SND_PCM_ACCESS_RW_NONINTERLEAVED);
  }
  ERROR_CHECK (snd_pcm_hw_params_set_access_mask (this->handle, hw_params, mask),
               "The Gstreamer ALSA plugin does not support your hardware. Error: %s");
  
  if (this->format) {
    ERROR_CHECK (snd_pcm_hw_params_set_format (this->handle, hw_params, this->format->format),
                 "Sample format (%s) not available: %s", snd_pcm_format_name (this->format->format));
    ERROR_CHECK (snd_pcm_hw_params_set_channels (this->handle, hw_params, this->format->channels),
                 "Channels count (%d) not available: %s", this->format->channels);
    ERROR_CHECK (snd_pcm_hw_params_set_rate (this->handle, hw_params, this->format->rate, 0),
                 "error setting rate (%d): %s", this->format->rate);
  }

  ERROR_CHECK (snd_pcm_hw_params_set_periods_near (this->handle, hw_params, &this->period_count, 0), 
               "error setting buffer size to %u: %s", (guint) this->period_count);
  ERROR_CHECK (snd_pcm_hw_params_set_period_size_near (this->handle, hw_params, &this->period_size, 0), 
               "error setting period size to %u frames: %s", (guint) this->period_size);

  ERROR_CHECK (snd_pcm_hw_params (this->handle, hw_params),
               "Could not set hardware parameters: %s");

  /* now get the pcm caps */
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_PAUSE, snd_pcm_hw_params_can_pause (hw_params));
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_RESUME, snd_pcm_hw_params_can_resume (hw_params));
  GST_ALSA_CAPS_SET (this, GST_ALSA_CAPS_SYNC_START, snd_pcm_hw_params_can_sync_start (hw_params));
  
  if (this->mmap) {
    this->transmit = gst_alsa_do_mmap;
  } else {
    this->transmit = gst_alsa_do_read_write;
  }

  return TRUE;
}
static gboolean
gst_alsa_set_sw_params (GstAlsa *this)
{
  snd_pcm_sw_params_t *sw_params;

  snd_pcm_sw_params_alloca (&sw_params);
  ERROR_CHECK (snd_pcm_sw_params_current (this->handle, sw_params),
               "Could not get current software parameters: %s");
  
  ERROR_CHECK (snd_pcm_sw_params_set_silence_size (this->handle, sw_params, 0),
               "could not set silence size: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_silence_threshold (this->handle, sw_params, 0),
               "could not set silence threshold: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_avail_min (this->handle, sw_params, this->period_size),
               "could not set avail min: %s");
  /* we start explicitly */
  ERROR_CHECK (snd_pcm_sw_params_set_start_threshold (this->handle, sw_params, this->period_size * this->period_count + 1),
               "could not set start mode: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_stop_threshold (this->handle, sw_params, this->period_size * this->period_count),
               "could not set stop mode: %s");
  ERROR_CHECK (snd_pcm_sw_params_set_xfer_align(this->handle, sw_params, 1),
               "Unable to set transfer align for playback: %s");
  ERROR_CHECK (snd_pcm_sw_params (this->handle, sw_params),
               "could not set sw_params: %s");
  
  return TRUE;
}

static gboolean
gst_alsa_start_audio (GstAlsa *this)
{
  g_assert (GST_FLAG_IS_SET (this, GST_ALSA_OPEN));
  
  if (!gst_alsa_set_hw_params (this))
    return FALSE;
  if (!gst_alsa_set_sw_params (this))
    return FALSE;
  
  GST_FLAG_SET (this, GST_ALSA_RUNNING);
  return TRUE;
}

static gboolean
gst_alsa_drain_audio (GstAlsa *this) {
  g_assert (this != NULL);
  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "stopping alsa");
  
  if (this->stream == SND_PCM_STREAM_PLAYBACK) {
    ERROR_CHECK (snd_pcm_drain (this->handle),
                 "couldn't stop and drain buffer: %s");
  }

  GST_FLAG_UNSET (this, GST_ALSA_RUNNING);  
  return TRUE;
}

static gboolean
gst_alsa_stop_audio (GstAlsa *this)
{
  g_assert (this != NULL);
  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "stopping alsa, skipping pending frames");
  
  if (this->stream == SND_PCM_STREAM_PLAYBACK) {
    ERROR_CHECK (snd_pcm_drop (this->handle),
                 "couldn't stop (dropping frames): %s");
  }

  GST_FLAG_UNSET (this, GST_ALSA_RUNNING);  
  return TRUE;
}

static gboolean
gst_alsa_close_audio (GstAlsa *this)
{
  g_return_val_if_fail (this != NULL, FALSE);
  g_return_val_if_fail (this->handle != NULL, FALSE);

  ERROR_CHECK (snd_pcm_close (this->handle), "Error closing device: %s");

  this->handle = NULL;
  GST_FLAG_UNSET (this, GST_ALSA_OPEN);

  return TRUE;
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
