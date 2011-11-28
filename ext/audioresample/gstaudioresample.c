/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003,2004 David A. Schleef <ds@schleef.org>
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
#include "gstaudioresample.h"
#include <gst/audio/audio.h>

GST_DEBUG_CATEGORY_STATIC (audioresample_debug);
#define GST_CAT_DEFAULT audioresample_debug

/* Audioresample signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FILTERLEN
};

#define SUPPORTED_CAPS \
  GST_STATIC_CAPS (\
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true"
#if 0
    /* disabled because it segfaults */
"audio/x-raw-float, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, " "width = (int) 32"
#endif
    )

     static GstStaticPadTemplate gst_audioresample_sink_template =
         GST_STATIC_PAD_TEMPLATE ("sink",
         GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

     static GstStaticPadTemplate gst_audioresample_src_template =
         GST_STATIC_PAD_TEMPLATE ("src",
         GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

     static void gst_audioresample_base_init (gpointer g_class);
     static void gst_audioresample_class_init (AudioresampleClass * klass);
     static void gst_audioresample_init (Audioresample * audioresample);
     static void gst_audioresample_dispose (GObject * object);

     static void gst_audioresample_chain (GstPad * pad, GstData * _data);

     static void gst_audioresample_set_property (GObject * object,
         guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_audioresample_get_property (GObject * object,
         guint prop_id, GValue * value, GParamSpec * pspec);

     static GstElementClass *parent_class = NULL;

/*static guint gst_audioresample_signals[LAST_SIGNAL] = { 0 }; */

     GType audioresample_get_type (void)
     {
       static GType audioresample_type = 0;

       if (!audioresample_type)
       {
         static const GTypeInfo audioresample_info = {
         sizeof (AudioresampleClass),
               gst_audioresample_base_init,
               NULL,
               (GClassInitFunc) gst_audioresample_class_init,
               NULL,
               NULL,
               sizeof (Audioresample), 0,
               (GInstanceInitFunc) gst_audioresample_init,};

         audioresample_type =
             g_type_register_static (GST_TYPE_ELEMENT, "Audioresample",
             &audioresample_info, 0);
       }
       return audioresample_type;
     }

static void gst_audioresample_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audioresample_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audioresample_sink_template);

  gst_element_class_set_details_simple (gstelement_class, "Audio scaler",
      "Filter/Converter/Audio",
      "Resample audio", "David Schleef <ds@schleef.org>");
}

static void gst_audioresample_class_init (AudioresampleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_audioresample_set_property;
  gobject_class->get_property = gst_audioresample_get_property;
  gobject_class->dispose = gst_audioresample_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
      g_param_spec_int ("filter-length", "filter_length", "filter_length",
          0, G_MAXINT, 16,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (audioresample_debug, "audioresample", 0,
      "audioresample element");
}

static void gst_audioresample_expand_caps (GstCaps * caps)
{
  gint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    const GValue *value;

    value = gst_structure_get_value (structure, "rate");
    if (value == NULL) {
      GST_ERROR ("caps structure doesn't have required rate field");
      return;
    }

    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, 0);
  }
}

static GstCaps *gst_audioresample_getcaps (GstPad * pad)
{
  Audioresample *audioresample;
  GstCaps *caps;
  GstPad *otherpad;

  audioresample = GST_AUDIORESAMPLE (gst_pad_get_parent (pad));

  otherpad = (pad == audioresample->srcpad) ? audioresample->sinkpad :
      audioresample->srcpad;
  caps = gst_pad_get_allowed_caps (otherpad);

  gst_audioresample_expand_caps (caps);

  return caps;
}

static GstCaps *gst_audioresample_fixate (GstPad * pad, const GstCaps * caps)
{
  Audioresample *audioresample;
  GstPad *otherpad;
  int rate;
  GstCaps *copy;
  GstStructure *structure;

    audioresample = GST_AUDIORESAMPLE (gst_pad_get_parent (pad));

  if (pad == audioresample->srcpad) {
    otherpad = audioresample->sinkpad;
    rate = audioresample->i_rate;
  } else
  {
    otherpad = audioresample->srcpad;
    rate = audioresample->o_rate;
  }
  if (!GST_PAD_IS_NEGOTIATING (otherpad))
    return NULL;
  if (gst_caps_get_size (caps) > 1)
    return NULL;

  copy = gst_caps_copy (caps);
  structure = gst_caps_get_structure (copy, 0);
  if (rate) {
    if (gst_structure_fixate_field_nearest_int (structure, "rate", rate)) {
      return copy;
    }
  }
  gst_caps_free (copy);
  return NULL;
}

static GstPadLinkReturn gst_audioresample_link (GstPad * pad,
    const GstCaps * caps)
{
  Audioresample *audioresample;
  GstStructure *structure;
  int rate;
  int channels;
  gboolean ret;
  GstPad *otherpad;

    audioresample = GST_AUDIORESAMPLE (gst_pad_get_parent (pad));

    otherpad = (pad == audioresample->srcpad) ? audioresample->sinkpad :
      audioresample->srcpad;

    structure = gst_caps_get_structure (caps, 0);
    ret = gst_structure_get_int (structure, "rate", &rate);
    ret &= gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
  {
    return GST_PAD_LINK_REFUSED;
  }

  if (gst_pad_is_negotiated (otherpad))
  {
    GstCaps *othercaps = gst_caps_copy (caps);
    int otherrate;
    GstPadLinkReturn linkret;

    if (pad == audioresample->srcpad) {
      otherrate = audioresample->i_rate;
    } else {
      otherrate = audioresample->o_rate;
    }
    gst_caps_set_simple (othercaps, "rate", G_TYPE_INT, otherrate, NULL);
    linkret = gst_pad_try_set_caps (otherpad, othercaps);
    if (GST_PAD_LINK_FAILED (linkret)) {
      return GST_PAD_LINK_REFUSED;
    }

  }

  audioresample->channels = channels;
  resample_set_n_channels (audioresample->resample, audioresample->channels);
  if (pad == audioresample->srcpad) {
    audioresample->o_rate = rate;
    resample_set_output_rate (audioresample->resample, audioresample->o_rate);
    GST_DEBUG ("set o_rate to %d", rate);
  } else {
    audioresample->i_rate = rate;
    resample_set_input_rate (audioresample->resample, audioresample->i_rate);
    GST_DEBUG ("set i_rate to %d", rate);
  }

  return GST_PAD_LINK_OK;
}

static void gst_audioresample_init (Audioresample * audioresample)
{
  ResampleState *r;

  audioresample->sinkpad =
      gst_pad_new_from_static_template (&gst_audioresample_sink_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (audioresample), audioresample->sinkpad);
  gst_pad_set_chain_function (audioresample->sinkpad, gst_audioresample_chain);
  gst_pad_set_link_function (audioresample->sinkpad, gst_audioresample_link);
  gst_pad_set_getcaps_function (audioresample->sinkpad,
      gst_audioresample_getcaps);
  gst_pad_set_fixate_function (audioresample->sinkpad,
      gst_audioresample_fixate);

  audioresample->srcpad =
      gst_pad_new_from_static_template (&gst_audioresample_src_template, "src");

  gst_element_add_pad (GST_ELEMENT (audioresample), audioresample->srcpad);
  gst_pad_set_link_function (audioresample->srcpad, gst_audioresample_link);
  gst_pad_set_getcaps_function (audioresample->srcpad,
      gst_audioresample_getcaps);
  gst_pad_set_fixate_function (audioresample->srcpad, gst_audioresample_fixate);

  r = resample_new ();
  audioresample->resample = r;

  resample_set_filter_length (r, 64);
  resample_set_format (r, RESAMPLE_FORMAT_S16);
}

static void gst_audioresample_dispose (GObject * object)
{
  Audioresample *audioresample = GST_AUDIORESAMPLE (object);

  if (audioresample->resample) {
    resample_free (audioresample->resample);
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void gst_audioresample_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  Audioresample *audioresample;
  ResampleState *r;
  guchar *data;
  gulong size;
  int outsize;
  GstBuffer *outbuf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  audioresample = GST_AUDIORESAMPLE (gst_pad_get_parent (pad));

  if (!GST_IS_BUFFER (_data)) {
    gst_pad_push (audioresample->srcpad, _data);
    return;
  }

  if (audioresample->passthru) {
    gst_pad_push (audioresample->srcpad, GST_DATA (buf));
    return;
  }

  r = audioresample->resample;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG ("got buffer of %ld bytes", size);

  resample_add_input_data (r, data, size, (ResampleCallback) gst_data_unref,
      buf);

  outsize = resample_get_output_size (r);
  /* FIXME this is audioresample being dumb.  dunno why */
  if (outsize == 0) {
    GST_ERROR ("overriding outbuf size");
    outsize = size;
  }
  outbuf = gst_buffer_new_and_alloc (outsize);

  outsize = resample_get_output_data (r, GST_BUFFER_DATA (outbuf), outsize);
  GST_BUFFER_SIZE (outbuf) = outsize;

  GST_BUFFER_TIMESTAMP (outbuf) =
      audioresample->offset * GST_SECOND / audioresample->o_rate;
  audioresample->offset += outsize / sizeof (gint16) / audioresample->channels;

  gst_pad_push (audioresample->srcpad, GST_DATA (outbuf));
}

static void
    gst_audioresample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Audioresample *audioresample;

    g_return_if_fail (GST_IS_AUDIORESAMPLE (object));
    audioresample = GST_AUDIORESAMPLE (object);

  switch (prop_id) {
    case ARG_FILTERLEN:
      audioresample->filter_length = g_value_get_int (value);
      GST_DEBUG_OBJECT (GST_ELEMENT (audioresample), "new filter length %d\n",
          audioresample->filter_length);
      resample_set_filter_length (audioresample->resample,
          audioresample->filter_length);
      break;
      default:G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
    gst_audioresample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Audioresample *audioresample;

  g_return_if_fail (GST_IS_AUDIORESAMPLE (object));
  audioresample = GST_AUDIORESAMPLE (object);

  switch (prop_id) {
    case ARG_FILTERLEN:
      g_value_set_int (value, audioresample->filter_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean plugin_init (GstPlugin * plugin)
{
  resample_init ();

  if (!gst_element_register (plugin, "audioresample", GST_RANK_PRIMARY,
          GST_TYPE_AUDIORESAMPLE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioresample",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
