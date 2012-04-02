/* GStreamer
 * (c) 2010, 2012 Alexander Saprykin <xelfium@gmail.com>
 *
 * gsttoc.h: generic TOC API declaration
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

#ifndef __GST_TOC_H__
#define __GST_TOC_H__

#include <gst/gstconfig.h>
#include <gst/gststructure.h>
#include <gst/gsttaglist.h>
#include <gst/gstformat.h>

G_BEGIN_DECLS

typedef struct _GstTocEntry GstTocEntry;
typedef struct _GstToc GstToc;

/**
 * GstTocEntryType:
 * @GST_TOC_ENTRY_TYPE_CHAPTER: a chapter type entry.
 * @GST_TOC_ENTRY_TYPE_EDITION: an edition entry (angle or alternative in other terms).
 *
 * The different types of TOC entry.
 */
typedef enum {
  GST_TOC_ENTRY_TYPE_CHAPTER     = 0,
  GST_TOC_ENTRY_TYPE_EDITION     = 1
} GstTocEntryType;

/**
 * GstTocEntry:
 * @uid: unique (for a whole TOC) id of the entry. This value should be persistent and
 * should not be changed while updating TOC. @uid should be handled as "opaque" value
 * without meaning (e.g. applications should not assume the /editionX/chapterY/chapter/Z structure,
 * other demuxers could do something else), it should help to track updates of certain entries.
 * @type: #GstTocEntryType of this entry.
 * @subentries: list of #GstTocEntry children.
 * @pads: list of #GstPad objects, related to this #GstTocEntry.
 * @tags: tags related to this entry.
 * @info: extra information related to this entry.
 *
 * Definition of TOC entry structure.
 */
struct _GstTocEntry {
  gchar *uid;
  GstTocEntryType type;
  GList *subentries;
  GList *pads;
  GstTagList *tags;
  GstStructure *info;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* FIXME: pad member should be GstPad type, but that's
 * impossible due to recursive includes */

/**
 * GstToc:
 * @entries: list of #GstTocEntry entries of the TOC.
 * @tags: tags related to the whole TOC.
 * @info: extra information related to the TOC.
 *
 * Definition of TOC structure.
 */
struct _GstToc {
  GList *entries;
  GstTagList *tags;
  GstStructure *info;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* functions to create new structures */
GstToc *        gst_toc_new                     (void);
GstTocEntry *   gst_toc_entry_new               (GstTocEntryType type, const gchar *uid);
GstTocEntry *   gst_toc_entry_new_with_pad      (GstTocEntryType type, const gchar *uid, gpointer pad);

/* functions to free structures */
void            gst_toc_entry_free              (GstTocEntry *entry);
void            gst_toc_free                    (GstToc *toc);

GstTocEntry *   gst_toc_find_entry              (const GstToc *toc, const gchar *uid);
GstTocEntry *   gst_toc_entry_copy              (const GstTocEntry *entry);
GstToc      *   gst_toc_copy                    (const GstToc *toc);

void            gst_toc_entry_set_start_stop    (GstTocEntry *entry, gint64 start, gint64 stop);
gboolean        gst_toc_entry_get_start_stop    (const GstTocEntry *entry, gint64 *start, gint64 *stop);

G_END_DECLS

#endif /* __GST_TOC_H__ */

