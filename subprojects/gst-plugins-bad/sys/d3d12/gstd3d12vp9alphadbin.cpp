/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-d3d12vp9alphadecodebin
 * @title: d3d12vp9alphadecodebin
 *
 * A Direct3D12-based VP9 decoder with alpha support
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0.exe filesrc location=file.webm ! parsebin ! \
 *   d3d12vp9alphadecodebin ! d3d12compositor ! d3d12videosink
 * ```
 *
 * The above pipeline decodes a VP9 stream with alpha via d3d12vp9alphadecodebin,
 * then d3d12compositor blends the transparent areas with a checkerboard
 * pattern, and renders the final output via d3d12videosink.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/pbutils/pbutils.h>
#include "gstd3d12vp9alphadbin.h"
#include <string>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_vp9_alpha_dbin_debug);
#define GST_CAT_DEFAULT gst_d3d12_vp9_alpha_dbin_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

enum
{
  PROP_ADAPTER_LUID = 1,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

struct GstD3D12Vp9AlphaDbinCData
{
  GstCaps *sink_caps;
  gchar *factory_name;
  gint64 adapter_luid;
  guint device_id;
  guint vendor_id;
  gchar *description;
};

struct GstD3D12Vp9AlphaDbin
{
  GstBin parent;
  const gchar *missing_elem;
};

struct GstD3D12Vp9AlphaDbinClass
{
  GstBinClass parent_class;

  const gchar *factory_name;
  gint64 adapter_luid;
  guint device_id;
  guint vendor_id;
};

static GTypeClass *parent_class = nullptr;
#define GST_D3D12_VP9_ALPHA_DBIN(obj) ((GstD3D12Vp9AlphaDbin *) (obj))
#define GST_D3D12_VP9_ALPHA_DBIN_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),G_TYPE_FROM_INSTANCE (obj),GstD3D12Vp9AlphaDbinClass))

static void gst_d3d12_vp9_alpha_dbin_constructed (GObject * object);
static void gst_d3d12_vp9_alpha_dbin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn
gst_d3d12_vp9_alpha_dbin_change_state (GstElement * elem,
    GstStateChange transition);

static void
gst_d3d12_vp9_alpha_dbin_class_init (GstD3D12Vp9AlphaDbinClass * klass,
    gpointer data)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto elem_class = GST_ELEMENT_CLASS (klass);
  auto cdata = (GstD3D12Vp9AlphaDbinCData *) data;
  auto param_flags = (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->constructed = gst_d3d12_vp9_alpha_dbin_constructed;
  object_class->get_property = gst_d3d12_vp9_alpha_dbin_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of created device",
          G_MININT64, G_MAXINT64, 0, param_flags));

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0, param_flags));

  g_object_class_install_property (object_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0, param_flags));

  std::string long_name = "Direct3D12 " +
      std::string (cdata->description) + " VP9 Alpha Decodebin";
  std::string desc =
      "Direct3D12-based wrapper bin for VP9 with alpha decoding on " +
      std::string (cdata->description);

  gst_element_class_set_metadata (elem_class, long_name.c_str (),
      "Codec/Decoder/Video/Hardware", desc.c_str (),
      "Seungha Yang <seungha@centricular.com>");

  auto pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (elem_class, pad_templ);
  gst_element_class_add_static_pad_template (elem_class, &src_template);

  elem_class->change_state =
      GST_DEBUG_FUNCPTR (gst_d3d12_vp9_alpha_dbin_change_state);

  klass->factory_name = cdata->factory_name;
  klass->adapter_luid = cdata->adapter_luid;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);
}

static void
gst_d3d12_vp9_alpha_dbin_init (GstD3D12Vp9AlphaDbin * self)
{
}

static void
gst_d3d12_vp9_alpha_dbin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto klass = GST_D3D12_VP9_ALPHA_DBIN_GET_CLASS (object);
  switch (prop_id) {
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, klass->adapter_luid);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, klass->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, klass->vendor_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_vp9_alpha_dbin_constructed (GObject * object)
{
  auto self = GST_D3D12_VP9_ALPHA_DBIN (object);
  auto klass = GST_D3D12_VP9_ALPHA_DBIN_GET_CLASS (object);
  auto elem_class = GST_ELEMENT_CLASS (klass);
  auto elem = GST_ELEMENT (self);
  auto bin = GST_BIN (self);

  G_OBJECT_CLASS (parent_class)->constructed (object);

  auto templ = gst_element_class_get_pad_template (elem_class, "sink");
  auto sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_element_add_pad (elem, sinkpad);

  templ = gst_element_class_get_pad_template (elem_class, "src");
  auto srcpad = gst_ghost_pad_new_no_target_from_template ("src", templ);
  gst_element_add_pad (elem, srcpad);

  auto demux = gst_element_factory_make ("codecalphademux", nullptr);
  if (!demux) {
    self->missing_elem = "codecalphademux";
    return;
  }
  gst_bin_add (bin, demux);

  auto main_queue = gst_element_factory_make ("queue", nullptr);
  if (!main_queue) {
    self->missing_elem = "queue";
    return;
  }

  auto alpha_queue = gst_element_factory_make ("queue", nullptr);
  gst_bin_add_many (bin, main_queue, alpha_queue, nullptr);
  g_object_set (main_queue, "max-size-time", (guint64) 0, "max-size-bytes", 0,
      "max-size-buffers", 3, nullptr);
  g_object_set (alpha_queue, "max-size-time", (guint64) 0, "max-size-bytes", 0,
      "max-size-buffers", 3, nullptr);

  auto main_parse = gst_element_factory_make ("vp9parse", nullptr);
  if (!main_parse) {
    self->missing_elem = "vp9parse";
    return;
  }

  auto alpha_parse = gst_element_factory_make ("vp9parse", nullptr);
  gst_bin_add_many (bin, main_parse, alpha_parse, nullptr);

  auto main_dec = gst_element_factory_make (klass->factory_name, nullptr);
  if (!main_dec) {
    self->missing_elem = klass->factory_name;
    return;
  }

  auto alpha_dec = gst_element_factory_make (klass->factory_name, nullptr);
  gst_bin_add_many (bin, main_dec, alpha_dec, nullptr);

  g_object_set (main_dec, "qos", FALSE, nullptr);
  g_object_set (alpha_dec, "qos", FALSE, nullptr);

  auto combine = gst_element_factory_make ("d3d12alphacombine", nullptr);
  auto download = gst_element_factory_make ("d3d12download", nullptr);
  gst_bin_add_many (bin, combine, download, nullptr);

  auto pad = gst_element_get_static_pad (demux, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sinkpad), pad);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (download, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (srcpad), pad);
  gst_object_unref (pad);

  gst_element_link_pads (demux, "src", main_parse, "sink");
  gst_element_link_many (main_parse, main_queue, main_dec, nullptr);
  gst_element_link_pads (main_dec, "src", combine, "sink");

  gst_element_link_pads (demux, "alpha", alpha_parse, "sink");
  gst_element_link_many (alpha_parse, alpha_queue, alpha_dec, nullptr);
  gst_element_link_pads (alpha_dec, "src", combine, "alpha");

  gst_element_link (combine, download);
}

static GstStateChangeReturn
gst_d3d12_vp9_alpha_dbin_change_state (GstElement * elem,
    GstStateChange transition)
{
  auto self = GST_D3D12_VP9_ALPHA_DBIN (elem);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (self->missing_elem) {
        GST_WARNING_OBJECT (self, "%s element is missing", self->missing_elem);
        gst_element_post_message (elem, gst_missing_element_message_new (elem,
                self->missing_elem));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (elem, transition);
}

void
gst_d3d12_vp9_alpha_decodebin_register (GstPlugin * plugin,
    GstD3D12Device * device, guint rank, GstCaps * sink_caps,
    const gchar * factory_name)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  GTypeInfo type_info = {
    sizeof (GstD3D12Vp9AlphaDbinClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_d3d12_vp9_alpha_dbin_class_init,
    nullptr,
    nullptr,
    sizeof (GstD3D12Vp9AlphaDbin),
    0,
    (GInstanceInitFunc) gst_d3d12_vp9_alpha_dbin_init,
  };

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_vp9_alpha_dbin_debug,
      "d3d12vp9alphadecodebin", 0, "d3d12vp9alphadecodebin");

  auto cdata = g_new0 (GstD3D12Vp9AlphaDbinCData, 1);
  g_object_get (device, "adapter-luid", &cdata->adapter_luid,
      "device-id", &cdata->device_id, "vendor-id", &cdata->vendor_id,
      "description", &cdata->description, nullptr);
  cdata->factory_name = g_strdup (factory_name);
  cdata->sink_caps = gst_caps_copy (sink_caps);
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;

  type_name = g_strdup ("GstD3D12Vp9AlphaDecodebin");
  feature_name = g_strdup ("d3d12vp9alphadecodebin");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D12Vp9AlphaDevice%dDecodebin", index);
    feature_name = g_strdup_printf ("d3d12vp9alphadevice%ddecodebin", index);
  }

  type = g_type_register_static (GST_TYPE_BIN,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
