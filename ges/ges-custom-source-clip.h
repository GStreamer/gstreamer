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

#ifndef _GES_CUSTOM_SOURCE_CLIP
#define _GES_CUSTOM_SOURCE_CLIP

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-source-clip.h>

G_BEGIN_DECLS

#define GES_TYPE_CUSTOM_SOURCE_CLIP ges_custom_source_clip_get_type()

#define GES_CUSTOM_SOURCE_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_CUSTOM_SOURCE_CLIP, GESCustomSourceClip))

#define GES_CUSTOM_SOURCE_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_CUSTOM_SOURCE_CLIP, GESCustomSourceClipClass))

#define GES_IS_CUSTOM_SOURCE_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_CUSTOM_SOURCE_CLIP))

#define GES_IS_CUSTOM_SOURCE_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_CUSTOM_SOURCE_CLIP))

#define GES_CUSTOM_SOURCE_CLIP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_CUSTOM_SOURCE_CLIP, GESCustomSourceClipClass))

typedef struct _GESCustomSourceClipPrivate   GESCustomSourceClipPrivate;

/**
 * GESFillTrackElementUserFunc:
 * @object: the #GESClip controlling the track element
 * @trobject: the #GESTrackElement
 * @gnlobj: the GNonLin object that needs to be filled.
 * @user_data: the gpointer to optional user data
 *
 * A function that will be called when the GNonLin object of a corresponding
 * track element needs to be filled.
 *
 * The implementer of this function shall add the proper #GstElement to @gnlobj
 * using gst_bin_add().
 *
 * Returns: TRUE if the implementer succesfully filled the @gnlobj, else #FALSE.
 */
typedef gboolean (*GESFillTrackElementUserFunc) (GESClip * object,
					     GESTrackElement * trobject,
					     GstElement * gnlobj,
					     gpointer user_data);

/**
 * GESCustomSourceClip:
 *
 * Debugging custom timeline source
 */

struct _GESCustomSourceClip {
  GESSourceClip parent;

  /*< private >*/
  GESCustomSourceClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESCustomSourceClipClass:
 */

struct _GESCustomSourceClipClass {
  GESSourceClipClass parent_class;

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_custom_source_clip_get_type (void);

GESCustomSourceClip*
ges_custom_source_clip_new (GESFillTrackElementUserFunc func,
				gpointer user_data);

GESAsset*
ges_asset_custom_source_clip_new (GESFillTrackElementUserFunc func,
				gpointer user_data);


G_END_DECLS

#endif /* _GES_CUSTOM_SOURCE_CLIP */

