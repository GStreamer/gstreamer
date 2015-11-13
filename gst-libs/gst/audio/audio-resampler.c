/* GStreamer
 * Copyright (C) <2015> Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "audio-resampler.h"

typedef struct _Tap
{
  gpointer taps;

  gint sample_inc;
  gint next_phase;
  gint size;
} Tap;

typedef void (*MakeTapsFunc) (GstAudioResampler * resampler, Tap * t, gint j);
typedef void (*ResampleFunc) (GstAudioResampler * resampler, gpointer in[],
    gsize in_len, gpointer out[], gsize out_len, gsize * consumed,
    gsize * produced, gboolean move);
typedef void (*DeinterleaveFunc) (GstAudioResampler * resampler,
    gpointer * sbuf, gpointer in[], gsize in_frames);
typedef void (*MirrorFunc) (GstAudioResampler * resampler, gpointer * sbuf);

struct _GstAudioResampler
{
  GstAudioResamplerMethod method;
  GstAudioResamplerFlags flags;
  GstAudioFormat format;
  GstStructure *options;
  guint channels;
  gint in_rate;
  gint out_rate;
  gint bps, bpf;
  gint ostride;

  gdouble cutoff;
  gdouble kaiser_beta;
  /* for cubic */
  gdouble b, c;

  guint n_taps;
  Tap *taps;
  gpointer coeff;
  gpointer tmpcoeff;

  DeinterleaveFunc deinterleave;
  MirrorFunc mirror;
  ResampleFunc resample;

  gboolean filling;
  gint samp_inc;
  gint samp_frac;
  gint samp_index;
  gint samp_phase;
  gint skip;

  gpointer samples;
  gsize samples_len;
  gsize samples_avail;
  gpointer *sbuf;
};

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("audio-resampler", 0,
        "audio-resampler object");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

/**
 * SECTION:gstaudioresampler
 * @short_description: Utility structure for resampler information
 *
 * #GstAudioResampler is a structure which holds the information
 * required to perform various kinds of resampling filtering.
 *
 */

typedef struct
{
  gdouble cutoff;
  gdouble downsample_cutoff_factor;
  gdouble stopband_attenuation;
  gdouble transition_bandwidth;
} KaiserQualityMap;

static const KaiserQualityMap kaiser_qualities[] = {
  {0.860, 0.96511, 60, 0.7},    /* 8 taps */
  {0.880, 0.96591, 65, 0.29},   /* 16 taps */
  {0.910, 0.96923, 70, 0.145},  /* 32 taps */
  {0.920, 0.97600, 80, 0.105},  /* 48 taps */
  {0.940, 0.97979, 85, 0.087},  /* 64 taps default quality */
  {0.940, 0.98085, 95, 0.077},  /* 80 taps */
  {0.945, 0.99471, 100, 0.068}, /* 96 taps */
  {0.950, 1.0, 105, 0.055},     /* 128 taps */
  {0.960, 1.0, 110, 0.045},     /* 160 taps */
  {0.968, 1.0, 115, 0.039},     /* 192 taps */
  {0.975, 1.0, 120, 0.0305}     /* 256 taps */
};

typedef struct
{
  guint n_taps;
  gdouble cutoff;
} BlackmanQualityMap;

static const BlackmanQualityMap blackman_qualities[] = {
  {8, 0.5,},
  {16, 0.6,},
  {24, 0.72,},
  {32, 0.8,},
  {48, 0.85,},                  /* default */
  {64, 0.90,},
  {80, 0.92,},
  {96, 0.933,},
  {128, 0.950,},
  {148, 0.955,},
  {160, 0.960,}
};

#define DEFAULT_QUALITY GST_AUDIO_RESAMPLER_QUALITY_DEFAULT
#define DEFAULT_OPT_CUBIC_B 1.0
#define DEFAULT_OPT_CUBIC_C 0.0

static gdouble
get_opt_double (GstStructure * options, const gchar * name, gdouble def)
{
  gdouble res;
  if (!options || !gst_structure_get_double (options, name, &res))
    res = def;
  return res;
}

static gint
get_opt_int (GstStructure * options, const gchar * name, gint def)
{
  gint res;
  if (!options || !gst_structure_get_int (options, name, &res))
    res = def;
  return res;
}

#define GET_OPT_CUTOFF(options,def) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_CUTOFF,def)
#define GET_OPT_DOWN_CUTOFF_FACTOR(options,def) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_DOWN_CUTOFF_FACTOR, def)
#define GET_OPT_STOP_ATTENUATION(options,def) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_STOP_ATTENUATION, def)
#define GET_OPT_TRANSITION_BANDWIDTH(options,def) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_TRANSITION_BANDWIDTH, def)
#define GET_OPT_CUBIC_B(options) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_CUBIC_B, DEFAULT_OPT_CUBIC_B)
#define GET_OPT_CUBIC_C(options) get_opt_double(options, \
    GST_AUDIO_RESAMPLER_OPT_CUBIC_C, DEFAULT_OPT_CUBIC_C)
#define GET_OPT_N_TAPS(options,def) get_opt_int(options, \
    GST_AUDIO_RESAMPLER_OPT_N_TAPS, def)

#include "dbesi0.c"
#define bessel dbesi0

static inline gdouble
get_nearest_tap (GstAudioResampler * resampler, gdouble x)
{
  gdouble a = fabs (x);

  if (a < 0.5)
    return 1.0;
  else
    return 0.0;
}

static inline gdouble
get_linear_tap (GstAudioResampler * resampler, gdouble x)
{
  gdouble a;

  a = fabs (x) / resampler->n_taps;

  if (a < 1.0)
    return 1.0 - a;
  else
    return 0.0;
}

static inline gdouble
get_cubic_tap (GstAudioResampler * resampler, gdouble x)
{
  gdouble a, a2, a3, b, c;

  a = fabs (x * 4.0) / resampler->n_taps;
  a2 = a * a;
  a3 = a2 * a;

  b = resampler->b;
  c = resampler->c;

  if (a <= 1.0)
    return ((12.0 - 9.0 * b - 6.0 * c) * a3 +
        (-18.0 + 12.0 * b + 6.0 * c) * a2 + (6.0 - 2.0 * b)) / 6.0;
  else if (a <= 2.0)
    return ((-b - 6.0 * c) * a3 +
        (6.0 * b + 30.0 * c) * a2 +
        (-12.0 * b - 48.0 * c) * a + (8.0 * b + 24.0 * c)) / 6.0;
  else
    return 0.0;
}

static inline gdouble
get_blackman_nuttall_tap (GstAudioResampler * resampler, gdouble x)
{
  gdouble s, y, w, Fc = resampler->cutoff;

  y = G_PI * x;
  s = (y == 0.0 ? Fc : sin (y * Fc) / y);

  w = 2.0 * y / resampler->n_taps + G_PI;
  return s * (0.3635819 - 0.4891775 * cos (w) + 0.1365995 * cos (2 * w) -
      0.0106411 * cos (3 * w));
}

static inline gdouble
get_kaiser_tap (GstAudioResampler * resampler, gdouble x)
{
  gdouble s, y, w, Fc = resampler->cutoff;

  y = G_PI * x;
  s = (y == 0.0 ? Fc : sin (y * Fc) / y);

  w = 2.0 * x / resampler->n_taps;
  return s * bessel (resampler->kaiser_beta * sqrt (MAX (1 - w * w, 0)));
}

#define CONVERT_TAPS(type, precision)                                   \
G_STMT_START {                                                          \
  type *taps = t->taps = (type *) resampler->coeff + j * n_taps;        \
  gdouble multiplier = (1 << precision);                                \
  gint i, j;                                                            \
  gdouble offset, l_offset, h_offset;                                   \
  gboolean exact = FALSE;                                               \
  /* Round to integer, but with an adjustable bias that we use to */    \
  /* eliminate the DC error. */                                         \
  l_offset = 0.0;                                                       \
  h_offset = 1.0;                                                       \
  offset = 0.5;                                                         \
  for (i = 0; i < 32; i++) {                                            \
    gint64 sum = 0;                                                     \
    for (j = 0; j < n_taps; j++)                                        \
      sum += taps[j] = floor (offset + tmpcoeff[j] * multiplier / weight); \
    if (sum == (1 << precision)) {                                      \
      exact = TRUE;                                                     \
      break;                                                            \
    }                                                                   \
    if (l_offset == h_offset)                                           \
      break;                                                            \
    if (sum < (1 << precision)) {                                       \
      if (offset > l_offset)                                            \
        l_offset = offset;                                              \
      offset += (h_offset - l_offset) / 2;                              \
    } else {                                                            \
      if (offset < h_offset)                                            \
        h_offset = offset;                                              \
      offset -= (h_offset - l_offset) / 2;                              \
    }                                                                   \
  }                                                                     \
  if (!exact)                                                           \
    GST_WARNING ("can't find exact taps");                              \
} G_STMT_END

#include "audio-resampler-core.h"

static void
make_taps (GstAudioResampler * resampler, Tap * t, gint j)
{
  gint n_taps = resampler->n_taps;
  gdouble x, weight = 0.0;
  gdouble *tmpcoeff = resampler->tmpcoeff;
  gint tap_offs = n_taps / 2;
  gint out_rate = resampler->out_rate;
  gint l;

  x = ((double) (1.0 - tap_offs) - (double) j / out_rate);

  switch (resampler->method) {
    case GST_AUDIO_RESAMPLER_METHOD_NEAREST:
      for (l = 0; l < n_taps; l++, x += 1.0)
        weight += tmpcoeff[l] = get_nearest_tap (resampler, x);
      break;

    case GST_AUDIO_RESAMPLER_METHOD_LINEAR:
      for (l = 0; l < n_taps; l++, x += 1.0)
        weight += tmpcoeff[l] = get_linear_tap (resampler, x);
      break;

    case GST_AUDIO_RESAMPLER_METHOD_CUBIC:
      for (l = 0; l < n_taps; l++, x += 1.0)
        weight += tmpcoeff[l] = get_cubic_tap (resampler, x);
      break;

    case GST_AUDIO_RESAMPLER_METHOD_BLACKMAN_NUTTALL:
      for (l = 0; l < n_taps; l++, x += 1.0)
        weight += tmpcoeff[l] = get_blackman_nuttall_tap (resampler, x);
      break;

    case GST_AUDIO_RESAMPLER_METHOD_KAISER:
      for (l = 0; l < n_taps; l++, x += 1.0)
        weight += tmpcoeff[l] = get_kaiser_tap (resampler, x);
      break;

    default:
      break;
  }

  switch (resampler->format) {
    case GST_AUDIO_FORMAT_F64:
    {
      gdouble *taps = t->taps = (gdouble *) resampler->coeff + j * n_taps;
      for (l = 0; l < n_taps; l++)
        taps[l] = tmpcoeff[l] / weight;
      break;
    }
    case GST_AUDIO_FORMAT_F32:
    {
      gfloat *taps = t->taps = (gfloat *) resampler->coeff + j * n_taps;
      for (l = 0; l < n_taps; l++)
        taps[l] = tmpcoeff[l] / weight;
      break;
    }
    case GST_AUDIO_FORMAT_S32:
      CONVERT_TAPS (gint32, PRECISION_S32);
      break;
    case GST_AUDIO_FORMAT_S16:
      CONVERT_TAPS (gint16, PRECISION_S16);
      break;
    default:
      break;
  }
}

#define MAKE_RESAMPLE_FUNC(type)                                                \
static void                                                                     \
resample_ ##type (GstAudioResampler * resampler, gpointer in[], gsize in_len,   \
    gpointer out[], gsize out_len, gsize * consumed, gsize * produced,          \
    gboolean move)                                                              \
{                                                                               \
  gint c, di = 0;                                                               \
  gint n_taps = resampler->n_taps;                                              \
  gint channels = resampler->channels;                                          \
  gint ostride = resampler->ostride;                                            \
  gint samp_index = 0;                                                          \
  gint samp_phase = 0;                                                          \
                                                                                \
  for (c = 0; c < channels; c++) {                                              \
    type *ip = in[c];                                                           \
    type *op = ostride == 1 ? out[c] : (type *)out[0] + c;                      \
                                                                                \
    samp_index = resampler->samp_index;                                         \
    samp_phase = resampler->samp_phase;                                         \
                                                                                \
    for (di = 0; di < out_len; di++) {                                          \
      Tap *t = &resampler->taps[samp_phase];                                    \
      type *ipp = &ip[samp_index];                                              \
                                                                                \
      if (t->taps == NULL)                                                      \
        make_taps (resampler, t, samp_phase);                                   \
                                                                                \
      inner_product_ ##type (op, ipp, t->taps, n_taps);                         \
      op += ostride;                                                            \
                                                                                \
      samp_phase = t->next_phase;                                               \
      samp_index += t->sample_inc;                                              \
    }                                                                           \
    if (move)                                                                   \
      memmove (ip, &ip[samp_index], (in_len - samp_index) * sizeof(type));      \
  }                                                                             \
  *consumed = samp_index - resampler->samp_index;                               \
  *produced = di;                                                               \
                                                                                \
  resampler->samp_index = move ? 0 : samp_index;                                \
  resampler->samp_phase = samp_phase;                                           \
}

MAKE_RESAMPLE_FUNC (gdouble);
MAKE_RESAMPLE_FUNC (gfloat);
MAKE_RESAMPLE_FUNC (gint32);
MAKE_RESAMPLE_FUNC (gint16);

#define MAKE_RESAMPLE_INTERLEAVED_FUNC(type,channels)                                   \
static void                                                                             \
resample_interleaved_ ##type##_##channels (GstAudioResampler * resampler, gpointer in[],\
    gsize in_len, gpointer out[], gsize out_len, gsize * consumed, gsize * produced,    \
    gboolean move)                                                                      \
{                                                                                       \
  gint di = 0;                                                                          \
  gint n_taps = resampler->n_taps;                                                      \
  gint ostride = resampler->ostride;                                                    \
  gint samp_index = 0;                                                                  \
  gint samp_phase = 0;                                                                  \
                                                                                        \
  {                                                                                     \
    type *ip = in[0];                                                                   \
    type *op = out[0];                                                                  \
                                                                                        \
    samp_index = resampler->samp_index;                                                 \
    samp_phase = resampler->samp_phase;                                                 \
                                                                                        \
    for (di = 0; di < out_len; di++) {                                                  \
      Tap *t = &resampler->taps[samp_phase];                                            \
      type *ipp = &ip[samp_index * channels];                                           \
                                                                                        \
      if (t->taps == NULL)                                                              \
        make_taps (resampler, t, samp_phase);                                           \
                                                                                        \
      inner_product_ ##type## _##channels (op, ipp, t->taps, n_taps);                   \
                                                                                        \
      op += ostride;                                                                    \
      samp_phase = t->next_phase;                                                       \
      samp_index += t->sample_inc;                                                      \
    }                                                                                   \
    if (move)                                                                           \
      memmove (ip, &ip[samp_index * channels],                                          \
          (in_len - samp_index) * sizeof(type) * channels);                             \
  }                                                                                     \
  *consumed = samp_index - resampler->samp_index;                                       \
  *produced = di;                                                                       \
                                                                                        \
  resampler->samp_index = move ? 0 : samp_index;                                        \
  resampler->samp_phase = samp_phase;                                                   \
}

MAKE_RESAMPLE_INTERLEAVED_FUNC (gdouble, 2);
MAKE_RESAMPLE_INTERLEAVED_FUNC (gint16, 2);


#define MAKE_DEINTERLEAVE_FUNC(type)                                    \
static void                                                             \
deinterleave_ ##type (GstAudioResampler * resampler, gpointer sbuf[],   \
    gpointer in[], gsize in_frames)                                     \
{                                                                       \
  guint i, c, channels = resampler->channels;                           \
  gsize samples_avail = resampler->samples_avail;                       \
  for (c = 0; c < channels; c++) {                                      \
    type *s = (type *) sbuf[c] + samples_avail;                         \
    if (in == NULL) {                                                   \
      for (i = 0; i < in_frames; i++)                                   \
        s[i] = 0;                                                       \
    } else {                                                            \
      type *ip = (type *) in[0] + c;                                    \
      for (i = 0; i < in_frames; i++, ip += channels)                   \
        s[i] = *ip;                                                     \
    }                                                                   \
  }                                                                     \
}

MAKE_DEINTERLEAVE_FUNC (gdouble);
MAKE_DEINTERLEAVE_FUNC (gfloat);
MAKE_DEINTERLEAVE_FUNC (gint32);
MAKE_DEINTERLEAVE_FUNC (gint16);

static void
deinterleave_copy (GstAudioResampler * resampler, gpointer sbuf[],
    gpointer in[], gsize in_frames)
{
  gsize samples_avail = resampler->samples_avail;
  gint bpf = resampler->bpf;

  if (in == NULL)
    memset ((guint8 *) sbuf[0] + samples_avail * bpf, 0, in_frames * bpf);
  else
    memcpy ((guint8 *) sbuf[0] + samples_avail * bpf, in[0], in_frames * bpf);
}

static void
deinterleave_copy_n (GstAudioResampler * resampler, gpointer sbuf[],
    gpointer in[], gsize in_frames)
{
  guint c, channels = resampler->channels;
  gsize samples_avail = resampler->samples_avail;
  gint bps = resampler->bps;

  for (c = 0; c < channels; c++) {
    if (in == NULL)
      memset ((guint8 *) sbuf[c] + samples_avail * bps, 0, in_frames * bps);
    else
      memcpy ((guint8 *) sbuf[c] + samples_avail * bps, in[c], in_frames * bps);
  }
}

/* mirror input samples into the history when we have nothing else */
#define MAKE_MIRROR_FUNC(type)                                          \
static void                                                             \
mirror_ ##type (GstAudioResampler * resampler, gpointer sbuf[])         \
{                                                                       \
  guint i, c, channels = resampler->channels;                           \
  gint si = resampler->n_taps / 2;                                      \
  gint n_taps = resampler->n_taps;                                      \
  for (c = 0; c < channels; c++) {                                      \
    type *s = sbuf[c];                                                  \
    for (i = 0; i < si; i++)                                            \
      s[i] = -s[n_taps - i];                                            \
  }                                                                     \
}

MAKE_MIRROR_FUNC (gdouble);
MAKE_MIRROR_FUNC (gfloat);
MAKE_MIRROR_FUNC (gint32);
MAKE_MIRROR_FUNC (gint16);

static void
calculate_kaiser_params (GstAudioResampler * resampler)
{
  gdouble A, B, dw, tr_bw, Fc;
  gint n;
  const KaiserQualityMap *q = &kaiser_qualities[DEFAULT_QUALITY];

  /* default cutoff */
  Fc = q->cutoff;
  if (resampler->out_rate < resampler->in_rate)
    Fc *= q->downsample_cutoff_factor;

  Fc = GET_OPT_CUTOFF (resampler->options, Fc);
  A = GET_OPT_STOP_ATTENUATION (resampler->options, q->stopband_attenuation);
  tr_bw =
      GET_OPT_TRANSITION_BANDWIDTH (resampler->options,
      q->transition_bandwidth);

  GST_LOG ("Fc %f, A %f, tr_bw %f", Fc, A, tr_bw);

  /* calculate Beta */
  if (A > 50)
    B = 0.1102 * (A - 8.7);
  else if (A >= 21)
    B = 0.5842 * pow (A - 21, 0.4) + 0.07886 * (A - 21);
  else
    B = 0.0;
  /* calculate transition width in radians */
  dw = 2 * G_PI * (tr_bw);
  /* order of the filter */
  n = (A - 8.0) / (2.285 * dw);

  resampler->kaiser_beta = B;
  resampler->n_taps = n + 1;
  resampler->cutoff = Fc;

  GST_LOG ("using Beta %f n_taps %d cutoff %f", resampler->kaiser_beta,
      resampler->n_taps, resampler->cutoff);
}

static void
resampler_calculate_taps (GstAudioResampler * resampler)
{
  gint bps;
  gint j;
  gint n_taps;
  gint out_rate;
  gint in_rate;

  switch (resampler->method) {
    case GST_AUDIO_RESAMPLER_METHOD_NEAREST:
      resampler->n_taps = 2;
      break;
    case GST_AUDIO_RESAMPLER_METHOD_LINEAR:
      resampler->n_taps = GET_OPT_N_TAPS (resampler->options, 2);
      break;
    case GST_AUDIO_RESAMPLER_METHOD_CUBIC:
      resampler->n_taps = GET_OPT_N_TAPS (resampler->options, 4);
      resampler->b = GET_OPT_CUBIC_B (resampler->options);
      resampler->c = GET_OPT_CUBIC_C (resampler->options);;
      break;
    case GST_AUDIO_RESAMPLER_METHOD_BLACKMAN_NUTTALL:
    {
      const BlackmanQualityMap *q = &blackman_qualities[DEFAULT_QUALITY];
      resampler->n_taps = GET_OPT_N_TAPS (resampler->options, q->n_taps);
      resampler->cutoff = GET_OPT_CUTOFF (resampler->options, q->cutoff);
      break;
    }
    case GST_AUDIO_RESAMPLER_METHOD_KAISER:
      calculate_kaiser_params (resampler);
      break;
  }

  in_rate = resampler->in_rate;
  out_rate = resampler->out_rate;

  if (out_rate < in_rate) {
    resampler->cutoff = resampler->cutoff * out_rate / in_rate;
    resampler->n_taps = resampler->n_taps * in_rate / out_rate;
  }
  /* only round up for bigger taps, the small taps are used for nearest,
   * linear and cubic and we want to use less taps for those. */
  if (resampler->n_taps > 4)
    resampler->n_taps = GST_ROUND_UP_8 (resampler->n_taps);

  n_taps = resampler->n_taps;
  bps = resampler->bps;

  GST_LOG ("using n_taps %d cutoff %f", n_taps, resampler->cutoff);

  resampler->taps = g_realloc_n (resampler->taps, out_rate, sizeof (Tap));
  resampler->coeff = g_realloc_n (resampler->coeff, out_rate, bps * n_taps);
  resampler->tmpcoeff =
      g_realloc_n (resampler->tmpcoeff, n_taps, sizeof (gdouble));

  resampler->samp_inc = in_rate / out_rate;
  resampler->samp_frac = in_rate % out_rate;

  for (j = 0; j < out_rate; j++) {
    Tap *t = &resampler->taps[j];
    t->taps = NULL;
    t->sample_inc = (j + in_rate) / out_rate;
    t->next_phase = (j + in_rate) % out_rate;
  }

  switch (resampler->format) {
    case GST_AUDIO_FORMAT_F64:
      if (resampler->channels == 2 && n_taps >= 4) {
        resampler->resample = resample_interleaved_gdouble_2;
        resampler->deinterleave = deinterleave_copy;
      } else {
        resampler->resample = resample_gdouble;
        resampler->deinterleave = deinterleave_gdouble;
      }
      resampler->mirror = mirror_gdouble;
      break;
    case GST_AUDIO_FORMAT_F32:
      resampler->resample = resample_gfloat;
      resampler->deinterleave = deinterleave_gfloat;
      resampler->mirror = mirror_gfloat;
      break;
    case GST_AUDIO_FORMAT_S32:
      resampler->resample = resample_gint32;
      resampler->deinterleave = deinterleave_gint32;
      resampler->mirror = mirror_gint32;
      break;
    case GST_AUDIO_FORMAT_S16:
      if (resampler->channels == 2 && n_taps >= 4) {
        resampler->resample = resample_interleaved_gint16_2;
        resampler->deinterleave = deinterleave_copy;
      } else {
        resampler->resample = resample_gint16;
        resampler->deinterleave = deinterleave_gint16;
      }
      resampler->mirror = mirror_gint16;
      break;
    default:
      break;
  }
  if (resampler->flags & GST_AUDIO_RESAMPLER_FLAG_NON_INTERLEAVED) {
    resampler->deinterleave = deinterleave_copy_n;
    resampler->ostride = 1;
  } else {
    resampler->ostride = resampler->channels;
  }
}

#define PRINT_TAPS(type,print)                  \
G_STMT_START {                                  \
  type sum = 0.0, *taps;                        \
                                                \
  if (t->taps == NULL)                          \
    make_taps (resampler, t, i);                \
                                                \
  taps = t->taps;                               \
  for (j = 0; j < n_taps; j++) {                \
    type tap = taps[j];                         \
    fprintf (stderr, "\t%" print " ", tap);     \
    sum += tap;                                 \
  }                                             \
  fprintf (stderr, "\t: sum %" print "\n", sum);\
} G_STMT_END

static void
resampler_dump (GstAudioResampler * resampler)
{
#if 0
  gint i, n_taps, out_rate;
  gint64 a;

  out_rate = resampler->out_rate;
  n_taps = resampler->n_taps;

  fprintf (stderr, "out size %d, max taps %d\n", out_rate, n_taps);

  a = g_get_monotonic_time ();

  for (i = 0; i < out_rate; i++) {
    gint j;
    Tap *t = &resampler->taps[i];

    fprintf (stderr, "%u: %d %d\t ", i, t->sample_inc, t->next_phase);
    switch (resampler->format) {
      case GST_AUDIO_FORMAT_F64:
        PRINT_TAPS (gdouble, "f");
        break;
      case GST_AUDIO_FORMAT_F32:
        PRINT_TAPS (gfloat, "f");
        break;
      case GST_AUDIO_FORMAT_S32:
        PRINT_TAPS (gint32, "d");
        break;
      case GST_AUDIO_FORMAT_S16:
        PRINT_TAPS (gint16, "d");
        break;
      default:
        break;
    }
  }
  fprintf (stderr, "time %" G_GUINT64_FORMAT "\n", g_get_monotonic_time () - a);
#endif
}

/**
 * gst_audio_resampler_options_set_quality:
 * @method: a #GstAudioResamplerMethod
 * @quality: the quality
 * @in_rate: the input rate
 * @out_rate: the output rate
 * @options: a #GstStructure
 *
 * Set the parameters for resampling from @in_rate to @out_rate using @method
 * for @quality in @options.
 */
void
gst_audio_resampler_options_set_quality (GstAudioResamplerMethod method,
    guint quality, guint in_rate, guint out_rate, GstStructure * options)
{
  g_return_if_fail (options != NULL);
  g_return_if_fail (quality < 11);
  g_return_if_fail (in_rate != 0 && out_rate != 0);

  switch (method) {
    case GST_AUDIO_RESAMPLER_METHOD_NEAREST:
      break;
    case GST_AUDIO_RESAMPLER_METHOD_LINEAR:
      gst_structure_set (options,
          GST_AUDIO_RESAMPLER_OPT_N_TAPS, G_TYPE_INT, 2, NULL);
      break;
    case GST_AUDIO_RESAMPLER_METHOD_CUBIC:
      gst_structure_set (options,
          GST_AUDIO_RESAMPLER_OPT_N_TAPS, G_TYPE_INT, 4,
          GST_AUDIO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, DEFAULT_OPT_CUBIC_B,
          GST_AUDIO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, DEFAULT_OPT_CUBIC_C,
          NULL);
      break;
    case GST_AUDIO_RESAMPLER_METHOD_BLACKMAN_NUTTALL:
    {
      const BlackmanQualityMap *map = &blackman_qualities[quality];
      gst_structure_set (options,
          GST_AUDIO_RESAMPLER_OPT_N_TAPS, G_TYPE_INT, map->n_taps,
          GST_AUDIO_RESAMPLER_OPT_CUTOFF, G_TYPE_DOUBLE, map->cutoff, NULL);
      break;
    }
    case GST_AUDIO_RESAMPLER_METHOD_KAISER:
    {
      const KaiserQualityMap *map = &kaiser_qualities[quality];
      gdouble cutoff;

      cutoff = map->cutoff;
      if (out_rate < in_rate)
        cutoff *= map->downsample_cutoff_factor;

      gst_structure_set (options,
          GST_AUDIO_RESAMPLER_OPT_CUTOFF, G_TYPE_DOUBLE, cutoff,
          GST_AUDIO_RESAMPLER_OPT_STOP_ATTENUATION, G_TYPE_DOUBLE,
          map->stopband_attenuation,
          GST_AUDIO_RESAMPLER_OPT_TRANSITION_BANDWIDTH, G_TYPE_DOUBLE,
          map->transition_bandwidth, NULL);
      break;
    }
  }
}

/**
 * gst_audio_resampler_new:
 * @resampler: a #GstAudioResampler
 * @method: a #GstAudioResamplerMethod
 * @flags: #GstAudioResamplerFlags
 * @in_rate: input rate
 * @out_rate: output rate
 * @options: extra options
 *
 * Make a new resampler.
 *
 * Returns: %TRUE on success
 */
GstAudioResampler *
gst_audio_resampler_new (GstAudioResamplerMethod method,
    GstAudioResamplerFlags flags,
    GstAudioFormat format, guint channels,
    guint in_rate, guint out_rate, GstStructure * options)
{
  GstAudioResampler *resampler;
  const GstAudioFormatInfo *info;

  g_return_val_if_fail (in_rate != 0, FALSE);
  g_return_val_if_fail (out_rate != 0, FALSE);

  resampler = g_slice_new0 (GstAudioResampler);
  resampler->method = method;
  resampler->flags = flags;
  resampler->format = format;
  resampler->channels = channels;

  info = gst_audio_format_get_info (format);
  resampler->bps = GST_AUDIO_FORMAT_INFO_WIDTH (info) / 8;
  resampler->bpf = resampler->bps * channels;
  resampler->sbuf = g_malloc0 (sizeof (gpointer) * channels);

  GST_DEBUG ("method %d, bps %d, bpf %d", method, resampler->bps,
      resampler->bpf);

  gst_audio_resampler_update (resampler, in_rate, out_rate, options);

  return resampler;
}

/**
 * gst_audio_resampler_update:
 * @resampler: a #GstAudioResampler
 * @in_rate: new input rate
 * @out_rate: new output rate
 * @options: new options or %NULL
 *
 * Update the resampler parameters for @resampler. This function should
 * not be called concurrently with any other function on @resampler.
 *
 * Returns: %TRUE if the new parameters could be set
 */
gboolean
gst_audio_resampler_update (GstAudioResampler * resampler,
    guint in_rate, guint out_rate, GstStructure * options)
{
  gint gcd;

  g_return_val_if_fail (resampler != NULL, FALSE);
  g_return_val_if_fail (in_rate != 0, FALSE);
  g_return_val_if_fail (out_rate != 0, FALSE);

  gcd = gst_util_greatest_common_divisor (in_rate, out_rate);
  in_rate /= gcd;
  out_rate /= gcd;

  resampler->in_rate = in_rate;
  resampler->out_rate = out_rate;
  if (options) {
    if (resampler->options)
      gst_structure_free (resampler->options);
    resampler->options = gst_structure_copy (options);
  }

  GST_DEBUG ("%u->%u", in_rate, out_rate);

  resampler_calculate_taps (resampler);
  resampler_dump (resampler);

  resampler->filling = TRUE;
  resampler->samp_index = 0;
  resampler->samp_phase = 0;
  resampler->samples_avail = resampler->n_taps / 2 - 1;

  return TRUE;
}

/**
 * gst_audio_resampler_free:
 * @resampler: a #GstAudioResampler
 *
 * Free a previously allocated #GstAudioResampler @resampler.
 *
 * Since: 1.6
 */
void
gst_audio_resampler_free (GstAudioResampler * resampler)
{
  g_return_if_fail (resampler != NULL);

  g_free (resampler->taps);
  g_free (resampler->coeff);
  g_free (resampler->tmpcoeff);
  g_free (resampler->samples);
  g_free (resampler->sbuf);
  if (resampler->options)
    gst_structure_free (resampler->options);
  g_slice_free (GstAudioResampler, resampler);
}

static inline gsize
calc_out (GstAudioResampler * resampler, gsize in)
{
  return ((in * resampler->out_rate -
          resampler->samp_phase) / resampler->in_rate) + 1;
}

/**
 * gst_audio_resampler_get_out_frames:
 * @resampler: a #GstAudioResampler
 * @in_frames: number of input frames
 *
 * Get the number of output frames that would be currently available when
 * @in_frames are given to @resampler.
 *
 * Returns: The number of frames that would be availabe after giving
 * @in_frames as input to @resampler.
 */
gsize
gst_audio_resampler_get_out_frames (GstAudioResampler * resampler,
    gsize in_frames)
{
  gsize need, avail;

  g_return_val_if_fail (resampler != NULL, 0);

  need = resampler->n_taps + resampler->samp_index + resampler->skip;
  avail = resampler->samples_avail + in_frames;
  if (avail < need)
    return 0;

  return calc_out (resampler, avail - need);
}

/**
 * gst_audio_resampler_get_in_frames:
 * @resampler: a #GstAudioResampler
 * @out_frames: number of input frames
 *
 * Get the number of input frames that would currently be needed
 * to produce @out_frames from @resampler.
 *
 * Returns: The number of input frames needed for producing
 * @out_frames of data from @resampler.
 */
gsize
gst_audio_resampler_get_in_frames (GstAudioResampler * resampler,
    gsize out_frames)
{
  gsize in_frames;

  g_return_val_if_fail (resampler != NULL, 0);

  in_frames =
      (resampler->samp_phase +
      out_frames * resampler->samp_frac) / resampler->out_rate;
  in_frames += out_frames * resampler->samp_inc;

  return in_frames;
}

/**
 * gst_audio_resampler_get_max_latency:
 * @resampler: a #GstAudioResampler
 *
 * Get the maximum number of input samples that the resampler would
 * need before producing output.
 *
 * Returns: the latency of @resampler as expressed in the number of
 * frames.
 */
gsize
gst_audio_resampler_get_max_latency (GstAudioResampler * resampler)
{
  g_return_val_if_fail (resampler != NULL, 0);

  return resampler->n_taps / 2;
}

/* make the buffers to hold the (deinterleaved) samples */
static inline gpointer *
get_sample_bufs (GstAudioResampler * resampler, gsize need)
{
  if (resampler->samples_len < need) {
    guint c, channels = resampler->channels;
    GST_LOG ("realloc %d -> %d", (gint) resampler->samples_len, (gint) need);
    /* FIXME, move history */
    resampler->samples = g_realloc (resampler->samples, need * resampler->bpf);
    resampler->samples_len = need;
    /* set up new pointers */
    for (c = 0; c < channels; c++)
      resampler->sbuf[c] =
          (gint8 *) resampler->samples +
          (c * resampler->samples_len * resampler->bps);
  }
  return resampler->sbuf;
}

/**
 * gst_audio_resampler_resample:
 * @resampler: a #GstAudioResampler
 * @in: input samples
 * @in_frames: number of input frames
 * @out: output samples
 * @out_frames: maximum output frames
 * @consumed: number of frames consumed
 * @produced: number of frames produced
 *
 * Perform resampling on @in_frames frames in @in and write at most
 * @out_frames of frames to @out.
 *
 * In case the samples are interleaved, @in and @out must point to an
 * array with a single element pointing to a block of interleaved samples.
 *
 * If non-interleaved samples are used, @in and @out must point to an
 * array with pointers to memory blocks, one for each channel.
 *
 * @in may be %NULL, in which case @in_frames of 0 samples are pushed
 * into the resampler.
 *
 * The number of frames consumed is returned in @consumed and can be
 * less than @in_frames due to latency of the resampler or because
 * the number of samples produced equals @out_frames.
 *
 * The number of frames produced is returned in @produced.
 */
void
gst_audio_resampler_resample (GstAudioResampler * resampler,
    gpointer in[], gsize in_frames, gpointer out[], gsize out_frames,
    gsize * consumed, gsize * produced)
{
  gsize samples_avail;
  gsize out2, need;
  gpointer *sbuf;

  /* do sample skipping */
  if (resampler->skip >= in_frames) {
    /* we need tp skip all input */
    resampler->skip -= in_frames;
    *consumed = in_frames;
    *produced = 0;
    return;
  }
  /* skip the last samples by advancing the sample index */
  resampler->samp_index += resampler->skip;

  samples_avail = resampler->samples_avail;

  /* make sure we have enough space to copy our samples */
  sbuf = get_sample_bufs (resampler, in_frames + samples_avail);

  /* copy/deinterleave the samples */
  resampler->deinterleave (resampler, sbuf, in, in_frames);

  /* update new amount of samples in our buffer */
  resampler->samples_avail = samples_avail += in_frames;

  need = resampler->n_taps + resampler->samp_index;
  if (samples_avail < need) {
    /* not enough samples to start */
    *consumed = in_frames;
    *produced = 0;
    return;
  }

  if (resampler->filling) {
    /* if we are filling up our history duplicate the samples to the left */
    resampler->mirror (resampler, sbuf);
    resampler->filling = FALSE;
  }

  /* calculate maximum number of available output samples */
  out2 = calc_out (resampler, samples_avail - need);
  out_frames = MIN (out2, out_frames);

  /* resample all channels */
  resampler->resample (resampler, sbuf, samples_avail, out, out_frames,
      consumed, produced, TRUE);

  GST_LOG ("in %" G_GSIZE_FORMAT ", used %" G_GSIZE_FORMAT ", consumed %"
      G_GSIZE_FORMAT ", produced %" G_GSIZE_FORMAT, in_frames, samples_avail,
      *consumed, *produced);

  /* update pointers */
  if (*consumed > 0) {
    gssize left = samples_avail - *consumed;
    if (left > 0) {
      /* we consumed part of our samples */
      resampler->samples_avail = left;
    } else {
      /* we consumed all our samples, empty our buffers */
      resampler->samples_avail = 0;
      resampler->skip = -left;
    }
    /* we always consume everything */
    *consumed = in_frames;
  }
}
