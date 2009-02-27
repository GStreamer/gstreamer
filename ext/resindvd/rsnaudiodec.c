/* GStreamer
 * Copyright (C) <2009> Jan Schmidt <thaytan@noraisin.net>
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
#include "config.h"
#endif

#include "rsnaudiodec.h"

GST_DEBUG_CATEGORY_STATIC (rsn_audiodec_debug);
#define GST_CAT_DEFAULT rsn_audiodec_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg,mpegversion=(int)1;"
        "audio/x-private1-lpcm;"
        "audio/x-private1-ac3;" "audio/x-private1-dts;" "audio/ac3")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 32, 64 }; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 24, "
        "depth = (int) 24, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true")
    );

G_DEFINE_TYPE (RsnAudioDec, rsn_audiodec, GST_TYPE_BIN);

static gboolean rsn_audiodec_set_sink_caps (GstPad * sinkpad, GstCaps * caps);
static GstCaps *rsn_audiodec_get_sink_caps (GstPad * sinkpad);
static GstFlowReturn rsn_audiodec_chain (GstPad * pad, GstBuffer * buf);
static gboolean rsn_audiodec_sink_event (GstPad * pad, GstEvent * event);

static void
rsn_audiodec_class_init (RsnAudioDecClass * klass)
{
  static GstElementDetails element_details = {
    "RsnAudioDec",
    "Audio/Decoder",
    "Resin DVD audio stream decoder",
    "Jan Schmidt <thaytan@noraisin.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (rsn_audiodec_debug, "rsn_audiodec",
      0, "Resin DVD audio stream decoder");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &element_details);
}

static void
rsn_audiodec_init (RsnAudioDec * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_set_sink_caps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_get_sink_caps));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static gboolean
rsn_audiodec_set_sink_caps (GstPad * sinkpad, GstCaps * caps)
{
  return FALSE;
}

static GstCaps *
rsn_audiodec_get_sink_caps (GstPad * sinkpad)
{
  return NULL;
}

static GstFlowReturn
rsn_audiodec_chain (GstPad * pad, GstBuffer * buf)
{
  return GST_FLOW_ERROR;
}

static gboolean
rsn_audiodec_sink_event (GstPad * pad, GstEvent * event)
{
  return FALSE;
}
