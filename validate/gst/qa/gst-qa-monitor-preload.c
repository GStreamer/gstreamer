/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor-preload.c - QA Element monitors preload functions
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

#include <gst/gst.h>
#include <string.h>
#include "gst-qa-runner.h"

#define __USE_GNU
#include <dlfcn.h>

/*
 * Functions that wrap object creation so gst-qa can be used
 * to monitor 'standard' applications
 */

static void
gst_qa_preload_wrap (GstElement * element)
{
  GstQaRunner *runner;

  runner = gst_qa_runner_new (element);

  /* TODO this will actually never unref the runner as it holds a ref
   * to the element */
  g_object_set_data_full ((GObject *) element, "qa-runner", runner,
      g_object_unref);
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
    gst_qa_preload_wrap (element);
  }
  return element;
}

gpointer
g_object_new (GType object_type, const gchar * first_property_name, ...)
{
  static gpointer (*g_object_new_real) (GType, const gchar *, ...) = NULL;
  gpointer obj;
  va_list var_args;

  if (!g_object_new_real)
    g_object_new_real = dlsym (RTLD_NEXT, "g_object_new");

  va_start (var_args, first_property_name);
  obj = g_object_new_valist (object_type, first_property_name, var_args);
  va_end (var_args);

  if (GST_IS_PIPELINE (obj)) {
    gst_qa_preload_wrap (obj);
  }

  return obj;
}

gpointer
g_object_newv (GType object_type, guint n_parameters, GParameter * parameters)
{
  static gpointer (*g_object_newv_real) (GType, guint, GParameter *) = NULL;
  gpointer obj;

  if (!g_object_newv_real)
    g_object_newv_real = dlsym (RTLD_NEXT, "g_object_newv");

  obj = g_object_newv_real (object_type, n_parameters, parameters);

  if (GST_IS_PIPELINE (obj)) {
    gst_qa_preload_wrap (obj);
  }

  return obj;
}
