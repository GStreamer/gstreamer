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

#ifndef __GES_TYPES_H__
#define __GES_TYPES_H__

typedef struct _GESCustomTimelineSource GESCustomTimelineSource;
typedef struct _GESCustomTimelineSourceClass GESCustomTimelineSourceClass;

typedef struct _GESSimpleTimelineLayer GESSimpleTimelineLayer;
typedef struct _GESSimpleTimelineLayerClass GESSimpleTimelineLayerClass;

typedef struct _GESTimeline GESTimeline;
typedef struct _GESTimelineClass GESTimelineClass;

typedef struct _GESTimelineLayer GESTimelineLayer;
typedef struct _GESTimelineLayerClass GESTimelineLayerClass;

typedef struct _GESTimelineObject GESTimelineObject;
typedef struct _GESTimelineObjectClass GESTimelineObjectClass;

typedef struct _GESTimelinePipeline GESTimelinePipeline;
typedef struct _GESTimelinePipelineClass GESTimelinePipelineClass;

typedef struct _GESTimelineSource GESTimelineSource;
typedef struct _GESTimelineSourceClass GESTimelineSourceClass;

typedef struct _GESTimelineFileSource GESTimelineFileSource;
typedef struct _GESTimelineFileSourceClass GESTimelineFileSourceClass;
typedef struct _GESTimelineTransition GESTimelineTransition;
typedef struct _GESTimelineTransitionClass GESTimelineTransitionClass;

typedef struct _GESTrack GESTrack;
typedef struct _GESTrackClass GESTrackClass;

typedef struct _GESTrackObject GESTrackObject;
typedef struct _GESTrackObjectClass GESTrackObjectClass;

typedef struct _GESTrackSource GESTrackSource;
typedef struct _GESTrackSourceClass GESTrackSourceClass;

typedef struct _GESTrackFileSource GESTrackFileSource;
typedef struct _GESTrackFileSourceClass GESTrackFileSourceClass;

typedef struct _GESTrackTransition GESTrackTransition;
typedef struct _GESTrackTransitionClass GESTrackTransitionClass;


#endif /* __GES_TYPES_H__ */
