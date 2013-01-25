/* GStreamer Editing Services
 * Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
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

#ifndef _GES_STANDARD_EFFECT_CLIP
#define _GES_STANDARD_EFFECT_CLIP

#include <glib-object.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_STANDARD_EFFECT_CLIP ges_standard_effect_clip_get_type()

#define GES_STANDARD_EFFECT_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_STANDARD_EFFECT_CLIP, GESStandardEffectClip))

#define GES_STANDARD_EFFECT_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_STANDARD_EFFECT_CLIP, GESStandardEffectClipClass))

#define GES_IS_STANDARD_EFFECT_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_STANDARD_EFFECT_CLIP))

#define GES_IS_STANDARD_EFFECT_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_STANDARD_EFFECT_CLIP))

#define GES_STANDARD_EFFECT_CLIP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_STANDARD_EFFECT_CLIP, GESStandardEffectClipClass))

typedef struct _GESStandardEffectClipPrivate GESStandardEffectClipPrivate;

/**
 * GESStandardEffectClip:
 */
struct _GESStandardEffectClip {
  /*< private >*/
  GESBaseEffectClip parent;

  GESStandardEffectClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESStandardEffectClipClass:
 *
 */

struct _GESStandardEffectClipClass {
  /*< private >*/
  GESBaseEffectClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_standard_effect_clip_get_type (void);

GESStandardEffectClip *
ges_standard_effect_clip_new (const gchar * video_bin_description,
				      const gchar * audio_bin_description);

G_END_DECLS
#endif /* _GES_STANDARD_EFFECT_CLIP */
