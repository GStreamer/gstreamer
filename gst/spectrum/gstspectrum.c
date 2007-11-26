/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Stefan Kost <ensonic@users.sf.net>
 *               <2007> Sebastian Dröge <slomo@circular-chaos.org>
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
 * If <link linkend="GstSpectrum--message">message property</link> is #TRUE it
 * sends analysis results as application message named
 * <classname>&quot;spectrum&quot;</classname> after each interval of time given
 * by the <link linkend="GstSpectrum--interval">interval property</link>.
 * The message's structure contains two fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;endtime&quot;</classname>:
 *   the end time of the buffer that triggered the message
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gfloat
 *   <classname>&quot;magnitude&quot;</classname>:
 *   the level for each frequency band in dB. All values below the value of the
 *   <link linkend="GstSpectrum--threshold">threshold property</link> will
 *   be set to the threshold.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gfloat
 *   <classname>&quot;phase&quot;</classname>:
 *   the phase for each frequency band. The value is between -pi and pi.
 *   </para>
 * </listitem>

 *
 * This element cannot be used with the gst-launch command in a sensible way.
 * The included demo shows how to use it in an application.
 *
 * Last reviewed on 2007-11-11 (0.10.6)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <math.h>
#include "gstspectrum.h"

#include <gst/fft/gstfft.h>
#include <gst/fft/gstffts16.h>
#include <gst/fft/gstffts32.h>
#include <gst/fft/gstfftf32.h>
#include <gst/fft/gstfftf64.h>

GST_DEBUG_CATEGORY_STATIC (gst_spectrum_debug);
#define GST_CAT_DEFAULT gst_spectrum_debug

/* elementfactory information */
static const GstElementDetails gst_spectrum_details =
GST_ELEMENT_DETAILS ("Spectrum analyzer",
    "Filter/Analyzer/Audio",
    "Run an FFT on the audio signal, output spectrum data",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Stefan Kost <ensonic@users.sf.net>, "
    "Sebastian Dröge <slomo@circular-chaos.org>");

#define ALLOWED_CAPS \
    "audio/x-raw-int, "                                               \
    " width = (int) 16, "                                             \
    " depth = (int) 16, "                                             \
    " signed = (boolean) true, "                                      \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]; "                                  \
    "audio/x-raw-int, "                                               \
    " width = (int) 32, "                                             \
    " depth = (int) 32, "                                             \
    " signed = (boolean) true, "                                      \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]; "                                  \
    "audio/x-raw-float, "                                             \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]"

/* Spectrum properties */
#define DEFAULT_SIGNAL_SPECTRUM		TRUE
#define DEFAULT_SIGNAL_MAGNITUDE	TRUE
#define DEFAULT_SIGNAL_PHASE		FALSE
#define DEFAULT_SIGNAL_INTERVAL		(GST_SECOND / 10)
#define DEFAULT_BANDS			128
#define DEFAULT_THRESHOLD		-60

#define SPECTRUM_WINDOW_BASE 9
#define SPECTRUM_WINDOW_LEN (1 << (SPECTRUM_WINDOW_BASE+1))

enum
{
  PROP_0,
  PROP_SIGNAL_SPECTRUM,
  PROP_SIGNAL_MAGNITUDE,
  PROP_SIGNAL_PHASE,
  PROP_SIGNAL_INTERVAL,
  PROP_BANDS,
  PROP_THRESHOLD
};

GST_BOILERPLATE (GstSpectrum, gst_spectrum, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static void gst_spectrum_dispose (GObject * object);
static void gst_spectrum_finalize (GObject * object);
static void gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_spectrum_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_spectrum_start (GstBaseTransform * trans);
static gboolean gst_spectrum_stop (GstBaseTransform * trans);
static gboolean gst_spectrum_event (GstBaseTransform * trans, GstEvent * event);
static GstFlowReturn gst_spectrum_transform_ip (GstBaseTransform * trans,
    GstBuffer * in);
static gboolean gst_spectrum_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

static void process_s16 (GstSpectrum * spectrum, const gint16 * samples);
static void process_s32 (GstSpectrum * spectrum, const gint32 * samples);
static void process_f32 (GstSpectrum * spectrum, const gfloat * samples);
static void process_f64 (GstSpectrum * spectrum, const gdouble * samples);

static void
gst_spectrum_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &gst_spectrum_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_spectrum_class_init (GstSpectrumClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *filter_class = GST_AUDIO_FILTER_CLASS (klass);

  gobject_class->set_property = gst_spectrum_set_property;
  gobject_class->get_property = gst_spectrum_get_property;
  gobject_class->dispose = gst_spectrum_dispose;
  gobject_class->finalize = gst_spectrum_finalize;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_spectrum_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_spectrum_stop);
  trans_class->event = GST_DEBUG_FUNCPTR (gst_spectrum_event);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_spectrum_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;

  filter_class->setup = GST_DEBUG_FUNCPTR (gst_spectrum_setup);

  g_object_class_install_property (gobject_class, PROP_SIGNAL_SPECTRUM,
      g_param_spec_boolean ("message", "Message",
          "Post a level message for each passed interval",
          DEFAULT_SIGNAL_SPECTRUM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SIGNAL_MAGNITUDE,
      g_param_spec_boolean ("message-magnitude", "Magnitude",
          "Post the magnitude of the spectrum",
          DEFAULT_SIGNAL_MAGNITUDE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SIGNAL_PHASE,
      g_param_spec_boolean ("message-phase", "Phase",
          "Post the phase of the spectrum",
          DEFAULT_SIGNAL_PHASE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SIGNAL_INTERVAL,
      g_param_spec_uint64 ("interval", "Interval",
          "Interval of time between message posts (in nanoseconds)",
          1, G_MAXUINT64, DEFAULT_SIGNAL_INTERVAL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_BANDS,
      g_param_spec_uint ("bands", "Bands", "number of frequency bands",
          0, G_MAXUINT, DEFAULT_BANDS, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold",
          "dB threshold for result. All lower values will be set to this",
          G_MININT, 0, DEFAULT_THRESHOLD, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (gst_spectrum_debug, "spectrum", 0,
      "audio spectrum analyser element");
}

static void
gst_spectrum_init (GstSpectrum * spectrum, GstSpectrumClass * g_class)
{
  spectrum->adapter = gst_adapter_new ();

  spectrum->message = DEFAULT_SIGNAL_SPECTRUM;
  spectrum->message_magnitude = DEFAULT_SIGNAL_MAGNITUDE;
  spectrum->message_phase = DEFAULT_SIGNAL_PHASE;
  spectrum->interval = DEFAULT_SIGNAL_INTERVAL;
  spectrum->bands = DEFAULT_BANDS;
  spectrum->threshold = DEFAULT_THRESHOLD;

  spectrum->spect_magnitude = g_new0 (gfloat, spectrum->bands);
  spectrum->spect_phase = g_new0 (gfloat, spectrum->bands);
}

static void
gst_spectrum_dispose (GObject * object)
{
  GstSpectrum *spectrum = GST_SPECTRUM (object);

  if (spectrum->adapter) {
    g_object_unref (spectrum->adapter);
    spectrum->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_spectrum_finalize (GObject * object)
{
  GstSpectrum *spectrum = GST_SPECTRUM (object);

  g_free (spectrum->in);
  if (spectrum->fft_free_func) {
    spectrum->fft_free_func (spectrum->fft_ctx);
    spectrum->fft_ctx = NULL;
    spectrum->fft_free_func = NULL;
  }
  g_free (spectrum->freqdata);
  g_free (spectrum->spect_magnitude);
  g_free (spectrum->spect_phase);

  spectrum->in = NULL;
  spectrum->spect_magnitude = NULL;
  spectrum->spect_phase = NULL;
  spectrum->freqdata = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpectrum *filter = GST_SPECTRUM (object);

  switch (prop_id) {
    case PROP_SIGNAL_SPECTRUM:
      filter->message = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_MAGNITUDE:
      filter->message_magnitude = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_PHASE:
      filter->message_phase = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_INTERVAL:
      filter->interval = g_value_get_uint64 (value);
      break;
    case PROP_BANDS:
      GST_BASE_TRANSFORM_LOCK (filter);

      filter->bands = g_value_get_uint (value);
      g_free (filter->spect_magnitude);
      g_free (filter->spect_phase);
      g_free (filter->in);
      g_free (filter->freqdata);

      if (filter->fft_free_func) {
        filter->fft_free_func (filter->fft_ctx);
        filter->fft_ctx = NULL;
        filter->fft_free_func = NULL;
      }

      filter->in = NULL;
      filter->freqdata = NULL;
      filter->spect_magnitude = g_new0 (gfloat, filter->bands);
      filter->spect_phase = g_new0 (gfloat, filter->bands);
      filter->num_frames = 0;
      filter->num_fft = 0;
      GST_BASE_TRANSFORM_UNLOCK (filter);
      GST_DEBUG_OBJECT (filter, "reallocation, spect = %p, bands =%d ",
          filter->spect_magnitude, filter->bands);
      break;
    case PROP_THRESHOLD:
      filter->threshold = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_spectrum_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpectrum *filter = GST_SPECTRUM (object);

  switch (prop_id) {
    case PROP_SIGNAL_SPECTRUM:
      g_value_set_boolean (value, filter->message);
      break;
    case PROP_SIGNAL_MAGNITUDE:
      g_value_set_boolean (value, filter->message_magnitude);
      break;
    case PROP_SIGNAL_PHASE:
      g_value_set_boolean (value, filter->message_phase);
      break;
    case PROP_SIGNAL_INTERVAL:
      g_value_set_uint64 (value, filter->interval);
      break;
    case PROP_BANDS:
      g_value_set_uint (value, filter->bands);
      break;
    case PROP_THRESHOLD:
      g_value_set_int (value, filter->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_spectrum_start (GstBaseTransform * trans)
{
  GstSpectrum *filter = GST_SPECTRUM (trans);

  filter->num_frames = 0;
  filter->num_fft = 0;
  if (filter->spect_magnitude)
    memset (filter->spect_magnitude, 0, filter->bands * sizeof (gfloat));
  if (filter->spect_phase)
    memset (filter->spect_phase, 0, filter->bands * sizeof (gfloat));

  return TRUE;
}

static gboolean
gst_spectrum_stop (GstBaseTransform * trans)
{
  GstSpectrum *filter = GST_SPECTRUM (trans);

  gst_adapter_clear (filter->adapter);

  return TRUE;
}

static gboolean
gst_spectrum_event (GstBaseTransform * trans, GstEvent * event)
{
  GstSpectrum *filter = GST_SPECTRUM (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
      gst_adapter_clear (filter->adapter);
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
gst_spectrum_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstSpectrum *filter = GST_SPECTRUM (base);

  if (filter->in) {
    g_free (filter->in);
    filter->in = NULL;
  }

  if (filter->fft_free_func) {
    filter->fft_free_func (filter->fft_ctx);
    filter->fft_ctx = NULL;
    filter->fft_free_func = NULL;
  }

  if (filter->freqdata) {
    g_free (filter->freqdata);
    filter->freqdata = NULL;
  }

  if (format->type == GST_BUFTYPE_LINEAR && format->width == 32)
    filter->process = (GstSpectrumProcessFunc) process_s32;
  else if (format->type == GST_BUFTYPE_LINEAR && format->width == 16)
    filter->process = (GstSpectrumProcessFunc) process_s16;
  else if (format->type == GST_BUFTYPE_FLOAT && format->width == 64)
    filter->process = (GstSpectrumProcessFunc) process_f64;
  else if (format->type == GST_BUFTYPE_FLOAT && format->width == 32)
    filter->process = (GstSpectrumProcessFunc) process_f32;
  else
    g_assert_not_reached ();

  return TRUE;
}

static GstMessage *
gst_spectrum_message_new (GstSpectrum * spectrum, GstClockTime endtime)
{
  GstStructure *s;
  GValue v = { 0, };
  GValue *l;
  guint i;
  gfloat *spect_magnitude = spectrum->spect_magnitude;
  gfloat *spect_phase = spectrum->spect_phase;

  GST_DEBUG_OBJECT (spectrum, "preparing message, spect = %p, bands =%d ",
      spect_magnitude, spectrum->bands);

  s = gst_structure_new ("spectrum", "endtime", GST_TYPE_CLOCK_TIME,
      endtime, NULL);

  if (spectrum->message_magnitude) {
    g_value_init (&v, GST_TYPE_LIST);
    /* will copy-by-value */
    gst_structure_set_value (s, "magnitude", &v);
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_FLOAT);
    l = (GValue *) gst_structure_get_value (s, "magnitude");
    for (i = 0; i < spectrum->bands; i++) {
      g_value_set_float (&v, spect_magnitude[i]);
      gst_value_list_append_value (l, &v);      /* copies by value */
    }
    g_value_unset (&v);
  }

  if (spectrum->message_phase) {
    g_value_init (&v, GST_TYPE_LIST);
    /* will copy-by-value */
    gst_structure_set_value (s, "phase", &v);
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_FLOAT);
    l = (GValue *) gst_structure_get_value (s, "phase");
    for (i = 0; i < spectrum->bands; i++) {
      g_value_set_float (&v, spect_phase[i]);
      gst_value_list_append_value (l, &v);      /* copies by value */
    }
    g_value_unset (&v);
  }

  return gst_message_new_element (GST_OBJECT (spectrum), s);
}

#define DEFINE_PROCESS_FUNC_INT(width,next_width,max) \
static void \
process_s##width (GstSpectrum *spectrum, const gint##width *samples) \
{ \
  gfloat *spect_magnitude = spectrum->spect_magnitude; \
  gfloat *spect_phase = spectrum->spect_phase; \
  gint channels = GST_AUDIO_FILTER (spectrum)->format.channels; \
  gint i, j, k; \
  gint##next_width acc; \
  GstFFTS##width##Complex *freqdata; \
  GstFFTS##width *ctx; \
  gint##width *in; \
  gint nfft = 2 * spectrum->bands - 2; \
  \
  if (!spectrum->in) \
    spectrum->in = (guint8 *) g_new (gint##width, nfft); \
  \
  in = (gint##width *) spectrum->in; \
  \
  for (i = 0, j = 0; i < nfft; i++) { \
    /* convert to mono */ \
    for (k = 0, acc = 0; k < channels; k++) \
      acc += samples[j++]; \
    in[i] = (gint##width) (acc / channels); \
  } \
  \
  if (!spectrum->fft_ctx) { \
    spectrum->fft_ctx = gst_fft_s##width##_new (nfft, FALSE); \
    spectrum->fft_free_func = (GstSpectrumFFTFreeFunc) gst_fft_s##width##_free; \
  } \
  ctx = spectrum->fft_ctx; \
  \
  gst_fft_s##width##_window (ctx, in, GST_FFT_WINDOW_HAMMING); \
  \
  if (!spectrum->freqdata) \
    spectrum->freqdata = g_new (GstFFTS##width##Complex, spectrum->bands); \
  \
  freqdata = (GstFFTS##width##Complex *) spectrum->freqdata; \
  \
  gst_fft_s##width##_fft (ctx, in, freqdata); \
  spectrum->num_fft++; \
  \
  /* Calculate magnitude in db */ \
  for (i = 0; i < spectrum->bands; i++) { \
    gdouble val = 0.0; \
    val = (gdouble) freqdata[i].r * (gdouble) freqdata[i].r; \
    val += (gdouble) freqdata[i].i * (gdouble) freqdata[i].i; \
    val /= max*max; \
    val = 10.0 * log10 (val); \
    if (val < spectrum->threshold) \
      val = spectrum->threshold; \
    spect_magnitude[i] += val; \
  } \
   \
  /* Calculate phase */ \
  for (i = 0; i < spectrum->bands; i++) \
    spect_phase[i] += atan2 (freqdata[i].i, freqdata[i].r); \
   \
}

DEFINE_PROCESS_FUNC_INT (16, 32, 32767.0);
DEFINE_PROCESS_FUNC_INT (32, 64, 2147483647.0);

#define DEFINE_PROCESS_FUNC_FLOAT(width,type) \
static void \
process_f##width (GstSpectrum *spectrum, const g##type *samples) \
{ \
  gfloat *spect_magnitude = spectrum->spect_magnitude; \
  gfloat *spect_phase = spectrum->spect_phase; \
  gint channels = GST_AUDIO_FILTER (spectrum)->format.channels; \
  gint i, j, k; \
  g##type acc; \
  GstFFTF##width##Complex *freqdata; \
  GstFFTF##width *ctx; \
  g##type *in; \
  gint nfft = 2 * spectrum->bands - 2; \
  \
  if (!spectrum->in) \
    spectrum->in = (guint8 *) g_new (g##type, nfft); \
  \
  in = (g##type *) spectrum->in; \
  \
  for (i = 0, j = 0; i < nfft; i++) { \
    /* convert to mono */ \
    for (k = 0, acc = 0; k < channels; k++) \
      acc += samples[j++]; \
    in[i] = (g##type) (acc / channels); \
    if (abs (in[i]) > 1.0) \
      g_assert_not_reached(); \
  } \
  \
  if (!spectrum->fft_ctx) { \
    spectrum->fft_ctx = gst_fft_f##width##_new (nfft, FALSE); \
    spectrum->fft_free_func = (GstSpectrumFFTFreeFunc) gst_fft_f##width##_free; \
  } \
  ctx = spectrum->fft_ctx; \
  \
  gst_fft_f##width##_window (ctx, in, GST_FFT_WINDOW_HAMMING); \
  \
  if (!spectrum->freqdata) \
    spectrum->freqdata = g_new (GstFFTF##width##Complex, spectrum->bands); \
  \
  freqdata = (GstFFTF##width##Complex *) spectrum->freqdata; \
  \
  gst_fft_f##width##_fft (ctx, in, freqdata); \
  spectrum->num_fft++; \
  \
  /* Calculate magnitude in db */ \
  for (i = 0; i < spectrum->bands; i++) { \
    gdouble val = 0.0; \
    val = freqdata[i].r * freqdata[i].r; \
    val += freqdata[i].i * freqdata[i].i; \
    val /= nfft*nfft; \
    val = 10.0 * log10 (val); \
    if (val < spectrum->threshold) \
      val = spectrum->threshold; \
    spect_magnitude[i] += val; \
  } \
   \
  /* Calculate phase */ \
  for (i = 0; i < spectrum->bands; i++) \
    spect_phase[i] += atan2 (freqdata[i].i, freqdata[i].r); \
   \
}

DEFINE_PROCESS_FUNC_FLOAT (32, float);
DEFINE_PROCESS_FUNC_FLOAT (64, double);

static GstFlowReturn
gst_spectrum_transform_ip (GstBaseTransform * trans, GstBuffer * in)
{
  GstSpectrum *spectrum = GST_SPECTRUM (trans);
  gint wanted;
  gint i;
  gfloat *spect_magnitude = spectrum->spect_magnitude;
  gfloat *spect_phase = spectrum->spect_phase;
  gint rate = GST_AUDIO_FILTER (spectrum)->format.rate;
  gint channels = GST_AUDIO_FILTER (spectrum)->format.channels;
  gint width = GST_AUDIO_FILTER (spectrum)->format.width / 8;
  gint nfft = 2 * spectrum->bands - 2;

  GstClockTime endtime =
      gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (in));
  GstClockTime blktime = GST_FRAMES_TO_CLOCK_TIME (nfft, rate);

  GST_LOG_OBJECT (spectrum, "input size: %d bytes", GST_BUFFER_SIZE (in));

  /* can we do this nicer? */
  gst_adapter_push (spectrum->adapter, gst_buffer_copy (in));
  /* required number of bytes */
  wanted = channels * nfft * width;

  while (gst_adapter_available (spectrum->adapter) >= wanted) {
    const guint8 *samples;

    samples = gst_adapter_peek (spectrum->adapter, wanted);

    spectrum->process (spectrum, samples);

    spectrum->num_frames += nfft;
    endtime += blktime;
    /* do we need to message ? */
    if (spectrum->num_frames >=
        GST_CLOCK_TIME_TO_FRAMES (spectrum->interval, rate)) {
      if (spectrum->message) {
        GstMessage *m;

        /* Calculate average */
        for (i = 0; i < spectrum->bands; i++) {
          spect_magnitude[i] /= spectrum->num_fft;
          spect_phase[i] /= spectrum->num_fft;
        }

        m = gst_spectrum_message_new (spectrum, endtime);

        gst_element_post_message (GST_ELEMENT (spectrum), m);
      }
      memset (spect_magnitude, 0, spectrum->bands * sizeof (gfloat));
      memset (spect_phase, 0, spectrum->bands * sizeof (gfloat));
      spectrum->num_frames = 0;
      spectrum->num_fft = 0;
    }

    gst_adapter_flush (spectrum->adapter, wanted);
  }

  return GST_FLOW_OK;
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
