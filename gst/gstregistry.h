/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstregistry.h: Header for registry handling
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


#ifndef __GST_REGISTRY_H__
#define __GST_REGISTRY_H__

#define GLOBAL_REGISTRY_DIR      GST_CONFIG_DIR
#define GLOBAL_REGISTRY_FILE     GLOBAL_REGISTRY_DIR"/registry.xml"
#define GLOBAL_REGISTRY_FILE_TMP GLOBAL_REGISTRY_DIR"/.registry.xml.tmp"

#define LOCAL_REGISTRY_DIR       ".gstreamer"
#define LOCAL_REGISTRY_FILE      LOCAL_REGISTRY_DIR"/registry.xml"
#define LOCAL_REGISTRY_FILE_TMP  LOCAL_REGISTRY_DIR"/.registry.xml.tmp"

#define REGISTRY_DIR_PERMS (S_ISGID | \
                            S_IRUSR | S_IWUSR | S_IXUSR | \
		            S_IRGRP | S_IXGRP | \
			    S_IROTH | S_IXOTH)
#define REGISTRY_TMPFILE_PERMS (S_IRUSR | S_IWUSR)
#define REGISTRY_FILE_PERMS (S_IRUSR | S_IWUSR | \
                             S_IRGRP | S_IWGRP | \
			     S_IROTH | S_IWOTH)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstRegistryWrite GstRegistryWrite;
struct _GstRegistryWrite {
  gchar *dir;
  gchar *file;
  gchar *tmp_file;
};

typedef struct _GstRegistryRead GstRegistryRead;
struct _GstRegistryRead {
  gchar *global_reg;
  gchar *local_reg;
};

GstRegistryWrite 	*gst_registry_write_get  	(void);
GstRegistryRead 	*gst_registry_read_get  	(void);
void 			gst_registry_option_set 	(const gchar *registry);
  
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_REGISTRY_H__ */
