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
 * SECTION:element-audioinvert
 * @short_description: Swaps upper and lower half of audio samples
 *
 * <refsect2>
 * Swaps upper and lower half of audio samples. Mixing an inverted sample on top of
 * the original with a slight delay can produce effects that sound like resonance.
 * Creating a stereo sample from a mono source, with one channel inverted produces wide-stereo sounds.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc wave=saw ! audioinvert invert=0.4 ! alsasink
 * gst-launch filesrc location="melo1.ogg" ! oggdemux ! vorbisdec ! audioconvert ! audioinvert invert=0.4 ! alsasink
 * gst-launch audiotestsrc wave=saw ! audioconvert ! audioinvert invert=0.4 ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "audioinvert.h"

#define GST_CAT_DEFAULT gst_audio_invert_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("AudioInvert",
    "Filter/Effect/Audio",
    "Swaps upper and lower half of audio samples",
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
  PROP_DEGREE
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_invert_debug, "audioinvert", 0, "audioinvert element");

GST_BOILERPLATE_FULL (GstAudioInvert, gst_audio_invert, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_audio_invert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_invert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_audio_invert_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_audio_invert_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);

static void gst_audio_invert_transform_int (GstAudioInvert * filter,
    gint16 * data, guint num_samples);
static void gst_audio_invert_transform_float (GstAudioInvert * filter,
    gfloat * data, guint num_samples);

/* GObject vmethod implementations */

static void
gst_audio_invert_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_audio_invert_class_init (GstAudioInvertClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_audio_invert_set_property;
  gobject_class->get_property = gst_audio_invert_get_property;

  g_object_class_install_property (gobject_class, PROP_DEGREE,
      g_param_spec_float ("degree", "Degree",
          "Degree of inversion", 0.0, 1.0,
          0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_audio_invert_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_invert_transform_ip);
}

static void
gst_audio_invert_init (GstAudioInvert * filter, GstAudioInvertClass * klass)
{
  filter->degree = 0.0;
  filter->width = 0;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
}

static void
gst_audio_invert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioInvert *filter = GST_AUDIO_INVERT (object);

  switch (prop_id) {
    case PROP_DEGREE:
      filter->degree = g_value_get_float (value);
      gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter),
          filter->degree == 0.0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_invert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioInvert *filter = GST_AUDIO_INVERT (object);

  switch (prop_id) {
    case PROP_DEGREE:
      g_value_set_float (value, filter->degree);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

static gboolean
gst_audio_invert_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAudioInvert *filter = GST_AUDIO_INVERT (base);
  const GstStructure *structure;
  gboolean ret;
  gint width;
  const gchar *fmt;

  /*GST_INFO ("incaps are %" GST_PTR_FORMAT, incaps); */

  structure = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (structure, "width", &width);
  if (!ret)
    goto no_width;
  filter->width = width / 8;


  fmt = gst_structure_get_name (structure);
  if (!strcmp (fmt, "audio/x-raw-int"))
    filter->process = (GstAudioInvertProcessFunc)
        gst_audio_invert_transform_int;
  else
    filter->process = (GstAudioInvertProcessFunc)
        gst_audio_invert_transform_float;

  return TRUE;

no_width:
  GST_DEBUG ("no width in caps");
  return FALSE;
}

static void
gst_audio_invert_transform_int (GstAudioInvert * filter,
    gint16 * data, guint num_samples)
{
  gint i;
  gfloat dry = 1.0 - filter->degree;
  glong val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * dry + (-1 - (*data)) * filter->degree;
    *data++ = (gint16) CLAMP (val, G_MININT16, G_MAXINT16);
  }
}

static void
gst_audio_invert_transform_float (GstAudioInvert * filter,
    gfloat * data, guint num_samples)
{
  gint i;
  gfloat dry = 1.0 - filter->degree;
  glong val;

  for (i = 0; i < num_samples; i++) {
    val = (*data) * dry - (*data) * filter->degree;
    *data++ = val;
  }
}


/* this function does the actual processing
 */
static GstFlowReturn
gst_audio_invert_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstAudioInvert *filter = GST_AUDIO_INVERT (base);
  guint num_samples = GST_BUFFER_SIZE (buf) / filter->width;

  if (!gst_buffer_is_writable (buf))
    return GST_FLOW_OK;

  filter->process (filter, GST_BUFFER_DATA (buf), num_samples);

  return GST_FLOW_OK;
}
