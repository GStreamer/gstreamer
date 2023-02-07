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

/**
 * SECTION:gestypes
 * @title: GES Types
 * @short_description: GStreamer Editing Services data types
 *
 * GStreamer Editing Services data types
 */

#pragma once

#include <glib.h>
#include <ges/ges-prelude.h>

G_BEGIN_DECLS

/**
 * GES_PADDING: (attributes doc.skip=true)
 */
#define GES_PADDING         4

/**
 * GES_PADDING_LARGE: (attributes doc.skip=true)
 */
#define GES_PADDING_LARGE   20

/**
 * GESFrameNumber:
 *
 * A datatype to hold a frame number.
 *
 * Since: 1.18
 */
typedef gint64 GESFrameNumber;

/**
 * GES_FRAME_NUMBER_NONE: (value 9223372036854775807) (type GESFrameNumber)
 *
 * Constant to define an undefined frame number
 */
#define GES_FRAME_NUMBER_NONE             ((gint64) 9223372036854775807)

/**
 * GES_FRAME_NUMBER_IS_VALID:
 * Tests if a given GESFrameNumber represents a valid frame
 */
#define GES_FRAME_NUMBER_IS_VALID(frames) (((GESFrameNumber) frames) != GES_FRAME_NUMBER_NONE)

/**
 * GES_TYPE_FRAME_NUMBER:
 *
 * The #GType of a #GESFrameNumber.
 */
#define GES_TYPE_FRAME_NUMBER G_TYPE_UINT64

/* Type definitions */

typedef struct _GESTimeline GESTimeline;
typedef struct _GESTimelineClass GESTimelineClass;

typedef struct _GESLayer GESLayer;
typedef struct _GESLayerClass GESLayerClass;

typedef struct _GESTimelineElementClass GESTimelineElementClass;
typedef struct _GESTimelineElement GESTimelineElement;

typedef struct _GESContainer GESContainer;
typedef struct _GESContainerClass GESContainerClass;

typedef struct _GESClip GESClip;
typedef struct _GESClipClass GESClipClass;

typedef struct _GESOperationClip GESOperationClip;
typedef struct _GESOperationClipClass GESOperationClipClass;

typedef struct _GESPipeline GESPipeline;
typedef struct _GESPipelineClass GESPipelineClass;

typedef struct _GESSourceClip GESSourceClip;
typedef struct _GESSourceClipClass GESSourceClipClass;

typedef struct _GESBaseEffectClip GESBaseEffectClip;
typedef struct _GESBaseEffectClipClass GESBaseEffectClipClass;

typedef struct _GESUriClip GESUriClip;
typedef struct _GESUriClipClass GESUriClipClass;

typedef struct _GESBaseTransitionClip GESBaseTransitionClip;
typedef struct _GESBaseTransitionClipClass GESBaseTransitionClipClass;

typedef struct _GESTransitionClip GESTransitionClip;
typedef struct _GESTransitionClipClass GESTransitionClipClass;

typedef struct _GESTestClip GESTestClip;
typedef struct _GESTestClipClass GESTestClipClass;

typedef struct _GESTitleClip GESTitleClip;
typedef struct _GESTitleClipClass GESTitleClipClass;

typedef struct _GESOverlayClip GESOverlayClip;
typedef struct _GESOverlayClipClass GESOverlayClipClass;

typedef struct _GESTextOverlayClip GESTextOverlayClip;
typedef struct _GESTextOverlayClipClass GESTextOverlayClipClass;

typedef struct _GESEffectClip GESEffectClip;
typedef struct _GESEffectClipClass GESEffectClipClass;

typedef struct _GESGroup GESGroup;
typedef struct _GESGroupClass GESGroupClass;

typedef struct _GESTrack GESTrack;
typedef struct _GESTrackClass GESTrackClass;

typedef struct _GESTrackElement GESTrackElement;
typedef struct _GESTrackElementClass GESTrackElementClass;

typedef struct _GESSource GESSource;
typedef struct _GESSourceClass GESSourceClass;

typedef struct _GESOperation GESOperation;
typedef struct _GESOperationClass GESOperationClass;

typedef struct _GESBaseEffect GESBaseEffect;
typedef struct _GESBaseEffectClass GESBaseEffectClass;

typedef struct _GESEffect GESEffect;
typedef struct _GESEffectClass GESEffectClass;

typedef struct _GESVideoSource GESVideoSource;
typedef struct _GESVideoSourceClass GESVideoSourceClass;

typedef struct _GESAudioSource GESAudioSource;
typedef struct _GESAudioSourceClass GESAudioSourceClass;

typedef struct _GESVideoUriSource GESVideoUriSource;
typedef struct _GESVideoUriSourceClass GESVideoUriSourceClass;

typedef struct _GESAudioUriSource GESAudioUriSource;
typedef struct _GESAudioUriSourceClass GESAudioUriSourceClass;

typedef struct _GESImageSource GESImageSource;
typedef struct _GESImageSourceClass GESImageSourceClass;

typedef struct _GESMultiFileSource GESMultiFileSource;
typedef struct _GESMultiFileSourceClass GESMultiFileSourceClass;

typedef struct _GESTransition GESTransition;
typedef struct _GESTransitionClass GESTransitionClass;

typedef struct _GESAudioTransition GESAudioTransition;
typedef struct _GESAudioTransitionClass
  GESAudioTransitionClass;

typedef struct _GESVideoTransition GESVideoTransition;
typedef struct _GESVideoTransitionClass
  GESVideoTransitionClass;

typedef struct _GESVideoTestSource GESVideoTestSource;
typedef struct _GESVideoTestSourceClass
  GESVideoTestSourceClass;

typedef struct _GESAudioTestSource GESAudioTestSource;
typedef struct _GESAudioTestSourceClass
  GESAudioTestSourceClass;

typedef struct _GESTitleSource GESTitleSource;
typedef struct _GESTitleSourceClass
  GESTitleSourceClass;

typedef struct _GESTextOverlay GESTextOverlay;
typedef struct _GESTextOverlayClass
  GESTextOverlayClass;

typedef struct _GESFormatter GESFormatter;
typedef struct _GESFormatterClass GESFormatterClass;

typedef struct _GESPitiviFormatter GESPitiviFormatter;
typedef struct _GESPitiviFormatterClass GESPitiviFormatterClass;

typedef struct _GESAsset GESAsset;
typedef struct _GESAssetClass GESAssetClass;

typedef struct _GESClipAsset GESClipAsset;
typedef struct _GESClipAssetClass GESClipAssetClass;

typedef struct _GESUriClipAsset GESUriClipAsset;
typedef struct _GESUriClipAssetClass GESUriClipAssetClass;

typedef struct _GESTrackElementAsset GESTrackElementAsset;
typedef struct _GESTrackElementAssetClass GESTrackElementAssetClass;

typedef struct _GESUriSourceAsset GESUriSourceAsset;
typedef struct _GESUriSourceAssetClass GESUriSourceAssetClass;

typedef struct _GESProject GESProject;
typedef struct _GESProjectClass GESProjectClass;

typedef struct _GESExtractable GESExtractable;
typedef struct _GESExtractableInterface GESExtractableInterface;

typedef struct _GESVideoTrackClass GESVideoTrackClass;
typedef struct _GESVideoTrack GESVideoTrack;

typedef struct _GESAudioTrackClass GESAudioTrackClass;
typedef struct _GESAudioTrack GESAudioTrack;

typedef struct _GESMarkerList GESMarkerList;
typedef struct _GESMarker GESMarker;

typedef struct _GESEffectAssetClass GESEffectAssetClass;
typedef struct _GESEffectAsset GESEffectAsset;

typedef struct _GESXmlFormatterClass GESXmlFormatterClass;
typedef struct _GESXmlFormatter GESXmlFormatter;

typedef struct _GESDiscovererManager GESDiscovererManager;
typedef struct _GESDiscovererManagerClass GESDiscovererManagerClass;

/**
 * GES_DECLARE_TYPE: (attributes doc.skip=true)
 */
#define GES_DECLARE_TYPE(ObjName, obj_name, OBJ_NAME)    \
  GES_API GType ges_##obj_name##_get_type(void);                               \
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS                                             \
  typedef struct _GES##ObjName##Private GES##ObjName##Private;                 \
                                                                               \
  G_DEFINE_AUTOPTR_CLEANUP_FUNC(GES##ObjName, gst_object_unref)                \
                                                                               \
  static G_GNUC_UNUSED inline GES##ObjName *GES_##OBJ_NAME(gpointer ptr) {                   \
    return G_TYPE_CHECK_INSTANCE_CAST(ptr, ges_##obj_name##_get_type(),        \
                                      GES##ObjName);                           \
  }                                                                            \
  static G_GNUC_UNUSED inline GES##ObjName##Class *GES_##OBJ_NAME##_CLASS(gpointer ptr) {    \
    return G_TYPE_CHECK_CLASS_CAST(ptr, ges_##obj_name##_get_type(),           \
                                   GES##ObjName##Class);                       \
  }                                                                            \
  static G_GNUC_UNUSED inline gboolean GES_IS_##OBJ_NAME(gpointer ptr) {                     \
    return G_TYPE_CHECK_INSTANCE_TYPE(ptr, ges_##obj_name##_get_type());       \
  }                                                                            \
  static G_GNUC_UNUSED inline gboolean GES_IS_##OBJ_NAME##_CLASS(gpointer ptr) {             \
    return G_TYPE_CHECK_CLASS_TYPE(ptr, ges_##obj_name##_get_type());          \
  }                                                                            \
  static G_GNUC_UNUSED inline GES##ObjName##Class *GES_##OBJ_NAME##_GET_CLASS(               \
      gpointer ptr) {                                                          \
    return G_TYPE_INSTANCE_GET_CLASS(ptr, ges_##obj_name##_get_type(),         \
                                     GES##ObjName##Class);                     \
  }                                                                            \
  G_GNUC_END_IGNORE_DEPRECATIONS

G_END_DECLS
