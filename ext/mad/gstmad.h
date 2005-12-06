/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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


#ifndef __GST_MAD_H__
#define __GST_MAD_H__

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <mad.h>
#include <id3tag.h>

G_BEGIN_DECLS


GType                   gst_mad_get_type                (void);
GType                   gst_id3_tag_get_type            (guint type);

GstTagList*             gst_mad_id3_to_tag_list         (const struct id3_tag * tag);
struct id3_tag *        gst_mad_tag_list_to_id3_tag     (GstTagList *           list);


G_END_DECLS

#endif /* __GST_MAD_H__ */
