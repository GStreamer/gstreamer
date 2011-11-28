/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstchecksumsink.h"

static void gst_checksum_sink_dispose (GObject * object);
static void gst_checksum_sink_finalize (GObject * object);

static gboolean gst_checksum_sink_start (GstBaseSink * sink);
static gboolean gst_checksum_sink_stop (GstBaseSink * sink);
static GstFlowReturn
gst_checksum_sink_render (GstBaseSink * sink, GstBuffer * buffer);

static GstStaticPadTemplate gst_checksum_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_checksum_sink_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */

GST_BOILERPLATE (GstChecksumSink, gst_checksum_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void
gst_checksum_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_checksum_sink_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_checksum_sink_sink_template);

  gst_element_class_set_details_simple (element_class, "Checksum sink",
      "Debug/Sink", "Calculates a checksum for buffers",
      "David Schleef <ds@schleef.org>");
}

static void
gst_checksum_sink_class_init (GstChecksumSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_checksum_sink_dispose;
  gobject_class->finalize = gst_checksum_sink_finalize;
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_checksum_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_checksum_sink_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_checksum_sink_render);
}

static void
gst_checksum_sink_init (GstChecksumSink * checksumsink,
    GstChecksumSinkClass * checksumsink_class)
{
  gst_base_sink_set_sync (GST_BASE_SINK (checksumsink), FALSE);
}

void
gst_checksum_sink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_checksum_sink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_checksum_sink_start (GstBaseSink * sink)
{
  return TRUE;
}

static gboolean
gst_checksum_sink_stop (GstBaseSink * sink)
{
  return TRUE;
}

static GstFlowReturn
gst_checksum_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  gchar *s;

  s = g_compute_checksum_for_data (G_CHECKSUM_SHA1, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  g_print ("%" GST_TIME_FORMAT " %s\n",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), s);

  g_free (s);

  return GST_FLOW_OK;
}
