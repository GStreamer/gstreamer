/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstmeta.c: Metadata subsystem for describing buffer properties
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

#include "gstmeta.h"
#include "gsttrace.h"


/**
 * gst_meta_new_size:
 * @size: the size of the new meta data
 *
 * Create a new metadata object with a given size
 *
 * Returns: new meta object
 */
GstMeta*
gst_meta_new_size (gint size) 
{
  GstMeta *meta;

  meta = g_malloc0 (size);
  gst_meta_ref (meta);

  return meta;
}

/**
 * gst_meta_ref:
 * @meta: the meta object to ref
 *
 * increases the refcount of a meta object
 */
void 
gst_meta_ref (GstMeta *meta) 
{
  g_return_if_fail (meta != NULL);

  gst_trace_add_entry (NULL, 0, meta, "ref meta");
  
#ifdef HAVE_ATOMIC_H
  atomic_inc (&(meta->refcount));
#else
  meta->refcount++;
#endif
}

/**
 * gst_meta_unref:
 * @meta: the meta object to unref
 *
 * decreases the refcount of a meta object. if the refcount is zero, the
 * meta object is freed.
 */
void 
gst_meta_unref (GstMeta *meta) 
{
  gint zero;
  g_return_if_fail (meta != NULL);

  gst_trace_add_entry (NULL, 0, meta, "unref meta");
#ifdef HAVE_ATOMIC_H
  zero = atomic_dec_and_test (&(meta->refcount));
#else
  meta->refcount--;
  zero = (meta->refcount == 0);
#endif
  if (zero) {
//    gst_trace_add_entry(NULL,0,meta,"destroy meta");
    g_free (meta);
//    g_print("freeing metadata\n");
  }
}


/**
 * gst_meta_cow:
 * @meta: the meta object prepare for write
 *
 * prepares a meta object for writing. A copy of the meta
 * object is returned if needed.
 *
 * Returns: the meta object or a copy.
 */
GstMeta*
gst_meta_cow (GstMeta *meta) 
{
  g_return_val_if_fail (meta != NULL, NULL);

  return NULL;
}
