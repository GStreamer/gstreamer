/* gst-python
 * Copyright (C) 2002 David I. Lehn
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
 * 
 * Author: David I. Lehn <dlehn@users.sourceforge.net>
 */

#include "pygobject.h"
#include <gst/gst.h>

#include "common.h"

void iterate_bin_all(GstBin *bin) {
	g_return_if_fail(bin != NULL);
	g_return_if_fail(GST_IS_BIN(bin));

	pyg_unblock_threads();
	while (gst_bin_iterate(bin));
	pyg_block_threads();
}

static gboolean iterate_bin(gpointer data) {
	GstBin *bin;

	bin = GST_BIN(data);
	return gst_bin_iterate(bin);
}

static void iterate_bin_destroy(gpointer data) {
	GstBin *bin;

	bin = GST_BIN(data);
	gst_object_unref(GST_OBJECT(bin));
}

guint add_iterate_bin(GstBin *bin) {
	g_return_val_if_fail(bin != NULL, FALSE);
	g_return_val_if_fail(GST_IS_BIN(bin), FALSE);

	gst_object_ref(GST_OBJECT(bin));
	return g_idle_add_full(
			G_PRIORITY_DEFAULT_IDLE,
			iterate_bin,
			bin,
			iterate_bin_destroy);
}

void remove_iterate_bin(guint id) {
	g_source_remove(id);
}
