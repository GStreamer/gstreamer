/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstcache.h: Header for GstCache
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

#ifndef __GST_CACHE_H__
#define __GST_CACHE_H__

#include <gst/gstobject.h>
#include <gst/gstformat.h>
#include <gst/gstcaps.h>

G_BEGIN_DECLS

#define GST_TYPE_CACHE		(gst_cache_get_type ())
#define GST_CACHE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CACHE, GstCache))
#define GST_CACHE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_CACHE, GstCacheClass))
#define GST_IS_CACHE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CACHE))
#define GST_IS_CACHE_CLASS(obj)	(GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CACHE))

typedef struct _GstCacheEntry GstCacheEntry;
typedef struct _GstCacheGroup GstCacheGroup;
typedef struct _GstCache GstCache;
typedef struct _GstCacheClass GstCacheClass;

typedef enum {
  GST_CACHE_UNKNOWN,
  GST_CACHE_CERTAIN,
  GST_CACHE_FUZZY
} GstCacheCertainty;

typedef enum {
  GST_CACHE_ENTRY_ID,
  GST_CACHE_ENTRY_ASSOCIATION,
  GST_CACHE_ENTRY_OBJECT,
  GST_CACHE_ENTRY_FORMAT,
} GstCacheEntryType;

#define GST_CACHE_NASSOCS(entry)		((entry)->data.assoc.nassocs)
#define GST_CACHE_ASSOC_FLAGS(entry)	((entry)->data.assoc.flags)
#define GST_CACHE_ASSOC_FORMAT(entry,i)	((entry)->data.assoc.assocs[(i)].format)
#define GST_CACHE_ASSOC_VALUE(entry,i)	((entry)->data.assoc.assocs[(i)].value)

typedef struct _GstCacheAssociation GstCacheAssociation;

struct _GstCacheAssociation {
  GstFormat 	format;
  gint64 	value;
};

typedef enum {
  GST_ACCOCIATION_FLAG_NONE 	= 0,
  GST_ACCOCIATION_FLAG_KEY_UNIT = (1 << 0),
} GstAssocFlags;

#define GST_CACHE_FORMAT_FORMAT(entry)	((entry)->data.format.format)
#define GST_CACHE_FORMAT_KEY(entry)	((entry)->data.format.key)

#define GST_CACHE_ID_DESCRIPTION(entry)	((entry)->data.id.description)

struct _GstCacheEntry {
  GstCacheEntryType	 type;
  gint			 id;

  union {
    struct {
      gchar		*description;
    } id;
    struct {
      gint		 nassocs;
      GstCacheAssociation 
	      		*assocs;
      GstAssocFlags	 flags;
    } assoc;
    struct {
      gchar		*key;
      GType		 type;
      gpointer		 object;
    } object;
    struct {
      GstFormat		 format;
      gchar		*key;
    } format;
  } data;
};

struct _GstCacheGroup {
  /* unique ID of group in cache */
  gint groupnum;

  /* list of entries */
  GList *entries;

  /* the certainty level of the group */
  GstCacheCertainty certainty;

  /* peer group that contains more certain entries */
  gint peergroup;
};

typedef gboolean 	(*GstCacheWriterResolver) 	(GstCache *cache, 
						   	 GstObject *writer, 
						   	 gint *writer_id,
						   	 gchar **writer_string,
						   	 gpointer user_data);
struct _GstCache {
  GstObject		 object;

  GList			*groups;
  GstCacheGroup		*curgroup;
  gint			 maxgroup;

  GstCacheWriterResolver resolver;
  gpointer		 user_data;

  GHashTable		*writers;
  gint			 last_id;
};

struct _GstCacheClass {
  GstObjectClass parent_class;

  gboolean	(*resolve_writer)	(GstCache *cache, GstObject *writer, 
		  			 gint *writer_id, gchar **writer_string);

  /* abstract methods */
  void		(*add_entry)		(GstCache *cache, GstCacheEntry *entry);
  void		(*remove_entry)		(GstCache *cache, GstCacheEntry *entry);
  void		(*modify_entry)		(GstCache *cache, GstCacheEntry *oldentry, 
		        	 	 GstCacheEntry *new_entry);

  GstCacheEntry* (*get_entry)		(GstCache *cache); 

  /* signals */
  void		(*entry_added)		(GstCache *cache, GstCacheEntry *entry);
  void		(*entry_removed)	(GstCache *cache, GstCacheEntry *entry);
  void		(*entry_modified)	(GstCache *cache, GstCacheEntry *oldentry, 
		        		 GstCacheEntry *new_entry);
};

GType			gst_cache_get_type		(void);
GstCache*		gst_cache_new			(void);

gint			gst_cache_get_group		(GstCache *cache);
gint			gst_cache_new_group		(GstCache *cache);
gboolean		gst_cache_set_group		(GstCache *cache, gint groupnum);

void			gst_cache_set_certainty		(GstCache *cache, 
							 GstCacheCertainty certainty);
GstCacheCertainty	gst_cache_get_certainty		(GstCache *cache);

gboolean 		gst_cache_get_writer_id 	(GstCache *cache, GstObject *writer, gint *id);

GstCacheEntry*		gst_cache_add_format		(GstCache *cache, gint id, GstFormat format); 
GstCacheEntry*		gst_cache_add_association	(GstCache *cache, gint id, GstAssocFlags flags,
							 GstFormat format, gint64 value, ...);
GstCacheEntry*		gst_cache_add_object		(GstCache *cache, gint id, gchar *key,
							 GType type, gpointer object);
GstCacheEntry*		gst_cache_add_id		(GstCache *cache, gint id,
							 gchar *description); 


G_END_DECLS

#endif /* __GST_CACHE_H__ */
