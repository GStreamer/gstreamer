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


#include <riff.h>

/*#define debug(format,args...) g_print(format,##args) */
#define debug(format,args...)


gulong gst_riff_fourcc_to_id(gchar *fourcc) {
  g_return_val_if_fail(fourcc != NULL, 0);

  return (fourcc[0] << 0) | (fourcc[1] << 8) |
         (fourcc[2] << 16) | (fourcc[3] << 24);
}

gchar *gst_riff_id_to_fourcc(gulong id) {
  gchar *fourcc = (gchar *)g_malloc(5);

  g_return_val_if_fail(fourcc != NULL, NULL);

  fourcc[0] = (id >> 0) & 0xff;
  fourcc[1] = (id >> 8) & 0xff;
  fourcc[2] = (id >> 16) & 0xff;
  fourcc[3] = (id >> 24) & 0xff;
  fourcc[4] = 0;

  return fourcc;
}
