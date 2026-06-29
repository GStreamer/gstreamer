/* GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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
 * SECTION:element-amfvqenhancer
 * @title: amfvqenhancer
 * @short_description: AMD AMF based video quality enhancer
 *
 * Wraps AMF's `AMFVQEnhancer` component, exposing its FCR
 * (compression-artifact reduction) controls.
 * This component performs the following functions:
 * - Removing/reducing the blocking artifacts introduced by AVC/HEVC
 *   compression with low bit rate.
 * - Preserving details.
 * The element preserves the input resolution and pixel format.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamfvqenhancer.h"
#include "gstamfbasefilter.h"
#include "gstamfutils.h"

#include <components/VQEnhancer.h>
#include <core/Data.h>
#include <core/Factory.h>

#include <string>

GST_DEBUG_CATEGORY_STATIC (gst_amf_vq_enhancer_debug);
#define GST_CAT_DEFAULT gst_amf_vq_enhancer_debug

/* *INDENT-OFF* */
using namespace amf;
/* *INDENT-ON* */

typedef struct _GstAmfVQEnhancerClassData
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gint64 adapter_luid;
  guint device_index;
} GstAmfVQEnhancerClassData;

enum
{
  PROP_0,
  PROP_ATTENUATION,
  PROP_SPLIT_VIEW,
};

#define DEFAULT_ATTENUATION         VE_FCR_DEFAULT_ATTENUATION
#define DEFAULT_SPLIT_VIEW          FALSE

typedef struct _GstAmfVQEnhancer GstAmfVQEnhancer;
typedef struct _GstAmfVQEnhancerClass GstAmfVQEnhancerClass;

struct _GstAmfVQEnhancer
{
  GstAmfBaseFilter parent;

  gfloat attenuation;
  gboolean split_view;
};

struct _GstAmfVQEnhancerClass
{
  GstAmfBaseFilterClass parent_class;

  gint64 adapter_luid;
  guint device_index;
};

#define GST_AMF_VQE(object) ((GstAmfVQEnhancer *) (object))
#define GST_AMF_VQE_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstAmfVQEnhancerClass))

static void gst_amf_vq_enhancer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amf_vq_enhancer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const wchar_t *gst_amf_vq_enhancer_get_component_id (GstAmfBaseFilter *
    self);
static gboolean gst_amf_vq_enhancer_configure_component (GstAmfBaseFilter *
    self, AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info);

static void
gst_amf_vq_enhancer_class_init (GstAmfVQEnhancerClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAmfBaseFilterClass *base_class = GST_AMF_BASE_FILTER_CLASS (klass);
  GstAmfVQEnhancerClassData *cdata = (GstAmfVQEnhancerClassData *) data;

  gobject_class->set_property = gst_amf_vq_enhancer_set_property;
  gobject_class->get_property = gst_amf_vq_enhancer_get_property;

  g_object_class_install_property (gobject_class, PROP_ATTENUATION,
      g_param_spec_float ("attenuation", "Attenuation",
          "Enhancement strength: lower is subtler, higher is stronger",
          0.02f, 0.4f, DEFAULT_ATTENUATION,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SPLIT_VIEW,
      g_param_spec_boolean ("split-view", "Split View",
          "Experimental side-by-side view of the enhanced and original picture",
          DEFAULT_SPLIT_VIEW,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_element_class_set_static_metadata (element_class,
      "AMD AMF Video Quality Enhancer",
      "Filter/Effect/Video/Hardware",
      "Reduces AVC/HEVC compression (blocking) artifacts while preserving "
      "details using AMD AMF", "GStreamer AMF contributors");

  base_class->get_component_id =
      GST_DEBUG_FUNCPTR (gst_amf_vq_enhancer_get_component_id);
  base_class->configure_component =
      GST_DEBUG_FUNCPTR (gst_amf_vq_enhancer_configure_component);

  klass->adapter_luid = cdata->adapter_luid;
  klass->device_index = cdata->device_index;
}

static void
gst_amf_vq_enhancer_init (GstAmfVQEnhancer * self)
{
  GstAmfVQEnhancerClass *klass = GST_AMF_VQE_GET_CLASS (self);

  gst_amf_base_filter_set_subclass_data (GST_AMF_BASE_FILTER (self),
      klass->adapter_luid, klass->device_index);

  self->attenuation = DEFAULT_ATTENUATION;
  self->split_view = DEFAULT_SPLIT_VIEW;
}

static void
gst_amf_vq_enhancer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfVQEnhancer *self = GST_AMF_VQE (object);

  switch (prop_id) {
    case PROP_ATTENUATION:
      self->attenuation = g_value_get_float (value);
      break;
    case PROP_SPLIT_VIEW:
      self->split_view = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amf_vq_enhancer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfVQEnhancer *self = GST_AMF_VQE (object);

  switch (prop_id) {
    case PROP_ATTENUATION:
      g_value_set_float (value, self->attenuation);
      break;
    case PROP_SPLIT_VIEW:
      g_value_set_boolean (value, self->split_view);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const wchar_t *
gst_amf_vq_enhancer_get_component_id (GstAmfBaseFilter * self)
{
  return AMFVQEnhancer;
}

static gboolean
gst_amf_vq_enhancer_configure_component (GstAmfBaseFilter * filter,
    AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info)
{
  GstAmfVQEnhancer *self = GST_AMF_VQE (filter);
  AMF_RESULT result;

  result = comp->SetProperty (AMF_VIDEO_ENHANCER_ENGINE_TYPE,
#ifdef G_OS_WIN32
      (amf_int64) amf::AMF_MEMORY_DX11);
#else
      (amf_int64) amf::AMF_MEMORY_VULKAN);
#endif
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set engine type");

  result = comp->SetProperty (AMF_VE_FCR_ATTENUATION,
      (amf_double) self->attenuation);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set attenuation");

  result = comp->SetProperty (AMF_VE_FCR_SPLIT_VIEW,
      (amf_int64) (self->split_view ? 1 : 0));
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set split-view");

  return TRUE;
}

static GstCaps *
gst_amf_vq_enhancer_build_template_caps (AMFComponent * comp, gboolean is_input)
{
  GstCaps *caps;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr io_caps;
  AMF_RESULT result;
  amf_int32 min_w, max_w;
  amf_int32 min_h, max_h;
  std::string formats;

  result = comp->GetCaps (&amf_caps);
  if (result != AMF_OK)
    return nullptr;

  if (is_input)
    result = amf_caps->GetInputCaps (&io_caps);
  else
    result = amf_caps->GetOutputCaps (&io_caps);
  if (result != AMF_OK)
    return nullptr;

  io_caps->GetWidthRange (&min_w, &max_w);
  io_caps->GetHeightRange (&min_h, &max_h);

  if (min_w <= 0 || min_h <= 0 || max_w <= 0 || max_h <= 0)
    return nullptr;

  {
    amf_int32 num_fmt = io_caps->GetNumOfFormats ();
    gboolean first = TRUE;
    formats = "{ ";
    for (amf_int32 i = 0; i < num_fmt; i++) {
      AMF_SURFACE_FORMAT fmt;
      amf_bool native;
      const char *name = nullptr;

      if (io_caps->GetFormatAt (i, &fmt, &native) != AMF_OK)
        continue;

      switch (fmt) {
        case AMF_SURFACE_NV12:
          name = "NV12";
          break;
        case AMF_SURFACE_P010:
          name = "P010_10LE";
          break;
        case AMF_SURFACE_BGRA:
          name = "BGRA";
          break;
        case AMF_SURFACE_RGBA:
          name = "RGBA";
          break;
        default:
          break;
      }
      if (!name)
        continue;
      if (!first)
        formats += ", ";
      formats += name;
      first = FALSE;
    }
    if (first)
      return nullptr;
    formats += " }";
  }

  {
    gchar *caps_str = g_strdup_printf ("video/x-raw, format = %s, "
        "width = (int) [ %d, %d ], height = (int) [ %d, %d ]",
        formats.c_str (), min_w, max_w, min_h, max_h);
    caps = gst_caps_from_string (caps_str);
    g_free (caps_str);
  }
  if (!caps)
    return nullptr;

#ifdef G_OS_WIN32
  {
    GstCaps *d3d11_caps = gst_caps_copy (caps);
    for (guint j = 0; j < gst_caps_get_size (d3d11_caps); j++) {
      gst_caps_set_features (d3d11_caps, j,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    }
    gst_caps_append (d3d11_caps, caps);
    return d3d11_caps;
  }
#else
  return caps;
#endif
}

void
gst_amf_vq_enhancer_register (GstPlugin * plugin, GstObject * device,
    gpointer context, guint rank)
{
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;
  GstAmfVQEnhancerClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_amf_vq_enhancer_debug, "amfvqenhancer", 0,
      "amfvqenhancer");

  if (!factory)
    return;

  guint64 ver = gst_amf_get_version ();
  if (ver < AMF_MAKE_FULL_VERSION (1, 4, 28, 0)) {
    GST_ERROR_OBJECT (device,
        "AMFVQEnhancer added in AMF 1.4.28. Your version is %llu.%llu.%llu",
        AMF_GET_MAJOR_VERSION (ver), AMF_GET_MINOR_VERSION (ver),
        AMF_GET_SUBMINOR_VERSION (ver));
    return;
  }

  result = factory->CreateComponent (amf_context, AMFVQEnhancer, &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device,
        "Failed to create AMFVQEnhancer for registration probe, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }
#ifndef G_OS_WIN32
  /* AMF defaults the enhancer engine to D3D11; Linux AMF contexts are
   * Vulkan-only.  Hint the engine immediately so subsequent capability
   * queries / Init in real use line up with the underlying Vulkan AMF
   * context. */
  result = comp->SetProperty (AMF_VIDEO_ENHANCER_ENGINE_TYPE,
      (amf_int64) AMF_MEMORY_VULKAN);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device,
        "Failed to hint AMF_VIDEO_ENHANCER_ENGINE_TYPE = Vulkan on probe "
        "component, result %" GST_AMF_RESULT_FORMAT,
        GST_AMF_RESULT_ARGS (result));
  }
#endif

  cdata = g_new0 (GstAmfVQEnhancerClassData, 1);
#ifdef G_OS_WIN32
  if (GST_IS_D3D11_DEVICE (device)) {
    GstD3D11Device *d3ddev = GST_D3D11_DEVICE (device);
    g_object_get (d3ddev, "adapter-luid", &cdata->adapter_luid, nullptr);
  }
#endif

  cdata->sink_caps =
      gst_amf_vq_enhancer_build_template_caps (comp.GetPtr (), TRUE);
  cdata->src_caps =
      gst_amf_vq_enhancer_build_template_caps (comp.GetPtr (), FALSE);

  if (!cdata->sink_caps || !cdata->src_caps) {
    GST_WARNING_OBJECT (device, "Failed to build VQ enhancer template caps");
    if (cdata->sink_caps)
      gst_caps_unref (cdata->sink_caps);
    if (cdata->src_caps)
      gst_caps_unref (cdata->src_caps);
    g_free (cdata);
    return;
  }

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GType type;
  gchar *type_name = g_strdup ("GstAmfVQEnhancer");
  gchar *feature_name = g_strdup ("amfvqenhancer");
  gint index = 0;

  GTypeInfo type_info = {
    sizeof (GstAmfVQEnhancerClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_vq_enhancer_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfVQEnhancer),
    0,
    (GInstanceInitFunc) gst_amf_vq_enhancer_init,
  };

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfDevice%dVQEnhancer", index);
    feature_name = g_strdup_printf ("amfdevice%dvqenhancer", index);
  }

  type = g_type_register_static (GST_TYPE_AMF_BASE_FILTER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING_OBJECT (device, "Failed to register element '%s'",
        feature_name);

  g_free (type_name);
  g_free (feature_name);
}
