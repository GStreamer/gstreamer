/*
 * GStreamer
 * Copyright 2005,2006 Zaheer Abbas Merali  <zaheerabbas at merali dot org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-plugin
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m audiotestsrc ! audioconvert ! osxaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <CoreAudio/CoreAudio.h>
#include "gstosxaudiosink.h"
#include "gstosxaudioelement.h"

GST_DEBUG_CATEGORY_STATIC (osx_audiosink_debug);
#define GST_CAT_DEFAULT osx_audiosink_debug

static GstElementDetails gst_osx_audio_sink_details =
GST_ELEMENT_DETAILS ("Audio Sink (OSX)",
    "Sink/Audio",
    "Output to a sound card in OS X",
    "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, " "rate = (int) 44100, " "channels = (int) 2")
    );

static void gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_osx_audio_sink_getcaps (GstBaseSink * sink);


static GstRingBuffer *gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink *
    sink);
/*static GstCaps* gst_osx_audio_sink_getcaps (GstBaseSink * bsink);*/
static void gst_osx_audio_sink_osxelement_init (gpointer g_iface,
    gpointer iface_data);
OSStatus gst_osx_audio_sink_io_proc (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow, const AudioBufferList * inInputData,
    const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData,
    const AudioTimeStamp * inOutputTime, void *inClientData);
static void
gst_osx_audio_sink_osxelement_do_init (GType type)
{
  static const GInterfaceInfo osxelement_info = {
    gst_osx_audio_sink_osxelement_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (osx_audiosink_debug, "osxaudiosink", 0,
      "OSX Audio Sink");
  GST_DEBUG ("Adding static interface\n");
  g_type_add_interface_static (type, GST_OSX_AUDIO_ELEMENT_TYPE,
      &osxelement_info);
}

GST_BOILERPLATE_FULL (GstOsxAudioSink, gst_osx_audio_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, gst_osx_audio_sink_osxelement_do_init)


     static void gst_osx_audio_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_osx_audio_sink_details);
}

/* initialize the plugin's class */
static void
gst_osx_audio_sink_class_init (GstOsxAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_get_property);

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_int ("device", "Device ID", "Device ID of output device",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_osx_audio_sink_getcaps);
  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_create_ringbuffer);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_osx_audio_sink_init (GstOsxAudioSink * sink, GstOsxAudioSinkClass * gclass)
{
/*  GstElementClass *klass = GST_ELEMENT_GET_CLASS (sink); */
  sink->ringbuffer = NULL;
  GST_DEBUG ("Initialising object\n");
  gst_osx_audio_sink_create_ringbuffer (sink);

}

static void
gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (sink->ringbuffer)
        sink->ringbuffer->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (object);
  int val = 0;

  switch (prop_id) {
    case ARG_DEVICE:
      if (sink->ringbuffer)
        val = sink->ringbuffer->device_id;

      g_value_set_int (value, val);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* GstBaseSink vmethod implementations */
static GstCaps *
gst_osx_audio_sink_getcaps (GstBaseSink * sink)
{
  GstCaps *caps;
  GstOsxAudioSink *osxsink;
  OSStatus status;
  AudioValueRange rates[10];
  UInt32 propertySize;
  int i;

  propertySize = sizeof (AudioValueRange) * 9;
  osxsink = GST_OSX_AUDIO_SINK (sink);

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
          (sink)));


  status = AudioDeviceGetProperty (osxsink->ringbuffer->device_id, 0, FALSE,
      kAudioDevicePropertyAvailableNominalSampleRates, &propertySize, &rates);

  GST_DEBUG
      ("Getting available sample rates: Status: %d number of ranges: %d\n",
      status, propertySize / sizeof (AudioValueRange));

  for (i = 0; i < propertySize / sizeof (AudioValueRange); i++) {
    g_print ("Range from %f to %f\n", rates[i].mMinimum, rates[i].mMaximum);
  }

  return caps;
}

/* GstBaseAudioSink vmethod implementations */
static GstRingBuffer *
gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstOsxAudioSink *osxsink;

  osxsink = GST_OSX_AUDIO_SINK (sink);
  if (!osxsink->ringbuffer) {
    GST_DEBUG ("Creating ringbuffer\n");
    osxsink->ringbuffer = g_object_new (GST_TYPE_OSX_RING_BUFFER, NULL);
    GST_DEBUG ("osx sink 0x%x element 0x%x  ioproc 0x%x\n", osxsink,
        GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink),
        (void *) gst_osx_audio_sink_io_proc);
    osxsink->ringbuffer->element =
        GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink);
  }

  return GST_RING_BUFFER (osxsink->ringbuffer);
}

OSStatus
gst_osx_audio_sink_io_proc (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow, const AudioBufferList * inInputData,
    const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData,
    const AudioTimeStamp * inOutputTime, void *inClientData)
{
  GstOsxRingBuffer *buf = GST_OSX_RING_BUFFER (inClientData);

  guint8 *readptr;
  gint readseg;
  gint len;

  if (gst_ring_buffer_prepare_read (GST_RING_BUFFER (buf), &readseg, &readptr,
          &len)) {
    outOutputData->mBuffers[0].mDataByteSize = len;
    memcpy ((char *) outOutputData->mBuffers[0].mData, readptr, len);

    /* clear written samples */
    gst_ring_buffer_clear (GST_RING_BUFFER (buf), readseg);

    /* we wrote one segment */
    gst_ring_buffer_advance (GST_RING_BUFFER (buf), 1);
  }
  return 0;
}

static void
gst_osx_audio_sink_osxelement_init (gpointer g_iface, gpointer iface_data)
{
  GstOsxAudioElementInterface *iface = (GstOsxAudioElementInterface *) g_iface;

  iface->io_proc = gst_osx_audio_sink_io_proc;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 *
 * exchange the string 'plugin' with your elemnt name
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "osxaudiosink",
      GST_RANK_NONE, GST_TYPE_OSX_AUDIO_SINK);
}

/* this is the structure that gstreamer looks for to register plugins
 *
 * exchange the strings 'plugin' and 'Template plugin' with you plugin name and
 * description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "osxaudio",
    "OSX Audio plugin",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
