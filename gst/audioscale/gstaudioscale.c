/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
/* Element-Checklist-Version: 5 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>

/*#define DEBUG_ENABLED */
#include <gstaudioscale.h>
#include <gst/audio/audio.h>
#include <gst/resample/resample.h>

/* elementfactory information */
static GstElementDetails gst_audioscale_details =
GST_ELEMENT_DETAILS ("Audio scaler",
    "Filter/Converter/Audio",
    "Resample audio",
    "David Schleef <ds@schleef.org>");

/* Audioscale signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FILTERLEN,
  ARG_METHOD,
  /* FILL ME */
};

static GstStaticPadTemplate gst_audioscale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_audioscale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

#define GST_TYPE_AUDIOSCALE_METHOD (gst_audioscale_method_get_type())
static GType
gst_audioscale_method_get_type (void)
{
  static GType audioscale_method_type = 0;
  static GEnumValue audioscale_methods[] = {
    {GST_RESAMPLE_NEAREST, "0", "Nearest"},
    {GST_RESAMPLE_BILINEAR, "1", "Bilinear"},
    {GST_RESAMPLE_SINC, "2", "Sinc"},
    {0, NULL, NULL},
  };

  if (!audioscale_method_type) {
    audioscale_method_type = g_enum_register_static ("GstAudioscaleMethod",
        audioscale_methods);
  }
  return audioscale_method_type;
}

static void gst_audioscale_base_init (gpointer g_class);
static void gst_audioscale_class_init (AudioscaleClass * klass);
static void gst_audioscale_init (Audioscale * audioscale);

static void gst_audioscale_chain (GstPad * pad, GstData * _data);

static void gst_audioscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audioscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_audioscale_signals[LAST_SIGNAL] = { 0 }; */

GType
audioscale_get_type (void)
{
  static GType audioscale_type = 0;

  if (!audioscale_type) {
    static const GTypeInfo audioscale_info = {
      sizeof (AudioscaleClass),
      gst_audioscale_base_init,
      NULL,
      (GClassInitFunc) gst_audioscale_class_init,
      NULL,
      NULL,
      sizeof (Audioscale),
      0,
      (GInstanceInitFunc) gst_audioscale_init,
    };

    audioscale_type =
        g_type_register_static (GST_TYPE_ELEMENT, "Audioscale",
        &audioscale_info, 0);
  }
  return audioscale_type;
}

static void
gst_audioscale_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_sink_template));

  gst_element_class_set_details (gstelement_class, &gst_audioscale_details);
}

static void
gst_audioscale_class_init (AudioscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_audioscale_set_property;
  gobject_class->get_property = gst_audioscale_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
      g_param_spec_int ("filter_length", "filter_length", "filter_length",
          0, G_MAXINT, 16, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AUDIOSCALE_METHOD, GST_RESAMPLE_SINC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static GstCaps *
gst_audioscale_getcaps (GstPad * pad)
{
  Audioscale *audioscale;
  GstCaps *caps;
  GstPad *otherpad;
  int i;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == audioscale->srcpad) ? audioscale->sinkpad :
      audioscale->srcpad;
  caps = gst_pad_get_allowed_caps (otherpad);

  /* we do this hack, because the audioscale lib doesn't handle
   * rate conversions larger than a factor of 2 */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    int rate_min, rate_max;
    GstStructure *structure = gst_caps_get_structure (caps, i);
    const GValue *value;

    value = gst_structure_get_value (structure, "rate");
    if (value == NULL)
      return NULL;

    if (G_VALUE_TYPE (value) == G_TYPE_INT) {
      rate_min = g_value_get_int (value);
      rate_max = rate_min;
    } else if (G_VALUE_TYPE (value) == GST_TYPE_INT_RANGE) {
      rate_min = gst_value_get_int_range_min (value);
      rate_max = gst_value_get_int_range_max (value);
    } else {
      return NULL;
    }

    rate_min /= 2;
    if (rate_max < G_MAXINT / 2) {
      rate_max *= 2;
    } else {
      rate_max = G_MAXINT;
    }

    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, rate_min,
        rate_max, NULL);
  }

  return caps;
}

static GstPadLinkReturn
gst_audioscale_link (GstPad * pad, const GstCaps * caps)
{
  Audioscale *audioscale;
  gst_resample_t *r;
  GstStructure *structure;
  int rate;
  int channels;
  int ret;
  GstPadLinkReturn link_ret;
  GstPad *otherpad;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  r = audioscale->gst_resample;

  otherpad = (pad == audioscale->srcpad) ? audioscale->sinkpad
      : audioscale->srcpad;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &rate);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  link_ret = gst_pad_try_set_caps (otherpad, gst_caps_copy (caps));
  if (GST_PAD_LINK_SUCCESSFUL (link_ret)) {
    audioscale->passthru = TRUE;
    r->channels = channels;
    r->i_rate = rate;
    r->o_rate = rate;
    return link_ret;
  }
  audioscale->passthru = FALSE;


  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *trycaps = gst_caps_copy (caps);

    gst_caps_set_simple (trycaps,
        "rate", G_TYPE_INT,
        (int) ((pad == audioscale->srcpad) ? r->i_rate : r->o_rate), NULL);
    link_ret = gst_pad_try_set_caps (otherpad, trycaps);
    if (GST_PAD_LINK_FAILED (link_ret)) {
      return link_ret;
    }
  }

  r->channels = channels;
  if (pad == audioscale->srcpad) {
    r->o_rate = rate;
  } else {
    r->i_rate = rate;
  }
  gst_resample_reinit (r);

  return GST_PAD_LINK_OK;
}

static void *
gst_audioscale_get_buffer (void *priv, unsigned int size)
{
  Audioscale *audioscale = priv;

  audioscale->outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (audioscale->outbuf) = size;
  GST_BUFFER_DATA (audioscale->outbuf) = g_malloc (size);
  GST_BUFFER_TIMESTAMP (audioscale->outbuf) =
      audioscale->offset * GST_SECOND / audioscale->gst_resample->o_rate;
  audioscale->offset +=
      size / sizeof (gint16) / audioscale->gst_resample->channels;

  return GST_BUFFER_DATA (audioscale->outbuf);
}

static void
gst_audioscale_init (Audioscale * audioscale)
{
  gst_resample_t *r;

  audioscale->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->sinkpad);
  gst_pad_set_chain_function (audioscale->sinkpad, gst_audioscale_chain);
  gst_pad_set_link_function (audioscale->sinkpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->sinkpad, gst_audioscale_getcaps);

  audioscale->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_src_template), "src");

  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->srcpad);
  gst_pad_set_link_function (audioscale->srcpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->srcpad, gst_audioscale_getcaps);

  r = g_new0 (gst_resample_t, 1);
  audioscale->gst_resample = r;

  r->priv = audioscale;
  r->get_buffer = gst_audioscale_get_buffer;
  r->method = GST_RESAMPLE_SINC;
  r->channels = 0;
  r->filter_length = 16;
  r->i_rate = -1;
  r->o_rate = -1;
  r->format = GST_RESAMPLE_S16;
  /*r->verbose = 1; */

  gst_resample_init (r);

  /* we will be reinitialized when the G_PARAM_CONSTRUCTs hit */
}

static void
gst_audioscale_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  Audioscale *audioscale;
  guchar *data;
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  if (audioscale->passthru) {
    gst_pad_push (audioscale->srcpad, GST_DATA (buf));
    return;
  }

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG ("gst_audioscale_chain: got buffer of %ld bytes in '%s'\n",
      size, gst_element_get_name (GST_ELEMENT (audioscale)));

  gst_resample_scale (audioscale->gst_resample, data, size);

  gst_pad_push (audioscale->srcpad, GST_DATA (audioscale->outbuf));

  gst_buffer_unref (buf);
}

static void
gst_audioscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Audioscale *src;
  gst_resample_t *r;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOSCALE (object));
  src = GST_AUDIOSCALE (object);
  r = src->gst_resample;

  switch (prop_id) {
    case ARG_FILTERLEN:
      r->filter_length = g_value_get_int (value);
      GST_DEBUG_OBJECT (GST_ELEMENT (src), "new filter length %d\n",
          r->filter_length);
      break;
    case ARG_METHOD:
      r->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_resample_reinit (r);
}

static void
gst_audioscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  Audioscale *src;
  gst_resample_t *r;

  src = GST_AUDIOSCALE (object);
  r = src->gst_resample;

  switch (prop_id) {
    case ARG_FILTERLEN:
      g_value_set_int (value, r->filter_length);
      break;
    case ARG_METHOD:
      g_value_set_enum (value, r->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  /* load support library */
  if (!gst_library_load ("gstresample"))
    return FALSE;

  if (!gst_element_register (plugin, "audioscale", GST_RANK_NONE,
          GST_TYPE_AUDIOSCALE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioscale",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
