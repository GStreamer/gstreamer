/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>

extern gint _gst_trace_on;

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

  if (!g_thread_supported ()) g_thread_init (NULL);

  gtk_init (argc,argv);

  _gst_cpu_initialize ();
  _gst_type_initialize ();
  _gst_plugin_initialize ();
  _gst_buffer_initialize ();

  /* register some standard builtin types */
  gst_elementfactory_register (gst_elementfactory_new ("bin",
			  gst_bin_get_type (), &gst_bin_details));
  gst_elementfactory_register (gst_elementfactory_new ("pipeline",
			  gst_pipeline_get_type (), &gst_pipeline_details));
  gst_elementfactory_register (gst_elementfactory_new("thread",
			  gst_thread_get_type (), &gst_thread_details));

  //gst_plugin_load_elementfactory("gsttypes");
  //gst_plugin_load("libgstelements.so");

  _gst_trace_on = 0;
  if (_gst_trace_on) {
    gst_trace = gst_trace_new ("gst.trace",1024);
    gst_trace_set_default (gst_trace);
  }
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
