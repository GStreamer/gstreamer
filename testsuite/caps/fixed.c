/* GStreamer
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

#include <gst/gst.h>

gint
main (gint argc, gchar * argv[])
{
#if 0
  GstCaps *caps;

  gst_init (&argc, &argv);

  caps = GST_CAPS_NEW ("testcaps", "unknown/unknown", NULL);

  /* newly crrated caps without props is fixed */
  g_assert (GST_CAPS_IS_FIXED (caps));

  entry = gst_props_entry_new ("foo", GST_PROPS_INT (5));
  /* this entry is fixed */
  g_assert (gst_props_entry_is_fixed (entry));

  props = gst_props_empty_new ();
  /* props are fixed when created */
  g_assert (GST_PROPS_IS_FIXED (props));

  gst_props_add_entry (props, entry);
  /* props should still be fixed */
  g_assert (GST_PROPS_IS_FIXED (props));

  gst_caps_set_props (caps, props);
  /* caps should still be fixed */
  g_assert (GST_CAPS_IS_FIXED (caps));

  entry = gst_props_entry_new ("bar", GST_PROPS_INT_RANGE (1, 5));
  /* this entry is variable */
  g_assert (!gst_props_entry_is_fixed (entry));

  gst_props_add_entry (props, entry);
  /* props should be variable now */
  g_assert (!GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (!GST_CAPS_IS_FIXED (caps));

  gst_props_remove_entry_by_name (props, "bar");
  /* props should be fixed again now */
  g_assert (GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (GST_CAPS_IS_FIXED (caps));

  gst_props_set (props, "foo", GST_PROPS_INT_RANGE (1, 5));
  /* props should be variable again now */
  g_assert (!GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (!GST_CAPS_IS_FIXED (caps));

  gst_props_set (props, "foo", GST_PROPS_INT (5));
  /* props should be fixed again now */
  g_assert (GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (GST_CAPS_IS_FIXED (caps));

#endif

  return 0;
}
