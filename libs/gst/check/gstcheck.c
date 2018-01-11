/* GStreamer
 *
 * Common code for GStreamer unittests
 *
 * Copyright (C) 2004,2006 Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2008 Thijs Vermeir <thijsvermeir@gmail.com>
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
/**
 * SECTION:gstcheck
 * @title: GstCheck
 * @short_description: Common code for GStreamer unit tests
 *
 * These macros and functions are for internal use of the unit tests found
 * inside the 'check' directories of various GStreamer packages.
 *
 * One notable feature is that one can use the environment variables GST_CHECKS
 * and GST_CHECKS_IGNORE to select which tests to run or skip. Both variables
 * can contain a comma separated list of test name globs (e.g. test_*).
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcheck.h"

GST_DEBUG_CATEGORY (check_debug);

/* logging function for tests
 * a test uses g_message() to log a debug line
 * a gst unit test can be run with GST_TEST_DEBUG env var set to see the
 * messages
 */

gboolean _gst_check_threads_running = FALSE;
GList *thread_list = NULL;
GMutex mutex;
GCond start_cond;               /* used to notify main thread of thread startups */
GCond sync_cond;                /* used to synchronize all threads and main thread */

GList *buffers = NULL;
GMutex check_mutex;
GCond check_cond;

/* FIXME 2.0: shouldn't _gst_check_debug be static? Not used anywhere */
gboolean _gst_check_debug = FALSE;
gboolean _gst_check_raised_critical = FALSE;
gboolean _gst_check_raised_warning = FALSE;
gboolean _gst_check_expecting_log = FALSE;
gboolean _gst_check_list_tests = FALSE;
static GQueue _gst_check_log_filters = G_QUEUE_INIT;
static GMutex _gst_check_log_filters_mutex;

struct _GstCheckLogFilter
{
  gchar *log_domain;
  GLogLevelFlags log_level;
  GRegex *regex;
  GstCheckLogFilterFunc func;
  gpointer user_data;
  GDestroyNotify destroy;
};


static gboolean
gst_check_match_log_filter (const GstCheckLogFilter * filter,
    const gchar * log_domain, GLogLevelFlags log_level, const gchar * message)
{
  if (g_strcmp0 (log_domain, filter->log_domain) != 0)
    return FALSE;

  if ((log_level & filter->log_level) == 0)
    return FALSE;

  if (!g_regex_match (filter->regex, message, 0, NULL))
    return FALSE;

  return TRUE;
}

static GstCheckLogFilter *
gst_check_alloc_log_filter (const gchar * log_domain, GLogLevelFlags log_level,
    GRegex * regex, GstCheckLogFilterFunc func, gpointer user_data,
    GDestroyNotify destroy_data)
{
  GstCheckLogFilter *filter;

  filter = g_slice_new (GstCheckLogFilter);
  filter->log_domain = g_strdup (log_domain);
  filter->log_level = log_level;
  filter->regex = regex;
  filter->func = func;
  filter->user_data = user_data;
  filter->destroy = destroy_data;

  return filter;
}

static void
gst_check_free_log_filter (GstCheckLogFilter * filter)
{
  if (!filter)
    return;

  g_free (filter->log_domain);
  g_regex_unref (filter->regex);
  if (filter->destroy)
    filter->destroy (filter->user_data);
  g_slice_free (GstCheckLogFilter, filter);
}


/**
 * gst_check_add_log_filter:
 * @log_domain: the log domain of the message
 * @log_level: the log level of the message
 * @regex: (transfer full): a #GRegex to match the message
 * @func: the function to call for matching messages
 * @user_data: the user data to pass to @func
 * @destroy_data: #GDestroyNotify for @user_data
 *
 * Add a callback @func to be called for all log messages that matches
 * @log_domain, @log_level and @regex. If @func is NULL the
 * matching logs will be silently discarded by GstCheck.
 *
 * MT safe.
 *
 * Returns: A filter that can be passed to @gst_check_remove_log_filter.
 *
 * Since: 1.12
 **/
GstCheckLogFilter *
gst_check_add_log_filter (const gchar * log_domain, GLogLevelFlags log_level,
    GRegex * regex, GstCheckLogFilterFunc func, gpointer user_data,
    GDestroyNotify destroy_data)
{
  GstCheckLogFilter *filter;

  g_return_val_if_fail (regex != NULL, NULL);

  filter = gst_check_alloc_log_filter (log_domain, log_level, regex,
      func, user_data, destroy_data);
  g_mutex_lock (&_gst_check_log_filters_mutex);
  g_queue_push_tail (&_gst_check_log_filters, filter);
  g_mutex_unlock (&_gst_check_log_filters_mutex);

  return filter;
}

/**
 * gst_check_remove_log_filter:
 * @filter: Filter returned by @gst_check_add_log_filter
 *
 * Remove a filter that has been added by @gst_check_add_log_filter.
 *
 * MT safe.
 *
 * Since: 1.12
 **/
void
gst_check_remove_log_filter (GstCheckLogFilter * filter)
{
  g_mutex_lock (&_gst_check_log_filters_mutex);
  g_queue_remove (&_gst_check_log_filters, filter);
  gst_check_free_log_filter (filter);
  g_mutex_unlock (&_gst_check_log_filters_mutex);
}

/**
 * gst_check_clear_log_filter:
 *
 * Clear all filters added by @gst_check_add_log_filter.
 *
 * MT safe.
 *
 * Since: 1.12
 **/
void
gst_check_clear_log_filter (void)
{
  g_mutex_lock (&_gst_check_log_filters_mutex);
  g_queue_foreach (&_gst_check_log_filters,
      (GFunc) gst_check_free_log_filter, NULL);
  g_queue_clear (&_gst_check_log_filters);
  g_mutex_unlock (&_gst_check_log_filters_mutex);
}

typedef struct
{
  const gchar *domain;
  const gchar *message;
  GLogLevelFlags level;
  gboolean discard;
} LogFilterApplyData;

static void
gst_check_apply_log_filter (GstCheckLogFilter * filter,
    LogFilterApplyData * data)
{
  if (gst_check_match_log_filter (filter, data->domain, data->level,
          data->message)) {
    if (filter->func)
      data->discard |= filter->func (data->domain, data->level,
          data->message, filter->user_data);
    else
      data->discard = TRUE;
  }
}

static gboolean
gst_check_filter_log_filter (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message)
{
  LogFilterApplyData data;

  data.domain = log_domain;
  data.level = log_level;
  data.message = message;
  data.discard = FALSE;

  g_mutex_lock (&_gst_check_log_filters_mutex);
  g_queue_foreach (&_gst_check_log_filters, (GFunc) gst_check_apply_log_filter,
      &data);
  g_mutex_unlock (&_gst_check_log_filters_mutex);

  if (data.discard)
    GST_DEBUG ("Discarding message: %s", message);

  return data.discard;
}

static gboolean
gst_check_log_fatal_func (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (gst_check_filter_log_filter (log_domain, log_level, message))
    return FALSE;

  return TRUE;
}


static void gst_check_log_message_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (gst_check_filter_log_filter (log_domain, log_level, message))
    return;

  if (_gst_check_debug) {
    g_print ("%s\n", message);
  }
}

static void gst_check_log_critical_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  if (gst_check_filter_log_filter (log_domain, log_level, message))
    return;

  if (!_gst_check_expecting_log) {
    gchar *trace;

    g_print ("\n\nUnexpected critical/warning: %s\n", message);

    trace = gst_debug_get_stack_trace (GST_STACK_TRACE_SHOW_FULL);
    if (trace) {
      g_print ("\nStack trace:\n%s\n", trace);
      g_free (trace);
    }
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

static gint
sort_plugins (GstPlugin * a, GstPlugin * b)
{
  int ret;

  ret = strcmp (gst_plugin_get_source (a), gst_plugin_get_source (b));
  if (ret == 0)
    ret = strcmp (gst_plugin_get_name (a), gst_plugin_get_name (b));
  return ret;
}

static void
print_plugins (void)
{
  GList *plugins, *l;

  plugins = gst_registry_get_plugin_list (gst_registry_get ());
  plugins = g_list_sort (plugins, (GCompareFunc) sort_plugins);
  for (l = plugins; l != NULL; l = l->next) {
    GstPlugin *plugin = GST_PLUGIN (l->data);

    if (strcmp (gst_plugin_get_source (plugin), "BLACKLIST") != 0) {
      GST_LOG ("%20s@%s", gst_plugin_get_name (plugin),
          GST_STR_NULL (gst_plugin_get_filename (plugin)));
    }
  }
  gst_plugin_list_free (plugins);
}

static void
gst_check_deinit (void)
{
  gst_deinit ();
  gst_check_clear_log_filter ();
}

/* gst_check_init:
 * @argc: (inout) (allow-none): pointer to application's argc
 * @argv: (inout) (array length=argc) (allow-none): pointer to application's argv
 *
 * Initialize GStreamer testing
 *
 * NOTE: Needs to be called before creating the testsuite
 * so that the tests can be listed.
 * */
void
gst_check_init (int *argc, char **argv[])
{
  guint timeout_multiplier = 1;
  GOptionContext *ctx;
  GError *err = NULL;
  GOptionEntry options[] = {
    {"list-tests", 'l', 0, G_OPTION_ARG_NONE, &_gst_check_list_tests,
        "List tests present in the testsuite", NULL},
    {NULL}
  };

  ctx = g_option_context_new ("gst-check");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, argc, argv, &err)) {
    if (err)
      g_printerr ("Error initializing: %s\n", GST_STR_NULL (err->message));
    else
      g_printerr ("Error initializing: Unknown error!\n");
    g_clear_error (&err);
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (check_debug, "check", 0, "check regression tests");

  if (atexit (gst_check_deinit) != 0) {
    GST_ERROR ("failed to set gst_check_deinit as exit function");
  }

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
  g_log_set_handler ("GLib-GIO", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_log_set_handler ("GLib", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
      gst_check_log_critical_func, NULL);
  g_test_log_set_fatal_handler (gst_check_log_fatal_func, NULL);

  print_plugins ();

#ifdef TARGET_CPU
  GST_INFO ("target CPU: %s", TARGET_CPU);
#endif

#ifdef HAVE_CPU_ARM
  timeout_multiplier = 10;
#endif

  if (timeout_multiplier > 1) {
    const gchar *tmult = g_getenv ("CK_TIMEOUT_MULTIPLIER");

    if (tmult == NULL) {
      gchar num_str[32];

      g_snprintf (num_str, sizeof (num_str), "%d", timeout_multiplier);
      GST_INFO ("slow CPU, setting CK_TIMEOUT_MULTIPLIER to %s", num_str);
      g_setenv ("CK_TIMEOUT_MULTIPLIER", num_str, TRUE);
    } else {
      GST_INFO ("CK_TIMEOUT_MULTIPLIER already set to '%s'", tmult);
    }
  }
}

/* message checking */
void
gst_check_message_error (GstMessage * message, GstMessageType type,
    GQuark domain, gint code)
{
  GError *error;
  gchar *debug;

  fail_unless (GST_MESSAGE_TYPE (message) == type,
      "message is of type %s instead of expected type %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)),
      gst_message_type_get_name (type));
  gst_message_parse_error (message, &error, &debug);
  fail_unless_equals_int (error->domain, domain);
  fail_unless_equals_int (error->code, code);
  g_error_free (error);
  g_free (debug);
}

/* helper functions */
GstFlowReturn
gst_check_chain_func (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pad, "chain_func: received buffer %p", buffer);
  buffers = g_list_append (buffers, buffer);

  g_mutex_lock (&check_mutex);
  g_cond_signal (&check_cond);
  g_mutex_unlock (&check_mutex);

  return GST_FLOW_OK;
}

/**
 * gst_check_setup_element:
 * @factory: factory
 *
 * setup an element for a filter test with mysrcpad and mysinkpad
 *
 * Returns: (transfer full): a new element
 */
GstElement *
gst_check_setup_element (const gchar * factory)
{
  GstElement *element;

  GST_DEBUG ("setup_element");

  element = gst_element_factory_make (factory, factory);
  fail_if (element == NULL, "Could not create a '%s' element", factory);
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

/**
 * gst_check_setup_src_pad:
 * @element: element to setup pad on
 * @tmpl: pad template
 *
 * Does the same as #gst_check_setup_src_pad_by_name with the <emphasis> name </emphasis> parameter equal to "sink".
 *
 * Returns: (transfer full): A new pad that can be used to inject data on @element
 */
GstPad *
gst_check_setup_src_pad (GstElement * element, GstStaticPadTemplate * tmpl)
{
  return gst_check_setup_src_pad_by_name (element, tmpl, "sink");
}

/**
 * gst_check_setup_src_pad_by_name:
 * @element: element to setup src pad on
 * @tmpl: pad template
 * @name: Name of the @element sink pad that will be linked to the src pad that will be setup
 *
 * Creates a new src pad (based on the given @tmpl) and links it to the given @element sink pad (the pad that matches the given @name).
 * Before using the src pad to push data on @element you need to call #gst_check_setup_events on the created src pad.
 *
 * Example of how to push a buffer on @element:
 *
 * |[<!-- language="C" -->
 * static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
 * GST_PAD_SINK,
 * GST_PAD_ALWAYS,
 * GST_STATIC_CAPS (YOUR_CAPS_TEMPLATE_STRING)
 * );
 * static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
 * GST_PAD_SRC,
 * GST_PAD_ALWAYS,
 * GST_STATIC_CAPS (YOUR_CAPS_TEMPLATE_STRING)
 * );
 *
 * GstElement * element = gst_check_setup_element ("element");
 * GstPad * mysrcpad = gst_check_setup_src_pad (element, &srctemplate);
 * GstPad * mysinkpad = gst_check_setup_sink_pad (element, &sinktemplate);
 *
 * gst_pad_set_active (mysrcpad, TRUE);
 * gst_pad_set_active (mysinkpad, TRUE);
 * fail_unless (gst_element_set_state (element, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS, "could not set to playing");
 *
 * GstCaps * caps = gst_caps_from_string (YOUR_DESIRED_SINK_CAPS);
 * gst_check_setup_events (mysrcpad, element, caps, GST_FORMAT_TIME);
 * gst_caps_unref (caps);
 *
 * fail_unless (gst_pad_push (mysrcpad, gst_buffer_new_and_alloc(2)) == GST_FLOW_OK);
 * ]|
 *
 * For very simple input/output test scenarios checkout #gst_check_element_push_buffer_list and #gst_check_element_push_buffer.
 *
 * Returns: (transfer full): A new pad that can be used to inject data on @element
 */
GstPad *
gst_check_setup_src_pad_by_name (GstElement * element,
    GstStaticPadTemplate * tmpl, const gchar * name)
{
  GstPadTemplate *ptmpl = gst_static_pad_template_get (tmpl);
  GstPad *srcpad;

  srcpad = gst_check_setup_src_pad_by_name_from_template (element, ptmpl, name);

  gst_object_unref (ptmpl);

  return srcpad;
}

/**
 * gst_check_setup_src_pad_from_template:
 * @element: element to setup pad on
 * @tmpl: pad template
 *
 * Returns: (transfer full): a new pad
 *
 * Since: 1.4
 */
GstPad *
gst_check_setup_src_pad_from_template (GstElement * element,
    GstPadTemplate * tmpl)
{
  return gst_check_setup_src_pad_by_name_from_template (element, tmpl, "sink");
}

/**
 * gst_check_setup_src_pad_by_name_from_template:
 * @element: element to setup pad on
 * @tmpl: pad template
 * @name: name
 *
 * Returns: (transfer full): a new pad
 *
 * Since: 1.4
 */
GstPad *
gst_check_setup_src_pad_by_name_from_template (GstElement * element,
    GstPadTemplate * tmpl, const gchar * name)
{
  GstPad *srcpad, *sinkpad;

  /* sending pad */
  srcpad = gst_pad_new_from_template (tmpl, "src");
  GST_DEBUG_OBJECT (element, "setting up sending pad %p", srcpad);
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  sinkpad = gst_element_get_static_pad (element, name);
  if (sinkpad == NULL)
    sinkpad = gst_element_get_request_pad (element, name);
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  return srcpad;
}

void
gst_check_teardown_pad_by_name (GstElement * element, const gchar * name)
{
  GstPad *pad_peer, *pad_element;

  /* clean up floating src pad */
  pad_element = gst_element_get_static_pad (element, name);
  /* We don't check the refcount here since there *might* be
   * a pad cache holding an extra reference on pad_element.
   * To get to a state where no pad cache will exist,
   * we first unlink that pad. */
  pad_peer = gst_pad_get_peer (pad_element);

  if (pad_peer) {
    if (gst_pad_get_direction (pad_element) == GST_PAD_SINK)
      gst_pad_unlink (pad_peer, pad_element);
    else
      gst_pad_unlink (pad_element, pad_peer);
  }

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (pad_element, "element pad_element", 2);
  gst_object_unref (pad_element);
  /* one more ref is held by element itself */

  if (pad_peer) {
    /* pad refs held by both creator and this function (through _get_peer) */
    ASSERT_OBJECT_REFCOUNT (pad_peer, "check pad_peer", 2);
    gst_object_unref (pad_peer);
    gst_object_unref (pad_peer);
  }
}

void
gst_check_teardown_src_pad (GstElement * element)
{
  gst_check_teardown_pad_by_name (element, "sink");
}

/**
 * gst_check_setup_sink_pad:
 * @element: element to setup pad on
 * @tmpl: pad template
 *
 * Does the same as #gst_check_setup_sink_pad_by_name with the <emphasis> name </emphasis> parameter equal to "src".
 *
 * Returns: (transfer full): a new pad that can be used to check the output of @element
 */
GstPad *
gst_check_setup_sink_pad (GstElement * element, GstStaticPadTemplate * tmpl)
{
  return gst_check_setup_sink_pad_by_name (element, tmpl, "src");
}

/**
 * gst_check_setup_sink_pad_by_name:
 * @element: element to setup pad on
 * @tmpl: pad template
 * @name: Name of the @element src pad that will be linked to the sink pad that will be setup
 *
 * Creates a new sink pad (based on the given @tmpl) and links it to the given @element src pad
 * (the pad that matches the given @name).
 * You can set event/chain/query functions on this pad to check the output of the @element.
 *
 * Returns: (transfer full): a new pad that can be used to check the output of @element
 */
GstPad *
gst_check_setup_sink_pad_by_name (GstElement * element,
    GstStaticPadTemplate * tmpl, const gchar * name)
{
  GstPadTemplate *ptmpl = gst_static_pad_template_get (tmpl);
  GstPad *sinkpad;

  sinkpad =
      gst_check_setup_sink_pad_by_name_from_template (element, ptmpl, name);

  gst_object_unref (ptmpl);

  return sinkpad;
}

/**
 * gst_check_setup_sink_pad_from_template:
 * @element: element to setup pad on
 * @tmpl: pad template
 *
 * Returns: (transfer full): a new pad
 *
 * Since: 1.4
 */
GstPad *
gst_check_setup_sink_pad_from_template (GstElement * element,
    GstPadTemplate * tmpl)
{
  return gst_check_setup_sink_pad_by_name_from_template (element, tmpl, "src");
}

/**
 * gst_check_setup_sink_pad_by_name_from_template:
 * @element: element to setup pad on
 * @tmpl: pad template
 * @name: name
 *
 * Returns: (transfer full): a new pad
 *
 * Since: 1.4
 */
GstPad *
gst_check_setup_sink_pad_by_name_from_template (GstElement * element,
    GstPadTemplate * tmpl, const gchar * name)
{
  GstPad *srcpad, *sinkpad;

  /* receiving pad */
  sinkpad = gst_pad_new_from_template (tmpl, "sink");
  GST_DEBUG_OBJECT (element, "setting up receiving pad %p", sinkpad);
  fail_if (sinkpad == NULL, "Could not create a sinkpad");

  srcpad = gst_element_get_static_pad (element, name);
  if (srcpad == NULL)
    srcpad = gst_element_get_request_pad (element, name);
  fail_if (srcpad == NULL, "Could not get source pad from %s",
      GST_ELEMENT_NAME (element));
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);

  GST_DEBUG_OBJECT (element, "Linking element src pad and receiving sink pad");
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link %s source and sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  GST_DEBUG_OBJECT (element, "set up srcpad, refcount is 1");
  return sinkpad;
}

void
gst_check_teardown_sink_pad (GstElement * element)
{
  gst_check_teardown_pad_by_name (element, "src");
}

/**
 * gst_check_drop_buffers:
 *
 * Unref and remove all buffers that are in the global @buffers GList,
 * emptying the list.
 */
void
gst_check_drop_buffers (void)
{
  while (buffers != NULL) {
    gst_buffer_unref (GST_BUFFER (buffers->data));
    buffers = g_list_delete_link (buffers, buffers);
  }
}

/**
 * gst_check_caps_equal:
 * @caps1: first caps to compare
 * @caps2: second caps to compare
 *
 * Compare two caps with gst_caps_is_equal and fail unless they are
 * equal.
 */
void
gst_check_caps_equal (GstCaps * caps1, GstCaps * caps2)
{
  gchar *name1 = gst_caps_to_string (caps1);
  gchar *name2 = gst_caps_to_string (caps2);

  fail_unless (gst_caps_is_equal (caps1, caps2),
      "caps ('%s') is not equal to caps ('%s')", name1, name2);
  g_free (name1);
  g_free (name2);
}


/**
 * gst_check_buffer_data:
 * @buffer: buffer to compare
 * @data: data to compare to
 * @size: size of data to compare
 *
 * Compare the buffer contents with @data and @size.
 */
void
gst_check_buffer_data (GstBuffer * buffer, gconstpointer data, gsize size)
{
  GstMapInfo info;

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  GST_MEMDUMP ("Converted data", info.data, info.size);
  GST_MEMDUMP ("Expected data", data, size);
  if (memcmp (info.data, data, size) != 0) {
    g_print ("\nConverted data:\n");
    gst_util_dump_mem (info.data, info.size);
    g_print ("\nExpected data:\n");
    gst_util_dump_mem (data, size);
    fail ("buffer contents not equal");
  }
  gst_buffer_unmap (buffer, &info);
}

static gboolean
buffer_event_function (GstPad * pad, GstObject * noparent, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *event_caps;
    GstCaps *expected_caps = gst_pad_get_element_private (pad);

    gst_event_parse_caps (event, &event_caps);
    fail_unless (gst_caps_is_fixed (expected_caps));
    fail_unless (gst_caps_is_fixed (event_caps));
    fail_unless (gst_caps_is_equal_fixed (event_caps, expected_caps));
    gst_event_unref (event);
    return TRUE;
  }

  return gst_pad_event_default (pad, noparent, event);
}

/**
 * gst_check_element_push_buffer_list:
 * @element_name: name of the element that needs to be created
 * @buffer_in: (element-type GstBuffer) (transfer full): a list of buffers that needs to be
 *  pushed to the element
 * @caps_in: the #GstCaps expected of the sinkpad of the element
 * @buffer_out: (element-type GstBuffer) (transfer full): a list of buffers that we expect from
 * the element
 * @caps_out: the #GstCaps expected of the srcpad of the element
 * @last_flow_return: the last buffer push needs to give this GstFlowReturn
 *
 * Create an element using the factory providing the @element_name and push the
 * buffers in @buffer_in to this element. The element should create the buffers
 * equal to the buffers in @buffer_out. We only check the size and the data of
 * the buffers. This function unrefs the buffers in the two lists.
 * The last_flow_return parameter indicates the expected flow return value from
 * pushing the final buffer in the list.
 * This can be used to set up a test which pushes some buffers and then an
 * invalid buffer, when the final buffer is expected to fail, for example.
 */
/* FIXME 2.0: rename this function now that there's GstBufferList? */
void
gst_check_element_push_buffer_list (const gchar * element_name,
    GList * buffer_in, GstCaps * caps_in, GList * buffer_out,
    GstCaps * caps_out, GstFlowReturn last_flow_return)
{
  GstElement *element;
  GstPad *pad_peer;
  GstPad *sink_pad = NULL;
  GstPad *src_pad;
  GstBuffer *buffer;

  /* check that there are no buffers waiting */
  gst_check_drop_buffers ();
  /* create the element */
  element = gst_check_setup_element (element_name);
  fail_if (element == NULL, "failed to create the element '%s'", element_name);
  fail_unless (GST_IS_ELEMENT (element), "the element is no element");
  /* create the src pad */
  buffer = GST_BUFFER (buffer_in->data);

  fail_unless (GST_IS_BUFFER (buffer), "There should be a buffer in buffer_in");
  src_pad = gst_pad_new ("src", GST_PAD_SRC);
  if (caps_in) {
    fail_unless (gst_caps_is_fixed (caps_in));
    gst_pad_use_fixed_caps (src_pad);
  }
  /* activate the pad */
  gst_pad_set_active (src_pad, TRUE);
  GST_DEBUG ("src pad activated");
  gst_check_setup_events (src_pad, element, caps_in, GST_FORMAT_BYTES);
  pad_peer = gst_element_get_static_pad (element, "sink");
  fail_if (pad_peer == NULL);
  fail_unless (gst_pad_link (src_pad, pad_peer) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (pad_peer);
  /* don't create the sink_pad if there is no buffer_out list */
  if (buffer_out != NULL) {

    GST_DEBUG ("buffer out detected, creating the sink pad");
    /* get the sink caps */
    if (caps_out) {
      gchar *temp;

      fail_unless (gst_caps_is_fixed (caps_out));
      temp = gst_caps_to_string (caps_out);

      GST_DEBUG ("sink caps requested by buffer out: '%s'", temp);
      g_free (temp);
    }

    /* get the sink pad */
    sink_pad = gst_pad_new ("sink", GST_PAD_SINK);
    fail_unless (GST_IS_PAD (sink_pad));
    /* configure the sink pad */
    gst_pad_set_chain_function (sink_pad, gst_check_chain_func);
    gst_pad_set_active (sink_pad, TRUE);
    if (caps_out) {
      gst_pad_set_element_private (sink_pad, caps_out);
      gst_pad_set_event_function (sink_pad, buffer_event_function);
    }
    /* get the peer pad */
    pad_peer = gst_element_get_static_pad (element, "src");
    fail_unless (gst_pad_link (pad_peer, sink_pad) == GST_PAD_LINK_OK,
        "Could not link sink and %s source pads", GST_ELEMENT_NAME (element));
    gst_object_unref (pad_peer);
  }
  fail_unless (gst_element_set_state (element,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  /* push all the buffers in the buffer_in list */
  fail_unless (g_list_length (buffer_in) > 0, "the buffer_in list is empty");
  while (buffer_in != NULL) {
    GstBuffer *next_buffer = GST_BUFFER (buffer_in->data);

    fail_unless (GST_IS_BUFFER (next_buffer),
        "data in buffer_in should be a buffer");
    /* remove the buffer from the list */
    buffer_in = g_list_remove (buffer_in, next_buffer);
    if (buffer_in == NULL) {
      fail_unless (gst_pad_push (src_pad, next_buffer) == last_flow_return,
          "we expect something else from the last buffer");
    } else {
      fail_unless (gst_pad_push (src_pad, next_buffer) == GST_FLOW_OK,
          "Failed to push buffer in");
    }
  }
  fail_unless (gst_element_set_state (element,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  /* check that there is a buffer out */
  fail_unless_equals_int (g_list_length (buffers), g_list_length (buffer_out));
  while (buffers != NULL) {
    GstBuffer *new = GST_BUFFER (buffers->data);
    GstBuffer *orig = GST_BUFFER (buffer_out->data);
    GstMapInfo newinfo, originfo;

    fail_unless (gst_buffer_map (new, &newinfo, GST_MAP_READ));
    fail_unless (gst_buffer_map (orig, &originfo, GST_MAP_READ));

    GST_LOG ("orig buffer: size %" G_GSIZE_FORMAT, originfo.size);
    GST_LOG ("new  buffer: size %" G_GSIZE_FORMAT, newinfo.size);
    GST_MEMDUMP ("orig buffer", originfo.data, originfo.size);
    GST_MEMDUMP ("new  buffer", newinfo.data, newinfo.size);

    /* remove the buffers */
    buffers = g_list_remove (buffers, new);
    buffer_out = g_list_remove (buffer_out, orig);

    fail_unless (originfo.size == newinfo.size,
        "size of the buffers are not the same");
    fail_unless (memcmp (originfo.data, newinfo.data, newinfo.size) == 0,
        "data is not the same");
#if 0
    gst_check_caps_equal (GST_BUFFER_CAPS (orig), GST_BUFFER_CAPS (new));
#endif

    gst_buffer_unmap (orig, &originfo);
    gst_buffer_unmap (new, &newinfo);

    gst_buffer_unref (new);
    gst_buffer_unref (orig);
  }
  /* teardown the element and pads */
  gst_pad_set_active (src_pad, FALSE);
  gst_check_teardown_src_pad (element);
  gst_pad_set_active (sink_pad, FALSE);
  gst_check_teardown_sink_pad (element);
  gst_check_teardown_element (element);
}

/**
 * gst_check_element_push_buffer:
 * @element_name: name of the element that needs to be created
 * @buffer_in: push this buffer to the element
 * @caps_in: the #GstCaps expected of the sinkpad of the element
 * @buffer_out: compare the result with this buffer
 * @caps_out: the #GstCaps expected of the srcpad of the element
 *
 * Create an element using the factory providing the @element_name and
 * push the @buffer_in to this element. The element should create one buffer
 * and this will be compared with @buffer_out. We only check the caps
 * and the data of the buffers. This function unrefs the buffers.
 */
void
gst_check_element_push_buffer (const gchar * element_name,
    GstBuffer * buffer_in, GstCaps * caps_in, GstBuffer * buffer_out,
    GstCaps * caps_out)
{
  GList *in = NULL;
  GList *out = NULL;

  in = g_list_append (in, buffer_in);
  out = g_list_append (out, buffer_out);

  gst_check_element_push_buffer_list (element_name, in, caps_in, out, caps_out,
      GST_FLOW_OK);
}

void
gst_check_abi_list (GstCheckABIStruct list[], gboolean have_abi_sizes)
{
  if (have_abi_sizes) {
    gboolean ok = TRUE;
    gint i;

    for (i = 0; list[i].name; i++) {
      if (list[i].size != list[i].abi_size) {
        ok = FALSE;
        g_print ("sizeof(%s) is %d, expected %d\n",
            list[i].name, list[i].size, list[i].abi_size);
      }
    }
    fail_unless (ok, "failed ABI check");
  } else {
    const gchar *fn;

    if ((fn = g_getenv ("GST_ABI"))) {
      GError *err = NULL;
      GString *s;
      gint i;

      s = g_string_new ("\nGstCheckABIStruct list[] = {\n");
      for (i = 0; list[i].name; i++) {
        g_string_append_printf (s, "  {\"%s\", sizeof (%s), %d},\n",
            list[i].name, list[i].name, list[i].size);
      }
      g_string_append (s, "  {NULL, 0, 0}\n");
      g_string_append (s, "};\n");
      if (!g_file_set_contents (fn, s->str, s->len, &err)) {
        g_print ("%s", s->str);
        g_printerr ("\nFailed to write ABI information: %s\n", err->message);
        g_clear_error (&err);
      } else {
        g_print ("\nWrote ABI information to '%s'.\n", fn);
      }
      g_string_free (s, TRUE);
    } else {
      g_print ("No structure size list was generated for this architecture.\n");
      g_print ("Run with GST_ABI environment variable set to output header.\n");
    }
  }
}

gint
gst_check_run_suite (Suite * suite, const gchar * name, const gchar * fname)
{
  SRunner *sr;
  gchar *xmlfilename = NULL;
  gint nf;
  GTimer *timer;

  sr = srunner_create (suite);

  if (g_getenv ("GST_CHECK_XML")) {
    /* how lucky we are to have __FILE__ end in .c */
    xmlfilename = g_strdup_printf ("%sheck.xml", fname);

    srunner_set_xml (sr, xmlfilename);
  }

  timer = g_timer_new ();
  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  g_print ("Check suite %s ran in %.3fs (tests failed: %d)\n",
      name, g_timer_elapsed (timer, NULL), nf);
  g_timer_destroy (timer);
  g_free (xmlfilename);
  srunner_free (sr);
  return nf;
}

static gboolean
gst_check_have_checks_list (const gchar * env_var_name)
{
  const gchar *env_val;

  env_val = g_getenv (env_var_name);
  return (env_val != NULL && *env_val != '\0');
}

static gboolean
gst_check_func_is_in_list (const gchar * env_var, const gchar * func_name)
{
  const gchar *gst_checks;
  gboolean res = FALSE;
  gchar **funcs, **f;

  gst_checks = g_getenv (env_var);

  if (gst_checks == NULL || *gst_checks == '\0')
    return FALSE;

  /* only run specified functions */
  funcs = g_strsplit (gst_checks, ",", -1);
  for (f = funcs; f != NULL && *f != NULL; ++f) {
    if (g_pattern_match_simple (*f, func_name)) {
      res = TRUE;
      break;
    }
  }
  g_strfreev (funcs);
  return res;
}

gboolean
_gst_check_run_test_func (const gchar * func_name)
{
  /* if we have a whitelist, run it only if it's in the whitelist */
  if (gst_check_have_checks_list ("GST_CHECKS"))
    return gst_check_func_is_in_list ("GST_CHECKS", func_name);

  /* if we have a blacklist, run it only if it's not in the blacklist */
  if (gst_check_have_checks_list ("GST_CHECKS_IGNORE"))
    return !gst_check_func_is_in_list ("GST_CHECKS_IGNORE", func_name);

  /* no filter specified => run all checks */
  return TRUE;
}

/**
 * gst_check_setup_events_with_stream_id:
 * @srcpad: The src #GstPad to push on
 * @element: The #GstElement use to create the stream id
 * @caps: (allow-none): #GstCaps in case caps event must be sent
 * @format: The #GstFormat of the default segment to send
 * @stream_id: A unique identifier for the stream
 *
 * Push stream-start, caps and segment event, which consist of the minimum
 * required events to allow streaming. Caps is optional to allow raw src
 * testing.
 */
void
gst_check_setup_events_with_stream_id (GstPad * srcpad, GstElement * element,
    GstCaps * caps, GstFormat format, const gchar * stream_id)
{
  GstSegment segment;

  gst_segment_init (&segment, format);

  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start (stream_id)));
  if (caps)
    fail_unless (gst_pad_push_event (srcpad, gst_event_new_caps (caps)));
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&segment)));
}

/**
 * gst_check_setup_events:
 * @srcpad: The src #GstPad to push on
 * @element: The #GstElement use to create the stream id
 * @caps: (allow-none): #GstCaps in case caps event must be sent
 * @format: The #GstFormat of the default segment to send
 *
 * Push stream-start, caps and segment event, which consist of the minimum
 * required events to allow streaming. Caps is optional to allow raw src
 * testing. If @element has more than one src or sink pad, use
 * gst_check_setup_events_with_stream_id() instead.
 */
void
gst_check_setup_events (GstPad * srcpad, GstElement * element,
    GstCaps * caps, GstFormat format)
{
  gchar *stream_id;

  stream_id = gst_pad_create_stream_id (srcpad, element, NULL);
  gst_check_setup_events_with_stream_id (srcpad, element, caps, format,
      stream_id);
  g_free (stream_id);
}

typedef struct _DestroyedObjectStruct
{
  GObject *object;
  gboolean destroyed;
} DestroyedObjectStruct;

static void
weak_notify (DestroyedObjectStruct * destroyed, GObject ** object)
{
  destroyed->destroyed = TRUE;
}

/**
 * gst_check_objects_destroyed_on_unref:
 * @object_to_unref: The #GObject to unref
 * @first_object: (allow-none): The first object that should be destroyed as a
 * concequence of unrefing @object_to_unref.
 * @... : Additional object that should have been destroyed.
 *
 * Unrefs @object_to_unref and checks that is has properly been
 * destroyed, also checks that the other objects passed in
 * parametter have been destroyed as a concequence of
 * unrefing @object_to_unref. Last variable argument should be NULL.
 *
 * Since: 1.6
 */
void
gst_check_objects_destroyed_on_unref (gpointer object_to_unref,
    gpointer first_object, ...)
{
  GObject *object;
  GList *objs = NULL, *tmp;
  DestroyedObjectStruct *destroyed = g_slice_new0 (DestroyedObjectStruct);

  destroyed->object = object_to_unref;
  g_object_weak_ref (object_to_unref, (GWeakNotify) weak_notify, destroyed);
  objs = g_list_prepend (objs, destroyed);

  if (first_object) {
    va_list varargs;

    object = first_object;

    va_start (varargs, first_object);
    while (object) {
      destroyed = g_slice_new0 (DestroyedObjectStruct);
      destroyed->object = object;
      g_object_weak_ref (object, (GWeakNotify) weak_notify, destroyed);
      objs = g_list_prepend (objs, destroyed);
      object = va_arg (varargs, GObject *);
    }
    va_end (varargs);
  }
  gst_object_unref (object_to_unref);

  for (tmp = objs; tmp; tmp = tmp->next) {
    DestroyedObjectStruct *destroyed = tmp->data;

    if (!destroyed->destroyed) {
      fail_unless (destroyed->destroyed,
          "%s_%p is not destroyed, %d refcounts left!",
          GST_IS_OBJECT (destroyed->
              object) ? GST_OBJECT_NAME (destroyed->object) :
          G_OBJECT_TYPE_NAME (destroyed), destroyed->object,
          destroyed->object->ref_count);
    }
    g_slice_free (DestroyedObjectStruct, tmp->data);
  }
  g_list_free (objs);
}

/**
 * gst_check_object_destroyed_on_unref:
 * @object_to_unref: The #GObject to unref
 *
 * Unrefs @object_to_unref and checks that is has properly been
 * destroyed.
 *
 * Since: 1.6
 */
void
gst_check_object_destroyed_on_unref (gpointer object_to_unref)
{
  gst_check_objects_destroyed_on_unref (object_to_unref, NULL, NULL);
}

/* For ABI compatibility with GStreamer < 1.5 */
/* *INDENT-OFF* */
void
_fail_unless (int result, const char *file, int line, const char *expr, ...)
G_GNUC_PRINTF (4, 5);
/* *INDENT-ON* */

void
_fail_unless (int result, const char *file, int line, const char *expr, ...)
{
  gchar *msg;
  va_list args;

  if (result) {
    _mark_point (file, line);
    return;
  }

  va_start (args, expr);
  msg = g_strdup_vprintf (expr, args);
  va_end (args);

  _ck_assert_failed (file, line, msg, NULL);
  g_free (msg);
}
