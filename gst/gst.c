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

#include "gst.h"
#include "gstqueue.h"
#ifndef GST_DISABLE_TYPEFIND
#include "gsttypefind.h"
#endif /* GST_DISABLE_TYPEFIND */
#ifndef GST_DISABLE_REGISTRY
#include "registries/gstxmlregistry.h"
#endif /* GST_DISABLE_REGISTRY */
#include "gstregistrypool.h"

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

static gboolean gst_initialized = FALSE;
/* this will be set in popt callbacks when a problem has been encountered */
static gboolean _gst_initialization_failure = FALSE;
extern gint _gst_trace_on;

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
  ARG_INFO_MASK,
  ARG_DEBUG_MASK,
  ARG_MASK,
  ARG_MASK_HELP,
  ARG_PLUGIN_SPEW,
  ARG_PLUGIN_PATH,
  ARG_PLUGIN_LOAD,
  ARG_SCHEDULER,
  ARG_NOTHREADS,
  ARG_REGISTRY
};

#ifndef NUL
#define NUL '\0'
#endif

/* default scheduler, can be changed in gstscheduler.h with
 * the GST_SCHEDULER_DEFAULT_NAME define.
 */
static const struct poptOption gstreamer_options[] = {
  {NULL, NUL, POPT_ARG_CALLBACK|POPT_CBFLAG_PRE|POPT_CBFLAG_POST, &init_popt_callback, 0, NULL, NULL},
  {"gst-version",        NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_VERSION,        "Print the GStreamer version", NULL},
  {"gst-fatal-warnings", NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_FATAL_WARNINGS, "Make all warnings fatal", NULL},
  {"gst-info-mask",      NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_INFO_MASK,      "info bitmask", "MASK"},
  {"gst-debug-mask",     NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_DEBUG_MASK,     "debugging bitmask", "MASK"},
  {"gst-mask",           NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_MASK,           "bitmask for both info and debugging", "MASK"},
  {"gst-mask-help",      NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_MASK_HELP,      "how to set the level of diagnostic output (-mask values)", NULL},
  {"gst-plugin-spew",    NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_PLUGIN_SPEW,    "enable verbose plugin loading diagnostics", NULL},
  {"gst-plugin-path",    NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_PATH,    "'" G_SEARCHPATH_SEPARATOR_S "'--separated path list for loading plugins", "PATHS"},
  {"gst-plugin-load",    NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_LOAD,    "comma-separated list of plugins to preload in addition to the list stored in env variable GST_PLUGIN_PATH", "PLUGINS"},
  {"gst-scheduler",      NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_SCHEDULER,      "scheduler to use ('"GST_SCHEDULER_DEFAULT_NAME"' is the default)", "SCHEDULER"},
  {"gst-nothreads",      NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_NOTHREADS,      "use NOPs for all threading and locking operations", NULL},
  {"gst-registry",       NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_REGISTRY,       "registry to use" , "REGISTRY"},
  POPT_TABLEEND
};

/**
 * gst_init_get_popt_table:
 *
 * Returns a popt option table with GStreamer's argument specifications. The
 * table is set up to use popt's callback method, so whenever the parsing is
 * actually performed (via a poptGetContext()), the GStreamer libraries will
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
 * Returns: TRUE if GStreamer coul be initialized
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
 * Returns: TRUE if GStreamer coul be initialized
 */
gboolean
gst_init_check_with_popt_table (int *argc, char **argv[],
		                const struct poptOption *popt_options)
{
  poptContext context;
  gint nextopt, i, j, nstrip;
  gchar **temp;
  const struct poptOption *options;
  const struct poptOption options_with[] = {
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 				 0, "Help options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gstreamer_options,	 0, "GStreamer options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) popt_options, 		 0, "Application options:", NULL},
    POPT_TABLEEND
  };
  const struct poptOption options_without[] = {
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 				 0, "Help options:", NULL},
    {NULL, NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gstreamer_options,	 0, "GStreamer options:", NULL},
    POPT_TABLEEND
  };

  if (gst_initialized)
  {
    GST_DEBUG (GST_CAT_GST_INIT, "already initialized gst\n");
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
  poptFreeContext (context);

  /* let's do this once there are 1.6.3 popt debs out
     *argc = poptStrippedArgv (context, *argc, *argv); */
  
  /* until then we'll do a very basic arg permutation */
  temp = *argv + 1;
  i = 1;
  nstrip = 0;
  g_assert (*argc > 0);
  while (i++ < *argc && *temp[0]=='-') {
    for (j = 1; j < *argc - 1; j++)
      (*argv)[j] = (*argv)[j+1];
    (*argv)[*argc-1] = *temp;
    nstrip++;
  }
  *argc -= nstrip;
  return TRUE;
}

#ifndef GST_DISABLE_REGISTRY
static void 
add_path_func (gpointer data, gpointer user_data)
{
  GstRegistry *registry = GST_REGISTRY (user_data);
  
  GST_INFO (GST_CAT_GST_INIT, "Adding plugin path: \"%s\"", (gchar *)data);
  gst_registry_add_path (registry, (gchar *)data);
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
  gboolean ret;
  GstPlugin *plugin;
  const gchar *filename;

  filename = (const gchar *) data;

  plugin = gst_plugin_new (filename);
  ret = gst_plugin_load_plugin (plugin, NULL);

  if (ret) {
    GST_INFO (GST_CAT_GST_INIT, "Loaded plugin: \"%s\"", filename);

    gst_registry_pool_add_plugin (plugin);
  }
  else
    GST_INFO (GST_CAT_GST_INIT, "Failed to load plugin: \"%s\"", filename);

  g_free (data);
}

static void 
parse_number (const gchar *number, guint32 *val)
{
  /* handle either 0xHEX or dec */
  if (*(number+1) == 'x') {
    sscanf (number+2, "%08x", val);
  } else {
    sscanf (number, "%d", val);
  }
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

    homedir = g_get_home_dir ();
    user_reg = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
    _user_registry = gst_xml_registry_new ("user_registry", user_reg);

    /* this test is a hack; gst-register sets this to false
     * so this is a test for the current instance being gst-register */
    if (_gst_registry_auto_load == TRUE) {
      /* do a sanity check here; either one of the two registries should exist */
      if (!g_file_test (user_reg, G_FILE_TEST_IS_REGULAR)) {
        if (!g_file_test (GLOBAL_REGISTRY_FILE, G_FILE_TEST_IS_REGULAR))
        {
          g_print ("Couldn't find user registry %s or global registry %s\n",
	           user_reg, GLOBAL_REGISTRY_FILE);
          g_error ("Please run gst-register either as root or user");
        }
      }
    }
    g_free (user_reg);
  }
#endif /* GST_DISABLE_REGISTRY */

  return TRUE;
}

static gboolean
gst_register_core_elements (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* register some standard builtin types */
  factory = gst_element_factory_new ("bin", gst_bin_get_type (), &gst_bin_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  factory = gst_element_factory_new ("pipeline", gst_pipeline_get_type (), &gst_pipeline_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  factory = gst_element_factory_new ("thread", gst_thread_get_type (), &gst_thread_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  factory = gst_element_factory_new ("queue", gst_queue_get_type (), &gst_queue_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
#ifndef GST_DISABLE_TYPEFIND
  factory = gst_element_factory_new ("typefind", gst_type_find_get_type (), &gst_type_find_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
#endif /* GST_DISABLE_TYPEFIND */

  return TRUE;
}

static GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,           
  GST_VERSION_MINOR,  
  "gst_core_plugins",
  gst_register_core_elements
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

  if (!g_thread_supported ()) {
    if (_gst_use_threads)
      g_thread_init (NULL);
    else
      g_thread_init (&gst_thread_dummy_functions);
  }
  
  llf = G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL;
  g_log_set_handler (g_log_domain_gstreamer, llf, debug_log_handler, NULL);
  
  GST_INFO (GST_CAT_GST_INIT, 
            "Initializing GStreamer Core Library version %s %s",
            GST_VERSION, _gst_use_threads?"":"(no threads)");
  
  _gst_format_initialize ();
  _gst_query_type_initialize ();
  gst_object_get_type ();
  gst_pad_get_type ();
  gst_real_pad_get_type ();
  gst_ghost_pad_get_type ();
  gst_element_factory_get_type ();
  gst_element_get_type ();
  gst_type_factory_get_type ();
  gst_scheduler_factory_get_type ();
  gst_bin_get_type ();
#ifndef GST_DISABLE_AUTOPLUG
  gst_autoplug_factory_get_type ();
#endif /* GST_DISABLE_AUTOPLUG */
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
 
  _gst_cpu_initialize ();
  _gst_props_initialize ();
  _gst_caps_initialize ();
  _gst_plugin_initialize ();
  _gst_event_initialize ();
  _gst_buffer_initialize ();

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

static void
gst_mask_help (void) 
{
  guint i;
  
  g_print ("\n  Mask (to be OR'ed)   info/debug         FLAGS   \n");
  g_print ("--------------------------------------------------------\n");
  
  for (i = 0; i<GST_CAT_MAX_CATEGORY; i++) {
    if (gst_get_category_name(i)) {

#if GST_DEBUG_COLOR
      g_print ("   0x%08x     %s%s     \033[%sm%s\033[00m\n", 1<<i, 
               (gst_info_get_categories() & (1<<i)?"(enabled)":"         "),
               (gst_debug_get_categories() & (1<<i)?"/(enabled)":"/         "),
               _gst_category_colors[i], gst_get_category_name (i));
#else
      g_print ("   0x%08x     %s%s     %s\n", 1<<i, 
               (gst_info_get_categories() & (1<<i)?"(enabled)":"         "),
               (gst_debug_get_categories() & (1<<i)?"/(enabled)":"/         "),
               gst_get_category_name (i));
#endif
    }
  }
}
  

static void
init_popt_callback (poptContext context, enum poptCallbackReason reason,
                    const struct poptOption *option, const char *arg, void *data) 
{
  gint val = 0;
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
    case ARG_INFO_MASK:
      parse_number (arg, &val);
      gst_info_set_categories (val);
      break;
    case ARG_DEBUG_MASK:
      parse_number (arg, &val);
      gst_debug_set_categories (val);
      break;
    case ARG_MASK:
      parse_number (arg, &val);
      gst_debug_set_categories (val);
      gst_info_set_categories (val);
      break;
    case ARG_MASK_HELP:
      gst_mask_help ();
      exit (0);
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
    case ARG_SCHEDULER:
      gst_scheduler_factory_set_default_name (arg);
      break;
    case ARG_NOTHREADS:
      gst_use_threads (FALSE);
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
 * @use_threads: flag indicating threads should be used
 *
 * Instructs the core to turn on/off threading. When threading
 * is turned off, all thread operations such as mutexes and conditionals
 * are turned into NOPs. use this if you want absolute minimal overhead
 * and you don't use any threads in the pipeline.
 */
void
gst_use_threads (gboolean use_threads)
{
  _gst_use_threads = use_threads;
}

/**
 * gst_has_threads:
 * 
 * Query if GStreamer has threads enabled.
 *
 * Returns: TRUE if threads are enabled.
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
 * Enter the main GStreamer processing loop 
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
 * Exits the main GStreamer processing loop 
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
 * Gets the version number of the GStreamer library
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

