/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gst_private.h"
#include "gst-i18n-lib.h"

#define TABLE(t, d, a, b) t[GST_ ## d ## _ERROR_ ## a] = g_strdup (b)
#define QUARK_FUNC(string)						\
GQuark gst_ ## string ## _error_quark (void) {				\
  static GQuark quark;							\
  if (!quark)								\
    quark = g_quark_from_static_string ("gst-" # string "-error-quark"); \
  return quark; }

/* initialize the dynamic table of translated core errors */
static gchar ** _gst_core_errors_init ()
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_CORE_ERROR_NUM_ERRORS);

  TABLE (t, CORE, FAILED,
         N_("GStreamer encountered a general core library error."));
  TABLE (t, CORE, TOO_LAZY,
         N_("GStreamer developers were too lazy to assign an error code "
            "to this error.  Please file a bug."));
  TABLE (t, CORE, NOT_IMPLEMENTED,
          N_("Internal GStreamer error: code not implemented.  File a bug."));
  TABLE (t, CORE, STATE_CHANGE,
          N_("Internal GStreamer error: state change failed.  File a bug."));
  TABLE (t, CORE, PAD,
          N_("Internal GStreamer error: pad problem.  File a bug."));
  TABLE (t, CORE, THREAD,
          N_("Internal GStreamer error: thread problem.  File a bug."));
  TABLE (t, CORE, SCHEDULER,
          N_("Internal GStreamer error: scheduler problem.  File a bug."));
  TABLE (t, CORE, NEGOTIATION,
          N_("Internal GStreamer error: negotiation problem.  File a bug."));
  TABLE (t, CORE, EVENT,
          N_("Internal GStreamer error: event problem.  File a bug."));
  TABLE (t, CORE, SEEK,
          N_("Internal GStreamer error: seek problem.  File a bug."));
  TABLE (t, CORE, CAPS,
          N_("Internal GStreamer error: caps problem.  File a bug."));
  TABLE (t, CORE, TAG,
          N_("Internal GStreamer error: tag problem.  File a bug."));

  return t;
}

/* initialize the dynamic table of translated library errors */
static gchar ** _gst_library_errors_init ()
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_LIBRARY_ERROR_NUM_ERRORS);

  TABLE (t, LIBRARY, FAILED,
         N_("GStreamer encountered a general supporting library error."));
  TABLE (t, LIBRARY, TOO_LAZY,
         N_("GStreamer developers were too lazy to assign an error code "
            "to this error.  Please file a bug."));
  TABLE (t, LIBRARY, INIT,
         N_("Could not initialize supporting library."));
  TABLE (t, LIBRARY, SHUTDOWN,
         N_("Could not close supporting library."));
  TABLE (t, LIBRARY, SETTINGS,
         N_("Could not close supporting library."));

  return t;
}

/* initialize the dynamic table of translated resource errors */
static gchar ** _gst_resource_errors_init ()
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_RESOURCE_ERROR_NUM_ERRORS);

  TABLE (t, RESOURCE, FAILED,
         N_("GStreamer encountered a general supporting library error."));
  TABLE (t, RESOURCE, TOO_LAZY,
         N_("GStreamer developers were too lazy to assign an error code "
            "to this error.  Please file a bug."));
  TABLE (t, RESOURCE, NOT_FOUND,
         N_("Resource not found."));
  TABLE (t, RESOURCE, BUSY,
         N_("Resource busy or not available."));
  TABLE (t, RESOURCE, OPEN_READ,
         N_("Could not open resource for reading."));
  TABLE (t, RESOURCE, OPEN_WRITE,
         N_("Could not open resource for writing."));
  TABLE (t, RESOURCE, OPEN_READ_WRITE,
         N_("Could not open resource for reading and writing."));
  TABLE (t, RESOURCE, CLOSE,
         N_("Could not close resource."));
  TABLE (t, RESOURCE, READ,
         N_("Could not read from resource."));
  TABLE (t, RESOURCE, WRITE,
         N_("Could not write to resource."));
  TABLE (t, RESOURCE, SEEK,
         N_("Could not perform seek on resource."));
  TABLE (t, RESOURCE, SYNC,
         N_("Could not synchronize on resource."));
  TABLE (t, RESOURCE, SETTINGS,
         N_("Could not get/set settings from/on resource."));

  return t;
}

/* initialize the dynamic table of translated stream errors */
static gchar ** _gst_stream_errors_init ()
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_STREAM_ERROR_NUM_ERRORS);

  TABLE (t, STREAM, FAILED,
         N_("GStreamer encountered a general supporting library error."));
  TABLE (t, STREAM, TOO_LAZY,
         N_("GStreamer developers were too lazy to assign an error code "
            "to this error.  Please file a bug."));
  TABLE (t, STREAM, NOT_IMPLEMENTED,
          N_("Element doesn't implement handling of this stream. "
             "Please file a bug."));
  TABLE (t, STREAM, TYPE_NOT_FOUND,
         N_("Could not determine type of stream."));
  TABLE (t, STREAM, WRONG_TYPE,
         N_("The stream is of a different type than handled by this element."));
  TABLE (t, STREAM, DECODE,
         N_("Could not decode stream."));
  TABLE (t, STREAM, ENCODE,
         N_("Could not encode stream."));
  TABLE (t, STREAM, DEMUX,
         N_("Could not demultiplex stream."));
  TABLE (t, STREAM, MUX,
         N_("Could not multiplex stream."));
  TABLE (t, STREAM, FORMAT,
         N_("Stream is of the wrong format."));

  return t;
}

QUARK_FUNC (core)
QUARK_FUNC (library)
QUARK_FUNC (resource)
QUARK_FUNC (stream)

/**
 * gst_error_get_message:
 * @domain: the GStreamer error domain this error belongs to.
 * @code: the error code belonging to the domain.
 *
 * Returns: a newly allocated string describing the error message in the
 * current locale.
 */

gchar *
gst_error_get_message (GQuark domain, gint code)
{
  static gchar **gst_core_errors = NULL;
  static gchar **gst_library_errors = NULL;
  static gchar **gst_resource_errors = NULL;
  static gchar **gst_stream_errors = NULL;

  gchar *message = NULL;

  /* initialize error message tables if necessary */
  if (gst_core_errors == NULL)
    gst_core_errors = _gst_core_errors_init ();
  if (gst_library_errors == NULL)
    gst_library_errors = _gst_library_errors_init ();
  if (gst_resource_errors == NULL)
    gst_resource_errors = _gst_resource_errors_init ();
  if (gst_stream_errors == NULL)
    gst_stream_errors = _gst_stream_errors_init ();


  if      (domain == GST_CORE_ERROR)     message = gst_core_errors    [code];
  else if (domain == GST_LIBRARY_ERROR)  message = gst_library_errors [code];
  else if (domain == GST_RESOURCE_ERROR) message = gst_resource_errors[code];
  else if (domain == GST_STREAM_ERROR)   message = gst_stream_errors  [code];
  else
  {
    g_warning ("No error messages for domain %s", g_quark_to_string (domain));
    return g_strdup_printf (_("No error message for domain %s"), g_quark_to_string (domain));
  }
  if (message)
    return g_strdup (_(message));
  else
    return g_strdup_printf (_("No standard error message for domain %s and code %d."),
            g_quark_to_string (domain), code);
}
