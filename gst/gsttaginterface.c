/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttaginterface.c: interface for tag setting on elements
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttaginterface.h"
#include <gobject/gvaluecollector.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_tag_interface_debug);
#define GST_CAT_DEFAULT tag_tag_interface_debug

static GQuark gst_tag_key;

typedef struct
{
  GstTagMergeMode mode;
  GstTagList *list;
} GstTagData;

GType
gst_tag_setter_get_type (void)
{
  static GType tag_setter_type = 0;

  if (!tag_setter_type) {
    static const GTypeInfo tag_setter_info = {
      sizeof (GstTagSetterIFace),	/* class_size */
      NULL,			/* base_init */
      NULL,			/* base_finalize */
      NULL,
      NULL,			/* class_finalize */
      NULL,			/* class_data */
      0,
      0,
      NULL
    };

    GST_DEBUG_CATEGORY_INIT (gst_tag_interface_debug, "GstTagInterface", 0,
	"interfaces for tagging");

    tag_setter_type = g_type_register_static (G_TYPE_INTERFACE, "GstTagSetter",
	&tag_setter_info, 0);

    g_type_interface_add_prerequisite (tag_setter_type, GST_TYPE_ELEMENT);

    gst_tag_key = g_quark_from_static_string ("GST_TAG_SETTER");
  }

  return tag_setter_type;
}
static void
gst_tag_data_free (gpointer p)
{
  GstTagData *data = (GstTagData *) p;

  if (data->list)
    gst_tag_list_free (data->list);

  g_free (data);
}
static GstTagData *
gst_tag_setter_get_data (GstTagSetter * setter)
{
  GstTagData *data;

  data = g_object_get_qdata (G_OBJECT (setter), gst_tag_key);
  if (!data) {
    data = g_new (GstTagData, 1);
    data->list = NULL;
    data->mode = GST_TAG_MERGE_KEEP;
    g_object_set_qdata_full (G_OBJECT (setter), gst_tag_key, data,
	gst_tag_data_free);
  }

  return data;
}

/**
 * gst_tag_setter_merge:
 * @setter: a #GstTagSetter
 * @list: a tag list to merge from
 * @mode: the mode to merge with
 *
 * Merges the given list into the setter's list using the given mode.
 */
void
gst_tag_setter_merge (GstTagSetter * setter, const GstTagList * list,
    GstTagMergeMode mode)
{
  GstTagData *data;

  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  data = gst_tag_setter_get_data (setter);
  if (!data->list) {
    data->list = gst_tag_list_copy (list);
  } else {
    gst_tag_list_merge (data->list, list, mode);
  }
}

/**
 * gst_tag_setter_add:
 * @setter: a #GstTagSetter
 * @mode: the mode to use
 * @tag: tag to set
 * @...: more tag / value pairs to set
 *
 * Adds the given tag / value pairs on the setter using the given merge mode. 
 * The list must be terminated with GST_TAG_INVALID.
 */
void
gst_tag_setter_add (GstTagSetter * setter, GstTagMergeMode mode,
    const gchar * tag, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  va_start (args, tag);
  gst_tag_setter_add_valist (setter, mode, tag, args);
  va_end (args);
}

/**
 * gst_tag_setter_add_values:
 * @setter: a #GstTagSetter
 * @mode: the mode to use
 * @tag: tag to set
 * @...: more tag / GValue pairs to set
 *
 * Adds the given tag / GValue pairs on the setter using the given merge mode. 
 * The list must be terminated with GST_TAG_INVALID.
 */
void
gst_tag_setter_add_values (GstTagSetter * setter, GstTagMergeMode mode,
    const gchar * tag, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  va_start (args, tag);
  gst_tag_setter_add_valist_values (setter, mode, tag, args);
  va_end (args);
}

/**
 * gst_tag_setter_add_valist:
 * @setter: a #GstTagSetter
 * @mode: the mode to use
 * @tag: tag to set
 * @var_args: tag / value pairs to set
 *
 * Adds the given tag / value pairs on the setter using the given merge mode. 
 * The list must be terminated with GST_TAG_INVALID.
 */
void
gst_tag_setter_add_valist (GstTagSetter * setter, GstTagMergeMode mode,
    const gchar * tag, va_list var_args)
{
  GstTagData *data;

  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  data = gst_tag_setter_get_data (setter);
  if (!data->list)
    data->list = gst_tag_list_new ();

  gst_tag_list_add_valist (data->list, mode, tag, var_args);
}

/**
 * gst_tag_setter_add_valist_values:
 * @setter: a #GstTagSetter
 * @mode: the mode to use
 * @tag: tag to set
 * @var_args: tag / GValue pairs to set
 *
 * Adds the given tag / GValue pairs on the setter using the given merge mode. 
 * The list must be terminated with GST_TAG_INVALID.
 */
void
gst_tag_setter_add_valist_values (GstTagSetter * setter, GstTagMergeMode mode,
    const gchar * tag, va_list var_args)
{
  GstTagData *data;

  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  data = gst_tag_setter_get_data (setter);
  if (!data->list)
    data->list = gst_tag_list_new ();

  gst_tag_list_add_valist_values (data->list, mode, tag, var_args);
}

/**
 * gst_tag_setter_get_list:
 * @setter: a #GstTagSetter
 *
 * Retrieves a copy of the current list of tags the setter uses.
 * You need to gst_tag_list_free() the list after use.
 *
 * Returns: a current snapshot of the taglist used in the setter
 *	    or NULL if none is used.
 */
const GstTagList *
gst_tag_setter_get_list (GstTagSetter * setter)
{
  g_return_val_if_fail (GST_IS_TAG_SETTER (setter), NULL);

  return gst_tag_setter_get_data (setter)->list;
}

/**
 * gst_tag_setter_set_merge_mode:
 * @setter: a #GstTagSetter
 * @mode: The mode with which tags are added
 *
 * Sets the given merge mode that is used for adding tags from events to tags
 * specified by this interface. The default is #GST_TAG_MERGE_KEEP, which keeps
 * the tags by this interface and discards tags from events.
 */
void
gst_tag_setter_set_merge_mode (GstTagSetter * setter, GstTagMergeMode mode)
{
  g_return_if_fail (GST_IS_TAG_SETTER (setter));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  gst_tag_setter_get_data (setter)->mode = mode;
}

/**
 * gst_tag_setter_get_merge_mode:
 * @setter: a #GstTagSetter
 *
 * Queries the mode by which tags inside the setter are overwritten by tags 
 * from events
 *
 * Returns: the merge mode used inside the element.
 */
GstTagMergeMode
gst_tag_setter_get_merge_mode (GstTagSetter * setter)
{
  g_return_val_if_fail (GST_IS_TAG_SETTER (setter), FALSE);

  return gst_tag_setter_get_data (setter)->mode;
}
