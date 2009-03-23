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

#include "vdpauvariables.h"
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

GST_BOILERPLATE_FULL (GstVdpauDecoder, gst_vdpaudecoder, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdpaudecoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vdpaudecoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

gboolean
gst_vdpaudecoder_push_video_surface (GstVdpauDecoder * dec,
    VdpVideoSurface * surface)
{
  switch (dec->format) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      /* YV12 specific code */
      break;
    default:
      break;
  }

  return TRUE;
}

typedef struct
{
  VdpChromaType chroma_type;
  VdpYCbCrFormat format;
  guint32 fourcc;
} VdpauFormats;

static VdpChromaType chroma_types[3] =
    { VDP_CHROMA_TYPE_420, VDP_CHROMA_TYPE_422, VDP_CHROMA_TYPE_444 };
static VdpauFormats formats[6] = {
  {
        VDP_CHROMA_TYPE_420,
        VDP_YCBCR_FORMAT_NV12,
        GST_MAKE_FOURCC ('N', 'V', '1', '2')
      },
  {
        VDP_CHROMA_TYPE_422,
        VDP_YCBCR_FORMAT_UYVY,
        GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')
      },
  {
        VDP_CHROMA_TYPE_444,
        VDP_YCBCR_FORMAT_V8U8Y8A8,
        GST_MAKE_FOURCC ('A', 'Y', 'U', 'V')
      },
  {
        VDP_CHROMA_TYPE_444,
        VDP_YCBCR_FORMAT_Y8U8V8A8,
        GST_MAKE_FOURCC ('A', 'V', 'U', 'Y')
      },
  {
        VDP_CHROMA_TYPE_422,
        VDP_YCBCR_FORMAT_YUYV,
        GST_MAKE_FOURCC ('Y', 'U', 'Y', 'V')
      },
  {
        VDP_CHROMA_TYPE_420,
        VDP_YCBCR_FORMAT_YV12,
        GST_MAKE_FOURCC ('Y', 'V', '1', '2')
      }
};

static GstCaps *
gst_vdpaudecoder_get_vdpau_support (GstVdpauDecoder * dec)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();

  for (i = 0; i < 3; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    status = vdp_video_surface_query_capabilities (dec->device, chroma_types[i],
        &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE) {
      GST_ELEMENT_ERROR (dec, RESOURCE, READ,
          ("Could not get VDPAU capabilites"),
          ("Could not query video surface capabilities"));

      return NULL;
    }
    if (is_supported) {
      gint j;

      for (j = 0; j < 6; j++) {
        if (formats[j].chroma_type != chroma_types[i])
          continue;

        status =
            vdp_video_surface_query_ycbcr_capabilities (dec->device,
            formats[j].chroma_type, formats[j].format, &is_supported);
        if (status != VDP_STATUS_OK
            && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT) {
          GST_ELEMENT_ERROR (dec, RESOURCE, READ,
              ("Could not get VDPAU capabilites"),
              ("Could not query video surface ycbcr capabilities"));

          return NULL;
        }
        if (is_supported) {
          GstCaps *format_caps;

          format_caps = gst_caps_new_simple ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, formats[j].fourcc,
              "width", GST_TYPE_INT_RANGE, 1, max_w,
              "height", GST_TYPE_INT_RANGE, 1, max_h,
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
          gst_caps_append (caps, format_caps);
        }
      }
    }
  }
  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return NULL;
  }

  return caps;
}

static gboolean
gst_vdpaudecoder_init_vdpau (GstVdpauDecoder * dec)
{
  Display *display;
  int screen;
  VdpStatus status;
  GstCaps *caps;

  /* FIXME: We probably want to use the same VdpDevice for every VDPAU element */
  display = XOpenDisplay (dec->display);
  if (!display) {
    GST_ELEMENT_ERROR (dec, RESOURCE, READ, ("Could not initialise VDPAU"),
        ("Could not open display"));
    return FALSE;
  }

  screen = DefaultScreen (display);
  status =
      vdp_device_create_x11 (display, screen, &dec->device,
      &vdp_get_proc_address);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (dec, RESOURCE, READ, ("Could not initialise VDPAU"),
        ("Could not create VDPAU device"));
    XCloseDisplay (display);

    return FALSE;
  }
  XCloseDisplay (display);

  vdp_get_proc_address (dec->device,
      VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES,
      (void **) &vdp_video_surface_query_capabilities);
  vdp_get_proc_address (dec->device,
      VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES,
      (void **) &vdp_video_surface_query_ycbcr_capabilities);
  vdp_get_proc_address (dec->device,
      VDP_FUNC_ID_DEVICE_DESTROY, (void **) &vdp_device_destroy);

  caps = gst_vdpaudecoder_get_vdpau_support (dec);
  if (!caps) {
    vdp_device_destroy (dec->device);
    dec->device = 0;
    return FALSE;
  }

  dec->src_caps = caps;

  return TRUE;
}

static GstStateChangeReturn
gst_vdpaudecoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVdpauDecoder *dec;

  dec = GST_VDPAU_DECODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_vdpaudecoder_init_vdpau (dec))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_vdpaudecoder_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstVdpauDecoder *dec = GST_VDPAU_DECODER (GST_OBJECT_PARENT (pad));

  GstCaps *src_caps, *new_caps;
  GstStructure *structure;
  gint width, height;
  gint framerate_numerator, framerate_denominator;
  guint32 fourcc_format;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate",
      &framerate_numerator, &framerate_denominator);

  src_caps = gst_pad_get_allowed_caps (dec->src);

  structure = gst_caps_get_structure (src_caps, 0);
  gst_structure_get_fourcc (structure, "format", &fourcc_format);
  gst_structure_set (structure,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, framerate_numerator,
      framerate_denominator, NULL);

  new_caps = gst_caps_copy_nth (src_caps, 0);
  gst_caps_unref (src_caps);
  gst_pad_fixate_caps (dec->src, new_caps);
  res = gst_pad_set_caps (dec->src, new_caps);

  gst_caps_unref (new_caps);

  if (!res)
    return FALSE;

  dec->format = fourcc_format;

  return TRUE;
}

static GstCaps *
gst_vdpaudecoder_src_getcaps (GstPad * pad)
{
  GstVdpauDecoder *dec;

  dec = GST_VDPAU_DECODER (GST_OBJECT_PARENT (pad));

  if (GST_PAD_CAPS (dec->src))
    return gst_caps_ref (GST_PAD_CAPS (dec->src));

  if (dec->src_caps)
    return gst_caps_ref (dec->src_caps);

  return gst_caps_copy (gst_pad_get_pad_template_caps (dec->src));
}

/* GObject vmethod implementations */

static void
gst_vdpaudecoder_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauDecoder",
      "Codec/Decoder/Video",
      "VDPAU decoder base class",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

/* initialize the vdpaudecoder's class */
static void
gst_vdpaudecoder_class_init (GstVdpauDecoderClass * klass)
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

  gstelement_class->change_state = gst_vdpaudecoder_change_state;
}

static void
gst_vdpaudecoder_init (GstVdpauDecoder * dec, GstVdpauDecoderClass * klass)
{
  dec->display = NULL;
  dec->device = 0;
  dec->silent = FALSE;
  dec->src_caps = NULL;

  dec->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_getcaps_function (dec->src, gst_vdpaudecoder_src_getcaps);
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);

  dec->sink = gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink"), "sink");
  gst_pad_set_setcaps_function (dec->sink, gst_vdpaudecoder_sink_set_caps);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sink);
}

static void
gst_vdpaudecoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpauDecoder *dec = GST_VDPAU_DECODER (object);

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
  GstVdpauDecoder *dec = GST_VDPAU_DECODER (object);

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
