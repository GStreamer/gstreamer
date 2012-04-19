/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

#ifndef __GES_INTERNAL_H__
#define __GES_INTERNAL_H__

#include <gst/gst.h>
#include "ges-track-object.h"

GST_DEBUG_CATEGORY_EXTERN (_ges_debug);
#define GST_CAT_DEFAULT _ges_debug

GESTrackObject *
ges_track_object_copy (GESTrackObject * object, gboolean deep);

#endif /* __GES_INTERNAL_H__ */
