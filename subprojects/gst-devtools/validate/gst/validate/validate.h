/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 */

#ifndef _GST_VALIDATE_H
#define _GST_VALIDATE_H

#include <gst/validate/validate-prelude.h>

#include <gst/validate/gst-validate-types.h>
#include <gst/validate/gst-validate-enums.h>
#include <gst/validate/gst-validate-scenario.h>

#include <gst/validate/gst-validate-bin-monitor.h>
#include <gst/validate/gst-validate-media-info.h>
#include <gst/validate/gst-validate-monitor-factory.h>
#include <gst/validate/gst-validate-override-registry.h>
#include <gst/validate/gst-validate-pad-monitor.h>
#include <gst/validate/gst-validate-pipeline-monitor.h>
#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-reporter.h>
#include <gst/validate/gst-validate-runner.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/validate/media-descriptor-writer.h>

G_BEGIN_DECLS

GST_VALIDATE_API
void gst_validate_init (void);
GST_VALIDATE_API
void gst_validate_init_debug (void);
GST_VALIDATE_API
void gst_validate_deinit (void);
GST_VALIDATE_API
GList * gst_validate_plugin_get_config (GstPlugin * plugin);
GST_VALIDATE_API
gboolean gst_validate_is_initialized (void);
GST_VALIDATE_API
GstStructure *gst_validate_setup_test_file(const gchar * testfile, gboolean use_fakesinks);

G_END_DECLS

#endif /* _GST_VALIDATE_H */
