/* GStreamer
 * Copyright (C) 2021 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.com>
 *
 * gstfactories.h: A trace to log which plugin & factories are being used
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

#ifndef __GST_FACTORIES_TRACER_H__
#define __GST_FACTORIES_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(GstFactoriesTracer, gst_factories_tracer, GST,
    FACTORIES_TRACER, GstTracer)
/**
 * GstFactoriesTracer:
 *
 * Opaque #GstFactoriesTracer data structure
 */
struct _GstFactoriesTracer {
  GstTracer 	 parent;

  /*< private >*/
};

G_END_DECLS

#endif /* __GST_FACTORIES_TRACER_H__ */
