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
#include "gstvdpvideobufferpool.h"
#include "gstvdputils.h"

#include "gstvdpvideosrcpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_video_src_pad_debug);
#define GST_CAT_DEFAULT gst_vdp_video_src_pad_debug

enum
{
  PROP_0,
  PROP_DEVICE
};

struct _GstVdpVideoSrcPad
{
  GstPad pad;

  GstVdpBufferPool *bpool;
  GstCaps *caps;

  gboolean yuv_output;
  gint width, height;
  guint32 fourcc;

  /* properties */
  GstVdpDevice *device;
};

struct _GstVdpVideoSrcPadClass
{
  GstPadClass pad_class;
};

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_video_src_pad_debug, "vdpvideosrcpad", 0, "GstVdpVideoSrcPad");

G_DEFINE_TYPE_WITH_CODE (GstVdpVideoSrcPad, gst_vdp_video_src_pad, GST_TYPE_PAD,
    DEBUG_INIT ());

GstVdpVideoSrcPad *
gst_vdp_video_src_pad_new (GstPadTemplate * templ, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);
  g_return_val_if_fail ((templ->direction == GST_PAD_SRC), NULL);

  return g_object_new (GST_TYPE_VDP_VIDEO_SRC_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
}

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

GstFlowReturn
gst_vdp_video_src_pad_alloc_buffer (GstVdpVideoSrcPad * vdp_pad,
    GstVdpVideoBuffer ** video_buf, GError ** error)
{
  GstCaps *caps;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_SRC_PAD (vdp_pad), GST_FLOW_ERROR);

  caps = GST_PAD_CAPS (vdp_pad);
  if (!caps)
    return GST_FLOW_NOT_NEGOTIATED;

  *video_buf =
      (GstVdpVideoBuffer *) gst_vdp_buffer_pool_get_buffer (vdp_pad->bpool,
      error);
  if (!*video_buf)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static gboolean
gst_vdp_video_src_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpVideoSrcPad *vdp_pad = GST_VDP_VIDEO_SRC_PAD (pad);
  const GstStructure *structure;

  GstCaps *video_caps;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
      return FALSE;
    if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
      return FALSE;
    if (!gst_structure_get_fourcc (structure, "format", &vdp_pad->fourcc))
      return FALSE;

    video_caps = gst_vdp_yuv_to_video_caps (caps);
    vdp_pad->yuv_output = TRUE;
  } else if (gst_structure_has_name (structure, "video/x-vdpau-video")) {
    if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
      return FALSE;
    if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
      return FALSE;

    video_caps = gst_caps_ref (caps);
    vdp_pad->yuv_output = FALSE;
  } else
    return FALSE;

  gst_vdp_buffer_pool_set_caps (vdp_pad->bpool, video_caps);
  gst_caps_unref (video_caps);

  return TRUE;
}

static GstCaps *
gst_vdp_video_src_pad_getcaps (GstPad * pad)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) pad;

  const GstCaps *templ_caps;

  if (vdp_pad->caps)
    return gst_caps_ref (vdp_pad->caps);

  else if ((templ_caps = gst_pad_get_pad_template_caps (pad)))
    return gst_caps_copy (templ_caps);

  return NULL;
}

static gboolean
gst_vdp_video_src_pad_activate_push (GstPad * pad, gboolean active)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) pad;

  if (!active) {
    if (vdp_pad->caps)
      gst_caps_unref (vdp_pad->caps);
    vdp_pad->caps = NULL;

    if (vdp_pad->device)
      gst_object_unref (vdp_pad->device);
    vdp_pad->device = NULL;
  }

  return TRUE;
}

static void
gst_vdp_video_src_pad_set_device (GstVdpVideoSrcPad * vdp_pad,
    GstVdpDevice * device)
{
  GstCaps *caps;
  const GstCaps *templ_caps;

  if (vdp_pad->bpool)
    g_object_unref (vdp_pad->bpool);
  if (vdp_pad->device)
    g_object_unref (vdp_pad->device);

  vdp_pad->device = device;
  vdp_pad->bpool = gst_vdp_video_buffer_pool_new (device);

  /* update caps */
  if (vdp_pad->caps)
    gst_caps_unref (vdp_pad->caps);

  caps = gst_vdp_video_buffer_get_allowed_caps (device);

  if ((templ_caps = gst_pad_get_pad_template_caps (GST_PAD (vdp_pad)))) {
    vdp_pad->caps = gst_caps_intersect (caps, templ_caps);
    gst_caps_unref (caps);
  } else
    vdp_pad->caps = caps;
}

static void
gst_vdp_video_src_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpVideoSrcPad *vdp_pad = (GstVdpVideoSrcPad *) object;

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, vdp_pad->device);
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
    case PROP_DEVICE:
      gst_vdp_video_src_pad_set_device (vdp_pad, g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_video_src_pad_init (GstVdpVideoSrcPad * vdp_pad)
{
  GstPad *pad = GST_PAD (vdp_pad);

  vdp_pad->device = NULL;
  vdp_pad->caps = NULL;

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

  /**
   * GstVdpVideoSrcPad:device:
   *
   * The #GstVdpDevice this pool is bound to.
   */
  g_object_class_install_property
      (object_class,
      PROP_DEVICE,
      g_param_spec_object ("device",
          "Device",
          "The GstVdpDevice the pad should use",
          GST_TYPE_VDP_DEVICE, G_PARAM_READWRITE));
}
