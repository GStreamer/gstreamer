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

#include "gst_private.h"

#include "gstcpu.h"
#include "gsttype.h"
#include "gstplugin.h"
#include "gstbuffer.h"
#include "gstbin.h"
#include "gstpipeline.h"
#include "gstthread.h"
#include "gstqueue.h"
#include "gstautoplug.h"
#ifndef GST_DISABLE_TYPEFIND
#include "gsttypefind.h"
#endif

#define MAX_PATH_SPLIT	16

gchar *_gst_progname;


extern gint _gst_trace_on;
extern gboolean _gst_plugin_spew;


static gboolean 	gst_init_check 		(int *argc, gchar ***argv);

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
#ifndef GST_DISABLE_TRACE
  GstTrace *gst_trace;
#endif

  if (!g_thread_supported ()) g_thread_init (NULL);

#ifdef USE_GLIB2
  g_type_init();
#else
  {
    gchar *display;
    /* Only initialise gtk fully if we have an X display.
     * FIXME: this fails if the display is specified differently, eg, by
     * a command line parameter. This is okay though, since this is only
     * a quick hack and should be replaced when we move to gobject.*/
    display = g_getenv("DISPLAY");
    if (display == NULL) {
      gtk_type_init ();
    } else {
      gtk_init (argc,argv);
    }
  }
#endif

  if (!gst_init_check (argc,argv)) {
    exit (0);				// FIXME!
  }

  GST_INFO (GST_CAT_GST_INIT, "Initializing GStreamer Core Library");

  gst_elementfactory_get_type ();
  gst_typefactory_get_type ();
#ifndef GST_DISABLE_AUTOPLUG
  gst_autoplugfactory_get_type ();
#endif

  _gst_cpu_initialize ();
  _gst_props_initialize ();
  _gst_caps_initialize ();
  _gst_plugin_initialize ();
  _gst_buffer_initialize ();
  _gst_buffer_pool_initialize ();

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
#endif // GST_DISABLE_TRACE
}

static void
gst_add_paths_func (const gchar *pathlist) 
{
  gchar **paths;
  gint j = 0;
  gchar *lastpath = g_strdup (pathlist);

  while (lastpath) {
    paths = g_strsplit (lastpath, G_SEARCHPATH_SEPARATOR_S, MAX_PATH_SPLIT);
    g_free (lastpath);
    lastpath = NULL;

    while (paths[j]) {
      GST_INFO (GST_CAT_GST_INIT, "Adding plugin path: \"%s\"", paths[j]);
      gst_plugin_add_path (paths[j]);
      if (++j == MAX_PATH_SPLIT) {
        lastpath = g_strdup (paths[j]);
        g_strfreev (paths); 
        j=0;
        break;
      }
    }
  }
}

/* returns FALSE if the program can be aborted */
static gboolean
gst_init_check (int     *argc,
		gchar ***argv)
{
  gboolean ret = TRUE;
  gboolean showhelp = FALSE;

  _gst_progname = NULL;

  if (argc && argv) {
    gint i, j, k;

    _gst_progname = g_strdup(*argv[0]);

    for (i=1; i< *argc; i++) {
      if (!strncmp ("--gst-info-mask=", (*argv)[i], 16)) {
	guint32 val;

        // handle either 0xHEX or dec
        if (*((*argv)[i]+17) == 'x') {
          sscanf ((*argv)[i]+18, "%08x", &val);
        } else {
          sscanf ((*argv)[i]+16, "%d", &val);
        }

	gst_info_set_categories (val);

	(*argv)[i] = NULL;
      }
      else if (!strncmp ("--gst-debug-mask=", (*argv)[i], 17)) {
	guint32 val;

        // handle either 0xHEX or dec
        if (*((*argv)[i]+18) == 'x') {
          sscanf ((*argv)[i]+19, "%08x", &val);
        } else {
          sscanf ((*argv)[i]+17, "%d", &val);
        }

	gst_debug_set_categories (val);

	(*argv)[i] = NULL;
      }
      else if (!strncmp ("--gst-mask=", (*argv)[i], 11)) {
	guint32 val;

        // handle either 0xHEX or dec
        if (*((*argv)[i]+12) == 'x') {
          sscanf ((*argv)[i]+13, "%08x", &val);
        } else {
          sscanf ((*argv)[i]+11, "%d", &val);
        }

	gst_debug_set_categories (val);
	gst_info_set_categories (val);

	(*argv)[i] = NULL;
      }
      else if (!strncmp ("--gst-plugin-spew", (*argv)[i], 17)) {
        _gst_plugin_spew = TRUE;

        (*argv)[i] = NULL;
      }
      else if (!strncmp ("--gst-plugin-path=", (*argv)[i], 17)) {
	gst_add_paths_func ((*argv)[i]+18);

        (*argv)[i] = NULL;
      }
      else if (!strncmp ("--help", (*argv)[i], 6)) {
	showhelp = TRUE;
      }
    }

    for (i = 1; i < *argc; i++) {
      for (k = i; k < *argc; k++)
        if ((*argv)[k] != NULL)
          break;

      if (k > i) {
        k -= i;
        for (j = i + k; j < *argc; j++)
          (*argv)[j-k] = (*argv)[j];
        *argc -= k;
      }
    }
  }

  if (_gst_progname == NULL) {
    _gst_progname = g_strdup("gstprog");
  }


  /* check for ENV variables */
  {
    const gchar *plugin_path = g_getenv("GST_PLUGIN_PATH");
    gst_add_paths_func (plugin_path);
  }

  if (showhelp) {
    guint i;

    g_print ("usage %s [OPTION...]\n", _gst_progname);

    g_print ("\nGStreamer options\n");
    g_print ("  --gst-info-mask=FLAGS               GST info flags to set (current %08x)\n", gst_info_get_categories());
    g_print ("  --gst-debug-mask=FLAGS              GST debugging flags to set\n");
    g_print ("  --gst-mask=FLAGS                    GST info *and* debug flags to set\n");
    g_print ("  --gst-plugin-spew                   Enable printout of errors while loading GST plugins\n");
    g_print ("  --gst-plugin-path=PATH              Add directories separated with '%s' to the plugin search path\n",
		    G_SEARCHPATH_SEPARATOR_S);

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

    ret = FALSE;
  }

  return ret;
}

/**
 * gst_main:
 *
 * Enter the main GStreamer processing loop 
 */
void 
gst_main (void) 
{
#ifndef USE_GLIB2
  gtk_main ();
#endif
}

/**
 * gst_main_quit:
 *
 * Exits the main GStreamer processing loop 
 */
void 
gst_main_quit (void) 
{
#ifndef USE_GLIB2
  gtk_main_quit ();
#endif
}
