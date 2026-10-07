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

#pragma once

#include <gst/hip/gsthip_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_HIP_KERNEL_MODULE (gst_hip_kernel_module_get_type ())

GST_HIP_API
GType gst_hip_kernel_module_get_type (void);

GST_HIP_API
GstHipKernelModule * gst_hip_kernel_module_new_from_source (GstHipDevice * device,
                                                            const gchar * source,
                                                            const gchar ** options,
                                                            guint num_options,
                                                            GError ** error);

GST_HIP_API
GstHipKernelModule * gst_hip_kernel_module_new_from_blob (GstHipDevice * device,
                                                          gconstpointer blob,
                                                          GError ** error);

GST_HIP_API
gboolean gst_hip_kernel_module_ensure_entry_point (GstHipKernelModule * module,
                                                   const gchar * entry_point);

GST_HIP_API
GstHipKernelModule * gst_hip_kernel_module_ref (GstHipKernelModule * module);

GST_HIP_API
void gst_hip_kernel_module_unref (GstHipKernelModule * module);

GST_HIP_API
gboolean gst_hip_kernel_module_launch (GstHipKernelModule * module,
                                       const gchar * entry_point,
                                       GstHipStream * stream,
                                       guint grid_x,
                                       guint grid_y,
                                       guint grid_z,
                                       guint block_x,
                                       guint block_y,
                                       guint block_z,
                                       const GstHipKernelArgs * args);

GST_HIP_API
GType gst_hip_kernel_args_get_type (void);

GST_HIP_API
GstHipKernelArgs * gst_hip_kernel_args_new (void);

GST_HIP_API
GstHipKernelArgs * gst_hip_kernel_args_copy (const GstHipKernelArgs * args);

GST_HIP_API
void gst_hip_kernel_args_free (GstHipKernelArgs * args);

GST_HIP_API
void gst_hip_kernel_args_clear (GstHipKernelArgs * args);

GST_HIP_API
gboolean gst_hip_kernel_args_add_memory (GstHipKernelArgs * args,
                                         GstMemory * memory,
                                         gsize offset);

GST_HIP_API
gboolean gst_hip_kernel_args_add_value (GstHipKernelArgs * args,
                                        gconstpointer value,
                                        gsize size,
                                        gsize alignment);

GST_HIP_API
void gst_hip_kernel_args_add_uint8 (GstHipKernelArgs * args,
                                    guint8 value);

GST_HIP_API
void gst_hip_kernel_args_add_int8 (GstHipKernelArgs * args,
                                   gint8 value);

GST_HIP_API
void gst_hip_kernel_args_add_uint16 (GstHipKernelArgs * args,
                                     guint16 value);

GST_HIP_API
void gst_hip_kernel_args_add_int16 (GstHipKernelArgs * args,
                                    gint16 value);

GST_HIP_API
void gst_hip_kernel_args_add_uint32 (GstHipKernelArgs * args,
                                     guint32 value);

GST_HIP_API
void gst_hip_kernel_args_add_int32 (GstHipKernelArgs * args,
                                    gint32 value);

GST_HIP_API
void gst_hip_kernel_args_add_uint64 (GstHipKernelArgs * args,
                                     guint64 value);

GST_HIP_API
void gst_hip_kernel_args_add_int64 (GstHipKernelArgs * args,
                                    gint64 value);

GST_HIP_API
void gst_hip_kernel_args_add_float (GstHipKernelArgs * args,
                                    gfloat value);

GST_HIP_API
void gst_hip_kernel_args_add_double (GstHipKernelArgs * args,
                                     gdouble value);

G_END_DECLS
