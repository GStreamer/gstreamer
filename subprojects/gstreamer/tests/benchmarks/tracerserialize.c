/* GStreamer
 * Copyright (C) 2016 Stefan Sauer <ensonic@users.sf.net>
 *
 * tracerserialize.c: benchmark for log serialisation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITcreating %d capsNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* to check the sizes run:
 *
 * GST_DEBUG="default:7" GST_DEBUG_FILE=trace.log ./tracerserialize
 *
 * grep "log_gst_structure" trace.log >tracerserialize.gststructure.log
 * grep "log_g_variant" trace.log >tracerserialize.gvariant.log
 *
 */

#include <gst/gst.h>

#define NUM_LOOPS 100000

static void
log_gst_structure (const gchar * name, const gchar * first, ...)
{
  va_list var_args;
  GstStructure *s;
  gchar *l;

  va_start (var_args, first);
  s = gst_structure_new_valist (name, first, var_args);
  l = gst_structure_to_string (s);
  GST_TRACE ("%s", l);
  g_free (l);
  gst_structure_free (s);
  va_end (var_args);
}

static void
log_gst_structure_tmpl (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  if (G_LIKELY (GST_LEVEL_TRACE <= _gst_debug_min)) {
    gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_TRACE, __FILE__,
        GST_FUNCTION, __LINE__, NULL, format, var_args);
  }
  va_end (var_args);
}

static void
log_g_variant (const gchar * format, ...)
{
  va_list var_args;
  GVariant *v;
  gchar *l;

  va_start (var_args, format);
  v = g_variant_new_va (format, NULL, &var_args);
  l = g_variant_print (v, FALSE);
  GST_TRACE ("%s", l);
  g_free (l);
  g_variant_unref (v);
  va_end (var_args);
}

gint
main (gint argc, gchar * argv[])
{
  GstClockTime start, end;
  gint i;

  gst_init (&argc, &argv);

  start = gst_util_get_timestamp ();
  for (i = 0; i < NUM_LOOPS; i++) {
    log_gst_structure ("name",
        "ts", G_TYPE_UINT64, (guint64) 0,
        "index", G_TYPE_UINT, 10,
        "test", G_TYPE_STRING, "hallo",
        "bool", G_TYPE_BOOLEAN, TRUE,
        "flag", GST_TYPE_PAD_DIRECTION, GST_PAD_SRC, NULL);
  }
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT ": GstStructure\n", GST_TIME_ARGS (end - start));

  start = gst_util_get_timestamp ();
  for (i = 0; i < NUM_LOOPS; i++) {
    log_gst_structure_tmpl ("name, ts=(guint64)%" G_GUINT64_FORMAT
        ", index=(uint)%u, test=(string)%s, bool=(boolean)%s, flag=(GstPadDirection)%d;",
        (guint64) 0, 10, "hallo", (TRUE ? "true" : "false"), GST_PAD_SRC);
  }
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT ": GstStructure template\n",
      GST_TIME_ARGS (end - start));

  start = gst_util_get_timestamp ();
  for (i = 0; i < NUM_LOOPS; i++) {
    log_g_variant ("(stusbu)", "name", (guint64) 0, 10, "hallo", TRUE,
        GST_PAD_SRC);
  }
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT ": GVariant\n", GST_TIME_ARGS (end - start));

  return 0;
}
