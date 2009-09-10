/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __GST_SSIM_H__
#define __GST_SSIM_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

enum
{
  PROP_0,
  PROP_SSIM_TYPE,
  PROP_WINDOW_TYPE,
  PROP_WINDOW_SIZE,
  PROP_GAUSS_SIGMA,
};


#define GST_TYPE_SSIM            (gst_ssim_get_type())
#define GST_SSIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),            \
    GST_TYPE_SSIM,GstSSim))
#define GST_IS_SSIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),            \
    GST_TYPE_SSIM))
#define GST_SSIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,            \
    GST_TYPE_SSIM,GstSSimClass))
#define GST_IS_SSIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,            \
    GST_TYPE_SSIM))
#define GST_SSIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,            \
    GST_TYPE_SSIM,GstSSimClass))

typedef struct _GstSSim             GstSSim;
typedef struct _GstSSimClass        GstSSimClass;

typedef struct _GstSSimWindowCache {
  gint x_window_start;
  gint x_weight_start;
  gint x_window_end;
  gint y_window_start;
  gint y_weight_start;
  gint y_window_end;
  gfloat element_summ;
} GstSSimWindowCache;

typedef void (*GstSSimFunction) (GstSSim *ssim, guint8 *org, gfloat *orgmu,
    guint8 *mod, guint8 *out, gfloat *mean, gfloat *lowest, gfloat *highest);

typedef struct _GstSSimOutputContext GstSSimOutputContext;

/* TODO: check if all fields are used */
struct _GstSSimOutputContext {
  GstPad       *pad;
  gboolean      segment_pending;
};

/**
 * GstSSim:
 *
 * The ssim object structure.
 */
struct _GstSSim {
  GstElement      element;

  /* Array of GstSSimOutputContext */
  GPtrArray      *src;
  
  gint            padcount;

  GstCollectPads *collect;
  GstPad         *orig;

  gint            frame_rate;
  gint            frame_rate_base;
  gint            width;
  gint            height;
  GstCaps        *sinkcaps;
  GstCaps        *srccaps;

  /* SSIM type (0 - canonical; 1 - without mu) */
  gint            ssimtype;
  
  /* Size of a window, windows are square */
  gint            windowsize;

  /* Type of a weight-generator. 0 - no weighting. 1 - Gaussian weighting */
  gint            windowtype;

  /* Array of width*height GstSSimWindowCaches */
  GstSSimWindowCache *windows;

  /* Array of windowsize*windowsize gfloats */
  gfloat         *weights;

  /* For Gaussian function */
  gfloat          sigma;
  
  GstSSimFunction func;

  gfloat         const1;
  gfloat         const2;

  /* counters to keep track of timestamps */
  gint64          timestamp;
  gint64          offset;

  /* sink event handling */
  GstPadEventFunction  collect_event;
  GstSegment      segment;
  guint64         segment_position;
  gdouble         segment_rate;
};

struct _GstSSimClass {
  GstElementClass parent_class;
};

GType    gst_ssim_get_type (void);

G_END_DECLS

#endif /* __GST_SSIM_H__ */
