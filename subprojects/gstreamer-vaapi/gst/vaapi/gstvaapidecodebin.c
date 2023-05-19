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
 * vaapidecodebin is similar vaapi{CODEC}dec, but it is composed by
 * the unregistered vaapidecode, a #GstQueue, and the
 * #GstVaapiPostproc, if it is available and functional in the setup.
 *
 * It offers the functionality of GstVaapiDecoder and the many options
 * of #GstVaapiPostproc.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/big_buck_bunny.mov ! qtdemux ! h264parse ! vaapidecodebin ! vaapisink
 * ]|
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
#include "gstvaapi.h"

#define GST_PLUGIN_NAME "vaapidecodebin"
#define GST_PLUGIN_DESC "A VA-API based bin with a decoder and a postprocessor"

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapi_decode_bin);
#define GST_CAT_DEFAULT gst_debug_vaapi_decode_bin

#define DEFAULT_QUEUE_MAX_SIZE_BUFFERS 1
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
    GST_CAPS_CODEC("video/x-h265")
    GST_CAPS_CODEC("video/x-wmv")
    GST_CAPS_CODEC("video/x-vp8")
    GST_CAPS_CODEC("video/x-vp9")
    ;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapi_decode_bin_src_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ", "
  GST_CAPS_INTERLACED_FALSE "; "
#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
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

extern gboolean _gst_vaapi_has_video_processing;

static gboolean gst_vaapi_decode_bin_configure (GstVaapiDecodeBin * self);

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
      /* @TODO: Add run-time disabling support */
      vaapidecbin->disable_vpp = g_value_get_boolean (value);
      break;
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

static GstStateChangeReturn
gst_vaapi_decode_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVaapiDecodeBin *vaapidecbin = GST_VAAPI_DECODE_BIN (element);
  GstStateChangeReturn ret;

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_vaapi_decode_bin_parent_class)->change_state
      (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_vaapi_decode_bin_configure (vaapidecbin))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
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

  element_class->change_state = gst_vaapi_decode_bin_change_state;
  gst_element_class_set_static_metadata (element_class,
      "VA-API Decode Bin",
      "Codec/Decoder/Video/Hardware",
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
  GstElement *capsfilter;
  GstCaps *caps;
  GstPad *queue_srcpad, *bin_srcpad, *capsfilter_sinkpad, *vpp_srcpad;
  gboolean res;
  gboolean has_vpp;

  g_object_set (G_OBJECT (vaapidecbin->queue),
      "max-size-bytes", vaapidecbin->max_size_bytes,
      "max-size-buffers", vaapidecbin->max_size_buffers,
      "max-size-time", vaapidecbin->max_size_time, NULL);

  if (vaapidecbin->disable_vpp || vaapidecbin->configured)
    return TRUE;

  has_vpp = _gst_vaapi_has_video_processing;

  if (!has_vpp && (vaapidecbin->deinterlace_method ==
          GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE
          || vaapidecbin->deinterlace_method ==
          GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED)) {
    GST_ERROR_OBJECT (vaapidecbin,
        "Don't have VPP support but advanced deinterlacing selected");
    return FALSE;
  }

  if (!has_vpp)
    return TRUE;

  GST_INFO_OBJECT (vaapidecbin, "enabling VPP");

  /* capsfilter to force memory:VASurface */
  caps = gst_caps_from_string ("video/x-raw(memory:VASurface)");
  if (!caps)
    goto error_cannot_set_caps;
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* create the postproc */
  vaapidecbin->postproc = gst_element_factory_make ("vaapipostproc", NULL);
  if (!vaapidecbin->postproc)
    goto error_vpp_missing;
  g_object_set (G_OBJECT (vaapidecbin->postproc), "deinterlace-method",
      vaapidecbin->deinterlace_method, NULL);

  gst_bin_add_many (GST_BIN (vaapidecbin), capsfilter, vaapidecbin->postproc,
      NULL);

  if (!gst_element_link (capsfilter, vaapidecbin->postproc))
    goto error_sync_state;
  if (!gst_element_sync_state_with_parent (capsfilter))
    goto error_sync_state;
  if (!gst_element_sync_state_with_parent (vaapidecbin->postproc))
    goto error_sync_state;

  /* break source ghost pad target */
  bin_srcpad =
      gst_element_get_static_pad (GST_ELEMENT_CAST (vaapidecbin), "src");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (bin_srcpad), NULL))
    goto error_link_pad;

  /* link decoder and queue */
  queue_srcpad = gst_element_get_static_pad (vaapidecbin->queue, "src");
  capsfilter_sinkpad = gst_element_get_static_pad (capsfilter, "sink");
  res = (gst_pad_link (queue_srcpad, capsfilter_sinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (capsfilter_sinkpad);
  gst_object_unref (queue_srcpad);
  if (!res)
    goto error_link_pad;

  /* set vpp source pad as source ghost pad target */
  vpp_srcpad = gst_element_get_static_pad (vaapidecbin->postproc, "src");
  res = gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (bin_srcpad), vpp_srcpad);
  gst_object_unref (vpp_srcpad);
  if (!res)
    goto error_link_pad;

  gst_object_unref (bin_srcpad);
  vaapidecbin->configured = TRUE;

  return TRUE;

  /* ERRORS */
error_cannot_set_caps:
  {
    GST_ELEMENT_ERROR (vaapidecbin, CORE, PAD,
        ("Failed to configure caps for VA Surfaces."), (NULL));
    return FALSE;
  }
error_vpp_missing:
  {
    GST_ELEMENT_ERROR (vaapidecbin, CORE, PAD,
        ("Failed to load vaapipostproc."), (NULL));
    return FALSE;
  }
error_sync_state:
  {
    GST_ELEMENT_ERROR (vaapidecbin, CORE, STATE_CHANGE,
        ("Failed to sync state of vaapipostproc."), (NULL));
    return FALSE;
  }
error_link_pad:
  {
    gst_object_unref (bin_srcpad);
    GST_ELEMENT_ERROR (vaapidecbin, CORE, PAD,
        ("Failed to configure the vaapidecodebin."), (NULL));
    return FALSE;
  }
}

static void
gst_vaapi_decode_bin_init (GstVaapiDecodeBin * vaapidecbin)
{
  GstPad *pad, *ghostpad;

  vaapidecbin->max_size_bytes = DEFAULT_QUEUE_MAX_SIZE_BYTES;
  vaapidecbin->max_size_buffers = DEFAULT_QUEUE_MAX_SIZE_BUFFERS;
  vaapidecbin->max_size_time = DEFAULT_QUEUE_MAX_SIZE_TIME;
  vaapidecbin->disable_vpp = (g_getenv ("GST_VAAPI_DISABLE_VPP") != NULL);

  /* create the decoder */
  vaapidecbin->decoder =
      g_object_new (g_type_from_name ("GstVaapiDecode"), NULL);
  g_assert (vaapidecbin->decoder);

  /* create the queue */
  vaapidecbin->queue = gst_element_factory_make ("queue", NULL);
  g_assert (vaapidecbin->queue);

  gst_bin_add_many (GST_BIN (vaapidecbin), vaapidecbin->decoder,
      vaapidecbin->queue, NULL);

  if (!gst_element_link (vaapidecbin->decoder, vaapidecbin->queue)) {
    GST_WARNING_OBJECT (vaapidecbin, "Failed to link decoder and queue");
    return;
  }

  /* create ghost pad sink */
  pad = gst_element_get_static_pad (vaapidecbin->decoder, "sink");
  if (!pad) {
    GST_WARNING_OBJECT (vaapidecbin, "Failed to get decoder sink pad");
    return;
  }

  ghostpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  if (!gst_element_add_pad (GST_ELEMENT (vaapidecbin), ghostpad)) {
    GST_WARNING_OBJECT (vaapidecbin, "Failed to add decoder sink pad to bin");
    return;
  }

  /* create ghost pad src */
  pad = gst_element_get_static_pad (GST_ELEMENT (vaapidecbin->queue), "src");
  ghostpad = gst_ghost_pad_new_from_template ("src", pad,
      GST_PAD_PAD_TEMPLATE (pad));
  gst_object_unref (pad);
  if (!gst_element_add_pad (GST_ELEMENT (vaapidecbin), ghostpad))
    GST_WARNING_OBJECT (vaapidecbin, "Failed to add queue source pad to bin");
}
