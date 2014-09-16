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
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdpdecoder.h"
#include "gstvdpvideomemory.h"
#include "gstvdpvideobufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_decoder_debug

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_decoder_debug, "vdpdecoder", 0, \
    "VDPAU decoder base class");
#define gst_vdp_decoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVdpDecoder, gst_vdp_decoder, GST_TYPE_VIDEO_DECODER,
    DEBUG_INIT);

enum
{
  PROP_0,
  PROP_DISPLAY
};

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


GstFlowReturn
gst_vdp_decoder_render (GstVdpDecoder * vdp_decoder, VdpPictureInfo * info,
    guint n_bufs, VdpBitstreamBuffer * bufs, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret;

  VdpStatus status;

  GstVdpVideoMemory *vmem;
#ifndef GST_DISABLE_GST_DEBUG
  GstClockTime before, after;
#endif

  GST_DEBUG_OBJECT (vdp_decoder, "n_bufs:%d, frame:%d", n_bufs,
      frame->system_frame_number);

  ret =
      gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (vdp_decoder),
      frame);
  if (ret != GST_FLOW_OK)
    goto fail_alloc;

  vmem = (GstVdpVideoMemory *) gst_buffer_get_memory (frame->output_buffer, 0);
  if (!vmem
      || !gst_memory_is_type ((GstMemory *) vmem,
          GST_VDP_VIDEO_MEMORY_ALLOCATOR))
    goto no_mem;

  GST_DEBUG_OBJECT (vdp_decoder, "Calling VdpDecoderRender()");
#ifndef GST_DISABLE_GST_DEBUG
  before = gst_util_get_timestamp ();
#endif
  status =
      vdp_decoder->device->vdp_decoder_render (vdp_decoder->decoder,
      vmem->surface, info, n_bufs, bufs);
#ifndef GST_DISABLE_GST_DEBUG
  after = gst_util_get_timestamp ();
#endif
  if (status != VDP_STATUS_OK)
    goto decode_error;

  GST_DEBUG_OBJECT (vdp_decoder, "VdpDecoderRender() took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (after - before));

  return GST_FLOW_OK;

decode_error:
  GST_ELEMENT_ERROR (vdp_decoder, RESOURCE, READ,
      ("Could not decode"),
      ("Error returned from vdpau was: %s",
          vdp_decoder->device->vdp_get_error_string (status)));

  gst_video_decoder_drop_frame (GST_VIDEO_DECODER (vdp_decoder), frame);

  return GST_FLOW_ERROR;

fail_alloc:
  {
    GST_WARNING_OBJECT (vdp_decoder, "Failed to get an output frame");
    return ret;
  }

no_mem:
  {
    GST_ERROR_OBJECT (vdp_decoder, "Didn't get VdpVideoSurface backed buffer");
    return GST_FLOW_ERROR;
  }
}

GstFlowReturn
gst_vdp_decoder_init_decoder (GstVdpDecoder * vdp_decoder,
    VdpDecoderProfile profile, guint32 max_references,
    GstVideoCodecState * output_state)
{
  GstVdpDevice *device;

  VdpStatus status;

  device = vdp_decoder->device;

  if (vdp_decoder->decoder != VDP_INVALID_HANDLE) {
    status = device->vdp_decoder_destroy (vdp_decoder->decoder);
    if (status != VDP_STATUS_OK)
      goto destroy_decoder_error;
  }

  GST_DEBUG_OBJECT (vdp_decoder,
      "device:%u, profile:%d, width:%d, height:%d, max_references:%d",
      device->device, profile, output_state->info.width,
      output_state->info.height, max_references);

  status = device->vdp_decoder_create (device->device, profile,
      output_state->info.width, output_state->info.height, max_references,
      &vdp_decoder->decoder);
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
gst_vdp_decoder_decide_allocation (GstVideoDecoder * video_decoder,
    GstQuery * query)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (video_decoder);
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint size, min = 0, max = 0;
  GstStructure *config;
  GstVideoInfo vinfo;
  gboolean update_pool;

  gst_query_parse_allocation (query, &outcaps, NULL);
  gst_video_info_init (&vinfo);
  gst_video_info_from_caps (&vinfo, outcaps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, vinfo.size);
    update_pool = TRUE;
  } else {
    pool = NULL;
    size = vinfo.size;
    min = max = 0;

    update_pool = FALSE;
  }

  if (pool == NULL
      || !gst_buffer_pool_has_option (pool,
          GST_BUFFER_POOL_OPTION_VDP_VIDEO_META)) {
    if (pool)
      gst_object_unref (pool);
    /* no pool or pool doesn't support GstVdpVideoMeta, we can make our own */
    GST_DEBUG_OBJECT (video_decoder,
        "no pool or doesn't support GstVdpVideoMeta, making new pool");
    pool = gst_vdp_video_buffer_pool_new (vdp_decoder->device);
  }

  /* now configure */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VDP_VIDEO_META);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (pool)
    gst_object_unref (pool);

  return TRUE;

}

static gboolean
gst_vdp_decoder_start (GstVideoDecoder * video_decoder)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (video_decoder);
  GError *err = NULL;

  GST_DEBUG_OBJECT (video_decoder, "Starting");

  vdp_decoder->device = gst_vdp_get_device (vdp_decoder->display, &err);
  if (G_UNLIKELY (!vdp_decoder->device))
    goto device_error;

  vdp_decoder->decoder = VDP_INVALID_HANDLE;

  return TRUE;

device_error:
  gst_vdp_decoder_post_error (vdp_decoder, err);
  return FALSE;
}

static gboolean
gst_vdp_decoder_stop (GstVideoDecoder * video_decoder)
{
  GstVdpDecoder *vdp_decoder = GST_VDP_DECODER (video_decoder);

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
gst_vdp_decoder_init (GstVdpDecoder * vdp_decoder)
{

}

static void
gst_vdp_decoder_class_init (GstVdpDecoderClass * klass)
{
  GObjectClass *object_class;
  GstVideoDecoderClass *video_decoder_class;
  GstElementClass *element_class;

  GstCaps *src_caps;
  GstPadTemplate *src_template;

  object_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  object_class->get_property = gst_vdp_decoder_get_property;
  object_class->set_property = gst_vdp_decoder_set_property;
  object_class->finalize = gst_vdp_decoder_finalize;

  video_decoder_class->start = gst_vdp_decoder_start;
  video_decoder_class->stop = gst_vdp_decoder_stop;
  video_decoder_class->decide_allocation = gst_vdp_decoder_decide_allocation;

  GST_FIXME ("Actually create srcpad template from hw capabilities");
  src_caps =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_MEMORY_VDPAU,
          "{ YV12 }") ";" GST_VIDEO_CAPS_MAKE ("{ YV12 }"));
  src_template =
      gst_pad_template_new (GST_VIDEO_DECODER_SRC_NAME, GST_PAD_SRC,
      GST_PAD_ALWAYS, src_caps);

  gst_element_class_add_pad_template (element_class, src_template);

  g_object_class_install_property (object_class,
      PROP_DISPLAY, g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}
