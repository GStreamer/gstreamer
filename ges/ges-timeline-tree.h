#pragma once

#include <ges/ges.h>
#include "ges-auto-transition.h"

void timeline_tree_track_element          (GNode *root,
                                           GESTimelineElement *element);

void timeline_tree_stop_tracking_element  (GNode *root,
                                           GESTimelineElement *element);

gboolean timeline_tree_can_move_element   (GNode *root,
                                           GESTimelineElement *element,
                                           guint32 priority,
                                           GstClockTime start,
                                           GstClockTime duration,
                                           GError ** error);

gboolean timeline_tree_ripple             (GNode *root,
                                           GESTimelineElement *element,
                                           gint64 layer_priority_offset,
                                           GstClockTimeDiff offset,
                                           GESEdge edge,
                                           GstClockTime snapping_distance,
                                           GError ** error);

void ges_timeline_emit_snapping           (GESTimeline * timeline,
                                           GESTrackElement * elem1,
                                           GESTrackElement * elem2,
                                           GstClockTime snap_time);

gboolean timeline_tree_trim               (GNode *root,
                                           GESTimelineElement *element,
                                           gint64 layer_priority_offset,
                                           GstClockTimeDiff offset,
                                           GESEdge edge,
                                           GstClockTime snapping_distance,
                                           GError ** error);


gboolean timeline_tree_move               (GNode *root,
                                           GESTimelineElement *element,
                                           gint64 layer_priority_offset,
                                           GstClockTimeDiff offset,
                                           GESEdge edge,
                                           GstClockTime snapping_distance,
                                           GError ** error);

gboolean timeline_tree_roll               (GNode * root,
                                           GESTimelineElement * element,
                                           GstClockTimeDiff offset,
                                           GESEdge edge,
                                           GstClockTime snapping_distance,
                                           GError ** error);

typedef GESAutoTransition *
(*GESTreeGetAutoTransitionFunc)           (GESTimeline * timeline,
                                           GESTrackElement * previous,
                                           GESTrackElement * next,
                                           GstClockTime transition_duration);

void
timeline_tree_create_transitions_for_track_element (GNode * root,
                                                    GESTrackElement * element,
                                                    GESTreeGetAutoTransitionFunc get_auto_transition);
void timeline_tree_create_transitions     (GNode *root,
                                           GESTreeGetAutoTransitionFunc get_auto_transition);

GstClockTime timeline_tree_get_duration   (GNode *root);

void timeline_tree_debug                  (GNode * root);

GESAutoTransition *
ges_timeline_create_transition           (GESTimeline * timeline, GESTrackElement * previous,
                                           GESTrackElement * next, GESClip * transition,
                                           GESLayer * layer, guint64 start, guint64 duration);
GESAutoTransition *
ges_timeline_find_auto_transition         (GESTimeline * timeline, GESTrackElement * prev,
                                           GESTrackElement * next, GstClockTime transition_duration);

void
timeline_update_duration                  (GESTimeline * timeline);

void timeline_tree_reset_layer_active     (GNode *root, GESLayer *layer);
void timeline_tree_set_smart_rendering    (GNode * root, gboolean rendering_smartly);

void timeline_tree_init_debug             (void);
