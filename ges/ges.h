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

#ifndef __GES_H__
#define __GES_H__
#include <glib.h>
#include <gst/gst.h>

#include <ges/ges-types.h>
#include <ges/ges-enums.h>

#include <ges/ges-timeline.h>
#include <ges/ges-timeline-layer.h>
#include <ges/ges-simple-timeline-layer.h>
#include <ges/ges-timeline-element.h>
#include <ges/ges-clip.h>
#include <ges/ges-timeline-pipeline.h>
#include <ges/ges-source-clip.h>
#include <ges/ges-timeline-test-source.h>
#include <ges/ges-title-clip.h>
#include <ges/ges-operation-clip.h>
#include <ges/ges-effect-clip.h>
#include <ges/ges-overlay-clip.h>
#include <ges/ges-text-overlay-clip.h>
#include <ges/ges-transition-clip.h>
#include <ges/ges-standard-transition-clip.h>
#include <ges/ges-standard-effect-clip.h>
#include <ges/ges-custom-source-clip.h>
#include <ges/ges-effect-clip.h>
#include <ges/ges-uri-clip.h>
#include <ges/ges-screenshot.h>
#include <ges/ges-asset.h>
#include <ges/ges-asset-clip.h>
#include <ges/ges-asset-track-object.h>
#include <ges/ges-uri-asset.h>
#include <ges/ges-project.h>
#include <ges/ges-extractable.h>
#include <ges/ges-base-xml-formatter.h>
#include <ges/ges-xml-formatter.h>

#include <ges/ges-track.h>
#include <ges/ges-track-object.h>
#include <ges/ges-track-source.h>
#include <ges/ges-track-operation.h>

#include <ges/ges-track-filesource.h>
#include <ges/ges-track-image-source.h>
#include <ges/ges-track-video-test-source.h>
#include <ges/ges-track-audio-test-source.h>
#include <ges/ges-track-title-source.h>
#include <ges/ges-track-text-overlay.h>
#include <ges/ges-track-transition.h>
#include <ges/ges-track-video-transition.h>
#include <ges/ges-track-audio-transition.h>
#include <ges/ges-track-effect.h>
#include <ges/ges-track-parse-launch-effect.h>
#include <ges/ges-formatter.h>
#include <ges/ges-pitivi-formatter.h>
#include <ges/ges-utils.h>
#include <ges/ges-meta-container.h>

G_BEGIN_DECLS

#define GES_VERSION_MAJOR (1)
#define GES_VERSION_MINOR (0)
#define GES_VERSION_MICRO (0)
#define GES_VERSION_NANO  (0)

gboolean ges_init    (void);

void     ges_version (guint * major, guint * minor, guint * micro,
                      guint * nano);

#define GES_ERROR_DOMAIN g_quark_from_static_string("GES")

G_END_DECLS

#endif /* __GES_H__ */
