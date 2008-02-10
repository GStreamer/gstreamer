/* 
 * GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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
 * SECTION:element-audioamplify
 * @short_description: Amplifies an audio stream with selectable clipping mode
 *
 * <refsect2>
 * Amplifies an audio stream by a given factor and allows the selection of different clipping modes.
 * The difference between the clipping modes is best evaluated by testing.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc wave=saw ! audioamplify amplification=1.5 ! alsasink
 * gst-launch filesrc location="melo1.ogg" ! oggdemux ! vorbisdec ! audioconvert ! audioamplify amplification=1.5 method=wrap-negative ! alsasink
 * gst-launch audiotestsrc wave=saw ! audioconvert ! audioamplify amplification=1.5 method=wrap-positive ! audioconvert ! alsasink
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

#include "audioamplify.h"

#define GST_CAT_DEFAULT gst_audio_amplify_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Audio amplifier",
    "Filter/Effect/Audio",
    "Amplifies an audio stream by a given factor",
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
  PROP_AMPLIFICATION,
  PROP_CLIPPING_METHOD
};

enum
{
  METHOD_CLIP = 0,
  METHOD_WRAP_NEGATIVE,
  METHOD_WRAP_POSITIVE,
  NUM_METHODS
};

#define GST_TYPE_AUDIO_AMPLIFY_CLIPPING_METHOD (gst_audio_amplify_clipping_method_get_type ())
static GType
gst_audio_amplify_clipping_method_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {METHOD_CLIP, "Normal Clipping (default)", "clip"},
      {METHOD_WRAP_NEGATIVE,
            "Push overdriven values back from the opposite side",
          "wrap-negative"},
      {METHOD_WRAP_POSITIVE, "Push overdriven values back from the same side",
          "wrap-positive"},
      {0, NULL, NULL}
    };

    /* FIXME 0.11: rename to GstAudioAmplifyClippingMethod */
    gtype = g_enum_register_static ("GstAudioPanoramaClippingMethod", values);
  }
  return gtype;
}

#define ALLOWED_CAPS \
    "audio/x-raw-int,"                                                \
    " depth=(int)16,"                                                 \
    " width=(int)16,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " signed=(bool)TRUE,"                                             \
    " rate=(int)[1,MAX],"                                             \
    " channels=(int)[1,MAX]; "                                        \
    "audio/x-raw-float,"                                              \
    " width=(int)32,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " rate=(int)[1,MAX],"                                             \
    " channels=(int)[1,MAX]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_amplify_debug, "audioamplify", 0, "audioamplify element");

GST_BOILERPLATE_FULL (GstAudioAmplify, gst_audio_amplify, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void gst_audio_amplify_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_amplify_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_audio_amplify_setup (GstAudioFilter * filter,
    GstRingBufferSpec * format);
static GstFlowReturn gst_audio_amplify_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);

static void gst_audio_amplify_transform_int_clip (GstAudioAmplify * filter,
    gint16 * data, guint num_samples);
static void gst_audio_amplify_transform_int_wrap_negative (GstAudioAmplify *
    filter, gint16 * data, guint num_samples);
static void gst_audio_amplify_transform_int_wrap_positive (GstAudioAmplify *
    filter, gint16 * data, guint num_samples);
static void gst_audio_amplify_transform_float_clip (GstAudioAmplify * filter,
    gfloat * data, guint num_samples);
static void gst_audio_amplify_transform_float_wrap_negative (GstAudioAmplify *
    filter, gfloat * data, guint num_samples);
static void gst_audio_amplify_transform_float_wrap_positive (GstAudioAmplify *
    filter, gfloat * data, guint num_samples);

/* table of processing functions: [format][clipping_method] */
static GstAudioAmplifyProcessFunc processing_functions[2][3] = {
  {
        (GstAudioAmplifyProcessFunc) gst_audio_amplify_transform_int_clip,
        (GstAudioAmplifyProcessFunc)
        gst_audio_amplify_transform_int_wrap_negative,
        (GstAudioAmplifyProcessFunc)
      gst_audio_amplify_transform_int_wrap_positive},
  {
        (GstAudioAmplifyProcessFunc) gst_audio_amplify_transform_float_clip,
        (GstAudioAmplifyProcessFunc)
        gst_audio_amplify_transform_float_wrap_negative,
        (GstAudioAmplifyProcessFunc)
      gst_audio_amplify_transform_float_wrap_positive}
};

/* GObject vmethod implementations */

static void
gst_audio_amplify_base_init (gpointer klass)
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
gst_audio_amplify_class_init (GstAudioAmplifyClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_audio_amplify_set_property;
  gobject_class->get_property = gst_audio_amplify_get_property;

  g_object_class_install_property (gobject_class, PROP_AMPLIFICATION,
      g_param_spec_float ("amplification", "Amplification",
          "Factor of amplification", 0.0, G_MAXFLOAT,
          1.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /**
   * GstAudioAmplify:clipping-method
   *
   * Clipping method: clip mode set values higher than the maximum to the
   * maximum. The wrap-negative mode pushes those values back from the
   * opposite side, wrap-positive pushes them back from the same side.
   *
   **/
  g_object_class_install_property (gobject_class, PROP_CLIPPING_METHOD,
      g_param_spec_enum ("clipping-method", "Clipping method",
          "Selects how to handle values higher than the maximum",
          GST_TYPE_AUDIO_AMPLIFY_CLIPPING_METHOD, METHOD_CLIP,
          G_PARAM_READWRITE));

  GST_AUDIO_FILTER_CLASS (klass)->setup =
      GST_DEBUG_FUNCPTR (gst_audio_amplify_setup);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_amplify_transform_ip);
}

static void
gst_audio_amplify_init (GstAudioAmplify * filter, GstAudioAmplifyClass * klass)
{
  filter->amplification = 1.0;
  filter->clipping_method = METHOD_CLIP;
  filter->format_index = 0;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (filter), TRUE);
}

static gboolean
gst_audio_amplify_set_process_function (GstAudioAmplify * filter)
{
  gint method_index;

  /* set processing function */

  method_index = filter->clipping_method;
  if (method_index >= NUM_METHODS || method_index < 0)
    method_index = METHOD_CLIP;

  filter->process = processing_functions[filter->format_index][method_index];
  return TRUE;
}

static void
gst_audio_amplify_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioAmplify *filter = GST_AUDIO_AMPLIFY (object);

  switch (prop_id) {
    case PROP_AMPLIFICATION:
      filter->amplification = g_value_get_float (value);
      gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter),
          filter->amplification == 1.0);
      break;
    case PROP_CLIPPING_METHOD:
      filter->clipping_method = g_value_get_enum (value);
      gst_audio_amplify_set_process_function (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_amplify_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioAmplify *filter = GST_AUDIO_AMPLIFY (object);

  switch (prop_id) {
    case PROP_AMPLIFICATION:
      g_value_set_float (value, filter->amplification);
      break;
    case PROP_CLIPPING_METHOD:
      g_value_set_enum (value, filter->clipping_method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstAudioFilter vmethod implementations */
static gboolean
gst_audio_amplify_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstAudioAmplify *filter = GST_AUDIO_AMPLIFY (base);
  gboolean ret;

  if (format->type == GST_BUFTYPE_LINEAR && format->width == 16)
    filter->format_index = 0;
  else if (format->type == GST_BUFTYPE_FLOAT && format->width == 32)
    filter->format_index = 1;
  else
    goto wrong_format;

  ret = gst_audio_amplify_set_process_function (filter);
  if (!ret)
    GST_WARNING ("can't process input");

  return ret;

wrong_format:
  GST_DEBUG ("wrong format");
  return FALSE;
}

static void
gst_audio_amplify_transform_int_clip (GstAudioAmplify * filter,
    gint16 * data, guint num_samples)
{
  gint i;
  glong val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    *data++ = (gint16) CLAMP (val, G_MININT16, G_MAXINT16);
  }
}

static void
gst_audio_amplify_transform_int_wrap_negative (GstAudioAmplify * filter,
    gint16 * data, guint num_samples)
{
  gint i;
  glong val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    if (val > G_MAXINT16)
      val = ((val - G_MININT16) & 0xffff) + G_MININT16;
    else if (val < G_MININT16)
      val = ((val - G_MAXINT16) & 0xffff) + G_MAXINT16;
    *data++ = val;
  }
}

static void
gst_audio_amplify_transform_int_wrap_positive (GstAudioAmplify * filter,
    gint16 * data, guint num_samples)
{
  gint i;
  glong val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    while (val > G_MAXINT16 || val < G_MININT16) {
      if (val > G_MAXINT16)
        val = G_MAXINT16 - (val - G_MAXINT16);
      else if (val < G_MININT16)
        val = G_MININT16 - (val - G_MININT16);
    }
    *data++ = val;
  }
}

static void
gst_audio_amplify_transform_float_clip (GstAudioAmplify * filter,
    gfloat * data, guint num_samples)
{
  gint i;
  gfloat val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    if (val > 1.0)
      val = 1.0;
    else if (val < -1.0)
      val = -1.0;

    *data++ = val;
  }
}

static void
gst_audio_amplify_transform_float_wrap_negative (GstAudioAmplify * filter,
    gfloat * data, guint num_samples)
{
  gint i;
  gfloat val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    while (val > 1.0 || val < -1.0) {
      if (val > 1.0)
        val = -1.0 + (val - 1.0);
      else if (val < -1.0)
        val = 1.0 + (val + 1.0);
    }
    *data++ = val;
  }
}

static void
gst_audio_amplify_transform_float_wrap_positive (GstAudioAmplify * filter,
    gfloat * data, guint num_samples)
{
  gint i;
  gfloat val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * filter->amplification;
    while (val > 1.0 || val < -1.0) {
      if (val > 1.0)
        val = 1.0 - (val - 1.0);
      else if (val < -1.0)
        val = -1.0 - (val + 1.0);
    }
    *data++ = val;
  }
}

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_audio_amplify_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstAudioAmplify *filter = GST_AUDIO_AMPLIFY (base);
  guint num_samples =
      GST_BUFFER_SIZE (buf) / (GST_AUDIO_FILTER (filter)->format.width / 8);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
    gst_object_sync_values (G_OBJECT (filter), GST_BUFFER_TIMESTAMP (buf));

  if (gst_base_transform_is_passthrough (base) ||
      G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP)))
    return GST_FLOW_OK;

  filter->process (filter, GST_BUFFER_DATA (buf), num_samples);

  return GST_FLOW_OK;
}
