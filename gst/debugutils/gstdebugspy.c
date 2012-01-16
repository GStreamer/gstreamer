/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2011 Igalia S.L.
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
 * SECTION:element-debugspy
 *
 * A spy element that can provide information on buffers going through it, with
 * bus messages.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -m videotestsrc ! debugspy ! fakesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstdebugspy.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_spy_debug);
#define GST_CAT_DEFAULT gst_debug_spy_debug

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
  PROP_CHECKSUM_TYPE
};

/* create a GType for GChecksumType */
#define GST_DEBUG_SPY_CHECKSUM_TYPE (gst_debug_spy_checksum_get_type())
static GType
gst_debug_spy_checksum_get_type (void)
{
  static GType checksum_type = 0;

  static const GEnumValue checksum_values[] = {
    {G_CHECKSUM_MD5, "Use the MD5 hashing algorithm", "md5"},
    {G_CHECKSUM_SHA1, "Use the SHA-1 hashing algorithm", "sha1"},
    {G_CHECKSUM_SHA256, "Use the SHA-256 hashing algorithm", "sha256"},
    {0, NULL, NULL}
  };

  if (!checksum_type)
    checksum_type = g_enum_register_static ("GChecksumType", checksum_values);

  return checksum_type;
}

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

GST_BOILERPLATE (GstDebugSpy, gst_debug_spy, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_debug_spy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_debug_spy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_debug_spy_transform_ip (GstBaseTransform * transform,
    GstBuffer * buf);

/* GObject vmethod implementations */

static void
gst_debug_spy_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_debug_spy_debug, "debugspy", 0, "debugspy");

  gst_element_class_set_details_simple (element_class,
      "DebugSpy",
      "Filter/Analyzer/Debug",
      "DebugSpy provides information on buffers with bus messages",
      "Guillaume Emont <gemont@igalia.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* initialize the debugspy's class */
static void
gst_debug_spy_class_init (GstDebugSpyClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = (GObjectClass *) klass;
  base_transform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_debug_spy_set_property;
  gobject_class->get_property = gst_debug_spy_get_property;

  base_transform_class->passthrough_on_same_caps = TRUE;
  base_transform_class->transform_ip = gst_debug_spy_transform_ip;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CHECKSUM_TYPE,
      g_param_spec_enum ("checksum-type", "Checksum TYpe",
          "Checksum algorithm to use", GST_DEBUG_SPY_CHECKSUM_TYPE,
          G_CHECKSUM_SHA1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_debug_spy_init (GstDebugSpy * debugspy, GstDebugSpyClass * gclass)
{
  debugspy->silent = FALSE;
  debugspy->checksum_type = G_CHECKSUM_SHA1;
}

static void
gst_debug_spy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDebugSpy *debugspy = GST_DEBUGSPY (object);

  switch (prop_id) {
    case PROP_SILENT:
      debugspy->silent = g_value_get_boolean (value);
      break;
    case PROP_CHECKSUM_TYPE:
      debugspy->checksum_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_debug_spy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDebugSpy *debugspy = GST_DEBUGSPY (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, debugspy->silent);
      break;
    case PROP_CHECKSUM_TYPE:
      g_value_set_enum (value, debugspy->checksum_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_debug_spy_transform_ip (GstBaseTransform * transform, GstBuffer * buf)
{
  GstDebugSpy *debugspy = GST_DEBUGSPY (transform);

  if (debugspy->silent == FALSE) {
    gchar *checksum;
    GstMessage *message;
    GstStructure *message_structure;

    checksum = g_compute_checksum_for_data (debugspy->checksum_type,
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    message_structure = gst_structure_new ("buffer",
        "checksum", G_TYPE_STRING, checksum,
        "timestamp", GST_TYPE_CLOCK_TIME, GST_BUFFER_TIMESTAMP (buf),
        "duration", GST_TYPE_CLOCK_TIME, GST_BUFFER_DURATION (buf),
        "offset", G_TYPE_UINT64, GST_BUFFER_OFFSET (buf),
        "offset_end", G_TYPE_UINT64, GST_BUFFER_OFFSET_END (buf),
        "size", G_TYPE_UINT, GST_BUFFER_SIZE (buf),
        "caps", GST_TYPE_CAPS, GST_BUFFER_CAPS (buf), NULL);

    g_free (checksum);

    message =
        gst_message_new_element (GST_OBJECT (transform), message_structure);

    gst_element_post_message (GST_ELEMENT (transform), message);

  }

  return GST_FLOW_OK;
}
