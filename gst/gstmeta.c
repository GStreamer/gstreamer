/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gst/gstmeta.h>
#include <gst/gsttrace.h>


GstMeta *gst_meta_new_size(gint size) {
  GstMeta *meta;

  meta = g_malloc(size);
  gst_meta_ref(meta);

  return meta;
}

void gst_meta_ref(GstMeta *meta) {
  g_return_if_fail(meta != NULL);

  gst_trace_add_entry(NULL,0,meta,"ref meta");
  meta->refcount++;
}

void gst_meta_unref(GstMeta *meta) {
  g_return_if_fail(meta != NULL);

  gst_trace_add_entry(NULL,0,meta,"unref meta");
  meta->refcount--;

  if (meta->refcount == 0) {
//    gst_trace_add_entry(NULL,0,meta,"destroy meta");
    g_free(meta);
    g_print("freeing metadata\n");
  }
}


GstMeta *gst_meta_cow(GstMeta *meta) {
  g_return_if_fail(meta != NULL);
}
