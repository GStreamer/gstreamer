/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttaginterface.h: Interfaces for tagging
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

#ifndef __GST_TAG_INTERFACE_H__
#define __GST_TAG_INTERFACE_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_TAG_SETTER		(gst_tag_setter_get_type ())
#define GST_TAG_SETTER(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TAG_SETTER, GstTagSetter))
#define GST_TAG_SETTER_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), GST_TYPE_TAG_SETTER, GstTagSetter))
#define GST_IS_TAG_SETTER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TAG_SETTER))
#define GST_TAG_SETTER_GET_IFACE(obj)	(G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_TAG_SETTER, GstTagSetterIFace))
typedef struct _GstTagSetter GstTagSetter;	/* Dummy typedef */
typedef struct _GstTagSetterIFace GstTagSetterIFace;

/* use an empty interface here to allow detection of elements using user-set
   tags */
struct _GstTagSetterIFace
{
  GTypeInterface g_iface;

  /* signals */

  /* virtual table */
};

GType
gst_tag_setter_get_type (void)
    G_GNUC_CONST;

     void gst_tag_setter_merge (GstTagSetter * setter,
    const GstTagList * list, GstTagMergeMode mode);
     void gst_tag_setter_add (GstTagSetter * setter,
    GstTagMergeMode mode, const gchar * tag, ...);

     void gst_tag_setter_add_values (GstTagSetter * setter,
    GstTagMergeMode mode, const gchar * tag, ...);

     void gst_tag_setter_add_valist (GstTagSetter * setter,
    GstTagMergeMode mode, const gchar * tag, va_list var_args);

     void gst_tag_setter_add_valist_values (GstTagSetter * setter,
    GstTagMergeMode mode, const gchar * tag, va_list var_args);

     G_CONST_RETURN GstTagList *gst_tag_setter_get_list (GstTagSetter * setter);

     void gst_tag_setter_set_merge_mode (GstTagSetter * setter,
    GstTagMergeMode mode);
     GstTagMergeMode gst_tag_setter_get_merge_mode (GstTagSetter * setter);

G_END_DECLS
#endif /* __GST_TAG_INTERFACE_H__ */
