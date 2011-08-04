/* GStreamer
 * Copyright (C) 2010 Wesley Miller <wmiller@sdr.com>
 *
 *
 *  gst_element_print_properties(): a tool to inspect GStreamer
 *                                  element properties
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GST_ELEMENT_PRINT_PROPERTIES_H
#define GST_ELEMENT_PRINT_PROPERTIES_H

extern void gst_element_print_properties (GstElement * element);
extern void print_column_titles (guint c2w, guint c3w, guint c4w);
extern void print_element_info (GstElement * element, guint c2w, guint c3w,
    guint c4w);
extern gchar *flags_to_string (GFlagsValue * vals, guint flags);
extern void print_caps (const GstCaps * caps, const gchar * pfx);
extern gboolean print_field (GQuark field, const GValue * value, gpointer pfx);

#endif
