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
#include "gsttypefind.h"



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
  GstTrace *gst_trace;

  GST_INFO (GST_CAT_GST_INIT, "Initializing GStreamer Core Library");

  if (!g_thread_supported ()) g_thread_init (NULL);

  _gst_progname = g_strdup(*argv[0]);

  gtk_init (argc,argv);

  if (!gst_init_check (argc,argv)) {
    exit (0);
  }

  _gst_cpu_initialize ();
  _gst_type_initialize ();
  _gst_plugin_initialize ();
  _gst_buffer_initialize ();

  /* register some standard builtin types */
  gst_elementfactory_new ("bin", gst_bin_get_type (), &gst_bin_details);
  gst_elementfactory_new ("pipeline", gst_pipeline_get_type (), &gst_pipeline_details);
  gst_elementfactory_new ("thread", gst_thread_get_type (), &gst_thread_details);
  gst_elementfactory_new ("queue", gst_queue_get_type (), &gst_queue_details);
  gst_elementfactory_new ("typefind", gst_typefind_get_type (), &gst_typefind_details);

  _gst_trace_on = 0;
  if (_gst_trace_on) {
    gst_trace = gst_trace_new ("gst.trace",1024);
    gst_trace_set_default (gst_trace);
  }
}

/* returns FALSE if the program can be aborted */
static gboolean
gst_init_check (int     *argc,
		gchar ***argv)
{
  gboolean ret = TRUE;
  gboolean showhelp = FALSE;

  if (argc && argv) {
    gint i, j, k;

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
      else if (!strncmp ("--gst-plugin-spew", (*argv)[i], 17)) {
        _gst_plugin_spew = TRUE;

        (*argv)[i] = NULL;
      }
      else if (!strncmp ("--gst-plugin-path=", (*argv)[i], 17)) {
        gst_plugin_add_path ((*argv)[i]+18);

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

  if (showhelp) {
    guint i;

    g_print ("usage %s [OPTION...]\n", (*argv)[0]);

    g_print ("\nGStreamer options\n");
    g_print ("  --gst-info-mask=FLAGS               GST info flags to set (current %08x)\n", gst_info_get_categories());
    g_print ("  --gst-debug-mask=FLAGS              GST debugging flags to set\n");
    g_print ("  --gst-plugin-spew                   Enable printout of errors while loading GST plugins\n");
    g_print ("  --gst-plugin-path=PATH              Add a directory to the plugin search path\n");

    g_print ("\n  Mask (to be OR'ed)   info/debug         FLAGS   \n");
    g_print ("--------------------------------------------------------\n");

    for (i = 0; i<GST_CAT_MAX_CATEGORY; i++) {
      g_print ("   0x%08x     %s%s     %s\n", 1<<i, 
                  (gst_info_get_categories() & (1<<i)?"(enabled)":"         "),
                  (gst_debug_get_categories() & (1<<i)?"/(enabled)":"/         "),
		   gst_get_category_name (i));
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
  gtk_main ();
}

/**
 * gst_main_quit:
 *
 * Exits the main GStreamer processing loop 
 */
void 
gst_main_quit (void) 
{
  gtk_main_quit ();
}
