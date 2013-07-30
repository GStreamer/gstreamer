/* GStreamer
 * Copyright (C) 2013 Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>
 *
 * gst-qa-default-overrides.c - Test overrides
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
#include <stdio.h>
#include "gst-qa-override.h"
#include "gst-qa-override-registry.h"
#include "gst-qa-report.h"

int
gst_qa_create_overrides (void)
{
  GstQaOverride *o;

  /* Some random test override. Will moan on:
     gst-launch videotestsrc num-buffers=10 ! video/x-raw-yuv !  fakesink */
  o = gst_qa_override_new ();
  gst_qa_override_change_severity (o, GST_QA_ISSUE_ID_CAPS_IS_MISSING_FIELD,
      GST_QA_REPORT_LEVEL_CRITICAL);
  gst_qa_override_register_by_name ("capsfilter0", o);
  return 1;
}
