/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstindex.h: Header for GstIndex, base class to handle efficient
 *             storage or caching of seeking information.
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

#ifndef __GST_INDEX_H__
#define __GST_INDEX_H__

#include <gst/gstobject.h>
#include <gst/gstformat.h>
#include <gst/gstpluginfeature.h>

G_BEGIN_DECLS

#define GST_TYPE_INDEX			(gst_index_get_type ())
#define GST_INDEX(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_INDEX, GstIndex))
#define GST_IS_INDEX(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_INDEX))
#define GST_INDEX_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_INDEX, GstIndexClass))
#define GST_IS_INDEX_CLASS(klass)	(GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_INDEX))
#define GST_INDEX_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_INDEX, GstIndexClass))

typedef struct _GstIndexEntry GstIndexEntry;
typedef struct _GstIndexGroup GstIndexGroup;
typedef struct _GstIndex GstIndex;
typedef struct _GstIndexClass GstIndexClass;

typedef enum {
  GST_INDEX_UNKNOWN,
  GST_INDEX_CERTAIN,
  GST_INDEX_FUZZY
} GstIndexCertainty;

typedef enum {
  GST_INDEX_ENTRY_ID,
  GST_INDEX_ENTRY_ASSOCIATION,
  GST_INDEX_ENTRY_OBJECT,
  GST_INDEX_ENTRY_FORMAT
} GstIndexEntryType;

typedef enum {
  GST_INDEX_LOOKUP_EXACT,
  GST_INDEX_LOOKUP_BEFORE,
  GST_INDEX_LOOKUP_AFTER
} GstIndexLookupMethod;

#define GST_INDEX_NASSOCS(entry)		((entry)->data.assoc.nassocs)
#define GST_INDEX_ASSOC_FLAGS(entry)		((entry)->data.assoc.flags)
#define GST_INDEX_ASSOC_FORMAT(entry,i)		((entry)->data.assoc.assocs[(i)].format)
#define GST_INDEX_ASSOC_VALUE(entry,i)		((entry)->data.assoc.assocs[(i)].value)

typedef struct _GstIndexAssociation GstIndexAssociation;

struct _GstIndexAssociation {
  GstFormat 	format;
  gint64 	value;
};

typedef enum {
  GST_ASSOCIATION_FLAG_NONE 	= 0,
  GST_ASSOCIATION_FLAG_KEY_UNIT = (1 << 0),

  /* new flags should start here */
  GST_ASSOCIATION_FLAG_LAST	= (1 << 8)
} GstAssocFlags;

#define GST_INDEX_FORMAT_FORMAT(entry)		((entry)->data.format.format)
#define GST_INDEX_FORMAT_KEY(entry)		((entry)->data.format.key)

#define GST_INDEX_ID_INVALID			(-1)

#define GST_INDEX_ID_DESCRIPTION(entry)		((entry)->data.id.description)

struct _GstIndexEntry {
  GstIndexEntryType	 type;
  gint			 id;

  union {
    struct {
      gchar		*description;
    } id;
    struct {
      gint		 nassocs;
      GstIndexAssociation 
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

struct _GstIndexGroup {
  /* unique ID of group in index */
  gint groupnum;

  /* list of entries */
  GList *entries;

  /* the certainty level of the group */
  GstIndexCertainty certainty;

  /* peer group that contains more certain entries */
  gint peergroup;
};

typedef gboolean 	(*GstIndexFilter)	 	(GstIndex *index, 
							 GstIndexEntry *entry);

typedef enum {
  GST_INDEX_RESOLVER_CUSTOM,
  GST_INDEX_RESOLVER_GTYPE,
  GST_INDEX_RESOLVER_PATH
} GstIndexResolverMethod;

typedef gboolean 	(*GstIndexResolver) 		(GstIndex *index, 
						   	 GstObject *writer, 
						   	 gchar **writer_string,
						   	 gpointer user_data);
typedef enum {
  GST_INDEX_WRITABLE 		= GST_OBJECT_FLAG_LAST,	
  GST_INDEX_READABLE,	

  GST_INDEX_FLAG_LAST 		= GST_OBJECT_FLAG_LAST + 8
} GstIndexFlags;

#define GST_INDEX_IS_READABLE(obj)    (GST_FLAG_IS_SET (obj, GST_INDEX_READABLE))
#define GST_INDEX_IS_WRITABLE(obj)    (GST_FLAG_IS_SET (obj, GST_INDEX_WRITABLE))

struct _GstIndex {
  GstObject		 object;

  GList			*groups;
  GstIndexGroup		*curgroup;
  gint			 maxgroup;

  GstIndexResolverMethod method;
  GstIndexResolver 	 resolver;
  gpointer		 resolver_user_data;

  GstIndexFilter	 filter;
  gpointer		 filter_user_data;

  GHashTable		*writers;
  gint			 last_id;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstIndexClass {
  GstObjectClass parent_class;

  gboolean	(*get_writer_id)	(GstIndex *index, gint *writer_id, gchar *writer_string);

  void		(*commit)		(GstIndex *index, gint id);

  /* abstract methods */
  void		(*add_entry)		(GstIndex *index, GstIndexEntry *entry);

  GstIndexEntry* (*get_assoc_entry)	(GstIndex *index, gint id, 
		                         GstIndexLookupMethod method, GstAssocFlags flags,
		                         GstFormat format, gint64 value,
					 GCompareDataFunc func,
					 gpointer user_data); 
  /* signals */
  void		(*entry_added)		(GstIndex *index, GstIndexEntry *entry);

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_index_get_type		(void);
GstIndex*		gst_index_new			(void);
void			gst_index_commit		(GstIndex *index, gint id);

gint			gst_index_get_group		(GstIndex *index);
gint			gst_index_new_group		(GstIndex *index);
gboolean		gst_index_set_group		(GstIndex *index, gint groupnum);

void			gst_index_set_certainty		(GstIndex *index, 
							 GstIndexCertainty certainty);
GstIndexCertainty	gst_index_get_certainty		(GstIndex *index);

void			gst_index_set_filter		(GstIndex *index, 
		                                         GstIndexFilter filter, gpointer user_data);
void			gst_index_set_resolver		(GstIndex *index, 
		                                         GstIndexResolver resolver, gpointer user_data);

gboolean 		gst_index_get_writer_id 	(GstIndex *index, GstObject *writer, gint *id);

GstIndexEntry*		gst_index_add_format		(GstIndex *index, gint id, GstFormat format); 
GstIndexEntry*		gst_index_add_association	(GstIndex *index, gint id, GstAssocFlags flags,
							 GstFormat format, gint64 value, ...);
GstIndexEntry*		gst_index_add_object		(GstIndex *index, gint id, gchar *key,
							 GType type, gpointer object);
GstIndexEntry*		gst_index_add_id		(GstIndex *index, gint id,
							 gchar *description); 

GstIndexEntry*		gst_index_get_assoc_entry	(GstIndex *index, gint id, 
		 					 GstIndexLookupMethod method, GstAssocFlags flags,
		                                         GstFormat format, gint64 value);
GstIndexEntry*		gst_index_get_assoc_entry_full	(GstIndex *index, gint id, 
							 GstIndexLookupMethod method, GstAssocFlags flags,
		                                         GstFormat format, gint64 value,
							 GCompareDataFunc func,
							 gpointer user_data);

/* working with index entries */
GstIndexEntry *         gst_index_entry_copy            (GstIndexEntry *entry);
void			gst_index_entry_free		(GstIndexEntry *entry);
gboolean		gst_index_entry_assoc_map	(GstIndexEntry *entry,
		                                         GstFormat format, gint64 *value);
/*
 * creating indexs
 *
 */
#define GST_TYPE_INDEX_FACTORY  		(gst_index_factory_get_type())
#define GST_INDEX_FACTORY(obj) 			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_INDEX_FACTORY, GstIndexFactory))
#define GST_IS_INDEX_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_INDEX_FACTORY))
#define GST_INDEX_FACTORY_CLASS(klass) 		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_INDEX_FACTORY, GstIndexFactoryClass))
#define GST_IS_INDEX_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_INDEX_FACTORY))
#define GST_INDEX_FACTORY_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_INDEX_FACTORY, GstIndexFactoryClass))

typedef struct _GstIndexFactory GstIndexFactory;
typedef struct _GstIndexFactoryClass GstIndexFactoryClass;

struct _GstIndexFactory {
  GstPluginFeature feature;
	    
  gchar *longdesc;            /* long description of the index (well, don't overdo it..) */
  GType type;                 /* unique GType of the index */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstIndexFactoryClass {
  GstPluginFeatureClass parent; 

  gpointer _gst_reserved[GST_PADDING];
};

GType 			gst_index_factory_get_type 	(void);

GstIndexFactory*	gst_index_factory_new 		(const gchar *name, 
							 const gchar *longdesc, GType type);
void 			gst_index_factory_destroy	(GstIndexFactory *factory);

GstIndexFactory*	gst_index_factory_find		(const gchar *name);

GstIndex*		gst_index_factory_create 	(GstIndexFactory *factory);
GstIndex*		gst_index_factory_make   	(const gchar *name);

G_END_DECLS

#endif /* __GST_INDEX_H__ */
