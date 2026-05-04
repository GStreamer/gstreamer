/* Shared test helpers for inference element unit tests.
 *
 * Copyright (C) 2026 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.com>
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

#include <glib.h>

#include <gst/video/video.h>
#include <gst/analytics/analytics.h>

void fill_expected_flat_rgb_u8 (guint8 * out, gsize n_pixels,
    guint8 r, guint8 g, guint8 b);

void fill_expected_flat_rgb_i8 (gint8 * out, gsize n_pixels,
    gint8 r, gint8 g, gint8 b);

void fill_expected_flat_rgb_i32 (gint32 * out, gsize n_pixels,
    gint32 r, gint32 g, gint32 b);

void fill_expected_flat_rgb_f32 (gfloat * out, gsize n_pixels,
    gfloat r, gfloat g, gfloat b);

void fill_expected_gray_f32 (gfloat * out, gsize n_values,
    gfloat value);

void fill_expected_chw_rgb_f32 (gfloat * out, gsize n_pixels,
    gfloat r, gfloat g, gfloat b);

void assert_tensor_values_f32 (const GstTensor * tensor,
    const gfloat * expected, gsize n_values, gfloat epsilon,
    const gchar * file, gint line);

void assert_tensor_values_i32 (const GstTensor *tensor,
    const gint32 *expected, gsize n_values,
    const gchar *file, gint line);

void assert_tensor_values_u8 (const GstTensor *tensor, const guint8 *expected,
    gsize n_values, const gchar *file, gint line);

void assert_tensor_values_i8 (const GstTensor *tensor, const gint8 *expected,
    gsize n_values, const gchar *file, gint line);

gchar *setup_model_with_modelinfo (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name,
    const gchar * modelinfo_content);

gchar *setup_model_with_ranges (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name,
    const gchar * ranges);

gchar *setup_model_without_input_info (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name);

void cleanup_temp_model (gchar * model_path);

GstBuffer *create_solid_color_buffer (GstVideoFormat format,
    guint width, guint height, guint8 r, guint8 g, guint8 b, guint8 a);

GstBuffer *create_solid_color_buffer_aligned (GstVideoFormat format,
    GstAllocationParams * alloc_params, guint width, guint height, guint8 r,
    guint8 g, guint8 b, guint8 a);

GstBuffer *create_solid_gray_buffer (GstVideoFormat format,
    GstAllocationParams * alloc_params, guint width, guint height,
    guint8 value);

