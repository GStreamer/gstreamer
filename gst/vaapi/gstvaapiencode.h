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

  GPtrArray *         (*get_properties) (void);
  gboolean            (*get_property)   (GstVaapiEncode * encode,
                                         guint prop_id, GValue * value);
  gboolean            (*set_property)   (GstVaapiEncode * encode,
                                         guint prop_id, const GValue * value);

  gboolean            (*set_config)     (GstVaapiEncode * encode);
  GstCaps *           (*get_caps)       (GstVaapiEncode * encode);
  GstVaapiEncoder *   (*alloc_encoder)  (GstVaapiEncode * encode,
                                         GstVaapiDisplay * display);
  GstFlowReturn       (*alloc_buffer)   (GstVaapiEncode * encode,
                                         GstVaapiCodedBuffer * coded_buf,
                                         GstBuffer ** outbuf_ptr);
  GstVaapiProfile     (*get_profile)    (GstCaps * caps);
};

GType
gst_vaapiencode_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
gboolean
gst_vaapiencode_init_properties (GstVaapiEncode * encode);

G_GNUC_INTERNAL
gboolean
gst_vaapiencode_class_init_properties (GstVaapiEncodeClass * encode_class);

G_END_DECLS

#endif /* GST_VAAPIENCODE_H */
