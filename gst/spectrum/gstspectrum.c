/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Stefan Kost <ensonic@users.sf.net>
 *               <2007-2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * The Spectrum element analyzes the frequency spectrum of an audio signal.
 * If the #GstSpectrum:post-messages property is #TRUE, it sends analysis results
 * as application messages named
 * <classname>&quot;spectrum&quot;</classname> after each interval of time given
 * by the #GstSpectrum:interval property.
 *
 * The message's structure contains some combination of these fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;timestamp&quot;</classname>:
 *   the timestamp of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;stream-time&quot;</classname>:
 *   the stream time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;running-time&quot;</classname>:
 *   the running_time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;duration&quot;</classname>:
 *   the duration of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;endtime&quot;</classname>:
 *   the end time of the buffer that triggered the message as stream time (this
 *   is deprecated, as it can be calculated from stream-time + duration)
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gfloat
 *   <classname>&quot;magnitude&quot;</classname>:
 *   the level for each frequency band in dB. All values below the value of the
 *   #GstSpectrum:threshold property will be set to the threshold. Only present
 *   if the message-magnitude property is true.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gfloat
 *   <classname>&quot;phase&quot;</classname>:
 *   The phase for each frequency band. The value is between -pi and pi. Only
 *   present if the message-phase property is true.
 *   </para>
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Example application</title>
 * |[
 * <xi:include xmlns:xi="http://www.w3.org/2003/XInclude" parse="text" href="../../../../tests/examples/spectrum/spectrum-example.c" />
 * ]|
 * </refsect2>
 *
 * Last reviewed on 2009-01-14 (0.10.12)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include "gstspectrum.h"

GST_DEBUG_CATEGORY_STATIC (gst_spectrum_debug);
#define GST_CAT_DEFAULT gst_spectrum_debug

/* elementfactory information */

#define ALLOWED_CAPS \
    "audio/x-raw-int, "                                               \
    " width = (int) 16, "                                             \
    " depth = (int) [ 1, 16 ], "                                      \
    " signed = (boolean) true, "                                      \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]; "                                  \
    "audio/x-raw-int, "                                               \
    " width = (int) 32, "                                             \
    " depth = (int) [ 1, 32 ], "                                      \
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
#define DEFAULT_MESSAGE			TRUE
#define DEFAULT_POST_MESSAGES			TRUE
#define DEFAULT_MESSAGE_MAGNITUDE	TRUE
#define DEFAULT_MESSAGE_PHASE		FALSE
#define DEFAULT_INTERVAL		(GST_SECOND / 10)
#define DEFAULT_BANDS			128
#define DEFAULT_THRESHOLD		-60

enum
{
  PROP_0,
  PROP_MESSAGE,
  PROP_POST_MESSAGES,
  PROP_MESSAGE_MAGNITUDE,
  PROP_MESSAGE_PHASE,
  PROP_INTERVAL,
  PROP_BANDS,
  PROP_THRESHOLD
};

GST_BOILERPLATE (GstSpectrum, gst_spectrum, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static void gst_spectrum_finalize (GObject * object);
static void gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_spectrum_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_spectrum_start (GstBaseTransform * trans);
static gboolean gst_spectrum_stop (GstBaseTransform * trans);
static GstFlowReturn gst_spectrum_transform_ip (GstBaseTransform * trans,
    GstBuffer * in);
static gboolean gst_spectrum_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

static void
gst_spectrum_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details_simple (element_class, "Spectrum analyzer",
      "Filter/Analyzer/Audio",
      "Run an FFT on the audio signal, output spectrum data",
      "Erik Walthinsen <omega@cse.ogi.edu>, "
      "Stefan Kost <ensonic@users.sf.net>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

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
  gobject_class->finalize = gst_spectrum_finalize;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_spectrum_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_spectrum_stop);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_spectrum_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;

  filter_class->setup = GST_DEBUG_FUNCPTR (gst_spectrum_setup);

  /* FIXME 0.11, remove in favour of post-messages */
  g_object_class_install_property (gobject_class, PROP_MESSAGE,
      g_param_spec_boolean ("message", "Message",
          "Whether to post a 'spectrum' element message on the bus for each "
          "passed interval (deprecated, use post-messages)", DEFAULT_MESSAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstSpectrum:post-messages
   *
   * Post messages on the bus with spectrum information.
   *
   * Since: 0.10.17
   */
  g_object_class_install_property (gobject_class, PROP_POST_MESSAGES,
      g_param_spec_boolean ("post-messages", "Post Messages",
          "Whether to post a 'spectrum' element message on the bus for each "
          "passed interval", DEFAULT_POST_MESSAGES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MESSAGE_MAGNITUDE,
      g_param_spec_boolean ("message-magnitude", "Magnitude",
          "Whether to add a 'magnitude' field to the structure of any "
          "'spectrum' element messages posted on the bus",
          DEFAULT_MESSAGE_MAGNITUDE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MESSAGE_PHASE,
      g_param_spec_boolean ("message-phase", "Phase",
          "Whether to add a 'phase' field to the structure of any "
          "'spectrum' element messages posted on the bus",
          DEFAULT_MESSAGE_PHASE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTERVAL,
      g_param_spec_uint64 ("interval", "Interval",
          "Interval of time between message posts (in nanoseconds)",
          1, G_MAXUINT64, DEFAULT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BANDS,
      g_param_spec_uint ("bands", "Bands", "Number of frequency bands",
          0, G_MAXUINT, DEFAULT_BANDS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold",
          "dB threshold for result. All lower values will be set to this",
          G_MININT, 0, DEFAULT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_spectrum_debug, "spectrum", 0,
      "audio spectrum analyser element");
}

static void
gst_spectrum_init (GstSpectrum * spectrum, GstSpectrumClass * g_class)
{
  spectrum->post_messages = DEFAULT_POST_MESSAGES;
  spectrum->message_magnitude = DEFAULT_MESSAGE_MAGNITUDE;
  spectrum->message_phase = DEFAULT_MESSAGE_PHASE;
  spectrum->interval = DEFAULT_INTERVAL;
  spectrum->bands = DEFAULT_BANDS;
  spectrum->threshold = DEFAULT_THRESHOLD;
}

static void
gst_spectrum_reset_state (GstSpectrum * spectrum)
{
  if (spectrum->fft_ctx)
    gst_fft_f32_free (spectrum->fft_ctx);
  g_free (spectrum->input);
  g_free (spectrum->input_tmp);
  g_free (spectrum->freqdata);
  g_free (spectrum->spect_magnitude);
  g_free (spectrum->spect_phase);

  spectrum->fft_ctx = NULL;
  spectrum->input = NULL;
  spectrum->input_tmp = NULL;
  spectrum->spect_magnitude = NULL;
  spectrum->spect_phase = NULL;
  spectrum->freqdata = NULL;

  spectrum->num_frames = 0;
  spectrum->num_fft = 0;

  spectrum->accumulated_error = 0;
}

static void
gst_spectrum_finalize (GObject * object)
{
  GstSpectrum *spectrum = GST_SPECTRUM (object);

  gst_spectrum_reset_state (spectrum);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_spectrum_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpectrum *filter = GST_SPECTRUM (object);

  switch (prop_id) {
    case PROP_MESSAGE:
    case PROP_POST_MESSAGES:
      filter->post_messages = g_value_get_boolean (value);
      break;
    case PROP_MESSAGE_MAGNITUDE:
      filter->message_magnitude = g_value_get_boolean (value);
      break;
    case PROP_MESSAGE_PHASE:
      filter->message_phase = g_value_get_boolean (value);
      break;
    case PROP_INTERVAL:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->interval = g_value_get_uint64 (value);
      gst_spectrum_reset_state (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_BANDS:
      GST_BASE_TRANSFORM_LOCK (filter);

      if (filter->bands == g_value_get_uint (value)) {
        GST_BASE_TRANSFORM_UNLOCK (filter);
        break;
      }

      filter->bands = g_value_get_uint (value);

      gst_spectrum_reset_state (filter);

      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_THRESHOLD:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->threshold = g_value_get_int (value);
      gst_spectrum_reset_state (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
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
    case PROP_MESSAGE:
    case PROP_POST_MESSAGES:
      g_value_set_boolean (value, filter->post_messages);
      break;
    case PROP_MESSAGE_MAGNITUDE:
      g_value_set_boolean (value, filter->message_magnitude);
      break;
    case PROP_MESSAGE_PHASE:
      g_value_set_boolean (value, filter->message_phase);
      break;
    case PROP_INTERVAL:
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

  return TRUE;
}

static gboolean
gst_spectrum_stop (GstBaseTransform * trans)
{
  GstSpectrum *filter = GST_SPECTRUM (trans);

  gst_spectrum_reset_state (filter);

  return TRUE;
}

static gboolean
gst_spectrum_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstSpectrum *filter = GST_SPECTRUM (base);

  gst_spectrum_reset_state (filter);

  return TRUE;
}

static GstMessage *
gst_spectrum_message_new (GstSpectrum * spectrum, GstClockTime timestamp,
    GstClockTime duration)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (spectrum);
  GstStructure *s;
  GValue v = { 0, };
  GValue *l;
  guint i;
  gfloat *spect_magnitude = spectrum->spect_magnitude;
  gfloat *spect_phase = spectrum->spect_phase;
  GstClockTime endtime, running_time, stream_time;

  GST_DEBUG_OBJECT (spectrum, "preparing message, spect = %p, bands =%d ",
      spect_magnitude, spectrum->bands);

  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  /* endtime is for backwards compatibility */
  endtime = stream_time + duration;

  s = gst_structure_new ("spectrum",
      "endtime", GST_TYPE_CLOCK_TIME, endtime,
      "timestamp", G_TYPE_UINT64, timestamp,
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "duration", G_TYPE_UINT64, duration, NULL);

  if (spectrum->message_magnitude) {
    /* FIXME 0.11: this should be an array, not a list */
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
    /* FIXME 0.11: this should be an array, not a list */
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

static GstFlowReturn
gst_spectrum_transform_ip (GstBaseTransform * trans, GstBuffer * buffer)
{
  GstSpectrum *spectrum = GST_SPECTRUM (trans);
  guint i;
  guint rate = GST_AUDIO_FILTER (spectrum)->format.rate;
  guint channels = GST_AUDIO_FILTER (spectrum)->format.channels;
  gfloat max_value =
      (1UL << (GST_AUDIO_FILTER (spectrum)->format.depth - 1)) - 1;
  guint width = GST_AUDIO_FILTER (spectrum)->format.width / 8;
  gboolean fp = (GST_AUDIO_FILTER (spectrum)->format.type == GST_BUFTYPE_FLOAT);
  guint bands = spectrum->bands;
  guint nfft = 2 * bands - 2;
  gint threshold = spectrum->threshold;
  gfloat *input;
  gfloat *input_tmp;
  GstFFTF32Complex *freqdata;
  gfloat *spect_magnitude;
  gfloat *spect_phase;
  GstFFTF32 *fft_ctx;
  const guint8 *data = GST_BUFFER_DATA (buffer);
  guint size = GST_BUFFER_SIZE (buffer);

  GST_LOG_OBJECT (spectrum, "input size: %d bytes", GST_BUFFER_SIZE (buffer));

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_DEBUG_OBJECT (spectrum, "Discontinuity detected -- resetting state");
    gst_spectrum_reset_state (spectrum);
  }

  /* If we don't have a FFT context yet get one and
   * allocate memory for everything
   */
  if (spectrum->fft_ctx == NULL) {
    spectrum->input = g_new0 (gfloat, nfft);
    spectrum->input_tmp = g_new0 (gfloat, nfft);
    spectrum->freqdata = g_new0 (GstFFTF32Complex, bands);
    spectrum->spect_magnitude = g_new0 (gfloat, bands);
    spectrum->spect_phase = g_new0 (gfloat, bands);
    spectrum->fft_ctx = gst_fft_f32_new (nfft, FALSE);
    spectrum->frames_per_interval =
        gst_util_uint64_scale (spectrum->interval, rate, GST_SECOND);
    spectrum->error_per_interval = (spectrum->interval * rate) % GST_SECOND;
    if (spectrum->frames_per_interval == 0)
      spectrum->frames_per_interval = 1;
    spectrum->num_frames = 0;
    spectrum->num_fft = 0;
    spectrum->accumulated_error = 0;
  }

  if (spectrum->num_frames == 0)
    spectrum->message_ts = GST_BUFFER_TIMESTAMP (buffer);

  input = spectrum->input;
  input_tmp = spectrum->input_tmp;
  freqdata = spectrum->freqdata;
  spect_magnitude = spectrum->spect_magnitude;
  spect_phase = spectrum->spect_phase;
  fft_ctx = spectrum->fft_ctx;

  while (size >= width * channels) {

    /* Move the current frame into our ringbuffer and
     * take the average of all channels
     */
    spectrum->input[spectrum->input_pos] = 0.0;
    if (fp && width == 4) {
      gfloat *in = (gfloat *) data;
      for (i = 0; i < channels; i++)
        spectrum->input[spectrum->input_pos] += in[i];
    } else if (fp && width == 8) {
      gdouble *in = (gdouble *) data;
      for (i = 0; i < channels; i++)
        spectrum->input[spectrum->input_pos] += in[i];
    } else if (!fp && width == 4) {
      gint32 *in = (gint32 *) data;
      for (i = 0; i < channels; i++)
        /* max_value will be 0 when depth is 1, interpret -1 and 0
         * as -1 and +1 if that's the case.
         */
        spectrum->input[spectrum->input_pos] +=
            max_value ? in[i] / max_value : in[i] * 2 + 1;
    } else if (!fp && width == 2) {
      gint16 *in = (gint16 *) data;
      for (i = 0; i < channels; i++)
        spectrum->input[spectrum->input_pos] +=
            max_value ? in[i] / max_value : in[i] * 2 + 1;
    } else {
      g_assert_not_reached ();
    }
    spectrum->input[spectrum->input_pos] /= channels;

    data += width * channels;
    size -= width * channels;
    spectrum->input_pos = (spectrum->input_pos + 1) % nfft;
    spectrum->num_frames++;

    /* If we have enough frames for an FFT or we
     * have all frames required for the interval run
     * an FFT. In the last case we probably take the
     * FFT of frames that we already handled.
     */
    if (spectrum->num_frames % nfft == 0 ||
        ((spectrum->accumulated_error < GST_SECOND
                && spectrum->num_frames == spectrum->frames_per_interval)
            || (spectrum->accumulated_error >= GST_SECOND
                && spectrum->num_frames - 1 ==
                spectrum->frames_per_interval))) {

      for (i = 0; i < nfft; i++)
        input_tmp[i] = input[(spectrum->input_pos + i) % nfft];

      gst_fft_f32_window (fft_ctx, input_tmp, GST_FFT_WINDOW_HAMMING);

      gst_fft_f32_fft (fft_ctx, input_tmp, freqdata);
      spectrum->num_fft++;

      /* Calculate magnitude in db */
      for (i = 0; i < bands; i++) {
        gdouble val = 0.0;
        val = freqdata[i].r * freqdata[i].r;
        val += freqdata[i].i * freqdata[i].i;
        val /= nfft * nfft;
        val = 10.0 * log10 (val);
        if (val < threshold)
          val = threshold;
        spect_magnitude[i] += val;
      }

      /* Calculate phase */
      for (i = 0; i < bands; i++)
        spect_phase[i] += atan2 (freqdata[i].i, freqdata[i].r);
    }

    /* Do we have the FFTs for one interval? */
    if ((spectrum->accumulated_error < GST_SECOND
            && spectrum->num_frames == spectrum->frames_per_interval)
        || (spectrum->accumulated_error >= GST_SECOND
            && spectrum->num_frames - 1 == spectrum->frames_per_interval)) {

      if (spectrum->accumulated_error >= GST_SECOND)
        spectrum->accumulated_error -= GST_SECOND;
      else
        spectrum->accumulated_error += spectrum->error_per_interval;

      if (spectrum->post_messages) {
        GstMessage *m;

        /* Calculate average */
        for (i = 0; i < bands; i++) {
          spect_magnitude[i] /= spectrum->num_fft;
          spect_phase[i] /= spectrum->num_fft;
        }

        m = gst_spectrum_message_new (spectrum, spectrum->message_ts,
            spectrum->interval);

        gst_element_post_message (GST_ELEMENT (spectrum), m);
      }
      memset (spect_magnitude, 0, bands * sizeof (gfloat));
      memset (spect_phase, 0, bands * sizeof (gfloat));

      if (GST_CLOCK_TIME_IS_VALID (spectrum->message_ts))
        spectrum->message_ts +=
            gst_util_uint64_scale (spectrum->num_frames, GST_SECOND, rate);
      spectrum->num_frames = 0;
      spectrum->num_fft = 0;
    }
  }

  g_assert (size == 0);

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
