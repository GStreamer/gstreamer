/*
 * GStreamer
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
#include <gst/video/video.h>

#include "gstvdpauvideobuffer.h"
#include "gstvdpauyuvvideo.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdpau_yuv_video_debug);
#define GST_CAT_DEFAULT gst_vdpau_yuv_video_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDPAU_VIDEO_CAPS));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdpau_yuv_video_debug, "vdpauvideoyuv", 0, "YUV to VDPAU video surface");

GST_BOILERPLATE_FULL (GstVdpauYUVVideo, gst_vdpau_yuv_video, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdpau_yuv_video_finalize (GObject * object);
static void gst_vdpau_yuv_video_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vdpau_yuv_video_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GstFlowReturn
gst_vdpau_yuv_video_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpauYUVVideo *yuv_video;
  GstVdpauDevice *device;
  VdpVideoSurface surface;
  GstBuffer *outbuf = NULL;

  yuv_video = GST_VDPAU_YUV_VIDEO (GST_OBJECT_PARENT (pad));
  device = yuv_video->device;

  outbuf =
      GST_BUFFER (gst_vdpau_video_buffer_new (device, yuv_video->chroma_type,
          yuv_video->width, yuv_video->height));
  surface = GST_VDPAU_VIDEO_BUFFER (outbuf)->surface;

  switch (yuv_video->format) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      VdpStatus status;
      guint8 *data[3];
      guint32 stride[3];

      data[0] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          0, yuv_video->width, yuv_video->height);
      data[1] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          2, yuv_video->width, yuv_video->height);
      data[2] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          1, yuv_video->width, yuv_video->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          0, yuv_video->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          2, yuv_video->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          1, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't push YV12 data to VDPAU"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        goto error;
      }
      break;
    }
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    {
      VdpStatus status;
      guint8 *data[3];
      guint32 stride[3];

      data[0] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          0, yuv_video->width, yuv_video->height);
      data[1] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          2, yuv_video->width, yuv_video->height);
      data[2] = GST_BUFFER_DATA (buffer) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          1, yuv_video->width, yuv_video->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          0, yuv_video->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          2, yuv_video->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          1, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't push YV12 data to VDPAU"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        goto error;
      }
      break;
    }
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    {
      VdpStatus status;
      guint8 *data[2];
      guint32 stride[2];

      data[0] = GST_BUFFER_DATA (buffer);
      data[1] = GST_BUFFER_DATA (buffer) + yuv_video->width * yuv_video->height;

      stride[0] = yuv_video->width;
      stride[1] = yuv_video->width;

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_NV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        goto error;
      }
      break;
    }
    default:
      break;
  }

  gst_buffer_unref (buffer);

  gst_buffer_copy_metadata (outbuf, buffer, GST_BUFFER_COPY_TIMESTAMPS);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (yuv_video->src));

  return gst_pad_push (yuv_video->src, outbuf);

error:
  gst_buffer_unref (outbuf);
  return GST_FLOW_ERROR;
}

static GstCaps *
gst_vdpau_yuv_video_get_caps (GstVdpauYUVVideo * yuv_video)
{
  GstVdpauDevice *device;
  GstCaps *caps;
  gint i;

  device = yuv_video->device;

  caps = gst_caps_new_empty ();

  for (i = 0; i < N_CHROMA_TYPES; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    status =
        device->vdp_video_surface_query_capabilities (device->device,
        chroma_types[i], &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE) {
      GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
          ("Could not get query VDPAU video surface capabilites"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));

      goto error;
    }
    if (is_supported) {
      gint j;

      for (j = 0; j < N_FORMATS; j++) {
        if (formats[j].chroma_type != chroma_types[i])
          continue;

        status =
            device->vdp_video_surface_query_ycbcr_capabilities (device->device,
            formats[j].chroma_type, formats[j].format, &is_supported);
        if (status != VDP_STATUS_OK
            && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT) {
          GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
              ("Could not query VDPAU YCbCr capabilites"),
              ("Error returned from vdpau was: %s",
                  device->vdp_get_error_string (status)));

          goto error;
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
error:
  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return NULL;
  }

  return caps;
}

static gboolean
gst_vdpau_yuv_video_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpauYUVVideo *yuv_video = GST_VDPAU_YUV_VIDEO (GST_OBJECT_PARENT (pad));

  GstStructure *structure;
  guint32 fourcc;
  gint chroma_type = 0;
  gint width, height;
  gint framerate_numerator, framerate_denominator;
  gint par_numerator, par_denominator;
  gint i;
  GstCaps *src_caps, *new_caps;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_fourcc (structure, "format", &fourcc);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate",
      &framerate_numerator, &framerate_denominator);
  gst_structure_get_fraction (structure, "pixel-aspect-ratio",
      &par_numerator, &par_denominator);

  for (i = 0; i < N_FORMATS; i++) {
    if (formats[i].fourcc == fourcc) {
      chroma_type = formats[i].chroma_type;
      break;
    }
  }

  src_caps = gst_pad_get_allowed_caps (yuv_video->src);
  if (G_UNLIKELY (!src_caps || !gst_caps_get_size (src_caps)))
    return FALSE;

  new_caps = gst_caps_copy_nth (src_caps, 0);
  gst_caps_unref (src_caps);
  if (G_UNLIKELY (!new_caps))
    return FALSE;

  structure = gst_caps_get_structure (new_caps, 0);

  gst_structure_set (structure,
      "device", G_TYPE_OBJECT, yuv_video->device,
      "chroma-type", G_TYPE_INT, chroma_type,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, framerate_numerator,
      framerate_denominator, "pixel-aspect-ratio", GST_TYPE_FRACTION,
      par_numerator, par_denominator, NULL);

  gst_pad_fixate_caps (yuv_video->src, new_caps);
  res = gst_pad_set_caps (yuv_video->src, new_caps);

  gst_caps_unref (new_caps);

  if (G_UNLIKELY (!res))
    return FALSE;

  yuv_video->width = width;
  yuv_video->height = height;
  yuv_video->format = fourcc;
  yuv_video->chroma_type = chroma_type;

  return TRUE;
}

static GstCaps *
gst_vdpau_yuv_video_sink_getcaps (GstPad * pad)
{
  GstVdpauYUVVideo *yuv_video;

  yuv_video = GST_VDPAU_YUV_VIDEO (GST_OBJECT_PARENT (pad));

  if (yuv_video->sink_caps)
    return gst_caps_copy (yuv_video->sink_caps);

  return gst_caps_copy (gst_pad_get_pad_template_caps (yuv_video->sink));
}

static GstStateChangeReturn
gst_vdpau_yuv_video_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVdpauYUVVideo *yuv_video;

  yuv_video = GST_VDPAU_YUV_VIDEO (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      yuv_video->device = gst_vdpau_get_device (yuv_video->display);
      if (!yuv_video->sink_caps)
        yuv_video->sink_caps = gst_vdpau_yuv_video_get_caps (yuv_video);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_object_unref (yuv_video->device);
      yuv_video->device = NULL;
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

/* GObject vmethod implementations */

static void
gst_vdpau_yuv_video_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauYUVVideo",
      "Coyuv_video/Decoder/Video",
      "VDPAU video surface to YUV",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_vdpau_yuv_video_class_init (GstVdpauYUVVideoClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_vdpau_yuv_video_finalize;
  gobject_class->set_property = gst_vdpau_yuv_video_set_property;
  gobject_class->get_property = gst_vdpau_yuv_video_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = gst_vdpau_yuv_video_change_state;
}

static void
gst_vdpau_yuv_video_init (GstVdpauYUVVideo * yuv_video,
    GstVdpauYUVVideoClass * klass)
{
  yuv_video->sink_caps = NULL;

  yuv_video->display = NULL;
  yuv_video->device = NULL;

  yuv_video->height = 0;
  yuv_video->width = 0;
  yuv_video->format = 0;
  yuv_video->chroma_type = 0;

  yuv_video->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (yuv_video), yuv_video->src);

  yuv_video->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_getcaps_function (yuv_video->sink,
      gst_vdpau_yuv_video_sink_getcaps);
  gst_pad_set_setcaps_function (yuv_video->sink,
      gst_vdpau_yuv_video_sink_setcaps);
  gst_pad_set_chain_function (yuv_video->sink, gst_vdpau_yuv_video_chain);
  gst_element_add_pad (GST_ELEMENT (yuv_video), yuv_video->sink);
  gst_pad_set_active (yuv_video->sink, TRUE);
}

static void
gst_vdpau_yuv_video_finalize (GObject * object)
{
  GstVdpauYUVVideo *yuv_video = (GstVdpauYUVVideo *) object;

  g_free (yuv_video->display);
}

static void
gst_vdpau_yuv_video_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpauYUVVideo *yuv_video = GST_VDPAU_YUV_VIDEO (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (yuv_video->display);
      yuv_video->display = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdpau_yuv_video_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpauYUVVideo *yuv_video = GST_VDPAU_YUV_VIDEO (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, yuv_video->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
