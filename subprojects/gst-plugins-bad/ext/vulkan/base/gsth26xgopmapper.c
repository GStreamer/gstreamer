/* GStreamer
 * Copyright (C) 2021 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 * Copyright (C) 2025 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gsth26xgopmapper.h"

GST_DEBUG_CATEGORY (gst_debug_h26x_gop_mapper);
#define GST_CAT_DEFAULT gst_debug_h26x_gop_mapper

G_DEFINE_TYPE_WITH_CODE (GstH26XGOPMapper, gst_h26x_gop_mapper, GST_TYPE_OBJECT, {
      GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "h26xgopmapper", 0,
          "H26X GOP Mapper");
    }

);

static void
gst_h26x_gop_mapper_finalize (GObject * obj)
{
  GstH26XGOPMapper *self = GST_H26X_GOP_MAPPER (obj);

  g_clear_pointer (&self->frame_map, g_array_unref);

  G_OBJECT_CLASS (gst_h26x_gop_mapper_parent_class)->finalize (obj);
}

static void
gst_h26x_gop_mapper_class_init (GstH26XGOPMapperClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_h26x_gop_mapper_finalize;
}

static void
gst_h26x_gop_mapper_init (GstH26XGOPMapper * self)
{
}

GstH26XGOPMapper *
gst_h26x_gop_mapper_new (void)
{
  return g_object_new (GST_TYPE_H26X_GOP_MAPPER, NULL);
}

static const gchar *
gst_h26x_gop_type_to_string (gint type)
{
  const char *str[] = { "P", "B", "I" };

  if (type >= GST_H26X_GOP_TYPE_P && type <= GST_H26X_GOP_TYPE_I)
    return str[type];

  GST_ERROR ("unknown %d slice type", type);
  return NULL;
}

static void
gst_h26x_gop_mapper_log_map (GstH26XGOPMapper * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  GString *str;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_INFO)
    return;

  str = g_string_new (NULL);

  g_string_append_printf (str, "[ ");

  for (i = 0; i < self->params.idr_period; i++) {
    GstH26XGOP *pic = &g_array_index (self->frame_map, GstH26XGOP, i);

    if (i == 0) {
      g_string_append_printf (str, "IDR");
      continue;
    } else {
      g_string_append_printf (str, ", ");
    }

    g_string_append_printf (str, "%s", gst_h26x_gop_type_to_string (pic->type));

    if (self->params.b_pyramid && GST_H26X_GOP_IS (pic, B)) {
      g_string_append_printf (str, "<L%d (%d, %d)>", pic->pyramid_level,
          pic->left_ref_poc_diff, pic->right_ref_poc_diff);
    }

    if (pic->is_ref) {
      g_string_append_printf (str, "(ref)");
    }

  }

  g_string_append_printf (str, " ]");

  GST_INFO_OBJECT (self, "GOP size: %d / GOP structure: %s",
      self->params.idr_period, str->str);

  g_string_free (str, TRUE);
#endif
}

struct PyramidInfo
{
  guint level;
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

/* recursive function */
static void
gst_h26x_gop_mapper_set_b_pyramid_info (struct PyramidInfo *info, guint len,
    guint current_level, guint highest_level)
{
  guint index;

  g_assert (len >= 1 && len <= 31);

  if (current_level == highest_level || len == 1) {
    for (index = 0; index < len; index++) {
      info[index].level = current_level;
      info[index].left_ref_poc_diff = (index + 1) * -2;
      info[index].right_ref_poc_diff = (len - index) * 2;
    }

    return;
  }

  index = len / 2;
  info[index].level = current_level;
  info[index].left_ref_poc_diff = (index + 1) * -2;
  info[index].right_ref_poc_diff = (len - index) * 2;

  current_level++;

  if (index > 0) {
    gst_h26x_gop_mapper_set_b_pyramid_info (info, index, current_level,
        highest_level);
  }

  if (index + 1 < len) {
    gst_h26x_gop_mapper_set_b_pyramid_info (&info[index + 1], len - (index + 1),
        current_level, highest_level);
  }
}

static gboolean
gst_h26x_gop_mapper_validate_params (GstH26XGOPParameters * params)
{
  /* idr_period must be greater than 0 */
  if (params->idr_period == 0)
    return FALSE;

  if (params->b_pyramid && params->highest_pyramid_level == 0)
    return FALSE;

  /* num_bframes must be <= 31 (maximum supported by pyramid_info array) */
  if (params->highest_pyramid_level > 0
      && (params->num_bframes == 0 || params->num_bframes > 31))
    return FALSE;

  /* num_bframes must be equal to ip_period - 1 */
  if (params->num_bframes > 0 && params->num_bframes + 1 != params->ip_period)
    return FALSE;

  /* ip_period must be lesser than idr_period */
  if (params->ip_period > params->idr_period)
    return FALSE;

  /* i_period must be lesser than idr_period */
  if (params->i_period > params->idr_period)
    return FALSE;

  return TRUE;
}

void
gst_h26x_gop_mapper_generate (GstH26XGOPMapper * self)
{
  guint i;
  GstH26XGOPParameters *params;
  struct PyramidInfo pyramid_info[31] = { 0 };
  guint num_iframes;

  g_return_if_fail (GST_IS_H26X_GOP_MAPPER (self));

  params = &self->params;

  /* Validate parameters */
  if (!gst_h26x_gop_mapper_validate_params (&self->params)) {
    GST_WARNING_OBJECT (self, "Invalid GOP parameters");
    return;
  }

  if (params->highest_pyramid_level > 0) {
    gst_h26x_gop_mapper_set_b_pyramid_info (pyramid_info, params->num_bframes,
        0, params->highest_pyramid_level);
  }

  if (!self->frame_map) {
    self->frame_map =
        g_array_sized_new (TRUE, TRUE, sizeof (GstH26XGOP), params->idr_period);
  }
  self->frame_map = g_array_set_size (self->frame_map, params->idr_period);

  num_iframes = params->num_iframes;

  for (i = 0; i < params->idr_period; i++) {
    GstH26XGOP *pic = &g_array_index (self->frame_map, GstH26XGOP, i);

    if (i == 0) {
      pic->type = GST_H26X_GOP_TYPE_I;
      pic->is_ref = TRUE;
      continue;
    }

    /* Intra only stream. */
    if (params->ip_period == 0) {
      pic->type = GST_H26X_GOP_TYPE_I;
      pic->is_ref = FALSE;
      continue;
    }

    if (i % params->ip_period > 0) {
      pic->type = GST_H26X_GOP_TYPE_B;

      if (params->highest_pyramid_level > 0) {
        guint pyramid_index =
            i % params->ip_period - 1 /* The first P or IDR */ ;

        pic->pyramid_level = pyramid_info[pyramid_index].level;
        pic->is_ref = (pic->pyramid_level < params->highest_pyramid_level);
        pic->left_ref_poc_diff = pyramid_info[pyramid_index].left_ref_poc_diff;
        pic->right_ref_poc_diff =
            pyramid_info[pyramid_index].right_ref_poc_diff;
      } else {
        pic->pyramid_level = 0;
        pic->is_ref = FALSE;
        pic->left_ref_poc_diff = pic->right_ref_poc_diff = 0;
      }

      continue;
    }

    if (params->i_period > 0 && i % params->i_period == 0 && num_iframes > 0) {
      /* Replace P frames with I frames */
      pic->type = GST_H26X_GOP_TYPE_I;
      pic->is_ref = TRUE;
      num_iframes--;
      continue;
    }

    pic->type = GST_H26X_GOP_TYPE_P;
    pic->is_ref = TRUE;
  }

  /* XXX: Force the last one to be a P */
  if (params->idr_period > 1 && params->ip_period > 0) {
    GstH26XGOP *pic =
        &g_array_index (self->frame_map, GstH26XGOP, params->idr_period - 1);

    pic->type = GST_H26X_GOP_TYPE_P;
    pic->is_ref = TRUE;
  }

  gst_h26x_gop_mapper_log_map (self);
}

GstH26XGOP *
gst_h26x_gop_mapper_get_next (GstH26XGOPMapper * self)
{
  GstH26XGOP *ret;

  g_return_val_if_fail (GST_IS_H26X_GOP_MAPPER (self), NULL);

  if (!self->frame_map || self->frame_map->len == 0)
    gst_h26x_gop_mapper_generate (self);

  g_return_val_if_fail (self->frame_map->len > 0, NULL);
  g_return_val_if_fail (self->cur_frame_index <= self->frame_map->len, NULL);

  /* calculate next index */
  self->cur_frame_index %= self->params.idr_period;

  ret = &g_array_index (self->frame_map, GstH26XGOP, self->cur_frame_index++);

  return ret;
}

void
gst_h26x_gop_mapper_set_current_index (GstH26XGOPMapper * self,
    guint32 cur_frame_index)
{
  g_return_if_fail (GST_IS_H26X_GOP_MAPPER (self));
  g_return_if_fail (cur_frame_index < self->frame_map->len);

  self->cur_frame_index = MIN (cur_frame_index, self->frame_map->len - 1);
}

void
gst_h26x_gop_mapper_reset_index (GstH26XGOPMapper * self)
{
  g_return_if_fail (GST_IS_H26X_GOP_MAPPER (self));

  self->cur_frame_index = 0;
}

guint32
gst_h26x_gop_mapper_get_current_index (GstH26XGOPMapper * self)
{
  g_return_val_if_fail (GST_IS_H26X_GOP_MAPPER (self), -1);

  return self->cur_frame_index;
}

gboolean
gst_h26x_gop_mapper_is_last_current_index (GstH26XGOPMapper * self)
{
  g_return_val_if_fail (GST_IS_H26X_GOP_MAPPER (self), FALSE);

  return self->cur_frame_index == self->frame_map->len;
}

gboolean
gst_h26x_gop_mapper_set_params (GstH26XGOPMapper * self,
    GstH26XGOPParameters * params)
{
  g_return_val_if_fail (GST_IS_H26X_GOP_MAPPER (self), FALSE);
  g_return_val_if_fail (params != NULL, FALSE);

  if (!gst_h26x_gop_mapper_validate_params (params))
    return FALSE;

  self->params = *params;
  return TRUE;
}

void
gst_h26x_gop_mapper_reset (GstH26XGOPMapper * self)
{
  g_return_if_fail (GST_IS_H26X_GOP_MAPPER (self));

  self->cur_frame_index = 0;
  self->params = (GstH26XGOPParameters) {
  0,};

  if (self->frame_map)
    self->frame_map = g_array_set_size (self->frame_map, 0);
}
