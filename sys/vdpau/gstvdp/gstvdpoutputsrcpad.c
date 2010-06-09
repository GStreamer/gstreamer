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

#include "gstvdputils.h"
#include "gstvdpvideobuffer.h"

#include "gstvdpoutputsrcpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_output_src_pad_debug);
#define GST_CAT_DEFAULT gst_vdp_output_src_pad_debug

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_TEMPL_CAPS
};

typedef enum _GstVdpOutputSrcPadFormat GstVdpOutputSrcPadFormat;

enum _GstVdpOutputSrcPadFormat
{
  GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB,
  GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU
};

struct _GstVdpOutputSrcPad
{
  GstPad pad;

  GstCaps *caps;
  GstVdpDevice *device;

  GstCaps *input_caps;
  GstVdpOutputSrcPadFormat output_format;
  VdpRGBAFormat rgba_format;
  gint width, height;

  /* properties */
  gchar *display;
  GstCaps *templ_caps;
};

struct _GstVdpOutputSrcPadClass
{
  GstPadClass pad_class;
};

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_output_src_pad_debug, "vdpoutputsrcpad", 0, "GstVdpOutputSrcPad");

G_DEFINE_TYPE_WITH_CODE (GstVdpOutputSrcPad, gst_vdp_output_src_pad,
    GST_TYPE_PAD, DEBUG_INIT ());

GstFlowReturn
gst_vdp_output_src_pad_push (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer * output_buf, GError ** error)
{
  GstPad *pad;
  GstBuffer *outbuf;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VDP_OUTPUT_BUFFER (output_buf), GST_FLOW_ERROR);

  pad = (GstPad *) vdp_pad;

  if (G_UNLIKELY (!GST_PAD_CAPS (pad)))
    return GST_FLOW_NOT_NEGOTIATED;

  switch (vdp_pad->output_format) {
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB:
    {
      guint size;

      gst_vdp_output_buffer_calculate_size (output_buf, &size);

      /* FIXME: we don't do pad_alloc here since we really want a buffer of
       * the specified size */
      outbuf = gst_buffer_new_and_alloc (size);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (vdp_pad));

      if (!gst_vdp_output_buffer_download (output_buf, outbuf, error)) {
        gst_buffer_unref (GST_BUFFER_CAST (output_buf));
        gst_buffer_unref (outbuf);
        return GST_FLOW_ERROR;
      }

      gst_buffer_copy_metadata (outbuf, (const GstBuffer *) output_buf,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
      gst_buffer_unref (GST_BUFFER_CAST (output_buf));
      break;
    }
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU:
    {
      outbuf = GST_BUFFER_CAST (output_buf);
      break;
    }

    default:
      g_assert_not_reached ();
      break;
  }

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (vdp_pad));

  return gst_pad_push (pad, outbuf);
}

static void
gst_vdp_output_src_pad_update_caps (GstVdpOutputSrcPad * vdp_pad)
{
  GstCaps *allowed_caps;

  allowed_caps = gst_vdp_output_buffer_get_allowed_caps (vdp_pad->device);

  if (vdp_pad->caps)
    gst_caps_unref (vdp_pad->caps);

  if (vdp_pad->templ_caps) {
    vdp_pad->caps = gst_caps_intersect (allowed_caps, vdp_pad->templ_caps);
    gst_caps_unref (allowed_caps);
  } else
    vdp_pad->caps = allowed_caps;

  GST_DEBUG_OBJECT (vdp_pad, "allowed caps: %" GST_PTR_FORMAT, vdp_pad->caps);
}

static GstFlowReturn
gst_vdp_output_src_pad_create_buffer (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer ** output_buf, GError ** error)
{
  GstFlowReturn ret;
  GstBuffer *neg_buf;
  GstStructure *structure;

  /* negotiate */
  ret = gst_pad_alloc_buffer_and_set_caps (GST_PAD_CAST (vdp_pad),
      GST_BUFFER_OFFSET_NONE, 0, GST_PAD_CAPS (vdp_pad), &neg_buf);

  if (ret == GST_FLOW_OK) {
    gint new_width, new_height;

    structure = gst_caps_get_structure (GST_BUFFER_CAPS (neg_buf), 0);
    if (!gst_structure_get_int (structure, "width", &new_width) ||
        !gst_structure_get_int (structure, "height", &new_height))
      goto invalid_caps;

    if (new_width != vdp_pad->width || new_height != vdp_pad->height) {
      GST_DEBUG_OBJECT (vdp_pad, "new dimensions: %dx%d", new_width,
          new_height);

      vdp_pad->width = new_width;
      vdp_pad->height = new_height;

      gst_caps_set_simple (vdp_pad->input_caps,
          "width", G_TYPE_INT, new_width,
          "height", G_TYPE_INT, new_height, NULL);
    }

    gst_buffer_unref (neg_buf);
  }

  *output_buf = gst_vdp_output_buffer_new (vdp_pad->device,
      vdp_pad->rgba_format, vdp_pad->width, vdp_pad->height);
  if (!*output_buf)
    goto output_buf_error;

  gst_buffer_set_caps (GST_BUFFER_CAST (*output_buf), vdp_pad->input_caps);

  return GST_FLOW_OK;

invalid_caps:
  gst_buffer_unref (neg_buf);

  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element allocated buffer with invalid caps");
  return GST_FLOW_ERROR;

output_buf_error:
  gst_buffer_unref (neg_buf);
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
      "Couldn't create a GstVdpOutputBuffer");
  return GST_FLOW_ERROR;

}

static gboolean
gst_vdp_output_src_pad_open_device (GstVdpOutputSrcPad * vdp_pad,
    GError ** error)
{
  GstVdpDevice *device;

  vdp_pad->device = device = gst_vdp_get_device (vdp_pad->display);
  if (G_UNLIKELY (!vdp_pad->device))
    goto device_error;

  gst_vdp_output_src_pad_update_caps (vdp_pad);

  return TRUE;

device_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Couldn't create GstVdpDevice");
  return FALSE;
}

GstFlowReturn
gst_vdp_output_src_pad_alloc_buffer (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer ** output_buf, GError ** error)
{
  GstCaps *caps;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), GST_FLOW_ERROR);

  caps = GST_PAD_CAPS (vdp_pad);
  if (!caps)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (vdp_pad->output_format) {
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB:
    {
      if (G_UNLIKELY (!vdp_pad->device)) {
        if (!gst_vdp_output_src_pad_open_device (vdp_pad, error))
          return GST_FLOW_ERROR;
      }

      ret = gst_vdp_output_src_pad_create_buffer (vdp_pad, output_buf, error);
      if (ret != GST_FLOW_OK)
        return ret;

      break;
    }

    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU:
    {
      GstStructure *structure;

      ret = gst_pad_alloc_buffer ((GstPad *) vdp_pad, 0, 0, caps,
          (GstBuffer **) output_buf);
      if (ret != GST_FLOW_OK)
        return ret;

      structure = gst_caps_get_structure (GST_BUFFER_CAPS (*output_buf), 0);
      if (!gst_structure_has_name (structure, "video/x-vdpau-output"))
        goto wrong_caps;

      if (G_UNLIKELY (!vdp_pad->device)) {
        vdp_pad->device =
            g_object_ref (GST_VDP_VIDEO_BUFFER (*output_buf)->device);

        gst_vdp_output_src_pad_update_caps (vdp_pad);
      }

      break;
    }

    default:
      g_assert_not_reached ();
      break;
  }

  return GST_FLOW_OK;

wrong_caps:
  gst_buffer_unref (GST_BUFFER_CAST (*output_buf));

  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element returned buffer with wrong caps");
  return GST_FLOW_ERROR;
}

gboolean
gst_vdp_output_src_pad_negotiate_output (GstVdpOutputSrcPad * vdp_pad,
    GstCaps * video_caps)
{
  GstCaps *allowed_caps, *output_caps, *src_caps;
  const GstStructure *structure;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (video_caps), FALSE);

  allowed_caps = gst_pad_get_allowed_caps (GST_PAD_CAST (vdp_pad));
  if (G_UNLIKELY (!allowed_caps))
    goto allowed_caps_error;
  if (G_UNLIKELY (gst_caps_is_empty (allowed_caps))) {
    gst_caps_unref (allowed_caps);
    goto allowed_caps_error;
  }
  GST_DEBUG ("allowed_caps: %" GST_PTR_FORMAT, allowed_caps);

  output_caps = gst_vdp_video_to_output_caps (video_caps);
  src_caps = gst_caps_intersect (output_caps, allowed_caps);
  gst_caps_unref (output_caps);
  gst_caps_unref (allowed_caps);

  if (gst_caps_is_empty (src_caps))
    goto not_negotiated;

  gst_pad_fixate_caps (GST_PAD_CAST (vdp_pad), src_caps);

  GST_DEBUG ("src_caps: %" GST_PTR_FORMAT, src_caps);

  structure = gst_caps_get_structure (src_caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    if (!gst_vdp_caps_to_rgba_format (src_caps, &vdp_pad->rgba_format))
      return FALSE;

    vdp_pad->output_format = GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB;
  } else if (gst_structure_has_name (structure, "video/x-vdpau-output")) {
    if (!gst_structure_get_int (structure, "rgba-format",
            (gint *) & vdp_pad->rgba_format))
      return FALSE;

    vdp_pad->output_format = GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU;
  } else
    return FALSE;

  if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
    return FALSE;

  if (gst_pad_set_caps (GST_PAD (vdp_pad), src_caps)) {
    vdp_pad->input_caps = gst_caps_copy (video_caps);
    return TRUE;
  }
  return FALSE;

allowed_caps_error:
  GST_ERROR_OBJECT (vdp_pad, "Got invalid allowed caps");
  return FALSE;

not_negotiated:
  gst_caps_unref (src_caps);
  GST_ERROR_OBJECT (vdp_pad, "Couldn't find suitable output format");
  return FALSE;
}

GstFlowReturn
gst_vdp_output_src_pad_get_device (GstVdpOutputSrcPad * vdp_pad,
    GstVdpDevice ** device, GError ** error)
{
  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), FALSE);

  if (G_UNLIKELY (!vdp_pad->device)) {
    GstCaps *src_caps;
    GstStructure *structure;

    src_caps = gst_pad_get_allowed_caps (GST_PAD (vdp_pad));
    gst_pad_fixate_caps (GST_PAD (vdp_pad), src_caps);

    if (gst_caps_is_empty (src_caps)) {
      gst_caps_unref (src_caps);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    structure = gst_caps_get_structure (src_caps, 0);
    if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
      if (!gst_vdp_output_src_pad_open_device (vdp_pad, error)) {
        gst_caps_unref (src_caps);
        return GST_FLOW_ERROR;
      }
    }

    else {
      GstFlowReturn ret;
      GstBuffer *buf;

      ret = gst_pad_alloc_buffer (GST_PAD (vdp_pad), 0, 0, src_caps, &buf);
      if (ret != GST_FLOW_OK) {
        gst_caps_unref (src_caps);
        goto alloc_failed;
      }

      if (!gst_caps_is_equal_fixed (src_caps, GST_BUFFER_CAPS (buf))) {
        gst_caps_unref (src_caps);
        gst_buffer_unref (buf);
        goto wrong_caps;
      }

      vdp_pad->device = g_object_ref (GST_VDP_OUTPUT_BUFFER (buf)->device);
    }

    gst_caps_unref (src_caps);
  }

  *device = vdp_pad->device;
  return GST_FLOW_OK;

alloc_failed:
  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Couldn't allocate buffer");
  return GST_FLOW_ERROR;

wrong_caps:
  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element returned buffer with wrong caps");
  return GST_FLOW_ERROR;
}

static GstCaps *
gst_vdp_output_src_pad_getcaps (GstPad * pad)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) pad;

  if (vdp_pad->caps)
    return gst_caps_ref (vdp_pad->caps);

  return gst_caps_ref (vdp_pad->templ_caps);
}

static gboolean
gst_vdp_output_src_pad_activate_push (GstPad * pad, gboolean active)
{
  GstVdpOutputSrcPad *vdp_pad = GST_VDP_OUTPUT_SRC_PAD (pad);

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

GstVdpOutputSrcPad *
gst_vdp_output_src_pad_new (GstCaps * templ_caps)
{
  return g_object_new (GST_TYPE_VDP_OUTPUT_SRC_PAD, "template-caps", templ_caps,
      "direction", GST_PAD_SRC, NULL);
}

static void
gst_vdp_output_src_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, vdp_pad->display);
      break;
    case PROP_TEMPL_CAPS:
      gst_value_set_caps (value, vdp_pad->templ_caps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_output_src_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      vdp_pad->display = g_value_dup_string (value);
      break;
    case PROP_TEMPL_CAPS:
      if (vdp_pad->templ_caps)
        gst_caps_unref (vdp_pad->templ_caps);
      vdp_pad->templ_caps = gst_caps_copy (gst_value_get_caps (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_output_src_pad_finalize (GObject * object)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) object;

  if (vdp_pad->templ_caps)
    gst_caps_unref (vdp_pad->templ_caps);

  g_free (vdp_pad->display);

  G_OBJECT_CLASS (gst_vdp_output_src_pad_parent_class)->finalize (object);
}

static void
gst_vdp_output_src_pad_init (GstVdpOutputSrcPad * vdp_pad)
{
  GstPad *pad = GST_PAD (vdp_pad);

  vdp_pad->device = NULL;
  vdp_pad->caps = NULL;

  vdp_pad->display = NULL;
  vdp_pad->templ_caps = NULL;

  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_getcaps));
  gst_pad_set_activatepush_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_activate_push));
}

static void
gst_vdp_output_src_pad_class_init (GstVdpOutputSrcPadClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_vdp_output_src_pad_get_property;
  object_class->set_property = gst_vdp_output_src_pad_set_property;
  object_class->finalize = gst_vdp_output_src_pad_finalize;

  g_object_class_install_property (object_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_TEMPL_CAPS,
      g_param_spec_boxed ("template-caps", "Template caps",
          "Template caps", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

}
