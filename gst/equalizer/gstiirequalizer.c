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
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

typedef struct _GstIirEqualizer GstIirEqualizer;
typedef struct _GstIirEqualizerClass GstIirEqualizerClass;

#define GST_TYPE_IIR_EQUALIZER \
  (gst_iir_equalizer_get_type())
#define GST_IIR_EQUALIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IIR_EQUALIZER,GstIirEqualizer))
#define GST_IIR_EQUALIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IIR_EQUALIZER,GstIirEqualizerClass))
#define GST_IS_IIR_EQUALIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IIR_EQUALIZER))
#define GST_IS_IIR_EQUALIZER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IIR_EQUALIZER))

#define LOWEST_FREQ (20.0)
#define HIGHEST_FREQ (20000.0)

typedef void (*ProcessFunc) (GstIirEqualizer * equ, guint8 * data, guint size,
    guint channels);

typedef struct
{
  gdouble alpha;                /* IIR coefficients for outputs */
  gdouble beta;                 /* IIR coefficients for inputs */
  gdouble gamma;                /* IIR coefficients for inputs */
} SecondOrderFilter;

struct _GstIirEqualizer
{
  GstAudiofilter audiofilter;

  /* properties */
  guint freq_count;
  gdouble bandwidth;
  gdouble *freqs;
  gdouble *values;

  /* data */
  SecondOrderFilter *filter;
  gpointer history;
  ProcessFunc process;
  guint history_size;
};

struct _GstIirEqualizerClass
{
  GstAudiofilterClass audiofilter_class;
};

enum
{
  ARG_0,
  ARG_BANDS,
  ARG_BANDWIDTH,
  ARG_VALUES
      /* FILL ME */
};

static void gst_iir_equalizer_base_init (gpointer g_class);
static void gst_iir_equalizer_class_init (gpointer g_class,
    gpointer class_data);
static void gst_iir_equalizer_init (GTypeInstance * instance, gpointer g_class);
static void gst_iir_equalizer_finalize (GObject * object);

static void gst_iir_equalizer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_iir_equalizer_setup (GstAudiofilter * iir_equalizer);
static void gst_iir_equalizer_filter_inplace (GstAudiofilter *
    iir_equalizer, GstBuffer * buf);

static GstAudiofilterClass *parent_class;

GType
gst_iir_equalizer_get_type (void)
{
  static GType iir_equalizer_type = 0;

  if (!iir_equalizer_type) {
    static const GTypeInfo iir_equalizer_info = {
      sizeof (GstIirEqualizerClass),
      gst_iir_equalizer_base_init,
      NULL,
      gst_iir_equalizer_class_init,
      NULL,
      gst_iir_equalizer_init,
      sizeof (GstIirEqualizer),
      0,
      NULL,
    };

    iir_equalizer_type = g_type_register_static (GST_TYPE_AUDIOFILTER,
        "GstIirEqualizer", &iir_equalizer_info, 0);
  }
  return iir_equalizer_type;
}

static void
gst_iir_equalizer_base_init (gpointer g_class)
{
  static const GstElementDetails iir_equalizer_details =
      GST_ELEMENT_DETAILS ("Equalizer",
      "Filter/Effect/Audio",
      "Direct Form IIR equalizer",
      "Benjamin Otte <otte@gnome.org>");
  GstIirEqualizerClass *klass = (GstIirEqualizerClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &iir_equalizer_details);

  caps = gst_caps_from_string ("audio/x-raw-int, depth=(int)16, width=(int)16, "
      "endianness=(int)BYTE_ORDER, signed=(bool)TRUE, "
      "rate=(int)[1000,MAX], channels=(int)[1,6];"
      "audio/x-raw-float, width=(int)32, endianness=(int)BYTE_ORDER,"
      "rate=(int)[1000,MAX], channels=(int)[1,6]");
  gst_audiofilter_class_add_pad_templates (GST_AUDIOFILTER_CLASS (g_class),
      caps);
  gst_caps_free (caps);
}

static void
gst_iir_equalizer_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstIirEqualizerClass *klass;
  GstAudiofilterClass *audiofilter_class;

  klass = (GstIirEqualizerClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  audiofilter_class = (GstAudiofilterClass *) g_class;

  gobject_class->set_property = gst_iir_equalizer_set_property;
  gobject_class->get_property = gst_iir_equalizer_get_property;
  gobject_class->finalize = gst_iir_equalizer_finalize;

  parent_class = g_type_class_peek_parent (g_class);

  g_object_class_install_property (gobject_class, ARG_BANDS,
      g_param_spec_uint ("bands", "bands", "number of different bands to use",
          2, 64, 15, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_BANDWIDTH,
      g_param_spec_double ("bandwidth", "bandwidth",
          "bandwidth calculated as distance between bands * this value", 0.1,
          5.0, 1.0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  /* FIXME FIXME FIXME */
  g_object_class_install_property (gobject_class, ARG_VALUES,
      g_param_spec_pointer ("values", "values",
          "expects a gdouble* of values to use for the bands",
          G_PARAM_WRITABLE));

  audiofilter_class->setup = gst_iir_equalizer_setup;
  audiofilter_class->filter_inplace = gst_iir_equalizer_filter_inplace;
}

static void
gst_iir_equalizer_init (GTypeInstance * instance, gpointer g_class)
{
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
      1.0 / (equ->freq_count - 1)) * equ->bandwidth;
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
  GstAudiofilter *audio = GST_AUDIOFILTER (equ);

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
  equ->history =
      g_realloc (equ->history,
      equ->history_size * audio->channels * band_count);
  memset (equ->history, 0, equ->history_size * audio->channels * band_count);
  equ->freqs[0] = LOWEST_FREQ;
  for (i = 1; i < band_count; i++) {
    equ->freqs[i] = equ->freqs[i - 1] * step;
  }

  if (audio->rate) {
    guint i;

    for (i = 0; i < band_count; i++) {
      setup_filter (equ, &equ->filter[i], arg_to_scale (equ->values[i]),
          equ->freqs[i] / audio->rate);
    }
  }
}

static void
gst_iir_equalizer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  switch (prop_id) {
    case ARG_BANDS:
      gst_iir_equalizer_compute_frequencies (equ, g_value_get_uint (value));
      break;
    case ARG_BANDWIDTH:
      if (g_value_get_double (value) != equ->bandwidth) {
        equ->bandwidth = g_value_get_double (value);
        if (GST_AUDIOFILTER (equ)->rate) {
          guint i;

          for (i = 0; i < equ->freq_count; i++) {
            setup_filter (equ, &equ->filter[i], arg_to_scale (equ->values[i]),
                equ->freqs[i] / GST_AUDIOFILTER (equ)->rate);
          }
        }
      }
      break;
    case ARG_VALUES:
    {
      gdouble *new = g_value_get_pointer (value);
      guint i;

      for (i = 0; i < equ->freq_count; i++) {
        if (new[i] != equ->values[i]) {
          equ->values[i] = new[i];
          setup_filter (equ, &equ->filter[i], arg_to_scale (new[i]),
              equ->freqs[i] / GST_AUDIOFILTER (equ)->rate);
        }
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iir_equalizer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  switch (prop_id) {
    case ARG_BANDS:
      g_value_set_uint (value, equ->freq_count);
      break;
    case ARG_BANDWIDTH:
      g_value_set_double (value, equ->bandwidth);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

static void
gst_iir_equalizer_filter_inplace (GstAudiofilter * filter, GstBuffer * buf)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (filter);

  equ->process (equ, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
      filter->channels);
}

static void
gst_iir_equalizer_setup (GstAudiofilter * audio)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (audio);

  if (audio->width == 16) {
    equ->history_size = history_size_gint16;
    equ->process = gst_iir_equ_process_gint16;
  } else if (audio->width == 32) {
    equ->history_size = history_size_gfloat;
    equ->process = gst_iir_equ_process_gfloat;
  } else {
    g_assert_not_reached ();
  }
  gst_iir_equalizer_compute_frequencies (equ, equ->freq_count);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstaudiofilter"))
    return FALSE;

  return gst_element_register (plugin, "equalizer", GST_RANK_NONE,
      GST_TYPE_IIR_EQUALIZER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "equalizer",
    "GStreamer equalizers",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
