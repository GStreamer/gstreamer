/*
 *  gstvaapivalue.c - GValue implementations specific to VA-API
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2014 Intel Corporation
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

/**
 * SECTION:gstvaapivalue
 * @short_description: GValue implementations specific to VA-API
 */

#include "sysdeps.h"
#include <gobject/gvaluecollector.h>
#include "gstvaapivalue.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static gpointer
default_copy_func (gpointer data)
{
  return data;
}

static void
default_free_func (gpointer data)
{
}

/* --- GstVaapiPoint --- */

GType
gst_vaapi_point_get_type (void)
{
  static gsize g_type = 0;

  if (g_once_init_enter (&g_type)) {
    GType type =
        g_boxed_type_register_static (g_intern_static_string ("GstVaapiPoint"),
        default_copy_func, default_free_func);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* --- GstVaapiRectangle --- */

GType
gst_vaapi_rectangle_get_type (void)
{
  static gsize g_type = 0;

  if (g_once_init_enter (&g_type)) {
    GType type =
        g_boxed_type_register_static (g_intern_static_string
        ("GstVaapiRectangle"),
        default_copy_func, default_free_func);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* --- GstVaapiRenderMode --- */

GType
gst_vaapi_render_mode_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue render_modes[] = {
    {GST_VAAPI_RENDER_MODE_OVERLAY,
        "Overlay render mode", "overlay"},
    {GST_VAAPI_RENDER_MODE_TEXTURE,
        "Textured-blit render mode", "texture"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&g_type)) {
    GType type = g_enum_register_static ("GstVaapiRenderMode", render_modes);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* --- GstVaapiRotation --- */

GType
gst_vaapi_rotation_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue rotation_values[] = {
    {GST_VAAPI_ROTATION_0,
        "Unrotated mode", "0"},
    {GST_VAAPI_ROTATION_90,
        "Rotated by 90°, clockwise", "90"},
    {GST_VAAPI_ROTATION_180,
        "Rotated by 180°, clockwise", "180"},
    {GST_VAAPI_ROTATION_270,
        "Rotated by 270°, clockwise", "270"},
    {GST_VAAPI_ROTATION_AUTOMATIC,
        "Rotated by image-orientating tag°", "Automatic"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    GType type = g_enum_register_static ("GstVaapiRotation", rotation_values);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* --- GstVaapiRateControl --- */

GType
gst_vaapi_rate_control_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue rate_control_values[] = {
    {GST_VAAPI_RATECONTROL_NONE,
        "None", "none"},
    {GST_VAAPI_RATECONTROL_CQP,
        "Constant QP", "cqp"},
    {GST_VAAPI_RATECONTROL_CBR,
        "Constant bitrate", "cbr"},
    {GST_VAAPI_RATECONTROL_VCM,
        "Video conference", "vcm"},
    {GST_VAAPI_RATECONTROL_VBR,
        "Variable bitrate", "vbr"},
    {GST_VAAPI_RATECONTROL_VBR_CONSTRAINED,
        "Variable bitrate - Constrained", "vbr_constrained"},
    {GST_VAAPI_RATECONTROL_MB,
        "Macroblock based rate control", "mb"},
    {GST_VAAPI_RATECONTROL_ICQ,
        "Constant QP - Intelligent", "icq"},
    {GST_VAAPI_RATECONTROL_QVBR,
        "Variable bitrate - Quality defined", "qvbr"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    GType type = g_enum_register_static ("GstVaapiRateControl",
        rate_control_values);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

static gboolean
build_enum_subset_values_from_mask (GstVaapiEnumSubset * subset, guint32 mask)
{
  GEnumClass *enum_class;
  const GEnumValue *value;
  guint i, n;

  enum_class = g_type_class_ref (subset->parent_type);
  if (!enum_class)
    return FALSE;

  for (i = 0, n = 0; i < 32 && n < subset->num_values; i++) {
    if (!(mask & (1U << i)))
      continue;
    value = g_enum_get_value (enum_class, i);
    if (!value)
      continue;
    subset->values[n++] = *value;
  }
  g_type_class_unref (enum_class);
  if (n != subset->num_values - 1)
    goto error_invalid_num_values;
  return TRUE;

  /* ERRORS */
error_invalid_num_values:
  {
    GST_ERROR ("invalid number of static values for `%s'", subset->type_name);
    return FALSE;
  }
}

GType
gst_vaapi_type_define_enum_subset_from_mask (GstVaapiEnumSubset * subset,
    guint32 mask)
{
  if (g_once_init_enter (&subset->type)) {
    GType type;

    build_enum_subset_values_from_mask (subset, mask);
    memset (&subset->type_info, 0, sizeof (subset->type_info));
    g_enum_complete_type_info (subset->parent_type, &subset->type_info,
        subset->values);

    type = g_type_register_static (G_TYPE_ENUM, subset->type_name,
        &subset->type_info, 0);
    g_once_init_leave (&subset->type, type);
  }
  return subset->type;
}

/**
 * gst_vaapi_enum_type_get_nick:
 * @type: an enum #GType
 * @value: the value to get its nick
 *
 * Returns: (transfer none); the string associated with
 *   @value. Otherwise "<unknown>"
 **/
const gchar *
gst_vaapi_enum_type_get_nick (GType type, gint value)
{
  gpointer const klass = g_type_class_peek (type);
  GEnumValue *const e = g_enum_get_value (klass, value);

  if (e)
    return e->value_nick;
  return "<unknown>";
}
