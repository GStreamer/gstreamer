/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracers.c: tracing modules
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstlatency.h"
#include "gstlog.h"
#include "gstrusage.h"
#include "gststats.h"
#include "gstleaks.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_tracer_register (plugin, "latency", gst_latency_tracer_get_type ()))
    return FALSE;
#ifndef GST_DISABLE_GST_DEBUG
  if (!gst_tracer_register (plugin, "log", gst_log_tracer_get_type ()))
    return FALSE;
#endif
#ifdef HAVE_GETRUSAGE
  if (!gst_tracer_register (plugin, "rusage", gst_rusage_tracer_get_type ()))
    return FALSE;
#endif
  if (!gst_tracer_register (plugin, "stats", gst_stats_tracer_get_type ()))
    return FALSE;
  if (!gst_tracer_register (plugin, "leaks", gst_leaks_tracer_get_type ()))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, coretracers,
    "GStreamer core tracers", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
