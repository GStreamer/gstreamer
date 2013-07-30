/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-override-registry.h - QA Override registry
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_QA_OVERRIDE_REGISTRY_H__
#define __GST_QA_OVERRIDE_REGISTRY_H__

#include <glib-object.h>
#include <gst/gst.h>
#include "gst-qa-report.h"
#include "gst-qa-monitor.h"
#include "gst-qa-override.h"

G_BEGIN_DECLS

typedef struct {
  GMutex mutex;

  GQueue name_overrides;
  GQueue gtype_overrides;
  GQueue klass_overrides;
} GstQaOverrideRegistry;

GstQaOverrideRegistry * gst_qa_override_registry_get (void);

void gst_qa_override_register_by_name (const gchar * name, GstQaOverride * override);
void gst_qa_override_register_by_type (GType gtype, GstQaOverride * override);
void gst_qa_override_register_by_klass (const gchar * klass, GstQaOverride * override);

void gst_qa_override_registry_attach_overrides (GstQaMonitor * monitor);

int gst_qa_override_registry_preload (void);

G_END_DECLS

#endif /* __GST_QA_OVERRIDE_REGISTRY_H__ */

