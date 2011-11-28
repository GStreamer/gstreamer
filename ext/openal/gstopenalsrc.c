/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Victor Lin <bornstub@gmail.com>
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
 * SECTION:element-openalsrc
 * @short_description: record sound from your sound card using OpenAL
 *
 * <refsect2>
 * <para>
 * This element lets you record sound using the OpenAL
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v openalsrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=mymusic.ogg
 * </programlisting>
 * will record sound from your sound card using OpenAL and encode it to an Ogg/Vorbis file
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/gsterror.h>

#include "gstopenalsrc.h"

GST_DEBUG_CATEGORY_STATIC (openalsrc_debug);

#define GST_CAT_DEFAULT openalsrc_debug

#define DEFAULT_DEVICE              NULL
#define DEFAULT_DEVICE_NAME         NULL

/**
    Filter signals and args
**/
enum
{
  /* FILL ME */
  LAST_SIGNAL
};


/**
    Properties
**/
enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) TRUE, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

GST_BOILERPLATE (GstOpenalSrc, gst_openal_src, GstAudioSrc, GST_TYPE_AUDIO_SRC);

static void gst_openal_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_openal_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_openal_src_open (GstAudioSrc * src);
static gboolean
gst_openal_src_prepare (GstAudioSrc * src, GstRingBufferSpec * spec);
static gboolean gst_openal_src_unprepare (GstAudioSrc * src);
static gboolean gst_openal_src_close (GstAudioSrc * src);
static guint
gst_openal_src_read (GstAudioSrc * src, gpointer data, guint length);
static guint gst_openal_src_delay (GstAudioSrc * src);
static void gst_openal_src_reset (GstAudioSrc * src);

static void gst_openal_src_finalize (GObject * object);

static void
gst_openal_src_base_init (gpointer gclass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class, "OpenAL src",
      "Source/Audio",
      "OpenAL source capture audio from device",
      "Victor Lin <bornstub@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_openal_src_class_init (GstOpenalSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioSrcClass *gstaudio_src_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstaudio_src_class = GST_AUDIO_SRC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (openalsrc_debug, "openalsrc",
      0, "OpenAL source capture audio from device");

  gobject_class->set_property = gst_openal_src_set_property;
  gobject_class->get_property = gst_openal_src_get_property;
  gobject_class->finalize = gst_openal_src_finalize;

  gstaudio_src_class->open = GST_DEBUG_FUNCPTR (gst_openal_src_open);
  gstaudio_src_class->prepare = GST_DEBUG_FUNCPTR (gst_openal_src_prepare);
  gstaudio_src_class->unprepare = GST_DEBUG_FUNCPTR (gst_openal_src_unprepare);
  gstaudio_src_class->close = GST_DEBUG_FUNCPTR (gst_openal_src_close);
  gstaudio_src_class->read = GST_DEBUG_FUNCPTR (gst_openal_src_read);
  gstaudio_src_class->delay = GST_DEBUG_FUNCPTR (gst_openal_src_delay);
  gstaudio_src_class->reset = GST_DEBUG_FUNCPTR (gst_openal_src_reset);

  g_object_class_install_property (gobject_class,
      PROP_DEVICE,
      g_param_spec_string ("device",
          "Device",
          "Specific capture device to open, NULL indicate default device",
          DEFAULT_DEVICE, G_PARAM_READWRITE)
      );

  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name",
          "Device name",
          "Readable name of device", DEFAULT_DEVICE_NAME, G_PARAM_READABLE)
      );
}

static void
gst_openal_src_init (GstOpenalSrc * osrc, GstOpenalSrcClass * gclass)
{
  osrc->deviceName = g_strdup (DEFAULT_DEVICE_NAME);
  osrc->device = DEFAULT_DEVICE;
  osrc->deviceHandle = NULL;
}

static void
gst_openal_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenalSrc *osrc = GST_OPENAL_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      osrc->device = g_value_dup_string (value);
      break;
    case PROP_DEVICE_NAME:
      osrc->deviceName = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openal_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOpenalSrc *osrc = GST_OPENAL_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, osrc->device);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, osrc->deviceName);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_openal_src_open (GstAudioSrc * asrc)
{
  /* We don't do anything here */
  return TRUE;
}

static gboolean
gst_openal_src_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{

  GstOpenalSrc *osrc = GST_OPENAL_SRC (asrc);
  ALenum format;
  guint64 bufferSize;

  switch (spec->width) {
    case 8:
      format = AL_FORMAT_STEREO8;
      break;
    case 16:
      format = AL_FORMAT_STEREO16;
      break;
    default:
      g_assert_not_reached ();
  }

  bufferSize =
      spec->buffer_time * spec->rate * spec->bytes_per_sample / 1000000;

  GST_INFO_OBJECT (osrc, "Open device : %s", osrc->deviceName);
  osrc->deviceHandle =
      alcCaptureOpenDevice (osrc->device, spec->rate, format, bufferSize);

  if (!osrc->deviceHandle) {
    GST_ELEMENT_ERROR (osrc,
        RESOURCE,
        FAILED,
        ("Can't open device \"%s\"", osrc->device),
        ("Can't open device \"%s\"", osrc->device)
        );
    return FALSE;
  }

  osrc->deviceName =
      g_strdup (alcGetString (osrc->deviceHandle, ALC_DEVICE_SPECIFIER));
  osrc->bytes_per_sample = spec->bytes_per_sample;

  GST_INFO_OBJECT (osrc, "Start capture");
  alcCaptureStart (osrc->deviceHandle);

  return TRUE;
}

static gboolean
gst_openal_src_unprepare (GstAudioSrc * asrc)
{

  GstOpenalSrc *osrc = GST_OPENAL_SRC (asrc);

  GST_INFO_OBJECT (osrc, "Close device : %s", osrc->deviceName);
  if (osrc->deviceHandle) {
    alcCaptureStop (osrc->deviceHandle);
    alcCaptureCloseDevice (osrc->deviceHandle);
  }

  return TRUE;
}

static gboolean
gst_openal_src_close (GstAudioSrc * asrc)
{
  /* We don't do anything here */
  return TRUE;
}

static guint
gst_openal_src_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstOpenalSrc *osrc = GST_OPENAL_SRC (asrc);
  gint samples;

  alcGetIntegerv (osrc->deviceHandle, ALC_CAPTURE_SAMPLES, sizeof (samples),
      &samples);

  if (samples * osrc->bytes_per_sample > length) {
    samples = length / osrc->bytes_per_sample;
  }

  if (samples) {
    GST_DEBUG_OBJECT (osrc, "Read samples : %d", samples);
    alcCaptureSamples (osrc->deviceHandle, data, samples);
  }

  return samples * osrc->bytes_per_sample;
}

static guint
gst_openal_src_delay (GstAudioSrc * asrc)
{
  GstOpenalSrc *osrc = GST_OPENAL_SRC (asrc);
  gint samples;

  alcGetIntegerv (osrc->deviceHandle, ALC_CAPTURE_SAMPLES, sizeof (samples),
      &samples);

  return samples;
}

static void
gst_openal_src_reset (GstAudioSrc * asrc)
{
  /* We don't do anything here */
}

static void
gst_openal_src_finalize (GObject * object)
{
  GstOpenalSrc *osrc = GST_OPENAL_SRC (object);

  g_free (osrc->deviceName);
  g_free (osrc->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}
