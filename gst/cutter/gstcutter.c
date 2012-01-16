/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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
 * SECTION:element-cutter
 *
 * Analyses the audio signal for periods of silence. The start and end of
 * silence is signalled by bus messages named
 * <classname>&quot;cutter&quot;</classname>.
 * The message's structure contains two fields:
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
 *   gboolean
 *   <classname>&quot;above&quot;</classname>:
 *   %TRUE for begin of silence and %FALSE for end of silence.
 *   </para>
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -m filesrc location=foo.ogg ! decodebin ! audioconvert ! cutter ! autoaudiosink
 * ]| Show cut messages.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstcutter.h"
#include "math.h"

GST_DEBUG_CATEGORY_STATIC (cutter_debug);
#define GST_CAT_DEFAULT cutter_debug

#define CUTTER_DEFAULT_THRESHOLD_LEVEL    0.1
#define CUTTER_DEFAULT_THRESHOLD_LENGTH  (500 * GST_MSECOND)
#define CUTTER_DEFAULT_PRE_LENGTH        (200 * GST_MSECOND)

static GstStaticPadTemplate cutter_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, " "signed = (boolean) true")
    );

static GstStaticPadTemplate cutter_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, " "signed = (boolean) true")
    );

enum
{
  PROP_0,
  PROP_THRESHOLD,
  PROP_THRESHOLD_DB,
  PROP_RUN_LENGTH,
  PROP_PRE_LENGTH,
  PROP_LEAKY
};

GST_BOILERPLATE (GstCutter, gst_cutter, GstElement, GST_TYPE_ELEMENT);

static void gst_cutter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cutter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_cutter_chain (GstPad * pad, GstBuffer * buffer);

static gboolean gst_cutter_get_caps (GstPad * pad, GstCutter * filter);

static void
gst_cutter_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &cutter_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &cutter_sink_factory);
  gst_element_class_set_details_simple (element_class, "Audio cutter",
      "Filter/Editor/Audio",
      "Audio Cutter to split audio into non-silent bits",
      "Thomas Vander Stichele <thomas at apestaart dot org>");
}

static void
gst_cutter_class_init (GstCutterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_cutter_set_property;
  gobject_class->get_property = gst_cutter_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
          "Volume threshold before trigger",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_THRESHOLD_DB,
      g_param_spec_double ("threshold-dB", "Threshold (dB)",
          "Volume threshold before trigger (in dB)",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RUN_LENGTH,
      g_param_spec_uint64 ("run-length", "Run length",
          "Length of drop below threshold before cut_stop (in nanoseconds)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PRE_LENGTH,
      g_param_spec_uint64 ("pre-length", "Pre-recording buffer length",
          "Length of pre-recording buffer (in nanoseconds)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LEAKY,
      g_param_spec_boolean ("leaky", "Leaky",
          "do we leak buffers when below threshold ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (cutter_debug, "cutter", 0, "Audio cutting");
}

static void
gst_cutter_init (GstCutter * filter, GstCutterClass * g_class)
{
  filter->sinkpad =
      gst_pad_new_from_static_template (&cutter_sink_factory, "sink");
  filter->srcpad =
      gst_pad_new_from_static_template (&cutter_src_factory, "src");

  filter->threshold_level = CUTTER_DEFAULT_THRESHOLD_LEVEL;
  filter->threshold_length = CUTTER_DEFAULT_THRESHOLD_LENGTH;
  filter->silent_run_length = 0 * GST_SECOND;
  filter->silent = TRUE;
  filter->silent_prev = FALSE;  /* previous value of silent */

  filter->pre_length = CUTTER_DEFAULT_PRE_LENGTH;
  filter->pre_run_length = 0 * GST_SECOND;
  filter->pre_buffer = NULL;
  filter->leaky = FALSE;

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_cutter_chain);
  gst_pad_use_fixed_caps (filter->sinkpad);

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_use_fixed_caps (filter->srcpad);
}

static GstMessage *
gst_cutter_message_new (GstCutter * c, gboolean above, GstClockTime timestamp)
{
  GstStructure *s;

  s = gst_structure_new ("cutter",
      "above", G_TYPE_BOOLEAN, above,
      "timestamp", GST_TYPE_CLOCK_TIME, timestamp, NULL);

  return gst_message_new_element (GST_OBJECT (c), s);
}

/* Calculate the Normalized Cumulative Square over a buffer of the given type
 * and over all channels combined */

#define DEFINE_CUTTER_CALCULATOR(TYPE, RESOLUTION)                            \
static void inline                                                            \
gst_cutter_calculate_##TYPE (TYPE * in, guint num,                            \
                            double *NCS)                                      \
{                                                                             \
  register int j;                                                             \
  double squaresum = 0.0;           /* square sum of the integer samples */   \
  register double square = 0.0;     /* Square */                              \
  gdouble normalizer;               /* divisor to get a [-1.0, 1.0] range */  \
                                                                              \
  *NCS = 0.0;                       /* Normalized Cumulative Square */        \
                                                                              \
  normalizer = (double) (1 << (RESOLUTION * 2));                              \
                                                                              \
  for (j = 0; j < num; j++)                                                   \
  {                                                                           \
    square = ((double) in[j]) * in[j];                                        \
    squaresum += square;                                                      \
  }                                                                           \
                                                                              \
                                                                              \
  *NCS = squaresum / normalizer;                                              \
}

DEFINE_CUTTER_CALCULATOR (gint16, 15);
DEFINE_CUTTER_CALCULATOR (gint8, 7);


static GstFlowReturn
gst_cutter_chain (GstPad * pad, GstBuffer * buf)
{
  GstCutter *filter;
  gint16 *in_data;
  guint num_samples;
  gdouble NCS = 0.0;            /* Normalized Cumulative Square of buffer */
  gdouble RMS = 0.0;            /* RMS of signal in buffer */
  gdouble NMS = 0.0;            /* Normalized Mean Square of buffer */
  GstBuffer *prebuf;            /* pointer to a prebuffer element */

  g_return_val_if_fail (pad != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  filter = GST_CUTTER (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (filter != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_CUTTER (filter), GST_FLOW_ERROR);

  if (!filter->have_caps) {
    if (!(gst_cutter_get_caps (pad, filter)))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  in_data = (gint16 *) GST_BUFFER_DATA (buf);
  GST_LOG_OBJECT (filter, "length of prerec buffer: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (filter->pre_run_length));

  /* calculate mean square value on buffer */
  switch (filter->width) {
    case 16:
      num_samples = GST_BUFFER_SIZE (buf) / 2;
      gst_cutter_calculate_gint16 (in_data, num_samples, &NCS);
      NMS = NCS / num_samples;
      break;
    case 8:
      num_samples = GST_BUFFER_SIZE (buf);
      gst_cutter_calculate_gint8 ((gint8 *) in_data, num_samples, &NCS);
      NMS = NCS / num_samples;
      break;
    default:
      /* this shouldn't happen */
      g_warning ("no mean square function for width %d\n", filter->width);
      break;
  }

  filter->silent_prev = filter->silent;

  RMS = sqrt (NMS);
  /* if RMS below threshold, add buffer length to silent run length count
   * if not, reset
   */
  GST_LOG_OBJECT (filter, "buffer stats: NMS %f, RMS %f, audio length %f", NMS,
      RMS,
      gst_guint64_to_gdouble (gst_audio_duration_from_pad_buffer
          (filter->sinkpad, buf)));
  if (RMS < filter->threshold_level)
    filter->silent_run_length +=
        gst_guint64_to_gdouble (gst_audio_duration_from_pad_buffer
        (filter->sinkpad, buf));
  else {
    filter->silent_run_length = 0 * GST_SECOND;
    filter->silent = FALSE;
  }

  if (filter->silent_run_length > filter->threshold_length)
    /* it has been silent long enough, flag it */
    filter->silent = TRUE;

  /* has the silent status changed ? if so, send right signal
   * and, if from silent -> not silent, flush pre_record buffer
   */
  if (filter->silent != filter->silent_prev) {
    if (filter->silent) {
      GstMessage *m =
          gst_cutter_message_new (filter, FALSE, GST_BUFFER_TIMESTAMP (buf));
      GST_DEBUG_OBJECT (filter, "signaling CUT_STOP");
      gst_element_post_message (GST_ELEMENT (filter), m);
    } else {
      gint count = 0;
      GstMessage *m =
          gst_cutter_message_new (filter, TRUE, GST_BUFFER_TIMESTAMP (buf));

      GST_DEBUG_OBJECT (filter, "signaling CUT_START");
      gst_element_post_message (GST_ELEMENT (filter), m);
      /* first of all, flush current buffer */
      GST_DEBUG_OBJECT (filter, "flushing buffer of length %" GST_TIME_FORMAT,
          GST_TIME_ARGS (filter->pre_run_length));
      while (filter->pre_buffer) {
        prebuf = (g_list_first (filter->pre_buffer))->data;
        filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
        gst_pad_push (filter->srcpad, prebuf);
        ++count;
      }
      GST_DEBUG_OBJECT (filter, "flushed %d buffers", count);
      filter->pre_run_length = 0 * GST_SECOND;
    }
  }
  /* now check if we have to send the new buffer to the internal buffer cache
   * or to the srcpad */
  if (filter->silent) {
    filter->pre_buffer = g_list_append (filter->pre_buffer, buf);
    filter->pre_run_length +=
        gst_guint64_to_gdouble (gst_audio_duration_from_pad_buffer
        (filter->sinkpad, buf));
    while (filter->pre_run_length > filter->pre_length) {
      prebuf = (g_list_first (filter->pre_buffer))->data;
      g_assert (GST_IS_BUFFER (prebuf));
      filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
      filter->pre_run_length -=
          gst_guint64_to_gdouble (gst_audio_duration_from_pad_buffer
          (filter->sinkpad, prebuf));
      /* only pass buffers if we don't leak */
      if (!filter->leaky)
        gst_pad_push (filter->srcpad, prebuf);
      else
        gst_buffer_unref (prebuf);
    }
  } else
    gst_pad_push (filter->srcpad, buf);

  return GST_FLOW_OK;
}


static gboolean
gst_cutter_get_caps (GstPad * pad, GstCutter * filter)
{
  GstCaps *caps;
  GstStructure *structure;

  caps = gst_pad_get_caps (pad);
  if (!caps) {
    GST_INFO ("no caps on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    return FALSE;
  }
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &filter->width);
  filter->max_sample = 1 << (filter->width - 1);        /* signed */
  filter->have_caps = TRUE;

  gst_caps_unref (caps);
  return TRUE;
}


static void
gst_cutter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCutter *filter;

  g_return_if_fail (GST_IS_CUTTER (object));
  filter = GST_CUTTER (object);

  switch (prop_id) {
    case PROP_THRESHOLD:
      filter->threshold_level = g_value_get_double (value);
      GST_DEBUG ("DEBUG: set threshold level to %f", filter->threshold_level);
      break;
    case PROP_THRESHOLD_DB:
      /* set the level given in dB
       * value in dB = 20 * log (value)
       * values in dB < 0 result in values between 0 and 1
       */
      filter->threshold_level = pow (10, g_value_get_double (value) / 20);
      GST_DEBUG_OBJECT (filter, "set threshold level to %f",
          filter->threshold_level);
      break;
    case PROP_RUN_LENGTH:
      /* set the minimum length of the silent run required */
      filter->threshold_length =
          gst_guint64_to_gdouble (g_value_get_uint64 (value));
      break;
    case PROP_PRE_LENGTH:
      /* set the length of the pre-record block */
      filter->pre_length = gst_guint64_to_gdouble (g_value_get_uint64 (value));
      break;
    case PROP_LEAKY:
      /* set if the pre-record buffer is leaky or not */
      filter->leaky = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cutter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCutter *filter;

  g_return_if_fail (GST_IS_CUTTER (object));
  filter = GST_CUTTER (object);

  switch (prop_id) {
    case PROP_RUN_LENGTH:
      g_value_set_uint64 (value, filter->threshold_length);
      break;
    case PROP_THRESHOLD:
      g_value_set_double (value, filter->threshold_level);
      break;
    case PROP_THRESHOLD_DB:
      g_value_set_double (value, 20 * log (filter->threshold_level));
      break;
    case PROP_PRE_LENGTH:
      g_value_set_uint64 (value, filter->pre_length);
      break;
    case PROP_LEAKY:
      g_value_set_boolean (value, filter->leaky);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cutter", GST_RANK_NONE, GST_TYPE_CUTTER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cutter",
    "Audio Cutter to split audio into non-silent bits",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
