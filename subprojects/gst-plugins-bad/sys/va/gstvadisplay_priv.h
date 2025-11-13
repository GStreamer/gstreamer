/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
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

#include <gst/va/gstva.h>
#include <va/va.h>

G_BEGIN_DECLS
GArray *              gst_va_display_get_profiles         (GstVaDisplay * self,
                                                           guint32 codec,
                                                           VAEntrypoint entrypoint);
GArray *              gst_va_display_get_image_formats    (GstVaDisplay * self);
gboolean              gst_va_display_has_vpp              (GstVaDisplay * self);

gint32                gst_va_display_get_max_slice_num    (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_display_get_slice_structure  (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_display_get_max_num_reference
                                                          (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint,
                                                           guint32 * list0,
                                                           guint32 * list1);
guint32               gst_va_display_get_prediction_direction
                                                          (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_display_get_rate_control_mode
                                                          (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_display_get_quality_level    (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_display_has_trellis          (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_display_has_tile             (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_display_get_rtformat         (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean               gst_va_display_get_packed_headers  (GstVaDisplay * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint,
                                                           guint32 * packed_headers);


G_END_DECLS
