/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstregistry.c: handle registry
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

#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "gstinfo.h"
#include "gstregistry.h"

static gchar *gst_registry_option = NULL;

/* save the registry specified as an option */
void
gst_registry_option_set (const gchar *registry)
{
  gst_registry_option = g_strdup (registry);
  return;
}

/* decide if we're going to use the global registry or not 
 * - if root, use global
 * - if not root :
 *   - if user can write to global, use global
 *   - else use local
 */
gboolean
gst_registry_use_global (void)
{
  /* struct stat reg_stat; */
  FILE *reg;
  
  if (getuid () == 0) return TRUE; 	/* root always uses global */

  /* check if we can write to the global registry somehow */
  reg = fopen (GLOBAL_REGISTRY_FILE, "a");
  if (reg == NULL) { return FALSE; }
  else
  {
    /* we can write to it, do so for kicks */
    fclose (reg);
  }
  
  /* we can write to it, so now see if we can write in the dir as well */ 
  if (access (GLOBAL_REGISTRY_DIR, W_OK) == 0) return TRUE;

  return FALSE;
}

/* get the data that tells us where we can write the registry
 * Allocate, fill in the GstRegistryWrite struct according to 
 * current situation, and return it */
GstRegistryWrite *
gst_registry_write_get ()
{
  GstRegistryWrite *gst_reg = g_malloc (sizeof (GstRegistryWrite));
  
  /* if a registry is specified on command line, use that one */
  if (gst_registry_option)
  {
    /* FIXME: maybe parse the dir from file ? */
    gst_reg->dir = NULL;
    gst_reg->file = gst_registry_option;
    /* we cannot use the temp dir since the move needs to be on same device */
    gst_reg->tmp_file = g_strdup_printf ("%s.tmp", gst_registry_option);
  }
  else
  {
    if (gst_registry_use_global ())
    {
      gst_reg->dir      = g_strdup (GLOBAL_REGISTRY_DIR);
      gst_reg->file     = g_strdup (GLOBAL_REGISTRY_FILE);
      gst_reg->tmp_file = g_strdup (GLOBAL_REGISTRY_FILE_TMP);
    }
    else
    {
      gchar *homedir = (gchar *) g_get_home_dir ();
      
      gst_reg->dir = g_strjoin ("/", homedir, LOCAL_REGISTRY_DIR, NULL);
      gst_reg->file = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
      gst_reg->tmp_file = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE_TMP, NULL);
    }
  } 
  return gst_reg;
}

/* fill in the GstRegistryRead struct according to current situation */
GstRegistryRead *
gst_registry_read_get ()
{
  GstRegistryRead *gst_reg = g_malloc (sizeof (GstRegistryRead));
  
  /* if a registry is specified on command line, use that one */
  if (gst_registry_option)
  {
    /* FIXME: maybe parse the dir from file ? */
    gst_reg->local_reg = NULL;
    gst_reg->global_reg = gst_registry_option;
  }
  else
  {
    gchar *homedir = (gchar *) g_get_home_dir ();
    gst_reg->local_reg = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
    if (g_file_test (gst_reg->local_reg, G_FILE_TEST_EXISTS) == FALSE)
    {
      /* it does not exist, so don't read from it */
      g_free (gst_reg->local_reg);
    }
    gst_reg->global_reg = g_strdup (GLOBAL_REGISTRY_FILE);
  }
  return gst_reg;
}
