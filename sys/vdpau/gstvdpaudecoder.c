/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
#include <gst/controller/gstcontroller.h>

#include <X11/Xlib.h>
#include <vdpau/vdpau_x11.h>
#include "gstvdpaudecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdpaudecoder_debug);
#define GST_CAT_DEFAULT gst_vdpaudecoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_SILENT
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]"));

/* debug category for fltering log messages
 *
 * FIXME:exchange the string 'Template vdpaudecoder' with your description
 */
#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdpaudecoder_debug, "vdpaudecoder", 0, "vdpaudecoder base class");

GST_BOILERPLATE_FULL (GstVDPAUDecoder, gst_vdpaudecoder, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdpaudecoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vdpaudecoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_vdpaudecoder_change_state (GstElement * element,
    GstStateChange transition);

/* GObject vmethod implementations */

static void
gst_vdpaudecoder_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VDPAUDecoder",
      "Generic/Filter",
      "VDPAU decoder base class",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

/* initialize the vdpaudecoder's class */
static void
gst_vdpaudecoder_class_init (GstVDPAUDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_vdpaudecoder_set_property;
  gobject_class->get_property = gst_vdpaudecoder_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vdpaudecoder_change_state);
}

static void
gst_vdpaudecoder_init (GstVDPAUDecoder * dec, GstVDPAUDecoderClass * klass)
{
  dec->display = NULL;
  dec->device = 0;
  dec->silent = FALSE;

  dec->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);

  dec->sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (dec), dec->sink);
}

static GstStateChangeReturn
gst_vdpaudecoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVDPAUDecoder *dec;

  dec = GST_VDPAUDECODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      Display *display;
      int screen;
      VdpStatus status;

      /* FIXME: We probably want to use the same VdpDevice for every VDPAU element */
      display = XOpenDisplay (dec->display);
      if (!display) {
        GST_ELEMENT_ERROR (dec, RESOURCE, WRITE, ("Could not initialise VDPAU"),
            ("Could not open display"));
        return GST_STATE_CHANGE_FAILURE;
      }

      screen = DefaultScreen (display);
      status = vdp_device_create_x11 (display, screen, &dec->device, NULL);
      if (status != VDP_STATUS_OK) {
        GST_ELEMENT_ERROR (dec, RESOURCE, WRITE, ("Could not initialise VDPAU"),
            ("Could not create VDPAU device"));
        XCloseDisplay (display);

        return GST_STATE_CHANGE_FAILURE;
      }
      XCloseDisplay (display);
      break;
    }

    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_vdpaudecoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVDPAUDecoder *dec = GST_VDPAUDECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (dec->display);
      dec->display = g_value_dup_string (value);
      break;
    case PROP_SILENT:
      dec->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdpaudecoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVDPAUDecoder *dec = GST_VDPAUDECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, dec->display);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, dec->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vdpau",
    "vdpau elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
