/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>
 *
 * gst-validate-default-overrides.c - Test overrides
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/validate/gst-validate-override.h>
#include <gst/validate/gst-validate-override-registry.h>
#include <gst/validate/gst-validate-report.h>

/* public symbol */
GST_PLUGIN_EXPORT int gst_validate_create_overrides (void);

int
gst_validate_create_overrides (void)
{
  GstValidateOverride *o;

  /* Some random test override. Will moan on:
     gst-launch videotestsrc num-buffers=10 ! video/x-raw-yuv !  fakesink */
  o = gst_validate_override_new ();
  gst_validate_override_change_severity (o,
      g_quark_from_string ("caps::is-missing-field"),
      GST_VALIDATE_REPORT_LEVEL_CRITICAL);
  gst_validate_override_register_by_name ("capsfilter0", o);
  g_object_unref (o);
  return 1;
}
