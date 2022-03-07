/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-override-registry.h - Validate Override registry
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

#ifndef __GST_VALIDATE_OVERRIDE_REGISTRY_H__
#define __GST_VALIDATE_OVERRIDE_REGISTRY_H__

#include <glib-object.h>
#include <gst/gst.h>

#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-monitor.h>
#include <gst/validate/gst-validate-override.h>

G_BEGIN_DECLS

typedef struct {
  GMutex mutex;

  GQueue name_overrides;
  GQueue gtype_overrides;
  GQueue klass_overrides;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
} GstValidateOverrideRegistry;

GST_VALIDATE_API
GstValidateOverrideRegistry * gst_validate_override_registry_get (void);

GST_VALIDATE_API GList *
gst_validate_override_registry_get_override_for_names (GstValidateOverrideRegistry *reg,
        const gchar *name, ...);
GST_VALIDATE_API GList *
gst_validate_override_registry_get_override_list (GstValidateOverrideRegistry *registry);
GST_VALIDATE_API
void gst_validate_override_register_by_name (const gchar * name, GstValidateOverride * override);
GST_VALIDATE_API
void gst_validate_override_register_by_type (GType gtype, GstValidateOverride * override);
GST_VALIDATE_API
void gst_validate_override_register_by_klass (const gchar * klass, GstValidateOverride * override);

GST_VALIDATE_API
void gst_validate_override_registry_attach_overrides (GstValidateMonitor * monitor);

GST_VALIDATE_API
int gst_validate_override_registry_preload (void);

G_END_DECLS

#endif /* __GST_VALIDATE_OVERRIDE_REGISTRY_H__ */

