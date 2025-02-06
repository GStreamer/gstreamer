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
};

#define DEFAULT_DEVICE_ID -1
#define DEFAULT_WARP_TYPE GST_NV_DS_DEWARP_WARP_NONE
#define DEFAULT_ROTATION_AXES GST_NV_DS_DEWARP_AXES_YXZ
#define DEFAULT_TOP_ANGLE 90
#define DEFAULT_BOTTOM_ANGLE -90
#define DEFAULT_ANGLE 0
#define DEFAULT_FOV 180.0
#define DEFAULT_CONTROL 0.6

struct GstNvDsDewarpPrivate
{
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

  params.dstWidth = priv->out_info.width;
  params.dstHeight = priv->out_info.height;
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

static gboolean
gst_nv_ds_dewarp_set_caps (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  auto self = GST_NV_DS_DEWARP (trans);
  auto priv = self->priv;

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

  auto cuda_ret = CuTexObjectCreate (&texture,
      &resource_desc, &texture_desc, nullptr);
  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR_OBJECT (self, "Couldn't create texture object");
    gst_video_frame_unmap (&in_frame);
    gst_video_frame_unmap (&out_frame);
    return GST_FLOW_ERROR;
  }

  CUstream cuda_stream = 0;
  auto in_cmem = GST_CUDA_MEMORY_CAST (in_mem);
  auto stream = gst_cuda_memory_get_stream (in_cmem);
  if (stream)
    cuda_stream = gst_cuda_stream_get_handle (stream);
  else
    cuda_stream = gst_cuda_stream_get_handle (priv->stream);

  auto ret = nvwarpWarpBuffer (priv->handle, (cudaStream_t) cuda_stream,
      (cudaTextureObject_t) texture,
      GST_VIDEO_FRAME_PLANE_DATA (&out_frame, 0),
      GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 0));
  CuStreamSynchronize (cuda_stream);

  CuTexObjectDestroy (texture);
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
