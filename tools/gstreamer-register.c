/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstreamer-register.c: Plugin subsystem for loading elements, types, and libs
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
#include <gst/gst.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <string.h>
#include <errno.h>

#include "config.h"

#define GLOBAL_REGISTRY_DIR      GST_CONFIG_DIR
#define GLOBAL_REGISTRY_FILE     GLOBAL_REGISTRY_DIR"/reg.xml"
#define GLOBAL_REGISTRY_FILE_TMP GLOBAL_REGISTRY_DIR"/.reg.xml.tmp"

extern gboolean _gst_plugin_spew;

static void error_perm() {
    g_print("\n(%s)\n"
	    "Do you have the appropriate permissions?\n"
	    "You may need to be root to run this command.\n\n",
	    strerror(errno));
    exit(1);
}

static void usage(const char *progname) {
    g_print("usage: %s\n", progname);
    g_print("Builds the plugin registry for gstreamer.\n");
    g_print("This command will usually require superuser privileges.\n\n");
    exit(0);
}

static int is_file(const char * filename) {
  struct stat statbuf;
  if(stat(filename, &statbuf)) return 0;
  return S_ISREG(statbuf.st_mode);
}

static int is_dir(const char * filename) {
  struct stat statbuf;
  if(stat(filename, &statbuf)) return 0;
  return S_ISDIR(statbuf.st_mode);
}

static void move_file(const char * nameold, const char * namenew) {
    if (!is_file(nameold)) {
	g_print("Temporary `%s' is not a file", nameold);
	error_perm();
    }
    if (is_dir(namenew)) {
	g_print("Destination path `%s' is a directory\n", namenew);
	g_print("Please remove, or reconfigure GStreamer\n\n");
	exit(1);
    }
    if (rename(nameold, namenew)) {
	g_print("Cannot move `%s' to `%s'", nameold, namenew);
	error_perm();
    }
}

static int make_dir(const char * dirname) {
    mode_t mode = 0766;
    return !mkdir(dirname, mode);
}

void check_dir(const char * dirname) {
    if (!is_dir(dirname)) {
	if (!make_dir(dirname)) {
	    g_print("Cannot create GStreamer registry directory `%s'",
		    dirname);
	    error_perm();
	}
    }
}

int main(int argc,char *argv[]) 
{
    xmlDocPtr doc;

    // Init gst
    _gst_plugin_spew = TRUE;
    gst_init(&argc,&argv);

    // Check args
    if (argc != 1) usage(argv[0]);

    // Check that directory for config exists
    check_dir(GLOBAL_REGISTRY_DIR);
    
    // Read the plugins
    doc = xmlNewDoc("1.0");
    doc->root = xmlNewDocNode(doc, NULL, "GST-PluginRegistry", NULL);
    gst_plugin_save_thyself(doc->root);

    // Save the registry to a tmp file.
    if (xmlSaveFile(GLOBAL_REGISTRY_FILE_TMP, doc) <= 0) {
	g_print("Cannot save new registry `%s'",
		GLOBAL_REGISTRY_FILE_TMP);
	error_perm();
    }

    // Make the tmp file live.
    move_file(GLOBAL_REGISTRY_FILE_TMP, GLOBAL_REGISTRY_FILE);

    return(0);
}

