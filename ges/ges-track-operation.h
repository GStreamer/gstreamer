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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TRACK_OPERATION
#define _GES_TRACK_OPERATION

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-track-element.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_OPERATION ges_track_operation_get_type()

#define GES_TRACK_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_OPERATION, GESTrackOperation))

#define GES_TRACK_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_OPERATION, GESTrackOperationClass))

#define GES_IS_TRACK_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_OPERATION))

#define GES_IS_TRACK_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_OPERATION))

#define GES_TRACK_OPERATION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_OPERATION, GESTrackOperationClass))

typedef struct _GESTrackOperationPrivate GESTrackOperationPrivate;

/**
 * GESTrackOperation:
 *
 * Base class for overlays, transitions, and effects
 */

struct _GESTrackOperation {
  /*< private >*/
  GESTrackElement parent;

  GESTrackOperationPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTrackOperationClass:
 */

struct _GESTrackOperationClass {
  /*< private >*/
  GESTrackElementClass parent_class;

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_operation_get_type (void);

G_END_DECLS

#endif /* _GES_TRACK_OPERATION */
