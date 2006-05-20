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
    "Erik Walthinsen <omega@cse.ogi.edu>,"
    "Stefan Kost <ensonic@users.sf.net>");

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
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
  ARG_WIDTH
};


static void gst_spectrum_base_init (gpointer g_class);
static void gst_spectrum_class_init (GstSpectrumClass * klass);
static void gst_spectrum_init (GstSpectrum * spectrum);
static void gst_spectrum_dispose (GObject * object);
static void gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_spectrum_chain (GstPad * pad, GstBuffer * buffer);

#define fixed short
extern int gst_spectrum_fix_fft (fixed fr[], fixed fi[], int m, int inverse);
extern void gst_spectrum_fix_loud (fixed loud[], fixed fr[], fixed fi[], int n,
    int scale_shift);
extern void gst_spectrum_window (fixed fr[], int n);

static GstElementClass *parent_class = NULL;

/*static guint gst_spectrum_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_spectrum_get_type (void)
{
  static GType spectrum_type = 0;

  if (!spectrum_type) {
    static const GTypeInfo spectrum_info = {
      sizeof (GstSpectrumClass),
      gst_spectrum_base_init,
      NULL,
      (GClassInitFunc) gst_spectrum_class_init,
      NULL,
      NULL,
      sizeof (GstSpectrum),
      0,
      (GInstanceInitFunc) gst_spectrum_init,
    };

    spectrum_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSpectrum",
        &spectrum_info, 0);
    GST_DEBUG_CATEGORY_INIT (gst_spectrum_debug, "spectrum", 0,
        "audio spectrum analyser element");
  }
  return spectrum_type;
}

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
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_spectrum_set_property;
  gobject_class->dispose = gst_spectrum_dispose;

  g_object_class_install_property (gobject_class, ARG_WIDTH,
      g_param_spec_int ("width", "width", "width",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));

}

static void
gst_spectrum_init (GstSpectrum * spectrum)
{
  spectrum->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (spectrum), spectrum->sinkpad);
  gst_pad_set_chain_function (spectrum->sinkpad, gst_spectrum_chain);

  spectrum->srcpad =
      gst_pad_new_from_static_template (&src_template_factory, "src");
  gst_element_add_pad (GST_ELEMENT (spectrum), spectrum->srcpad);

  spectrum->width = 75;
  spectrum->base = 8;
  spectrum->len = 1024;

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
      spectrum->width = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_spectrum_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpectrum *spectrum;
  gint16 *samples;
  gint step, pos, num, i;
  guchar *spect;
  GstBuffer *newbuf;

  spectrum = GST_SPECTRUM (GST_OBJECT_PARENT (pad));
  samples = (gint16 *) GST_BUFFER_DATA (buffer);

  GST_LOG ("buffer-size = %ld", GST_BUFFER_SIZE (buffer));

  /* FIXME:need a gst_adapter */
  num = GST_BUFFER_SIZE (buffer) / 2;
  num = MIN (num, spectrum->len);

  for (i = 0; i < num; i++)
    spectrum->re[i] = (samples[(i * 2)] + samples[(i * 2) + 1]) >> 1;

  gst_spectrum_window (spectrum->re, spectrum->len);
  gst_spectrum_fix_fft (spectrum->re, spectrum->im, spectrum->base, FALSE);
  gst_spectrum_fix_loud (spectrum->loud, spectrum->re, spectrum->im,
      spectrum->len, 0);

  /* resample to requested width */
  step = spectrum->len / (spectrum->width * 4); /* <-- shouldn't this be 2 instead of 4? */
  spect = (guchar *) g_malloc (spectrum->width);
  for (i = 0, pos = 0; i < spectrum->width; i++, pos += step) {
    /* > -60 db? */
    if (spectrum->loud[pos] > -60) {
      spect[i] = spectrum->loud[pos] + 60;
      /*
         if (spect[i] > 15);
         spect[i] = 15;
       */
    } else
      /* treat as silence */
      spect[i] = 0;
  }
  gst_buffer_unref (buffer);

  newbuf = gst_buffer_new ();
  GST_BUFFER_DATA (newbuf) = spect;
  GST_BUFFER_SIZE (newbuf) = spectrum->width;

  return gst_pad_push (spectrum->srcpad, newbuf);
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
