/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-override.h - Validate Override that allows customizing Validate behavior
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

#ifndef __GST_VALIDATE_OVERRIDE_H__
#define __GST_VALIDATE_OVERRIDE_H__

#include <glib-object.h>
#include <gst/gst.h>

typedef struct _GstValidateOverride GstValidateOverride;
typedef struct _GstValidateOverrideClass GstValidateOverrideClass;
typedef struct _GstValidateOverridePrivate GstValidateOverridePrivate;


#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-monitor.h>

G_BEGIN_DECLS

typedef void (*GstValidateOverrideBufferHandler)(GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstBuffer * buffer);
typedef void (*GstValidateOverrideEventHandler)(GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstEvent * event);
typedef void (*GstValidateOverrideQueryHandler)(GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstQuery * query);
typedef void (*GstValidateOverrideGetCapsHandler)(GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstCaps * caps);
typedef void (*GstValidateOverrideSetCapsHandler)(GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstCaps * caps);
typedef void (*GstValidateOverrideElementAddedHandler)(GstValidateOverride * override,
    GstValidateMonitor * bin_monitor, GstElement * new_child);

struct _GstValidateOverrideClass
{
  /*<private>*/
  GstObjectClass parent_class;

  gboolean (*can_attach)(GstValidateOverride * override,
      GstValidateMonitor * monitor);

  void (*attached)(GstValidateOverride * override);

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstValidateOverride
{
  GstObject parent;

  GstValidateOverrideBufferHandler buffer_handler;
  GstValidateOverrideEventHandler event_handler;
  GstValidateOverrideQueryHandler query_handler;
  GstValidateOverrideBufferHandler buffer_probe_handler;
  GstValidateOverrideGetCapsHandler getcaps_handler;
  GstValidateOverrideSetCapsHandler setcaps_handler;
  GstValidateOverrideElementAddedHandler element_added_handler;

  /*<private>*/
  GstValidateOverridePrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

GST_VALIDATE_API
GType gst_validate_override_get_type (void) G_GNUC_CONST;

/* TYPE MACROS */
#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_OVERRIDE (gst_validate_override_get_type ())
#define GST_VALIDATE_OVERRIDE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VALIDATE_OVERRIDE, GstValidateOverride))
#define GST_VALIDATE_OVERRIDE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VALIDATE_OVERRIDE, GstValidateOverrideClass))
#define GST_IS_VALIDATE_OVERRIDE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VALIDATE_OVERRIDE))
#define GST_IS_VALIDATE_OVERRIDE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VALIDATE_OVERRIDE))
#define GST_VALIDATE_OVERRIDE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_OVERRIDE, GstValidateOverrideClass))
#endif

GST_VALIDATE_API
GstValidateOverride *    gst_validate_override_new (void);

void               gst_validate_override_free (GstValidateOverride * override);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstValidateOverride, gst_validate_override_free)

GST_VALIDATE_API
void               gst_validate_override_change_severity (GstValidateOverride * override, GstValidateIssueId issue_id, GstValidateReportLevel new_level);
GST_VALIDATE_API
GstValidateReportLevel   gst_validate_override_get_severity (GstValidateOverride * override, GstValidateIssueId issue_id, GstValidateReportLevel default_level);

GST_VALIDATE_API
void               gst_validate_override_event_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstEvent * event);
GST_VALIDATE_API
void               gst_validate_override_buffer_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstBuffer * buffer);
GST_VALIDATE_API
void               gst_validate_override_query_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstQuery * query);
GST_VALIDATE_API
void               gst_validate_override_buffer_probe_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstBuffer * buffer);
GST_VALIDATE_API
void               gst_validate_override_getcaps_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstCaps * caps);
GST_VALIDATE_API
void               gst_validate_override_setcaps_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstCaps * caps);

GST_VALIDATE_API
void               gst_validate_override_set_event_handler (GstValidateOverride * override, GstValidateOverrideEventHandler handler);
GST_VALIDATE_API
void               gst_validate_override_set_buffer_handler (GstValidateOverride * override, GstValidateOverrideBufferHandler handler);
GST_VALIDATE_API
void               gst_validate_override_set_query_handler (GstValidateOverride * override, GstValidateOverrideQueryHandler handler);
GST_VALIDATE_API
void               gst_validate_override_set_buffer_probe_handler (GstValidateOverride * override, GstValidateOverrideBufferHandler handler);
GST_VALIDATE_API
void               gst_validate_override_set_getcaps_handler (GstValidateOverride * override, GstValidateOverrideGetCapsHandler handler);
GST_VALIDATE_API
void               gst_validate_override_set_setcaps_handler (GstValidateOverride * override, GstValidateOverrideSetCapsHandler handler);
GST_VALIDATE_API
void               gst_validate_override_element_added_handler (GstValidateOverride * override, GstValidateMonitor * monitor, GstElement * child);
GST_VALIDATE_API
void               gst_validate_override_set_element_added_handler (GstValidateOverride * override, GstValidateOverrideElementAddedHandler func);

GST_VALIDATE_API
gboolean           gst_validate_override_can_attach (GstValidateOverride * override, GstValidateMonitor *monitor);

GST_VALIDATE_API
void           gst_validate_override_attached (GstValidateOverride * override);

G_END_DECLS

#endif /* #ifndef __GST_VALIDATE_OVERRIDE_H__*/
