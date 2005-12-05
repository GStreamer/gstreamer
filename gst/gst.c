/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst.c: Initialization and non-pipeline operations
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
 * SECTION:gst
 * @short_description: Media library supporting arbitrary formats and filter
 *                     graphs.
 * @see_also: Check out both <ulink url="http://www.cse.ogi.edu/sysl/">OGI's
 *            pipeline</ulink> and Microsoft's DirectShow for some background.
 *
 * GStreamer is a framework for constructing graphs of various filters
 * (termed elements here) that will handle streaming media.  Any discreet
 * (packetizable) media type is supported, with provisions for automatically
 * determining source type.  Formatting/framing information is provided with
 * a powerful negotiation framework.  Plugins are heavily used to provide for
 * all elements, allowing one to construct plugins outside of the GST
 * library, even released binary-only if license require (please don't).
 *
 * GStreamer borrows heavily from both the <ulink
 * url="http://www.cse.ogi.edu/sysl/">OGI media pipeline</ulink> and
 * Microsoft's DirectShow, hopefully taking the best of both and leaving the
 * cruft behind.  Its interface is still very fluid and thus can be changed
 * to increase the sanity/noise ratio.
 *
 * The <application>GStreamer</application> library should be initialized with
 * gst_init() before it can be used. You should pass pointers to the main argc
 * and argv variables so that GStreamer can process its own command line
 * options, as shown in the following example.
 *
 * <example>
 * <title>Initializing the gstreamer library</title>
 * <programlisting language="c">
 * int
 * main (int argc, char *argv[])
 * {
 *   // initialize the GStreamer library
 *   gst_init (&amp;argc, &amp;argv);
 *   ...
 * }
 * </programlisting>
 * </example>
 *
 * It's allowed to pass two NULL pointers to gst_init() in case you don't want
 * to pass the command line args to GStreamer.
 *
 * You can also use GOption to initialize your own parameters as shown in
 * the next code fragment:
 * <example>
 * <title>Initializing own parameters when initializing gstreamer</title>
 * <programlisting>
 * static gboolean stats = FALSE;
 * ...
 * int
 * main (int argc, char *argv[])
 * {
 *  GOptionEntry options[] = {
 *   {"tags", 't', 0, G_OPTION_ARG_NONE, &amp;tags,
 *       N_("Output tags (also known as metadata)"), NULL},
 *   {NULL}
 *  };
 *  ctx = g_option_context_new ("gst-launch");
 *  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
 *  g_option_context_add_group (ctx, gst_init_get_option_group ());
 *  if (!g_option_context_parse (ctx, &amp;argc, &amp;argv, &amp;err)) {
 *    g_print ("Error initializing: &percnt;s\n", GST_STR_NULL (err->message));
 *    exit (1);
 *  }
 *  g_option_context_free (ctx);
 * ...
 * }
 * </programlisting>
 * </example>
 *
 * Use gst_version() to query the library version at runtime or use the
 * GST_VERSION_* macros to find the version at compile time. Optionally
 * gst_version_string() returns a printable string.
 *
 * The gst_deinit() call is used to clean up all internal resources used
 * by <application>GStreamer</application>. It is mostly used in unit tests 
 * to check for leaks.
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#include <stdlib.h>
#include <stdio.h>

#include "gst_private.h"
#include "gst-i18n-lib.h"
#include <locale.h>             /* for LC_ALL */

#include "gst.h"

#define GST_CAT_DEFAULT GST_CAT_GST_INIT

#define MAX_PATH_SPLIT	16
#define GST_PLUGIN_SEPARATOR ","

static gboolean gst_initialized = FALSE;

extern gint _gst_trace_on;

/* set to TRUE when segfaults need to be left as is */
gboolean _gst_disable_segtrap = FALSE;


static void load_plugin_func (gpointer data, gpointer user_data);
static gboolean init_pre (void);
static gboolean init_post (void);
static gboolean parse_goption_arg (const gchar * s_opt,
    const gchar * arg, gpointer data, GError ** err);

static GSList *preload_plugins = NULL;

const gchar *g_log_domain_gstreamer = "GStreamer";

static void
debug_log_handler (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer user_data)
{
  g_log_default_handler (log_domain, log_level, message, user_data);
  /* FIXME: do we still need this ? fatal errors these days are all
   * other than core errors */
  /* g_on_error_query (NULL); */
}

enum
{
  ARG_VERSION = 1,
  ARG_FATAL_WARNINGS,
#ifndef GST_DISABLE_GST_DEBUG
  ARG_DEBUG_LEVEL,
  ARG_DEBUG,
  ARG_DEBUG_DISABLE,
  ARG_DEBUG_NO_COLOR,
  ARG_DEBUG_HELP,
#endif
  ARG_PLUGIN_SPEW,
  ARG_PLUGIN_PATH,
  ARG_PLUGIN_LOAD,
  ARG_SEGTRAP_DISABLE
};

/* debug-spec ::= category-spec [, category-spec]*
 * category-spec ::= category:val | val
 * category ::= [^:]+
 * val ::= [0-5]
 */

#ifndef NUL
#define NUL '\0'
#endif

#ifndef GST_DISABLE_GST_DEBUG
static gboolean
parse_debug_category (gchar * str, const gchar ** category)
{
  if (!str)
    return FALSE;

  /* works in place */
  g_strstrip (str);

  if (str[0] != NUL) {
    *category = str;
    return TRUE;
  }

  return FALSE;
}

static gboolean
parse_debug_level (gchar * str, gint * level)
{
  if (!str)
    return FALSE;

  /* works in place */
  g_strstrip (str);

  if (str[0] != NUL && str[1] == NUL
      && str[0] >= '0' && str[0] < '0' + GST_LEVEL_COUNT) {
    *level = str[0] - '0';
    return TRUE;
  }

  return FALSE;
}

static void
parse_debug_list (const gchar * list)
{
  gchar **split;
  gchar **walk;

  g_return_if_fail (list != NULL);

  split = g_strsplit (list, ",", 0);

  for (walk = split; *walk; walk++) {
    if (strchr (*walk, ':')) {
      gchar **values = g_strsplit (*walk, ":", 2);

      if (values[0] && values[1]) {
        gint level;
        const gchar *category;

        if (parse_debug_category (values[0], &category)
            && parse_debug_level (values[1], &level))
          gst_debug_set_threshold_for_name (category, level);
      }

      g_strfreev (values);
    } else {
      gint level;

      if (parse_debug_level (*walk, &level))
        gst_debug_set_default_threshold (level);
    }
  }

  g_strfreev (split);
}
#endif

#ifndef GST_HAVE_GLIB_2_8
#define G_OPTION_FLAG_NO_ARG 0
#endif

/**
 * gst_init_get_option_group:
 *
 * Returns a #GOptionGroup with GStreamer's argument specifications. The
 * group is set up to use standard GOption callbacks, so when using this
 * group in combination with GOption parsing methods, all argument parsing
 * and initialization is automated.
 *
 * This function is useful if you want to integrate GStreamer with other
 * libraries that use GOption (see g_option_context_add_group() ).
 *
 * Returns: a pointer to GStreamer's option group. Should be dereferenced
 * after use.
 */

GOptionGroup *
gst_init_get_option_group (void)
{
  GOptionGroup *group;
  static GOptionEntry gst_args[] = {
    {"gst-version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
        parse_goption_arg, N_("Print the GStreamer version"), NULL},
    {"gst-fatal-warnings", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
        parse_goption_arg, N_("Make all warnings fatal"), NULL},
#ifndef GST_DISABLE_GST_DEBUG
    {"gst-debug-help", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          parse_goption_arg, N_("Print available debug categories and exit"),
        NULL},
    {"gst-debug-level", 0, 0, G_OPTION_ARG_CALLBACK, parse_goption_arg,
          N_("Default debug level from 1 (only error) to 5 (anything) or "
              "0 for no output"),
        N_("LEVEL")},
    {"gst-debug", 0, 0, G_OPTION_ARG_CALLBACK, parse_goption_arg,
          N_("Comma-separated list of category_name:level pairs to set "
              "specific levels for the individual categories. Example: "
              "GST_AUTOPLUG:5,GST_ELEMENT_*:3"),
        N_("LIST")},
    {"gst-debug-no-color", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
        parse_goption_arg, N_("Disable colored debugging output"), NULL},
    {"gst-debug-disable", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
        parse_goption_arg, N_("Disable debugging"), NULL},
#endif
    {"gst-plugin-spew", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          parse_goption_arg, N_("Enable verbose plugin loading diagnostics"),
        NULL},
    {"gst-plugin-path", 0, 0, G_OPTION_ARG_CALLBACK, parse_goption_arg,
        N_("Colon-separated paths containing plugins"), N_("PATHS")},
    {"gst-plugin-load", 0, 0, G_OPTION_ARG_CALLBACK, parse_goption_arg,
          N_("Comma-separated list of plugins to preload in addition to the "
              "list stored in environment variable GST_PLUGIN_PATH"),
        N_("PLUGINS")},
    {"gst-disable-segtrap", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          parse_goption_arg,
          N_("Disable trapping of segmentation faults during plugin loading"),
        NULL},
    {NULL}
  };

  group = g_option_group_new ("gst", _("GStreamer Options"),
      _("Show GStreamer Options"), NULL, NULL);
  g_option_group_set_parse_hooks (group, (GOptionParseFunc) init_pre,
      (GOptionParseFunc) init_post);

  g_option_group_add_entries (group, gst_args);
  g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);

  return group;
}

/**
 * gst_init_check:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 * @err: pointer to a #GError to which a message will be posted on error
 *
 * Initializes the GStreamer library, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 *
 * This function will return %FALSE if GStreamer could not be initialized
 * for some reason.  If you want your program to fail fatally,
 * use gst_init() instead.
 *
 * Returns: %TRUE if GStreamer could be initialized.
 */
gboolean
gst_init_check (int *argc, char **argv[], GError ** err)
{
  GOptionGroup *group;
  GOptionContext *ctx;
  gboolean res;

  if (gst_initialized) {
    GST_DEBUG ("already initialized gst");
    return TRUE;
  }

  ctx = g_option_context_new ("- GStreamer initialization");
  g_option_context_set_ignore_unknown_options (ctx, TRUE);
  group = gst_init_get_option_group ();
  g_option_context_add_group (ctx, group);
  res = g_option_context_parse (ctx, argc, argv, err);
  g_option_context_free (ctx);

  if (res) {
    gst_initialized = TRUE;
  }

  return res;
}

/**
 * gst_init:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 *
 * Initializes the GStreamer library, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 *
 * <note><para>
 * This function will terminate your program if it was unable to initialize
 * GStreamer for some reason.  If you want your program to fall back,
 * use gst_init_check() instead.
 * </para></note>
 *
 * WARNING: This function does not work in the same way as corresponding
 * functions in other glib-style libraries, such as gtk_init().  In
 * particular, unknown command line options cause this function to
 * abort program execution.
 */
void
gst_init (int *argc, char **argv[])
{
  GError *err = NULL;

  if (!gst_init_check (argc, argv, &err)) {
    g_print ("Could not initialized GStreamer: %s\n",
        err ? err->message : "unknown error occurred");
    if (err) {
      g_error_free (err);
    }
    exit (1);
  }
}

#ifndef GST_DISABLE_REGISTRY
static void
add_path_func (gpointer data, gpointer user_data)
{
  GST_INFO ("Adding plugin path: \"%s\"", (gchar *) data);
  gst_registry_scan_path (gst_registry_get_default (), (gchar *) data);
}
#endif

static void
prepare_for_load_plugin_func (gpointer data, gpointer user_data)
{
  preload_plugins = g_slist_prepend (preload_plugins, data);
}

static void
load_plugin_func (gpointer data, gpointer user_data)
{
  GstPlugin *plugin;
  const gchar *filename;
  GError *err = NULL;

  filename = (const gchar *) data;

  plugin = gst_plugin_load_file (filename, &err);

  if (plugin) {
    GST_INFO ("Loaded plugin: \"%s\"", filename);

    gst_default_registry_add_plugin (plugin);
  } else {
    if (err) {
      /* Report error to user, and free error */
      GST_ERROR ("Failed to load plugin: %s\n", err->message);
      g_error_free (err);
    } else {
      GST_WARNING ("Failed to load plugin: \"%s\"", filename);
    }
  }

  g_free (data);
}

static void
split_and_iterate (const gchar * stringlist, gchar * separator, GFunc iterator,
    gpointer user_data)
{
  gchar **strings;
  gint j = 0;
  gchar *lastlist = g_strdup (stringlist);

  while (lastlist) {
    strings = g_strsplit (lastlist, separator, MAX_PATH_SPLIT);
    g_free (lastlist);
    lastlist = NULL;

    while (strings[j]) {
      iterator (strings[j], user_data);
      if (++j == MAX_PATH_SPLIT) {
        lastlist = g_strdup (strings[j]);
        g_strfreev (strings);
        j = 0;
        break;
      }
    }
    g_strfreev (strings);
  }
}

/* we have no fail cases yet, but maybe in the future */
static gboolean
init_pre (void)
{
  g_type_init ();

  if (g_thread_supported ()) {
    /* somebody already initialized threading */
  } else {
    g_thread_init (NULL);
  }
  /* we need threading to be enabled right here */
  _gst_debug_init ();

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

#ifndef GST_DISABLE_GST_DEBUG
  {
    const gchar *debug_list;

    if (g_getenv ("GST_DEBUG_NO_COLOR") != NULL)
      gst_debug_set_colored (FALSE);

    debug_list = g_getenv ("GST_DEBUG");
    if (debug_list) {
      parse_debug_list (debug_list);
    }
  }
#endif
  /* This is the earliest we can make stuff show up in the logs.
   * So give some useful info about GStreamer here */
  GST_INFO ("Initializing GStreamer Core Library version %s", VERSION);
  GST_INFO ("Using library installed in %s", LIBDIR);

  return TRUE;
}

static gboolean
gst_register_core_elements (GstPlugin * plugin)
{
  /* register some standard builtin types */
  if (!gst_element_register (plugin, "bin", GST_RANK_PRIMARY,
          GST_TYPE_BIN) ||
      !gst_element_register (plugin, "pipeline", GST_RANK_PRIMARY,
          GST_TYPE_PIPELINE)
      )
    g_assert_not_reached ();

  return TRUE;
}

static GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "staticelements",
  "core elements linked into the GStreamer library",
  gst_register_core_elements,
  VERSION,
  GST_LICENSE,
  PACKAGE,
  GST_PACKAGE_NAME,
  GST_PACKAGE_ORIGIN,

  GST_PADDING_INIT
};

/*
 * this bit handles:
 * - initalization of threads if we use them
 * - log handler
 * - initial output
 * - initializes gst_format
 * - registers a bunch of types for gst_objects
 *
 * - we don't have cases yet where this fails, but in the future
 *   we might and then it's nice to be able to return that
 */
static gboolean
init_post (void)
{
  GLogLevelFlags llf;

#ifndef GST_DISABLE_TRACE
  GstTrace *gst_trace;
#endif /* GST_DISABLE_TRACE */

  llf = G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL;
  g_log_set_handler (g_log_domain_gstreamer, llf, debug_log_handler, NULL);

  _gst_format_initialize ();
  _gst_query_initialize ();
  gst_object_get_type ();
  gst_pad_get_type ();
  gst_element_factory_get_type ();
  gst_element_get_type ();
  gst_type_find_factory_get_type ();
  gst_bin_get_type ();

#ifndef GST_DISABLE_INDEX
  gst_index_factory_get_type ();
#endif /* GST_DISABLE_INDEX */
#ifndef GST_DISABLE_URI
  gst_uri_handler_get_type ();
#endif /* GST_DISABLE_URI */

  /* register core plugins */
  _gst_plugin_register_static (&plugin_desc);

  gst_structure_get_type ();
  _gst_value_initialize ();
  gst_caps_get_type ();
  _gst_plugin_initialize ();
  _gst_event_initialize ();
  _gst_buffer_initialize ();
  _gst_message_initialize ();
  _gst_tag_initialize ();

#ifndef GST_DISABLE_REGISTRY
  {
    char *registry_file;
    const char *plugin_path;
    GstRegistry *default_registry;

    default_registry = gst_registry_get_default ();
    registry_file = g_strdup (g_getenv ("GST_REGISTRY"));
    if (registry_file == NULL) {
      registry_file = g_build_filename (g_get_home_dir (),
          ".gstreamer-" GST_MAJORMINOR, "registry.xml", NULL);
    }
    GST_DEBUG ("Reading registry cache");
    gst_registry_xml_read_cache (default_registry, registry_file);

    /* GST_PLUGIN_PATH specifies a list of directories to scan for
     * additional plugins.  These take precedence over the system plugins */
    plugin_path = g_getenv ("GST_PLUGIN_PATH");
    if (plugin_path) {
      char **list;
      int i;

      GST_DEBUG ("GST_PLUGIN_PATH set to %s", plugin_path);
      list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
      for (i = 0; list[i]; i++) {
        gst_registry_scan_path (default_registry, list[i]);
      }
      g_strfreev (list);
    } else {
      GST_DEBUG ("GST_PLUGIN_PATH not set");
    }

    /* GST_PLUGIN_SYSTEM_PATH specifies a list of plugins that are always
     * loaded by default.  If not set, this defaults to the system-installed
     * path, and the plugins installed in the user's home directory */
    plugin_path = g_getenv ("GST_PLUGIN_SYSTEM_PATH");
    if (plugin_path == NULL) {
      char *home_plugins;

      GST_DEBUG ("GST_PLUGIN_SYSTEM_PATH not set");

      /* plugins in the user's home directory take precedence over
       * system-installed ones */
      home_plugins = g_build_filename (g_get_home_dir (),
          ".gstreamer-" GST_MAJORMINOR, "plugins", NULL);
      gst_registry_scan_path (default_registry, home_plugins);
      g_free (home_plugins);

      /* add the main (installed) library path */
      gst_registry_scan_path (default_registry, PLUGINDIR);
    } else {
      char **list;
      int i;

      GST_DEBUG ("GST_PLUGIN_SYSTEM_PATH set to %s", plugin_path);
      list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
      for (i = 0; list[i]; i++) {
        gst_registry_scan_path (default_registry, list[i]);
      }
      g_strfreev (list);
    }

    gst_registry_xml_write_cache (default_registry, registry_file);

    _gst_registry_remove_cache_plugins (default_registry);

    g_free (registry_file);
  }

#endif /* GST_DISABLE_REGISTRY */

  /* if we need to preload plugins */
  if (preload_plugins) {
    g_slist_foreach (preload_plugins, load_plugin_func, NULL);
    g_slist_free (preload_plugins);
    preload_plugins = NULL;
  }
#ifndef GST_DISABLE_TRACE
  _gst_trace_on = 0;
  if (_gst_trace_on) {
    gst_trace = gst_trace_new ("gst.trace", 1024);
    gst_trace_set_default (gst_trace);
  }
#endif /* GST_DISABLE_TRACE */

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
static gboolean
select_all (GstPlugin * plugin, gpointer user_data)
{
  return TRUE;
}

static gint
sort_by_category_name (gconstpointer a, gconstpointer b)
{
  return strcmp (gst_debug_category_get_name ((GstDebugCategory *) a),
      gst_debug_category_get_name ((GstDebugCategory *) b));
}
static void
gst_debug_help (void)
{
  GSList *list, *walk;
  GList *list2, *g;

  if (!init_post ())
    exit (1);

  list2 = gst_registry_plugin_filter (gst_registry_get_default (),
      select_all, FALSE, NULL);

  /* FIXME this is gross.  why don't debug have categories PluginFeatures? */
  for (g = list2; g; g = g_list_next (g)) {
    GstPlugin *plugin = GST_PLUGIN (g->data);

    gst_plugin_load (plugin);
  }
  g_list_free (list2);

  list = gst_debug_get_all_categories ();
  walk = list = g_slist_sort (list, sort_by_category_name);

  g_print ("\n");
  g_print ("name                  level    description\n");
  g_print ("---------------------+--------+--------------------------------\n");

  while (walk) {
    GstDebugCategory *cat = (GstDebugCategory *) walk->data;

    if (gst_debug_is_colored ()) {
      gchar *color = gst_debug_construct_term_color (cat->color);

      g_print ("%s%-20s\033[00m  %1d %s  %s%s\033[00m\n",
          color,
          gst_debug_category_get_name (cat),
          gst_debug_category_get_threshold (cat),
          gst_debug_level_get_name (gst_debug_category_get_threshold (cat)),
          color, gst_debug_category_get_description (cat));
      g_free (color);
    } else {
      g_print ("%-20s  %1d %s  %s\n", gst_debug_category_get_name (cat),
          gst_debug_category_get_threshold (cat),
          gst_debug_level_get_name (gst_debug_category_get_threshold (cat)),
          gst_debug_category_get_description (cat));
    }
    walk = g_slist_next (walk);
  }
  g_slist_free (list);
  g_print ("\n");
}
#endif

static gboolean
parse_one_option (gint opt, const gchar * arg, GError ** err)
{
  switch (opt) {
    case ARG_VERSION:
      g_print ("GStreamer Core Library version %s\n", GST_VERSION);
      exit (0);
    case ARG_FATAL_WARNINGS:{
      GLogLevelFlags fatal_mask;

      fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
      fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
      g_log_set_always_fatal (fatal_mask);
      break;
    }
#ifndef GST_DISABLE_GST_DEBUG
    case ARG_DEBUG_LEVEL:{
      gint tmp = 0;

      tmp = strtol (arg, NULL, 0);
      if (tmp >= 0 && tmp < GST_LEVEL_COUNT) {
        gst_debug_set_default_threshold (tmp);
      }
      break;
    }
    case ARG_DEBUG:
      parse_debug_list (arg);
      break;
    case ARG_DEBUG_NO_COLOR:
      gst_debug_set_colored (FALSE);
      break;
    case ARG_DEBUG_DISABLE:
      gst_debug_set_active (FALSE);
      break;
    case ARG_DEBUG_HELP:
      gst_debug_help ();
      exit (0);
#endif
    case ARG_PLUGIN_SPEW:
      break;
    case ARG_PLUGIN_PATH:
#ifndef GST_DISABLE_REGISTRY
      split_and_iterate (arg, G_SEARCHPATH_SEPARATOR_S, add_path_func, NULL);
#endif /* GST_DISABLE_REGISTRY */
      break;
    case ARG_PLUGIN_LOAD:
      split_and_iterate (arg, ",", prepare_for_load_plugin_func, NULL);
      break;
    case ARG_SEGTRAP_DISABLE:
      _gst_disable_segtrap = TRUE;
      break;
    default:
      g_set_error (err, G_OPTION_ERROR, G_OPTION_ERROR_UNKNOWN_OPTION,
          _("Unknown option"));
      return FALSE;
  }

  return TRUE;
}

static gboolean
parse_goption_arg (const gchar * opt,
    const gchar * arg, gpointer data, GError ** err)
{
  const struct
  {
    gchar *opt;
    int val;
  } options[] = {
    {
    "--gst-version", ARG_VERSION}, {
    "--gst-fatal-warnings", ARG_FATAL_WARNINGS},
#ifndef GST_DISABLE_GST_DEBUG
    {
    "--gst-debug-level", ARG_DEBUG_LEVEL}, {
    "--gst-debug", ARG_DEBUG}, {
    "--gst-debug-disable", ARG_DEBUG_DISABLE}, {
    "--gst-debug-no-color", ARG_DEBUG_NO_COLOR}, {
    "--gst-debug-help", ARG_DEBUG_HELP},
#endif
    {
    "--gst-plugin-spew", ARG_PLUGIN_SPEW}, {
    "--gst-plugin-path", ARG_PLUGIN_PATH}, {
    "--gst-plugin-load", ARG_PLUGIN_LOAD}, {
    "--gst-disable-segtrap", ARG_SEGTRAP_DISABLE}, {
    NULL}
  };
  gint val = 0, n;

  for (n = 0; options[n].opt; n++) {
    if (!strcmp (opt, options[n].opt)) {
      val = options[n].val;
      break;
    }
  }

  return parse_one_option (val, arg, err);
}

/**
 * gst_deinit:
 *
 * Clean up.
 * Call only once, before exiting.
 * After this call GStreamer should not be used anymore.
 */

extern GstRegistry *_gst_registry_default;
void
gst_deinit (void)
{
  GstClock *clock;

  GST_INFO ("deinitializing GStreamer");
  clock = gst_system_clock_obtain ();
  gst_object_unref (clock);
  gst_object_unref (clock);

  _gst_registry_cleanup ();

  gst_initialized = FALSE;
  GST_INFO ("deinitialized GStreamer");
}

/**
 * gst_version:
 * @major: pointer to a guint to store the major version number
 * @minor: pointer to a guint to store the minor version number
 * @micro: pointer to a guint to store the micro version number
 * @nano:  pointer to a guint to store the nano version number
 *
 * Gets the version number of the GStreamer library.
 */
void
gst_version (guint * major, guint * minor, guint * micro, guint * nano)
{
  g_return_if_fail (major);
  g_return_if_fail (minor);
  g_return_if_fail (micro);
  g_return_if_fail (nano);

  *major = GST_VERSION_MAJOR;
  *minor = GST_VERSION_MINOR;
  *micro = GST_VERSION_MICRO;
  *nano = GST_VERSION_NANO;
}

/**
 * gst_version_string:
 *
 * This function returns a string that is useful for describing this version
 * of GStreamer to the outside world: user agent strings, logging, ...
 *
 * Returns: a newly allocated string describing this version of GStreamer.
 */

gchar *
gst_version_string ()
{
  guint major, minor, micro, nano;

  gst_version (&major, &minor, &micro, &nano);
  if (nano == 0)
    return g_strdup_printf ("GStreamer %d.%d.%d", major, minor, micro);
  else if (nano == 1)
    return g_strdup_printf ("GStreamer %d.%d.%d (CVS)", major, minor, micro);
  else
    return g_strdup_printf ("GStreamer %d.%d.%d (prerelease)", major, minor,
        micro);
}
