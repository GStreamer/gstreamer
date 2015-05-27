/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor-preload.c - Validate Element monitors preload functions
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <gst/validate/validate.h>

#define __USE_GNU
#include <dlfcn.h>

static GstValidateRunner *runner = NULL;

static void
exit_report_printer (void)
{
  if (runner)
    gst_validate_runner_exit (runner, TRUE);
}

/*
 * Functions that wrap object creation so gst-validate can be used
 * to monitor 'standard' applications
 */

static void
gst_validate_preload_wrap (GstElement * element)
{
  if (runner == NULL) {
    gst_validate_init ();
    runner = gst_validate_runner_new ();
    atexit (exit_report_printer);
  }

  /* the reference to the monitor is lost */
  gst_validate_monitor_factory_create (GST_OBJECT_CAST (element), runner, NULL);
}

GstElement *
gst_element_factory_make (const gchar * element_name, const gchar * name)
{
  static GstElement *(*gst_element_factory_make_real) (const gchar *,
      const gchar *) = NULL;
  GstElement *element;

  if (!gst_element_factory_make_real)
    gst_element_factory_make_real =
        dlsym (RTLD_NEXT, "gst_element_factory_make");

  element = gst_element_factory_make_real (element_name, name);

  if (GST_IS_PIPELINE (element)) {
    gst_validate_preload_wrap (element);
  }
  return element;
}

GstElement *
gst_pipeline_new (const gchar * name)
{
  static GstElement *(*gst_pipeline_new_real) (const gchar *) = NULL;
  GstElement *element;

  if (!gst_pipeline_new_real)
    gst_pipeline_new_real = dlsym (RTLD_NEXT, "gst_pipeline_new");

  element = gst_pipeline_new_real (name);
  gst_validate_preload_wrap (element);
  return element;
}

GstElement *
gst_parse_launchv (const gchar ** argv, GError ** error)
{
  static GstElement *(*gst_parse_launchv_real) (const gchar **, GError **) =
      NULL;
  GstElement *element;

  if (!gst_parse_launchv_real)
    gst_parse_launchv_real = dlsym (RTLD_NEXT, "gst_parse_launchv");

  element = gst_parse_launchv_real (argv, error);
  if (GST_IS_PIPELINE (element)) {
    gst_validate_preload_wrap (element);
  }
  return element;
}

GstElement *
gst_parse_launchv_full (const gchar ** argv, GstParseContext * context,
    GstParseFlags flags, GError ** error)
{
  static GstElement *(*gst_parse_launchv_full_real) (const gchar **,
      GstParseContext *, GstParseFlags, GError **) = NULL;
  GstElement *element;

  if (!gst_parse_launchv_full_real)
    gst_parse_launchv_full_real = dlsym (RTLD_NEXT, "gst_parse_launchv_full");

  element = gst_parse_launchv_full_real (argv, context, flags, error);
  if (GST_IS_PIPELINE (element)) {
    gst_validate_preload_wrap (element);
  }
  return element;
}

GstElement *
gst_parse_launch (const gchar * pipeline_description, GError ** error)
{
  static GstElement *(*gst_parse_launch_real) (const gchar *, GError **) = NULL;
  GstElement *element;

  if (!gst_parse_launch_real)
    gst_parse_launch_real = dlsym (RTLD_NEXT, "gst_parse_launch");

  element = gst_parse_launch_real (pipeline_description, error);
  if (GST_IS_PIPELINE (element)) {
    gst_validate_preload_wrap (element);
  }
  return element;
}

GstElement *
gst_parse_launch_full (const gchar * pipeline_description,
    GstParseContext * context, GstParseFlags flags, GError ** error)
{
  static GstElement *(*gst_parse_launch_full_real) (const gchar *,
      GstParseContext *, GstParseFlags, GError **) = NULL;
  GstElement *element;

  if (!gst_parse_launch_full_real)
    gst_parse_launch_full_real = dlsym (RTLD_NEXT, "gst_parse_launch_full");

  element =
      gst_parse_launch_full_real (pipeline_description, context, flags, error);
  if (GST_IS_PIPELINE (element)) {
    gst_validate_preload_wrap (element);
  }
  return element;
}
