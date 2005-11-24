/* GStreamer
 *
 * Common code for GStreamer unittests
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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
 * SECTION:gstcheck
 * @short_description: Common code for GStreamer unit tests
 *
 * These macros and functions are for internal use of the unit tests found
 * inside the 'check' directories of various GStreamer packages.
 */

#include "gstcheck.h"

GST_DEBUG_CATEGORY (check_debug);

/* logging function for tests
 * a test uses g_message() to log a debug line
 * a gst unit test can be run with GST_TEST_DEBUG env var set to see the
 * messages
 */

gboolean _gst_check_threads_running = FALSE;
GList *thread_list = NULL;
GMutex *mutex;
GCond *start_cond;              /* used to notify main thread of thread startups */
GCond *sync_cond;               /* used to synchronize all threads and main thread */

gboolean _gst_check_debug = FALSE;
gboolean _gst_check_raised_critical = FALSE;
gboolean _gst_check_raised_warning = FALSE;
gboolean _gst_check_expecting_log = FALSE;

void gst_check_log_message_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (_gst_check_debug) {
    g_print ("%s", message);
  }
}

void gst_check_log_critical_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (!_gst_check_expecting_log) {
    g_print ("\n\nUnexpected critical/warning: %s\n", message);
    fail ("Unexpected critical/warning: %s", message);
  }

  if (_gst_check_debug) {
    g_print ("\nExpected critical/warning: %s\n", message);
  }

  if (log_level & G_LOG_LEVEL_CRITICAL)
    _gst_check_raised_critical = TRUE;
  if (log_level & G_LOG_LEVEL_WARNING)
    _gst_check_raised_warning = TRUE;
}

/* initialize GStreamer testing */
void
gst_check_init (int *argc, char **argv[])
{
  gst_init (argc, argv);

  GST_DEBUG_CATEGORY_INIT (check_debug, "check", 0, "check regression tests");

  if (g_getenv ("GST_TEST_DEBUG"))
    _gst_check_debug = TRUE;

  g_log_set_handler (NULL, G_LOG_LEVEL_MESSAGE, gst_check_log_message_func,
      NULL);
  g_log_set_handler (NULL, G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_log_set_handler ("GStreamer", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_log_set_handler ("GLib-GObject", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
}

/* message checking */
void
gst_check_message_error (GstMessage * message, GstMessageType type,
    GQuark domain, gint code)
{
  GError *error;
  gchar *debug;

  fail_unless_equals_int (GST_MESSAGE_TYPE (message), type);
  gst_message_parse_error (message, &error, &debug);
  fail_unless_equals_int (error->domain, domain);
  fail_unless_equals_int (error->code, code);
  g_error_free (error);
  g_free (debug);
}

/* helper functions */
GstFlowReturn
gst_check_chain_func (GstPad * pad, GstBuffer * buffer)
{
  GST_DEBUG ("chain_func: received buffer %p", buffer);
  buffers = g_list_append (buffers, buffer);

  return GST_FLOW_OK;
}

/* setup an element for a filter test with mysrcpad and mysinkpad */
GstElement *
gst_check_setup_element (const gchar * factory)
{
  GstElement *element;

  GST_DEBUG ("setup_element");

  element = gst_element_factory_make (factory, factory);
  fail_if (element == NULL, "Could not create a %s", factory);
  ASSERT_OBJECT_REFCOUNT (element, factory, 1);
  return element;
}

void
gst_check_teardown_element (GstElement * element)
{
  GST_DEBUG ("teardown_element");

  fail_unless (gst_element_set_state (element, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT (element, "element", 1);
  gst_object_unref (element);
}

/* FIXME: set_caps isn't that useful
 */
GstPad *
gst_check_setup_src_pad (GstElement * element,
    GstStaticPadTemplate * template, GstCaps * caps)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  sinkpad = gst_element_get_pad (element, "sink");
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  if (caps)
    fail_unless (gst_pad_set_caps (srcpad, caps));
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  return srcpad;
}

void
gst_check_teardown_src_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating src pad */
  sinkpad = gst_element_get_pad (element, "sink");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  srcpad = gst_pad_get_peer (sinkpad);

  gst_pad_unlink (srcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (srcpad);
}

/* FIXME: set_caps isn't that useful; might want to check if fixed,
 * then use set_use_fixed or somesuch */
GstPad *
gst_check_setup_sink_pad (GstElement * element, GstStaticPadTemplate * template,
    GstCaps * caps)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up receiving pad");
  /* receiving pad */
  sinkpad = gst_pad_new_from_static_template (template, "sink");
  fail_if (sinkpad == NULL, "Could not create a sinkpad");

  srcpad = gst_element_get_pad (element, "src");
  fail_if (srcpad == NULL, "Could not get source pad from %s",
      GST_ELEMENT_NAME (element));
  if (caps)
    fail_unless (gst_pad_set_caps (sinkpad, caps));
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link %s source and sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  return sinkpad;
}

void
gst_check_teardown_sink_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating sink pad */
  srcpad = gst_element_get_pad (element, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  gst_pad_unlink (srcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get_pad) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  gst_object_unref (sinkpad);
}
