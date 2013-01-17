/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TIMELINE_OVERLAY
#define _GES_TIMELINE_OVERLAY

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-operation-clip.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_OVERLAY ges_timeline_overlay_get_type()

#define GES_TIMELINE_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_OVERLAY, GESTimelineOverlay))

#define GES_TIMELINE_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_OVERLAY, GESTimelineOverlayClass))

#define GES_IS_TIMELINE_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_OVERLAY))

#define GES_IS_TIMELINE_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_OVERLAY))

#define GES_TIMELINE_OVERLAY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_OVERLAY, GESTimelineOverlayClass))

typedef struct _GESTimelineOverlayPrivate GESTimelineOverlayPrivate;

/**
 * GESTimelineOverlay:
 */

struct _GESTimelineOverlay {
  /*< private >*/
  GESOperationClip parent;

  GESTimelineOverlayPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineOverlayClass:
 * @parent_class: parent class
 */

struct _GESTimelineOverlayClass {
  GESOperationClipClass parent_class;

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_overlay_get_type (void);

G_END_DECLS

#endif /* _GES_TIMELINE_OVERLAY */

