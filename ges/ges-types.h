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

#ifndef __GES_TYPES_H__
#define __GES_TYPES_H__

/* Padding */
#define GES_PADDING         4

/* padding for very extensible base classes */
#define GES_PADDING_LARGE   20

/* Type definitions */

typedef struct _GESCustomTimelineSource GESCustomTimelineSource;
typedef struct _GESCustomTimelineSourceClass GESCustomTimelineSourceClass;

typedef struct _GESSimpleTimelineLayer GESSimpleTimelineLayer;
typedef struct _GESSimpleTimelineLayerClass GESSimpleTimelineLayerClass;

typedef struct _GESTimeline GESTimeline;
typedef struct _GESTimelineClass GESTimelineClass;

typedef struct _GESTimelineLayer GESTimelineLayer;
typedef struct _GESTimelineLayerClass GESTimelineLayerClass;

typedef struct _GESTimelineElementClass GESTimelineElementClass;
typedef struct _GESTimelineElement GESTimelineElement;

typedef struct _GESClip GESClip;
typedef struct _GESClipClass GESClipClass;

typedef struct _GESTimelineOperation GESTimelineOperation;
typedef struct _GESTimelineOperationClass GESTimelineOperationClass;

typedef struct _GESTimelinePipeline GESTimelinePipeline;
typedef struct _GESTimelinePipelineClass GESTimelinePipelineClass;

typedef struct _GESTimelineSource GESTimelineSource;
typedef struct _GESTimelineSourceClass GESTimelineSourceClass;

typedef struct _GESEffectClip GESEffectClip;
typedef struct _GESEffectClipClass GESEffectClipClass;

typedef struct _GESUriClip GESUriClip;
typedef struct _GESUriClipClass GESUriClipClass;

typedef struct _GESTransitionClip GESTransitionClip;
typedef struct _GESTransitionClipClass GESTransitionClipClass;

typedef struct _GESStandardTransitionClip GESStandardTransitionClip;
typedef struct _GESStandardTransitionClipClass GESStandardTransitionClipClass;

typedef struct _GESTimelineTestSource GESTimelineTestSource;
typedef struct _GESTimelineTestSourceClass GESTimelineTestSourceClass;

typedef struct _GESTimelineTitleSource GESTimelineTitleSource;
typedef struct _GESTimelineTitleSourceClass GESTimelineTitleSourceClass;

typedef struct _GESTimelineOverlay GESTimelineOverlay;
typedef struct _GESTimelineOverlayClass GESTimelineOverlayClass;

typedef struct _GESTimelineTextOverlay GESTimelineTextOverlay;
typedef struct _GESTimelineTextOverlayClass GESTimelineTextOverlayClass;

typedef struct _GESStandardEffectClip GESStandardEffectClip;
typedef struct _GESStandardEffectClipClass GESStandardEffectClipClass;

typedef struct _GESTrack GESTrack;
typedef struct _GESTrackClass GESTrackClass;

typedef struct _GESTrackObject GESTrackObject;
typedef struct _GESTrackObjectClass GESTrackObjectClass;

typedef struct _GESTrackSource GESTrackSource;
typedef struct _GESTrackSourceClass GESTrackSourceClass;

typedef struct _GESTrackOperation GESTrackOperation;
typedef struct _GESTrackOperationClass GESTrackOperationClass;

typedef struct _GESTrackEffect GESTrackEffect;
typedef struct _GESTrackEffectClass GESTrackEffectClass;

typedef struct _GESTrackParseLaunchEffect GESTrackParseLaunchEffect;
typedef struct _GESTrackParseLaunchEffectClass GESTrackParseLaunchEffectClass;

typedef struct _GESTrackFileSource GESTrackFileSource;
typedef struct _GESTrackFileSourceClass GESTrackFileSourceClass;

typedef struct _GESTrackImageSource GESTrackImageSource;
typedef struct _GESTrackImageSourceClass GESTrackImageSourceClass;

typedef struct _GESTrackTransition GESTrackTransition;
typedef struct _GESTrackTransitionClass GESTrackTransitionClass;

typedef struct _GESTrackAudioTransition GESTrackAudioTransition;
typedef struct _GESTrackAudioTransitionClass
  GESTrackAudioTransitionClass;

typedef struct _GESTrackVideoTransition GESTrackVideoTransition;
typedef struct _GESTrackVideoTransitionClass
  GESTrackVideoTransitionClass;

typedef struct _GESTrackVideoTestSource GESTrackVideoTestSource;
typedef struct _GESTrackVideoTestSourceClass
  GESTrackVideoTestSourceClass;

typedef struct _GESTrackAudioTestSource GESTrackAudioTestSource;
typedef struct _GESTrackAudioTestSourceClass
  GESTrackAudioTestSourceClass;

typedef struct _GESTrackTitleSource GESTrackTitleSource;
typedef struct _GESTrackTitleSourceClass
  GESTrackTitleSourceClass;

typedef struct _GESTrackTextOverlay GESTrackTextOverlay;
typedef struct _GESTrackTextOverlayClass
  GESTrackTextOverlayClass;

typedef struct _GESFormatter GESFormatter;
typedef struct _GESFormatterClass GESFormatterClass;

typedef struct _GESPitiviFormatter GESPitiviFormatter;
typedef struct _GESPitiviFormatterClass GESPitiviFormatterClass;

typedef struct _GESAsset GESAsset;
typedef struct _GESAssetClass GESAssetClass;

typedef struct _GESAssetClip GESAssetClip;
typedef struct _GESAssetClipClass GESAssetClipClass;

typedef struct _GESUriClipAsset GESUriClipAsset;
typedef struct _GESUriClipAssetClass GESUriClipAssetClass;

typedef struct _GESAssetTrackObject GESAssetTrackObject;
typedef struct _GESAssetTrackObjectClass GESAssetTrackObjectClass;

typedef struct _GESAssetTrackFileSource GESAssetTrackFileSource;
typedef struct _GESAssetTrackFileSourceClass GESAssetTrackFileSourceClass;

typedef struct _GESProject GESProject;
typedef struct _GESProjectClass GESProjectClass;

typedef struct _GESExtractable GESExtractable;
typedef struct _GESExtractableInterface GESExtractableInterface;

#endif /* __GES_TYPES_H__ */
