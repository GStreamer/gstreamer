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

#include "gstvdpvideobuffer.h"

#include "gstvdpvideosrcpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_video_src_pad_debug);
#define GST_CAT_DEFAULT gst_vdp_video_src_pad_debug

enum
{
  PROP_0,
  PROP_DISPLAY
};

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_video_src_pad_debug, "vdpvideosrcpad", 0, "GstVdpVideoSrcPad");

G_DEFINE_TYPE_WITH_CODE (GstVdpVideoSrcPad, gst_vdp_video_src_pad, GST_TYPE_PAD,
    DEBUG_INIT ());

GstFlowReturn
gst_vdp_video_src_pad_push (GstVdpVideoSrcPad * vdp_pad,
    GstVdpVideoBuffer * video_buf)
{
  GstPad *pad;
  GstBuffer *out_buf;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VDP_VIDEO_BUFFER (video_buf), GST_FLOW_ERROR);

  pad = (GstPad *) vdp_pad;

  if (G_UNLIKELY (!GST_PAD_CAPS (pad)))
    return GST_FLOW_NOT_NEGOTIATED;

  if (vdp_pad->yuv_output) {
    guint size;
    GstFlowReturn ret;
    GstCaps *caps;

    if (!gst_vdp_video_buffer_calculate_size (vdp_pad->fourcc, vdp_pad->width,
            vdp_pad->height, &size)) {
      GST_ERROR_OBJECT (vdp_pad, "Couldn't calculate buffer size for caps");
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      return GST_FLOW_ERROR;
    }

    caps = GST_PAD_CAPS (pad);
    ret = gst_pad_alloc_buffer (pad,
        GST_BUFFER_OFFSET_NONE, size, caps, &out_buf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      return ret;
    }

    if (!gst_caps_is_equal_fixed (caps, GST_BUFFER_CAPS (out_buf))) {
      GST_ERROR_OBJECT (vdp_pad, "Couldn't calculate buffer size for caps");
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      return GST_FLOW_ERROR;
    }

    if (!gst_vdp_video_buffer_download (video_buf, out_buf, vdp_pad->fourcc,
            vdp_pad->width, vdp_pad->height)) {
      GST_ERROR_OBJECT (vdp_pad,
          "Couldn't convert from GstVdpVideoBuffer to the requested format");
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      gst_buffer_unref (GST_BUFFER_CAST (out_buf));
    }

    gst_buffer_copy_metadata (out_buf, (const GstBuffer *) video_buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
    gst_buffer_unref (GST_BUFFER_CAST (video_buf));
  } else
    out_buf = GST_BUFFER (video_buf);

  gst_buffer_set_caps (out_buf, GST_PAD_CAPS (vdp_pad));

  return gst_pad_push (pad, out_buf);
}

static void
gst_vdp_video_src_pad_update_caps (GstVdpVideoSrcPad * vdp_pad)
{
  GstCaps *yuv_caps, *video_caps, *caps;

  video_caps = gst_vdp_video_buffer_get_allowed_video_caps (vdp_pad->device);
  yuv_caps = gst_vdp_video_buffer_get_allowed_yuv_caps (vdp_pad->device);
  gst_caps_append (video_caps, yuv_caps);
  caps = gst_caps_intersect (video_caps, vdp_pad->caps);

  gst_caps_unref (video_caps);

  gst_caps_unref (vdp_pad->caps);
  vdp_pad->caps = caps;
}

GstFlowReturn
gst_vdp_video_src_pad_alloc_buffer (GstVdpVideoSrcPad * vdp_pad,
    GstVdpVideoBuffer ** video_buf)
{
  GstCaps *caps;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), GST_FLOW_ERROR);

  caps = GST_PAD_CAPS (vdp_pad);
  if (!caps)
    return GST_FLOW_NOT_NEGOTIATED;

  if (vdp_pad->yuv_output) {
    GstVdpDevice *device;

    if (G_UNLIKELY (!vdp_pad->device)) {
      vdp_pad->device = gst_vdp_get_device (vdp_pad->display);
      if (G_UNLIKELY (!vdp_pad->device))
        goto device_error;

      gst_vdp_video_src_pad_update_caps (vdp_pad);
    }
    device = vdp_pad->device;

    *video_buf = gst_vdp_video_buffer_new (device, VDP_CHROMA_TYPE_420,
        vdp_pad->width, vdp_pad->height);
    if (!*video_buf)
      goto video_buffer_error;
  } else {
    ret = gst_pad_alloc_buffer ((GstPad *) vdp_pad, 0, 0, caps,
        (GstBuffer **) video_buf);
    if (ret != GST_FLOW_OK)
      return ret;

    if (!gst_caps_is_equal_fixed (caps, GST_BUFFER_CAPS (*video_buf)))
      goto wrong_caps;

    if (G_UNLIKELY (!vdp_pad->device)) {
      vdp_pad->device =
          g_object_ref (GST_VDP_VIDEO_BUFFER (*video_buf)->device);

      gst_vdp_video_src_pad_update_caps (vdp_pad);
    }
  }

  return GST_FLOW_OK;

device_error:
  GST_ERROR_OBJECT (vdp_pad, "Couldn't create GstVdpDevice");
  return GST_FLOW_ERROR;

video_buffer_error:
  GST_ERROR_OBJECT (vdp_pad, "Couldn't create GstVdpVideoBuffer");
  return GST_FLOW_ERROR;

wrong_caps:
  GST_ERROR_OBJECT (vdp_pad, "Sink element returned buffer with wrong caps");
  gst_buffer_unref (GST_BUFFER_CAST (*video_buf));
  return GST_FLOW_ERROR;
}

gboolean
gst_vdp_video_src_pad_set_caps (GstVdpVideoSrcPad * vdp_pad, GstCaps * caps)
{
  const GstStructure *structure;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), FALSE);

  if (G_UNLIKELY (!caps))
    return gst_pad_set_caps (GST_PAD (vdp_pad), caps);

  if (G_UNLIKELY (!GST_IS_CAPS (caps) || !gst_caps_is_fixed (caps)))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
      return FALSE;
    if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
      return FALSE;
    if (!gst_structure_get_fourcc (structure, "format", &vdp_pad->fourcc))
      return FALSE;

    vdp_pad->yuv_output = TRUE;
  } else if (gst_structure_has_name (structure, "video/x-vdpau-video")) {
    if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
      return FALSE;
    if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
      return FALSE;

    vdp_pad->yuv_output = FALSE;
  } else
    return FALSE;

  return gst_pad_set_caps (GST_PAD (vdp_pad), caps);
}

GstVdpDevice *
gst_vdp_video_src_pad_get_device (GstVdpVideoSrcPad * vdp_pad)
{
  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), FALSE);

  return vdp_pad->device;
}

GstCaps *
gst_vdp_video_src_pad_get_template_caps ()
{
  return gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);
}

static GstCaps *
gst_vdp_video_src_pad_getcaps (GstPad * pad)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) pad;

  return gst_caps_ref (vdp_pad->caps);
}

static gboolean
gst_vdp_video_src_pad_activate_push (GstPad * pad, gboolean active)
{
  GstVdpVideoSrcPad *vdp_pad = GST_VDP_VIDEO_SRC_PAD (pad);

  if (!active) {
    if (vdp_pad->device)
      g_object_unref (vdp_pad->device);
    vdp_pad->device = NULL;

    gst_caps_unref (vdp_pad->caps);
    vdp_pad->caps = gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);
  }

  return TRUE;
}

GstVdpVideoSrcPad *
gst_vdp_video_src_pad_new ()
{
  return g_object_new (GST_TYPE_VDP_VIDEO_SRC_PAD, "direction", GST_PAD_SRC,
      NULL);
}

static void
gst_vdp_video_src_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, vdp_pad->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_video_src_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      vdp_pad->display = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_video_src_pad_finalize (GObject * object)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) object;

  g_free (vdp_pad->display);

  G_OBJECT_CLASS (gst_vdp_video_src_pad_parent_class)->finalize (object);
}

static void
gst_vdp_video_src_pad_init (GstVdpVideoSrcPad * vdp_pad)
{
  GstPad *pad = GST_PAD (vdp_pad);

  vdp_pad->device = NULL;
  vdp_pad->display = NULL;

  vdp_pad->caps = gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);

  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_video_src_pad_getcaps));
  gst_pad_set_activatepush_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_video_src_pad_activate_push));
}

static void
gst_vdp_video_src_pad_class_init (GstVdpVideoSrcPadClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_vdp_video_src_pad_get_property;
  object_class->set_property = gst_vdp_video_src_pad_set_property;
  object_class->finalize = gst_vdp_video_src_pad_finalize;

  g_object_class_install_property (object_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

}
