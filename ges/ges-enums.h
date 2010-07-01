/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#ifndef __GES_ENUMS_H__
#define __GES_ENUMS_H__

#include <glib-object.h>

#define GES_TYPE_TRACK_TYPE (ges_track_type_get_type ())

/**
 * GESTrackType:
 * @GES_TRACK_TYPE_UNKNOWN: A track of unknown type (i.e. invalid)
 * @GES_TRACK_TYPE_AUDIO: An audio track
 * @GES_TRACK_TYPE_VIDEO: A video track
 * @GES_TRACK_TYPE_TEXT: A text (subtitle) track
 * @GES_TRACK_TYPE_CUSTOM: A custom-content track
 *
 * Types of content handled by a track. If the content is not one of
 * @GES_TRACK_TYPE_AUDIO, @GES_TRACK_TYPE_VIDEO or @GES_TRACK_TYPE_TEXT,
 * the user of the #GESTrack must set the type to @GES_TRACK_TYPE_CUSTOM.
 *
 * @GES_TRACK_TYPE_UNKNOWN is for internal purposes and should not be used
 * by users
 */

typedef enum {
  GES_TRACK_TYPE_UNKNOWN = 1 << 0,
  GES_TRACK_TYPE_AUDIO   = 1 << 1,
  GES_TRACK_TYPE_VIDEO   = 1 << 2,
  GES_TRACK_TYPE_TEXT    = 1 << 3,
  GES_TRACK_TYPE_CUSTOM  = 1 << 4,
} GESTrackType;

G_BEGIN_DECLS

GType ges_track_type_get_type (void);

G_END_DECLS

#endif /* __GES_ENUMS_H__ */
