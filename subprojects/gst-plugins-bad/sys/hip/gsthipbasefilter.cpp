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

#include "gsthipbasefilter.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_hip_base_filter_debug);
#define GST_CAT_DEFAULT gst_hip_base_filter_debug

/* cached quark to avoid contention on the global quark table lock */
#define META_TAG_VIDEO meta_tag_video_quark
static GQuark meta_tag_video_quark;

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_VENDOR,
};

#define DEFAULT_DEVICE_ID -1
#define DEFAULT_VENDOR GST_HIP_VENDOR_UNKNOWN

struct _GstHipBaseFilterPrivate
{
  ~_GstHipBaseFilterPrivate ()
  {
    gst_clear_caps (&in_caps);
    gst_clear_caps (&out_caps);
  }

  std::recursive_mutex lock;
  GstCaps *in_caps = nullptr;
  GstCaps *out_caps = nullptr;

  gint device_id = DEFAULT_DEVICE_ID;
  GstHipVendor vendor = DEFAULT_VENDOR;
};

#define gst_hip_base_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstHipBaseFilter, gst_hip_base_filter,
    GST_TYPE_BASE_TRANSFORM);

static void gst_hip_base_filter_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hip_base_filter_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_hip_base_filter_finalize (GObject * object);
static void gst_hip_base_filter_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_hip_base_filter_start (GstBaseTransform * trans);
static gboolean gst_hip_base_filter_stop (GstBaseTransform * trans);
static gboolean gst_hip_base_filter_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_hip_base_filter_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_hip_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static void gst_hip_base_filter_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static gboolean
gst_hip_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);

static void
gst_hip_base_filter_class_init (GstHipBaseFilterClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_hip_base_filter_finalize;
  object_class->set_property = gst_hip_base_filter_set_property;
  object_class->get_property = gst_hip_base_filter_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("device-id",
          "Device ID", "HIP device ID to use (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_VENDOR,
      g_param_spec_enum ("vendor", "Vendor", "Vendor type",
          GST_TYPE_HIP_VENDOR, GST_HIP_VENDOR_UNKNOWN,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_hip_base_filter_set_context);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_hip_base_filter_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_hip_base_filter_stop);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_hip_base_filter_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_hip_base_filter_get_unit_size);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_hip_base_filter_query);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_hip_base_filter_before_transform);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_hip_base_filter_transform_meta);

  GST_DEBUG_CATEGORY_INIT (gst_hip_base_filter_debug,
      "hipbasefilter", 0, "hipbasefilter");

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_BASE_FILTER, (GstPluginAPIFlags) 0);
  meta_tag_video_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_STR);
}

static void
gst_hip_base_filter_init (GstHipBaseFilter * self)
{
  self->priv = new GstHipBaseFilterPrivate ();
}

static void
gst_hip_base_filter_finalize (GObject * object)
{
  auto self = GST_HIP_BASE_FILTER (object);

  gst_clear_object (&self->device);
  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hip_base_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_BASE_FILTER (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    case PROP_VENDOR:
      priv->vendor = (GstHipVendor) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_base_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_BASE_FILTER (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    case PROP_VENDOR:
      g_value_set_enum (value, priv->vendor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_base_filter_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_HIP_BASE_FILTER (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_hip_handle_set_context (element, context, priv->vendor,
        priv->device_id, &self->device);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_hip_base_filter_start (GstBaseTransform * trans)
{
  auto self = GST_HIP_BASE_FILTER (trans);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!gst_hip_ensure_element_data (GST_ELEMENT (trans),
            priv->vendor, priv->device_id, &self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't get HIP device");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_hip_base_filter_stop (GstBaseTransform * trans)
{
  auto self = GST_HIP_BASE_FILTER (trans);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_clear_object (&self->device);
    gst_clear_caps (&priv->in_caps);
    gst_clear_caps (&priv->out_caps);
  }

  return TRUE;
}

static gboolean
gst_hip_base_filter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  auto self = GST_HIP_BASE_FILTER (trans);
  auto priv = self->priv;
  GstVideoInfo in_info, out_info;

  if (!self->device) {
    GST_ERROR_OBJECT (self, "HIP device is not configured");
    return FALSE;
  }

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps)) {
    GST_ERROR_OBJECT (self, "invalid incaps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps)) {
    GST_ERROR_OBJECT (self, "invalid incaps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  self->in_info = in_info;
  self->out_info = out_info;
  gst_caps_replace (&priv->in_caps, incaps);
  gst_caps_replace (&priv->out_caps, outcaps);

  auto klass = GST_HIP_BASE_FILTER_GET_CLASS (self);
  if (klass->set_info)
    return klass->set_info (self, incaps, &in_info, outcaps, &out_info);

  return TRUE;
}

static gboolean
gst_hip_base_filter_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static gboolean
gst_hip_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_HIP_BASE_FILTER (trans);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_hip_handle_context_query (GST_ELEMENT (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static void
gst_hip_base_filter_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  auto self = GST_HIP_BASE_FILTER (trans);
  auto priv = self->priv;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_hip_memory (mem))
    return;

  auto hmem = GST_HIP_MEMORY_CAST (mem);
  /* Same context, nothing to do */
  if (gst_hip_device_is_equal (self->device, hmem->device))
    return;

  GST_INFO_OBJECT (self, "Updating device %" GST_PTR_FORMAT " -> %"
      GST_PTR_FORMAT, self->device, hmem->device);

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_clear_object (&self->device);
    self->device = (GstHipDevice *) gst_object_ref (hmem->device);
  }

  /* subclass will update internal object.
   * Note that gst_base_transform_reconfigure() might not trigger this
   * unless caps was changed meanwhile */
  gst_hip_base_filter_set_caps (trans, priv->in_caps, priv->out_caps);

  /* Mark reconfigure so that we can update pool */
  gst_base_transform_reconfigure_src (trans);
}

static gboolean
gst_hip_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  auto info = meta->info;
  auto tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, META_TAG_VIDEO)))
    return TRUE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}
