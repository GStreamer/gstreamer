/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#ifndef __GST_ALPHA_DECODE_BIN_H__
#define __GST_ALPHA_DECODE_BIN_H__

#include <gst/gst.h>
#include <gst/video/video-prelude.h>

/**
 * GST_ALPHA_DECODE_BIN_RANK_OFFSET:
 *
 * Rank offset for alpha decode bin wrappers.
 *
 * When wrapping a decoder, use the original decoder rank plus this offset. The
 * offset is large enough to let an alpha wrapper outrank small rank adjustments
 * used by hardware decoders, while keeping lower-ranked wrappers below the
 * primary rank threshold.
 *
 * Since: 1.30
 */
#define GST_ALPHA_DECODE_BIN_RANK_OFFSET 10

G_BEGIN_DECLS

/**
 * GstAlphaDecodeBinRole:
 * @GST_ALPHA_DECODE_BIN_ROLE_DEMUX: the element that separates the encoded
 *     color stream from the encoded alpha stream
 * @GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER: the decoder for the encoded color
 *     stream
 * @GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER: the decoder for the encoded alpha
 *     stream
 * @GST_ALPHA_DECODE_BIN_ROLE_COMBINE: the element that combines decoded color
 *     and alpha streams into one decoded stream
 * @GST_ALPHA_DECODE_BIN_ROLE_COUNT: the number of alpha decode bin roles
 *
 * The element roles used to construct a #GstAlphaDecodeBin subclass.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_ALPHA_DECODE_BIN_ROLE_DEMUX,
  GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER,
  GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER,
  GST_ALPHA_DECODE_BIN_ROLE_COMBINE,
  GST_ALPHA_DECODE_BIN_ROLE_COUNT,
} GstAlphaDecodeBinRole;

/**
 * GST_TYPE_ALPHA_DECODE_BIN:
 *
 * The #GType for #GstAlphaDecodeBin.
 *
 * Since: 1.30
 */
#define GST_TYPE_ALPHA_DECODE_BIN (gst_alpha_decode_bin_get_type())

/**
 * GstAlphaDecodeBin:
 *
 * Base class for alpha decode bin wrapper elements.
 *
 * Since: 1.30
 */
/**
 * GstAlphaDecodeBin.parent_instance:
 *
 * Parent instance.
 *
 * Since: 1.30
 */
GST_VIDEO_API
G_DECLARE_DERIVABLE_TYPE (GstAlphaDecodeBin,
    gst_alpha_decode_bin, GST, ALPHA_DECODE_BIN, GstBin);

/**
 * GstAlphaDecodeBinClass.parent_class:
 *
 * Parent class.
 *
 * Since: 1.30
 */
/**
 * GstAlphaDecodeBinClass:
 * @parent_class: parent #GstBinClass
 *
 * Base class for bins that decode streams with encoded alpha data by
 * demuxing, decoding the color and alpha streams separately, and combining the
 * decoded output.
 *
 * Subclasses must add their own sink pad template. For each
 * #GstAlphaDecodeBinRole, subclasses can either create an element from
 * @create_role_element or configure a fallback factory name with
 * gst_alpha_decode_bin_class_set_role_factory_name().
 *
 * Since: 1.30
 */
struct _GstAlphaDecodeBinClass
{
  GstBinClass parent_class;

  /**
   * GstAlphaDecodeBinClass::create_role_element:
   * @self: a #GstAlphaDecodeBin
   * @role: a #GstAlphaDecodeBinRole
   * @input_caps: (nullable): upstream caps allowed on the alpha decode bin
   *     sink pad
   * @error: (out) (optional): return location for a #GError
   *
   * Called when constructing the internal pipeline to let subclasses create an
   * element for the requested #GstAlphaDecodeBinRole. Subclasses can use
   * @input_caps to select role elements for the encoded stream before data
   * flow starts. If the vfunc returns %NULL without setting @error, the
   * factory name configured with
   * gst_alpha_decode_bin_class_set_role_factory_name() is used as fallback.
   * If the vfunc returns %NULL and sets @error, construction fails. If the
   * vfunc returns an element and sets @error, the error is logged as a warning
   * and ignored.
   *
   * Returns: (transfer full) (nullable): a role element, or %NULL to use the
   *     configured fallback factory
   *
   * Since: 1.30
   */
  GstElement * (*create_role_element) (GstAlphaDecodeBin * self,
      GstAlphaDecodeBinRole role, const GstCaps * input_caps,
      GError ** error);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * gst_alpha_decode_bin_class_set_role_factory_name:
 * @klass: a #GstAlphaDecodeBinClass
 * @role: a #GstAlphaDecodeBinRole
 * @factory_name: (nullable): element factory name to use for @role, or %NULL
 *     to unset the configured factory
 *
 * Sets the element factory name to use for @role when constructing instances
 * of @klass.
 *
 * This is usually called from a subclass' class init function. The configured
 * factory name is used when the subclass does not implement
 * #GstAlphaDecodeBinClass.create_role_element, or when that vfunc returns
 * %NULL without setting an error.
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_alpha_decode_bin_class_set_role_factory_name
    (GstAlphaDecodeBinClass * klass, GstAlphaDecodeBinRole role,
    const gchar * factory_name);

G_END_DECLS
#endif
