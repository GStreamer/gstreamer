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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-gstsdimux
 *
 * The gstsdimux element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! gstsdimux ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gst.h>
#include "gstsdimux.h"

/* prototypes */


static void gst_sdi_mux_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_sdi_mux_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_sdi_mux_dispose (GObject * object);
static void gst_sdi_mux_finalize (GObject * object);

static GstPad *gst_sdi_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_sdi_mux_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_sdi_mux_change_state (GstElement * element, GstStateChange transition);
static const GstQueryType *gst_sdi_mux_get_query_types (GstElement * element);
static gboolean gst_sdi_mux_query (GstElement * element, GstQuery * query);
static GstFlowReturn gst_sdi_mux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_sdi_mux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_sdi_mux_src_event (GstPad * pad, GstEvent * event);

enum
{
  PROP_0
};

/* pad templates */

#define GST_VIDEO_CAPS_NTSC(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=480," \
  "framerate=30000/1001,interlaced=TRUE,pixel-aspect-ratio=10/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_NTSC_WIDE(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=480," \
  "framerate=30000/1001,interlaced=TRUE,pixel-aspect-ratio=40/33," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_PAL(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=576," \
  "framerate=25/1,interlaced=TRUE,pixel-aspect-ratio=12/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_PAL_WIDE(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=576," \
  "framerate=25/1,interlaced=TRUE,pixel-aspect-ratio=16/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"

static GstStaticPadTemplate gst_sdi_mux_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_NTSC ("{UYVY,v210}") ";"
        GST_VIDEO_CAPS_PAL ("{UYVY,v210}"))
    );

static GstStaticPadTemplate gst_sdi_mux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("application/x-raw-sdi,rate=270,format=(fourcc){UYVY,v210}")
    );

/* class initialization */

GST_BOILERPLATE (GstSdiMux, gst_sdi_mux, GstElement, GST_TYPE_ELEMENT);

static void
gst_sdi_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sdi_mux_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sdi_mux_sink_template));

  gst_element_class_set_static_metadata (element_class, "SDI Muxer",
      "Muxer",
      "Multiplex raw audio and video into SDI",
      "David Schleef <ds@schleef.org>");
}

static void
gst_sdi_mux_class_init (GstSdiMuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_sdi_mux_set_property;
  gobject_class->get_property = gst_sdi_mux_get_property;
  gobject_class->dispose = gst_sdi_mux_dispose;
  gobject_class->finalize = gst_sdi_mux_finalize;
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_sdi_mux_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_sdi_mux_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_sdi_mux_change_state);
  element_class->get_query_types =
      GST_DEBUG_FUNCPTR (gst_sdi_mux_get_query_types);
  element_class->query = GST_DEBUG_FUNCPTR (gst_sdi_mux_query);

}

static void
gst_sdi_mux_init (GstSdiMux * sdimux, GstSdiMuxClass * sdimux_class)
{

  sdimux->sinkpad =
      gst_pad_new_from_static_template (&gst_sdi_mux_sink_template, "sink");
  gst_pad_set_event_function (sdimux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdi_mux_sink_event));
  gst_pad_set_chain_function (sdimux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdi_mux_chain));
  gst_element_add_pad (GST_ELEMENT (sdimux), sdimux->sinkpad);

  sdimux->srcpad = gst_pad_new_from_static_template (&gst_sdi_mux_src_template,
      "src");
  gst_pad_set_event_function (sdimux->srcpad,
      GST_DEBUG_FUNCPTR (gst_sdi_mux_src_event));
  gst_element_add_pad (GST_ELEMENT (sdimux), sdimux->srcpad);

}

void
gst_sdi_mux_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_SDI_MUX (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_sdi_mux_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_SDI_MUX (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_sdi_mux_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_SDI_MUX (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_sdi_mux_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_SDI_MUX (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



static GstPad *
gst_sdi_mux_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{

  return NULL;
}

static void
gst_sdi_mux_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_sdi_mux_change_state (GstElement * element, GstStateChange transition)
{

  return GST_STATE_CHANGE_SUCCESS;
}

static const GstQueryType *
gst_sdi_mux_get_query_types (GstElement * element)
{

  return NULL;
}

static gboolean
gst_sdi_mux_query (GstElement * element, GstQuery * query)
{

  return FALSE;
}

static GstFlowReturn
gst_sdi_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSdiMux *sdimux;

  sdimux = GST_SDI_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sdimux, "chain");


  gst_object_unref (sdimux);
  return GST_FLOW_OK;
}

static gboolean
gst_sdi_mux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstSdiMux *sdimux;

  sdimux = GST_SDI_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sdimux, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (sdimux->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_push_event (sdimux->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
      res = gst_pad_push_event (sdimux->srcpad, event);
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (sdimux->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (sdimux->srcpad, event);
      break;
  }

  gst_object_unref (sdimux);
  return res;
}

static gboolean
gst_sdi_mux_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstSdiMux *sdimux;

  sdimux = GST_SDI_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sdimux, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_pad_push_event (sdimux->sinkpad, event);
      break;
    default:
      res = gst_pad_push_event (sdimux->sinkpad, event);
      break;
  }

  gst_object_unref (sdimux);
  return res;
}
