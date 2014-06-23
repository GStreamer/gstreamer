/* GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-openslessrc
 * @see_also: openslessink
 *
 * This element reads data from default audio input using the OpenSL ES API in Android OS.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v openslessrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=recorded.ogg
 * ]| Record from default audio input and encode to Ogg/Vorbis.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "openslessrc.h"

GST_DEBUG_CATEGORY_STATIC (opensles_src_debug);
#define GST_CAT_DEFAULT opensles_src_debug

/* *INDENT-OFF* */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) { 16000 }, "
        "channels = (int) 1")
    );
/* *INDENT-ON* */

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (opensles_src_debug, "opensles_src", 0, \
      "OpenSL ES Src");
#define parent_class gst_opensles_src_parent_class
G_DEFINE_TYPE_WITH_CODE (GstOpenSLESSrc, gst_opensles_src,
    GST_TYPE_AUDIO_BASE_SRC, _do_init);

static GstAudioRingBuffer *
gst_opensles_src_create_ringbuffer (GstAudioBaseSrc * base)
{
  GstAudioRingBuffer *rb;

  rb = gst_opensles_ringbuffer_new (RB_MODE_SRC);

  return rb;
}

static void
gst_opensles_src_class_init (GstOpenSLESSrcClass * klass)
{
  GstElementClass *gstelement_class;
  GstAudioBaseSrcClass *gstaudiobasesrc_class;

  gstelement_class = (GstElementClass *) klass;
  gstaudiobasesrc_class = (GstAudioBaseSrcClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_static_metadata (gstelement_class, "OpenSL ES Src",
      "Source/Audio",
      "Input sound using the OpenSL ES APIs",
      "Josep Torra <support@fluendo.com>");

  gstaudiobasesrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_opensles_src_create_ringbuffer);
}

static void
gst_opensles_src_init (GstOpenSLESSrc * src)
{
  /* Override some default values to fit on the AudioFlinger behaviour of
   * processing 20ms buffers as minimum buffer size. */
  GST_AUDIO_BASE_SRC (src)->buffer_time = 400000;
  GST_AUDIO_BASE_SRC (src)->latency_time = 20000;
}
