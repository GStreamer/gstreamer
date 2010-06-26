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

struct _GstVdpVideoSrcPad
{
  GstPad pad;

  GstCaps *caps;
  GstVdpDevice *device;

  gboolean yuv_output;
  gint width, height;
  guint32 fourcc;

  /* properties */
  gchar *display;
};

struct _GstVdpVideoSrcPadClass
{
  GstPadClass pad_class;
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
      GST_ERROR_OBJECT (vdp_pad,
          "Sink element allocated buffer with different caps");
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      gst_buffer_unref (out_buf);
      return GST_FLOW_ERROR;
    }

    if (!gst_vdp_video_buffer_download (video_buf, out_buf, vdp_pad->fourcc,
            vdp_pad->width, vdp_pad->height)) {
      GST_ERROR_OBJECT (vdp_pad,
          "Couldn't convert from GstVdpVideoBuffer to the requested format");
      gst_buffer_unref (GST_BUFFER_CAST (video_buf));
      gst_buffer_unref (out_buf);
      return GST_FLOW_ERROR;
    }

    gst_buffer_copy_metadata (out_buf, (const GstBuffer *) video_buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
    gst_buffer_unref (GST_BUFFER_CAST (video_buf));
  } else
    out_buf = GST_BUFFER_CAST (video_buf);

  /* FIXME: can't use gst_buffer_set_caps since we may have additional
   * references to the bufffer. We can't either use
   * gst_buffer_make_metadata_writable since that creates a regular buffer and
   * not a GstVdpVideoBuffer */
  gst_caps_replace (&(GST_BUFFER_CAPS (out_buf)), GST_PAD_CAPS (vdp_pad));

  return gst_pad_push (pad, out_buf);
}

static void
gst_vdp_video_src_pad_update_caps (GstVdpVideoSrcPad * vdp_pad)
{
  GstCaps *caps;
  const GstCaps *templ_caps;

  if (vdp_pad->caps)
    gst_caps_unref (vdp_pad->caps);

  caps = gst_vdp_video_buffer_get_allowed_caps (vdp_pad->device);

  if ((templ_caps = gst_pad_get_pad_template_caps (GST_PAD (vdp_pad)))) {
    vdp_pad->caps = gst_caps_intersect (caps, templ_caps);
    gst_caps_unref (caps);
  } else
    vdp_pad->caps = caps;
}

static gboolean
gst_vdp_video_src_pad_open_device (GstVdpVideoSrcPad * vdp_pad, GError ** error)
{
  GstVdpDevice *device;

  vdp_pad->device = device = gst_vdp_get_device (vdp_pad->display);
  if (G_UNLIKELY (!vdp_pad->device))
    goto device_error;

  return TRUE;

device_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Couldn't create GstVdpDevice");
  return FALSE;
}

static GstFlowReturn
gst_vdp_video_src_pad_alloc_with_caps (GstVdpVideoSrcPad * vdp_pad,
    GstCaps * caps, GstVdpVideoBuffer ** video_buf, GError ** error)
{
  GstFlowReturn ret;

  ret = gst_pad_alloc_buffer ((GstPad *) vdp_pad, 0, 0, caps,
      (GstBuffer **) video_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!gst_caps_is_equal_fixed (caps, GST_BUFFER_CAPS (*video_buf)))
    goto wrong_caps;

  if (!GST_IS_VDP_VIDEO_BUFFER (*video_buf))
    goto invalid_buf;

  return GST_FLOW_OK;

wrong_caps:
  gst_buffer_unref (GST_BUFFER (*video_buf));
  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element returned buffer with wrong caps");
  return GST_FLOW_ERROR;

invalid_buf:
  gst_buffer_unref (GST_BUFFER (*video_buf));
  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element returned buffer of wrong type");
  return GST_FLOW_ERROR;
}

GstFlowReturn
gst_vdp_video_src_pad_alloc_buffer (GstVdpVideoSrcPad * vdp_pad,
    GstVdpVideoBuffer ** video_buf, GError ** error)
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
      if (!gst_vdp_video_src_pad_open_device (vdp_pad, error))
        return GST_FLOW_ERROR;

      gst_vdp_video_src_pad_update_caps (vdp_pad);
    }
    device = vdp_pad->device;

    *video_buf = gst_vdp_video_buffer_new (device, VDP_CHROMA_TYPE_420,
        vdp_pad->width, vdp_pad->height);
    if (!*video_buf)
      goto video_buf_error;
  } else {
    ret = gst_vdp_video_src_pad_alloc_with_caps (vdp_pad, caps, video_buf,
        error);
    if (ret != GST_FLOW_OK)
      return ret;

    if (G_UNLIKELY (!vdp_pad->device)) {
      vdp_pad->device =
          g_object_ref (GST_VDP_VIDEO_BUFFER (*video_buf)->device);

      gst_vdp_video_src_pad_update_caps (vdp_pad);
    }
  }

  return GST_FLOW_OK;

video_buf_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
      "Couldn't create a GstVdpVideoBuffer");
  return GST_FLOW_ERROR;
}

static gboolean
gst_vdp_video_src_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpVideoSrcPad *vdp_pad = GST_VDP_VIDEO_SRC_PAD (pad);
  const GstStructure *structure;

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

  return TRUE;
}

GstFlowReturn
gst_vdp_video_src_pad_get_device (GstVdpVideoSrcPad * vdp_pad,
    GstVdpDevice ** device, GError ** error)
{
  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), FALSE);

  if (!GST_PAD_CAPS (vdp_pad))
    return GST_FLOW_NOT_NEGOTIATED;

  if (G_UNLIKELY (!vdp_pad->device)) {

    if (vdp_pad->yuv_output) {
      if (!gst_vdp_video_src_pad_open_device (vdp_pad, error))
        return GST_FLOW_ERROR;
    }

    else {
      GstFlowReturn ret;
      GstVdpVideoBuffer *buf;

      ret = gst_vdp_video_src_pad_alloc_with_caps (vdp_pad,
          GST_PAD_CAPS (vdp_pad), &buf, error);
      if (ret != GST_FLOW_OK)
        return ret;

      vdp_pad->device = g_object_ref (buf->device);
      gst_buffer_unref (GST_BUFFER (buf));
    }

    gst_vdp_video_src_pad_update_caps (vdp_pad);
  }

  *device = vdp_pad->device;
  return GST_FLOW_OK;
}

static GstCaps *
gst_vdp_video_src_pad_getcaps (GstPad * pad)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) pad;

  const GstCaps *templ_caps;

  if (vdp_pad->caps)
    return gst_caps_copy (vdp_pad->caps);

  else if ((templ_caps = gst_pad_get_pad_template_caps (pad)))
    return gst_caps_copy (templ_caps);

  return NULL;
}

static gboolean
gst_vdp_video_src_pad_activate_push (GstPad * pad, gboolean active)
{
  GstVdpVideoSrcPad *vdp_pad = GST_VDP_VIDEO_SRC_PAD (pad);

  if (!active) {
    if (vdp_pad->device)
      g_object_unref (vdp_pad->device);
    vdp_pad->device = NULL;

    if (vdp_pad->caps)
      gst_caps_unref (vdp_pad->caps);
    vdp_pad->caps = NULL;
  }

  return TRUE;
}

GstVdpVideoSrcPad *
gst_vdp_video_src_pad_new (GstPadTemplate * templ, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);
  g_return_val_if_fail ((templ->direction == GST_PAD_SRC), NULL);

  return g_object_new (GST_TYPE_VDP_VIDEO_SRC_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
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
  vdp_pad->caps = NULL;

  vdp_pad->display = NULL;

  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_video_src_pad_getcaps));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_video_src_pad_setcaps));
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
