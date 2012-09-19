/*
 * GStreamer
 * Copyright (C) 2005,2006 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2007,2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
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
 *
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

/**
 * SECTION:element-osxaudiosink
 *
 * This element renders raw audio samples using the CoreAudio api.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! osxaudiosink
 * ]| Play an Ogg/Vorbis file.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/multichannel.h>
#include <gst/audio/gstaudioiec61937.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioHardware.h>

#include "gstosxaudiosink.h"
#include "gstosxaudioelement.h"

GST_DEBUG_CATEGORY_STATIC (osx_audiosink_debug);
#define GST_CAT_DEFAULT osx_audiosink_debug

#include "gstosxcoreaudio.h"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_VOLUME
};

#define DEFAULT_VOLUME 1.0

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, 9];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, 9];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 24, "
        "depth = (int) 24, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, 9];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, 9];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [1, MAX], " "channels = (int) [1, MAX];"
        "audio/x-ac3, framed = (boolean) true;"
        "audio/x-dts, framed = (boolean) true")
    );

static void gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_osx_audio_sink_stop (GstBaseSink * base);
static GstCaps *gst_osx_audio_sink_getcaps (GstBaseSink * base);
static gboolean gst_osx_audio_sink_acceptcaps (GstPad * pad, GstCaps * caps);

static GstBuffer *gst_osx_audio_sink_sink_payload (GstBaseAudioSink * sink,
    GstBuffer * buf);
static GstRingBuffer *gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink *
    sink);
static void gst_osx_audio_sink_osxelement_init (gpointer g_iface,
    gpointer iface_data);
static gboolean gst_osx_audio_sink_select_device (GstOsxAudioSink * osxsink);
static void gst_osx_audio_sink_set_volume (GstOsxAudioSink * sink);

static OSStatus gst_osx_audio_sink_io_proc (GstOsxRingBuffer * buf,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList * bufferList);

static void
gst_osx_audio_sink_do_init (GType type)
{
  static const GInterfaceInfo osxelement_info = {
    gst_osx_audio_sink_osxelement_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (osx_audiosink_debug, "osxaudiosink", 0,
      "OSX Audio Sink");
  GST_DEBUG ("Adding static interface");
  g_type_add_interface_static (type, GST_OSX_AUDIO_ELEMENT_TYPE,
      &osxelement_info);
}

GST_BOILERPLATE_FULL (GstOsxAudioSink, gst_osx_audio_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, gst_osx_audio_sink_do_init);

static void
gst_osx_audio_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (element_class, "Audio Sink (OSX)",
      "Sink/Audio",
      "Output to a sound card in OS X",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");
}

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

  gobject_class->set_property = gst_osx_audio_sink_set_property;
  gobject_class->get_property = gst_osx_audio_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_int ("device", "Device ID", "Device ID of output device",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_osx_audio_sink_getcaps);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_osx_audio_sink_stop);

  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_create_ringbuffer);
  gstbaseaudiosink_class->payload =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_sink_payload);
}

static void
gst_osx_audio_sink_init (GstOsxAudioSink * sink, GstOsxAudioSinkClass * gclass)
{
  GST_DEBUG ("Initialising object");

  sink->device_id = kAudioDeviceUnknown;
  sink->cached_caps = NULL;

  sink->volume = DEFAULT_VOLUME;

  gst_pad_set_acceptcaps_function (GST_BASE_SINK (sink)->sinkpad,
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_acceptcaps));
}

static void
gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      sink->device_id = g_value_get_int (value);
      break;
    case ARG_VOLUME:
      sink->volume = g_value_get_double (value);
      gst_osx_audio_sink_set_volume (sink);
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
  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_int (value, sink->device_id);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, sink->volume);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_osx_audio_sink_stop (GstBaseSink * base)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (base);

  if (sink->cached_caps) {
    gst_caps_unref (sink->cached_caps);
    sink->cached_caps = NULL;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SINK_CLASS, stop, (base), TRUE);
}

static GstCaps *
gst_osx_audio_sink_getcaps (GstBaseSink * base)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (base);
  gchar *caps_string = NULL;

  if (sink->cached_caps) {
    caps_string = gst_caps_to_string (sink->cached_caps);
    GST_DEBUG_OBJECT (sink, "using cached caps: %s", caps_string);
    g_free (caps_string);
    return gst_caps_ref (sink->cached_caps);
  }

  GST_DEBUG_OBJECT (sink, "using template caps");
  return NULL;
}

static gboolean
gst_osx_audio_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (gst_pad_get_parent_element (pad));
  GstOsxRingBuffer *osxbuf;
  GstCaps *pad_caps;
  GstStructure *st;
  gboolean ret = FALSE;
  GstRingBufferSpec spec = { 0 };
  gchar *caps_string = NULL;

  osxbuf = GST_OSX_RING_BUFFER (GST_BASE_AUDIO_SINK (sink)->ringbuffer);

  caps_string = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (sink, "acceptcaps called with %s", caps_string);
  g_free (caps_string);

  pad_caps = gst_pad_get_caps (pad);
  if (pad_caps) {
    gboolean cret = gst_caps_can_intersect (pad_caps, caps);
    gst_caps_unref (pad_caps);
    if (!cret)
      goto done;
  }

  /* If we've not got fixed caps, creating a stream might fail,
   * so let's just return from here with default acceptcaps
   * behaviour */
  if (!gst_caps_is_fixed (caps))
    goto done;

  /* parse helper expects this set, so avoid nasty warning
   * will be set properly later on anyway  */
  spec.latency_time = GST_SECOND;
  if (!gst_ring_buffer_parse_caps (&spec, caps))
    goto done;

  /* Make sure input is framed and can be payloaded */
  switch (spec.type) {
    case GST_BUFTYPE_AC3:
    {
      gboolean framed = FALSE;

      st = gst_caps_get_structure (caps, 0);

      gst_structure_get_boolean (st, "framed", &framed);
      if (!framed || gst_audio_iec61937_frame_size (&spec) <= 0)
        goto done;
      break;
    }
    case GST_BUFTYPE_DTS:
    {
      gboolean parsed = FALSE;

      st = gst_caps_get_structure (caps, 0);

      gst_structure_get_boolean (st, "parsed", &parsed);
      if (!parsed || gst_audio_iec61937_frame_size (&spec) <= 0)
        goto done;
      break;
    }
    default:
      break;
  }
  ret = TRUE;

done:
  gst_object_unref (sink);
  return ret;
}

static GstBuffer *
gst_osx_audio_sink_sink_payload (GstBaseAudioSink * sink, GstBuffer * buf)
{
  GstOsxAudioSink *osxsink;

  osxsink = GST_OSX_AUDIO_SINK (sink);

  if (RINGBUFFER_IS_SPDIF (sink->ringbuffer->spec.type)) {
    gint framesize = gst_audio_iec61937_frame_size (&sink->ringbuffer->spec);
    GstBuffer *out;

    if (framesize <= 0)
      return NULL;

    out = gst_buffer_new_and_alloc (framesize);

    /* FIXME: the endianness needs to be queried and then set */
    if (!gst_audio_iec61937_payload (GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf), GST_BUFFER_DATA (out),
            GST_BUFFER_SIZE (out), &sink->ringbuffer->spec, G_BYTE_ORDER)) {
      gst_buffer_unref (out);
      return NULL;
    }

    gst_buffer_copy_metadata (out, buf, GST_BUFFER_COPY_ALL);

    /* Fix endianness */
    swab ((gchar *) GST_BUFFER_DATA (buf),
        (gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    return out;
  } else {
    return gst_buffer_ref (buf);
  }
}

static GstRingBuffer *
gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstOsxAudioSink *osxsink;
  GstOsxRingBuffer *ringbuffer;

  osxsink = GST_OSX_AUDIO_SINK (sink);

  if (!gst_osx_audio_sink_select_device (osxsink)) {
    return NULL;
  }

  GST_DEBUG ("Creating ringbuffer");
  ringbuffer = g_object_new (GST_TYPE_OSX_RING_BUFFER, NULL);
  GST_DEBUG ("osx sink %p element %p  ioproc %p", osxsink,
      GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink),
      (void *) gst_osx_audio_sink_io_proc);

  gst_osx_audio_sink_set_volume (osxsink);

  ringbuffer->element = GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink);
  ringbuffer->device_id = osxsink->device_id;

  return GST_RING_BUFFER (ringbuffer);
}

/* HALOutput AudioUnit will request fairly arbitrarily-sized chunks
 * of data, not of a fixed size. So, we keep track of where in
 * the current ringbuffer segment we are, and only advance the segment
 * once we've read the whole thing */
static OSStatus
gst_osx_audio_sink_io_proc (GstOsxRingBuffer * buf,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList * bufferList)
{
  guint8 *readptr;
  gint readseg;
  gint len;
  gint stream_idx = buf->stream_idx;
  gint remaining = bufferList->mBuffers[stream_idx].mDataByteSize;
  gint offset = 0;

  while (remaining) {
    if (!gst_ring_buffer_prepare_read (GST_RING_BUFFER (buf),
            &readseg, &readptr, &len))
      return 0;

    len -= buf->segoffset;

    if (len > remaining)
      len = remaining;

    memcpy ((char *) bufferList->mBuffers[stream_idx].mData + offset,
        readptr + buf->segoffset, len);

    buf->segoffset += len;
    offset += len;
    remaining -= len;

    if ((gint) buf->segoffset == GST_RING_BUFFER (buf)->spec.segsize) {
      /* clear written samples */
      gst_ring_buffer_clear (GST_RING_BUFFER (buf), readseg);

      /* we wrote one segment */
      gst_ring_buffer_advance (GST_RING_BUFFER (buf), 1);

      buf->segoffset = 0;
    }
  }
  return 0;
}

static void
gst_osx_audio_sink_osxelement_init (gpointer g_iface, gpointer iface_data)
{
  GstOsxAudioElementInterface *iface = (GstOsxAudioElementInterface *) g_iface;

  iface->io_proc = (AURenderCallback) gst_osx_audio_sink_io_proc;
}

static void
gst_osx_audio_sink_set_volume (GstOsxAudioSink * sink)
{
  if (!sink->audiounit)
    return;

  AudioUnitSetParameter (sink->audiounit, kHALOutputParam_Volume,
      kAudioUnitScope_Global, 0, (float) sink->volume, 0);
}

static inline void
_dump_channel_layout (AudioChannelLayout * channel_layout)
{
  UInt32 i;

  GST_DEBUG ("mChannelLayoutTag: 0x%lx",
      (unsigned long) channel_layout->mChannelLayoutTag);
  GST_DEBUG ("mChannelBitmap: 0x%lx",
      (unsigned long) channel_layout->mChannelBitmap);
  GST_DEBUG ("mNumberChannelDescriptions: %lu",
      (unsigned long) channel_layout->mNumberChannelDescriptions);
  for (i = 0; i < channel_layout->mNumberChannelDescriptions; i++) {
    AudioChannelDescription *channel_desc =
        &channel_layout->mChannelDescriptions[i];
    GST_DEBUG ("  mChannelLabel: 0x%lx mChannelFlags: 0x%lx "
        "mCoordinates[0]: %f mCoordinates[1]: %f "
        "mCoordinates[2]: %f",
        (unsigned long) channel_desc->mChannelLabel,
        (unsigned long) channel_desc->mChannelFlags,
        channel_desc->mCoordinates[0], channel_desc->mCoordinates[1],
        channel_desc->mCoordinates[2]);
  }
}

static gboolean
gst_osx_audio_sink_allowed_caps (GstOsxAudioSink * osxsink)
{
  gint i, max_channels = 0;
  gboolean spdif_allowed, use_positions = FALSE;
  AudioChannelLayout *layout;
  GstElementClass *element_class;
  GstPadTemplate *pad_template;
  GstCaps *caps, *in_caps;

  GstAudioChannelPosition pos[9] = {
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID,
    GST_AUDIO_CHANNEL_POSITION_INVALID
  };

  /* First collect info about the HW capabilites and preferences */
  spdif_allowed = _audio_device_is_spdif_avail (osxsink->device_id);
  layout = _audio_device_get_channel_layout (osxsink->device_id);

  GST_DEBUG_OBJECT (osxsink, "Selected device ID: %u SPDIF allowed: %d",
      (unsigned) osxsink->device_id, spdif_allowed);

  if (layout) {
    _dump_channel_layout (layout);
    max_channels = layout->mNumberChannelDescriptions;
  } else {
    GST_WARNING_OBJECT (osxsink, "This driver does not support "
        "kAudioDevicePropertyPreferredChannelLayout.");
    max_channels = 2;
  }

  if (max_channels > 2) {
    max_channels = MIN (max_channels, 9);
    use_positions = TRUE;
    for (i = 0; i < max_channels; i++) {
      switch (layout->mChannelDescriptions[i].mChannelLabel) {
        case kAudioChannelLabel_Left:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          break;
        case kAudioChannelLabel_Right:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          break;
        case kAudioChannelLabel_Center:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
          break;
        case kAudioChannelLabel_LFEScreen:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_LFE;
          break;
        case kAudioChannelLabel_LeftSurround:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
          break;
        case kAudioChannelLabel_RightSurround:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
          break;
        case kAudioChannelLabel_RearSurroundLeft:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
          break;
        case kAudioChannelLabel_RearSurroundRight:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
          break;
        case kAudioChannelLabel_CenterSurround:
          pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
          break;
        default:
          GST_WARNING_OBJECT (osxsink, "unrecognized channel: %d",
              (int) layout->mChannelDescriptions[i].mChannelLabel);
          use_positions = FALSE;
          max_channels = 2;
          break;
      }
    }
  }
  g_free (layout);

  /* Recover the template caps */
  element_class = GST_ELEMENT_GET_CLASS (osxsink);
  pad_template = gst_element_class_get_pad_template (element_class, "sink");
  in_caps = gst_pad_template_get_caps (pad_template);

  /* Create the allowed subset  */
  caps = gst_caps_new_empty ();
  for (i = 0; i < gst_caps_get_size (in_caps); i++) {
    GstStructure *in_s, *out_s;

    in_s = gst_caps_get_structure (in_caps, i);

    if (gst_structure_has_name (in_s, "audio/x-ac3") ||
        gst_structure_has_name (in_s, "audio/x-dts")) {
      if (spdif_allowed) {
        gst_caps_append_structure (caps, gst_structure_copy (in_s));
      }
    } else {
      if (max_channels > 2 && use_positions) {
        out_s = gst_structure_copy (in_s);
        gst_structure_remove_field (out_s, "channels");
        gst_structure_set (out_s, "channels", G_TYPE_INT, max_channels, NULL);
        gst_audio_set_channel_positions (out_s, pos);
        gst_caps_append_structure (caps, out_s);
      }
      out_s = gst_structure_copy (in_s);
      gst_structure_remove_field (out_s, "channels");
      gst_structure_set (out_s, "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
      gst_caps_append_structure (caps, out_s);
    }
  }

  if (osxsink->cached_caps) {
    gst_caps_unref (osxsink->cached_caps);
  }

  osxsink->cached_caps = caps;

  return TRUE;
}

static gboolean
gst_osx_audio_sink_select_device (GstOsxAudioSink * osxsink)
{
  AudioDeviceID *devices = NULL;
  AudioDeviceID default_device_id = 0;
  AudioChannelLayout *channel_layout;
  gint i, ndevices = 0;
  gboolean res = FALSE;

  devices = _audio_system_get_devices (&ndevices);

  if (ndevices < 1) {
    GST_ERROR_OBJECT (osxsink, "no audio output devices found");
    goto done;
  }

  GST_DEBUG_OBJECT (osxsink, "found %d audio device(s)", ndevices);

  for (i = 0; i < ndevices; i++) {
    gchar *device_name;

    if ((device_name = _audio_device_get_name (devices[i]))) {
      if (!_audio_device_has_output (devices[i])) {
        GST_DEBUG_OBJECT (osxsink, "Input Device ID: %u Name: %s",
            (unsigned) devices[i], device_name);
      } else {
        GST_DEBUG_OBJECT (osxsink, "Output Device ID: %u Name: %s",
            (unsigned) devices[i], device_name);

        channel_layout = _audio_device_get_channel_layout (devices[i]);
        if (channel_layout) {
          _dump_channel_layout (channel_layout);
          g_free (channel_layout);
        }
      }

      g_free (device_name);
    }
  }

  /* Find the ID of the default output device */
  default_device_id = _audio_system_get_default_output ();

  /* Here we decide if selected device is valid or autoselect
   * the default one when required */
  if (osxsink->device_id == kAudioDeviceUnknown) {
    if (default_device_id != kAudioDeviceUnknown) {
      osxsink->device_id = default_device_id;
      res = TRUE;
    }
  } else {
    for (i = 0; i < ndevices; i++) {
      if (osxsink->device_id == devices[i]) {
        res = TRUE;
      }
    }

    if (res && !_audio_device_is_alive (osxsink->device_id)) {
      GST_ERROR_OBJECT (osxsink, "Requested device not usable");
      res = FALSE;
      goto done;
    }
  }

  res = gst_osx_audio_sink_allowed_caps (osxsink);

done:
  g_free (devices);

  return res;
}
