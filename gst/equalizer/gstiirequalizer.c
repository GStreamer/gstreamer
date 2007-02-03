/* GStreamer
 * Copyright (C) <2004> Benjamin Otte <otte@gnome.org>
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

#include <math.h>
#include <string.h>

#include "gstiirequalizer.h"

#define GST_EQUALIZER_TRANSFORM_LOCK(eq) \
    g_mutex_lock (GST_BASE_TRANSFORM(eq)->transform_lock)

#define GST_EQUALIZER_TRANSFORM_UNLOCK(eq) \
    g_mutex_unlock (GST_BASE_TRANSFORM(eq)->transform_lock)

enum
{
  ARG_0,
  ARG_NUM_BANDS,
  ARG_BAND_WIDTH,
  ARG_BAND_VALUES
};

static void gst_iir_equalizer_finalize (GObject * object);
static void gst_iir_equalizer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_iir_equalizer_setup (GstAudioFilter * filter,
    GstRingBufferSpec * fmt);
static GstFlowReturn gst_iir_equalizer_transform_ip (GstBaseTransform * btrans,
    GstBuffer * buf);

GST_DEBUG_CATEGORY_STATIC (equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug

#define ALLOWED_CAPS \
    "audio/x-raw-int,"                                                \
    " depth=(int)16,"                                                 \
    " width=(int)16,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " signed=(bool)TRUE,"                                             \
    " rate=(int)[1000,MAX],"                                          \
    " channels=(int)[1,MAX]; "                                        \
    "audio/x-raw-float,"                                              \
    " width=(int)32,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " rate=(int)[1000,MAX],"                                          \
    " channels=(int)[1,MAX]"

GST_BOILERPLATE (GstIirEqualizer, gst_iir_equalizer, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static void
gst_iir_equalizer_base_init (gpointer g_class)
{
  GstAudioFilterClass *audiofilter_class = GST_AUDIO_FILTER_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  const GstElementDetails iir_equalizer_details =
      GST_ELEMENT_DETAILS ("Equalizer",
      "Filter/Effect/Audio",
      "Direct Form IIR equalizer",
      "Benjamin Otte <otte@gnome.org>");
  GstCaps *caps;

  gst_element_class_set_details (element_class, &iir_equalizer_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (audiofilter_class, caps);
  gst_caps_unref (caps);
}

static void
gst_iir_equalizer_class_init (GstIirEqualizerClass * klass)
{
  GstAudioFilterClass *audio_filter_class = (GstAudioFilterClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_iir_equalizer_set_property;
  gobject_class->get_property = gst_iir_equalizer_get_property;
  gobject_class->finalize = gst_iir_equalizer_finalize;

  g_object_class_install_property (gobject_class, ARG_NUM_BANDS,
      g_param_spec_uint ("num-bands", "num-bands",
          "number of different bands to use", 2, 64, 15,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_BAND_WIDTH,
      g_param_spec_double ("band-width", "band-width",
          "band width calculated as distance between bands * this value", 0.1,
          5.0, 1.0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_BAND_VALUES,
      g_param_spec_value_array ("band-values", "band values",
          "GValueArray holding gdouble values, one for each band with values "
          "ranging from -1.0 to +1.0",
          g_param_spec_double ("band-value", "band-value",
              "Equaliser Band Value", -1.0, 1.0, 0.0, G_PARAM_WRITABLE),
          G_PARAM_WRITABLE));

  audio_filter_class->setup = gst_iir_equalizer_setup;
  btrans_class->transform_ip = gst_iir_equalizer_transform_ip;
}

static void
gst_iir_equalizer_init (GstIirEqualizer * eq, GstIirEqualizerClass * g_class)
{
  /* nothing to do here */
}

static void
gst_iir_equalizer_finalize (GObject * object)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  g_free (equ->freqs);
  g_free (equ->values);
  g_free (equ->filter);
  g_free (equ->history);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* args are in the range [-1 ... 1] with 0 meaning "no action"
 * convert to [-0.2 ... 1] with 0 meaning no action via the function
 * f(x) = 0.25 * 5 ^ x - 0.25
 */
static gdouble
arg_to_scale (gdouble arg)
{
  return 0.25 * exp (log (5) * arg) - 0.25;
}

static void
setup_filter (GstIirEqualizer * equ, SecondOrderFilter * filter, gdouble gain,
    gdouble frequency)
{
  gdouble q = pow (HIGHEST_FREQ / LOWEST_FREQ,
      1.0 / (equ->freq_count - 1)) * equ->band_width;
  gdouble theta = frequency * 2 * M_PI;

  filter->beta = (q - theta / 2) / (2 * q + theta);
  filter->gamma = (0.5 + filter->beta) * cos (theta);
  filter->alpha = (0.5 - filter->beta) / 2;

  filter->beta *= 2.0;
  filter->alpha *= 2.0 * gain;
  filter->gamma *= 2.0;
  GST_INFO ("gain = %g, frequency = %g, alpha = %g, beta = %g, gamma=%g\n",
      gain, frequency, filter->alpha, filter->beta, filter->gamma);
}

static void
gst_iir_equalizer_compute_frequencies (GstIirEqualizer * equ, guint band_count)
{
  gdouble *old_values;
  guint old_count, i;
  gdouble step = pow (HIGHEST_FREQ / LOWEST_FREQ, 1.0 / (band_count - 1));
  GstAudioFilter *audio = GST_AUDIO_FILTER (equ);

  old_count = equ->freq_count;
  equ->freq_count = band_count;
  old_values = equ->values;
  if (old_count < band_count) {
    equ->freqs = g_realloc (equ->freqs, sizeof (gdouble) * band_count);
    memset (equ->freqs + sizeof (gdouble) * old_count, 0,
        sizeof (gdouble) * (band_count - old_count));
    equ->values = g_realloc (equ->values, sizeof (gdouble) * band_count);
    memset (equ->values + sizeof (gdouble) * old_count, 0,
        sizeof (gdouble) * (band_count - old_count));
    equ->filter =
        g_realloc (equ->filter, sizeof (SecondOrderFilter) * band_count);
    memset (equ->filter + sizeof (SecondOrderFilter) * old_count, 0,
        sizeof (SecondOrderFilter) * (band_count - old_count));
  }

  /* free + alloc = no memcpy */
  g_free (equ->history);
  equ->history =
      g_malloc0 (equ->history_size * audio->format.channels * band_count);
  equ->freqs[0] = LOWEST_FREQ;
  for (i = 1; i < band_count; i++) {
    equ->freqs[i] = equ->freqs[i - 1] * step;
  }

  if (audio->format.rate > 0) {
    guint i;

    for (i = 0; i < band_count; i++) {
      setup_filter (equ, &equ->filter[i], arg_to_scale (equ->values[i]),
          equ->freqs[i] / audio->format.rate);
    }
  }
}

static void
gst_iir_equalizer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  GST_EQUALIZER_TRANSFORM_LOCK (equ);
  GST_OBJECT_LOCK (equ);
  switch (prop_id) {
    case ARG_NUM_BANDS:
      gst_iir_equalizer_compute_frequencies (equ, g_value_get_uint (value));
      break;
    case ARG_BAND_WIDTH:
      if (g_value_get_double (value) != equ->band_width) {
        equ->band_width = g_value_get_double (value);
        if (GST_AUDIO_FILTER (equ)->format.rate) {
          guint i;

          for (i = 0; i < equ->freq_count; i++) {
            setup_filter (equ, &equ->filter[i], arg_to_scale (equ->values[i]),
                equ->freqs[i] / GST_AUDIO_FILTER (equ)->format.rate);
          }
        }
      }
      break;
    case ARG_BAND_VALUES:{
      GValueArray *arr;

      arr = (GValueArray *) g_value_get_boxed (value);
      if (arr == NULL) {
        g_warning ("Application tried to set empty band value array");
      } else if (arr->n_values != equ->freq_count) {
        g_warning ("Application tried to set %u band values, but there are "
            "%u bands", arr->n_values, equ->freq_count);
      } else {
        guint i;

        for (i = 0; i < arr->n_values; ++i) {
          gdouble new_val;

          new_val = g_value_get_double (g_value_array_get_nth (arr, i));
          if (new_val != equ->values[i]) {
            equ->values[i] = new_val;
            setup_filter (equ, &equ->filter[i], arg_to_scale (new_val),
                equ->freqs[i] / GST_AUDIO_FILTER (equ)->format.rate);
          }
        }
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (equ);
  GST_EQUALIZER_TRANSFORM_UNLOCK (equ);
}

static void
gst_iir_equalizer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  GST_EQUALIZER_TRANSFORM_LOCK (equ);
  GST_OBJECT_LOCK (equ);
  switch (prop_id) {
    case ARG_NUM_BANDS:
      g_value_set_uint (value, equ->freq_count);
      break;
    case ARG_BAND_WIDTH:
      g_value_set_double (value, equ->band_width);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (equ);
  GST_EQUALIZER_TRANSFORM_UNLOCK (equ);
}

/* start of code that is type specific */

#define CREATE_OPTIMIZED_FUNCTIONS(TYPE,BIG_TYPE,MIN_VAL,MAX_VAL)       \
typedef struct {                                                        \
  TYPE x1, x2;          /* history of input values for a filter */      \
  TYPE y1, y2;          /* history of output values for a filter */     \
} SecondOrderHistory ## TYPE;                                           \
                                                                        \
static inline TYPE                                                      \
one_step_ ## TYPE (SecondOrderFilter *filter,                           \
    SecondOrderHistory ## TYPE *history, TYPE input)                    \
{                                                                       \
  /* calculate output */                                                \
  TYPE output = filter->alpha * (input - history->x2) +                 \
    filter->gamma * history->y1 - filter->beta * history->y2;           \
  /* update history */                                                  \
  history->y2 = history->y1;                                            \
  history->y1 = output;                                                 \
  history->x2 = history->x1;                                            \
  history->x1 = input;                                                  \
                                                                        \
  return output;                                                        \
}                                                                       \
                                                                        \
static const guint                                                      \
history_size_ ## TYPE = sizeof (SecondOrderHistory ## TYPE);            \
                                                                        \
static void                                                             \
gst_iir_equ_process_ ## TYPE (GstIirEqualizer *equ, guint8 *data,       \
guint size, guint channels)                                             \
{                                                                       \
  guint frames = size / channels / sizeof (TYPE);                       \
  guint i, c, f;                                                        \
  BIG_TYPE cur;                                                         \
  TYPE val;                                                             \
                                                                        \
  for (i = 0; i < frames; i++) {                                        \
    for (c = 0; c < channels; c++) {                                    \
      SecondOrderHistory ## TYPE *history = equ->history;               \
      val = *((TYPE *) data);                                           \
      cur = 0;                                                          \
      for (f = 0; f < equ->freq_count; f++) {                           \
        SecondOrderFilter *filter = &equ->filter[f];                    \
                                                                        \
        cur += one_step_ ## TYPE (filter, history, val);                \
        history++;                                                      \
      }                                                                 \
      cur += val * 0.25;                                                \
      cur = CLAMP (cur, MIN_VAL, MAX_VAL);                              \
      *((TYPE *) data) = (TYPE) cur;                                    \
      data += sizeof (TYPE);                                            \
    }                                                                   \
  }                                                                     \
}

CREATE_OPTIMIZED_FUNCTIONS (gint16, gint, -32768, 32767);
CREATE_OPTIMIZED_FUNCTIONS (gfloat, gfloat, -1.0, 1.0);

static GstFlowReturn
gst_iir_equalizer_transform_ip (GstBaseTransform * btrans, GstBuffer * buf)
{
  GstAudioFilter *filter = GST_AUDIO_FILTER (btrans);
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (btrans);

  if (G_UNLIKELY (filter->format.channels < 1 || equ->process == NULL))
    return GST_FLOW_NOT_NEGOTIATED;

  equ->process (equ, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
      filter->format.channels);

  return GST_FLOW_OK;
}

static gboolean
gst_iir_equalizer_setup (GstAudioFilter * audio, GstRingBufferSpec * fmt)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (audio);

  switch (fmt->width) {
    case 16:
      equ->history_size = history_size_gint16;
      equ->process = gst_iir_equ_process_gint16;
      break;
    case 32:
      equ->history_size = history_size_gfloat;
      equ->process = gst_iir_equ_process_gfloat;
      break;
    default:
      return FALSE;
  }
  gst_iir_equalizer_compute_frequencies (equ, equ->freq_count);
  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (equalizer_debug, "equalizer", 0, "equalizer");

  return gst_element_register (plugin, "equalizer", GST_RANK_NONE,
      GST_TYPE_IIR_EQUALIZER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "equalizer",
    "GStreamer equalizers",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
