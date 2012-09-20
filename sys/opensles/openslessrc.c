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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "openslessrc.h"

GST_DEBUG_CATEGORY_STATIC (opensles_src_debug);
#define GST_CAT_DEFAULT opensles_src_debug

#define RATES "8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000, 192000"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { " RATES "}, "
        "channels = (int) [1, 2];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) { " RATES "}, " "channels = (int) [1, 2]")
    );

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (opensles_src_debug, "opensles_src", 0,
      "OpenSL ES Src");
}

GST_BOILERPLATE_FULL (GstOpenSLESSrc, gst_opensles_src, GstBaseAudioSrc,
    GST_TYPE_BASE_AUDIO_SRC, _do_init);

static void
gst_opensles_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);

  gst_element_class_set_details_simple (element_class, "OpenSL ES Src",
      "Src/Audio",
      "Input sound using the OpenSL ES APIs",
      "Josep Torra <support@fluendo.com>");
}

static GstRingBuffer *
gst_opensles_src_create_ringbuffer (GstBaseAudioSrc * base)
{
  GstRingBuffer *rb;

  rb = gst_opensles_ringbuffer_new (RB_MODE_SRC);

  return rb;
}

static void
gst_opensles_src_class_init (GstOpenSLESSrcClass * klass)
{
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;

  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbaseaudiosrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_opensles_src_create_ringbuffer);
}

static void
gst_opensles_src_init (GstOpenSLESSrc * src, GstOpenSLESSrcClass * gclass)
{
}
