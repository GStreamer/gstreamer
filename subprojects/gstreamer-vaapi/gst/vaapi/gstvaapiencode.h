/*
 *  gstvaapiencode.h - VA-API video encoder
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPIENCODE_H
#define GST_VAAPIENCODE_H

#include "gstvaapipluginbase.h"
#include <gst/vaapi/gstvaapiencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE \
  (gst_vaapiencode_get_type ())
#define GST_VAAPIENCODE_CAST(obj) \
  ((GstVaapiEncode *)(obj))
#define GST_VAAPIENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIENCODE, GstVaapiEncode))
#define GST_VAAPIENCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIENCODE, GstVaapiEncodeClass))
#define GST_VAAPIENCODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPIENCODE, GstVaapiEncodeClass))
#define GST_IS_VAAPIENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE))
#define GST_IS_VAAPIENCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE))

typedef struct _GstVaapiEncodeInitData GstVaapiEncodeInitData;
struct _GstVaapiEncodeInitData
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
};

/* *INDENT-OFF* */
#define GST_VAAPI_ENCODE_STATIC_SINK_CAPS                \
  GST_VAAPI_MAKE_SURFACE_CAPS ", "                       \
  GST_CAPS_INTERLACED_FALSE "; "                         \
  GST_VIDEO_CAPS_MAKE (GST_VAAPI_FORMATS_ALL) ", "       \
  GST_CAPS_INTERLACED_FALSE ";"                          \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(                     \
      GST_CAPS_FEATURE_MEMORY_DMABUF,                    \
      GST_VAAPI_FORMATS_ALL) ","                         \
  GST_CAPS_INTERLACED_FALSE
/* *INDENT-ON* */

#define GST_VAAPI_ENCODE_REGISTER_TYPE(NAME, CODEC, CLASS, _EXT_FMT_, FUN) \
  static GstVaapiEncodeInitData encode_init_data = { NULL, NULL };         \
  static GType encode_type = G_TYPE_INVALID;                               \
  static gpointer gst_vaapiencode_##NAME##_parent_class = NULL;            \
  static void                                                              \
  gst_vaapiencode_##NAME##_class_init (                                    \
      GstVaapiEncode##CLASS##Class * klass, gpointer data);                \
  static void gst_vaapiencode_##NAME##_class_intern_init (gpointer klass,  \
      gpointer data)                                                       \
  {                                                                        \
    gst_vaapiencode_##NAME##_parent_class =                                \
        g_type_class_peek_parent (klass);                                  \
    gst_vaapiencode_##NAME##_class_init (klass, data);                     \
  }                                                                        \
  static void                                                              \
  gst_vaapiencode_##NAME##_init (GstVaapiEncode##CLASS * encode);          \
  GType                                                                    \
  gst_vaapiencode_##NAME##_register_type (GstVaapiDisplay * display)       \
  {                                                                        \
    GstCaps *caps;                                                         \
    guint i, n;                                                            \
    GTypeInfo type_info = {                                                \
      sizeof (GstVaapiEncodeClass),                                        \
      NULL,                                                                \
      NULL,                                                                \
      (GClassInitFunc) gst_vaapiencode_##NAME##_class_intern_init,         \
      NULL,                                                                \
      NULL,                                                                \
      sizeof (GstVaapiEncode##CLASS),                                      \
      0,                                                                   \
      (GInstanceInitFunc) gst_vaapiencode_##NAME##_init,                   \
    };                                                                     \
    GArray *extra_fmts = NULL;                                             \
    GstVideoFormat ext_video_fmts[] = _EXT_FMT_;                           \
                                                                           \
    GST_DEBUG_CATEGORY_INIT (gst_vaapi_##NAME##_encode_debug,              \
        GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);                              \
                                                                           \
    if ((n =  G_N_ELEMENTS (ext_video_fmts)))  {                           \
      extra_fmts =                                                         \
          g_array_sized_new (FALSE, FALSE, sizeof (GstVideoFormat), n);    \
      for (i = 0; i < n; i++)                                              \
        g_array_append_val (extra_fmts, ext_video_fmts[i]);                \
    }                                                                      \
    caps = gst_vaapi_build_template_raw_caps_by_codec (display,            \
        GST_VAAPI_CONTEXT_USAGE_ENCODE,                                    \
        GST_VAAPI_CODEC_##CODEC, extra_fmts);                              \
    g_clear_pointer (&extra_fmts, g_array_unref);                          \
    if (!caps) {                                                           \
      GST_ERROR ("failed to get sink caps for " #CODEC                     \
          " encode, can not register");                                    \
      return G_TYPE_INVALID;                                               \
    }                                                                      \
                                                                           \
    for (i = 0; i < gst_caps_get_size (caps); i++) {                       \
      GstStructure *structure = gst_caps_get_structure (caps, i);          \
      if (!structure)                                                      \
        continue;                                                          \
      gst_structure_set (structure, "interlace-mode", G_TYPE_STRING,       \
          "progressive", NULL);                                            \
    }                                                                      \
    GST_DEBUG (#CODEC" encode's sink caps %" GST_PTR_FORMAT, caps);        \
                                                                           \
    /* class data will be leaked if the element never gets instantiated */ \
    GST_MINI_OBJECT_FLAG_SET (caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);   \
    encode_init_data.sink_caps = caps;                                     \
                                                                           \
    caps = gst_vaapi_build_template_coded_caps_by_codec (display,          \
        GST_VAAPI_CONTEXT_USAGE_ENCODE,                                    \
        GST_VAAPI_CODEC_##CODEC, GST_CODEC_CAPS,                           \
        FUN);                                                              \
    if (!caps) {                                                           \
      GST_ERROR ("failed to get src caps for " #CODEC                      \
          " encode, can not register");                                    \
      gst_caps_unref (encode_init_data.sink_caps);                         \
      return G_TYPE_INVALID;                                               \
    }                                                                      \
    GST_DEBUG (#CODEC" encode's src caps %" GST_PTR_FORMAT, caps);         \
    /* class data will be leaked if the element never gets instantiated */ \
    GST_MINI_OBJECT_FLAG_SET (caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);   \
    encode_init_data.src_caps = caps;                                      \
    type_info.class_data = &encode_init_data;                              \
    encode_type = g_type_register_static (GST_TYPE_VAAPIENCODE,            \
        "GstVaapiEncode"#CLASS, &type_info, 0);                            \
                                                                           \
    return encode_type;                                                    \
  }                                                                        \
                                                                           \
  GType                                                                    \
  gst_vaapiencode_##NAME##_get_type (void)                                 \
  {                                                                        \
    g_assert (encode_type != G_TYPE_INVALID);                              \
    return encode_type;                                                    \
  }

typedef struct _GstVaapiEncode GstVaapiEncode;
typedef struct _GstVaapiEncodeClass GstVaapiEncodeClass;

struct _GstVaapiEncode
{
  /*< private >*/
  GstVaapiPluginBase parent_instance;

  GstVaapiEncoder *encoder;
  GstVideoCodecState *input_state;
  gboolean input_state_changed;
  /* needs to be set by the subclass implementation */
  gboolean need_codec_data;
  GstVideoCodecState *output_state;
  GPtrArray *prop_values;
  GstCaps *allowed_sinkpad_caps;
};

struct _GstVaapiEncodeClass
{
  /*< private >*/
  GstVaapiPluginBaseClass parent_class;

  guint               prop_num;
  gboolean            (*set_config)     (GstVaapiEncode * encode);
  GstCaps *           (*get_caps)       (GstVaapiEncode * encode);
  GstVaapiEncoder *   (*alloc_encoder)  (GstVaapiEncode * encode,
                                         GstVaapiDisplay * display);
  GstFlowReturn       (*alloc_buffer)   (GstVaapiEncode * encode,
                                         GstVaapiCodedBuffer * coded_buf,
                                         GstBuffer ** outbuf_ptr);
  /* Get all possible profiles based on allowed caps */
  GArray *            (*get_allowed_profiles)  (GstVaapiEncode * encode,
                                                GstCaps * allowed);
};

GType
gst_vaapiencode_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
void
gst_vaapiencode_set_property_subclass (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

G_GNUC_INTERNAL
void
gst_vaapiencode_get_property_subclass (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

G_GNUC_INTERNAL
gboolean
gst_vaapiencode_class_install_properties (GstVaapiEncodeClass * klass,
    GObjectClass * encoder_class);

G_END_DECLS

#endif /* GST_VAAPIENCODE_H */
