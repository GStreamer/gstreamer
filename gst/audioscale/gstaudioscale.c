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
#include "gstaudioscale.h"
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
  ARG_METHOD
      /* FILL ME */
};

#define SUPPORTED_CAPS \
  GST_STATIC_CAPS (\
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true")
#if 0
  /* disabled because it segfaults */
"audio/x-raw-float, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, " "width = (int) 32")
#endif
     static GstStaticPadTemplate gst_audioscale_sink_template =
         GST_STATIC_PAD_TEMPLATE ("sink",
         GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

     static GstStaticPadTemplate gst_audioscale_src_template =
         GST_STATIC_PAD_TEMPLATE ("src",
         GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

#define GST_TYPE_AUDIOSCALE_METHOD (gst_audioscale_method_get_type())
     static GType gst_audioscale_method_get_type (void)
     {
       static GType audioscale_method_type = 0;
       static GEnumValue audioscale_methods[] =
       {
         {
         GST_RESAMPLE_NEAREST, "0", "Nearest"}
         ,
         {
         GST_RESAMPLE_BILINEAR, "1", "Bilinear"}
         , {
         GST_RESAMPLE_SINC, "2", "Sinc"}
         , {
         0, NULL, NULL}
       ,};

       if (!audioscale_method_type) {
         audioscale_method_type = g_enum_register_static ("GstAudioscaleMethod",
             audioscale_methods);
       }
       return audioscale_method_type;
     }

static void gst_audioscale_base_init (gpointer g_class);
static void gst_audioscale_class_init (AudioscaleClass * klass);
static void gst_audioscale_init (Audioscale * audioscale);
static void gst_audioscale_dispose (GObject * object);

static void gst_audioscale_chain (GstPad * pad, GstData * _data);

static void gst_audioscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audioscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_audioscale_signals[LAST_SIGNAL] = { 0 }; */

GType audioscale_get_type (void)
{
  static GType audioscale_type = 0;

  if (!audioscale_type)
  {
    static const GTypeInfo audioscale_info =
    {
    sizeof (AudioscaleClass),
          gst_audioscale_base_init,
          NULL,
          (GClassInitFunc) gst_audioscale_class_init,
          NULL,
          NULL,
          sizeof (Audioscale), 0, (GInstanceInitFunc) gst_audioscale_init,};

    audioscale_type =
        g_type_register_static (GST_TYPE_ELEMENT, "Audioscale",
        &audioscale_info, 0);
  }
  return audioscale_type;
}

static void gst_audioscale_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_sink_template));

  gst_element_class_set_details (gstelement_class, &gst_audioscale_details);
}

static void gst_audioscale_class_init (AudioscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_audioscale_set_property;
  gobject_class->get_property = gst_audioscale_get_property;
  gobject_class->dispose = gst_audioscale_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
      g_param_spec_int ("filter_length", "filter_length", "filter_length",
          0, G_MAXINT, 16, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AUDIOSCALE_METHOD, GST_RESAMPLE_SINC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void gst_audioscale_expand_value (GValue * dest, const GValue * src)
{
  int rate_min, rate_max;

  if (G_VALUE_TYPE (src) == G_TYPE_INT ||
      G_VALUE_TYPE (src) == GST_TYPE_INT_RANGE)
  {
    if (G_VALUE_TYPE (src) == G_TYPE_INT) {
      rate_min = g_value_get_int (src);
      rate_max = rate_min;
    } else
    {
      rate_min = gst_value_get_int_range_min (src);
      rate_max = gst_value_get_int_range_max (src);
    }

    rate_min = (rate_min + 1) / 2;
    if (rate_min < 1)
      rate_min = 1;
    if (rate_max < G_MAXINT / 2) {
      rate_max *= 2;
    } else {
      rate_max = G_MAXINT;
    }

    g_value_init (dest, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (dest, rate_min, rate_max);
    return;
  }

  if (G_VALUE_TYPE (src) == GST_TYPE_LIST) {
    int i;

    g_value_init (dest, GST_TYPE_LIST);
    for (i = 0; i < gst_value_list_get_size (src); i++) {
      const GValue *s = gst_value_list_get_value (src, i);
      GValue d =
      {
      0};
      int j;

      gst_audioscale_expand_value (&d, s);

      for (j = 0; j < gst_value_list_get_size (dest); j++) {
        const GValue *s2 = gst_value_list_get_value (dest, j);
        GValue d2 =
        {
        0};

        gst_value_union (&d2, &d, s2);
        if (G_VALUE_TYPE (&d2) == GST_TYPE_INT_RANGE) {
          g_value_unset ((GValue *) s2);
          gst_value_init_and_copy ((GValue *) s2, &d2);
          break;
        }
        g_value_unset (&d2);
      }
      if (j == gst_value_list_get_size (dest)) {
        gst_value_list_append_value (dest, &d);
      }
      g_value_unset (&d);
    }

    if (gst_value_list_get_size (dest) == 1) {
      const GValue *s = gst_value_list_get_value (dest, 0);
      GValue d =
      {
      0};

      gst_value_init_and_copy (&d, s);
      g_value_unset (dest);
      gst_value_init_and_copy (dest, &d);
      g_value_unset (&d);
    }

    return;
  }

  GST_ERROR ("unexpected value type");
}

static void gst_audioscale_expand_caps (GstCaps * caps)
{
  gint i;

  /* we do this hack, because the audioscale lib doesn't handle
   * rate conversions larger than a factor of 2 */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    const GValue *value;
    GValue dest =
    {
    0};

    value = gst_structure_get_value (structure, "rate");
    if (value == NULL) {
      GST_ERROR ("caps structure doesn't have required rate field");
      return;
    }

    gst_audioscale_expand_value (&dest, value);

    gst_structure_set_value (structure, "rate", &dest);
  }
}

static GstCaps *gst_audioscale_getcaps (GstPad * pad)
{
  Audioscale *audioscale;
  GstCaps *caps;
  GstPad *otherpad;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == audioscale->srcpad) ? audioscale->sinkpad :
      audioscale->srcpad;
  caps = gst_pad_get_allowed_caps (otherpad);

  gst_audioscale_expand_caps (caps);

  return caps;
}

static GstCaps *gst_audioscale_fixate (GstPad * pad, const GstCaps * caps)
{
  Audioscale *audioscale;
  gst_resample_t *r;
  GstPad *otherpad;
  int rate;
  GstCaps *copy;
  GstStructure *structure;

    audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
    r = audioscale->gst_resample;
  if (pad == audioscale->srcpad)
  {
    otherpad = audioscale->sinkpad;
    rate = r->i_rate;
  } else
  {
    otherpad = audioscale->srcpad;
    rate = r->o_rate;
  }
  if (!GST_PAD_IS_NEGOTIATING (otherpad))
    return NULL;
  if (gst_caps_get_size (caps) > 1)
    return NULL;

  copy = gst_caps_copy (caps);
  structure = gst_caps_get_structure (copy, 0);
  if (gst_caps_structure_fixate_field_nearest_int (structure, "rate", rate))
    return copy;
  gst_caps_free (copy);
  return NULL;
}

static GstPadLinkReturn gst_audioscale_link (GstPad * pad, const GstCaps * caps)
{
  Audioscale *audioscale;
  gst_resample_t *r;
  GstStructure *structure;
  double *rate, *otherrate;
  int temp;
  gboolean ret;
  GstPadLinkReturn link_ret;
  GstPad *otherpad;
  GstCaps *copy;

    audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
    r = audioscale->gst_resample;

  if (pad == audioscale->srcpad)
  {
    otherpad = audioscale->sinkpad;
    rate = &r->o_rate;
    otherrate = &r->i_rate;
  } else
  {
    otherpad = audioscale->srcpad;
    rate = &r->i_rate;
    otherrate = &r->o_rate;
  }

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &temp);
  ret &= gst_structure_get_int (structure, "channels", &r->channels);
  g_return_val_if_fail (ret, GST_PAD_LINK_REFUSED);
  *rate = temp;

  copy = gst_caps_copy (caps);
  gst_audioscale_expand_caps (copy);
  link_ret = gst_pad_try_set_caps_nonfixed (otherpad, copy);

  if (GST_PAD_LINK_FAILED (link_ret))
    return link_ret;

  caps = gst_pad_get_negotiated_caps (otherpad);
  g_return_val_if_fail (caps, GST_PAD_LINK_REFUSED);
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &temp);
  g_return_val_if_fail (ret, GST_PAD_LINK_REFUSED);
  *otherrate = temp;
  if (g_str_equal (gst_structure_get_name (structure), "audio/x-raw-float")) {
    r->format = GST_RESAMPLE_FLOAT;
  } else {
    r->format = GST_RESAMPLE_S16;
  }

  audioscale->passthru = (r->i_rate == r->o_rate);
  gst_resample_reinit (r);

  return link_ret;
}

static void *gst_audioscale_get_buffer (void *priv, unsigned int size)
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

static void gst_audioscale_init (Audioscale * audioscale)
{
  gst_resample_t *r;

  audioscale->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->sinkpad);
  gst_pad_set_chain_function (audioscale->sinkpad, gst_audioscale_chain);
  gst_pad_set_link_function (audioscale->sinkpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->sinkpad, gst_audioscale_getcaps);
  gst_pad_set_fixate_function (audioscale->sinkpad, gst_audioscale_fixate);

  audioscale->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_src_template), "src");

  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->srcpad);
  gst_pad_set_link_function (audioscale->srcpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->srcpad, gst_audioscale_getcaps);
  gst_pad_set_fixate_function (audioscale->srcpad, gst_audioscale_fixate);

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

static void gst_audioscale_dispose (GObject * object)
{
  Audioscale *audioscale = GST_AUDIOSCALE (object);

  if (audioscale->gst_resample)
    g_free (audioscale->gst_resample);
  audioscale->gst_resample = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void gst_audioscale_chain (GstPad * pad, GstData * _data)
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
      case ARG_METHOD:r->method = g_value_get_enum (value);
      break;
      default:G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_resample_reinit (r);
}

static void
    gst_audioscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
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


static gboolean plugin_init (GstPlugin * plugin)
{
  /* load support library */
  if (!gst_library_load ("gstresample"))
    return FALSE;

  if (!gst_element_register (plugin, "audioscale", GST_RANK_SECONDARY,
          GST_TYPE_AUDIOSCALE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioscale",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
