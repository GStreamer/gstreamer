/* GstHarnessLink - A ref counted class arbitrating access to a
 * pad harness in a thread-safe manner.
 *
 * Copyright (C) 2023 Igalia S.L.
 * Copyright (C) 2023 Metrological
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_HARNESS_LINK_H__
#define __GST_HARNESS_LINK_H__

#include <gst/gst.h>
#include <gst/check/check-prelude.h>

G_BEGIN_DECLS

typedef struct _GstHarness GstHarness;

/**
 * GstHarnessLink: (skip)
 *
 * Opaque handle that can be used to release a pad lock over the harness.
 */
typedef struct _GstHarnessLink GstHarnessLink;

G_GNUC_INTERNAL
GType gst_harness_link_get_type (void);

G_GNUC_INTERNAL
void gst_harness_pad_link_set (GstPad* pad, GstHarness* harness);

G_GNUC_INTERNAL
GstHarnessLink* gst_harness_pad_link_lock (GstPad* pad, GstHarness** dst_harness);

G_GNUC_INTERNAL
void gst_harness_link_unlock (GstHarnessLink* link);

G_GNUC_INTERNAL
void gst_harness_pad_link_tear_down (GstPad* pad);

G_END_DECLS

#endif /* __GST_HARNESS_LINK_H__ */
