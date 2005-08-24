/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/base/gstbasetransform.h>

GST_DEBUG_CATEGORY_STATIC (audioresample_debug);
#define GST_CAT_DEFAULT audioresample_debug

/* elementfactory information */
static GstElementDetails gst_audioresample_details =
GST_ELEMENT_DETAILS ("Audio scaler",
    "Filter/Converter/Audio",
    "Resample audio",
    "David Schleef <ds@schleef.org>");

/* GstAudioresample signals and args */
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
      "signed = (boolean) true")

#if 0
  /* disabled because it segfaults */
"audio/x-raw-float, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, " "width = (int) 32")
#endif
     static GstStaticPadTemplate gst_audioresample_sink_template =
         GST_STATIC_PAD_TEMPLATE ("sink",
         GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

     static GstStaticPadTemplate gst_audioresample_src_template =
         GST_STATIC_PAD_TEMPLATE ("src",
         GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

     static void gst_audioresample_base_init (gpointer g_class);
     static void gst_audioresample_class_init (GstAudioresampleClass * klass);
     static void gst_audioresample_init (GstAudioresample * audioresample);
     static void gst_audioresample_dispose (GObject * object);

     static void gst_audioresample_set_property (GObject * object,
         guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_audioresample_get_property (GObject * object,
         guint prop_id, GValue * value, GParamSpec * pspec);

/* vmethods */
     gboolean audioresample_get_unit_size (GstBaseTransform * base,
         GstCaps * caps, guint * size);
     GstCaps *audioresample_transform_caps (GstBaseTransform * base,
         GstPadDirection direction, GstCaps * caps);
     gboolean audioresample_transform_size (GstBaseTransform * trans,
         GstPadDirection direction, GstCaps * incaps, guint insize,
         GstCaps * outcaps, guint * outsize);
     gboolean audioresample_set_caps (GstBaseTransform * base, GstCaps * incaps,
         GstCaps * outcaps);
     static GstFlowReturn audioresample_transform (GstBaseTransform * base,
         GstBuffer * inbuf, GstBuffer * outbuf);

/*static guint gst_audioresample_signals[LAST_SIGNAL] = { 0 }; */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (audioresample_debug, "audioresample", 0, "audio resampling element");

GST_BOILERPLATE_FULL (GstAudioresample, gst_audioresample, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

     static void gst_audioresample_base_init (gpointer g_class)
     {
       GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

       gst_element_class_add_pad_template (gstelement_class,
           gst_static_pad_template_get (&gst_audioresample_src_template));
       gst_element_class_add_pad_template (gstelement_class,
           gst_static_pad_template_get (&gst_audioresample_sink_template));

       gst_element_class_set_details (gstelement_class,
           &gst_audioresample_details);
     }

static void gst_audioresample_class_init (GstAudioresampleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_audioresample_set_property;
  gobject_class->get_property = gst_audioresample_get_property;
  gobject_class->dispose = gst_audioresample_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
      g_param_spec_int ("filter_length", "filter_length", "filter_length",
          0, G_MAXINT, 16, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  GST_BASE_TRANSFORM_CLASS (klass)->transform_size =
      GST_DEBUG_FUNCPTR (audioresample_transform_size);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (audioresample_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (audioresample_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (audioresample_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (audioresample_transform);
}

static void gst_audioresample_init (GstAudioresample * audioresample)
{
  ResampleState *r;

  r = resample_new ();
  audioresample->resample = r;

  resample_set_filter_length (r, 64);
  resample_set_format (r, RESAMPLE_FORMAT_S16);
}

static void gst_audioresample_dispose (GObject * object)
{
  GstAudioresample *audioresample = GST_AUDIORESAMPLE (object);

  if (audioresample->resample) {
    resample_free (audioresample->resample);
    audioresample->resample = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* vmethods */
gboolean
    audioresample_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size) {
  gint width, channels;
  GstStructure *structure;
  gboolean ret;

  g_return_val_if_fail (size, FALSE);

  /* this works for both float and int */
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);
  g_return_val_if_fail (ret, FALSE);

  *size = width * channels / 8;

  return TRUE;
}

GstCaps *audioresample_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *temp, *res;
  const GstCaps *templcaps;
  GstStructure *structure;

  temp = gst_caps_copy (caps);
  structure = gst_caps_get_structure (temp, 0);
  gst_structure_remove_field (structure, "rate");
  templcaps = gst_pad_get_pad_template_caps (base->srcpad);
  res = gst_caps_intersect (templcaps, temp);
  gst_caps_unref (temp);

  return res;
}

static gboolean
    resample_set_state_from_caps (ResampleState * state, GstCaps * incaps,
    GstCaps * outcaps, gint * channels, gint * inrate, gint * outrate)
{
  GstStructure *structure;
  gboolean ret;
  gint myinrate, myoutrate;
  int mychannels;

  GST_DEBUG ("incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  /* FIXME: once it does float, set the correct format */
#if 0
  if (g_str_equal (gst_structure_get_name (structure), "audio/x-raw-float")) {
    r->format = GST_RESAMPLE_FLOAT;
  } else {
    r->format = GST_RESAMPLE_S16;
  }
#endif

  ret = gst_structure_get_int (structure, "rate", &myinrate);
  ret &= gst_structure_get_int (structure, "channels", &mychannels);
  g_return_val_if_fail (ret, FALSE);

  structure = gst_caps_get_structure (outcaps, 0);
  ret = gst_structure_get_int (structure, "rate", &myoutrate);
  g_return_val_if_fail (ret, FALSE);

  if (channels)
    *channels = mychannels;
  if (inrate)
    *inrate = myinrate;
  if (outrate)
    *outrate = myoutrate;

  resample_set_n_channels (state, mychannels);
  resample_set_input_rate (state, myinrate);
  resample_set_output_rate (state, myoutrate);

  return TRUE;
}

gboolean audioresample_transform_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
  GstAudioresample *audioresample = GST_AUDIORESAMPLE (base);
  ResampleState *state;
  GstCaps *srccaps, *sinkcaps;
  gboolean use_internal = FALSE;        /* whether we use the internal state */
  gboolean ret = TRUE;

  /* FIXME: make sure incaps/outcaps get renamed to caps/othercaps, since
   * interpretation depends on the direction */
  if (direction == GST_PAD_SINK) {
    sinkcaps = caps;
    srccaps = othercaps;
  } else {
    sinkcaps = othercaps;
    srccaps = caps;
  }

  /* if the caps are the ones that _set_caps got called with; we can use
   * our own state; otherwise we'll have to create a state */
  if (gst_caps_is_equal (sinkcaps, audioresample->sinkcaps) &&
      gst_caps_is_equal (srccaps, audioresample->srccaps)) {
    use_internal = TRUE;
    state = audioresample->resample;
  } else {
    state = resample_new ();
    resample_set_state_from_caps (state, sinkcaps, srccaps, NULL, NULL, NULL);
  }

  /* we can use our own state to answer the question */
  if (direction == GST_PAD_SINK) {
    /* asked to convert size of an incoming buffer */
    *othersize = resample_get_output_size_for_input (state, size);
  } else {
    /* take a best guess, this is called cheating */
    *othersize = floor (size * state->i_rate / state->o_rate);
  }

  if (!use_internal) {
    resample_free (state);
  }

  return ret;
}

gboolean audioresample_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  gboolean ret;
  gint inrate, outrate;
  int channels;
  GstAudioresample *audioresample = GST_AUDIORESAMPLE (base);

  GST_DEBUG_OBJECT (base, "incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  ret = resample_set_state_from_caps (audioresample->resample, incaps, outcaps,
      &channels, &inrate, &outrate);

  g_return_val_if_fail (ret, FALSE);

  audioresample->channels = channels;
  GST_DEBUG_OBJECT (audioresample, "set channels to %d", channels);
  audioresample->i_rate = inrate;
  GST_DEBUG_OBJECT (audioresample, "set i_rate to %d", inrate);
  audioresample->o_rate = outrate;
  GST_DEBUG_OBJECT (audioresample, "set o_rate to %d", outrate);

  /* save caps so we can short-circuit in the size_transform if the caps
   * are the same */
  /* FIXME: clean them up in state change ? */
  gst_caps_ref (incaps);
  gst_caps_replace (&audioresample->sinkcaps, incaps);
  gst_caps_ref (outcaps);
  gst_caps_replace (&audioresample->srccaps, outcaps);

  return TRUE;
}

static GstFlowReturn
    audioresample_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  /* FIXME: this-> */
  GstAudioresample *audioresample = GST_AUDIORESAMPLE (base);
  ResampleState *r;
  guchar *data;
  gulong size;
  int outsize;

  /* FIXME: move to _inplace */
#if 0
  if (audioresample->passthru) {
    gst_pad_push (audioresample->srcpad, GST_DATA (buf));
    return;
  }
#endif

  r = audioresample->resample;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  GST_DEBUG_OBJECT (audioresample, "got buffer of %ld bytes", size);

  resample_add_input_data (r, data, size, NULL, NULL);

  outsize = resample_get_output_size (r);
  if (outsize != GST_BUFFER_SIZE (outbuf)) {
    GST_WARNING_OBJECT (audioresample,
        "overriding audioresample's outsize %d with outbuffer's size %d",
        outsize, GST_BUFFER_SIZE (outbuf));
    outsize = GST_BUFFER_SIZE (outbuf);
  }

  outsize = resample_get_output_data (r, GST_BUFFER_DATA (outbuf), outsize);
  GST_BUFFER_TIMESTAMP (outbuf) =
      audioresample->offset * GST_SECOND / audioresample->o_rate;
  audioresample->offset += outsize / sizeof (gint16) / audioresample->channels;

  if (outsize != GST_BUFFER_SIZE (outbuf)) {
    GST_WARNING_OBJECT (audioresample,
        "audioresample, you bastard ! you only gave me %d bytes, not %d",
        outsize, GST_BUFFER_SIZE (outbuf));
    /* if the size we get is smaller than the buffer, it's still fine; we
     * just waste a bit of space on the end */
    if (outsize < GST_BUFFER_SIZE (outbuf)) {
      GST_BUFFER_SIZE (outbuf) = outsize;
      return GST_FLOW_OK;
    } else {
      /* this is an error that needs fixing in the resample library; we told
       * it we wanted only GST_BUFFER_SIZE (outbuf), and it gave us more ! */
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

static void
    gst_audioresample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioresample *audioresample;

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
  GstAudioresample *audioresample;

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
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN);
