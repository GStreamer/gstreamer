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
#endif

#define MAX_PATH_SPLIT	16
#define GST_PLUGIN_SEPARATOR ","

gchar *_gst_progname;


extern gint _gst_trace_on;
extern gboolean _gst_plugin_spew;


static void 		load_plugin_func 	(gpointer data, gpointer user_data);
static void		init_popt_callback	(poptContext context, enum poptCallbackReason reason,
                                                 const struct poptOption *option, const char *arg, void *data);
static void		init_pre		(void);
static void		init_post		(void);


static GSList *preload_plugins = NULL;

const gchar *g_log_domain_gstreamer = "GStreamer";

static void
debug_log_handler (const gchar *log_domain,
		   GLogLevelFlags log_level,
		   const gchar *message,
		   gpointer user_data)
{
  g_log_default_handler(log_domain, log_level, message, user_data);
  g_on_error_query(NULL);
}

enum {
  ARG_INFO_MASK=1,
  ARG_DEBUG_MASK,
  ARG_MASK,
  ARG_MASK_HELP,
  ARG_PLUGIN_SPEW,
  ARG_PLUGIN_PATH,
  ARG_PLUGIN_LOAD,
  ARG_SCHEDULER
};

#ifndef NUL
#define NUL '\0'
#endif

/* FIXME: put in the extended mask help */
static const struct poptOption options[] = {
  {NULL, NUL, POPT_ARG_CALLBACK|POPT_CBFLAG_PRE|POPT_CBFLAG_POST, &init_popt_callback, 0, NULL, NULL},
  {"gst-info-mask",   NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_INFO_MASK,   "info bitmask", "MASK"},
  {"gst-debug-mask",  NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_DEBUG_MASK,  "debugging bitmask", "MASK"},
  {"gst-mask",        NUL, POPT_ARG_INT|POPT_ARGFLAG_STRIP,    NULL, ARG_MASK,        "bitmask for both info and debugging", "MASK"},
  {"gst-mask-help",   NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_MASK_HELP,   "how to set the level of diagnostic output (-mask values)", NULL},
  {"gst-plugin-spew", NUL, POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   NULL, ARG_PLUGIN_SPEW, "enable verbose plugin loading diagnostics", NULL},
  {"gst-plugin-path", NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_PATH, "'" G_SEARCHPATH_SEPARATOR_S "'--separated path list for loading plugins", "PATHS"},
  {"gst-plugin-load", NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_PLUGIN_LOAD, "comma-separated list of plugins to preload in addition to the list stored in env variable GST_PLUGIN_PATH", "PLUGINS"},
  {"gst-scheduler",   NUL, POPT_ARG_STRING|POPT_ARGFLAG_STRIP, NULL, ARG_SCHEDULER,   "scheduler to use ('basic' is the default)", "SCHEDULER"},
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
 * Returns: a pointer to the static GStreamer option table. No free is necessary.
 */
const struct poptOption *
gst_init_get_popt_table (void)
{
  return options;
}

/**
 * gst_init:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 *
 * Initializes the GStreamer library, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
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
 * Initializes the GStreamer library, parsing the options, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 */
void
gst_init_with_popt_table (int *argc, char **argv[], const struct poptOption *popt_options)
{
  poptContext context;
  gint nextopt, i, j, nstrip;
  gchar **temp;
  const struct poptOption *options;
  /* this is probably hacky, no? */
  const struct poptOption options_with[] = {
    {NULL,              NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 		0, "Help options:", NULL},
    {NULL,              NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gst_init_get_popt_table(), 0, "GStreamer options:", NULL},
    {NULL,              NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) popt_options, 		0, "Application options:", NULL},
    POPT_TABLEEND
  };
  const struct poptOption options_without[] = {
    {NULL,              NUL, POPT_ARG_INCLUDE_TABLE, poptHelpOptions, 		0, "Help options:", NULL},
    {NULL,              NUL, POPT_ARG_INCLUDE_TABLE, (struct poptOption *) gst_init_get_popt_table(), 0, "GStreamer options:", NULL},
    POPT_TABLEEND
  };

  if (!argc || !argv) {
    if (argc || argv)
      g_warning ("gst_init: Only one of arg or argv was NULL");
    
    init_pre();
    init_post();
    return;
  }

  if (popt_options == NULL) {
    options = options_without;
  } else {
    options = options_with;
  }
  context = poptGetContext ("GStreamer", *argc, (const char**)*argv, options, 0);
  
  while ((nextopt = poptGetNextOpt (context)) > 0); /* do nothing, it's all callbacks */
  
  if (nextopt != -1) {
    g_print ("Error on option %s: %s.\nRun '%s --help' to see a full list of available command line options.\n",
             poptBadOption (context, 0),
             poptStrerror (nextopt),
             (*argv)[0]);
    exit (1);
  }

  /* let's do this once there are 1.6.3 popt debs out
     *argc = poptStrippedArgv (context, *argc, *argv); */
  
  /* until then we'll do a very basic arg permutation
     this will break gst-launch -o */
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
}

static void 
add_path_func (gpointer data, gpointer user_data)
{
  GST_INFO (GST_CAT_GST_INIT, "Adding plugin path: \"%s\"", (gchar *)data);
  gst_plugin_add_path ((gchar *)data);
}

static void 
prepare_for_load_plugin_func (gpointer data, gpointer user_data)
{
  preload_plugins = g_slist_prepend (preload_plugins, data);
}

static void 
load_plugin_func (gpointer data, gpointer user_data)
{
  gboolean ret;
  ret = gst_plugin_load ((gchar *)data);
  if (ret)
    GST_INFO (GST_CAT_GST_INIT, "Loaded plugin: \"%s\"", (gchar *)data);
  else
    GST_INFO (GST_CAT_GST_INIT, "Failed to load plugin: \"%s\"", (gchar *)data);

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
split_and_iterate (const gchar *stringlist, gchar *separator, GFunc iterator) 
{
  gchar **strings;
  gint j = 0;
  gchar *lastlist = g_strdup (stringlist);

  while (lastlist) {
    strings = g_strsplit (lastlist, separator, MAX_PATH_SPLIT);
    g_free (lastlist);
    lastlist = NULL;

    while (strings[j]) {
      iterator (strings[j], NULL);
      if (++j == MAX_PATH_SPLIT) {
        lastlist = g_strdup (strings[j]);
        g_strfreev (strings); 
        j=0;
        break;
      }
    }
  }
}

static void
init_pre (void)
{
  if (!g_thread_supported ())
    g_thread_init (NULL);
  
  g_type_init();
}

static void
init_post (void)
{
  GLogLevelFlags llf;
  const gchar *plugin_path;
#ifndef GST_DISABLE_TRACE
  GstTrace *gst_trace;
#endif
  
  llf = G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL;
  g_log_set_handler(g_log_domain_gstreamer, llf, debug_log_handler, NULL);
  
  GST_INFO (GST_CAT_GST_INIT, "Initializing GStreamer Core Library");
  
  gst_object_get_type ();
  gst_pad_get_type ();
  gst_real_pad_get_type ();
  gst_ghost_pad_get_type ();
  gst_elementfactory_get_type ();
  gst_element_get_type ();
  gst_typefactory_get_type ();
  gst_schedulerfactory_get_type ();
  gst_bin_get_type ();
#ifndef GST_DISABLE_AUTOPLUG
  gst_autoplugfactory_get_type ();
#endif

  plugin_path = g_getenv("GST_PLUGIN_PATH");
  split_and_iterate (plugin_path, G_SEARCHPATH_SEPARATOR_S, add_path_func);
 
  _gst_cpu_initialize ();
  _gst_props_initialize ();
  _gst_caps_initialize ();
  _gst_plugin_initialize ();
  _gst_event_initialize ();
  _gst_buffer_initialize ();
  _gst_buffer_pool_initialize ();

  /* if we need to preload plugins */
  if (preload_plugins) {
    g_slist_foreach (preload_plugins, load_plugin_func, NULL);
    g_slist_free (preload_plugins);
    preload_plugins = NULL;
  }

  /* register some standard builtin types */
  gst_elementfactory_new ("bin", gst_bin_get_type (), &gst_bin_details);
  gst_elementfactory_new ("pipeline", gst_pipeline_get_type (), &gst_pipeline_details);
  gst_elementfactory_new ("thread", gst_thread_get_type (), &gst_thread_details);
  gst_elementfactory_new ("queue", gst_queue_get_type (), &gst_queue_details);
#ifndef GST_DISABLE_TYPEFIND
  gst_elementfactory_new ("typefind", gst_typefind_get_type (), &gst_typefind_details);
#endif

#ifndef GST_DISABLE_TRACE
  _gst_trace_on = 0;
  if (_gst_trace_on) {
    gst_trace = gst_trace_new ("gst.trace",1024);
    gst_trace_set_default (gst_trace);
  }
#endif /* GST_DISABLE_TRACE */
  if (_gst_progname == NULL) {
    _gst_progname = g_strdup("gstprog");
  }
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

  switch (reason) {
  case POPT_CALLBACK_REASON_PRE:
    init_pre();
    break;
  case POPT_CALLBACK_REASON_OPTION:
    switch (option->val) {
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
      _gst_plugin_spew = TRUE;
      break;
    case ARG_PLUGIN_PATH:
      split_and_iterate (arg, G_SEARCHPATH_SEPARATOR_S, add_path_func);
      break;
    case ARG_PLUGIN_LOAD:
      split_and_iterate (arg, ",", prepare_for_load_plugin_func);
      break;
    case ARG_SCHEDULER:
      gst_schedulerfactory_set_default_name (arg);
      break;
    default:
      g_warning ("option %d not recognized", option->val);
      break;
    }
    break;
  case POPT_CALLBACK_REASON_POST:
    init_post();
    break;
  }
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
    g_warning ("Quit more loops than there are");
  else {
    GMainLoop *loop = mainloops->data;
    mainloops = g_slist_delete_link (mainloops, mainloops);
    g_main_loop_quit (loop);
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

