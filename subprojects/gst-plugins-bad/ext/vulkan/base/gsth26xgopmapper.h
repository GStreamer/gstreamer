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

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstH26XGOP GstH26XGOP;
typedef struct _GstH26XGOPParameters GstH26XGOPParameters;
typedef struct _GstH26XGOPMapper GstH26XGOPMapper;

/**
 * GstH26XGOPType:
 *
 * Type of Picture slice.
 */
typedef enum
{
  GST_H26X_GOP_TYPE_P = 0,
  GST_H26X_GOP_TYPE_B = 1,
  GST_H26X_GOP_TYPE_I = 2
} GstH26XGOPType;

/**
 * GstH26XGOP:
 *
 * Description of an H.26X frame in the Group Of Pictures (GOP).
 *
 * Since: 1.30
 */
struct _GstH26XGOP
{
  /*< private >*/
  GstH26XGOPType type;
  gboolean is_ref;
  guint8 pyramid_level;

  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

/**
 * GstH26XGOPParameters:
 *
 * Parameters required to generate GOP map.
 */
struct _GstH26XGOPParameters
{
  /* frames between two IDR [idr, ...., idr) */
  guint32 idr_period;
  /* frames between I/P and P frames [I, B, B, .., B, P) */
  guint32 ip_period;
  /* frames between I frames [I, B, B, .., B, P, ..., I), open GOP */
  guint32 i_period;
  /* B frames between I/P and P. */
  guint32 num_bframes;
  /* Use B pyramid structure in the GOP. */
  gboolean b_pyramid;
  /* Level 0 is the simple B not acting as ref. */
  guint32 highest_pyramid_level;
  /* I frames within a GOP. */
  guint32 num_iframes;
};

/**
 * GstH26XGOPMapper:
 *
 * Object that creates a map of Group Of Pictures (GOP) given the H26X
 * parameters.
 *
 * Since: 1.30
 */
struct _GstH26XGOPMapper
{
  GstObject parent_instance;

  /*< private >*/
  GstH26XGOPParameters params;

  /* A map of all frames types within a GOP. */
  GArray *frame_map;
  /* current index in the frames types map. */
  guint32 cur_frame_index;
};

#define GST_TYPE_H26X_GOP_MAPPER gst_h26x_gop_mapper_get_type()

G_DECLARE_FINAL_TYPE (GstH26XGOPMapper, gst_h26x_gop_mapper, GST, H26X_GOP_MAPPER, GstObject)

GstH26XGOPMapper *   gst_h26x_gop_mapper_new                 (void);

void                 gst_h26x_gop_mapper_generate            (GstH26XGOPMapper * self);

GstH26XGOP *         gst_h26x_gop_mapper_get_next            (GstH26XGOPMapper * self);

void                 gst_h26x_gop_mapper_set_current_index   (GstH26XGOPMapper * self,
                                                              guint32 cur_frame_index);

guint32              gst_h26x_gop_mapper_get_current_index   (GstH26XGOPMapper * self);

void                 gst_h26x_gop_mapper_reset_index         (GstH26XGOPMapper * self);

gboolean             gst_h26x_gop_mapper_is_last_current_index
                                                             (GstH26XGOPMapper * self);

gboolean             gst_h26x_gop_mapper_set_params          (GstH26XGOPMapper *self,
                                                              GstH26XGOPParameters *params);

void                 gst_h26x_gop_mapper_reset               (GstH26XGOPMapper * self);

#define GST_H26X_GOP_IS(gop, type) (_gst_h26x_gop_is (gop, G_PASTE (GST_H26X_GOP_TYPE_, type)))
#define GST_H26X_GOP_IS_IDR(gop)   (_gst_h26x_gop_is_idr (gop))

static inline gboolean
_gst_h26x_gop_is (GstH26XGOP * gop, GstH26XGOPType type)
{
  return gop->type == type;
}

static inline gboolean
_gst_h26x_gop_is_idr (GstH26XGOP * gop)
{
  return (gop->type == GST_H26X_GOP_TYPE_I) && gop->is_ref;
}

G_END_DECLS
