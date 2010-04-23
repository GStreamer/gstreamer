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

/**
 * SECTION:gsterror
 * @short_description: Categorized error messages
 * @see_also: #GstMessage
 *
 * GStreamer elements can throw non-fatal warnings and fatal errors.
 * Higher-level elements and applications can programatically filter
 * the ones they are interested in or can recover from,
 * and have a default handler handle the rest of them.
 *
 * The rest of this section will use the term <quote>error</quote>
 * to mean both (non-fatal) warnings and (fatal) errors; they are treated
 * similarly.
 *
 * Errors from elements are the combination of a #GError and a debug string.
 * The #GError contains:
 * - a domain type: CORE, LIBRARY, RESOURCE or STREAM
 * - a code: an enum value specific to the domain
 * - a translated, human-readable message
 * - a non-translated additional debug string, which also contains
 * - file and line information
 *
 * Elements do not have the context required to decide what to do with
 * errors.  As such, they should only inform about errors, and stop their
 * processing.  In short, an element doesn't know what it is being used for.
 *
 * It is the application or compound element using the given element that
 * has more context about the use of the element. Errors can be received by
 * listening to the #GstBus of the element/pipeline for #GstMessage objects with
 * the type %GST_MESSAGE_ERROR or %GST_MESSAGE_WARNING. The thrown errors should
 * be inspected, and filtered if appropriate.
 *
 * An application is expected to, by default, present the user with a
 * dialog box (or an equivalent) showing the error message.  The dialog
 * should also allow a way to get at the additional debug information,
 * so the user can provide bug reporting information.
 *
 * A compound element is expected to forward errors by default higher up
 * the hierarchy; this is done by default in the same way as for other types
 * of #GstMessage.
 *
 * When applications or compound elements trigger errors that they can
 * recover from, they can filter out these errors and take appropriate action.
 * For example, an application that gets an error from xvimagesink
 * that indicates all XVideo ports are taken, the application can attempt
 * to use another sink instead.
 *
 * Elements throw errors using the #GST_ELEMENT_ERROR convenience macro:
 *
 * <example>
 * <title>Throwing an error</title>
 *   <programlisting>
 *     GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
 *       (_("No file name specified for reading.")), (NULL));
 *   </programlisting>
 * </example>
 *
 * Things to keep in mind:
 * <itemizedlist>
 *   <listitem><para>Don't go off inventing new error codes.  The ones
 *     currently provided should be enough.  If you find your type of error
 *     does not fit the current codes, you should use FAILED.</para></listitem>
 *   <listitem><para>Don't provide a message if the default one suffices.
 *     this keeps messages more uniform.  Use (NULL) - not forgetting the
 *     parentheses.</para></listitem>
 *   <listitem><para>If you do supply a custom message, it should be
 *     marked for translation.  The message should start with a capital
 *     and end with a period.  The message should describe the error in short,
 *     in a human-readable form, and without any complex technical terms.
 *     A user interface will present this message as the first thing a user
 *     sees.  Details, technical info, ... should go in the debug string.
 *   </para></listitem>
 *   <listitem><para>The debug string can be as you like.  Again, use (NULL)
 *     if there's nothing to add - file and line number will still be
 *     passed.  #GST_ERROR_SYSTEM can be used as a shortcut to give
 *     debug information on a system call error.</para></listitem>
 * </itemizedlist>
 *
 * Last reviewed on 2006-09-15 (0.10.10)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include <gst/gst.h>
#include "gst-i18n-lib.h"

#define TABLE(t, d, a, b) t[GST_ ## d ## _ERROR_ ## a] = g_strdup (b)
#define QUARK_FUNC(string)                                              \
GQuark gst_ ## string ## _error_quark (void) {                          \
  static GQuark quark;                                                  \
  if (!quark)                                                           \
    quark = g_quark_from_static_string ("gst-" # string "-error-quark"); \
  return quark; }

/* FIXME: Deprecate when we depend on GLib 2.26 */
GType
gst_g_error_get_type (void)
{
#if GLIB_CHECK_VERSION(2,25,2)
  return g_error_get_type ();
#else
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    type = g_boxed_type_register_static ("GstGError",
        (GBoxedCopyFunc) g_error_copy, (GBoxedFreeFunc) g_error_free);
  return type;
#endif
}

#define FILE_A_BUG "  Please file a bug at " PACKAGE_BUGREPORT "."

/* initialize the dynamic table of translated core errors */
static gchar **
_gst_core_errors_init (void)
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_CORE_ERROR_NUM_ERRORS);

  TABLE (t, CORE, FAILED,
      N_("GStreamer encountered a general core library error."));
  TABLE (t, CORE, TOO_LAZY,
      N_("GStreamer developers were too lazy to assign an error code "
          "to this error." FILE_A_BUG));
  TABLE (t, CORE, NOT_IMPLEMENTED,
      N_("Internal GStreamer error: code not implemented." FILE_A_BUG));
  TABLE (t, CORE, STATE_CHANGE,
      N_("GStreamer error: state change failed and some element failed to "
          "post a proper error message with the reason for the failure."));
  TABLE (t, CORE, PAD, N_("Internal GStreamer error: pad problem." FILE_A_BUG));
  TABLE (t, CORE, THREAD,
      N_("Internal GStreamer error: thread problem." FILE_A_BUG));
  TABLE (t, CORE, NEGOTIATION,
      N_("Internal GStreamer error: negotiation problem." FILE_A_BUG));
  TABLE (t, CORE, EVENT,
      N_("Internal GStreamer error: event problem." FILE_A_BUG));
  TABLE (t, CORE, SEEK,
      N_("Internal GStreamer error: seek problem." FILE_A_BUG));
  TABLE (t, CORE, CAPS,
      N_("Internal GStreamer error: caps problem." FILE_A_BUG));
  TABLE (t, CORE, TAG, N_("Internal GStreamer error: tag problem." FILE_A_BUG));
  TABLE (t, CORE, MISSING_PLUGIN,
      N_("Your GStreamer installation is missing a plug-in."));
  TABLE (t, CORE, CLOCK,
      N_("Internal GStreamer error: clock problem." FILE_A_BUG));
  TABLE (t, CORE, DISABLED,
      N_("This application is trying to use GStreamer functionality that "
          "has been disabled."));

  return t;
}

/* initialize the dynamic table of translated library errors */
static gchar **
_gst_library_errors_init (void)
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_LIBRARY_ERROR_NUM_ERRORS);

  TABLE (t, LIBRARY, FAILED,
      N_("GStreamer encountered a general supporting library error."));
  TABLE (t, LIBRARY, TOO_LAZY,
      N_("GStreamer developers were too lazy to assign an error code "
          "to this error." FILE_A_BUG));
  TABLE (t, LIBRARY, INIT, N_("Could not initialize supporting library."));
  TABLE (t, LIBRARY, SHUTDOWN, N_("Could not close supporting library."));
  TABLE (t, LIBRARY, SETTINGS, N_("Could not configure supporting library."));

  return t;
}

/* initialize the dynamic table of translated resource errors */
static gchar **
_gst_resource_errors_init (void)
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_RESOURCE_ERROR_NUM_ERRORS);

  TABLE (t, RESOURCE, FAILED,
      N_("GStreamer encountered a general resource error."));
  TABLE (t, RESOURCE, TOO_LAZY,
      N_("GStreamer developers were too lazy to assign an error code "
          "to this error." FILE_A_BUG));
  TABLE (t, RESOURCE, NOT_FOUND, N_("Resource not found."));
  TABLE (t, RESOURCE, BUSY, N_("Resource busy or not available."));
  TABLE (t, RESOURCE, OPEN_READ, N_("Could not open resource for reading."));
  TABLE (t, RESOURCE, OPEN_WRITE, N_("Could not open resource for writing."));
  TABLE (t, RESOURCE, OPEN_READ_WRITE,
      N_("Could not open resource for reading and writing."));
  TABLE (t, RESOURCE, CLOSE, N_("Could not close resource."));
  TABLE (t, RESOURCE, READ, N_("Could not read from resource."));
  TABLE (t, RESOURCE, WRITE, N_("Could not write to resource."));
  TABLE (t, RESOURCE, SEEK, N_("Could not perform seek on resource."));
  TABLE (t, RESOURCE, SYNC, N_("Could not synchronize on resource."));
  TABLE (t, RESOURCE, SETTINGS,
      N_("Could not get/set settings from/on resource."));
  TABLE (t, RESOURCE, NO_SPACE_LEFT, N_("No space left on the resource."));

  return t;
}

/* initialize the dynamic table of translated stream errors */
static gchar **
_gst_stream_errors_init (void)
{
  gchar **t = NULL;

  t = g_new0 (gchar *, GST_STREAM_ERROR_NUM_ERRORS);

  TABLE (t, STREAM, FAILED,
      N_("GStreamer encountered a general stream error."));
  TABLE (t, STREAM, TOO_LAZY,
      N_("GStreamer developers were too lazy to assign an error code "
          "to this error." FILE_A_BUG));
  TABLE (t, STREAM, NOT_IMPLEMENTED,
      N_("Element doesn't implement handling of this stream. "
          "Please file a bug."));
  TABLE (t, STREAM, TYPE_NOT_FOUND, N_("Could not determine type of stream."));
  TABLE (t, STREAM, WRONG_TYPE,
      N_("The stream is of a different type than handled by this element."));
  TABLE (t, STREAM, CODEC_NOT_FOUND,
      N_("There is no codec present that can handle the stream's type."));
  TABLE (t, STREAM, DECODE, N_("Could not decode stream."));
  TABLE (t, STREAM, ENCODE, N_("Could not encode stream."));
  TABLE (t, STREAM, DEMUX, N_("Could not demultiplex stream."));
  TABLE (t, STREAM, MUX, N_("Could not multiplex stream."));
  TABLE (t, STREAM, FORMAT, N_("The stream is in the wrong format."));
  TABLE (t, STREAM, DECRYPT,
      N_("The stream is encrypted and decryption is not supported."));
  TABLE (t, STREAM, DECRYPT_NOKEY,
      N_("The stream is encrypted and can't be decrypted because no suitable "
          "key has been supplied."));

  return t;
}

QUARK_FUNC (core);
QUARK_FUNC (library);
QUARK_FUNC (resource);
QUARK_FUNC (stream);

/**
 * gst_error_get_message:
 * @domain: the GStreamer error domain this error belongs to.
 * @code: the error code belonging to the domain.
 *
 * Get a string describing the error message in the current locale.
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


  if (domain == GST_CORE_ERROR)
    message = gst_core_errors[code];
  else if (domain == GST_LIBRARY_ERROR)
    message = gst_library_errors[code];
  else if (domain == GST_RESOURCE_ERROR)
    message = gst_resource_errors[code];
  else if (domain == GST_STREAM_ERROR)
    message = gst_stream_errors[code];
  else {
    g_warning ("No error messages for domain %s", g_quark_to_string (domain));
    return g_strdup_printf (_("No error message for domain %s."),
        g_quark_to_string (domain));
  }
  if (message)
    return g_strdup (_(message));
  else
    return
        g_strdup_printf (_
        ("No standard error message for domain %s and code %d."),
        g_quark_to_string (domain), code);
}
