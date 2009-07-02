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

/**
 * SECTION:element-vdpauvideopostprocess
 *
 * FIXME:Describe vdpaumpegdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpauvideopostprocess ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

/*
 * TODO:
 *  + add support for postprocessing eg. deinterlace
 *  + mixing videos. (This should perhaps be done in a separate element based on
 *  VdpOutputSurface)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>

#include "gstvdputils.h"
#include "gstvdpvideobuffer.h"
#include "gstvdpoutputbuffer.h"

#include "gstvdpvideopostprocess.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_vpp_debug);
#define GST_CAT_DEFAULT gst_vdp_vpp_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDP_VIDEO_CAPS));
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDP_OUTPUT_CAPS));

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_vpp_debug, "vdpauvideopostprocess", 0, "VDPAU video surface to output surface");

GST_BOILERPLATE_FULL (GstVdpVideoPostProcess, gst_vdp_vpp,
    GstElement, GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdp_vpp_finalize (GObject * object);

static gboolean
gst_vdp_vpp_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstCaps *output_caps, *allowed_caps, *src_caps;
  gboolean res;

  output_caps = gst_vdp_video_to_output_caps (caps);
  allowed_caps = gst_pad_get_allowed_caps (vpp->srcpad);

  src_caps = gst_caps_intersect (output_caps, allowed_caps);
  gst_caps_truncate (src_caps);
  GST_DEBUG ("output_caps: %" GST_PTR_FORMAT " allowed_caps: %" GST_PTR_FORMAT
      " src_caps: %" GST_PTR_FORMAT, output_caps, allowed_caps, src_caps);

  gst_caps_unref (output_caps);
  gst_caps_unref (allowed_caps);

  res = gst_pad_set_caps (vpp->srcpad, src_caps);

  gst_object_unref (vpp);
  return res;
}

static void
gst_vdp_vpp_flush (GstVdpVideoPostProcess * vpp)
{
  /* TODO: Write this */
}

static void
gst_vdp_vpp_start (GstVdpVideoPostProcess * vpp)
{
  vpp->mixer = VDP_INVALID_HANDLE;
  vpp->device = NULL;
}

static void
gst_vdp_vpp_stop (GstVdpVideoPostProcess * vpp)
{
  if (vpp->mixer != VDP_INVALID_HANDLE)
    vpp->device->vdp_video_mixer_destroy (vpp->mixer);
  if (!vpp->device)
    g_object_unref (vpp->device);
}

static GstFlowReturn
gst_vdp_vpp_alloc_output_buffer (GstVdpVideoPostProcess * vpp, GstCaps * caps,
    GstVdpOutputBuffer ** outbuf)
{
  GstFlowReturn ret;

  ret = gst_pad_alloc_buffer_and_set_caps (vpp->srcpad, 0, 0,
      caps, (GstBuffer **) outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!vpp->device) {
#define VDP_NUM_MIXER_PARAMETER 3
#define MAX_NUM_FEATURES 5

    GstStructure *structure;
    gint chroma_type;
    gint width, height;

    VdpStatus status;
    GstVdpDevice *device;

    VdpVideoMixerFeature features[5];
    guint n_features = 0;
    VdpVideoMixerParameter parameters[VDP_NUM_MIXER_PARAMETER] = {
      VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
      VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
      VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE
    };
    const void *parameter_values[VDP_NUM_MIXER_PARAMETER];

    structure = gst_caps_get_structure (GST_PAD_CAPS (vpp->sinkpad), 0);
    if (!gst_structure_get_int (structure, "chroma-type", &chroma_type) ||
        !gst_structure_get_int (structure, "width", &width) ||
        !gst_structure_get_int (structure, "height", &height))
      goto error;

    parameter_values[0] = &width;
    parameter_values[1] = &height;
    parameter_values[2] = &chroma_type;

    device = vpp->device = g_object_ref ((*outbuf)->device);


    status =
        device->vdp_video_mixer_create (device->device, n_features, features,
        VDP_NUM_MIXER_PARAMETER, parameters, parameter_values, &vpp->mixer);
    if (status != VDP_STATUS_OK) {
      GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
          ("Could not create vdpau video mixer"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));
      goto error;
    }
  }

  return ret;

error:
  gst_buffer_unref (GST_BUFFER (*outbuf));
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_vdp_vpp_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstFlowReturn ret;
  GstVdpOutputBuffer *outbuf;

  GstStructure *structure;
  GstVideoRectangle src_r, dest_r;
  VdpRect rect;

  GstVdpDevice *device;
  VdpStatus status;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (vpp, "Received discont buffer");
    gst_vdp_vpp_flush (vpp);
  }

  ret =
      gst_vdp_vpp_alloc_output_buffer (vpp, GST_PAD_CAPS (vpp->srcpad),
      &outbuf);
  if (ret != GST_FLOW_OK)
    goto done;

  structure = gst_caps_get_structure (GST_BUFFER_CAPS (buffer), 0);
  if (!gst_structure_get_int (structure, "width", &src_r.w) ||
      !gst_structure_get_int (structure, "height", &src_r.h))
    goto invalid_caps;

  structure = gst_caps_get_structure (GST_BUFFER_CAPS (outbuf), 0);
  if (!gst_structure_get_int (structure, "width", &dest_r.w) ||
      !gst_structure_get_int (structure, "height", &dest_r.h))
    goto invalid_caps;

  if (vpp->force_aspect_ratio) {
    GstVideoRectangle res_r;

    gst_video_sink_center_rect (src_r, dest_r, &res_r, TRUE);
    rect.x0 = res_r.x;
    rect.x1 = res_r.w + res_r.x;
    rect.y0 = res_r.y;
    rect.y1 = res_r.h + res_r.y;
  } else {
    rect.x0 = 0;
    rect.x1 = dest_r.w;
    rect.y0 = 0;
    rect.y1 = dest_r.h;
  }

  device = vpp->device;
  status = device->vdp_video_mixer_render (vpp->mixer, VDP_INVALID_HANDLE, NULL,
      VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME, 0, NULL,
      GST_VDP_VIDEO_BUFFER (buffer)->surface, 0, NULL, NULL, outbuf->surface,
      NULL, &rect, 0, NULL);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
        ("Could not post process frame"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  gst_buffer_copy_metadata (GST_BUFFER (outbuf), buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

  ret = gst_pad_push (vpp->srcpad, GST_BUFFER (outbuf));

done:
  gst_buffer_unref (buffer);
  gst_object_unref (vpp);

  return ret;

invalid_caps:
  gst_buffer_unref (GST_BUFFER (outbuf));
  ret = GST_FLOW_ERROR;
  goto done;
}

static GstCaps *
gst_vdp_vpp_sink_getcaps (GstPad * pad)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstCaps *caps;

  if (vpp->device)
    caps = gst_vdp_video_buffer_get_allowed_video_caps (vpp->device);
  else
    caps = gst_static_pad_template_get_caps (&sink_template);

  gst_object_unref (vpp);

  return caps;
}

static GstFlowReturn
gst_vdp_vpp_sink_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstVdpOutputBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstVdpDevice *device = NULL;
  GstStructure *structure;
  gint width, height;
  gint chroma_type;

  if (!vpp->device) {
    /* if we haven't got a device yet we must alloc a buffer downstream to get it */
    GstCaps *src_caps = gst_pad_get_allowed_caps (vpp->srcpad);
    gst_pad_fixate_caps (vpp->srcpad, src_caps);
    ret = gst_pad_alloc_buffer (vpp->srcpad, 0, 0, src_caps,
        (GstBuffer **) & outbuf);

    gst_caps_unref (src_caps);
    if (ret != GST_FLOW_OK)
      goto error;

    device = outbuf->device;
    gst_buffer_unref (GST_BUFFER (outbuf));
  } else
    device = vpp->device;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height) ||
      !gst_structure_get_int (structure, "chroma-type", &chroma_type))
    goto error;

  *buf = GST_BUFFER (gst_vdp_video_buffer_new (device,
          chroma_type, width, height));

  if (*buf == NULL)
    goto error;

  GST_BUFFER_SIZE (*buf) = size;
  GST_BUFFER_OFFSET (*buf) = offset;

  gst_buffer_set_caps (*buf, caps);

  ret = GST_FLOW_OK;

error:

  gst_object_unref (vpp);
  return ret;
}

static gboolean
gst_vdp_vpp_sink_event (GstPad * pad, GstEvent * event)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (vpp, "flush stop");

      gst_vdp_vpp_flush (vpp);
      res = gst_pad_push_event (vpp->srcpad, event);

      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
  }

  gst_object_unref (vpp);

  return res;
}

static GstStateChangeReturn
gst_vdp_vpp_change_state (GstElement * element, GstStateChange transition)
{
  GstVdpVideoPostProcess *vpp;
  GstStateChangeReturn ret;

  vpp = GST_VDP_VIDEO_POST_PROCESS (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_vdp_vpp_start (vpp);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vdp_vpp_stop (vpp);
      break;
    default:
      break;
  }

  return ret;
}

/* GObject vmethod implementations */
static void
gst_vdp_vpp_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstVdpVideoPostProcess *vpp = GST_VDP_VIDEO_POST_PROCESS (object);

  switch (property_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, vpp->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* GObject vmethod implementations */
static void
gst_vdp_vpp_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpVideoPostProcess *vpp = GST_VDP_VIDEO_POST_PROCESS (object);

  switch (property_id) {
    case PROP_FORCE_ASPECT_RATIO:
      vpp->force_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* GType vmethod implementations */

static void
gst_vdp_vpp_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VDPAU Mpeg Decoder",
      "Filter/Converter/Decoder/Video",
      "Post process GstVdpVideoBuffers and output GstVdpOutputBuffers",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_vpp_class_init (GstVdpVideoPostProcessClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_vdp_vpp_get_property;
  gobject_class->set_property = gst_vdp_vpp_set_property;
  gobject_class->finalize = gst_vdp_vpp_finalize;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, the plugin will only scale up the input surface to the"
          "maximum size where the aspect ratio can be preserved", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_vdp_vpp_change_state;
}

static void
gst_vdp_vpp_init (GstVdpVideoPostProcess * vpp,
    GstVdpVideoPostProcessClass * gclass)
{
  vpp->force_aspect_ratio = FALSE;

  /* SRC PAD */
  vpp->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (vpp), vpp->srcpad);

  /* SINK PAD */
  vpp->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vpp), vpp->sinkpad);

  gst_pad_set_getcaps_function (vpp->sinkpad, gst_vdp_vpp_sink_getcaps);
  gst_pad_set_setcaps_function (vpp->sinkpad, gst_vdp_vpp_sink_setcaps);
  gst_pad_set_chain_function (vpp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vdp_vpp_chain));
  gst_pad_set_event_function (vpp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vdp_vpp_sink_event));
  gst_pad_set_bufferalloc_function (vpp->sinkpad, gst_vdp_vpp_sink_bufferalloc);
}

static void
gst_vdp_vpp_finalize (GObject * object)
{
}
