/* GStreamer
 * Copyright (C) 2001 RidgeRun (http://www.ridgerun.com/)
 * Written by Erik Walthinsen <omega@ridgerun.com>
 *
 * gsttimecache.c: Cache for location<>time mapping
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

#include "gst_private.h"

#include "gsttimecache.h"


static void		gst_timecache_class_init	(GstTimeCacheClass *klass);
static void		gst_timecache_init		(GstTimeCache *tc);

static GstObject *timecache_parent_class = NULL;

GType
gst_timecache_get_type(void) {
  static GType tc_type = 0;

  if (!tc_type) {
    static const GTypeInfo tc_info = {
      sizeof(GstTimeCacheClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_timecache_class_init,
      NULL,
      NULL,
      sizeof(GstTimeCache),
      1,
      (GInstanceInitFunc)gst_timecache_init,
    };
    tc_type = g_type_register_static(GST_TYPE_OBJECT, "GstTimeCache", &tc_info, 0);
  }
  return tc_type;
}

static void
gst_timecache_class_init (GstTimeCacheClass *klass)
{
  timecache_parent_class = g_type_class_ref(GST_TYPE_OBJECT);
}

static GstTimeCacheGroup *
gst_timecache_group_new(guint groupnum)
{
  GstTimeCacheGroup *tcgroup = g_new(GstTimeCacheGroup,1);

  tcgroup->groupnum = groupnum;
  tcgroup->entries = NULL;
  tcgroup->certainty = GST_TIMECACHE_UNKNOWN;
  tcgroup->peergroup = -1;
  tcgroup->mintimestamp = 0LL;
  tcgroup->maxtimestamp = 0LL;
  tcgroup->minlocation = 0LL;
  tcgroup->maxlocation = 0LL;

  GST_DEBUG(0, "created new timecache group %d\n",groupnum);

  return tcgroup;
}

static void
gst_timecache_init (GstTimeCache *tc)
{
  tc->curgroup = gst_timecache_group_new(0);
  tc->maxgroup = 0;
  tc->groups = g_list_prepend(NULL, tc->curgroup);
  GST_DEBUG(0, "created new timecache\n");
}

GstTimeCache *
gst_timecache_new()
{
  GstTimeCache *tc;

  tc = g_object_new (gst_timecache_get_type (), NULL);

  return tc;
}

gint
gst_timecache_get_group(GstTimeCache *tc)
{
  return tc->curgroup->groupnum;
}

gint
gst_timecache_new_group(GstTimeCache *tc)
{
  tc->curgroup = gst_timecache_group_new(++tc->maxgroup);
  tc->groups = g_list_append(tc->groups,tc->curgroup);
  GST_DEBUG(0, "created new group %d in timecache\n",tc->maxgroup);
  return tc->maxgroup;
}

gboolean
gst_timecache_set_group(GstTimeCache *tc, gint groupnum)
{
  GList *list;
  GstTimeCacheGroup *tcgroup;

  // first check for null change
  if (groupnum == tc->curgroup->groupnum)
    return TRUE;

  // else search for the proper group
  list = tc->groups;
  while (list) {
    tcgroup = (GstTimeCacheGroup *)(list->data);
    list = g_list_next(list);
    if (tcgroup->groupnum == groupnum) {
      tc->curgroup = tcgroup;
      GST_DEBUG(0, "switched to timecache group %d\n", tcgroup->groupnum);
      return TRUE;
    }
  }

  // couldn't find the group in question
  GST_DEBUG(0, "couldn't find timecache group %d\n",groupnum);
  return FALSE;
}

void
gst_timecache_set_certainty(GstTimeCache *tc, GstTimeCacheCertainty certainty)
{
  tc->curgroup->certainty = certainty;
}

GstTimeCacheCertainty
gst_timecache_get_certainty(GstTimeCache *tc)
{
  return tc->curgroup->certainty;
}

void
gst_timecache_add_entry (GstTimeCache *tc, guint64 location, gint64 timestamp)
{
  GstTimeCacheEntry *entry = g_new(GstTimeCacheEntry,1);

  entry->location = location;
  entry->timestamp = timestamp;

  // add the entry to the list
  tc->curgroup->entries = g_list_prepend(tc->curgroup->entries,entry);

  // update the bounds
  if (tc->curgroup->mintimestamp > timestamp) tc->curgroup->mintimestamp = timestamp;
  if (tc->curgroup->maxtimestamp < timestamp) tc->curgroup->maxtimestamp = timestamp;
  if (tc->curgroup->minlocation > location) tc->curgroup->minlocation = location;
  if (tc->curgroup->maxlocation < location) tc->curgroup->maxlocation = location;

  GST_DEBUG(0, "added entry to timecache group %d\n",tc->curgroup->groupnum);
}

gint _gst_timecache_find_location (const GstTimeCacheEntry *entry, const guint64 *location) {
  if (*location < entry->location) return -1;
  else if (*location > entry->location) return 1;
  else return 0;
}

gboolean
gst_timecache_find_location (GstTimeCache *tc, guint64 location, gint64 *timestamp)
{
  GList *list;
  GstTimeCacheEntry *entry = NULL;

  // first check to see if it's in the current group
  if ((tc->curgroup->minlocation <= location) && (location <= tc->curgroup->maxlocation)) {
    GST_DEBUG(0, "location %Ld is in group %d\n",location,tc->curgroup->groupnum);
    list = g_list_find_custom(tc->curgroup->entries,&location,(GCompareFunc)_gst_timecache_find_location);
    if (list) entry = (GstTimeCacheEntry *)(list->data);
    if (entry) *timestamp = entry->timestamp;
    return TRUE;
  }

  // TODO: search other groups

  // failure
  return FALSE;
}

gint _gst_timecache_find_timestamp (const GstTimeCacheEntry *entry, const gint64 *timestamp) {
  if (*timestamp < entry->timestamp) return -1;
  else if (*timestamp > entry->timestamp) return 1;
  else return 0;
}

gboolean
gst_timecache_find_timestamp (GstTimeCache *tc, gint64 timestamp, guint64 *location)
{
  GList *entries, *groups;
  GstTimeCacheEntry *entry = NULL;
  GstTimeCacheGroup *group;

  // first check to see if it's in the current group
  if ((tc->curgroup->mintimestamp <= timestamp) && (timestamp <= tc->curgroup->maxtimestamp)) {
    GST_DEBUG(0, "timestamp %Ld may be in group %d\n",timestamp,tc->curgroup->groupnum);
    entries = g_list_find_custom(tc->curgroup->entries,&timestamp,(GCompareFunc)_gst_timecache_find_timestamp);
    if (entries) entry = (GstTimeCacheEntry *)(entries->data);
    if (entry) {
      *location = entry->location;
      return TRUE;
    }
  }

  groups = tc->groups;
  while (groups) {
    group = (GstTimeCacheGroup *)(groups->data);
    groups = g_list_next(groups);

    if ((group->mintimestamp <= timestamp) && (timestamp <= group->maxtimestamp)) {
      GST_DEBUG(0, "timestamp %Ld may be in group %d\n",timestamp,group->groupnum);
      entries = g_list_find_custom(group->entries,&timestamp,(GCompareFunc)_gst_timecache_find_timestamp);
      if (entries) entry = (GstTimeCacheEntry *)(entries->data);
      if (entry) {
        *location = entry->location;
        return TRUE;
      }
    }
  }

  // failure
  return FALSE;
}

