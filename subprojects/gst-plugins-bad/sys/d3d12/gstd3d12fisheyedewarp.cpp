/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12fisheyedewarp.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <memory>
#include <wrl.h>
#include <math.h>
#include <gst/d3dshader/gstd3dshader.h>
#include <DirectXMath.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace DirectX;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_fisheye_dewarp_debug);
#define GST_CAT_DEFAULT gst_d3d12_fisheye_dewarp_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

enum ProjectionType
{
  PROJECTION_PASSTHROUGH,
  PROJECTION_EQUIRECT,
  PROJECTION_PANORAMA,
  PROJECTION_PERSPECTIVE,
};

static GType
gst_d3d12_fisheye_dewarp_projection_type_get_type (void)
{
  static GType type = 0;
  static const GEnumValue types[] = {
    {PROJECTION_PASSTHROUGH, "Passthrough", "passthrough"},
    {PROJECTION_EQUIRECT, "Equirectangular", "equirect"},
    {PROJECTION_PANORAMA, "Panorama", "panorama"},
    {PROJECTION_PERSPECTIVE, "Perspective", "perspective"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12FisheyeDewarpProjectionType",
        types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum RotationSpace
{
  ROTATION_SPACE_LOCAL,
  ROTATION_SPACE_WORLD,
};

static GType
gst_d3d12_fisheye_dewarp_rotation_space_get_type (void)
{
  static GType type = 0;
  static const GEnumValue types[] = {
    {ROTATION_SPACE_LOCAL, "Local", "local"},
    {ROTATION_SPACE_WORLD, "World", "world"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12FisheyeDewarpRotationSpace", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum RotationOrder
{
  ROT_XYZ,
  ROT_XZY,
  ROT_YXZ,
  ROT_YZX,
  ROT_ZXY,
  ROT_ZYX,
};

static GType
gst_d3d12_fisheye_rotation_order_get_type (void)
{
  static GType type = 0;
  static const GEnumValue types[] = {
    {ROT_XYZ, "XYZ", "xyz"},
    {ROT_XZY, "XZY", "xzy"},
    {ROT_YXZ, "YXZ", "yxz"},
    {ROT_YZX, "YZX", "yzx"},
    {ROT_ZXY, "ZXY", "zxy"},
    {ROT_ZYX, "ZYX", "zyx"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12FisheyeDewarpRotationOrder", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum
{
  PROP_0,
  PROP_PROJ_TYPE,
  PROP_ROTATION_SPACE,
  PROP_CENTER_X,
  PROP_CENTER_Y,
  PROP_RADIUS_X,
  PROP_RADIUS_Y,
  PROP_VIEWPORT_X,
  PROP_VIEWPORT_Y,
  PROP_VIEWPORT_WIDTH,
  PROP_VIEWPORT_HEIGHT,
  PROP_ROI_X,
  PROP_ROI_Y,
  PROP_ROI_WIDTH,
  PROP_ROI_HEIGHT,
  PROP_FISHEYE_FOV,
  PROP_VERTICAL_FOV,
  PROP_HORIZONTAL_FOV,
  PROP_ROTATION_ORDER,
  PROP_ROTATION_X,
  PROP_ROTATION_Y,
  PROP_ROTATION_Z,
  PROP_INNER_RADIUS,
};

#define DEFAULT_PROJ_TYPE PROJECTION_EQUIRECT
#define DEFAULT_ROTATION_SPACE ROTATION_SPACE_LOCAL
#define DEFAULT_CENTER_X 0.5
#define DEFAULT_CENTER_Y 0.5
#define DEFAULT_RADIUS_X 0.5
#define DEFAULT_RADIUS_Y 0.5
#define DEFAULT_RECT_X 0.0
#define DEFAULT_RECT_Y 0.0
#define DEFAULT_RECT_WIDTH 1.0
#define DEFAULT_RECT_HEIGHT 1.0
#define DEFAULT_FISHEYE_FOV 180.0
#define DEFAULT_VERTICAL_FOV 90.0
#define DEFAULT_HORIZONTAL_FOV 90.0
#define DEFAULT_ROTATION_ORDER ROT_ZXY
#define DEFAULT_ANGLE 0.0
#define DEFAULT_INNER_RADIUS 0.3

/* *INDENT-OFF* */
struct DewarpRect
{
  double x = DEFAULT_RECT_X;
  double y = DEFAULT_RECT_Y;
  double width = DEFAULT_RECT_WIDTH;
  double height = DEFAULT_RECT_HEIGHT;
};

struct DewarpConstBuf
{
  XMFLOAT2 fisheyeCenter;
  XMFLOAT2 fisheyeRadius;

  FLOAT maxAngle;
  FLOAT horizontalFOV;
  FLOAT verticalFOV;
  FLOAT rollAngle;

  XMFLOAT2 roiOffset;
  XMFLOAT2 roiScale;

  FLOAT innerRadius;
  FLOAT invFocalLenX;
  FLOAT invFocalLenY;
  FLOAT padding;

  XMFLOAT4 RotationMatrixRow0;
  XMFLOAT4 RotationMatrixRow1;
  XMFLOAT4 RotationMatrixRow2;
};

struct DewarpContext
{
  ~DewarpContext()
  {
    if (fence_val) {
      gst_d3d12_device_fence_wait (device,
          D3D12_COMMAND_LIST_TYPE_DIRECT, fence_val);
    }

    gst_clear_object (&conv);
    gst_clear_object (&ca_pool);
    gst_clear_object (&desc_pool);
    gst_clear_object (&device);
  }

  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12PipelineState> pso_equirect;
  ComPtr<ID3D12PipelineState> pso_panorama;
  ComPtr<ID3D12PipelineState> pso_perspective;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12Resource> uv_remap;

  guint dispatch_x;
  guint dispatch_y;

  ID3D12Fence *cq_fence;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12DescHeapPool *desc_pool = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  guint64 fence_val = 0;
  GstD3D12Converter *conv = nullptr;
};

struct GstD3D12FisheyeDewarpPrivate
{
  GstD3D12FisheyeDewarpPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12FisheyeDewarpPrivate ()
  {
    gst_clear_object (&fence_data_pool);
  }

  GstD3D12FenceDataPool *fence_data_pool;

  std::shared_ptr<DewarpContext> ctx;

  gboolean prop_updated = FALSE;
  gboolean viewport_updated = FALSE;
  DewarpConstBuf cbuf;
  GstVideoRectangle original_viewport;

  ProjectionType proj_type = DEFAULT_PROJ_TYPE;
  RotationSpace rotation_space = DEFAULT_ROTATION_SPACE;
  double center[2] = { DEFAULT_CENTER_X, DEFAULT_CENTER_Y };
  double radius[2] = { DEFAULT_RADIUS_X, DEFAULT_RADIUS_Y };
  DewarpRect viewport;
  DewarpRect roi;
  double fisheye_fov = DEFAULT_FISHEYE_FOV;
  double vertical_fov = DEFAULT_VERTICAL_FOV;
  double horizontal_fov = DEFAULT_HORIZONTAL_FOV;
  RotationOrder rotation_order = DEFAULT_ROTATION_ORDER;
  double rotation_x = DEFAULT_ANGLE;
  double rotation_y = DEFAULT_ANGLE;
  double rotation_z = DEFAULT_ANGLE;
  double inner_radius = DEFAULT_INNER_RADIUS;

  std::recursive_mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12FisheyeDewarp
{
  GstD3D12BaseFilter parent;

  GstD3D12FisheyeDewarpPrivate *priv;
};

static void gst_d3d12_fisheye_dewarp_finalize (GObject * object);
static void gst_d3d12_fisheye_dewarp_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d12_fisheye_dewarp_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_d3d12_fisheye_dewarp_stop (GstBaseTransform * trans);
static gboolean gst_d3d12_fisheye_dewarp_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_fisheye_dewarp_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_d3d12_fisheye_dewarp_transform_meta (GstBaseTransform *
    trans, GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_d3d12_fisheye_dewarp_generate_output (GstBaseTransform
    * trans, GstBuffer ** buffer);
static GstFlowReturn gst_d3d12_fisheye_dewarp_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d12_fisheye_dewarp_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

#define gst_d3d12_fisheye_dewarp_parent_class parent_class
G_DEFINE_TYPE (GstD3D12FisheyeDewarp, gst_d3d12_fisheye_dewarp,
    GST_TYPE_D3D12_BASE_FILTER);

static void
gst_d3d12_fisheye_dewarp_class_init (GstD3D12FisheyeDewarpClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);
  GParamFlags param_flags =
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = gst_d3d12_fisheye_dewarp_set_property;
  object_class->get_property = gst_d3d12_fisheye_dewarp_get_property;
  object_class->finalize = gst_d3d12_fisheye_dewarp_finalize;

  g_object_class_install_property (object_class, PROP_PROJ_TYPE,
      g_param_spec_enum ("projection-type", "Projection Type",
          "Projection type to use",
          gst_d3d12_fisheye_dewarp_projection_type_get_type (),
          DEFAULT_PROJ_TYPE, param_flags));

  g_object_class_install_property (object_class, PROP_ROTATION_SPACE,
      g_param_spec_enum ("rotation-space", "Rotation Space",
          "Controls whether rotations are applied in local "
          "(intrinsic, camera-relative) or world (extrinsic, fixed-axis) space",
          gst_d3d12_fisheye_dewarp_rotation_space_get_type (),
          DEFAULT_ROTATION_SPACE, param_flags));

  g_object_class_install_property (object_class, PROP_CENTER_X,
      g_param_spec_double ("center-x", "Center X",
          "Normalized X position of fisheye circle",
          0, 1.0, DEFAULT_CENTER_X, param_flags));

  g_object_class_install_property (object_class, PROP_CENTER_Y,
      g_param_spec_double ("center-y", "Center Y",
          "Normalized Y position of fisheye circle",
          0, 1.0, DEFAULT_CENTER_Y, param_flags));

  g_object_class_install_property (object_class, PROP_RADIUS_X,
      g_param_spec_double ("radius-x", "Radius X",
          "Normalized horizontal radius of fisheye circle",
          0, 1.0, DEFAULT_RADIUS_X, param_flags));

  g_object_class_install_property (object_class, PROP_RADIUS_Y,
      g_param_spec_double ("radius-y", "Radius Y",
          "Normalized vertical radius of fisheye circle",
          0, 1.0, DEFAULT_RADIUS_Y, param_flags));

  g_object_class_install_property (object_class, PROP_VIEWPORT_X,
      g_param_spec_double ("viewport-x", "Viewport X",
          "Normalized top-left viewport X position",
          0, 1.0, DEFAULT_RECT_X, param_flags));

  g_object_class_install_property (object_class, PROP_VIEWPORT_Y,
      g_param_spec_double ("viewport-y", "Viewport Y",
          "Normalized top-left viewport Y position",
          0, 1.0, DEFAULT_RECT_Y, param_flags));

  g_object_class_install_property (object_class, PROP_VIEWPORT_WIDTH,
      g_param_spec_double ("viewport-width", "Viewport Width",
          "Normalized viewport width",
          0, 1.0, DEFAULT_RECT_WIDTH, param_flags));

  g_object_class_install_property (object_class, PROP_VIEWPORT_HEIGHT,
      g_param_spec_double ("viewport-height", "Viewport Height",
          "Normalized viewport height",
          0, 1.0, DEFAULT_RECT_HEIGHT, param_flags));

  g_object_class_install_property (object_class, PROP_ROI_X,
      g_param_spec_double ("roi-x", "ROI X",
          "Normalized horizontal ROI offset (top-left), in output image space",
          0, 1.0, DEFAULT_RECT_X, param_flags));

  g_object_class_install_property (object_class, PROP_ROI_Y,
      g_param_spec_double ("roi-y", "ROI Y",
          "Normalized vertical ROI offset (top-left), in output image space",
          0, 1.0, DEFAULT_RECT_Y, param_flags));

  g_object_class_install_property (object_class, PROP_ROI_WIDTH,
      g_param_spec_double ("roi-width", "ROI Width",
          "Normalized ROI width, in output image space",
          0, 1.0, DEFAULT_RECT_WIDTH, param_flags));

  g_object_class_install_property (object_class, PROP_ROI_HEIGHT,
      g_param_spec_double ("roi-height", "ROI Height",
          "Normalized ROI height, in output image space",
          0, 1.0, DEFAULT_RECT_HEIGHT, param_flags));

  g_object_class_install_property (object_class, PROP_FISHEYE_FOV,
      g_param_spec_double ("fisheye-fov", "Fisheye FOV",
          "Fisheye image field-of-view angle, in degrees",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_FISHEYE_FOV, param_flags));

  g_object_class_install_property (object_class, PROP_VERTICAL_FOV,
      g_param_spec_double ("vertical-fov", "Vertical FOV",
          "Vertical field-of-view angle of output, in degrees; "
          "ignored in 'panorama' projection",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_VERTICAL_FOV, param_flags));

  g_object_class_install_property (object_class, PROP_HORIZONTAL_FOV,
      g_param_spec_double ("horizontal-fov", "Horizontal FOV",
          "Horizontal field-of-view angle of output, in degrees; "
          "ignored in 'panorama' projection",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_HORIZONTAL_FOV, param_flags));

  g_object_class_install_property (object_class, PROP_ROTATION_ORDER,
      g_param_spec_enum ("rotation-order", "Rotation Order",
          "Rotation axis order to apply, ignored in 'panorama' projection",
          gst_d3d12_fisheye_rotation_order_get_type (),
          DEFAULT_ROTATION_ORDER, param_flags));

  g_object_class_install_property (object_class, PROP_ROTATION_X,
      g_param_spec_double ("rotation-x", "Rotation X",
          "Pitch (X-axis rotation) angle, in degrees; "
          "ignored in 'panorama' projection",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_ANGLE, param_flags));

  g_object_class_install_property (object_class, PROP_ROTATION_Y,
      g_param_spec_double ("rotation-y", "Rotation Y",
          "Yaw (Y-axis rotation) angle, in degrees; "
          "ignored in 'panorama' projection",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_ANGLE, param_flags));

  g_object_class_install_property (object_class, PROP_ROTATION_Z,
      g_param_spec_double ("rotation-z", "Rotation Z",
          "Roll (Z-axis rotation) angle, in degrees",
          -G_MAXDOUBLE, G_MAXDOUBLE, DEFAULT_ANGLE, param_flags));

  g_object_class_install_property (object_class, PROP_INNER_RADIUS,
      g_param_spec_double ("inner-radius", "Inner Radius",
          "Normalized inner radius for cropping central area "
          "(0.0 = center, 1.0 = full crop). Only used in 'panorama' projection",
          0.0, 1.0, DEFAULT_INNER_RADIUS, param_flags));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Fisheye Dewarp", "Filter/Converter/Video/Hardware",
      "Dewarping fisheye image", "Seungha Yang <seungha@centricular.com>");

  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_stop);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_decide_allocation);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_transform_meta);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_generate_output);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_transform);

  filter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d12_fisheye_dewarp_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SAMPLING_METHOD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_fisheye_dewarp_debug, "d3d12fisheyedewarp",
      0, "d3d12fisheyedewarp");
}

static void
gst_d3d12_fisheye_dewarp_init (GstD3D12FisheyeDewarp * self)
{
  self->priv = new GstD3D12FisheyeDewarpPrivate ();
}

static void
gst_d3d12_fisheye_dewarp_finalize (GObject * object)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_double_value (GstD3D12FisheyeDewarp * self, double *old_val,
    const GValue * new_val)
{
  auto priv = self->priv;
  auto tmp = g_value_get_double (new_val);

  if (tmp != *old_val) {
    priv->prop_updated = TRUE;
    *old_val = tmp;
  }
}

static void
update_viewport_value (GstD3D12FisheyeDewarp * self, double *old_val,
    const GValue * new_val)
{
  auto priv = self->priv;
  auto tmp = g_value_get_double (new_val);

  if (tmp != *old_val) {
    priv->viewport_updated = TRUE;
    *old_val = tmp;
  }
}

static void
gst_d3d12_fisheye_dewarp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PROJ_TYPE:
    {
      auto type = (ProjectionType) g_value_get_enum (value);
      if (type != priv->proj_type) {
        priv->proj_type = type;
        priv->prop_updated = TRUE;
      }
      break;
    }
    case PROP_ROTATION_SPACE:
    {
      auto space = (RotationSpace) g_value_get_enum (value);
      if (space != priv->rotation_space) {
        priv->rotation_space = space;
        priv->prop_updated = TRUE;
      }
      break;
    }
    case PROP_CENTER_X:
      update_double_value (self, &priv->center[0], value);
      break;
    case PROP_CENTER_Y:
      update_double_value (self, &priv->center[1], value);
      break;
    case PROP_RADIUS_X:
      update_double_value (self, &priv->radius[0], value);
      break;
    case PROP_RADIUS_Y:
      update_double_value (self, &priv->radius[1], value);
      break;
    case PROP_VIEWPORT_X:
      update_viewport_value (self, &priv->viewport.x, value);
      break;
    case PROP_VIEWPORT_Y:
      update_viewport_value (self, &priv->viewport.y, value);
      break;
    case PROP_VIEWPORT_WIDTH:
      update_viewport_value (self, &priv->viewport.width, value);
      break;
    case PROP_VIEWPORT_HEIGHT:
      update_viewport_value (self, &priv->viewport.height, value);
      break;
    case PROP_ROI_X:
      update_double_value (self, &priv->roi.x, value);
      break;
    case PROP_ROI_Y:
      update_double_value (self, &priv->roi.y, value);
      break;
    case PROP_ROI_WIDTH:
      update_double_value (self, &priv->roi.width, value);
      break;
    case PROP_ROI_HEIGHT:
      update_double_value (self, &priv->roi.height, value);
      break;
    case PROP_FISHEYE_FOV:
      update_double_value (self, &priv->fisheye_fov, value);
      break;
    case PROP_VERTICAL_FOV:
      update_double_value (self, &priv->vertical_fov, value);
      break;
    case PROP_HORIZONTAL_FOV:
      update_double_value (self, &priv->horizontal_fov, value);
      break;
    case PROP_ROTATION_ORDER:
    {
      auto order = (RotationOrder) g_value_get_enum (value);
      if (order != priv->rotation_order) {
        priv->rotation_order = order;
        priv->prop_updated = TRUE;
      }
      break;
    }
    case PROP_ROTATION_X:
      update_double_value (self, &priv->rotation_x, value);
      break;
    case PROP_ROTATION_Y:
      update_double_value (self, &priv->rotation_y, value);
      break;
    case PROP_ROTATION_Z:
      update_double_value (self, &priv->rotation_z, value);
      break;
    case PROP_INNER_RADIUS:
      update_double_value (self, &priv->inner_radius, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_fisheye_dewarp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PROJ_TYPE:
      g_value_set_enum (value, priv->proj_type);
      break;
    case PROP_ROTATION_SPACE:
      g_value_set_enum (value, priv->rotation_space);
      break;
    case PROP_CENTER_X:
      g_value_set_double (value, priv->center[0]);
      break;
    case PROP_CENTER_Y:
      g_value_set_double (value, priv->center[1]);
      break;
    case PROP_RADIUS_X:
      g_value_set_double (value, priv->radius[0]);
      break;
    case PROP_RADIUS_Y:
      g_value_set_double (value, priv->radius[1]);
      break;
    case PROP_VIEWPORT_X:
      g_value_set_double (value, priv->viewport.x);
      break;
    case PROP_VIEWPORT_Y:
      g_value_set_double (value, priv->viewport.y);
      break;
    case PROP_VIEWPORT_WIDTH:
      g_value_set_double (value, priv->viewport.width);
      break;
    case PROP_VIEWPORT_HEIGHT:
      g_value_set_double (value, priv->viewport.height);
      break;
    case PROP_ROI_X:
      g_value_set_double (value, priv->roi.x);
      break;
    case PROP_ROI_Y:
      g_value_set_double (value, priv->roi.y);
      break;
    case PROP_ROI_WIDTH:
      g_value_set_double (value, priv->roi.width);
      break;
    case PROP_ROI_HEIGHT:
      g_value_set_double (value, priv->roi.height);
      break;
    case PROP_FISHEYE_FOV:
      g_value_set_double (value, priv->fisheye_fov);
      break;
    case PROP_VERTICAL_FOV:
      g_value_set_double (value, priv->vertical_fov);
      break;
    case PROP_HORIZONTAL_FOV:
      g_value_set_double (value, priv->horizontal_fov);
      break;
    case PROP_ROTATION_ORDER:
      g_value_set_enum (value, priv->rotation_order);
      break;
    case PROP_ROTATION_X:
      g_value_set_double (value, priv->rotation_x);
      break;
    case PROP_ROTATION_Y:
      g_value_set_double (value, priv->rotation_y);
      break;
    case PROP_ROTATION_Z:
      g_value_set_double (value, priv->rotation_z);
      break;
    case PROP_INNER_RADIUS:
      g_value_set_double (value, priv->inner_radius);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_fisheye_dewarp_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (trans);
  auto priv = self->priv;

  priv->ctx = nullptr;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_d3d12_fisheye_dewarp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_D3D12_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint n_pools, i;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query)) {
    return FALSE;
  }

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, nullptr, nullptr,
        nullptr);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (filter->device);

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (filter->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_NONE);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    gst_d3d12_allocation_params_unset_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  /* size will be updated by d3d12 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_d3d12_fisheye_dewarp_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto filter = GST_D3D12_BASE_FILTER (trans);
  GstCaps *outcaps = nullptr;
  GstBufferPool *pool = nullptr;
  guint size, min = 0, max = 0;
  GstStructure *config;
  gboolean update_pool = FALSE;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  GstD3D12Format device_format;
  if (!gst_d3d12_device_get_format (filter->device,
          GST_VIDEO_INFO_FORMAT (&info), &device_format)) {
    GST_ERROR_OBJECT (filter, "Couldn't get device foramt");
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if ((device_format.format_flags & GST_D3D12_FORMAT_FLAG_OUTPUT_UAV)
      == GST_D3D12_FORMAT_FLAG_OUTPUT_UAV) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  if ((device_format.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (filter->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
        D3D12_HEAP_FLAG_SHARED);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        resource_flags);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
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

static HRESULT
gst_d3d12_fisheye_dewarp_get_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob = nullptr;
  static HRESULT hr = S_OK;

  GST_D3D12_CALL_ONCE_BEGIN {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_ROOT_PARAMETER root_params[2];
    CD3DX12_DESCRIPTOR_RANGE range_uav;

    root_params[0].InitAsConstants (sizeof (DewarpConstBuf) / 4, 0);

    range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    root_params[1].InitAsDescriptorTable (1, &range_uav);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 2, root_params,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    ComPtr < ID3DBlob > error_blob;
    hr = D3DX12SerializeVersionedRootSignature (&desc,
        D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &error_blob);
    if (!gst_d3d12_result (hr, device)) {
      const gchar *error_msg = nullptr;
      if (error_blob)
        error_msg = (const gchar *) error_blob->GetBufferPointer ();

      GST_ERROR_OBJECT (device,
          "Couldn't serialize rs, hr: 0x%x, error detail: %s",
          (guint) hr, GST_STR_NULL (error_msg));
    }
  } GST_D3D12_CALL_ONCE_END;

  if (rs_blob) {
    *blob = rs_blob;
    rs_blob->AddRef ();
  }

  return hr;
}

static inline float
fmod_angle (double angle)
{
  return (float) fmod (fmod (angle, 360.0f) + 360.0f, 360.0f);
}

static gboolean
gst_d3d12_fisheye_dewarp_update_cbuf (GstD3D12FisheyeDewarp * self)
{
  auto priv = self->priv;

  if (!priv->prop_updated)
    return TRUE;

  priv->cbuf.fisheyeCenter.x = (FLOAT) priv->center[0];
  priv->cbuf.fisheyeCenter.y = (FLOAT) priv->center[1];
  priv->cbuf.fisheyeRadius.x = (FLOAT) priv->radius[0];
  priv->cbuf.fisheyeRadius.y = (FLOAT) priv->radius[1];

  priv->cbuf.maxAngle =
      XMConvertToRadians (fmod_angle (priv->fisheye_fov) * 0.5f);
  priv->cbuf.horizontalFOV =
      XMConvertToRadians (fmod_angle (priv->horizontal_fov));
  priv->cbuf.verticalFOV = XMConvertToRadians (fmod_angle (priv->vertical_fov));

  priv->cbuf.roiOffset.x = (FLOAT) priv->roi.x;
  priv->cbuf.roiOffset.y = (FLOAT) priv->roi.y;
  priv->cbuf.roiScale.x = (FLOAT) priv->roi.width;
  priv->cbuf.roiScale.y = (FLOAT) priv->roi.height;

  priv->cbuf.innerRadius = priv->inner_radius;
  priv->cbuf.invFocalLenX = tanf (priv->cbuf.horizontalFOV * 0.5f);
  priv->cbuf.invFocalLenY = tanf (priv->cbuf.verticalFOV * 0.5f);

  auto pitch_angle = XMConvertToRadians (fmod_angle (priv->rotation_x));
  auto yaw_angle = XMConvertToRadians (fmod_angle (priv->rotation_y));
  auto roll_angle = XMConvertToRadians (fmod_angle (priv->rotation_z));

  priv->cbuf.rollAngle = roll_angle;

  auto rx = XMMatrixRotationX (pitch_angle);
  auto ry = XMMatrixRotationY (yaw_angle);
  auto rz = XMMatrixRotationZ (roll_angle);

  XMMATRIX m = XMMatrixIdentity ();
  if (priv->rotation_space == ROTATION_SPACE_WORLD) {
    switch (priv->rotation_order) {
      case ROT_XYZ:
        m = rx * ry * rz;
        break;
      case ROT_XZY:
        m = rx * rz * ry;
        break;
      case ROT_YXZ:
        m = ry * rx * rz;
        break;
      case ROT_YZX:
        m = ry * rz * rx;
        break;
      case ROT_ZXY:
        m = rz * rx * ry;
        break;
      case ROT_ZYX:
        m = rz * ry * rx;
        break;
    }
  } else {
    switch (priv->rotation_order) {
      case ROT_XYZ:
        m = rz * ry * rx;
        break;
      case ROT_XZY:
        m = ry * rz * rx;
        break;
      case ROT_YXZ:
        m = rz * rx * ry;
        break;
      case ROT_YZX:
        m = rx * rz * ry;
        break;
      case ROT_ZXY:
        m = ry * rx * rz;
        break;
      case ROT_ZYX:
        m = rx * ry * rz;
        break;
    }
  }

  XMFLOAT3X3 mat3x3;
  XMStoreFloat3x3 (&mat3x3, m);

  priv->cbuf.RotationMatrixRow0 =
      XMFLOAT4 (mat3x3._11, mat3x3._12, mat3x3._13, 0.0f);
  priv->cbuf.RotationMatrixRow1 =
      XMFLOAT4 (mat3x3._21, mat3x3._22, mat3x3._23, 0.0f);
  priv->cbuf.RotationMatrixRow2 =
      XMFLOAT4 (mat3x3._31, mat3x3._32, mat3x3._33, 0.0f);

  return TRUE;
}

static void
get_viewport (GstD3D12FisheyeDewarp * self, GstVideoRectangle * viewport)
{
  auto priv = self->priv;

  if (priv->original_viewport.w > 0 && priv->original_viewport.h > 0) {
    double x = priv->viewport.x;
    double y = priv->viewport.y;
    double w = priv->viewport.width;
    double h = priv->viewport.height;

    /* Ensure normalized coordinate */
    x = CLAMP (x, 0.0, 1.0);
    y = CLAMP (y, 0.0, 1.0);
    w = CLAMP (w, 0.0, 1.0);
    h = CLAMP (h, 0.0, 1.0);

    /* Scale to real viewport size */
    gint xi = (gint) round ((double) priv->original_viewport.w * x) +
        priv->original_viewport.x;
    gint yi = (gint) round ((double) priv->original_viewport.h * y) +
        priv->original_viewport.y;
    gint wi = (gint) round ((double) priv->original_viewport.w * w);
    gint hi = (gint) round ((double) priv->original_viewport.h * h);

    viewport->x = xi;
    viewport->y = yi;
    viewport->w = wi;
    viewport->h = hi;
  } else {
    viewport->x = 0;
    viewport->y = 0;
    viewport->w = 0;
    viewport->h = 0;
  }
}

static gboolean
gst_d3d12_fisheye_dewarp_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (filter);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  if (priv->ctx) {
    if (!gst_d3d12_device_is_equal (priv->ctx->device, filter->device)) {
      priv->ctx = nullptr;
    } else {
      gst_d3d12_device_fence_wait (priv->ctx->device,
          D3D12_COMMAND_LIST_TYPE_DIRECT, priv->ctx->fence_val);
      gst_clear_object (&priv->ctx->conv);
    }
  }

  if (priv->ctx && priv->ctx->uv_remap) {
    auto desc = GetDesc (priv->ctx->uv_remap);
    if ((gint) desc.Width != in_info->width ||
        (gint) desc.Height != in_info->height) {
      priv->ctx->uv_remap = nullptr;
    }
  }

  if (!priv->ctx) {
    auto ctx = std::make_shared < DewarpContext > ();
    ctx->device = (GstD3D12Device *) gst_object_ref (filter->device);
    auto device = gst_d3d12_device_get_device_handle (filter->device);
    ctx->ca_pool = gst_d3d12_cmd_alloc_pool_new (device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = { };
    desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc_heap_desc.NumDescriptors = 1;
    desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ctx->desc_pool = gst_d3d12_desc_heap_pool_new (device, &desc_heap_desc);

    ctx->cq = gst_d3d12_device_get_cmd_queue (ctx->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    ctx->cq_fence = gst_d3d12_cmd_queue_get_fence_handle (ctx->cq);

    ComPtr < ID3DBlob > rs_blob;
    auto hr = gst_d3d12_fisheye_dewarp_get_rs_blob (filter->device, &rs_blob);
    if (!gst_d3d12_result (hr, filter->device))
      return FALSE;

    hr = device->CreateRootSignature (0, rs_blob->GetBufferPointer (),
        rs_blob->GetBufferSize (), IID_PPV_ARGS (&ctx->rs));
    if (!gst_d3d12_result (hr, filter->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create root signature");
      return FALSE;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };

    GstD3DShaderByteCode cs_code;
    if (!gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_FISHEYE_EQUIRECT,
            GST_D3D_SM_5_0, &cs_code)) {
      GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
      return FALSE;
    }

    pso_desc.pRootSignature = ctx->rs.Get ();
    pso_desc.CS.pShaderBytecode = cs_code.byte_code;
    pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
    hr = device->CreateComputePipelineState (&pso_desc,
        IID_PPV_ARGS (&ctx->pso_equirect));
    if (!gst_d3d12_result (hr, filter->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create PSO");
      return FALSE;
    }

    if (!gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_FISHEYE_PANORAMA,
            GST_D3D_SM_5_0, &cs_code)) {
      GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
      return FALSE;
    }

    pso_desc.CS.pShaderBytecode = cs_code.byte_code;
    pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
    hr = device->CreateComputePipelineState (&pso_desc,
        IID_PPV_ARGS (&ctx->pso_panorama));
    if (!gst_d3d12_result (hr, filter->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create PSO");
      return FALSE;
    }

    if (!gst_d3d_plugin_shader_get_cs_blob
        (GST_D3D_PLUGIN_CS_FISHEYE_PERSPECTIVE, GST_D3D_SM_5_0, &cs_code)) {
      GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
      return FALSE;
    }

    pso_desc.CS.pShaderBytecode = cs_code.byte_code;
    pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
    hr = device->CreateComputePipelineState (&pso_desc,
        IID_PPV_ARGS (&ctx->pso_perspective));
    if (!gst_d3d12_result (hr, filter->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create PSO");
      return FALSE;
    }

    priv->ctx = std::move (ctx);
  }

  auto & ctx = priv->ctx;
  if (!ctx->uv_remap) {
    D3D12_HEAP_PROPERTIES heap_prop =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC desc =
        CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_R16G16B16A16_UNORM,
        in_info->width, in_info->height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

    auto device = gst_d3d12_device_get_device_handle (ctx->device);
    auto hr = device->CreateCommittedResource (&heap_prop,
        gst_d3d12_device_non_zeroed_supported (ctx->device) ?
        D3D12_HEAP_FLAG_CREATE_NOT_ZEROED : D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS (&ctx->uv_remap));
    if (!gst_d3d12_result (hr, ctx->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create LUT texture");
      return FALSE;
    }
  }

  ctx->conv = gst_d3d12_converter_new (ctx->device, nullptr,
      in_info, out_info, nullptr, nullptr, nullptr);

  priv->original_viewport.x = 0;
  priv->original_viewport.y = 0;
  priv->original_viewport.w = out_info->width;
  priv->original_viewport.h = out_info->height;

  GstVideoRectangle viewport;
  get_viewport (self, &viewport);
  gst_d3d12_converter_update_viewport (ctx->conv, viewport.x, viewport.y,
      viewport.w, viewport.h);

  ctx->dispatch_x = (in_info->width + 7) / 8;
  ctx->dispatch_y = (in_info->height + 7) / 8;

  /* need to build LUT later */
  priv->prop_updated = TRUE;
  priv->viewport_updated = FALSE;

  return TRUE;
}

static gboolean
gst_d3d12_fisheye_dewarp_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  if (meta->info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
      outbuf, meta, inbuf);
}

static GstFlowReturn
gst_d3d12_fisheye_dewarp_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!trans->queued_buf)
    return GST_FLOW_OK;

  if (priv->proj_type != PROJECTION_PASSTHROUGH) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
        buffer);
  }

  *buffer = trans->queued_buf;
  trans->queued_buf = nullptr;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_fisheye_dewarp_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_D3D12_FISHEYE_DEWARP (trans);
  auto priv = self->priv;
  GstD3D12CmdAlloc *gst_ca;
  GstD3D12FenceData *fence_data;
  auto ctx = priv->ctx;
  HRESULT hr;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  g_assert (priv->proj_type != PROJECTION_PASSTHROUGH);

  if (!ctx) {
    GST_ERROR_OBJECT (self, "Context is not configured");
    return GST_FLOW_ERROR;
  }

  if (!gst_d3d12_fisheye_dewarp_update_cbuf (self)) {
    GST_ERROR_OBJECT (self, "Couldn't update constant buffer");
    return GST_FLOW_ERROR;
  }

  auto device = gst_d3d12_device_get_device_handle (ctx->device);

  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  hr = ca->Reset ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (!ctx->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&priv->ctx->cl));
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (priv->prop_updated) {
    GstD3D12DescHeap *heap;
    if (!gst_d3d12_desc_heap_pool_acquire (ctx->desc_pool, &heap)) {
      GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
      gst_d3d12_fence_data_unref (fence_data);
      return GST_FLOW_ERROR;
    }

    auto heap_handle = gst_d3d12_desc_heap_get_handle (heap);
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (heap));

    auto device = gst_d3d12_device_get_device_handle (ctx->device);

    auto cpu_handle = GetCPUDescriptorHandleForHeapStart (heap_handle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView (ctx->uv_remap.Get (),
        nullptr, &uav_desc, cpu_handle);

    ID3D12DescriptorHeap *heaps[] = { heap_handle };

    ctx->cl->SetComputeRootSignature (ctx->rs.Get ());
    switch (priv->proj_type) {
      case PROJECTION_EQUIRECT:
        ctx->cl->SetPipelineState (ctx->pso_equirect.Get ());
        break;
      case PROJECTION_PANORAMA:
        ctx->cl->SetPipelineState (ctx->pso_panorama.Get ());
        break;
      case PROJECTION_PERSPECTIVE:
        ctx->cl->SetPipelineState (ctx->pso_perspective.Get ());
        break;
      default:
        g_assert_not_reached ();
        return GST_FLOW_ERROR;
    }

    ctx->cl->SetDescriptorHeaps (1, heaps);
    ctx->cl->SetComputeRoot32BitConstants (0, sizeof (DewarpConstBuf) / 4,
        &priv->cbuf, 0);
    ctx->cl->SetComputeRootDescriptorTable (1,
        GetGPUDescriptorHandleForHeapStart (heap_handle));
    ctx->cl->Dispatch (ctx->dispatch_x, ctx->dispatch_y, 1);

    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (ctx->uv_remap.Get (),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx->cl->ResourceBarrier (1, &barrier);

    priv->prop_updated = FALSE;
  }

  gst_d3d12_converter_set_remap (ctx->conv, ctx->uv_remap.Get ());

  if (priv->viewport_updated) {
    GstVideoRectangle viewport;
    get_viewport (self, &viewport);
    gst_d3d12_converter_update_viewport (ctx->conv, viewport.x, viewport.y,
        viewport.w, viewport.h);
    priv->viewport_updated = FALSE;
  }

  if (!gst_d3d12_converter_convert_buffer (ctx->conv, inbuf, outbuf, fence_data,
          ctx->cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't convert buffer");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    gst_d3d12_fence_data_unref (fence_data);
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cl[] = { ctx->cl.Get () };
  gst_d3d12_cmd_queue_execute_command_lists (ctx->cq, 1, cl, &ctx->fence_val);

  gst_d3d12_cmd_queue_set_notify (ctx->cq, ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  gst_d3d12_buffer_set_fence (outbuf, ctx->cq_fence, ctx->fence_val, FALSE);

  return GST_FLOW_OK;
}
