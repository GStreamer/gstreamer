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

#include <stdlib.h>
#include <stdio.h>

#include "gst_private.h"
#include "gst-i18n-lib.h"
#include <locale.h>		/* for LC_ALL */

#include "gst.h"
#include "gstqueue.h"
#ifndef GST_DISABLE_REGISTRY
#include "registries/gstxmlregistry.h"
#endif /* GST_DISABLE_REGISTRY */
#include "gstregistrypool.h"

#define GST_CAT_DEFAULT GST_CAT_GST_INIT

#define MAX_PATH_SPLIT	16
#define GST_PLUGIN_SEPARATOR ","

gchar *_gst_progname;

#ifndef GST_DISABLE_REGISTRY
gboolean _gst_registry_auto_load = TRUE;
static GstRegistry *_global_registry;
static GstRegistry *_user_registry;
static gboolean _gst_registry_fixed = FALSE;
#endif

static gboolean _gst_use_threads = TRUE;

static gboolean _gst_enable_cpu_opt = TRUE;

static gboolean gst_initialized = FALSE;
/* this will be set in popt callbacks when a problem has been encountered */
static gboolean _gst_initialization_failure = FALSE;
extern gint _gst_trace_on;

/* set to TRUE when segfaults need to be left as is */
gboolean _gst_disable_segtrap = FALSE;

extern GThreadFunctions gst_thread_dummy_functions;


static void	load_plugin_func	(gpointer data, gpointer user_data);
static void	init_popt_callback	(poptContext context,
		                         enum poptCallbackReason reason,
                                         const struct poptOption *option,
					 const char *arg, void *data);
static gboolean	init_pre		(void);
static gboolean	init_post		(void);

static GSList *preload_plugins = NULL;

const gchar *g_log_domain_gstreamer = "GStreamer";

static void
debug_log_handler (const gchar *log_domain,
		   GLogLevelFlags log_level,
		   const gchar *message,
		   gpointer user_data)
{
  g_log_default_handler (log_domain, log_level, message, user_data);
  /* FIXME: do we still need this ? fatal errors these days are all
   * other than core errors */
  /* g_on_error_query (NULL); */
}

enum {
  ARG_VERSION=1,
  ARG_FATAL_WARNINGS,
#ifndef GST_DISABLE_GST_DEBUG
  ARG_DEBUG_LEVEL,
  ARG_DEBUG,
  ARG_DEBUG_DISABLE,
  ARG_DEBUG_NO_COLOR,
  ARG_DEBUG_HELP,
#endif
  ARG_DISABLE_CPU_OPT,
  ARG_PLUGIN_SPEW,
  ARG_PLUGIN_PATH,
  ARG_PLUGIN_LOAD,
  ARG_SEGTRAP_DISABLE,
  ARG_SCHEDULER,
  ARG_REGISTRY
};

#ifndef NUL
#define NUL '\0'
#endif

/* default scheduler, can be changed in gstscheduler.h with
 * the GST_SCHEDULER_DEFAULT_NAME define.
 */
static const struct poptOption gstreamer_options[] = {
  {NULL, NUL, POPT_ARG_CALLBACK|POPT_CBFLAG_PRE|POPT_CBFLAG_POST, (void *) &init_popt_callback, 0, NULL, NULL},
  {"gst-version",        NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_VERSION,        N_("Print the GStreamer version"), NULL},
  {"gst-fatal-warnings", NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_FATAL_WARNINGS, N_("Make all warnings fatal"), NULL},
#ifndef GST_DISABLE_GST_DEBUG
  {"gst-debug-level",    NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_DEBUG_LEVEL,  N_("Default debug level from 1 (only error) to 5 (anything) or 0 for no output"), N_("LEVEL")},
  {"gst-debug",          NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_DEBUG,          N_("Comma-separated list of category_name:level pairs to set specific levels for the individual categories.\nExample: GST_AUTOPLUG:5,GST_ELEMENT_*:3"), N_("CATEGORIES")},
  {"gst-debug-no-color", NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_DEBUG_NO_COLOR, N_("Disable color debugging output"), NULL},
  {"gst-disable-debug",  NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_DEBUG_DISABLE,  N_("Disable debugging")},
  {"gst-debug-help",     NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_DEBUG_HELP,     N_("Print available debug categories and exit"), NULL},
#endif
  {"gst-disable-cpu-opt",NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,	  NULL, ARG_DISABLE_CPU_OPT,N_("Disable accelerated CPU instructions"), NULL},
  {"gst-plugin-spew",    NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_PLUGIN_SPEW,    N_("Enable verbose plugin loading diagnostics"), NULL},
  {"gst-plugin-path",    NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_PATH,    N_("path list for loading plugins (separated by '" G_SEARCHPATH_SEPARATOR_S "')"),N_("PATHS")},
  {"gst-plugin-load",    NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_LOAD,    N_("Comma-separated list of plugins to preload in addition to the list stored in env variable GST_PLUGIN_PATH"), N_("PLUGINS")},
  {"gst-disable-segtrap",NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_SEGTRAP_DISABLE,N_("Disable trapping of segmentation faults during plugin loading"), NULL},
  {"gst-scheduler",      NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_SCHEDULER,      N_("Scheduler to use ('"GST_SCHEDULER_DEFAULT_NAME"' is the default)"), N_("SCHEDULER")},
  {"gst-registry",       NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_REGISTRY,       N_("Registry to use") , N_("REGISTRY")},
  POPT_TABLEEND
};

/**
 * gst_init_get_popt_table:
 *
 * Returns a popt option table with GStreamer's argument specifications. The
 * table is set up to use popt's callback method, so whenever the parsing is
 * actually performed (via poptGetContext), the GStreamer libraries will
 * be initialized.
 *
 * Returns: a pointer to the static GStreamer option table.
 * No free is necessary.
 */
const struct poptOption *
gst_init_get_popt_table (void)
{
  return gstreamer_options;
}

/**
 * gst_init_check:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
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
gst_init_check (int *argc, char **argv[])
{
  return gst_init_check_with_popt_table (argc, argv, NULL);
}

/**
 * gst_init:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 *
 * Initializes the GStreamer library, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 *
 * This function will terminate your program if it was unable to initialize
 * GStreamer for some reason.  If you want your program to fall back,
 * use gst_init_check() instead.
 */
void
gst_init (int *argc, char **argv[])
{
  gst_init_with_popt_table (argc, argv, NULL);
}

/**
 * gst_init_with_popt_table:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 * @popt_options: pointer to a popt table to append
 *
 * Initializes the GStreamer library, parsing the options,
 * setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 *
 * This function will terminate your program if it was unable to initialize
 * GStreamer for some reason.  If you want your program to fall back,
 * use gst_init_check_with_popt_table() instead.
 */
void
gst_init_with_popt_table (int *argc, char **argv[],
	                  const struct poptOption *popt_options)
{
  if (!gst_init_check_with_popt_table (argc, argv, popt_options)) {
    g_print ("Could not initialize GStreamer !\n");
    exit (1);
  }
}
/**
 * gst_init_check_with_popt_table:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 * @popt_options: pointer to a popt table to append
 *
 * Initializes the GStreamer library, parsing the options,
 * setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 *
 * Returns: %TRUE if GStreamer could be initialized.
 */
gboolean
gst_init_check_with_popt_table (int *argc, char **argv[],
		                const struct poptOption *popt_options)
{
  poptContext context;
  gint nextopt;
  struct poptOption *options;
  struct poptOption options_with[] = {
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 				 0, "Help options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gstreamer_options,	 0, "GStreamer options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) popt_options, 		 0, "Application options:", NULL},
    POPT_TABLEEND
  };
  struct poptOption options_without[] = {
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 				 0, "Help options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gstreamer_options,	 0, "GStreamer options:", NULL},
    POPT_TABLEEND
  };

  if (gst_initialized)
  {
    GST_DEBUG ("already initialized gst");
    return TRUE;
  }

  if (!argc || !argv) {
    if (argc || argv)
      g_warning ("gst_init: Only one of argc or argv was NULL");

    if (!init_pre ()) return FALSE;
    if (!init_post ()) return FALSE;
    gst_initialized = TRUE;
    return TRUE;
  }

  if (popt_options == NULL) {
    options = options_without;
  } else {
    options = options_with;
  }
  context = poptGetContext ("GStreamer", *argc, (const char**)*argv,
		            options, 0);

  while ((nextopt = poptGetNextOpt (context)) > 0)
  {
    /* we only check for failures here, actual work is done in callbacks */
    if (_gst_initialization_failure) return FALSE;
  }

  if (nextopt != -1) {
    g_print ("Error on option %s: %s.\nRun '%s --help' "
	     "to see a full list of available command line options.\n",
             poptBadOption (context, 0),
             poptStrerror (nextopt),
             (*argv)[0]);

    poptFreeContext (context);
    return FALSE;
  }

  *argc = poptStrippedArgv (context, *argc, *argv);

  poptFreeContext (context);

  return TRUE;
}

#ifndef GST_DISABLE_REGISTRY
static void
add_path_func (gpointer data, gpointer user_data)
{
  GstRegistry *registry = GST_REGISTRY (user_data);

  GST_INFO ("Adding plugin path: \"%s\"", (gchar *) data);
  gst_registry_add_path (registry, (gchar *)data);
}
#endif

static void
prepare_for_load_plugin_func (gpointer data, gpointer user_data)
{
  preload_plugins = g_slist_prepend (preload_plugins, data);
}

static void
parse_debug_list (const gchar *list)
{
  gchar **split;
  gchar **walk;

  g_return_if_fail (list != NULL);

  walk = split = g_strsplit (list, ",", 0);

  while (walk[0]) {
    gchar **values = g_strsplit ( walk[0], ":", 2);
    if (values[0] && values[1]) {
      gint level = 0;
      g_strstrip (values[0]);
      g_strstrip (values[1]);
      level = strtol (values[1], NULL, 0);
      if (level >= 0 && level < GST_LEVEL_COUNT) {
        GST_DEBUG ("setting debugging to level %d for name \"%s\"",
                       level, values[0]);
        gst_debug_set_threshold_for_name (values[0], level);
      }
    }
    g_strfreev (values);
    walk++;
  }
  g_strfreev (split);
}
static void
load_plugin_func (gpointer data, gpointer user_data)
{
  GstPlugin *plugin;
  const gchar *filename;

  filename = (const gchar *) data;

  plugin = gst_plugin_load_file (filename, NULL);

  if (plugin) {
    GST_INFO ("Loaded plugin: \"%s\"", filename);

    gst_registry_pool_add_plugin (plugin);
  } else {
    GST_WARNING ("Failed to load plugin: \"%s\"", filename);
  }

  g_free (data);
}

static void
split_and_iterate (const gchar *stringlist, gchar *separator, GFunc iterator, gpointer user_data)
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
        j=0;
        break;
      }
    }
  }
}

/* we have no fail cases yet, but maybe in the future */
static gboolean
init_pre (void)
{

  g_type_init ();

  if (g_thread_supported ()) {
    /* somebody already initialized threading */
    _gst_use_threads = TRUE;
  } else {
    if (_gst_use_threads)
      g_thread_init (NULL);
    else
      g_thread_init (&gst_thread_dummy_functions);
  }
  /* we need threading to be enabled right here */
  _gst_debug_init();

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  textdomain (GETTEXT_PACKAGE);
#endif /* ENABLE_NLS */

#ifndef GST_DISABLE_REGISTRY
  {
    const gchar *debug_list;

    debug_list = g_getenv ("GST_DEBUG");
    if (debug_list) {
      parse_debug_list (debug_list);
    }
  }
#endif
#ifndef GST_DISABLE_REGISTRY
  {
    gchar *user_reg;
    const gchar *homedir;

    _global_registry = gst_xml_registry_new ("global_registry", GLOBAL_REGISTRY_FILE);

#ifdef PLUGINS_USE_BUILDDIR
    /* location libgstelements.so */
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/libs/gst");
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/gst/elements");
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/gst/types");
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/gst/autoplug");
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/gst/schedulers");
    gst_registry_add_path (_global_registry, PLUGINS_BUILDDIR "/gst/indexers");
#else
    /* add the main (installed) library path */
    gst_registry_add_path (_global_registry, PLUGINS_DIR);
#endif /* PLUGINS_USE_BUILDDIR */

    if (g_getenv ("GST_REGISTRY"))
    {
      user_reg = g_strdup (g_getenv ("GST_REGISTRY"));
    }
    else
    {
      homedir = g_get_home_dir ();
      user_reg = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
    }
    _user_registry = gst_xml_registry_new ("user_registry", user_reg);

    g_free (user_reg);
  }
#endif /* GST_DISABLE_REGISTRY */

  return TRUE;
}

static gboolean
gst_register_core_elements (GstPlugin *plugin)
{
  /* register some standard builtin types */
  g_assert (gst_element_register (plugin, "bin", GST_RANK_PRIMARY, GST_TYPE_BIN));
  g_assert (gst_element_register (plugin, "pipeline", GST_RANK_PRIMARY, GST_TYPE_PIPELINE));
  g_assert (gst_element_register (plugin, "thread", GST_RANK_PRIMARY, GST_TYPE_THREAD));
  g_assert (gst_element_register (plugin, "queue", GST_RANK_PRIMARY, GST_TYPE_QUEUE));

  return TRUE;
}

static GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gst_core_elements",
  "core elements of the GStreamer library",
  gst_register_core_elements,
  NULL,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN,

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
  const gchar *plugin_path;
#ifndef GST_DISABLE_TRACE
  GstTrace *gst_trace;
#endif /* GST_DISABLE_TRACE */

  llf = G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL;
  g_log_set_handler (g_log_domain_gstreamer, llf, debug_log_handler, NULL);

  GST_INFO ("Initializing GStreamer Core Library version %s %s",
            VERSION, _gst_use_threads ? "" : "(no threads)");

  _gst_format_initialize ();
  _gst_query_type_initialize ();
  gst_object_get_type ();
  gst_pad_get_type ();
  gst_real_pad_get_type ();
  gst_ghost_pad_get_type ();
  gst_element_factory_get_type ();
  gst_element_get_type ();
  gst_scheduler_factory_get_type ();
  gst_type_find_factory_get_type ();
  gst_bin_get_type ();
#ifndef GST_DISABLE_INDEX
  gst_index_factory_get_type ();
#endif /* GST_DISABLE_INDEX */
#ifndef GST_DISABLE_URI
  gst_uri_handler_get_type ();
#endif /* GST_DISABLE_URI */

  plugin_path = g_getenv ("GST_PLUGIN_PATH");
#ifndef GST_DISABLE_REGISTRY
  split_and_iterate (plugin_path, G_SEARCHPATH_SEPARATOR_S, add_path_func, _user_registry);
#endif /* GST_DISABLE_REGISTRY */

  /* register core plugins */
  _gst_plugin_register_static (&plugin_desc);

  _gst_cpu_initialize (_gst_enable_cpu_opt);
  _gst_structure_initialize ();
  _gst_value_initialize ();
  _gst_caps_initialize ();
  _gst_plugin_initialize ();
  _gst_event_initialize ();
  _gst_buffer_initialize ();
  _gst_tag_initialize ();

#ifndef GST_DISABLE_REGISTRY
  if (!_gst_registry_fixed) {
    /* don't override command-line options */
    if (g_getenv ("GST_REGISTRY")) {
      g_object_set (_user_registry, "location", g_getenv ("GST_REGISTRY"), NULL);
      _gst_registry_fixed = TRUE;
    }
  }

  if (!_gst_registry_fixed) {
    gst_registry_pool_add (_global_registry, 100);
    gst_registry_pool_add (_user_registry, 50);
  } else {
    gst_registry_pool_add (_user_registry, 50);
  }

  if (_gst_registry_auto_load) {
    gst_registry_pool_load_all ();
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
  if (_gst_progname == NULL) {
    _gst_progname = g_strdup ("gstprog");
  }

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
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
  GList *list2, *walk2;

  if (!init_post ())
    exit (1);

  walk2 = list2 = gst_registry_pool_plugin_list ();
  while (walk2) {
    GstPlugin *plugin = GST_PLUGIN (walk2->data);
    walk2 = g_list_next (walk2);

    if (!gst_plugin_is_loaded (plugin)) {
#ifndef GST_DISABLE_REGISTRY
      if (GST_IS_REGISTRY (plugin->manager)) {
	GST_CAT_LOG (GST_CAT_PLUGIN_LOADING, "loading plugin %s", plugin->desc.name);
	if (gst_registry_load_plugin (GST_REGISTRY (plugin->manager), plugin) != GST_REGISTRY_OK)
	  GST_CAT_WARNING (GST_CAT_PLUGIN_LOADING, "loading plugin %s failed", plugin->desc.name);
      }
#endif /* GST_DISABLE_REGISTRY */
    }
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
	       color,
               gst_debug_category_get_description (cat));
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

static void
init_popt_callback (poptContext context, enum poptCallbackReason reason,
                    const struct poptOption *option, const char *arg, void *data)
{
  GLogLevelFlags fatal_mask;

  if (gst_initialized)
    return;
  switch (reason) {
  case POPT_CALLBACK_REASON_PRE:
    if (!init_pre ()) _gst_initialization_failure = TRUE;
    break;
  case POPT_CALLBACK_REASON_OPTION:
    switch (option->val) {
    case ARG_VERSION:
      g_print ("GStreamer Core Library version %s\n", GST_VERSION);
      exit (0);
    case ARG_FATAL_WARNINGS:
      fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
      fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
      g_log_set_always_fatal (fatal_mask);
      break;
#ifndef GST_DISABLE_GST_DEBUG
    case ARG_DEBUG_LEVEL: {
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
    case ARG_DISABLE_CPU_OPT:
      _gst_enable_cpu_opt = FALSE;
      break;
    case ARG_PLUGIN_SPEW:
      break;
    case ARG_PLUGIN_PATH:
#ifndef GST_DISABLE_REGISTRY
      split_and_iterate (arg, G_SEARCHPATH_SEPARATOR_S, add_path_func, _user_registry);
#endif /* GST_DISABLE_REGISTRY */
      break;
    case ARG_PLUGIN_LOAD:
      split_and_iterate (arg, ",", prepare_for_load_plugin_func, NULL);
      break;
    case ARG_SEGTRAP_DISABLE:
      _gst_disable_segtrap = TRUE;
      break;
    case ARG_SCHEDULER:
      gst_scheduler_factory_set_default_name (arg);
      break;
    case ARG_REGISTRY:
#ifndef GST_DISABLE_REGISTRY
      g_object_set (G_OBJECT (_user_registry), "location", arg, NULL);
      _gst_registry_fixed = TRUE;
#endif /* GST_DISABLE_REGISTRY */
      break;
    default:
      g_warning ("option %d not recognized", option->val);
      break;
    }
    break;
  case POPT_CALLBACK_REASON_POST:
    if (!init_post ()) _gst_initialization_failure = TRUE;
    gst_initialized = TRUE;
    break;
  }
}

/**
 * gst_use_threads:
 * @use_threads: a #gboolean indicating whether threads should be used
 *
 * Instructs the core to turn on/off threading. When threading
 * is turned off, all thread operations such as mutexes and conditionals
 * are turned into NOPs. use this if you want absolute minimal overhead
 * and you don't use any threads in the pipeline.
 * <note><para>
 * This function may only be called before threads are initialized. This
 * usually happens when calling gst_init.
 * </para></note>
 */
void
gst_use_threads (gboolean use_threads)
{
  g_return_if_fail (!gst_initialized);
  g_return_if_fail (g_thread_supported ());

  _gst_use_threads = use_threads;
}

/**
 * gst_has_threads:
 *
 * Queries if GStreamer has threads enabled.
 *
 * Returns: %TRUE if threads are enabled.
 */
gboolean
gst_has_threads (void)
{
  return _gst_use_threads;
}


static GSList *mainloops = NULL;

/**
 * gst_main:
 *
 * Enters the main GStreamer processing loop.
 */
void
gst_main (void)
{
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  mainloops = g_slist_prepend (mainloops, loop);

  g_main_loop_run (loop);
}

/**
 * gst_main_quit:
 *
 * Exits the main GStreamer processing loop.
 */
void
gst_main_quit (void)
{
  if (!mainloops)
    g_error ("Quit more loops than there are");
  else {
    GMainLoop *loop = mainloops->data;
    mainloops = g_slist_delete_link (mainloops, mainloops);
    g_main_loop_quit (loop);
    g_main_loop_unref (loop);
  }
}

/**
 * gst_version:
 * @major: pointer to a guint to store the major version number
 * @minor: pointer to a guint to store the minor version number
 * @micro: pointer to a guint to store the micro version number
 *
 * Gets the version number of the GStreamer library.
 */
void
gst_version (guint *major, guint *minor, guint *micro)
{
  g_return_if_fail (major);
  g_return_if_fail (minor);
  g_return_if_fail (micro);

  *major = GST_VERSION_MAJOR;
  *minor = GST_VERSION_MINOR;
  *micro = GST_VERSION_MICRO;
}
