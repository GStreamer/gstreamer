/* GStreamer
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
 *
 * gstaudiotestsrc.c:
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
 * SECTION:element-audiotestsrc
 *
 * <refsect2>
 * AudioTestSrc can be used to generate basic audio signals. It support several
 * different waveforms and allows you to set the base frequency and volume.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc ! audioconvert ! alsasink
 * </programlisting>
 * This pipeline produces a sine with default frequency (mid-C) and volume.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc wave=2 freq=200 ! audioconvert ! tee name=t ! alsasink t. ! libvisual_lv_scope ! ffmpegcolorspace ! xvimagesink
 * </programlisting>
 * In this example a saw wave is generated. The wave is shown using a
 * scope visualizer from libvisual, allowing you to visually verify that
 * the saw wave is correct.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gst/controller/gstcontroller.h>

#include "gstaudiotestsrc.h"


GstElementDetails gst_audio_test_src_details = {
  "Audio test source",
  "Source/Audio",
  "Creates audio test signals of given frequency and volume",
  "Stefan Kost <ensonic@users.sf.net>"
};


enum
{
  PROP_0,
  PROP_SAMPLES_PER_BUFFER,
  PROP_WAVE,
  PROP_FREQ,
  PROP_VOLUME,
  PROP_IS_LIVE,
  PROP_TIMESTAMP_OFFSET,
};


static GstStaticPadTemplate gst_audio_test_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) [ 1, MAX ], " "channels = (int) 1")
    );


GST_BOILERPLATE (GstAudioTestSrc, gst_audio_test_src, GstBaseSrc,
    GST_TYPE_BASE_SRC);

#define GST_TYPE_AUDIO_TEST_SRC_WAVE (gst_audiostestsrc_wave_get_type())
static GType
gst_audiostestsrc_wave_get_type (void)
{
  static GType audiostestsrc_wave_type = 0;
  static GEnumValue audiostestsrc_waves[] = {
    {GST_AUDIO_TEST_SRC_WAVE_SINE, "Sine", "sine"},
    {GST_AUDIO_TEST_SRC_WAVE_SQUARE, "Square", "square"},
    {GST_AUDIO_TEST_SRC_WAVE_SAW, "Saw", "saw"},
    {GST_AUDIO_TEST_SRC_WAVE_TRIANGLE, "Triangle", "triangle"},
    {GST_AUDIO_TEST_SRC_WAVE_SILENCE, "Silence", "silence"},
    {GST_AUDIO_TEST_SRC_WAVE_WHITE_NOISE, "White noise", "white-noise"},
    {GST_AUDIO_TEST_SRC_WAVE_PINK_NOISE, "Pink noise", "pink-noise"},
    {0, NULL, NULL},
  };

  if (!audiostestsrc_wave_type) {
    audiostestsrc_wave_type = g_enum_register_static ("GstAudioTestSrcWave",
        audiostestsrc_waves);
  }
  return audiostestsrc_wave_type;
}

static void gst_audio_test_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_test_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_test_src_setcaps (GstBaseSrc * basesrc,
    GstCaps * caps);
static void gst_audio_test_src_src_fixate (GstPad * pad, GstCaps * caps);

static gboolean gst_audio_test_src_is_seekable (GstBaseSrc * basesrc);
static gboolean gst_audio_test_src_do_seek (GstBaseSrc * basesrc,
    GstSegment * segment);
static gboolean gst_audio_test_src_src_query (GstBaseSrc * basesrc,
    GstQuery * query);

static void gst_audio_test_src_change_wave (GstAudioTestSrc * src);

static void gst_audio_test_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_audio_test_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint length, GstBuffer ** buffer);


static void
gst_audio_test_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_test_src_src_template));
  gst_element_class_set_details (element_class, &gst_audio_test_src_details);
}

static void
gst_audio_test_src_class_init (GstAudioTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_audio_test_src_set_property;
  gobject_class->get_property = gst_audio_test_src_get_property;

  g_object_class_install_property (gobject_class, PROP_SAMPLES_PER_BUFFER,
      g_param_spec_int ("samplesperbuffer", "Samples per buffer",
          "Number of samples in each outgoing buffer",
          1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_WAVE, g_param_spec_enum ("wave", "Waveform", "Oscillator waveform", GST_TYPE_AUDIO_TEST_SRC_WAVE,        /* enum type */
          GST_AUDIO_TEST_SRC_WAVE_SINE, /* default value */
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_FREQ,
      g_param_spec_double ("freq", "Frequency", "Frequency of test signal",
          0.0, 20000.0, 440.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of test signal",
          0.0, 1.0, 0.8, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET,
      g_param_spec_int64 ("timestamp-offset", "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE));

  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_audio_test_src_setcaps);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_audio_test_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_audio_test_src_do_seek);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_audio_test_src_src_query);
  gstbasesrc_class->get_times =
      GST_DEBUG_FUNCPTR (gst_audio_test_src_get_times);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_audio_test_src_create);
}

static void
gst_audio_test_src_init (GstAudioTestSrc * src, GstAudioTestSrcClass * g_class)
{
  GstPad *pad = GST_BASE_SRC_PAD (src);

  gst_pad_set_fixatecaps_function (pad, gst_audio_test_src_src_fixate);

  src->samplerate = 44100;
  src->volume = 1.0;
  src->freq = 440.0;
  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);

  src->samples_per_buffer = 1024;
  src->timestamp_offset = G_GINT64_CONSTANT (0);

  src->wave = GST_AUDIO_TEST_SRC_WAVE_SINE;
  gst_audio_test_src_change_wave (src);
}

static void
gst_audio_test_src_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "rate", 44100);
}

static gboolean
gst_audio_test_src_setcaps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstAudioTestSrc *src = GST_AUDIO_TEST_SRC (basesrc);
  const GstStructure *structure;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &src->samplerate);

  return ret;
}

static gboolean
gst_audio_test_src_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstAudioTestSrc *src = GST_AUDIO_TEST_SRC (basesrc);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (src_fmt == dest_fmt) {
        dest_val = src_val;
        goto done;
      }

      switch (src_fmt) {
        case GST_FORMAT_DEFAULT:
          switch (dest_fmt) {
            case GST_FORMAT_TIME:
              /* samples to time */
              dest_val = src_val / src->samplerate;
              break;
            default:
              goto error;
          }
          break;
        case GST_FORMAT_TIME:
          switch (dest_fmt) {
            case GST_FORMAT_DEFAULT:
              /* time to samples */
              dest_val = src_val * src->samplerate;
              break;
            default:
              goto error;
          }
          break;
        default:
          goto error;
      }
    done:
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
      break;
    }
    default:
      break;
  }

  return res;
  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (src, "query failed");
    return FALSE;
  }
}

static void
gst_audio_test_src_create_sine (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble step, amp;

  step = 2 * M_PI * src->freq / src->samplerate;
  amp = src->volume * 32767.0;

  for (i = 0; i < src->samples_per_buffer; i++) {
    src->accumulator += step;
    if (src->accumulator >= 2 * M_PI)
      src->accumulator -= 2 * M_PI;

    samples[i] = (gint16) (sin (src->accumulator) * amp);
  }
}

static void
gst_audio_test_src_create_square (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble step, amp;

  step = 2 * M_PI * src->freq / src->samplerate;
  amp = src->volume * 32767.0;

  for (i = 0; i < src->samples_per_buffer; i++) {
    src->accumulator += step;
    if (src->accumulator >= 2 * M_PI)
      src->accumulator -= 2 * M_PI;

    samples[i] = (gint16) ((src->accumulator < M_PI) ? amp : -amp);
  }
}

static void
gst_audio_test_src_create_saw (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble step, amp;

  step = 2 * M_PI * src->freq / src->samplerate;
  amp = (src->volume * 32767.0) / M_PI;

  for (i = 0; i < src->samples_per_buffer; i++) {
    src->accumulator += step;
    if (src->accumulator >= 2 * M_PI)
      src->accumulator -= 2 * M_PI;

    if (src->accumulator < M_PI) {
      samples[i] = (gint16) (src->accumulator * amp);
    } else {
      samples[i] = (gint16) ((2 * M_PI - src->accumulator) * -amp);
    }
  }
}

static void
gst_audio_test_src_create_triangle (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble step, amp;

  step = 2 * M_PI * src->freq / src->samplerate;
  amp = (src->volume * 32767.0) / (M_PI * 0.5);

  for (i = 0; i < src->samples_per_buffer; i++) {
    src->accumulator += step;
    if (src->accumulator >= 2 * M_PI)
      src->accumulator -= 2 * M_PI;

    if (src->accumulator < (M_PI * 0.5)) {
      samples[i] = (gint16) (src->accumulator * amp);
    } else if (src->accumulator < (M_PI * 1.5)) {
      samples[i] = (gint16) ((src->accumulator - M_PI) * -amp);
    } else {
      samples[i] = (gint16) ((2 * M_PI - src->accumulator) * -amp);
    }
  }
}

static void
gst_audio_test_src_create_silence (GstAudioTestSrc * src, gint16 * samples)
{
  memset (samples, 0, src->samples_per_buffer * sizeof (gint16));
}

static void
gst_audio_test_src_create_white_noise (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble amp;

  amp = src->volume * 65535.0;

  for (i = 0; i < src->samples_per_buffer; i++) {
    samples[i] = (gint16) (32768 - (amp * rand () / (RAND_MAX + 1.0)));
  }
}

/* pink noise calculation is based on 
 * http://www.firstpr.com.au/dsp/pink-noise/phil_burk_19990905_patest_pink.c
 * which has been released under public domain
 * Many thanks Phil!
 */
static void
gst_audio_test_src_init_pink_noise (GstAudioTestSrc * src)
{
  gint i;
  gint num_rows = 12;           /* arbitrary: 1 .. PINK_MAX_RANDOM_ROWS */
  glong pmax;

  src->pink.index = 0;
  src->pink.index_mask = (1 << num_rows) - 1;
  /* calculate maximum possible signed random value.
   * Extra 1 for white noise always added. */
  pmax = (num_rows + 1) * (1 << (PINK_RANDOM_BITS - 1));
  src->pink.scalar = 1.0f / pmax;
  /* Initialize rows. */
  for (i = 0; i < num_rows; i++)
    src->pink.rows[i] = 0;
  src->pink.running_sum = 0;
}

/* Generate Pink noise values between -1.0 and +1.0 */
static gfloat
gst_audio_test_src_generate_pink_noise_value (GstPinkNoise * pink)
{
  glong new_random;
  glong sum;

  /* Increment and mask index. */
  pink->index = (pink->index + 1) & pink->index_mask;

  /* If index is zero, don't update any random values. */
  if (pink->index != 0) {
    /* Determine how many trailing zeros in PinkIndex. */
    /* This algorithm will hang if n==0 so test first. */
    gint num_zeros = 0;
    gint n = pink->index;

    while ((n & 1) == 0) {
      n = n >> 1;
      num_zeros++;
    }

    /* Replace the indexed ROWS random value.
     * Subtract and add back to RunningSum instead of adding all the random
     * values together. Only one changes each time.
     */
    pink->running_sum -= pink->rows[num_zeros];
    //new_random = ((glong)GenerateRandomNumber()) >> PINK_RANDOM_SHIFT;
    new_random = 32768.0 - (65536.0 * (gulong) rand () / (RAND_MAX + 1.0));
    pink->running_sum += new_random;
    pink->rows[num_zeros] = new_random;
  }

  /* Add extra white noise value. */
  new_random = 32768.0 - (65536.0 * (gulong) rand () / (RAND_MAX + 1.0));
  sum = pink->running_sum + new_random;

  /* Scale to range of -1.0 to 0.9999. */
  return (pink->scalar * sum);
}

static void
gst_audio_test_src_create_pink_noise (GstAudioTestSrc * src, gint16 * samples)
{
  gint i;
  gdouble amp;

  amp = src->volume * 32767.0;

  for (i = 0; i < src->samples_per_buffer; i++) {
    samples[i] =
        (gint16) (gst_audio_test_src_generate_pink_noise_value (&src->pink) *
        amp);
  }
}

static void
gst_audio_test_src_change_wave (GstAudioTestSrc * src)
{
  switch (src->wave) {
    case GST_AUDIO_TEST_SRC_WAVE_SINE:
      src->process = gst_audio_test_src_create_sine;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_SQUARE:
      src->process = gst_audio_test_src_create_square;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_SAW:
      src->process = gst_audio_test_src_create_saw;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_TRIANGLE:
      src->process = gst_audio_test_src_create_triangle;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_SILENCE:
      src->process = gst_audio_test_src_create_silence;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_WHITE_NOISE:
      src->process = gst_audio_test_src_create_white_noise;
      break;
    case GST_AUDIO_TEST_SRC_WAVE_PINK_NOISE:
      gst_audio_test_src_init_pink_noise (src);
      src->process = gst_audio_test_src_create_pink_noise;
      break;
    default:
      GST_ERROR ("invalid wave-form");
      break;
  }
}

static void
gst_audio_test_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_audio_test_src_do_seek (GstBaseSrc * basesrc, GstSegment * segment)
{
  GstAudioTestSrc *src = GST_AUDIO_TEST_SRC (basesrc);
  GstClockTime time;

  time = segment->time = segment->start;

  /* now move to the time indicated */
  src->n_samples = time * src->samplerate / GST_SECOND;
  src->running_time = src->n_samples * GST_SECOND / src->samplerate;

  g_assert (src->running_time <= time);

  return TRUE;
}

static gboolean
gst_audio_test_src_is_seekable (GstBaseSrc * basesrc)
{
  /* we're seekable... */
  return TRUE;
}

static GstFlowReturn
gst_audio_test_src_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstAudioTestSrc *src;
  GstBuffer *buf;
  GstClockTime next_time;

  src = GST_AUDIO_TEST_SRC (basesrc);

  if (!src->tags_pushed) {
    GstTagList *taglist;
    GstEvent *event;

    taglist = gst_tag_list_new ();

    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
        GST_TAG_DESCRIPTION, "audiotest wave", NULL);

    event = gst_event_new_tag (taglist);
    gst_pad_push_event (basesrc->srcpad, event);
    src->tags_pushed = TRUE;
  }

  buf = gst_buffer_new_and_alloc (src->samples_per_buffer * sizeof (gint16));
  gst_buffer_set_caps (buf, GST_PAD_CAPS (basesrc->srcpad));

  GST_BUFFER_TIMESTAMP (buf) = src->timestamp_offset + src->running_time;
  /* offset is the number of samples */
  GST_BUFFER_OFFSET (buf) = src->n_samples;
  src->n_samples += src->samples_per_buffer;
  GST_BUFFER_OFFSET_END (buf) = src->n_samples;
  next_time = src->n_samples * GST_SECOND / src->samplerate;
  GST_BUFFER_DURATION (buf) = next_time - src->running_time;

  gst_object_sync_values (G_OBJECT (src), src->running_time);

  src->running_time = next_time;

  src->process (src, (gint16 *) GST_BUFFER_DATA (buf));

  *buffer = buf;

  return GST_FLOW_OK;
}

static void
gst_audio_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioTestSrc *src = GST_AUDIO_TEST_SRC (object);

  switch (prop_id) {
    case PROP_SAMPLES_PER_BUFFER:
      src->samples_per_buffer = g_value_get_int (value);
      break;
    case PROP_WAVE:
      src->wave = g_value_get_enum (value);
      gst_audio_test_src_change_wave (src);
      break;
    case PROP_FREQ:
      src->freq = g_value_get_double (value);
      break;
    case PROP_VOLUME:
      src->volume = g_value_get_double (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioTestSrc *src = GST_AUDIO_TEST_SRC (object);

  switch (prop_id) {
    case PROP_SAMPLES_PER_BUFFER:
      g_value_set_int (value, src->samples_per_buffer);
      break;
    case PROP_WAVE:
      g_value_set_enum (value, src->wave);
      break;
    case PROP_FREQ:
      g_value_set_double (value, src->freq);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, src->volume);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "audiotestsrc",
      GST_RANK_NONE, GST_TYPE_AUDIO_TEST_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audiotestsrc",
    "Creates audio test signals of given frequency and volume",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
