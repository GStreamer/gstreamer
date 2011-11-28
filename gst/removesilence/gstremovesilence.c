/* GStreamer
 * Copyright (C) 2011 Tiago Katcipis <tiagokatcipis@gmail.com>
 * Copyright (C) 2011 Paulo Pizarro  <paulo.pizarro@gmail.com>
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
 * SECTION:element-removesilence
 *
 * Removes all silence periods from an audio stream, dropping silence buffers.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m filesrc location="audiofile" ! decodebin2 ! removesilence remove=true ! wavenc ! filesink location=without_audio.wav 
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gstremovesilence.h"


GST_DEBUG_CATEGORY_STATIC (gst_remove_silence_debug);
#define GST_CAT_DEFAULT gst_remove_silence_debug
#define DEFAULT_VAD_HYSTERESIS  480     /* 60 mseg */

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_REMOVE,
  PROP_HYSTERESIS
};


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw-int, "
        "rate=[1, MAX], channels=1, endianness=BYTE_ORDER, "
        "width=16, depth=16, signed=true"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw-int, "
        "rate=[1, MAX], channels=1, endianness=BYTE_ORDER, "
        "width=16, depth=16, signed=true"));


#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_remove_silence_debug, "removesilence", 0, "removesilence element")

GST_BOILERPLATE_FULL (GstRemoveSilence, gst_remove_silence, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);


static void gst_remove_silence_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_remove_silence_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_remove_silence_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);
static void gst_remove_silence_finalize (GObject * obj);
static void gst_remove_silence_reset (GstRemoveSilence * filter);

/* GObject vmethod implementations */

static void
gst_remove_silence_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "RemoveSilence",
      "Filter/Effect/Audio",
      "Removes all the silence periods from the audio stream.",
      "Tiago Katcipis <tiagokatcipis@gmail.com>\n \
       Paulo Pizarro  <paulo.pizarro@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}

/* initialize the removesilence's class */
static void
gst_remove_silence_class_init (GstRemoveSilenceClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = gst_remove_silence_finalize;
  gobject_class->set_property = gst_remove_silence_set_property;
  gobject_class->get_property = gst_remove_silence_get_property;

  g_object_class_install_property (gobject_class, PROP_REMOVE,
      g_param_spec_boolean ("remove", "Remove",
          "Set to true to remove silence from the stream, false otherwhise",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_HYSTERESIS,
      g_param_spec_uint64 ("hysteresis",
          "Hysteresis",
          "Set the hysteresis (on samples) used on the internal VAD",
          1, G_MAXUINT64, DEFAULT_VAD_HYSTERESIS, G_PARAM_READWRITE));


  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_remove_silence_transform_ip);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_remove_silence_init (GstRemoveSilence * filter,
    GstRemoveSilenceClass * gclass)
{
  filter->vad = vad_new (DEFAULT_VAD_HYSTERESIS);
  filter->remove = FALSE;

  if (!filter->vad) {
    GST_DEBUG ("Error initializing VAD !!");
    return;
  }

  gst_remove_silence_reset (filter);
}

static void
gst_remove_silence_reset (GstRemoveSilence * filter)
{
  GST_DEBUG ("Reseting VAD");
  if (filter->vad) {
    vad_reset (filter->vad);
  }
  GST_DEBUG ("VAD Reseted");
}

static void
gst_remove_silence_finalize (GObject * obj)
{
  GstRemoveSilence *filter = GST_REMOVE_SILENCE (obj);
  GST_DEBUG ("Destroying VAD");
  vad_destroy (filter->vad);
  filter->vad = NULL;
  GST_DEBUG ("VAD Destroyed");
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_remove_silence_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRemoveSilence *filter = GST_REMOVE_SILENCE (object);

  switch (prop_id) {
    case PROP_REMOVE:
      filter->remove = g_value_get_boolean (value);
      break;
    case PROP_HYSTERESIS:
      vad_set_hysteresis (filter->vad, g_value_get_uint64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_remove_silence_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRemoveSilence *filter = GST_REMOVE_SILENCE (object);

  switch (prop_id) {
    case PROP_REMOVE:
      g_value_set_boolean (value, filter->remove);
      break;
    case PROP_HYSTERESIS:
      g_value_set_uint64 (value, vad_get_hysteresis (filter->vad));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_remove_silence_transform_ip (GstBaseTransform * trans, GstBuffer * inbuf)
{
  GstRemoveSilence *filter = NULL;
  int frame_type;

  filter = GST_REMOVE_SILENCE (trans);

  frame_type =
      vad_update (filter->vad, (gint16 *) GST_BUFFER_DATA (inbuf),
      GST_BUFFER_SIZE (inbuf) / sizeof (gint16));

  if (frame_type == VAD_SILENCE) {

    GST_DEBUG ("Silence detected");

    if (filter->remove) {
      GST_DEBUG ("Removing silence");
      return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }

  }

  return GST_FLOW_OK;
}

/*Plugin init functions*/
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "removesilence", GST_RANK_NONE,
      gst_remove_silence_get_type ());
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "removesilence",
    "Removes silence from an audio stream",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
