/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gsttimecache.h: Header for GstTimeCache
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

#ifndef __GST_TIMECACHE_H__
#define __GST_TIMECACHE_H__

#include <gst/gstobject.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_TIMECACHE		(gst_timecache_get_type ())
#define GST_TIMECACHE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TIMECACHE, GstTimeCache))
#define GST_TIMECACHE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TIMECACHE, GstTimeCacheClass))
#define GST_IS_TIMECACHE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TIMECACHE))
#define GST_IS_TIMECACHE_CLASS(obj)	(GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TIMECACHE))

typedef struct _GstTimeCacheEntry GstTimeCacheEntry;
typedef struct _GstTimeCacheGroup GstTimeCacheGroup;
typedef struct _GstTimeCache GstTimeCache;
typedef struct _GstTimeCacheClass GstTimeCacheClass;

typedef enum {
  GST_TIMECACHE_UNKNOWN,
  GST_TIMECACHE_CERTAIN,
  GST_TIMECACHE_FUZZY_LOCATION,
  GST_TIMECACHE_FUZZY_TIMESTAMP,
  GST_TIMECACHE_FUZZY,
} GstTimeCacheCertainty;

struct _GstTimeCacheEntry {
  guint64 location;
  gint64 timestamp;
};

struct _GstTimeCacheGroup {
  // unique ID of group in cache
  gint groupnum;

  // list of entries
  GList *entries;

  // the certainty level of the group
  GstTimeCacheCertainty certainty;

  // peer group that contains more certain entries
  gint peergroup;

  // range variables
  gint64 mintimestamp,maxtimestamp;
  guint64 minlocation,maxlocation;
};

struct _GstTimeCacheClass {
  GstObjectClass parent_class;
};

struct _GstTimeCache {
  GstObject		object;

  GList			*groups;
  GstTimeCacheGroup	*curgroup;
  gint			maxgroup;
};

GType			gst_timecache_get_type		(void);
GstTimeCache*		gst_timecache_new		();

gint			gst_timecache_get_group		(GstTimeCache *tc);
gint			gst_timecache_new_group		(GstTimeCache *tc);
gboolean		gst_timecache_set_group		(GstTimeCache *tc, gint groupnum);

void			gst_timecache_set_certainty	(GstTimeCache *tc, GstTimeCacheCertainty certainty);
GstTimeCacheCertainty	gst_timecache_get_certainty	(GstTimeCache *tc);

void			gst_timecache_add_entry		(GstTimeCache *tc, guint64 location, gint64 timestamp);

gboolean		gst_timecache_find_location	(GstTimeCache *tc, guint64 location, gint64 *timestamp);
gboolean		gst_timecache_find_timestamp	(GstTimeCache *tc, gint64 timestamp, guint64 *location);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_TIMECACHE_H__ */
