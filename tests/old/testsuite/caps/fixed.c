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

/* START of block from 0.7, which is not in 0.6 */
#define GST_PROPS_ENTRY_IS_VARIABLE(a)  (((GstPropsEntry*)(a))->propstype > GST_PROPS_VAR_TYPE)

struct _GstPropsEntry {
  GQuark        propid;
  GstPropsType  propstype;

  union {
    /* flat values */
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;
    gfloat   float_data;

    /* structured values */
    struct {
      GList *entries;
    } list_data;     struct {
      gchar *string;     } string_data;
    struct {       gint min;
      gint max;
    } int_range_data;
    struct {
      gfloat min;
      gfloat max;
    } float_range_data;
  } data;
};

static void
gst_props_remove_entry_by_id (GstProps *props, GQuark propid)
{
  GList *properties;
  gboolean found;

  /* assume fixed */
  GST_PROPS_FLAG_SET (props, GST_PROPS_FIXED);

  found = FALSE;

  properties = props->properties;
  while (properties) {
    GList *current = properties;
    GstPropsEntry *lentry = (GstPropsEntry *) current->data;

    properties = g_list_next (properties);

    if (lentry->propid == propid) {
      found = TRUE;
      g_list_delete_link (props->properties, current);
    }
    else if (GST_PROPS_ENTRY_IS_VARIABLE (lentry)) {
      GST_PROPS_FLAG_UNSET (props, GST_PROPS_FIXED);
      /* no need to check for further variable entries
       * if we already removed the entry */
      if (found)
        break;
    }
  }
}

void
gst_props_remove_entry_by_name (GstProps *props, const gchar *name)
{
  GQuark quark;

  g_return_if_fail (props != NULL);
  g_return_if_fail (name != NULL);

  quark = g_quark_from_string (name);
  gst_props_remove_entry_by_id (props, quark);
}
/* END of block from 0.7, which is not in 0.6 */

gint 
main (gint argc, gchar *argv[])
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
  
  gst_init (&argc, &argv);

  caps = GST_CAPS_NEW (
		  "testcaps",
		  "unkown/unknown",
		  NULL);

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

  gst_props_set (props, "foo", GST_PROPS_INT_RANGE (1,5));
  /* props should be variable again now */
  g_assert (!GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (!GST_CAPS_IS_FIXED (caps));

  gst_props_set (props, "foo", GST_PROPS_INT (5));
  /* props should be fixed again now */
  g_assert (GST_PROPS_IS_FIXED (props));
  /* caps too */
  g_assert (GST_CAPS_IS_FIXED (caps));


  return 0;
}
