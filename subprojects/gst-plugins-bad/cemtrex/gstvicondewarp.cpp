/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2022  <<user@hostname.org>>
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
 * SECTION:element-vicondewarp
 *
 * FIXME:Describe vicondewarp here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vicondewarp ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

//#ifdef HAVE_CONFIG_H
#include <config.h>
//#endif

#include <iostream>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include "gstvicondewarp.h"

GST_DEBUG_CATEGORY_STATIC (gst_vicondewarp_debug);
#define GST_CAT_DEFAULT gst_vicondewarp_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_SILENT,
    PROP_STATUS,
    PROP_MOUNTPOS,
    PROP_VIEWTYPE,
    PROP_LENSNAME,
    PROP_DEWARP_PROPERTIES
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_vicondewarp_parent_class parent_class
G_DEFINE_TYPE (Gstvicondewarp, gst_vicondewarp, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE (vicondewarp, "vicondewarp", GST_RANK_NONE,
    GST_TYPE_VICONDEWARP);

static void gst_vicondewarp_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vicondewarp_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_vicondewarp_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_vicondewarp_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the vicondewarp's class */
static void
gst_vicondewarp_class_init (GstvicondewarpClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_vicondewarp_set_property;
  gobject_class->get_property = gst_vicondewarp_get_property;

  g_object_class_install_property(gobject_class, PROP_SILENT,
      g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_STATUS,
      g_param_spec_boolean("dewarpstate", "Dewarpstate", "Enable or Disable dewarp",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_MOUNTPOS,
      g_param_spec_int("mountpos", "Mountpos", "Mount Position", 0, 4,
          0, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_VIEWTYPE,
      g_param_spec_int("viewtype", "Viewtype", "View Type", 0, 5,
          0, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_LENSNAME,
      g_param_spec_string("lensname", "Lensname", "Lens Name", "A0**V", G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_DEWARP_PROPERTIES,
      g_param_spec_boxed("dewarp-properties", "Dewarp properties",
          "List of dewarp properties",
          GST_TYPE_STRUCTURE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "vicondewarp",
      "Generic",
      "Dewarps the 360 camera feed", "Jithin Nair jnair@cemtrexlabs.com");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_vicondewarp_init (Gstvicondewarp * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vicondewarp_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vicondewarp_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  ctx = new DWCONTEXT;
  ctx->inputBuf = NULL;
  ctx->outputBuf = NULL;
  ctx->height = 480;
  ctx->width = 640;
  ctx->camera = new IMV_CameraInterface;
  filter->silent = FALSE;
  filter->dewarpstatus = FALSE;
  filter->viewtype = 0;
  filter->mountpos = 0;
  filter->dewarp_prop = gst_structure_new("props",
      "view_1_pan", G_TYPE_FLOAT, 5.0,
      "view_1_tilt", G_TYPE_FLOAT, 5.0,
      "view_1_roll", G_TYPE_FLOAT, 5.0,
      "view_1_zoom", G_TYPE_FLOAT, 30.0,
      "view_2_pan", G_TYPE_FLOAT, 5.0,
      "view_2_tilt", G_TYPE_FLOAT, 5.0,
      "view_2_roll", G_TYPE_FLOAT, 5.0,
      "view_2_zoom", G_TYPE_FLOAT, 110.0,
      "view_3_pan", G_TYPE_FLOAT, 5.0,
      "view_3_tilt", G_TYPE_FLOAT, 5.0,
      "view_3_roll", G_TYPE_FLOAT, 5.0,
      "view_3_zoom", G_TYPE_FLOAT, 110.0,
      "view_4_pan", G_TYPE_FLOAT, 5.0,
      "view_4_tilt", G_TYPE_FLOAT, 5.0,
      "view_4_roll", G_TYPE_FLOAT, 5.0,
      "view_4_zoom", G_TYPE_FLOAT, 110.0,
      NULL);
  filter->silent = FALSE;
}

static void
gst_vicondewarp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstvicondewarp *filter = GST_VICONDEWARP (object);

  switch (prop_id) {
  case PROP_SILENT:
      filter->silent = g_value_get_boolean(value);
      break;
  case PROP_STATUS:
      filter->dewarpstatus = g_value_get_boolean(value);
      break;
  case PROP_MOUNTPOS:
      filter->mountpos = g_value_get_int(value);
      break;
  case PROP_VIEWTYPE:
      filter->viewtype = g_value_get_int(value);
      break;
  case PROP_DEWARP_PROPERTIES:
  {
      const GstStructure* s = gst_value_get_structure(value);
      filter->dewarp_prop = s ? gst_structure_copy(s) : NULL;
      break;
  }
  case PROP_LENSNAME:
      filter->lensname = g_value_get_string(value);
      break;
  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_vicondewarp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstvicondewarp *filter = GST_VICONDEWARP (object);

  switch (prop_id) {
  case PROP_SILENT:
      g_value_set_boolean(value, filter->silent);
      break;
  case PROP_STATUS:
      g_value_set_boolean(value, filter->dewarpstatus);
      break;
  case PROP_MOUNTPOS:
      g_value_set_int(value, filter->mountpos);
      break;
  case PROP_VIEWTYPE:
      g_value_set_int(value, filter->viewtype);
      break;
  case PROP_LENSNAME:
      g_value_set_string(value, filter->lensname);
      break;
  case PROP_DEWARP_PROPERTIES:
      gst_value_set_structure(value, filter->dewarp_prop);
      break;
  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_vicondewarp_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  Gstvicondewarp *filter;
  gboolean ret;

  filter = GST_VICONDEWARP (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      filter->input_caps = gst_caps_copy(caps);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_vicondewarp_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstvicondewarp *filter;

  GstSample* from_sample = NULL;
  GstSample* to_sample = NULL;
  GstSample* from_sample2 = NULL;
  GstSample* to_sample2 = NULL;

  GstCaps* caps = NULL;
  GstCaps* convcaps;
  GstMapInfo map, mmap;
  gboolean res;
  const gchar* frmt;
  int width, height;
  int orig_width, orig_height;
  unsigned char* odata;
  unsigned char* idata;
  const GValue* value;
  gchar* caps_string;

  filter = GST_VICONDEWARP(parent);

  if (filter->dewarpstatus)
  {
      caps = gst_pad_get_current_caps(pad);
      GstStructure* s = gst_caps_get_structure(caps, 0);
      frmt = gst_structure_get_string(s, "format");
      res = gst_structure_get_int(s, "width", &orig_width);
      res |= gst_structure_get_int(s, "height", &orig_height);

      from_sample = gst_sample_new(buf, filter->input_caps, NULL, NULL);

      if (orig_width > 3840 || orig_height > 2160)
      {
          width = 3840;
          height = 2160;
      }

      caps_string = g_strdup_printf("video/x-raw, format=NV12, width=%d, height=%d", width, height);
      convcaps = gst_caps_from_string(caps_string);
      g_free(caps_string);

      to_sample = gst_video_convert_sample(from_sample, convcaps, GST_CLOCK_TIME_NONE, NULL);

      GstBuffer* buffer = gst_sample_get_buffer(to_sample);

      gst_buffer_map(buffer, &map, (GstMapFlags)(GST_MAP_READ));

      int size = width * height * 3;
      GstBuffer* buf0 = gst_buffer_new_allocate(NULL, size, NULL);
      gst_buffer_map(buf0, &mmap, (GstMapFlags)(GST_MAP_WRITE));

      odata = mmap.data;
      idata = map.data;

      ctx->camera->SetLens((char*)filter->lensname);

      ctx->camera->SetZoomLimits(24, 180);

      if (ctx->outputBuf == NULL)
      {
          ctx->outputBuf = new IMV_Buffer;
          ctx->outputBuf->data = new unsigned char[width * height * 4];
      }
      ctx->outputBuf->frameWidth = width;
      ctx->outputBuf->frameHeight = height;
      ctx->outputBuf->frameX = 0;
      ctx->outputBuf->frameY = 0;
      ctx->outputBuf->width = width;
      ctx->outputBuf->height = height;
      if (ctx->inputBuf == NULL)
      {
          ctx->inputBuf = new IMV_Buffer;
      }
      ctx->inputBuf->data = idata;
      ctx->inputBuf->frameWidth = width;
      ctx->inputBuf->frameHeight = height;
      ctx->inputBuf->frameX = 0;
      ctx->inputBuf->frameY = 0;
      ctx->inputBuf->width = width;
      ctx->inputBuf->height = height;

      unsigned long iResult = ctx->camera->SetVideoParams(ctx->inputBuf, ctx->outputBuf,
          IMV_Defs::E_YUV_NV12, filter->viewtype, filter->mountpos);
      if (iResult == IMV_Defs::E_ERR_OK)
      {
          char* acsInfo = ctx->camera->GetACS();
          ctx->camera->SetACS(acsInfo);
      }
      ctx->camera->SetOutputVideoParams(ctx->outputBuf);
      if (filter->dewarp_prop)
      {
          value = gst_structure_get_value(filter->dewarp_prop, "view_1_pan");
          filter->view_1_pan = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_1_tilt");
          filter->view_1_tilt = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_1_zoom");
          filter->view_1_zoom = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_1_roll");
          filter->view_1_roll = g_value_get_double(value);

          value = gst_structure_get_value(filter->dewarp_prop, "view_2_pan");
          filter->view_2_pan = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_2_tilt");
          filter->view_2_tilt = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_2_zoom");
          filter->view_2_zoom = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_2_roll");
          filter->view_2_roll = g_value_get_double(value);

          value = gst_structure_get_value(filter->dewarp_prop, "view_3_pan");
          filter->view_3_pan = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_3_tilt");
          filter->view_3_tilt = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_3_zoom");
          filter->view_3_zoom = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_3_roll");
          filter->view_3_roll = g_value_get_double(value);

          value = gst_structure_get_value(filter->dewarp_prop, "view_4_pan");
          filter->view_4_pan = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_4_tilt");
          filter->view_4_tilt = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_4_zoom");
          filter->view_4_zoom = g_value_get_double(value);
          value = gst_structure_get_value(filter->dewarp_prop, "view_4_roll");
          filter->view_4_roll = g_value_get_double(value);
      }
      if (filter->viewtype == 1)
      {
          ctx->camera->SetPosition(&filter->view_1_pan, &filter->view_1_tilt, &filter->view_1_roll, &filter->view_1_zoom, IMV_Defs::E_COOR_ABSOLUTE, 1);
          ctx->camera->SetPosition(&filter->view_2_pan, &filter->view_2_tilt, &filter->view_2_roll, &filter->view_2_zoom, IMV_Defs::E_COOR_ABSOLUTE, 2);
          ctx->camera->SetPosition(&filter->view_3_pan, &filter->view_3_tilt, &filter->view_3_roll, &filter->view_3_zoom, IMV_Defs::E_COOR_ABSOLUTE, 3);
          ctx->camera->SetPosition(&filter->view_4_pan, &filter->view_4_tilt, &filter->view_4_roll, &filter->view_4_zoom, IMV_Defs::E_COOR_ABSOLUTE, 4);
      }
      else if (filter->viewtype == 2 || filter->viewtype == 4)
      {
          ctx->camera->SetPosition(&filter->view_1_pan, &filter->view_1_tilt, &filter->view_1_roll, &filter->view_1_zoom, IMV_Defs::E_COOR_ABSOLUTE, 1);
          ctx->camera->SetPosition(&filter->view_1_pan, &filter->view_1_tilt, &filter->view_1_roll, &filter->view_1_zoom, IMV_Defs::E_COOR_ABSOLUTE, 2);
      }
      else
      {
          ctx->camera->SetPosition(&filter->view_1_pan, &filter->view_1_tilt, &filter->view_1_roll, &filter->view_1_zoom, IMV_Defs::E_COOR_ABSOLUTE, 1);
      }
      ctx->camera->Update();
      memcpy(odata, ctx->outputBuf->data, size);
      gst_buffer_unmap(buf0, &mmap);
      gst_buffer_unmap(buffer, &map);
      buf0->pts = buf->pts;
      buf0->dts = buf->dts;

      from_sample2 = gst_sample_new(buf0, convcaps, NULL, NULL);
      gst_caps_unref(convcaps);

      caps_string = g_strdup_printf("video/x-raw, format=NV12, width=%d, height=%d", orig_width, orig_height);
      convcaps = gst_caps_from_string(caps_string);
      g_free(caps_string);
      to_sample2 = gst_video_convert_sample(from_sample2, convcaps, GST_CLOCK_TIME_NONE, NULL);
      gst_caps_unref(convcaps);

      GstBuffer* buffer2 = gst_sample_get_buffer(to_sample2);
      buffer2->pts = buf->pts;
      buffer2->dts = buf->dts;

      gst_pad_push(filter->srcpad, buffer2);

      gst_sample_unref(to_sample);
      gst_sample_unref(from_sample);
      gst_sample_unref(to_sample2);
      gst_sample_unref(from_sample2);
      gst_buffer_unref(buf0);
      gst_buffer_unref(buffer);
      gst_buffer_unref(buffer2);
      gst_caps_unref(caps);
  }
  else
  {
      gst_pad_push(filter->srcpad, buf);
  }
  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vicondewarp_init (GstPlugin * vicondewarp)
{
  /* debug category for filtering log messages
   *
   * exchange the string 'Template vicondewarp' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_vicondewarp_debug, "vicondewarp",
      0, "Dewarp vicondewarp");

  return GST_ELEMENT_REGISTER (vicondewarp, vicondewarp);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvicondewarp"
#endif

/* gstreamer looks for this structure to register vicondewarps
 *
 * exchange the string 'Template vicondewarp' with your vicondewarp description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vicondewarp,
    "vicondewarp",
    vicondewarp_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
