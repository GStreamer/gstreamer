/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst-register.c: Plugin subsystem for loading elements, types, and libs
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

#include <stdlib.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <string.h>
#include <errno.h>

#include "config.h"

extern gboolean _gst_plugin_spew;
extern gboolean _gst_warn_old_registry;
extern gboolean _gst_init_registry_write; /* we ask post_init to be delayed */

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

static void set_filemode(const char * filename, mode_t mode) {
    if(chmod(filename, mode)) {
	g_print("Cannot set file permissions on `%s' to %o", filename, mode);
	error_perm();
    }
}

static int get_filemode(const char * filename, mode_t * mode) {
    struct stat statbuf;
    if(stat(filename, &statbuf)) return 0;
    *mode = statbuf.st_mode & ~ S_IFMT;
    return 1;
}

static void move_file(const char * nameold,
		      const char * namenew,
		      mode_t * newmode) {
    if (!is_file(nameold)) {
	g_print("Temporary `%s' is not a file", nameold);
	error_perm();
    }
    if (is_dir(namenew)) {
	g_print("Destination path `%s' for registry file is a directory\n", namenew);
	g_print("Please remove, or reconfigure GStreamer\n\n");
	exit(1);
    }
    if (rename(nameold, namenew)) {
	g_print("Cannot move `%s' to `%s'", nameold, namenew);
	error_perm();
    }
    /* set mode again, to make this public */
    set_filemode(namenew, *newmode);
}

static void make_dir(const char * dirname) {
    mode_t mode = REGISTRY_DIR_PERMS;
    if(mkdir(dirname, mode)) {
	g_print("Cannot create GStreamer registry directory `%s'", dirname);
	error_perm();
    }

    if(chmod(dirname, mode)) {
	g_print("Cannot set permissions on GStreamer registry directory `%s' to %o", dirname, mode);
	error_perm();
    }

    return;
}

static void check_dir(const char * dirname) {
    if (!is_dir(dirname)) {
	make_dir(dirname);
    }
}

static void save_registry(const char *destfile,
			  xmlDocPtr * doc) {
    mode_t tmpmode = REGISTRY_TMPFILE_PERMS;
#if 0
    FILE *fp;
    int fd;

    fd = open(destfile, O_CREAT | O_TRUNC | O_WRONLY, tmpmode);
    if (fd == -1) {
	g_print("Cannot open `%s' to save new registry to.", destfile);
	error_perm();
    }
    fp = fdopen (fd, "wb");
    if (!fp) {
	g_print("Cannot fopen `%s' to save new registry to.", destfile);
	error_perm();
    }
    /* set mode to make this private */
    set_filemode(destfile, tmpmode);

    /*  FIXME: no way to check success of xmlDocDump, which is why
       this piece of code is ifdefed out.
       The version of libxml currently (Jan 2001) in their CVS tree fixes
       this problem. */

    xmlDocDump(fp, *doc);

    if (!fclose(fp)) {
	g_print("Cannot close `%s' having saved new registry.", destfile);
	error_perm();
    }

#else
#ifdef HAVE_LIBXML2
    /* indent the document */
    if (xmlSaveFormatFile(destfile, *doc, 1) <= 0) {
#else
    if (xmlSaveFile(destfile, *doc) <= 0) {
#endif
	g_print("Cannot save new registry to `%s'", destfile);
	error_perm();
    }
    set_filemode(destfile, tmpmode);
#endif
}

int main(int argc,char *argv[]) 
{
    xmlDocPtr doc;
    xmlNodePtr node;
    GstRegistryWrite *gst_reg;

    /* Mode of the file we're saving the repository to; */
    mode_t newmode;

    /* Get mode of old repository, or a default. */
    if (!get_filemode(GLOBAL_REGISTRY_FILE, &newmode)) {
	mode_t theumask = umask(0);
	umask(theumask);
	newmode = REGISTRY_FILE_PERMS & ~ theumask;
    }

    /* Init gst */
    _gst_plugin_spew = TRUE;
    _gst_warn_old_registry = FALSE;
    gst_info_enable_category(GST_CAT_PLUGIN_LOADING);
    _gst_init_registry_write = TRUE; /* signal that we're writing registry */
    gst_init(&argc,&argv);

    /* remove the old registry file first
     * if a local is returned, then do that, else remove the global one
     * If this fails, we simply ignore it since we'll overwrite it later
     * anyway */
    gst_reg = gst_registry_write_get ();
    unlink (gst_reg->file);

    GST_INFO (GST_CAT_PLUGIN_LOADING, " Writing to registry %s", gst_reg->file);

    /* Check args */
    if (argc != 1) usage(argv[0]);

    /* Read the plugins */
    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "GST-PluginRegistry", NULL);
    xmlDocSetRootElement (doc, node);
    gst_plugin_save_thyself(doc->xmlRootNode);
    
    if (gst_reg->dir)
      check_dir(gst_reg->dir);
    
    /* Save the registry to a tmp file. */
    save_registry(gst_reg->tmp_file, &doc);

    /* Make the tmp file live. */
    move_file(gst_reg->tmp_file, gst_reg->file, &newmode);
#ifdef THOMAS
    }
    else
    {
      gchar *homedir;
      gchar *reg_dir, *reg_file_tmp, *reg_file;

      homedir = (gchar *) g_get_home_dir ();
      reg_dir      = g_strjoin ("/", homedir, LOCAL_REGISTRY_DIR,      NULL);
      reg_file_tmp = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE_TMP, NULL);
      reg_file     = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE,     NULL);

      /* try to make the dir; we'll find out if it fails anyway */
      mkdir(reg_dir, S_IRWXU);
      g_free(reg_dir);

      /* Save the registry to a tmp file. */
      save_registry(reg_file_tmp, &doc);

      /* Make the tmp file live. */
      move_file(reg_file_tmp, reg_file, &newmode);
      g_free(reg_file_tmp);
      g_free(reg_file);
    }
#endif
    g_free (gst_reg);
    return(0);
}

