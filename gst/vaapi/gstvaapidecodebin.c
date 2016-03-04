/*
 *  gstvaapidecodebin.c
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *    Author: Victor Jaquez <victorx.jaquez@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-vaapidecodebin
 * @short_description: A VA-API based video decoder with a
 * post-processor
 *
 * vaapidecodebin is similar #GstVaapiDecode, but it is composed by
 * the vaapidecode, a #GstQueue, and the #GstVaapiPostproc, if it is
 * available and functional in the setup.
 *
 * It offers the functionality of #GstVaapiDecode and the many options
 * of #GstVaapiPostproc.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=~/big_buck_bunny.mov ! qtdemux ! h264parse ! vaapidecodebin ! vaapisink
 * ]|
 * </refsect2>
 */

#include "gstcompat.h"
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "gstvaapipluginutil.h"
#include "gstvaapidecodebin.h"
#include "gstvaapivideocontext.h"
#include "gstvaapipluginbase.h"

#define GST_PLUGIN_NAME "vaapidecodebin"
#define GST_PLUGIN_DESC "A VA-API based bin with a decoder and a postprocessor"

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapi_decode_bin);
#define GST_CAT_DEFAULT gst_debug_vaapi_decode_bin

#define DEFAULT_QUEUE_MAX_SIZE_BUFFERS 0
#define DEFAULT_QUEUE_MAX_SIZE_BYTES   0
#define DEFAULT_QUEUE_MAX_SIZE_TIME    0
#define DEFAULT_DEINTERLACE_METHOD     GST_VAAPI_DEINTERLACE_METHOD_BOB

enum
{
  PROP_0,
  PROP_MAX_SIZE_BUFFERS,
  PROP_MAX_SIZE_BYTES,
  PROP_MAX_SIZE_TIME,
  PROP_DEINTERLACE_METHOD,
  PROP_DISABLE_VPP,
  PROP_LAST
};

enum
{
  HAS_VPP_UNKNOWN,
  HAS_VPP_NO,
  HAS_VPP_YES
};

static GParamSpec *properties[PROP_LAST];

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "
/* *INDENT-OFF* */
static const char gst_vaapi_decode_bin_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=2, systemstream=(boolean)false")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
#if USE_HEVC_DECODER
    GST_CAPS_CODEC("video/x-h265")
#endif
    GST_CAPS_CODEC("video/x-wmv")
#if USE_VP8_DECODER
    GST_CAPS_CODEC("video/x-vp8")
#endif
#if USE_VP9_DECODER
    GST_CAPS_CODEC("video/x-vp9")
#endif
    ;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapi_decode_bin_src_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ", "
  GST_CAPS_INTERLACED_FALSE "; "
#if (USE_GLX || USE_EGL)
  GST_VAAPI_MAKE_GLTEXUPLOAD_CAPS ", "
  GST_CAPS_INTERLACED_FALSE "; "
#endif
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_FALSE;
/* *INDENT-ON* */

static GstStaticPadTemplate gst_vaapi_decode_bin_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapi_decode_bin_sink_caps_str));

static GstStaticPadTemplate gst_vaapi_decode_bin_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapi_decode_bin_src_caps_str));

G_DEFINE_TYPE (GstVaapiDecodeBin, gst_vaapi_decode_bin, GST_TYPE_BIN);

static void
post_missing_element_message (GstVaapiDecodeBin * vaapidecbin,
    const gchar * missing_factory)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (vaapidecbin),
      missing_factory);
  gst_element_post_message (GST_ELEMENT_CAST (vaapidecbin), msg);

  GST_ELEMENT_WARNING (vaapidecbin, CORE, MISSING_PLUGIN,
      ("Missing element '%s' - check your GStreamer installation.",
          missing_factory), ("video decoding might fail"));
}

static void
gst_vaapi_decode_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiDecodeBin *vaapidecbin = GST_VAAPI_DECODE_BIN (object);

  switch (prop_id) {
    case PROP_MAX_SIZE_BYTES:
      vaapidecbin->max_size_bytes = g_value_get_uint (value);
      g_object_set (G_OBJECT (vaapidecbin->queue), "max-size-bytes",
          vaapidecbin->max_size_bytes, NULL);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      vaapidecbin->max_size_buffers = g_value_get_uint (value);
      g_object_set (G_OBJECT (vaapidecbin->queue), "max-size-buffers",
          vaapidecbin->max_size_buffers, NULL);
      break;
    case PROP_MAX_SIZE_TIME:
      vaapidecbin->max_size_time = g_value_get_uint64 (value);
      g_object_set (G_OBJECT (vaapidecbin->queue), "max-size-time",
          vaapidecbin->max_size_time, NULL);
      break;
    case PROP_DEINTERLACE_METHOD:
      vaapidecbin->deinterlace_method = g_value_get_enum (value);
      if (vaapidecbin->postproc)
        g_object_set (G_OBJECT (vaapidecbin->postproc), "deinterlace-method",
            vaapidecbin->deinterlace_method, NULL);
      break;
    case PROP_DISABLE_VPP:
    {
      gboolean disable_vpp;

      disable_vpp = g_value_get_boolean (value);
      if (!disable_vpp && !vaapidecbin->has_vpp)
        GST_WARNING_OBJECT (vaapidecbin,
            "Cannot enable VPP since the VA driver does not support it");
      else
        vaapidecbin->disable_vpp = disable_vpp;

      /* @TODO: Add run-time disabling support */
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_decode_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiDecodeBin *vaapidecbin = GST_VAAPI_DECODE_BIN (object);

  switch (prop_id) {
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint (value, vaapidecbin->max_size_bytes);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, vaapidecbin->max_size_buffers);
      break;
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, vaapidecbin->max_size_time);
      break;
    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, vaapidecbin->deinterlace_method);
      break;
    case PROP_DISABLE_VPP:
      g_value_set_boolean (value, vaapidecbin->disable_vpp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_decode_bin_class_init (GstVaapiDecodeBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_vaapi_decode_bin_set_property;
  gobject_class->get_property = gst_vaapi_decode_bin_get_property;

  gst_element_class_set_static_metadata (element_class,
      "VA-API Decode Bin",
      "Codec/Decoder/Video",
      GST_PLUGIN_DESC,
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>, "
      "Victor Jaquez <victorx.jaquez@intel.com>");

  properties[PROP_MAX_SIZE_BYTES] = g_param_spec_uint ("max-size-bytes",
      "Max. size (kB)", "Max. amount of data in the queue (bytes, 0=disable)",
      0, G_MAXUINT, DEFAULT_QUEUE_MAX_SIZE_BYTES,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MAX_SIZE_BUFFERS] = g_param_spec_uint ("max-size-buffers",
      "Max. size (buffers)", "Max. number of buffers in the queue (0=disable)",
      0, G_MAXUINT, DEFAULT_QUEUE_MAX_SIZE_BUFFERS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MAX_SIZE_TIME] = g_param_spec_uint64 ("max-size-time",
      "Max. size (ns)", "Max. amount of data in the queue (in ns, 0=disable)",
      0, G_MAXUINT64, DEFAULT_QUEUE_MAX_SIZE_TIME,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_DEINTERLACE_METHOD] = g_param_spec_enum ("deinterlace-method",
      "Deinterlace method", "Deinterlace method to use",
      GST_VAAPI_TYPE_DEINTERLACE_METHOD, DEFAULT_DEINTERLACE_METHOD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_DISABLE_VPP] = g_param_spec_boolean ("disable-vpp",
      "Disable VPP",
      "Disable Video Post Processing (No support for run time disabling)",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapi_decode_bin_sink_factory);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapi_decode_bin_src_factory);

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_decode_bin,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);
}

static gboolean
gst_vaapi_decode_bin_configure (GstVaapiDecodeBin * vaapidecbin)
{
  const gchar *missing_factory = NULL;
  GstPad *pad, *ghostpad;

  /* create the decoder */
  vaapidecbin->decoder =
      g_object_new (g_type_from_name ("GstVaapiDecode"), NULL);

  /* create the queue */
  vaapidecbin->queue = gst_element_factory_make ("queue", NULL);
  if (!vaapidecbin->queue) {
    missing_factory = "queue";
    goto error_element_missing;
  }

  g_object_set (G_OBJECT (vaapidecbin->queue),
      "max-size-bytes", vaapidecbin->max_size_bytes,
      "max-size-buffers", vaapidecbin->max_size_buffers,
      "max-size-time", vaapidecbin->max_size_time, NULL);

  /* create the postproc */
  vaapidecbin->postproc = gst_element_factory_make ("vaapipostproc", NULL);
  if (!vaapidecbin->postproc) {
    missing_factory = "vaapipostproc";
    goto error_element_missing;
  }

  gst_bin_add_many (GST_BIN (vaapidecbin), vaapidecbin->decoder,
      vaapidecbin->queue, vaapidecbin->postproc, NULL);

  if (!gst_element_link_many (vaapidecbin->decoder, vaapidecbin->queue,
          vaapidecbin->postproc, NULL))
    goto error_link_pad;

  /* create ghost pad sink */
  pad = gst_element_get_static_pad (GST_ELEMENT (vaapidecbin->decoder), "sink");
  ghostpad = gst_ghost_pad_new_from_template ("sink", pad,
      GST_PAD_PAD_TEMPLATE (pad));
  gst_object_unref (pad);
  if (!gst_element_add_pad (GST_ELEMENT (vaapidecbin), ghostpad))
    goto error_adding_pad;

  /* create ghost pad src */
  pad = gst_element_get_static_pad (GST_ELEMENT (vaapidecbin->postproc), "src");
  ghostpad = gst_ghost_pad_new_from_template ("src", pad,
      GST_PAD_PAD_TEMPLATE (pad));
  gst_object_unref (pad);
  if (!gst_element_add_pad (GST_ELEMENT (vaapidecbin), ghostpad))
    goto error_adding_pad;

  return TRUE;

error_element_missing:
  {
    post_missing_element_message (vaapidecbin, missing_factory);
    return FALSE;
  }
error_link_pad:
  {
    GST_ELEMENT_ERROR (vaapidecbin, CORE, PAD, (NULL),
        ("Failed to configure the vaapidecodebin."));
    return FALSE;
  }
error_adding_pad:
  {
    GST_ELEMENT_ERROR (vaapidecbin, CORE, PAD, (NULL),
        ("Failed to adding pads."));
    return FALSE;
  }
}

static void
gst_vaapi_decode_bin_init (GstVaapiDecodeBin * vaapidecbin)
{
  vaapidecbin->has_vpp = HAS_VPP_UNKNOWN;
  vaapidecbin->deinterlace_method = DEFAULT_DEINTERLACE_METHOD;

  gst_vaapi_decode_bin_configure (vaapidecbin);
}
