/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

/* Some implementation was taken from NVIDIA DeepStream 7.0 source code */

/**
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>
#include <NVWarp360.h>
#include <mutex>
#include <string.h>
#include "gstnvdsdewarp.h"

GST_DEBUG_CATEGORY_STATIC (gst_nv_ds_dewarp_debug);
#define GST_CAT_DEFAULT gst_nv_ds_dewarp_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, "RGBA"))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, "RGBA"))
    );

#define RADIANS_PER_DEGREE (G_PI / 180.0)

typedef enum
{
  GST_NV_DS_DEWARP_WARP_NONE,
  GST_NV_DS_DEWARP_WARP_FISHEYE_PUSHBROOM,
  GST_NV_DS_DEWARP_WARP_FISHEYE_ROTCYLINDER,
  GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PERSPECTIVE,
  GST_NV_DS_DEWARP_WARP_FISHEYE_PERSPECTIVE,
  GST_NV_DS_DEWARP_WARP_FISHEYE_FISHEYE,
  GST_NV_DS_DEWARP_WARP_FISHEYE_CYLINDER,
  GST_NV_DS_DEWARP_WARP_FISHEYE_EQUIRECT,
  GST_NV_DS_DEWARP_WARP_FISHEYE_PANINI,
  GST_NV_DS_DEWARP_WARP_PERSPECTIVE_EQUIRECT,
  GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PANINI,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_CYLINDER,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_EQUIRECT,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_FISHEYE,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_PANINI,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_PERSPECTIVE,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_PUSHBROOM,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_STEREOGRAPHIC,
  GST_NV_DS_DEWARP_WARP_EQUIRECT_ROTCYLINDER,
} GstNvDsDewarpWarpType;

/**
 * GstNvDsDewarpWarp:
 *
 * Since: 1.26
 */
#define GST_TYPE_NV_DS_DEWARP_WARP (gst_nv_ds_dewarp_warp_get_type())
static GType
gst_nv_ds_dewarp_warp_get_type (void)
{
  static std::once_flag once;
  static GType type = 0;
  static const GEnumValue warp_types[] = {
    {GST_NV_DS_DEWARP_WARP_NONE, "None", "none"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_PUSHBROOM,
        "Fisheye Pushbroom", "fisheye-pushbroom"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_ROTCYLINDER,
        "Fisheye Rotcylinder", "fisheye-rotcylinder"},
    {GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PERSPECTIVE,
        "Perspective Perspective", "perspective-perspective"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_PERSPECTIVE,
        "Fisheye Perspective", "fisheye-perspective"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_FISHEYE,
        "Fisheye Fisheye", "fisheye-fisheye"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_CYLINDER,
        "Fisheye Cylinder", "fisheye-cylinder"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_EQUIRECT,
        "Fisheye Equirect", "fisheye-equirect"},
    {GST_NV_DS_DEWARP_WARP_FISHEYE_PANINI,
        "Fisheye Panini", "fisheye-panini"},
    {GST_NV_DS_DEWARP_WARP_PERSPECTIVE_EQUIRECT,
        "Perspective Equirect", "perspective-equirect"},
    {GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PANINI,
        "Perspective Panini", "perspective-panini"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_CYLINDER,
        "Equirect Cylinder", "equirect-cylinder"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_EQUIRECT,
        "Equirect Equirect", "equirect-equirect"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_FISHEYE,
        "Equirect Fisheye", "equirect-fisheye"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_PANINI,
        "Equirect Panini", "equirect-panini"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_PERSPECTIVE,
        "Equirect Perspective", "equirect-perspective"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_PUSHBROOM,
        "Equirect Pushbroom", "equirect-pushbroom"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_STEREOGRAPHIC,
        "Equirect Sterographic", "equirect-stereographic"},
    {GST_NV_DS_DEWARP_WARP_EQUIRECT_ROTCYLINDER,
        "Equirect Rotcylinder", "equirect-rotcylinder"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&]() {
        type = g_enum_register_static ("GstNvDsDewarpWarp", warp_types);
      });

  return type;
}

static nvwarpType_t
warp_type_to_native (GstNvDsDewarpWarpType type)
{
  switch (type) {
    case GST_NV_DS_DEWARP_WARP_NONE:
      return NVWARP_NONE;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_PUSHBROOM:
      return NVWARP_FISHEYE_PUSHBROOM;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_ROTCYLINDER:
      return NVWARP_FISHEYE_ROTCYLINDER;
    case GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PERSPECTIVE:
      return NVWARP_PERSPECTIVE_PERSPECTIVE;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_PERSPECTIVE:
      return NVWARP_FISHEYE_PERSPECTIVE;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_FISHEYE:
      return NVWARP_FISHEYE_FISHEYE;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_CYLINDER:
      return NVWARP_FISHEYE_CYLINDER;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_EQUIRECT:
      return NVWARP_FISHEYE_EQUIRECT;
    case GST_NV_DS_DEWARP_WARP_FISHEYE_PANINI:
      return NVWARP_FISHEYE_PANINI;
    case GST_NV_DS_DEWARP_WARP_PERSPECTIVE_EQUIRECT:
      return NVWARP_PERSPECTIVE_EQUIRECT;
    case GST_NV_DS_DEWARP_WARP_PERSPECTIVE_PANINI:
      return NVWARP_PERSPECTIVE_PANINI;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_CYLINDER:
      return NVWARP_EQUIRECT_CYLINDER;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_EQUIRECT:
      return NVWARP_EQUIRECT_EQUIRECT;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_FISHEYE:
      return NVWARP_EQUIRECT_FISHEYE;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_PANINI:
      return NVWARP_EQUIRECT_PANINI;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_PERSPECTIVE:
      return NVWARP_EQUIRECT_PERSPECTIVE;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_PUSHBROOM:
      return NVWARP_EQUIRECT_PUSHBROOM;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_STEREOGRAPHIC:
      return NVWARP_EQUIRECT_STEREOGRAPHIC;
    case GST_NV_DS_DEWARP_WARP_EQUIRECT_ROTCYLINDER:
      return NVWARP_EQUIRECT_ROTCYLINDER;
  }

  return NVWARP_NONE;
}

typedef enum
{
  GST_NV_DS_DEWARP_AXES_XYZ,
  GST_NV_DS_DEWARP_AXES_XZY,
  GST_NV_DS_DEWARP_AXES_YXZ,
  GST_NV_DS_DEWARP_AXES_YZX,
  GST_NV_DS_DEWARP_AXES_ZXY,
  GST_NV_DS_DEWARP_AXES_ZYX,
} GstNvDsDewarpAxes;

static const GEnumValue g_axes_types[] = {
  {GST_NV_DS_DEWARP_AXES_XYZ, "XYZ", "xyz"},
  {GST_NV_DS_DEWARP_AXES_XZY, "XZY", "xzy"},
  {GST_NV_DS_DEWARP_AXES_YXZ, "YXZ", "yxz"},
  {GST_NV_DS_DEWARP_AXES_YZX, "YZX", "yzx"},
  {GST_NV_DS_DEWARP_AXES_ZXY, "ZXY", "zxy"},
  {GST_NV_DS_DEWARP_AXES_ZYX, "ZYX", "zyx"},
  {0, nullptr, nullptr},
};

/**
 * GstNvDsDewarpAxes:
 *
 * Since: 1.26
 */
#define GST_TYPE_NV_DS_DEWARP_AXES (gst_nv_ds_dewarp_axes_get_type())
static GType
gst_nv_ds_dewarp_axes_get_type (void)
{
  static std::once_flag once;
  static GType type = 0;

  std::call_once (once,[&]() {
        type = g_enum_register_static ("GstNvDsDewarpAxes", g_axes_types);
      });

  return type;
}

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_WARP_TYPE,
  PROP_ROTATION_AXES,
  PROP_YAW,
  PROP_PITCH,
  PROP_ROLL,
  PROP_TOP_ANGLE,
  PROP_BOTTOM_ANGLE,
  PROP_FOV,
  PROP_CONTROL,
  PROP_ADD_BORDERS,
};

#define DEFAULT_DEVICE_ID -1
#define DEFAULT_WARP_TYPE GST_NV_DS_DEWARP_WARP_NONE
#define DEFAULT_ROTATION_AXES GST_NV_DS_DEWARP_AXES_YXZ
#define DEFAULT_TOP_ANGLE 90
#define DEFAULT_BOTTOM_ANGLE -90
#define DEFAULT_ANGLE 0
#define DEFAULT_FOV 180.0
#define DEFAULT_CONTROL 0.6
#define DEFAULT_ADD_BORDERS TRUE

struct GstNvDsDewarpPrivate
{
  GstNvDsDewarpPrivate ()
  {
    texture_token = gst_cuda_create_user_token ();
  }

   ~GstNvDsDewarpPrivate ()
  {
    reset ();
  }

  void reset ()
  {
    if (handle) {
      g_assert (context);
      gst_cuda_context_push (context);
      g_clear_pointer (&handle, nvwarpDestroyInstance);
      gst_cuda_context_pop (nullptr);
    }

    gst_clear_cuda_stream (&other_stream);
    gst_clear_cuda_stream (&stream);
    gst_clear_object (&context);
  }

  GstCudaContext *context = nullptr;
  GstCudaStream *stream = nullptr;
  GstCudaStream *other_stream = nullptr;
  nvwarpHandle handle = nullptr;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  bool params_updated = true;
  bool clear_background = false;
  GstVideoRectangle out_rect;
  gint64 texture_token = 0;

  std::recursive_mutex context_lock;
  std::mutex lock;
  gint device_id = DEFAULT_DEVICE_ID;
  GstNvDsDewarpWarpType warp_type = DEFAULT_WARP_TYPE;
  GstNvDsDewarpAxes axes = DEFAULT_ROTATION_AXES;
  gdouble yaw = DEFAULT_ANGLE;
  gdouble pitch = DEFAULT_ANGLE;
  gdouble roll = DEFAULT_ANGLE;
  gdouble top_angle = DEFAULT_TOP_ANGLE;
  gdouble bottom_angle = DEFAULT_BOTTOM_ANGLE;
  gdouble fov = DEFAULT_FOV;
  gdouble control = DEFAULT_CONTROL;
  gboolean add_borders = DEFAULT_ADD_BORDERS;
};

struct _GstNvDsDewarp
{
  GstBaseTransform parent;

  GstNvDsDewarpPrivate *priv;
};

static void gst_nv_ds_dewarp_finalize (GObject * object);
static void gst_nv_ds_dewarp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_ds_dewarp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_nv_ds_dewarp_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_nv_ds_dewarp_start (GstBaseTransform * trans);
static gboolean gst_nv_ds_dewarp_stop (GstBaseTransform * trans);
static gboolean gst_nv_ds_dewarp_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_nv_ds_dewarp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_nv_ds_dewarp_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static GstCaps *gst_nv_ds_dewarp_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_nv_ds_dewarp_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_nv_ds_dewarp_set_caps (GstBaseTransform * trans,
    GstCaps * in_caps, GstCaps * out_caps);
static void gst_nv_ds_dewarp_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static GstFlowReturn gst_nv_ds_dewarp_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_nv_ds_dewarp_parent_class parent_class
G_DEFINE_TYPE (GstNvDsDewarp, gst_nv_ds_dewarp, GST_TYPE_BASE_TRANSFORM);

static void
gst_nv_ds_dewarp_class_init (GstNvDsDewarpClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto elem_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_nv_ds_dewarp_finalize;
  object_class->set_property = gst_nv_ds_dewarp_set_property;
  object_class->get_property = gst_nv_ds_dewarp_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("device-id", "Device ID", "CUDA Device ID",
          -1, G_MAXINT32, DEFAULT_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_WARP_TYPE,
      g_param_spec_enum ("warp-type", "Warp type",
          "Warp type to use. \"wrap-type=none\" will enable passthrough mode",
          GST_TYPE_NV_DS_DEWARP_WARP, DEFAULT_WARP_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_ROTATION_AXES,
      g_param_spec_enum ("rotation-axes", "Rotation Axes",
          "Rotation Axes to apply. X rotation rotates the view upward, "
          "Y rightward, and Z clockwise. Default is \"YXZ\" "
          "as known as yaw, pitch, roll",
          GST_TYPE_NV_DS_DEWARP_AXES, DEFAULT_ROTATION_AXES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_YAW,
      g_param_spec_double ("yaw", "Yaw", "Yaw rotation angle in degrees",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_ANGLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_PITCH,
      g_param_spec_double ("pitch", "Pitch", "Pitch rotation angle in degrees",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_ANGLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_ROLL,
      g_param_spec_double ("roll", "Roll", "Roll rotation angle in degrees",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_ANGLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_TOP_ANGLE,
      g_param_spec_double ("top-angle", "Top Angle",
          "Top angle of view in degrees",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_TOP_ANGLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_BOTTOM_ANGLE,
      g_param_spec_double ("bottom-angle", "Bottom Angle",
          "Bottom angle of view in degrees",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_BOTTOM_ANGLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FOV,
      g_param_spec_double ("fov", "Fov", "Source field of view in degrees",
          0, G_MAXFLOAT, DEFAULT_FOV,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_CONTROL,
      g_param_spec_double ("control", "Control",
          "Projection specific control value", 0, 1, DEFAULT_CONTROL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add black borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  elem_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_set_context);

  gst_element_class_add_static_pad_template (elem_class, &sink_template);
  gst_element_class_add_static_pad_template (elem_class, &src_template);

  gst_element_class_set_static_metadata (elem_class,
      "NvDsDewarp",
      "Filter/Effect/Video/Hardware",
      "Performs dewraping using NVIDIA DeepStream NVWarp360 API",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->start = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_query);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_decide_allocation);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_fixate_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_set_caps);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_before_transform);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_nv_ds_dewarp_transform);

  gst_type_mark_as_plugin_api (GST_TYPE_NV_DS_DEWARP_WARP,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_NV_DS_DEWARP_AXES,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_nv_ds_dewarp_debug,
      "nvdsdewarp", 0, "nvdsdewarp");
}

static void
gst_nv_ds_dewarp_init (GstNvDsDewarp * self)
{
  self->priv = new GstNvDsDewarpPrivate ();
}

static void
gst_nv_ds_dewarp_finalize (GObject * object)
{
  auto self = GST_NV_DS_DEWARP (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_prop_double (GstNvDsDewarp * self, gdouble * prev, const GValue * value)
{
  auto priv = self->priv;
  auto val = g_value_get_double (value);

  if (*prev != val) {
    *prev = val;
    priv->params_updated = true;
  }
}

static void
gst_nv_ds_dewarp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_NV_DS_DEWARP (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    case PROP_WARP_TYPE:
    {
      auto warp_type = (GstNvDsDewarpWarpType) g_value_get_enum (value);
      if (priv->warp_type != warp_type) {
        priv->warp_type = warp_type;
        priv->params_updated = true;
      }
      break;
    }
    case PROP_ROTATION_AXES:
    {
      auto axes = (GstNvDsDewarpAxes) g_value_get_enum (value);
      if (priv->axes != axes) {
        priv->axes = axes;
        priv->params_updated = true;
      }
      break;
    }
    case PROP_YAW:
      update_prop_double (self, &priv->yaw, value);
      break;
    case PROP_PITCH:
      update_prop_double (self, &priv->pitch, value);
      break;
    case PROP_ROLL:
      update_prop_double (self, &priv->roll, value);
      break;
    case PROP_TOP_ANGLE:
      update_prop_double (self, &priv->top_angle, value);
      break;
    case PROP_BOTTOM_ANGLE:
      update_prop_double (self, &priv->bottom_angle, value);
      break;
    case PROP_FOV:
      update_prop_double (self, &priv->fov, value);
      break;
    case PROP_CONTROL:
      update_prop_double (self, &priv->control, value);
      break;
    case PROP_ADD_BORDERS:
    {
      auto val = g_value_get_boolean (value);
      if (val != priv->add_borders)
        gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (self));
      priv->add_borders = val;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_ds_dewarp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  auto self = GST_NV_DS_DEWARP (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    case PROP_WARP_TYPE:
      g_value_set_enum (value, priv->warp_type);
      break;
    case PROP_ROTATION_AXES:
      g_value_set_enum (value, priv->axes);
      break;
    case PROP_YAW:
      g_value_set_double (value, priv->yaw);
      break;
    case PROP_PITCH:
      g_value_set_double (value, priv->pitch);
      break;
    case PROP_ROLL:
      g_value_set_double (value, priv->roll);
      break;
    case PROP_TOP_ANGLE:
      g_value_set_double (value, priv->top_angle);
      break;
    case PROP_BOTTOM_ANGLE:
      g_value_set_double (value, priv->bottom_angle);
      break;
    case PROP_FOV:
      g_value_set_double (value, priv->fov);
      break;
    case PROP_CONTROL:
      g_value_set_double (value, priv->control);
      break;
    case PROP_ADD_BORDERS:
      g_value_set_boolean (value, priv->add_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_ds_dewarp_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_NV_DS_DEWARP (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
    gst_cuda_handle_set_context (element,
        context, priv->device_id, &priv->context);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_ds_dewarp_start (GstBaseTransform * trans)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_cuda_ensure_element_context (GST_ELEMENT (self),
          priv->device_id, &priv->context)) {
    GST_ERROR_OBJECT (self, "Failed to get CUDA context");
    return FALSE;
  }

  priv->stream = gst_cuda_stream_new (priv->context);

  if (!gst_cuda_context_push (priv->context)) {
    GST_ERROR_OBJECT (self, "CuCtxPushCurrent failed");
    priv->reset ();
    return FALSE;
  }

  auto ret = nvwarpCreateInstance (&priv->handle);
  gst_cuda_context_pop (nullptr);

  if (ret != NVWARP_SUCCESS) {
    auto error_str = nvwarpErrorStringFromCode (ret);
    GST_ERROR_OBJECT (self, "nvwarpCreateInstance failed, %d (%s)", ret,
        GST_STR_NULL (error_str));
    priv->reset ();
    return FALSE;
  }

  gst_video_info_init (&priv->in_info);
  gst_video_info_init (&priv->out_info);

  priv->params_updated = true;

  return TRUE;
}

static gboolean
gst_nv_ds_dewarp_stop (GstBaseTransform * trans)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  priv->reset ();

  return TRUE;
}

static gboolean
gst_nv_ds_dewarp_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
    if (gst_cuda_handle_context_query (GST_ELEMENT (self),
            query, priv->context)) {
      return TRUE;
    }
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans,
      direction, query);
}

static gboolean
gst_nv_ds_dewarp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (!gst_query_get_n_allocation_pools (query)) {
    pool = gst_cuda_buffer_pool_new (priv->context);

    auto config = gst_buffer_pool_get_config (pool);
    /* Forward downstream CUDA stream to upstream */
    if (priv->other_stream) {
      GST_DEBUG_OBJECT (self, "Have downstream CUDA stream, forwarding");
      gst_buffer_pool_config_set_cuda_stream (config, priv->other_stream);
    } else if (priv->stream) {
      GST_DEBUG_OBJECT (self, "Set our stream to proposing buffer pool");
      gst_buffer_pool_config_set_cuda_stream (config, priv->stream);
    }

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "failed to set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size by cuda buffer pool */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
        nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_nv_ds_dewarp_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;
  GstCaps *outcaps = nullptr;
  GstBufferPool *pool = nullptr;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;

  gst_query_parse_allocation (query, &outcaps, nullptr);
  if (!outcaps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto cpool = GST_CUDA_BUFFER_POOL (pool);
        if (cpool->context != priv->context)
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_from_caps (&vinfo, outcaps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (self, "create our pool");
    pool = gst_cuda_buffer_pool_new (priv->context);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_clear_cuda_stream (&priv->other_stream);
  priv->other_stream = gst_buffer_pool_config_get_cuda_stream (config);
  if (priv->other_stream) {
    GST_DEBUG_OBJECT (self, "Downstream provided CUDA stream");
  } else if (priv->stream) {
    GST_DEBUG_OBJECT (self, "Set our stream to decided buffer pool");
    gst_buffer_pool_config_set_cuda_stream (config, priv->stream);
  }

  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by cuda buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_nv_ds_dewarp_update_params (GstNvDsDewarp * self)
{
  auto trans = GST_BASE_TRANSFORM (self);
  auto priv = self->priv;

  priv->params_updated = false;

  if (priv->warp_type == GST_NV_DS_DEWARP_WARP_NONE) {
    GST_DEBUG_OBJECT (self, "wrap mode none, enable passthrough");
    gst_base_transform_set_passthrough (trans, TRUE);
    return TRUE;
  }

  gst_base_transform_reconfigure_src (trans);
  gst_base_transform_set_passthrough (trans, FALSE);

  nvwarpParams_t params;
  nvwarpInitParams (&params);

  params.type = warp_type_to_native (priv->warp_type);
  params.srcWidth = priv->in_info.width;
  params.srcHeight = priv->in_info.height;
  params.srcX0 = (params.srcWidth - 1) * 0.5;
  params.srcY0 = (params.srcHeight - 1) * 0.5;

  gdouble angle = priv->fov * 0.5 * RADIANS_PER_DEGREE;
  gdouble radian;
  if (priv->fov == 180.0)
    radian = priv->in_info.height;
  else
    radian = (priv->in_info.height - 1) * 0.5;

  auto ret = nvwarpComputeParamsSrcFocalLength (&params, angle, radian);
  if (ret != NVWARP_SUCCESS) {
    auto error_str = nvwarpErrorStringFromCode (ret);
    GST_ERROR_OBJECT (self, "nvwarpComputeParamsSrcFocalLength failed, %d (%s)",
        ret, GST_STR_NULL (error_str));
    return FALSE;
  }

  params.dstWidth = priv->out_rect.w;
  params.dstHeight = priv->out_rect.h;
  strcpy (params.rotAxes, g_axes_types[priv->axes].value_name);

  for (guint i = 0; i < 3; i++) {
    switch (params.rotAxes[i]) {
      case 'X':
        params.rotAngles[i] = priv->pitch * RADIANS_PER_DEGREE;
        break;
      case 'Y':
        params.rotAngles[i] = priv->yaw * RADIANS_PER_DEGREE;
        break;
      case 'Z':
        params.rotAngles[i] = priv->roll * RADIANS_PER_DEGREE;
        break;
      default:
        break;
    }
  }

  params.topAngle = priv->top_angle * RADIANS_PER_DEGREE;
  params.bottomAngle = priv->bottom_angle * RADIANS_PER_DEGREE;
  params.control[0] = priv->control;

  ret = nvwarpSetParams (priv->handle, &params);
  if (ret != NVWARP_SUCCESS) {
    auto error_str = nvwarpErrorStringFromCode (ret);
    GST_ERROR_OBJECT (self, "nvwarpSetParams failed, %d (%s)", ret,
        GST_STR_NULL (error_str));
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_nv_ds_dewarp_caps_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static GstCaps *
gst_nv_ds_dewarp_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_NV_DS_DEWARP (base);
  auto priv = self->priv;
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  std::lock_guard < std::mutex > lk (priv->lock);
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    gint from_par_n, from_par_d;

    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;

      from_par_n = from_par_d = 1;
    } else {
      from_par_n = gst_value_get_fraction_numerator (from_par);
      from_par_d = gst_value_get_fraction_denominator (from_par);
    }

    if (!to_par) {
      gint to_par_n, to_par_d;

      to_par_n = from_par_n;
      to_par_d = from_par_d;

      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, to_par_n, to_par_d);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          to_par_n, to_par_d, nullptr);
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, nullptr);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int_round (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              nullptr);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int_round (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              nullptr);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scale sized - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int_round (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);
        goto done;
      }

      /* If all this failed, keep the dimensions with the DAR that was closest
       * to the correct DAR. This changes the DAR but there's not much else to
       * do here.
       */
      if (set_w * ABS (set_h - h) < ABS (f_w - w) * f_h) {
        f_h = set_h;
        f_w = set_w;
      }
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, nullptr);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, nullptr);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, nullptr);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, nullptr);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_nv_ds_dewarp_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto ret = gst_nv_ds_dewarp_caps_rangify_size_info (caps);

  if (filter) {
    auto tmp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, ret);

  return ret;
}

static GstCaps *
gst_nv_ds_dewarp_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GST_DEBUG_OBJECT (base,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  othercaps = gst_nv_ds_dewarp_fixate_size (base, direction, caps, othercaps);

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_nv_ds_dewarp_set_caps (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gint borders_w = 0;
  gint borders_h = 0;
  gint in_width, in_height, in_par_n, in_par_d;

  if (!priv->handle) {
    GST_ERROR_OBJECT (self, "Dewarper handle is not configured");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&priv->in_info, in_caps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, in_caps);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&priv->out_info, out_caps)) {
    GST_ERROR_OBJECT (self, "Invalid output caps %" GST_PTR_FORMAT, out_caps);
    return FALSE;
  }

  auto in_info = &priv->in_info;
  auto out_info = &priv->out_info;

  in_width = in_info->height;
  in_height = in_info->width;
  in_par_n = in_info->par_d;
  in_par_d = in_info->par_n;

  if (!gst_util_fraction_multiply (in_width,
          in_height, in_par_n, in_par_d, &from_dar_n, &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (out_info->width,
          out_info->height, out_info->par_n, out_info->par_d, &to_dar_n,
          &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (priv->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          borders_h = out_info->height - to_h;
          borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          borders_h = 0;
          borders_w = out_info->width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (self, "Can't calculate borders");
      }
    } else {
      GST_INFO_OBJECT (self, "Display aspect ratio update %d/%d -> %d/%d",
          from_dar_n, from_dar_d, to_dar_n, to_dar_d);
    }
  }

  priv->out_rect.x = 0;
  priv->out_rect.y = 0;
  priv->out_rect.w = out_info->width;
  priv->out_rect.h = out_info->height;

  if (borders_w) {
    priv->out_rect.x = borders_w / 2;
    priv->out_rect.w = out_info->width - (2 * priv->out_rect.x);
  }

  if (borders_h) {
    priv->out_rect.y = borders_h / 2;
    priv->out_rect.h = out_info->height - (2 * priv->out_rect.y);
  }

  if (borders_w > 0 || borders_h > 0)
    priv->clear_background = true;
  else
    priv->clear_background = false;

  GST_DEBUG_OBJECT (self, "Output rect %dx%d at %d, %d", priv->out_rect.w,
      priv->out_rect.h, priv->out_rect.x, priv->out_rect.y);

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_cuda_context_push (priv->context);
  auto ret = gst_nv_ds_dewarp_update_params (self);
  gst_cuda_context_pop (nullptr);

  return ret;
}

static void
gst_nv_ds_dewarp_before_transform (GstBaseTransform * trans, GstBuffer * buffer)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->params_updated) {
    GST_DEBUG_OBJECT (self, "Property was updated, reconfigure instance");
    gst_cuda_context_push (priv->context);
    gst_nv_ds_dewarp_update_params (self);
    gst_cuda_context_pop (nullptr);
  }
}

struct GstNvDsDewarpTextureData
{
  GstCudaContext *context;
  CUtexObject texture;
};

static GstFlowReturn
gst_nv_ds_dewarp_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoFrame in_frame, out_frame;
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;
  CUtexObject texture;
  CUDA_RESOURCE_DESC resource_desc = { };
  CUDA_TEXTURE_DESC texture_desc = { };

  auto in_mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_cuda_memory (in_mem)) {
    GST_ERROR_OBJECT (self, "Input is not a cuda memory");
    return GST_FLOW_ERROR;
  }

  auto out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_cuda_memory (out_mem)) {
    GST_ERROR_OBJECT (self, "Output is not a cuda memory");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&in_frame, &priv->in_info, inbuf,
          (GstMapFlags) (GST_MAP_CUDA | GST_MAP_READ))) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &priv->out_info, outbuf,
          (GstMapFlags) (GST_MAP_CUDA | GST_MAP_WRITE))) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return GST_FLOW_ERROR;
  }

  /* NOTE: GstCudaMemory can cache a texture object and can get
   * via gst_cuda_memory_get_texture(), but the texture is incompatible
   * with DeepStream API, especially GstCuda allocates texture object
   * with CU_TRSF_NORMALIZED_COORDINATES flag which indicates UV-like normalized
   * texture coordinates but DeepStream wants integer coordinates.
   * Needs to create new texture here */
  resource_desc.resType = CU_RESOURCE_TYPE_PITCH2D;
  resource_desc.res.pitch2D.format = CU_AD_FORMAT_UNSIGNED_INT8;
  resource_desc.res.pitch2D.numChannels = 4;
  resource_desc.res.pitch2D.width = priv->in_info.width;
  resource_desc.res.pitch2D.height = priv->in_info.height;
  resource_desc.res.pitch2D.pitchInBytes =
      GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, 0);
  resource_desc.res.pitch2D.devPtr = (CUdeviceptr)
      GST_VIDEO_FRAME_PLANE_DATA (&in_frame, 0);
  texture_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
  /* Read value as normalized float */
  texture_desc.flags = 0;
  texture_desc.addressMode[0] = (CUaddress_mode) 1;
  texture_desc.addressMode[1] = (CUaddress_mode) 1;
  texture_desc.addressMode[2] = (CUaddress_mode) 1;

  if (!gst_cuda_context_push (priv->context)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    gst_video_frame_unmap (&in_frame);
    gst_video_frame_unmap (&out_frame);
    return GST_FLOW_ERROR;
  }

  CUresult cuda_ret = CUDA_SUCCESS;
  auto in_cmem = GST_CUDA_MEMORY_CAST (in_mem);
  auto texture_data = (GstNvDsDewarpTextureData *)
      gst_cuda_memory_get_token_data (in_cmem, priv->texture_token);
  if (texture_data && texture_data->context == priv->context) {
    GST_LOG_OBJECT (self, "Have cached texture");
    texture = texture_data->texture;
  } else {
    GST_DEBUG_OBJECT (self, "Creating new texture object");

    cuda_ret = CuTexObjectCreate (&texture,
        &resource_desc, &texture_desc, nullptr);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (self, "Couldn't create texture object");
      gst_video_frame_unmap (&in_frame);
      gst_video_frame_unmap (&out_frame);
      gst_cuda_context_pop (nullptr);
      return GST_FLOW_ERROR;
    }

    texture_data = new GstNvDsDewarpTextureData ();
    texture_data->context = (GstCudaContext *) gst_object_ref (priv->context);
    texture_data->texture = texture;

    gst_cuda_memory_set_token_data (in_cmem, priv->texture_token, texture_data,
        [](gpointer user_data)->void
        {
          auto data = (GstNvDsDewarpTextureData *) user_data;
          gst_cuda_context_push (data->context);
          CuTexObjectDestroy (data->texture); gst_cuda_context_pop (nullptr);
          delete data;
        });
  }

  CUstream cuda_stream = 0;
  auto in_stream = gst_cuda_memory_get_stream (in_cmem);
  auto out_cmem = GST_CUDA_MEMORY_CAST (out_mem);
  auto out_stream = gst_cuda_memory_get_stream (out_cmem);
  GstCudaStream *selected_stream = nullptr;

  /* If downstream does not aware of CUDA stream (i.e., using default stream) */
  if (!out_stream) {
    if (in_stream) {
      GST_TRACE_OBJECT (self, "Use upstram CUDA stream");
      selected_stream = in_stream;
    } else if (priv->stream) {
      GST_TRACE_OBJECT (self, "Use our CUDA stream");
      selected_stream = priv->stream;
    }
  } else {
    selected_stream = out_stream;
    if (in_stream) {
      if (in_stream == out_stream) {
        GST_TRACE_OBJECT (self, "Same stream");
      } else {
        GST_TRACE_OBJECT (self, "Different CUDA stream");
        gst_cuda_memory_sync (in_cmem);
      }
    }
  }

  cuda_stream = gst_cuda_stream_get_handle (selected_stream);

  auto data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&out_frame, 0);
  auto stride = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 0);
  auto offset = stride * priv->out_rect.y +
      priv->out_rect.x * GST_VIDEO_FRAME_COMP_PSTRIDE (&out_frame, 0);

  if (priv->clear_background) {
    cuda_ret = CuMemsetD2D32Async ((CUdeviceptr) data, stride,
        ((guint32) 0xff) << 24, priv->out_info.width, priv->out_info.height,
        cuda_stream);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (self, "Couldn't clear background");
      gst_video_frame_unmap (&in_frame);
      gst_video_frame_unmap (&out_frame);
      gst_cuda_context_pop (nullptr);
      return GST_FLOW_ERROR;
    }
  }

  auto ret = nvwarpWarpBuffer (priv->handle, (cudaStream_t) cuda_stream,
      (cudaTextureObject_t) texture, data + offset, stride);
  if (selected_stream != out_stream) {
    GST_MEMORY_FLAG_UNSET (out_cmem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
    GST_TRACE_OBJECT (self, "Waiting for convert sync");
    CuStreamSynchronize (cuda_stream);
  }
  gst_cuda_context_pop (nullptr);

  GstFlowReturn flow_ret = GST_FLOW_OK;
  if (ret != NVWARP_SUCCESS) {
    auto error_str = nvwarpErrorStringFromCode (ret);
    GST_ERROR_OBJECT (self, "nvwarpWarpBuffer failed, %d (%s)", ret,
        GST_STR_NULL (error_str));
    flow_ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return flow_ret;
}
