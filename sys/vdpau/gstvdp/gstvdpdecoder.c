/* GStreamer
*
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
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

#include "gstvdpvideosrcpad.h"

#include "gstvdpdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_decoder_debug

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_decoder_debug, "vdpdecoder", 0, \
    "VDPAU decoder base class");

GST_BOILERPLATE_FULL (GstVdpDecoder, gst_vdp_decoder, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

enum
{
  PROP_0,
  PROP_DISPLAY
};

static GstFlowReturn
gst_vdp_decoder_shape_output (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf)
{
  GstVdpVideoSrcPad *vdp_pad;

  vdp_pad =
      (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder);

  return gst_vdp_video_src_pad_push (vdp_pad, GST_VDP_VIDEO_BUFFER (buf));
}

static GstPad *
gst_vdp_decoder_create_srcpad (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * base_video_decoder_class)
{
  GstPadTemplate *pad_template;
  GstVdpVideoSrcPad *vdp_pad;

  pad_template = gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (base_video_decoder_class),
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  vdp_pad = gst_vdp_video_src_pad_new (pad_template,
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  return GST_PAD (vdp_pad);
}

void
gst_vdp_decoder_post_error (GstVdpDecoder * decoder, GError * error)
{
  GstMessage *message;

  g_return_if_fail (GST_IS_VDP_DECODER (decoder));
  g_return_if_fail (decoder != NULL);

  message = gst_message_new_error (GST_OBJECT (decoder), error, NULL);
  gst_element_post_message (GST_ELEMENT (decoder), message);
  g_error_free (error);
}

static GstFlowReturn
gst_vdp_decoder_alloc_buffer (GstVdpDecoder * vdp_decoder,
    GstVdpVideoBuffer ** video_buf)
{
  GstVdpVideoSrcPad *vdp_pad;

  GstFlowReturn ret;
  GError *err = NULL;

  vdp_pad = (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (vdp_decoder);

  ret = gst_vdp_video_src_pad_alloc_buffer (vdp_pad, video_buf, &err);
  if (ret == GST_FLOW_ERROR)
    gst_vdp_decoder_post_error (vdp_decoder, err);

  return ret;
}

GstFlowReturn
gst_vdp_decoder_render (GstVdpDecoder * vdp_decoder, VdpPictureInfo * info,
    guint n_bufs, VdpBitstreamBuffer * bufs, GstVdpVideoBuffer ** video_buf)
{
  GstFlowReturn ret;

  GstVdpDevice *device;
  VdpVideoSurface surface;
  VdpStatus status;

  ret = gst_vdp_decoder_alloc_buffer (vdp_decoder, video_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  device = (*video_buf)->device;
  surface = (*video_buf)->surface;

  status = device->vdp_decoder_render (vdp_decoder->decoder, surface,
      info, n_bufs, bufs);
  if (status != VDP_STATUS_OK)
    goto decode_error;

  return GST_FLOW_OK;

decode_error:
  GST_ELEMENT_ERROR (vdp_decoder, RESOURCE, READ,
      ("Could not decode"),
      ("Error returned from vdpau was: %s",
          device->vdp_get_error_string (status)));

  gst_buffer_unref (GST_BUFFER_CAST (*video_buf));

  return GST_FLOW_ERROR;
}

GstFlowReturn
gst_vdp_decoder_init_decoder (GstVdpDecoder * vdp_decoder,
    VdpDecoderProfile profile, guint32 max_references)
{
  GstVdpDevice *device;

  VdpStatus status;
  GstVideoState state;

  device = vdp_decoder->device;

  if (vdp_decoder->decoder != VDP_INVALID_HANDLE) {
    status = device->vdp_decoder_destroy (vdp_decoder->decoder);
    if (status != VDP_STATUS_OK)
      goto destroy_decoder_error;
  }

  if (!gst_base_video_decoder_set_src_caps (GST_BASE_VIDEO_DECODER
          (vdp_decoder)))
    return GST_FLOW_NOT_NEGOTIATED;

  state =
      gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER (vdp_decoder));

  status = device->vdp_decoder_create (device->device, profile,
      state.width, state.height, max_references, &vdp_decoder->decoder);
  if (status != VDP_STATUS_OK)
    goto create_decoder_error;

  return GST_FLOW_OK;

destroy_decoder_error:
  GST_ELEMENT_ERROR (vdp_decoder, RESOURCE, READ,
      ("Could not destroy vdpau decoder"),
      ("Error returned from vdpau was: %s",
          device->vdp_get_error_string (status)));

  return GST_FLOW_ERROR;

create_decoder_error:
  GST_ELEMENT_ERROR (vdp_decoder, RESOURCE, READ,
      ("Could not create vdpau decoder"),
      ("Error returned from vdpau was: %s",
          device->vdp_get_error_string (status)));

  return GST_FLOW_ERROR;
}

static gboolean
gst_vdp_decoder_start (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (base_video_decoder);

  GError *err;
  GstVdpVideoSrcPad *vdp_pad;

  err = NULL;
  vdp_decoder->device = gst_vdp_get_device (vdp_decoder->display, &err);
  if (G_UNLIKELY (!vdp_decoder->device))
    goto device_error;

  vdp_pad =
      (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder);
  g_object_set (G_OBJECT (vdp_pad), "device", vdp_decoder->device, NULL);

  vdp_decoder->decoder = VDP_INVALID_HANDLE;

  return TRUE;

device_error:
  gst_vdp_decoder_post_error (vdp_decoder, err);
  return FALSE;
}

static gboolean
gst_vdp_decoder_stop (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (base_video_decoder);

  if (vdp_decoder->decoder != VDP_INVALID_HANDLE) {
    GstVdpDevice *device = vdp_decoder->device;
    VdpStatus status;

    status = device->vdp_decoder_destroy (vdp_decoder->decoder);
    if (status != VDP_STATUS_OK) {
      GST_ELEMENT_ERROR (vdp_decoder, RESOURCE, READ,
          ("Could not destroy vdpau decoder"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));
      return FALSE;
    }
  }

  g_object_unref (vdp_decoder->device);

  return TRUE;
}

static void
gst_vdp_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, vdp_decoder->display);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (vdp_decoder->display);
      vdp_decoder->display = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_decoder_finalize (GObject * object)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (object);

  g_free (vdp_decoder->display);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vdp_decoder_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GstCaps *src_caps;
  GstPadTemplate *src_template;

  src_caps = gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);
  src_template = gst_pad_template_new (GST_BASE_VIDEO_DECODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);

  gst_element_class_add_pad_template (element_class, src_template);
  gst_object_unref (src_template);
}

static void
gst_vdp_decoder_init (GstVdpDecoder * vdp_decoder, GstVdpDecoderClass * klass)
{
  vdp_decoder->display = NULL;
}

static void
gst_vdp_decoder_class_init (GstVdpDecoderClass * klass)
{
  GObjectClass *object_class;
  GstBaseVideoDecoderClass *base_video_decoder_class;

  object_class = G_OBJECT_CLASS (klass);
  base_video_decoder_class = GST_BASE_VIDEO_DECODER_CLASS (klass);

  object_class->get_property = gst_vdp_decoder_get_property;
  object_class->set_property = gst_vdp_decoder_set_property;
  object_class->finalize = gst_vdp_decoder_finalize;

  base_video_decoder_class->start = gst_vdp_decoder_start;
  base_video_decoder_class->stop = gst_vdp_decoder_stop;

  base_video_decoder_class->create_srcpad = gst_vdp_decoder_create_srcpad;
  base_video_decoder_class->shape_output = gst_vdp_decoder_shape_output;

  g_object_class_install_property (object_class,
      PROP_DISPLAY, g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}
