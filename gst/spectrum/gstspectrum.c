/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Stefan Kost <ensonic@users.sf.net>
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
/**
 * SECTION:element-spectrum
 *
 * <refsect2>
 * The Spectrum element analyzes the frequency spectrum of an audio signal.
 * Analysis results are send as outgoing buffers.
 * The buffer contains a guint8 value per frequency band.
 * A value of 0 maps to the threshold property.
 *
 * It cannot be used with the gst-launch command in a sensible way. Instead the
 * included demo shows how to use it.
 *
 * Last reviewed on 2006-05-21 (0.10.3)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstspectrum.h"

GST_DEBUG_CATEGORY_STATIC (gst_spectrum_debug);
#define GST_CAT_DEFAULT gst_spectrum_debug

/* elementfactory information */
static const GstElementDetails gst_spectrum_details =
GST_ELEMENT_DETAILS ("Spectrum analyzer",
    "Filter/Analyzer/Audio",
    "Run an FFT on the audio signal, output spectrum data",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Stefan Kost <ensonic@users.sf.net>");

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [1, MAX], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* Spectrum signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_THRESHOLD
};

GST_BOILERPLATE (GstSpectrum, gst_spectrum, GstElement, GST_TYPE_ELEMENT);

static void gst_spectrum_dispose (GObject * object);
static void gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_spectrum_set_sink_caps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_spectrum_get_sink_caps (GstPad * pad);
static GstFlowReturn gst_spectrum_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_spectrum_change_state (GstElement * element,
    GstStateChange transition);

#define fixed short
extern int gst_spectrum_fix_fft (fixed fr[], fixed fi[], int m, int inverse);
extern void gst_spectrum_fix_loud (fixed loud[], fixed fr[], fixed fi[], int n,
    int scale_shift);
extern void gst_spectrum_window (fixed fr[], int n);

/*static guint gst_spectrum_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_spectrum_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_set_details (element_class, &gst_spectrum_details);
}

static void
gst_spectrum_class_init (GstSpectrumClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_spectrum_set_property;
  gobject_class->dispose = gst_spectrum_dispose;

  element->change_state = gst_spectrum_change_state;

  g_object_class_install_property (gobject_class, ARG_WIDTH,
      g_param_spec_uint ("width", "Width", "number of frequency bands",
          0, G_MAXUINT, 0, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold",
          "db threshold for result, maps to 0", G_MININT, 0, -60,
          G_PARAM_WRITABLE));

  GST_DEBUG_CATEGORY_INIT (gst_spectrum_debug, "spectrum", 0,
      "audio spectrum analyser element");
}

static void
gst_spectrum_init (GstSpectrum * spectrum, GstSpectrumClass * g_class)
{
  spectrum->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_pad_set_chain_function (spectrum->sinkpad, gst_spectrum_chain);
  gst_pad_set_setcaps_function (spectrum->sinkpad, gst_spectrum_set_sink_caps);
  gst_pad_set_getcaps_function (spectrum->sinkpad, gst_spectrum_get_sink_caps);
  gst_element_add_pad (GST_ELEMENT (spectrum), spectrum->sinkpad);

  spectrum->srcpad =
      gst_pad_new_from_static_template (&src_template_factory, "src");
  gst_element_add_pad (GST_ELEMENT (spectrum), spectrum->srcpad);

  spectrum->adapter = gst_adapter_new ();

  spectrum->width = 128;
  spectrum->base = 9;
  spectrum->len = 1024;         /* 2 ^ (base+1) */

  spectrum->loud = g_malloc (spectrum->len * sizeof (gint16));
  spectrum->im = g_malloc (spectrum->len * sizeof (gint16));
  memset (spectrum->im, 0, spectrum->len * sizeof (gint16));
  spectrum->re = g_malloc (spectrum->len * sizeof (gint16));
  memset (spectrum->re, 0, spectrum->len * sizeof (gint16));
}

static void
gst_spectrum_dispose (GObject * object)
{
  GstSpectrum *spectrum = GST_SPECTRUM (object);

  if (spectrum->adapter) {
    g_object_unref (spectrum->adapter);
    spectrum->adapter = NULL;
  }

  g_free (spectrum->re);
  g_free (spectrum->im);
  g_free (spectrum->loud);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpectrum *spectrum = GST_SPECTRUM (object);

  switch (prop_id) {
    case ARG_WIDTH:
      spectrum->width = g_value_get_uint (value);
      break;
    case ARG_THRESHOLD:
      spectrum->threshold = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_spectrum_set_sink_caps (GstPad * pad, GstCaps * caps)
{
  GstSpectrum *spectrum = GST_SPECTRUM (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gboolean ret;

  ret = gst_structure_get_int (structure, "channels", &spectrum->channels);
  GST_INFO ("Got channels = %d", spectrum->channels);

  gst_object_unref (spectrum);

  return ret;
}

static GstCaps *
gst_spectrum_get_sink_caps (GstPad * pad)
{
  GstSpectrum *spectrum = GST_SPECTRUM (gst_pad_get_parent (pad));
  GstCaps *res;
  GstStructure *structure;

  res = gst_caps_copy (gst_pad_get_pad_template_caps (spectrum->sinkpad));
  if (spectrum->channels) {
    structure = gst_caps_get_structure (res, 0);
    gst_structure_set (structure, "channels", G_TYPE_INT, spectrum->channels,
        NULL);
    GST_INFO ("Fixate channels = %d", spectrum->channels);
  }
  gst_object_unref (spectrum);
  return res;
}

static GstFlowReturn
gst_spectrum_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpectrum *spectrum = GST_SPECTRUM (gst_pad_get_parent (pad));
  gint16 *samples;
  gint wanted;
  gint i, j, k;
  gint32 acc;
  gfloat pos, step;
  guchar *spect;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;

  gst_adapter_push (spectrum->adapter, buffer);
  /* required number of bytes */
  wanted = spectrum->channels * spectrum->len * 2;
  /* FIXME: 4.0 was 2.0 before, but that include the mirrored spectrum */
  step = (gfloat) spectrum->len / (spectrum->width * 4.0);

  while (gst_adapter_available (spectrum->adapter) > wanted &&
      (ret == GST_FLOW_OK)) {

    samples = (gint16 *) gst_adapter_take (spectrum->adapter, wanted);

    for (i = 0, j = 0; i < spectrum->len; i++) {
      for (k = 0, acc = 0; k < spectrum->channels; k++)
        acc += samples[j++];
      spectrum->re[i] = (gint16) (acc / spectrum->channels);
    }

    gst_spectrum_window (spectrum->re, spectrum->len);
    gst_spectrum_fix_fft (spectrum->re, spectrum->im, spectrum->base, FALSE);
    gst_spectrum_fix_loud (spectrum->loud, spectrum->re, spectrum->im,
        spectrum->len, 0);

    ret = gst_pad_alloc_buffer_and_set_caps (spectrum->srcpad,
        GST_BUFFER_OFFSET_NONE, spectrum->width,
        GST_PAD_CAPS (spectrum->srcpad), &outbuf);

    /* resample to requested width */
    spect = GST_BUFFER_DATA (outbuf);
    for (i = 0, pos = 0.0; i < spectrum->width; i++, pos += step) {
      /* > -60 db? FIXME: make this a gobject property */
      if (spectrum->loud[(gint) pos] > spectrum->threshold) {
        spect[i] = spectrum->loud[(gint) pos] - spectrum->threshold;
        /*
           if (spect[i] > 15);
           spect[i] = 15;
         */
      } else
        /* treat as silence */
        spect[i] = 0;
    }

    ret = gst_pad_push (spectrum->srcpad, outbuf);
  }

  gst_object_unref (spectrum);
  return ret;
}

static GstStateChangeReturn
gst_spectrum_change_state (GstElement * element, GstStateChange transition)
{
  GstSpectrum *spectrum = GST_SPECTRUM (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (spectrum->adapter);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "spectrum", GST_RANK_NONE,
      GST_TYPE_SPECTRUM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "spectrum",
    "Run an FFT on the audio signal, output spectrum data",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
