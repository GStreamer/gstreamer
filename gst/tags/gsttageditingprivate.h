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


#ifndef __GST_OGG_PLUGINS_H__
#define __GST_OGG_PLUGINS_H__

#include "gsttagediting.h"

G_BEGIN_DECLS
  

typedef struct _GstTagEntryMatch GstTagEntryMatch;
struct _GstTagEntryMatch {
  gchar *	gstreamer_tag;
  gchar *	original_tag;
};


GType gst_vorbis_tag_get_type (void);


G_END_DECLS

#endif /* __GST_OGG_PLUGINS_H__ */
