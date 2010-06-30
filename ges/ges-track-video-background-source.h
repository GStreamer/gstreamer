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

#ifndef _GES_TRACK_VIDEO_BACKGROUND_SOURCE
#define _GES_TRACK_VIDEO_BACKGROUND_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE ges_track_vbg_src_get_type()

#define GES_TRACK_VIDEO_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE, GESTrackVideoBackgroundSource))

#define GES_TRACK_VIDEO_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE, GESTrackVideoBackgroundSourceClass))

#define GES_IS_TRACK_VIDEO_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE))

#define GES_IS_TRACK_VIDEO_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE))

#define GES_TRACK_VIDEO_BACKGROUND_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_VIDEO_BACKGROUND_SOURCE, GESTrackVideoBackgroundSourceClass))

/**$
 * GESTrackVideoBgSrcPattern:
 * @GES_TRACK_VIDEO_BG_SRC_SMPTE: A standard SMPTE test pattern
 * @GES_TRACK_VIDEO_BG_SRC_SNOW: Random noise
 * @GES_TRACK_VIDEO_BG_SRC_BLACK: A black image
 * @GES_TRACK_VIDEO_BG_SRC_WHITE: A white image
 * @GES_TRACK_VIDEO_BG_SRC_RED: A red image$
 * @GES_TRACK_VIDEO_BG_SRC_GREEN: A green image
 * @GES_TRACK_VIDEO_BG_SRC_BLUE: A blue image
 * @GES_TRACK_VIDEO_BG_SRC_CHECKERS1: Checkers pattern (1px)
 * @GES_TRACK_VIDEO_BG_SRC_CHECKERS2: Checkers pattern (2px)
 * @GES_TRACK_VIDEO_BG_SRC_CHECKERS4: Checkers pattern (4px)
 * @GES_TRACK_VIDEO_BG_SRC_CHECKERS8: Checkers pattern (8px)
 * @GES_TRACK_VIDEO_BG_SRC_CIRCULAR: Circular pattern
 * @GES_TRACK_VIDEO_BG_SRC_BLINK: Alternate between black and white
 * @GES_TRACK_VIDEO_BG_SRC_SMPTE75: SMPTE test pattern (75% color bars)
 *
 * The test pattern to produce
 */

typedef enum {
  GES_TRACK_VIDEO_BG_SRC_SMPTE,
  GES_TRACK_VIDEO_BG_SRC_SNOW,
  GES_TRACK_VIDEO_BG_SRC_BLACK,
  GES_TRACK_VIDEO_BG_SRC_WHITE,
  GES_TRACK_VIDEO_BG_SRC_RED,
  GES_TRACK_VIDEO_BG_SRC_GREEN,
  GES_TRACK_VIDEO_BG_SRC_BLUE,
  GES_TRACK_VIDEO_BG_SRC_CHECKERS1,
  GES_TRACK_VIDEO_BG_SRC_CHECKERS2,
  GES_TRACK_VIDEO_BG_SRC_CHECKERS4,
  GES_TRACK_VIDEO_BG_SRC_CHECKERS8,
  GES_TRACK_VIDEO_BG_SRC_CIRCULAR,
  GES_TRACK_VIDEO_BG_SRC_BLINK,
  GES_TRACK_VIDEO_BG_SRC_SMPTE75,
} GESTrackVideoBgSrcPattern;

/** 
 * GESTrackVideoBackgroundSource:
 * @parent: parent
 */
struct _GESTrackVideoBackgroundSource {
  GESTrackSource parent;

  /*< private >*/
  GESTrackVideoBgSrcPattern pattern;
};

/**
 * GESTrackVideoBackgroundSourceClass:
 * @parent_class: parent class
 */

struct _GESTrackVideoBackgroundSourceClass {
  GESTrackSourceClass parent_class;

  /* <public> */
};

GType ges_track_vbg_src_get_type (void);

GESTrackVideoBackgroundSource* ges_track_video_background_source_new (void);
void

ges_track_video_background_source_set_pattern(GESTrackVideoBackgroundSource *,
    GESTrackVideoBgSrcPattern);

G_END_DECLS

#endif /* _GES_TRACK_VIDEO_BACKGROUND_SOURCE */
