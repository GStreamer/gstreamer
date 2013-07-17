/*
 *  gstvaapifilter.h - Video processing abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPI_FILTER_H
#define GST_VAAPI_FILTER_H

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/video-format.h>

G_BEGIN_DECLS

typedef struct _GstVaapiFilter                  GstVaapiFilter;
typedef struct _GstVaapiFilterOpInfo            GstVaapiFilterOpInfo;

/**
 * @GST_VAAPI_FILTER_OP_FORMAT: Force output pixel format (#GstVideoFormat).
 * @GST_VAAPI_FILTER_OP_CROP: Crop source surface (#GstVaapiRectangle).
 * @GST_VAAPI_FILTER_OP_DENOISE: Noise reduction (float).
 * @GST_VAAPI_FILTER_OP_SHARPEN: Sharpening (float).
 *
 * The set of operations that could be applied to the filter.
 */
typedef enum {
    GST_VAAPI_FILTER_OP_FORMAT = 1,
    GST_VAAPI_FILTER_OP_CROP,
    GST_VAAPI_FILTER_OP_DENOISE,
    GST_VAAPI_FILTER_OP_SHARPEN,
} GstVaapiFilterOp;

/**
 * GstVaapiFilterOpInfo:
 * @operation: the #GstVaapiFilterOp
 * @pspec: the #GParamSpec describing the associated configurable value
 *
 * A #GstVaapiFilterOp descriptor.
 */
struct _GstVaapiFilterOpInfo {
    const GstVaapiFilterOp      op;
    GParamSpec * const          pspec;
};

/**
 * GstVaapiFilterStatus:
 * @GST_VAAPI_FILTER_STATUS_SUCCESS: Success.
 * @GST_VAAPI_FILTER_STATUS_ERROR_ALLOCATION_FAILED: No memory left.
 * @GST_VAAPI_FILTER_STATUS_ERROR_OPERATION_FAILED: Operation failed.
 * @GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER: Invalid parameter.
 * @GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_OPERATION: Unsupported operation.
 * @GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_FORMAT: Unsupported target format.
 *
 * Video processing status for gst_vaapi_filter_process().
 */
typedef enum {
    GST_VAAPI_FILTER_STATUS_SUCCESS = 0,
    GST_VAAPI_FILTER_STATUS_ERROR_ALLOCATION_FAILED,
    GST_VAAPI_FILTER_STATUS_ERROR_OPERATION_FAILED,
    GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER,
    GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_OPERATION,
    GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_FORMAT,
} GstVaapiFilterStatus;

GstVaapiFilter *
gst_vaapi_filter_new(GstVaapiDisplay *display);

GstVaapiFilter *
gst_vaapi_filter_ref(GstVaapiFilter *filter);

void
gst_vaapi_filter_unref(GstVaapiFilter *filter);

void
gst_vaapi_filter_replace(GstVaapiFilter **old_filter_ptr,
    GstVaapiFilter *new_filter);

GPtrArray *
gst_vaapi_filter_get_operations(GstVaapiFilter *filter);

gboolean
gst_vaapi_filter_set_operation(GstVaapiFilter *filter, GstVaapiFilterOp op,
    const GValue *value);

GstVaapiFilterStatus
gst_vaapi_filter_process(GstVaapiFilter *filter, GstVaapiSurface *src_surface,
    GstVaapiSurface *dst_surface, guint flags);

GArray *
gst_vaapi_filter_get_formats(GstVaapiFilter *filter);

gboolean
gst_vaapi_filter_set_format(GstVaapiFilter *filter, GstVideoFormat format);

gboolean
gst_vaapi_filter_set_cropping_rectangle(GstVaapiFilter *filter,
    const GstVaapiRectangle *rect);

gboolean
gst_vaapi_filter_set_denoising_level(GstVaapiFilter *filter, gfloat level);

gboolean
gst_vaapi_filter_set_sharpening_level(GstVaapiFilter *filter, gfloat level);

#endif /* GST_VAAPI_FILTER_H */
