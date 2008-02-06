/* 
 * GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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

/* 
 * Chebyshev type 1 filter design based on
 * "The Scientist and Engineer's Guide to DSP", Chapter 20.
 * http://www.dspguide.com/
 *
 * For type 2 and Chebyshev filters in general read
 * http://en.wikipedia.org/wiki/Chebyshev_filter
 *
 * Transformation from lowpass to bandpass/bandreject:
 * http://docs.dewresearch.com/DspHelp/html/IDH_LinearSystems_LowpassToBandPassZ.htm
 * http://docs.dewresearch.com/DspHelp/html/IDH_LinearSystems_LowpassToBandStopZ.htm
 * 
 */

/**
 * SECTION:element-audiochebband
 * @short_description: Chebyshev band pass and band reject filter
 *
 * <refsect2>
 * <para>
 * Attenuates all frequencies outside (bandpass) or inside (bandreject) of a frequency
 * band. The number of poles and the ripple parameter control the rolloff.
 * </para>
 * <para>
 * This element has the advantage over the windowed sinc bandpass and bandreject filter that it is
 * much faster and produces almost as good results. It's only disadvantages are the highly
 * non-linear phase and the slower rolloff compared to a windowed sinc filter with a large kernel.
 * </para>
 * <para>
 * For type 1 the ripple parameter specifies how much ripple in dB is allowed in the passband, i.e.
 * some frequencies in the passband will be amplified by that value. A higher ripple value will allow
 * a faster rolloff.
 * </para>
 * <para>
 * For type 2 the ripple parameter specifies the stopband attenuation. In the stopband the gain will
 * be at most this value. A lower ripple value will allow a faster rolloff.
 * </para>
 * <para>
 * As a special case, a Chebyshev type 1 filter with no ripple is a Butterworth filter.
 * </para>
 * <para><note>
 * Be warned that a too large number of poles can produce noise. The most poles are possible with
 * a cutoff frequency at a quarter of the sampling rate.
 * </note></para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc freq=1500 ! audioconvert ! audiochebband mode=band-pass lower-frequency=1000 upper-frequenc=6000 poles=4 ! audioconvert ! alsasink
 * gst-launch filesrc location="melo1.ogg" ! oggdemux ! vorbisdec ! audioconvert ! audiochebband mode=band-reject lower-frequency=1000 upper-frequency=4000 ripple=0.2 ! audioconvert ! alsasink
 * gst-launch audiotestsrc wave=white-noise ! audioconvert ! audiochebband mode=band-pass lower-frequency=1000 upper-frequency=4000 type=2 ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#include <math.h>

#include "audiochebband.h"

#define GST_CAT_DEFAULT gst_audio_cheb_band_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("AudioChebBand",
    "Filter/Effect/Audio",
    "Chebyshev band pass and band reject filter",
    "Sebastian Dröge <slomo@circular-chaos.org>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MODE,
  PROP_TYPE,
  PROP_LOWER_FREQUENCY,
  PROP_UPPER_FREQUENCY,
  PROP_RIPPLE,
  PROP_POLES
};

#define ALLOWED_CAPS \
    "audio/x-raw-float,"                                              \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_cheb_band_debug, "audiochebband", 0, "audiochebband element");

GST_BOILERPLATE_FULL (GstAudioChebBand, gst_audio_cheb_band,
    GstAudioFilter, GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void gst_audio_cheb_band_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_cheb_band_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_cheb_band_setup (GstAudioFilter * filter,
    GstRingBufferSpec * format);
static GstFlowReturn
gst_audio_cheb_band_transform_ip (GstBaseTransform * base, GstBuffer * buf);
static gboolean gst_audio_cheb_band_start (GstBaseTransform * base);

static void process_64 (GstAudioChebBand * filter,
    gdouble * data, guint num_samples);
static void process_32 (GstAudioChebBand * filter,
    gfloat * data, guint num_samples);

enum
{
  MODE_BAND_PASS = 0,
  MODE_BAND_REJECT
};

#define GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND_MODE (gst_audio_cheb_band_mode_get_type ())
static GType
gst_audio_cheb_band_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_BAND_PASS, "Band pass (default)",
          "band-pass"},
      {MODE_BAND_REJECT, "Band reject",
          "band-reject"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstAudioChebBandMode", values);
  }
  return gtype;
}

/* GObject vmethod implementations */

static void
gst_audio_cheb_band_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &element_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (klass),
      caps);
  gst_caps_unref (caps);
}

static void
gst_audio_cheb_band_dispose (GObject * object)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (object);

  if (filter->a) {
    g_free (filter->a);
    filter->a = NULL;
  }

  if (filter->b) {
    g_free (filter->b);
    filter->b = NULL;
  }

  if (filter->channels) {
    GstAudioChebBandChannelCtx *ctx;
    gint i, channels = GST_AUDIO_FILTER (filter)->format.channels;

    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      g_free (ctx->x);
      g_free (ctx->y);
    }

    g_free (filter->channels);
    filter->channels = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audio_cheb_band_class_init (GstAudioChebBandClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;
  GstAudioFilterClass *filter_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;
  filter_class = (GstAudioFilterClass *) klass;

  gobject_class->set_property = gst_audio_cheb_band_set_property;
  gobject_class->get_property = gst_audio_cheb_band_get_property;
  gobject_class->dispose = gst_audio_cheb_band_dispose;

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Low pass or high pass mode", GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND_MODE,
          MODE_BAND_PASS, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_TYPE,
      g_param_spec_int ("type", "Type",
          "Type of the chebychev filter", 1, 2,
          1, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /* FIXME: Don't use the complete possible range but restrict the upper boundary
   * so automatically generated UIs can use a slider without */
  g_object_class_install_property (gobject_class, PROP_LOWER_FREQUENCY,
      g_param_spec_float ("lower-frequency", "Lower frequency",
          "Start frequency of the band (Hz)", 0.0, 100000.0,
          0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_UPPER_FREQUENCY,
      g_param_spec_float ("upper-frequency", "Upper frequency",
          "Stop frequency of the band (Hz)", 0.0, 100000.0,
          0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_RIPPLE,
      g_param_spec_float ("ripple", "Ripple",
          "Amount of ripple (dB)", 0.0, 200.0,
          0.25, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  /* FIXME: What to do about this upper boundary? With a frequencies near
   * rate/4 32 poles are completely possible, with frequencies very low
   * or very high 16 poles already produces only noise */
  g_object_class_install_property (gobject_class, PROP_POLES,
      g_param_spec_int ("poles", "Poles",
          "Number of poles to use, will be rounded up to the next multiply of four",
          4, 32, 4, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_cheb_band_setup);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_cheb_band_transform_ip);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_audio_cheb_band_start);
}

static void
gst_audio_cheb_band_init (GstAudioChebBand * filter,
    GstAudioChebBandClass * klass)
{
  filter->lower_frequency = filter->upper_frequency = 0.0;
  filter->mode = MODE_BAND_PASS;
  filter->type = 1;
  filter->poles = 4;
  filter->ripple = 0.25;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);

  filter->have_coeffs = FALSE;
  filter->num_a = 0;
  filter->num_b = 0;
  filter->channels = NULL;
}

static void
generate_biquad_coefficients (GstAudioChebBand * filter,
    gint p, gdouble * a0, gdouble * a1, gdouble * a2, gdouble * a3,
    gdouble * a4, gdouble * b1, gdouble * b2, gdouble * b3, gdouble * b4)
{
  gint np = filter->poles / 2;
  gdouble ripple = filter->ripple;

  /* pole location in s-plane */
  gdouble rp, ip;

  /* zero location in s-plane */
  gdouble rz = 0.0, iz = 0.0;

  /* transfer function coefficients for the z-plane */
  gdouble x0, x1, x2, y1, y2;
  gint type = filter->type;

  /* Calculate pole location for lowpass at frequency 1 */
  {
    gdouble angle = (M_PI / 2.0) * (2.0 * p - 1) / np;

    rp = -sin (angle);
    ip = cos (angle);
  }

  /* If we allow ripple, move the pole from the unit
   * circle to an ellipse and keep cutoff at frequency 1 */
  if (ripple > 0 && type == 1) {
    gdouble es, vx;

    es = sqrt (pow (10.0, ripple / 10.0) - 1.0);

    vx = (1.0 / np) * asinh (1.0 / es);
    rp = rp * sinh (vx);
    ip = ip * cosh (vx);
  } else if (type == 2) {
    gdouble es, vx;

    es = sqrt (pow (10.0, ripple / 10.0) - 1.0);
    vx = (1.0 / np) * asinh (es);
    rp = rp * sinh (vx);
    ip = ip * cosh (vx);
  }

  /* Calculate inverse of the pole location to move from
   * type I to type II */
  if (type == 2) {
    gdouble mag2 = rp * rp + ip * ip;

    rp /= mag2;
    ip /= mag2;
  }

  /* Calculate zero location for frequency 1 on the
   * unit circle for type 2 */
  if (type == 2) {
    gdouble angle = M_PI / (np * 2.0) + ((p - 1) * M_PI) / (np);
    gdouble mag2;

    rz = 0.0;
    iz = cos (angle);
    mag2 = rz * rz + iz * iz;
    rz /= mag2;
    iz /= mag2;
  }

  /* Convert from s-domain to z-domain by
   * using the bilinear Z-transform, i.e.
   * substitute s by (2/t)*((z-1)/(z+1))
   * with t = 2 * tan(0.5).
   */
  if (type == 1) {
    gdouble t, m, d;

    t = 2.0 * tan (0.5);
    m = rp * rp + ip * ip;
    d = 4.0 - 4.0 * rp * t + m * t * t;

    x0 = (t * t) / d;
    x1 = 2.0 * x0;
    x2 = x0;
    y1 = (8.0 - 2.0 * m * t * t) / d;
    y2 = (-4.0 - 4.0 * rp * t - m * t * t) / d;
  } else {
    gdouble t, m, d;

    t = 2.0 * tan (0.5);
    m = rp * rp + ip * ip;
    d = 4.0 - 4.0 * rp * t + m * t * t;

    x0 = (t * t * iz * iz + 4.0) / d;
    x1 = (-8.0 + 2.0 * iz * iz * t * t) / d;
    x2 = x0;
    y1 = (8.0 - 2.0 * m * t * t) / d;
    y2 = (-4.0 - 4.0 * rp * t - m * t * t) / d;
  }

  /* Convert from lowpass at frequency 1 to either bandpass
   * or band reject.
   *
   * For bandpass substitute z^(-1) with:
   *
   *   -2            -1
   * -z   + alpha * z   - beta
   * ----------------------------
   *         -2            -1
   * beta * z   - alpha * z   + 1
   *
   * alpha = (2*a*b)/(1+b)
   * beta = (b-1)/(b+1)
   * a = cos((w1 + w0)/2) / cos((w1 - w0)/2)
   * b = tan(1/2) * cot((w1 - w0)/2)
   *
   * For bandreject substitute z^(-1) with:
   * 
   *  -2            -1
   * z   - alpha * z   + beta
   * ----------------------------
   *         -2            -1
   * beta * z   - alpha * z   + 1
   *
   * alpha = (2*a)/(1+b)
   * beta = (1-b)/(1+b)
   * a = cos((w1 + w0)/2) / cos((w1 - w0)/2)
   * b = tan(1/2) * tan((w1 - w0)/2)
   *
   */
  {
    gdouble a, b, d;
    gdouble alpha, beta;
    gdouble w0 =
        2.0 * M_PI * (filter->lower_frequency /
        GST_AUDIO_FILTER (filter)->format.rate);
    gdouble w1 =
        2.0 * M_PI * (filter->upper_frequency /
        GST_AUDIO_FILTER (filter)->format.rate);

    if (filter->mode == MODE_BAND_PASS) {
      a = cos ((w1 + w0) / 2.0) / cos ((w1 - w0) / 2.0);
      b = tan (1.0 / 2.0) / tan ((w1 - w0) / 2.0);

      alpha = (2.0 * a * b) / (1.0 + b);
      beta = (b - 1.0) / (b + 1.0);

      d = 1.0 + beta * (y1 - beta * y2);

      *a0 = (x0 + beta * (-x1 + beta * x2)) / d;
      *a1 = (alpha * (-2.0 * x0 + x1 + beta * x1 - 2.0 * beta * x2)) / d;
      *a2 =
          (-x1 - beta * beta * x1 + 2.0 * beta * (x0 + x2) +
          alpha * alpha * (x0 - x1 + x2)) / d;
      *a3 = (alpha * (x1 + beta * (-2.0 * x0 + x1) - 2.0 * x2)) / d;
      *a4 = (beta * (beta * x0 - x1) + x2) / d;
      *b1 = (alpha * (2.0 + y1 + beta * y1 - 2.0 * beta * y2)) / d;
      *b2 =
          (-y1 - beta * beta * y1 - alpha * alpha * (1.0 + y1 - y2) +
          2.0 * beta * (-1.0 + y2)) / d;
      *b3 = (alpha * (y1 + beta * (2.0 + y1) - 2.0 * y2)) / d;
      *b4 = (-beta * beta - beta * y1 + y2) / d;
    } else {
      a = cos ((w1 + w0) / 2.0) / cos ((w1 - w0) / 2.0);
      b = tan (1.0 / 2.0) * tan ((w1 - w0) / 2.0);

      alpha = (2.0 * a) / (1.0 + b);
      beta = (1.0 - b) / (1.0 + b);

      d = -1.0 + beta * (beta * y2 + y1);

      *a0 = (-x0 - beta * x1 - beta * beta * x2) / d;
      *a1 = (alpha * (2.0 * x0 + x1 + beta * x1 + 2.0 * beta * x2)) / d;
      *a2 =
          (-x1 - beta * beta * x1 - 2.0 * beta * (x0 + x2) -
          alpha * alpha * (x0 + x1 + x2)) / d;
      *a3 = (alpha * (x1 + beta * (2.0 * x0 + x1) + 2.0 * x2)) / d;
      *a4 = (-beta * beta * x0 - beta * x1 - x2) / d;
      *b1 = (alpha * (-2.0 + y1 + beta * y1 + 2.0 * beta * y2)) / d;
      *b2 =
          -(y1 + beta * beta * y1 + 2.0 * beta * (-1.0 + y2) +
          alpha * alpha * (-1.0 + y1 + y2)) / d;
      *b3 = (alpha * (beta * (-2.0 + y1) + y1 + 2.0 * y2)) / d;
      *b4 = -(-beta * beta + beta * y1 + y2) / d;
    }
  }
}

/* Evaluate the transfer function that corresponds to the IIR
 * coefficients at zr + zi*I and return the magnitude */
static gdouble
calculate_gain (gdouble * a, gdouble * b, gint num_a, gint num_b, gdouble zr,
    gdouble zi)
{
  gdouble sum_ar, sum_ai;
  gdouble sum_br, sum_bi;
  gdouble gain_r, gain_i;

  gdouble sum_r_old;
  gdouble sum_i_old;

  gint i;

  sum_ar = 0.0;
  sum_ai = 0.0;
  for (i = num_a; i >= 0; i--) {
    sum_r_old = sum_ar;
    sum_i_old = sum_ai;

    sum_ar = (sum_r_old * zr - sum_i_old * zi) + a[i];
    sum_ai = (sum_r_old * zi + sum_i_old * zr) + 0.0;
  }

  sum_br = 0.0;
  sum_bi = 0.0;
  for (i = num_b; i >= 0; i--) {
    sum_r_old = sum_br;
    sum_i_old = sum_bi;

    sum_br = (sum_r_old * zr - sum_i_old * zi) - b[i];
    sum_bi = (sum_r_old * zi + sum_i_old * zr) - 0.0;
  }
  sum_br += 1.0;
  sum_bi += 0.0;

  gain_r =
      (sum_ar * sum_br + sum_ai * sum_bi) / (sum_br * sum_br + sum_bi * sum_bi);
  gain_i =
      (sum_ai * sum_br - sum_ar * sum_bi) / (sum_br * sum_br + sum_bi * sum_bi);

  return (sqrt (gain_r * gain_r + gain_i * gain_i));
}

static void
generate_coefficients (GstAudioChebBand * filter)
{
  gint channels = GST_AUDIO_FILTER (filter)->format.channels;

  if (filter->a) {
    g_free (filter->a);
    filter->a = NULL;
  }

  if (filter->b) {
    g_free (filter->b);
    filter->b = NULL;
  }

  if (filter->channels) {
    GstAudioChebBandChannelCtx *ctx;
    gint i;

    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      g_free (ctx->x);
      g_free (ctx->y);
    }

    g_free (filter->channels);
    filter->channels = NULL;
  }

  if (GST_AUDIO_FILTER (filter)->format.rate == 0) {
    filter->num_a = 1;
    filter->a = g_new0 (gdouble, 1);
    filter->a[0] = 1.0;
    filter->num_b = 0;
    filter->channels = g_new0 (GstAudioChebBandChannelCtx, channels);
    GST_LOG_OBJECT (filter, "rate was not set yet");
    return;
  }

  filter->have_coeffs = TRUE;

  if (filter->upper_frequency <= filter->lower_frequency) {
    filter->num_a = 1;
    filter->a = g_new0 (gdouble, 1);
    filter->a[0] = (filter->mode == MODE_BAND_PASS) ? 0.0 : 1.0;
    filter->num_b = 0;
    filter->channels = g_new0 (GstAudioChebBandChannelCtx, channels);
    GST_LOG_OBJECT (filter, "frequency band had no or negative dimension");
    return;
  }

  if (filter->upper_frequency > GST_AUDIO_FILTER (filter)->format.rate / 2) {
    filter->upper_frequency = GST_AUDIO_FILTER (filter)->format.rate / 2;
    GST_LOG_OBJECT (filter, "clipped upper frequency to nyquist frequency");
  }

  if (filter->lower_frequency < 0.0) {
    filter->lower_frequency = 0.0;
    GST_LOG_OBJECT (filter, "clipped lower frequency to 0.0");
  }

  /* Calculate coefficients for the chebyshev filter */
  {
    gint np = filter->poles;
    gdouble *a, *b;
    gint i, p;

    filter->num_a = np + 1;
    filter->a = a = g_new0 (gdouble, np + 5);
    filter->num_b = np + 1;
    filter->b = b = g_new0 (gdouble, np + 5);

    filter->channels = g_new0 (GstAudioChebBandChannelCtx, channels);
    for (i = 0; i < channels; i++) {
      GstAudioChebBandChannelCtx *ctx = &filter->channels[i];

      ctx->x = g_new0 (gdouble, np + 1);
      ctx->y = g_new0 (gdouble, np + 1);
    }

    /* Calculate transfer function coefficients */
    a[4] = 1.0;
    b[4] = 1.0;

    for (p = 1; p <= np / 4; p++) {
      gdouble a0, a1, a2, a3, a4, b1, b2, b3, b4;
      gdouble *ta = g_new0 (gdouble, np + 5);
      gdouble *tb = g_new0 (gdouble, np + 5);

      generate_biquad_coefficients (filter, p, &a0, &a1, &a2, &a3, &a4, &b1,
          &b2, &b3, &b4);

      memcpy (ta, a, sizeof (gdouble) * (np + 5));
      memcpy (tb, b, sizeof (gdouble) * (np + 5));

      /* add the new coefficients for the new two poles
       * to the cascade by multiplication of the transfer
       * functions */
      for (i = 4; i < np + 5; i++) {
        a[i] =
            a0 * ta[i] + a1 * ta[i - 1] + a2 * ta[i - 2] + a3 * ta[i - 3] +
            a4 * ta[i - 4];
        b[i] =
            tb[i] - b1 * tb[i - 1] - b2 * tb[i - 2] - b3 * tb[i - 3] -
            b4 * tb[i - 4];
      }
      g_free (ta);
      g_free (tb);
    }

    /* Move coefficients to the beginning of the array
     * and multiply the b coefficients with -1 to move from
     * the transfer function's coefficients to the difference
     * equation's coefficients */
    b[4] = 0.0;
    for (i = 0; i <= np; i++) {
      a[i] = a[i + 4];
      b[i] = -b[i + 4];
    }

    /* Normalize to unity gain at frequency 0 and frequency
     * 0.5 for bandreject and unity gain at band center frequency
     * for bandpass */
    if (filter->mode == MODE_BAND_REJECT) {
      /* gain is sqrt(H(0)*H(0.5)) */

      gdouble gain1 = calculate_gain (a, b, np, np, 1.0, 0.0);
      gdouble gain2 = calculate_gain (a, b, np, np, -1.0, 0.0);

      gain1 = sqrt (gain1 * gain2);

      for (i = 0; i <= np; i++) {
        a[i] /= gain1;
      }
    } else {
      /* gain is H(wc), wc = center frequency */

      gdouble w1 =
          2.0 * M_PI * (filter->lower_frequency /
          GST_AUDIO_FILTER (filter)->format.rate);
      gdouble w2 =
          2.0 * M_PI * (filter->upper_frequency /
          GST_AUDIO_FILTER (filter)->format.rate);
      gdouble w0 = (w2 + w1) / 2.0;
      gdouble zr = cos (w0), zi = sin (w0);
      gdouble gain = calculate_gain (a, b, np, np, zr, zi);

      for (i = 0; i <= np; i++) {
        a[i] /= gain;
      }
    }

    GST_LOG_OBJECT (filter,
        "Generated IIR coefficients for the Chebyshev filter");
    GST_LOG_OBJECT (filter,
        "mode: %s, type: %d, poles: %d, lower-frequency: %.2f Hz, upper-frequency: %.2f Hz, ripple: %.2f dB",
        (filter->mode == MODE_BAND_PASS) ? "band-pass" : "band-reject",
        filter->type, filter->poles, filter->lower_frequency,
        filter->upper_frequency, filter->ripple);

    GST_LOG_OBJECT (filter, "%.2f dB gain @ 0Hz",
        20.0 * log10 (calculate_gain (a, b, np, np, 1.0, 0.0)));
    {
      gdouble w1 =
          2.0 * M_PI * (filter->lower_frequency /
          GST_AUDIO_FILTER (filter)->format.rate);
      gdouble w2 =
          2.0 * M_PI * (filter->upper_frequency /
          GST_AUDIO_FILTER (filter)->format.rate);
      gdouble w0 = (w2 + w1) / 2.0;
      gdouble zr, zi;

      zr = cos (w1);
      zi = sin (w1);
      GST_LOG_OBJECT (filter, "%.2f dB gain @ %dHz",
          20.0 * log10 (calculate_gain (a, b, np, np, zr, zi)),
          (int) filter->lower_frequency);
      zr = cos (w0);
      zi = sin (w0);
      GST_LOG_OBJECT (filter, "%.2f dB gain @ %dHz",
          20.0 * log10 (calculate_gain (a, b, np, np, zr, zi)),
          (int) ((filter->lower_frequency + filter->upper_frequency) / 2.0));
      zr = cos (w2);
      zi = sin (w2);
      GST_LOG_OBJECT (filter, "%.2f dB gain @ %dHz",
          20.0 * log10 (calculate_gain (a, b, np, np, zr, zi)),
          (int) filter->upper_frequency);
    }
    GST_LOG_OBJECT (filter, "%.2f dB gain @ %dHz",
        20.0 * log10 (calculate_gain (a, b, np, np, -1.0, 0.0)),
        GST_AUDIO_FILTER (filter)->format.rate / 2);
  }
}

static void
gst_audio_cheb_band_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (object);

  switch (prop_id) {
    case PROP_MODE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->mode = g_value_get_enum (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_TYPE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->type = g_value_get_int (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_LOWER_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->lower_frequency = g_value_get_float (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_UPPER_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->upper_frequency = g_value_get_float (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_RIPPLE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->ripple = g_value_get_float (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_POLES:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->poles = GST_ROUND_UP_4 (g_value_get_int (value));
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_cheb_band_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, filter->mode);
      break;
    case PROP_TYPE:
      g_value_set_int (value, filter->type);
      break;
    case PROP_LOWER_FREQUENCY:
      g_value_set_float (value, filter->lower_frequency);
      break;
    case PROP_UPPER_FREQUENCY:
      g_value_set_float (value, filter->upper_frequency);
      break;
    case PROP_RIPPLE:
      g_value_set_float (value, filter->ripple);
      break;
    case PROP_POLES:
      g_value_set_int (value, filter->poles);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstAudioFilter vmethod implementations */

static gboolean
gst_audio_cheb_band_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (base);
  gboolean ret = TRUE;

  if (format->width == 32)
    filter->process = (GstAudioChebBandProcessFunc)
        process_32;
  else if (format->width == 64)
    filter->process = (GstAudioChebBandProcessFunc)
        process_64;
  else
    ret = FALSE;

  filter->have_coeffs = FALSE;

  return ret;
}

static inline gdouble
process (GstAudioChebBand * filter,
    GstAudioChebBandChannelCtx * ctx, gdouble x0)
{
  gdouble val = filter->a[0] * x0;
  gint i, j;

  for (i = 1, j = ctx->x_pos; i < filter->num_a; i++) {
    val += filter->a[i] * ctx->x[j];
    j--;
    if (j < 0)
      j = filter->num_a - 1;
  }

  for (i = 1, j = ctx->y_pos; i < filter->num_b; i++) {
    val += filter->b[i] * ctx->y[j];
    j--;
    if (j < 0)
      j = filter->num_b - 1;
  }

  if (ctx->x) {
    ctx->x_pos++;
    if (ctx->x_pos > filter->num_a - 1)
      ctx->x_pos = 0;
    ctx->x[ctx->x_pos] = x0;
  }

  if (ctx->y) {
    ctx->y_pos++;
    if (ctx->y_pos > filter->num_b - 1)
      ctx->y_pos = 0;

    ctx->y[ctx->y_pos] = val;
  }

  return val;
}

#define DEFINE_PROCESS_FUNC(width,ctype) \
static void \
process_##width (GstAudioChebBand * filter, \
    g##ctype * data, guint num_samples) \
{ \
  gint i, j, channels = GST_AUDIO_FILTER (filter)->format.channels; \
  gdouble val; \
  \
  for (i = 0; i < num_samples / channels; i++) { \
    for (j = 0; j < channels; j++) { \
      val = process (filter, &filter->channels[j], *data); \
      *data++ = val; \
    } \
  } \
}

DEFINE_PROCESS_FUNC (32, float);
DEFINE_PROCESS_FUNC (64, double);

#undef DEFINE_PROCESS_FUNC

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_audio_cheb_band_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (base);
  guint num_samples =
      GST_BUFFER_SIZE (buf) / (GST_AUDIO_FILTER (filter)->format.width / 8);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
    gst_object_sync_values (G_OBJECT (filter), GST_BUFFER_TIMESTAMP (buf));

  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  if (!filter->have_coeffs)
    generate_coefficients (filter);

  filter->process (filter, GST_BUFFER_DATA (buf), num_samples);

  return GST_FLOW_OK;
}

static gboolean
gst_audio_cheb_band_start (GstBaseTransform * base)
{
  GstAudioChebBand *filter = GST_AUDIO_CHEB_BAND (base);
  gint channels = GST_AUDIO_FILTER (filter)->format.channels;
  GstAudioChebBandChannelCtx *ctx;
  gint i;

  /* Reset the history of input and output values if
   * already existing */
  if (channels && filter->channels) {
    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      if (ctx->x)
        memset (ctx->x, 0, (filter->poles + 1) * sizeof (gdouble));
      if (ctx->y)
        memset (ctx->y, 0, (filter->poles + 1) * sizeof (gdouble));
    }
  }
  return TRUE;
}
