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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-vdpauvideopostprocess
 * @title: vdpauvideopostprocess
 *
 * FIXME:Describe vdpaumpegdec here.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v -m fakesrc ! vdpauvideopostprocess ! fakesink silent=TRUE
 * ]|
 *
 */

/*
 * TODO:
 *  + add support for more postprocessing options
 *  + mixing videos. (This should perhaps be done in a separate element based on
 *  VdpOutputSurface)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

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
  PROP_DISPLAY,
  PROP_FORCE_ASPECT_RATIO,
  PROP_DEINTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_NOISE_REDUCTION,
  PROP_SHARPENING,
  PROP_INVERSE_TELECINE
};

G_DEFINE_TYPE (GstVdpVideoPostProcess, gst_vdp_vpp, GST_TYPE_ELEMENT);

static void gst_vdp_vpp_finalize (GObject * object);

#define GST_TYPE_VDP_DEINTERLACE_METHODS (gst_vdp_deinterlace_methods_get_type ())
static GType
gst_vdp_deinterlace_methods_get_type (void)
{
  static GType deinterlace_methods_type = 0;

  static const GEnumValue methods_types[] = {
    {GST_VDP_DEINTERLACE_METHOD_BOB,
          "Vertically scale a single field to the size of a single frame.",
        "bob"},
    {GST_VDP_DEINTERLACE_METHOD_TEMPORAL, "Motion Adaptive: Simple Detection",
        "temporal"},
    {GST_VDP_DEINTERLACE_METHOD_TEMPORAL_SPATIAL,
        "Motion Adaptive: Advanced Detection", "temporal-spatial"},
    {0, NULL, NULL},
  };

  if (!deinterlace_methods_type) {
    deinterlace_methods_type =
        g_enum_register_static ("GstVdpDeinterlaceMethods", methods_types);
  }
  return deinterlace_methods_type;
}

#define GST_TYPE_VDP_DEINTERLACE_MODES (gst_vdp_deinterlace_modes_get_type ())
static GType
gst_vdp_deinterlace_modes_get_type (void)
{
  static GType deinterlace_modes_type = 0;

  static const GEnumValue modes_types[] = {
    {GST_VDP_DEINTERLACE_MODE_AUTO, "Auto detection", "auto"},
    {GST_VDP_DEINTERLACE_MODE_INTERLACED, "Enfore deinterlacing", "interlaced"},
    {GST_VDP_DEINTERLACE_MODE_DISABLED, "Run in passthrough mode", "disabled"},
    {0, NULL, NULL},
  };

  if (!deinterlace_modes_type) {
    deinterlace_modes_type =
        g_enum_register_static ("GstVdpDeinterlaceModes", modes_types);
  }
  return deinterlace_modes_type;
}

static void
gst_vdp_vpp_set_attribute_float (GstVdpVideoPostProcess * vpp,
    VdpVideoMixerAttribute attribute, gfloat value)
{
  VdpVideoMixerAttribute attributes[1];
  const void *attribute_values[1];
  VdpStatus status;

  attributes[0] = attribute;
  attribute_values[0] = &value;

  status =
      vpp->device->vdp_video_mixer_set_attribute_values (vpp->mixer, 1,
      attributes, attribute_values);
  if (status != VDP_STATUS_OK) {
    GST_WARNING_OBJECT (vpp,
        "Couldn't set noise reduction level on mixer, "
        "error returned from vdpau was: %s",
        vpp->device->vdp_get_error_string (status));
  }
}

static void
gst_vdp_vpp_activate_feature (GstVdpVideoPostProcess * vpp,
    VdpVideoMixerFeature feature, gboolean activate)
{
  VdpVideoMixerFeature features[1];
  VdpBool enable[1];
  VdpStatus status;

  features[0] = feature;
  if (activate)
    enable[0] = VDP_TRUE;
  else
    enable[0] = VDP_FALSE;

  status =
      vpp->device->vdp_video_mixer_set_feature_enables (vpp->mixer, 1,
      features, enable);
  if (status != VDP_STATUS_OK) {
    GST_WARNING_OBJECT (vpp, "Couldn't set deinterlace method on mixer, "
        "error returned from vdpau was: %s",
        vpp->device->vdp_get_error_string (status));
  }
}

static VdpVideoMixerFeature
gst_vdp_feature_from_deinterlace_method (GstVdpDeinterlaceMethods method)
{
  gint i;
  VdpVideoMixerFeature feature = 0;

  typedef struct
  {
    GstVdpDeinterlaceMethods method;
    VdpVideoMixerFeature feature;
  } VdpDeinterlaceMethod;

  static const VdpDeinterlaceMethod deinterlace_methods[] = {
    {GST_VDP_DEINTERLACE_METHOD_TEMPORAL,
        VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL},
    {GST_VDP_DEINTERLACE_METHOD_TEMPORAL_SPATIAL,
        VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL}
  };

  for (i = 0; i < G_N_ELEMENTS (deinterlace_methods); i++) {
    if (deinterlace_methods[i].method == method) {
      feature = deinterlace_methods[i].feature;
      break;
    }
  }

  return feature;
}

static void
gst_vdp_vpp_activate_deinterlace_method (GstVdpVideoPostProcess * vpp,
    GstVdpDeinterlaceMethods method, gboolean activate)
{
  gst_vdp_vpp_activate_feature (vpp,
      gst_vdp_feature_from_deinterlace_method (method), activate);
}

static void
gst_vdp_picture_clear (GstVdpPicture pic)
{
  if (pic.buf) {
    gst_buffer_unref (GST_BUFFER (pic.buf));
    pic.buf = NULL;
  }
}

static gboolean
gst_vdp_vpp_is_interlaced (GstVdpVideoPostProcess * vpp)
{
  if (vpp->mode == GST_VDP_DEINTERLACE_MODE_INTERLACED)
    return TRUE;

  if (vpp->mode == GST_VDP_DEINTERLACE_MODE_AUTO && vpp->interlaced)
    return TRUE;

  return FALSE;
}

static guint
gst_vdp_vpp_get_required_pictures (GstVdpVideoPostProcess * vpp)
{
  guint ret;

  if (vpp->noise_reduction != 0.0)
    return 2;

  if (!gst_vdp_vpp_is_interlaced (vpp))
    return 1;

  switch (vpp->method) {
    case GST_VDP_DEINTERLACE_METHOD_BOB:
      ret = 1;
      break;
    case GST_VDP_DEINTERLACE_METHOD_TEMPORAL:
    case GST_VDP_DEINTERLACE_METHOD_TEMPORAL_SPATIAL:
      ret = 2;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return ret;
}

static gboolean
gst_vdp_vpp_get_next_picture (GstVdpVideoPostProcess * vpp,
    GstVdpPicture * current_pic,
    guint32 * video_surfaces_past_count, VdpVideoSurface * video_surfaces_past,
    guint32 * video_surfaces_future_count,
    VdpVideoSurface * video_surfaces_future)
{
  gint i;
  gint required_pictures;

  required_pictures = gst_vdp_vpp_get_required_pictures (vpp);

  if (vpp->n_future_pictures < required_pictures)
    return FALSE;

  *current_pic = vpp->future_pictures[0];
  for (i = 0; i < vpp->n_future_pictures - 1; i++) {
    vpp->future_pictures[i] = vpp->future_pictures[i + 1];
  }

  vpp->future_pictures[vpp->n_future_pictures - 1].buf = NULL;
  vpp->n_future_pictures--;

  *video_surfaces_future_count = vpp->n_future_pictures;
  for (i = 0; i < vpp->n_future_pictures; i++)
    video_surfaces_future[i] = vpp->future_pictures[i].buf->surface;

  *video_surfaces_past_count = vpp->n_past_pictures;
  for (i = 0; i < vpp->n_past_pictures; i++)
    video_surfaces_past[i] = vpp->past_pictures[i].buf->surface;

  if (vpp->n_past_pictures == MAX_PICTURES) {
    gst_vdp_picture_clear (vpp->past_pictures[MAX_PICTURES - 1]);
    vpp->n_past_pictures--;
  }

  /* move every picture upwards one step in the array */
  for (i = vpp->n_past_pictures; i > 0; i--) {
    vpp->past_pictures[i] = vpp->past_pictures[i - 1];
  }

  /* add the picture to the past surfaces */
  vpp->past_pictures[0] = *current_pic;
  vpp->n_past_pictures++;

  return TRUE;
}

static void
gst_vdp_vpp_add_buffer (GstVdpVideoPostProcess * vpp, GstVdpVideoBuffer * buf)
{
  gboolean repeated;
  gboolean tff;
  gboolean onefield;
  GstVdpPicture pic1, pic2;

  if (!gst_vdp_vpp_is_interlaced (vpp)) {
    pic1.buf = buf;
    pic1.structure = VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME;
    pic1.timestamp = GST_BUFFER_TIMESTAMP (buf);
    vpp->future_pictures[vpp->n_future_pictures++] = pic1;
    return;
  }

  repeated = GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_RFF);
  tff = GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_TFF);
  onefield = GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_ONEFIELD);

  pic1.buf = buf;
  pic2.buf = (GstVdpVideoBuffer *) gst_buffer_ref (GST_BUFFER (buf));
  if (tff) {
    pic1.structure = VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD;
    pic2.structure = VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD;
  } else {
    pic1.structure = VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD;
    pic2.structure = VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD;
  }

  pic1.timestamp = GST_BUFFER_TIMESTAMP (buf);
  pic2.timestamp = pic1.timestamp + vpp->field_duration;

  if (repeated) {
    vpp->future_pictures[vpp->n_future_pictures++] = pic1;

    pic1.timestamp = pic2.timestamp;
    vpp->future_pictures[vpp->n_future_pictures++] = pic1;
    gst_buffer_ref (GST_BUFFER (pic1.buf));

    pic2.timestamp += vpp->field_duration;
    vpp->future_pictures[vpp->n_future_pictures++] = pic2;
  } else if (!onefield) {
    vpp->future_pictures[vpp->n_future_pictures++] = pic1;
    vpp->future_pictures[vpp->n_future_pictures++] = pic2;
  } else {
    vpp->future_pictures[vpp->n_future_pictures++] = pic1;
    gst_buffer_unref (GST_BUFFER (pic2.buf));
  }
}

static void
gst_vdp_vpp_post_error (GstVdpVideoPostProcess * vpp, GError * error)
{
  GstMessage *message;

  message = gst_message_new_error (GST_OBJECT (vpp), error, NULL);
  gst_element_post_message (GST_ELEMENT (vpp), message);
  g_error_free (error);
}

static GstFlowReturn
gst_vdp_vpp_create_mixer (GstVdpVideoPostProcess * vpp)
{
#define VDP_NUM_MIXER_PARAMETER 3
#define MAX_NUM_FEATURES 5

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

  parameter_values[0] = &vpp->width;
  parameter_values[1] = &vpp->height;
  parameter_values[2] = &vpp->chroma_type;

  if (gst_vdp_vpp_is_interlaced (vpp)
      && vpp->method != GST_VDP_DEINTERLACE_METHOD_BOB) {
    features[n_features++] =
        gst_vdp_feature_from_deinterlace_method (vpp->method);
  }
  if (vpp->noise_reduction > 0.0)
    features[n_features++] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
  if (vpp->sharpening != 0.0)
    features[n_features++] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
  if (vpp->inverse_telecine)
    features[n_features++] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;

  device = vpp->device;

  status =
      device->vdp_video_mixer_create (device->device, n_features, features,
      VDP_NUM_MIXER_PARAMETER, parameters, parameter_values, &vpp->mixer);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
        ("Could not create vdpau video mixer"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
    return GST_FLOW_ERROR;
  }

  if (vpp->noise_reduction > 0.0) {
    gst_vdp_vpp_set_attribute_float (vpp,
        VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL, vpp->noise_reduction);
  }
  if (vpp->sharpening != 0.0) {
    gst_vdp_vpp_set_attribute_float (vpp,
        VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL, vpp->sharpening);
  }

  return GST_FLOW_OK;
}

static gint
gst_greatest_common_divisor (gint a, gint b)
{
  while (b != 0) {
    int temp = a;

    a = b;
    b = temp % b;
  }

  return ABS (a);
}

static gboolean
gst_fraction_double (gint * n_out, gint * d_out)
{
  gint n, d, gcd;

  n = *n_out;
  d = *d_out;

  if (d == 0)
    return FALSE;

  if (n == 0 || (n == G_MAXINT && d == 1))
    return TRUE;

  gcd = gst_greatest_common_divisor (n, d);
  n /= gcd;
  d /= gcd;

  if (G_MAXINT / 2 >= ABS (n)) {
    n *= 2;
  } else if (d >= 2) {
    d /= 2;
  } else
    return FALSE;

  *n_out = n;
  *d_out = d;

  return TRUE;
}

static gboolean
gst_vdp_vpp_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstCaps *video_caps = NULL;
  gboolean res = FALSE;

  GstCaps *allowed_caps, *output_caps, *src_caps;

  /* check if the input is non native */
  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    if (!gst_structure_get_fourcc (structure, "format", &vpp->fourcc))
      goto done;
    vpp->native_input = FALSE;
    video_caps = gst_vdp_yuv_to_video_caps (caps);
    if (!video_caps)
      goto done;

    if (!vpp->vpool)
      vpp->vpool = gst_vdp_video_buffer_pool_new (vpp->device);

    gst_vdp_buffer_pool_set_caps (vpp->vpool, video_caps);

  } else {
    vpp->native_input = TRUE;
    video_caps = gst_caps_ref (caps);

    if (vpp->vpool) {
      g_object_unref (vpp->vpool);
      vpp->vpool = NULL;
    }
  }

  structure = gst_caps_get_structure (video_caps, 0);
  if (!gst_structure_get_int (structure, "width", &vpp->width) ||
      !gst_structure_get_int (structure, "height", &vpp->height) ||
      !gst_structure_get_int (structure, "chroma-type",
          (gint *) & vpp->chroma_type))
    goto done;


  /* get interlaced flag */
  gst_structure_get_boolean (structure, "interlaced", &vpp->interlaced);

  /* extract par */
  if (gst_structure_has_field_typed (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION)) {
    gst_structure_get_fraction (structure, "pixel-aspect-ratio", &vpp->par_n,
        &vpp->par_d);
    vpp->got_par = TRUE;
  } else
    vpp->got_par = FALSE;

  allowed_caps = gst_pad_get_allowed_caps (vpp->srcpad);
  if (G_UNLIKELY (!allowed_caps))
    goto null_allowed_caps;
  if (G_UNLIKELY (gst_caps_is_empty (allowed_caps)))
    goto empty_allowed_caps;
  GST_DEBUG ("allowed_caps: %" GST_PTR_FORMAT, allowed_caps);

  output_caps = gst_vdp_video_to_output_caps (video_caps);
  src_caps = gst_caps_intersect (output_caps, allowed_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (output_caps);

  if (gst_caps_is_empty (src_caps))
    goto not_negotiated;

  gst_pad_fixate_caps (vpp->srcpad, src_caps);


  if (gst_vdp_vpp_is_interlaced (vpp)) {
    gint fps_n, fps_d;

    if (gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)) {
      gst_fraction_double (&fps_n, &fps_d);
      gst_caps_set_simple (src_caps, "framerate", GST_TYPE_FRACTION, fps_n,
          fps_d, NULL);
      vpp->field_duration = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
    }

    gst_caps_set_simple (src_caps, "interlaced", G_TYPE_BOOLEAN, FALSE, NULL);
  }

  GST_DEBUG ("src_caps: %" GST_PTR_FORMAT, src_caps);

  res = gst_pad_set_caps (vpp->srcpad, src_caps);
  gst_caps_unref (src_caps);

done:
  gst_object_unref (vpp);
  if (video_caps)
    gst_caps_unref (video_caps);

  return res;

null_allowed_caps:
  GST_ERROR_OBJECT (vpp, "Got null from gst_pad_get_allowed_caps");
  goto done;

empty_allowed_caps:
  GST_ERROR_OBJECT (vpp, "Got EMPTY caps from gst_pad_get_allowed_caps");

  gst_caps_unref (allowed_caps);
  goto done;

not_negotiated:
  gst_caps_unref (src_caps);
  GST_ERROR_OBJECT (vpp, "Couldn't find suitable output format");
  goto done;
}

static void
gst_vdp_vpp_flush (GstVdpVideoPostProcess * vpp)
{
  gint i;

  for (i = 0; i < vpp->n_future_pictures; i++) {
    gst_vdp_picture_clear (vpp->future_pictures[i]);
  }
  vpp->n_future_pictures = 0;

  for (i = 0; i < vpp->n_past_pictures; i++) {
    gst_vdp_picture_clear (vpp->past_pictures[i]);
  }
  vpp->n_past_pictures = 0;
}

static gboolean
gst_vdp_vpp_start (GstVdpVideoPostProcess * vpp)
{
  gint i;
  GError *err;

  vpp->interlaced = FALSE;
  vpp->field_duration = GST_CLOCK_TIME_NONE;

  vpp->earliest_time = GST_CLOCK_TIME_NONE;
  vpp->discont = FALSE;

  vpp->mixer = VDP_INVALID_HANDLE;
  vpp->vpool = NULL;

  for (i = 0; i < MAX_PICTURES; i++) {
    vpp->future_pictures[i].buf = NULL;
    vpp->past_pictures[i].buf = NULL;
  }
  vpp->n_future_pictures = 0;
  vpp->n_past_pictures = 0;

  err = NULL;
  vpp->device = gst_vdp_get_device (vpp->display, &err);
  if (G_UNLIKELY (!vpp->device))
    goto device_error;

  g_object_set (G_OBJECT (vpp->srcpad), "device", vpp->device, NULL);

  return TRUE;

device_error:
  gst_vdp_vpp_post_error (vpp, err);
  return FALSE;
}

static gboolean
gst_vdp_vpp_stop (GstVdpVideoPostProcess * vpp)
{
  gst_vdp_vpp_flush (vpp);

  if (vpp->vpool)
    g_object_unref (vpp->vpool);

  if (vpp->mixer != VDP_INVALID_HANDLE) {
    GstVdpDevice *device = vpp->device;
    VdpStatus status;

    status = device->vdp_video_mixer_destroy (vpp->mixer);
    if (status != VDP_STATUS_OK) {
      GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
          ("Could not destroy vdpau decoder"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));
      return FALSE;
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_vdp_vpp_drain (GstVdpVideoPostProcess * vpp)
{
  GstVdpPicture current_pic;

  guint32 video_surfaces_past_count;
  VdpVideoSurface video_surfaces_past[MAX_PICTURES];

  guint32 video_surfaces_future_count;
  VdpVideoSurface video_surfaces_future[MAX_PICTURES];

  GstFlowReturn ret;

  while (gst_vdp_vpp_get_next_picture (vpp,
          &current_pic,
          &video_surfaces_past_count, video_surfaces_past,
          &video_surfaces_future_count, video_surfaces_future)) {
    GError *err;
    GstVdpOutputBuffer *outbuf;

    GstStructure *structure;
    GstVideoRectangle src_r = { 0, };
    GstVideoRectangle dest_r = { 0, };
    VdpRect rect;

    GstVdpDevice *device;
    VdpStatus status;

    err = NULL;
    ret =
        gst_vdp_output_src_pad_alloc_buffer ((GstVdpOutputSrcPad *) vpp->srcpad,
        &outbuf, &err);
    if (ret != GST_FLOW_OK)
      goto output_pad_error;

    src_r.w = vpp->width;
    src_r.h = vpp->height;
    if (vpp->got_par) {
      gint new_width;

      new_width = gst_util_uint64_scale_int (src_r.w, vpp->par_n, vpp->par_d);
      src_r.x += (src_r.w - new_width) / 2;
      src_r.w = new_width;
    }

    structure = gst_caps_get_structure (GST_BUFFER_CAPS (outbuf), 0);
    if (!gst_structure_get_int (structure, "width", &dest_r.w) ||
        !gst_structure_get_int (structure, "height", &dest_r.h))
      goto invalid_caps;

    if (vpp->force_aspect_ratio) {
      GstVideoRectangle res_r = { 0, };

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
    status =
        device->vdp_video_mixer_render (vpp->mixer, VDP_INVALID_HANDLE, NULL,
        current_pic.structure, video_surfaces_past_count, video_surfaces_past,
        current_pic.buf->surface, video_surfaces_future_count,
        video_surfaces_future, NULL, outbuf->surface, NULL, &rect, 0, NULL);
    if (status != VDP_STATUS_OK)
      goto render_error;

    GST_BUFFER_TIMESTAMP (outbuf) = current_pic.timestamp;
    if (gst_vdp_vpp_is_interlaced (vpp))
      GST_BUFFER_DURATION (outbuf) = vpp->field_duration;
    else
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (current_pic.buf);

    if (GST_BUFFER_FLAG_IS_SET (current_pic.buf, GST_BUFFER_FLAG_DISCONT))
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

    if (GST_BUFFER_FLAG_IS_SET (current_pic.buf, GST_BUFFER_FLAG_PREROLL))
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_PREROLL);

    if (GST_BUFFER_FLAG_IS_SET (current_pic.buf, GST_BUFFER_FLAG_GAP))
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

    err = NULL;
    ret =
        gst_vdp_output_src_pad_push ((GstVdpOutputSrcPad *) vpp->srcpad,
        outbuf, &err);
    if (ret != GST_FLOW_OK)
      goto output_pad_error;

    continue;

  invalid_caps:
    gst_buffer_unref (GST_BUFFER (outbuf));
    GST_ELEMENT_ERROR (vpp, STREAM, FAILED, ("Invalid output caps"), (NULL));
    ret = GST_FLOW_ERROR;
    break;

  render_error:
    gst_buffer_unref (GST_BUFFER (outbuf));
    GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
        ("Could not postprocess frame"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
    ret = GST_FLOW_ERROR;
    break;

  output_pad_error:
    if (ret == GST_FLOW_ERROR && err != NULL)
      gst_vdp_vpp_post_error (vpp, err);
    break;
  }

  return ret;
}

static GstFlowReturn
gst_vdp_vpp_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));

  GstClockTime qostime;
  GstFlowReturn ret = GST_FLOW_OK;
  GError *err;

  GST_DEBUG ("chain");

  /* can only do QoS if the segment is in TIME */
  if (vpp->segment.format != GST_FORMAT_TIME)
    goto no_qos;

  /* QOS is done on the running time of the buffer, get it now */
  qostime = gst_segment_to_running_time (&vpp->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));

  if (qostime != -1) {
    gboolean need_skip;
    GstClockTime earliest_time;

    /* lock for getting the QoS parameters that are set (in a different thread)
     * with the QOS events */
    GST_OBJECT_LOCK (vpp);
    earliest_time = vpp->earliest_time;
    /* check for QoS, don't perform conversion for buffers
     * that are known to be late. */
    need_skip = GST_CLOCK_TIME_IS_VALID (earliest_time) && qostime != -1 &&
        qostime <= earliest_time;

    GST_OBJECT_UNLOCK (vpp);

    if (need_skip) {
      GST_DEBUG_OBJECT (vpp, "skipping transform: qostime %"
          GST_TIME_FORMAT " <= %" GST_TIME_FORMAT,
          GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));
      /* mark discont for next buffer */
      vpp->discont = TRUE;
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    }
  }

no_qos:

  if (vpp->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    vpp->discont = FALSE;
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (vpp, "Received discont buffer");
    gst_vdp_vpp_flush (vpp);
  }

  if (!vpp->native_input) {
    GstVdpVideoBuffer *video_buf;

    err = NULL;
    video_buf =
        (GstVdpVideoBuffer *) gst_vdp_buffer_pool_get_buffer (vpp->vpool, &err);
    if (G_UNLIKELY (!video_buf))
      goto video_buf_error;

    if (!gst_vdp_video_buffer_upload (video_buf, buffer, vpp->fourcc,
            vpp->width, vpp->height)) {
      gst_buffer_unref (GST_BUFFER (video_buf));
      GST_ELEMENT_ERROR (vpp, RESOURCE, READ,
          ("Couldn't upload YUV data to vdpau"), (NULL));
      ret = GST_FLOW_ERROR;
      goto error;
    }

    gst_buffer_copy_metadata (GST_BUFFER (video_buf), buffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

    gst_buffer_unref (buffer);
    buffer = GST_BUFFER (video_buf);
  }

  if (G_UNLIKELY (vpp->mixer == VDP_INVALID_HANDLE)) {
    ret = gst_vdp_vpp_create_mixer (vpp);
    if (ret != GST_FLOW_OK)
      goto error;
  }

  gst_vdp_vpp_add_buffer (vpp, GST_VDP_VIDEO_BUFFER (buffer));

  ret = gst_vdp_vpp_drain (vpp);

done:
  gst_object_unref (vpp);

  return ret;

error:
  gst_buffer_unref (buffer);
  goto done;

video_buf_error:
  gst_buffer_unref (GST_BUFFER (buffer));
  gst_vdp_vpp_post_error (vpp, err);
  ret = GST_FLOW_ERROR;
  goto done;
}

static GstCaps *
gst_vdp_vpp_sink_getcaps (GstPad * pad)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  GstCaps *caps;

  if (vpp->device) {
    caps = gst_vdp_video_buffer_get_allowed_caps (vpp->device);
  } else {
    GstElementClass *element_class = GST_ELEMENT_GET_CLASS (vpp);
    GstPadTemplate *sink_template;

    sink_template = gst_element_class_get_pad_template (element_class, "sink");
    caps = gst_caps_copy (gst_pad_template_get_caps (sink_template));
  }
  GST_DEBUG ("returning caps: %" GST_PTR_FORMAT, caps);

  gst_object_unref (vpp);

  return caps;
}

static gboolean
gst_vdp_vpp_src_event (GstPad * pad, GstEvent * event)
{
  GstVdpVideoPostProcess *vpp =
      GST_VDP_VIDEO_POST_PROCESS (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (vpp);
      vpp->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (vpp);

      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
  }

  gst_object_unref (vpp);

  return res;
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

      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &time);

      GST_OBJECT_LOCK (vpp);
      gst_segment_set_newsegment_full (&vpp->segment, update, rate,
          applied_rate, format, start, stop, time);
      GST_OBJECT_UNLOCK (vpp);

      res = gst_pad_event_default (pad, event);
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
      if (!gst_vdp_vpp_start (vpp))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_vdp_vpp_stop (vpp))
        ret = GST_STATE_CHANGE_FAILURE;
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
    case PROP_DISPLAY:
      g_value_set_string (value, vpp->display);
      break;

    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, vpp->force_aspect_ratio);
      break;

    case PROP_DEINTERLACE_MODE:
      g_value_set_enum (value, vpp->mode);
      break;

    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, vpp->method);
      break;

    case PROP_NOISE_REDUCTION:
      g_value_set_float (value, vpp->noise_reduction);
      break;

    case PROP_SHARPENING:
      g_value_set_float (value, vpp->sharpening);
      break;

    case PROP_INVERSE_TELECINE:
      g_value_set_boolean (value, vpp->inverse_telecine);
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
    case PROP_DISPLAY:
      g_free (vpp->display);
      vpp->display = g_value_dup_string (value);
      break;

    case PROP_FORCE_ASPECT_RATIO:
      vpp->force_aspect_ratio = g_value_get_boolean (value);
      break;

    case PROP_DEINTERLACE_MODE:
      vpp->mode = g_value_get_enum (value);
      break;

    case PROP_DEINTERLACE_METHOD:
    {
      GstVdpDeinterlaceMethods oldvalue;

      oldvalue = vpp->method;
      vpp->method = g_value_get_enum (value);
      if (oldvalue == vpp->method)
        break;

      if (vpp->mixer != VDP_INVALID_HANDLE) {
        if (oldvalue != GST_VDP_DEINTERLACE_METHOD_BOB)
          gst_vdp_vpp_activate_deinterlace_method (vpp, oldvalue, FALSE);

        if (vpp->method != GST_VDP_DEINTERLACE_METHOD_BOB)
          gst_vdp_vpp_activate_deinterlace_method (vpp, oldvalue, TRUE);
      }
      break;
    }

    case PROP_NOISE_REDUCTION:
    {
      gfloat old_value;

      old_value = vpp->noise_reduction;
      vpp->noise_reduction = g_value_get_float (value);
      if (vpp->noise_reduction == old_value)
        break;

      if (vpp->mixer != VDP_INVALID_HANDLE) {
        if (vpp->noise_reduction == 0.0)
          gst_vdp_vpp_activate_feature (vpp,
              VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION, FALSE);

        if (old_value == 0.0)
          gst_vdp_vpp_activate_feature (vpp,
              VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION, TRUE);

        gst_vdp_vpp_set_attribute_float (vpp,
            VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL,
            vpp->noise_reduction);
      }
      break;
    }

    case PROP_SHARPENING:
    {
      gfloat old_value;

      old_value = vpp->sharpening;
      vpp->sharpening = g_value_get_float (value);
      if (vpp->sharpening == old_value)
        break;

      if (vpp->mixer != VDP_INVALID_HANDLE) {
        if (vpp->sharpening == 0.0)
          gst_vdp_vpp_activate_feature (vpp,
              VDP_VIDEO_MIXER_FEATURE_SHARPNESS, FALSE);

        if (old_value == 0.0)
          gst_vdp_vpp_activate_feature (vpp,
              VDP_VIDEO_MIXER_FEATURE_SHARPNESS, TRUE);

        gst_vdp_vpp_set_attribute_float (vpp,
            VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL, vpp->sharpening);
      }
      break;
    }

    case PROP_INVERSE_TELECINE:
    {
      vpp->inverse_telecine = g_value_get_boolean (value);

      if (vpp->mixer != VDP_INVALID_HANDLE) {
        gst_vdp_vpp_activate_feature (vpp,
            VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE, vpp->inverse_telecine);
      }
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* GType vmethod implementations */

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_vpp_class_init (GstVdpVideoPostProcessClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstCaps *src_caps, *sink_caps;
  GstPadTemplate *src_template, *sink_template;

  GST_DEBUG_CATEGORY_INIT (gst_vdp_vpp_debug, "vdpauvideopostprocess", 0,
      "VDPAU video surface to output surface");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_vdp_vpp_get_property;
  gobject_class->set_property = gst_vdp_vpp_set_property;
  gobject_class->finalize = gst_vdp_vpp_finalize;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, the plugin will only scale up the input surface to the"
          "maximum size where the aspect ratio can be preserved", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE_MODE,
      g_param_spec_enum ("mode", "Deinterlace mode",
          "Specifies if the element should deinterlace or not",
          GST_TYPE_VDP_DEINTERLACE_MODES, GST_VDP_DEINTERLACE_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE_METHOD,
      g_param_spec_enum ("method", "Deinterlace method",
          "Specifies which deinterlace method to use",
          GST_TYPE_VDP_DEINTERLACE_METHODS, GST_VDP_DEINTERLACE_METHOD_BOB,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NOISE_REDUCTION,
      g_param_spec_float ("noise-reduction", "Noise reduction",
          "The amount of noise reduction that should be done", 0.0, 1.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARPENING,
      g_param_spec_float ("sharpening", "Sharpening",
          "The amount of sharpening or blurring to be applied", -1.0, 1.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INVERSE_TELECINE,
      g_param_spec_boolean ("inverse-telecine", "Inverse telecine",
          "Whether inverse telecine should be used", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (gstelement_class,
      "VdpauVideoPostProcess",
      "Filter/Converter/Decoder/Video",
      "Post process GstVdpVideoBuffers and output GstVdpOutputBuffers",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gstelement_class->change_state = gst_vdp_vpp_change_state;

  /* SRC PAD */
  src_caps = gst_vdp_output_buffer_get_template_caps ();
  src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      src_caps);
  gst_element_class_add_pad_template (gstelement_class, src_template);
  gst_caps_unref (src_caps);

  /* SINK PAD */
  sink_caps = gst_vdp_video_buffer_get_caps (FALSE, 0);
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      sink_caps);
  gst_element_class_add_pad_template (gstelement_class, sink_template);
  gst_caps_unref (sink_caps);
}

static void
gst_vdp_vpp_init (GstVdpVideoPostProcess * vpp)
{
  GstPadTemplate *src_template, *sink_template;

  vpp->device = NULL;
  vpp->mixer = VDP_INVALID_HANDLE;

  vpp->display = NULL;

  vpp->force_aspect_ratio = FALSE;
  vpp->mode = GST_VDP_DEINTERLACE_MODE_AUTO;
  vpp->method = GST_VDP_DEINTERLACE_METHOD_BOB;

  vpp->noise_reduction = 0.0;
  vpp->sharpening = 0.0;

  /* SRC PAD */
  src_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (gclass), "src");

  vpp->srcpad = GST_PAD (gst_vdp_output_src_pad_new (src_template, "src"));
  gst_element_add_pad (GST_ELEMENT (vpp), vpp->srcpad);

  gst_pad_set_event_function (vpp->srcpad,
      GST_DEBUG_FUNCPTR (gst_vdp_vpp_src_event));

  /* SINK PAD */
  sink_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (gclass), "sink");
  vpp->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vpp), vpp->sinkpad);

  gst_pad_set_getcaps_function (vpp->sinkpad, gst_vdp_vpp_sink_getcaps);
  gst_pad_set_setcaps_function (vpp->sinkpad, gst_vdp_vpp_sink_setcaps);
  gst_pad_set_chain_function (vpp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vdp_vpp_chain));
  gst_pad_set_event_function (vpp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vdp_vpp_sink_event));
}

static void
gst_vdp_vpp_finalize (GObject * object)
{
}
