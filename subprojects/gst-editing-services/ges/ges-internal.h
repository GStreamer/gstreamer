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

#pragma once

#include <gst/gst.h>
#include <gst/pbutils/encoding-profile.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#ifndef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT (_ges_debug ())
#endif

#include "ges-timeline.h"
#include "ges-track-element.h"
#include "ges-timeline-element.h"

#include "ges-asset.h"
#include "ges-base-xml-formatter.h"
#include "ges-timeline-tree.h"

G_GNUC_INTERNAL
GstDebugCategory * _ges_debug (void);

/*  The first 2 NLE priorities are used for:
 *    0- The Mixing element
 *    1- The Gaps
 */
#define MIN_NLE_PRIO 2
#define LAYER_HEIGHT 1000

#define _START(obj) GES_TIMELINE_ELEMENT_START (obj)
#define _INPOINT(obj) GES_TIMELINE_ELEMENT_INPOINT (obj)
#define _DURATION(obj) GES_TIMELINE_ELEMENT_DURATION (obj)
#define _MAXDURATION(obj) GES_TIMELINE_ELEMENT_MAX_DURATION (obj)
#define _PRIORITY(obj) GES_TIMELINE_ELEMENT_PRIORITY (obj)
#ifndef _END
#define _END(obj) (_START (obj) + _DURATION (obj))
#endif
#define _set_start0 ges_timeline_element_set_start
#define _set_inpoint0 ges_timeline_element_set_inpoint
#define _set_duration0 ges_timeline_element_set_duration
#define _set_priority0 ges_timeline_element_set_priority

#define GES_CLOCK_TIME_IS_LESS(first, second) \
  (GST_CLOCK_TIME_IS_VALID (first) && (!GST_CLOCK_TIME_IS_VALID (second) \
  || (first) < (second)))

#define DEFAULT_FRAMERATE_N 30
#define DEFAULT_FRAMERATE_D 1
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

#define GES_TIMELINE_ELEMENT_FORMAT \
    "s<%p>" \
    " [ %" GST_TIME_FORMAT \
    " (%" GST_TIME_FORMAT \
    ") - %" GST_TIME_FORMAT "(%" GST_TIME_FORMAT") layer: %" G_GINT32_FORMAT "] "

#define GES_TIMELINE_ELEMENT_ARGS(element) \
    GES_TIMELINE_ELEMENT_NAME(element), element, \
    GST_TIME_ARGS(GES_TIMELINE_ELEMENT_START(element)), \
    GST_TIME_ARGS(GES_TIMELINE_ELEMENT_INPOINT(element)), \
    GST_TIME_ARGS(GES_TIMELINE_ELEMENT_DURATION(element)), \
    GST_TIME_ARGS(GES_TIMELINE_ELEMENT_MAX_DURATION(element)), \
    GES_TIMELINE_ELEMENT_LAYER_PRIORITY(element)

#define GES_FORMAT GES_TIMELINE_ELEMENT_FORMAT
#define GES_ARGS GES_TIMELINE_ELEMENT_ARGS

#define GES_IS_TIME_EFFECT(element) \
  (GES_IS_BASE_EFFECT (element) \
  && ges_base_effect_is_time_effect (GES_BASE_EFFECT (element)))

#define GES_TIMELINE_ELEMENT_SET_BEING_EDITED(element) \
  ELEMENT_SET_FLAG ( \
      ges_timeline_element_peak_toplevel (GES_TIMELINE_ELEMENT (element)), \
      GES_TIMELINE_ELEMENT_SET_SIMPLE)

#define GES_TIMELINE_ELEMENT_UNSET_BEING_EDITED(element) \
  ELEMENT_UNSET_FLAG ( \
      ges_timeline_element_peak_toplevel (GES_TIMELINE_ELEMENT (element)), \
      GES_TIMELINE_ELEMENT_SET_SIMPLE)

#define GES_TIMELINE_ELEMENT_BEING_EDITED(element) \
  ELEMENT_FLAG_IS_SET ( \
      ges_timeline_element_peak_toplevel (GES_TIMELINE_ELEMENT (element)), \
      GES_TIMELINE_ELEMENT_SET_SIMPLE)

/************************
 * Our property masks   *
 ************************/
#define GES_PARAM_NO_SERIALIZATION (1 << (G_PARAM_USER_SHIFT + 1))

#define SUPRESS_UNUSED_WARNING(a) (void)a

G_GNUC_INTERNAL void
ges_timeline_freeze_auto_transitions (GESTimeline * timeline, gboolean freeze);

G_GNUC_INTERNAL GESAutoTransition *
ges_timeline_get_auto_transition_at_edge (GESTimeline * timeline, GESTrackElement * source,
  GESEdge edge);

G_GNUC_INTERNAL gboolean ges_timeline_is_disposed (GESTimeline* timeline);

G_GNUC_INTERNAL gboolean
ges_timeline_edit (GESTimeline * timeline, GESTimelineElement * element,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge,
    guint64 position, GError ** error);

G_GNUC_INTERNAL gboolean
ges_timeline_layer_priority_in_gap (GESTimeline * timeline, guint layer_priority);

G_GNUC_INTERNAL void
ges_timeline_set_track_selection_error  (GESTimeline * timeline,
                                         gboolean was_error,
                                         GError * error);
G_GNUC_INTERNAL gboolean
ges_timeline_take_track_selection_error (GESTimeline * timeline,
                                         GError ** error);

G_GNUC_INTERNAL void
timeline_add_group             (GESTimeline *timeline,
                                GESGroup *group);
G_GNUC_INTERNAL void
timeline_remove_group          (GESTimeline *timeline,
                                GESGroup *group);
G_GNUC_INTERNAL void
timeline_emit_group_added      (GESTimeline *timeline,
                                GESGroup *group);
G_GNUC_INTERNAL void
timeline_emit_group_removed    (GESTimeline * timeline,
                                GESGroup * group, GPtrArray * array);

G_GNUC_INTERNAL
gboolean
timeline_add_element           (GESTimeline *timeline,
                                GESTimelineElement *element);
G_GNUC_INTERNAL
gboolean
timeline_remove_element       (GESTimeline *timeline,
                               GESTimelineElement *element);

G_GNUC_INTERNAL
GNode *
timeline_get_tree           (GESTimeline *timeline);

G_GNUC_INTERNAL
void
timeline_fill_gaps            (GESTimeline *timeline);

G_GNUC_INTERNAL void
timeline_create_transitions (GESTimeline * timeline, GESTrackElement * track_element);

G_GNUC_INTERNAL void timeline_get_framerate(GESTimeline *self, gint *fps_n,
                                            gint *fps_d);
G_GNUC_INTERNAL void
ges_timeline_set_moving_track_elements (GESTimeline * timeline, gboolean moving);

G_GNUC_INTERNAL gboolean
ges_timeline_add_clip (GESTimeline * timeline, GESClip * clip, GError ** error);

G_GNUC_INTERNAL void
ges_timeline_remove_clip (GESTimeline * timeline, GESClip * clip);

G_GNUC_INTERNAL void
ges_timeline_set_smart_rendering (GESTimeline * timeline, gboolean rendering_smartly);

G_GNUC_INTERNAL gboolean
ges_timeline_get_smart_rendering (GESTimeline *timeline);

G_GNUC_INTERNAL GstStreamCollection*
ges_timeline_get_stream_collection (GESTimeline *timeline);

G_GNUC_INTERNAL void
ges_auto_transition_set_source (GESAutoTransition * self, GESTrackElement * source, GESEdge edge);



G_GNUC_INTERNAL
void
track_resort_and_fill_gaps    (GESTrack *track);

G_GNUC_INTERNAL
void
track_disable_last_gap        (GESTrack *track, gboolean disabled);

G_GNUC_INTERNAL void
ges_asset_cache_init (void);

G_GNUC_INTERNAL void
ges_asset_cache_deinit (void);

G_GNUC_INTERNAL void
ges_asset_set_id (GESAsset *asset, const gchar *id);

G_GNUC_INTERNAL void
ges_asset_cache_put (GESAsset * asset, GTask *task);

G_GNUC_INTERNAL gboolean
ges_asset_cache_set_loaded(GType extractable_type, const gchar * id, GError *error);

/* FIXME: marked as GES_API just so they can be used in tests! */

GES_API GESAsset*
ges_asset_cache_lookup(GType extractable_type, const gchar * id);

GES_API gboolean
ges_asset_try_proxy (GESAsset *asset, const gchar *new_id);

G_GNUC_INTERNAL gboolean
ges_asset_finish_proxy (GESAsset * proxy);

G_GNUC_INTERNAL gboolean
ges_asset_request_id_update (GESAsset *asset, gchar **proposed_id,
    GError *error);
G_GNUC_INTERNAL gchar *
ges_effect_asset_id_get_type_and_bindesc (const char    *id,
                                          GESTrackType  *track_type,
                                          GError       **error);

G_GNUC_INTERNAL void _ges_uri_asset_cleanup (void);

G_GNUC_INTERNAL gboolean _ges_uri_asset_ensure_setup (gpointer uriasset_class);

/* GESExtractable internall methods
 *
 * FIXME Check if that should be public later
 */
G_GNUC_INTERNAL GType
ges_extractable_type_get_asset_type              (GType type);

G_GNUC_INTERNAL gchar *
ges_extractable_type_check_id                    (GType type, const gchar *id, GError **error);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
G_GNUC_INTERNAL GParameter *
ges_extractable_type_get_parameters_from_id      (GType type, const gchar *id,
                                                  guint *n_params);
G_GNUC_END_IGNORE_DEPRECATIONS;

G_GNUC_INTERNAL GType
ges_extractable_get_real_extractable_type_for_id (GType type, const gchar * id);

G_GNUC_INTERNAL gboolean
ges_extractable_register_metas                   (GType extractable_type, GESAsset *asset);

/************************************************
 *                                              *
 *        GESFormatter internal methods         *
 *                                              *
 ************************************************/
G_GNUC_INTERNAL void
ges_formatter_set_project                        (GESFormatter *formatter,
                                                  GESProject *project);
G_GNUC_INTERNAL GESProject *
ges_formatter_get_project                        (GESFormatter *formatter);
G_GNUC_INTERNAL  GESAsset *
_find_formatter_asset_for_id                     (const gchar *id);



/************************************************
 *                                              *
 *        GESProject internal methods           *
 *                                              *
 ************************************************/

/* FIXME This should probably become public, but we need to make sure it
 * is the right API before doing so */
G_GNUC_INTERNAL  gboolean ges_project_set_loaded                  (GESProject * project,
                                                                   GESFormatter *formatter,
                                                                   GError *error);
G_GNUC_INTERNAL  gchar * ges_project_try_updating_id              (GESProject *self,
                                                                   GESAsset *asset,
                                                                   GError *error);
G_GNUC_INTERNAL  void ges_project_add_loading_asset               (GESProject *project,
                                                                   GType extractable_type,
                                                                   const gchar *id);
G_GNUC_INTERNAL  gchar* ges_uri_asset_try_update_id               (GError *error, GESAsset *wrong_asset);
/************************************************
 *                                              *
 *   GESBaseXmlFormatter internal methods       *
 *                                              *
 ************************************************/

/* FIXME GESBaseXmlFormatter is all internal for now, the API is not stable
 * fo now, so do not expose it */
G_GNUC_INTERNAL void ges_base_xml_formatter_add_clip (GESBaseXmlFormatter * self,
                                                                 const gchar *id,
                                                                 const char *asset_id,
                                                                 GType type,
                                                                 GstClockTime start,
                                                                 GstClockTime inpoint,
                                                                 GstClockTime duration,
                                                                 guint layer_prio,
                                                                 GESTrackType track_types,
                                                                 GstStructure *properties,
                                                                 GstStructure * children_properties,
                                                                 const gchar *metadatas,
                                                                 GError **error);
G_GNUC_INTERNAL void ges_base_xml_formatter_add_asset        (GESBaseXmlFormatter * self,
                                                                 const gchar * id,
                                                                 GType extractable_type,
                                                                 GstStructure *properties,
                                                                 const gchar *metadatas,
                                                                 const gchar *proxy_id,
                                                                 GError **error);
G_GNUC_INTERNAL void ges_base_xml_formatter_add_layer           (GESBaseXmlFormatter *self,
                                                                 GType extractable_type,
                                                                 guint priority,
                                                                 GstStructure *properties,
                                                                 const gchar *metadatas,
                                                                 gchar **deactivated_tracks,
                                                                 GError **error);
G_GNUC_INTERNAL void ges_base_xml_formatter_add_track           (GESBaseXmlFormatter *self,
                                                                 GESTrackType track_type,
                                                                 GstCaps *caps,
                                                                 const gchar *id,
                                                                 GstStructure *properties,
                                                                 const gchar *metadatas,
                                                                 GError **error);
G_GNUC_INTERNAL void ges_base_xml_formatter_add_encoding_profile(GESBaseXmlFormatter * self,
                                                                 const gchar *type,
                                                                 const gchar *parent,
                                                                 const gchar * name,
                                                                 const gchar * description,
                                                                 GstCaps * format,
                                                                 const gchar * preset,
                                                                 GstStructure * preset_properties,
                                                                 const gchar * preset_name,
                                                                 guint id,
                                                                 guint presence,
                                                                 GstCaps * restriction,
                                                                 guint pass,
                                                                 gboolean variableframerate,
                                                                 GstStructure * properties,
                                                                 gboolean enabled,
                                                                 GError ** error);
G_GNUC_INTERNAL void ges_base_xml_formatter_add_track_element   (GESBaseXmlFormatter *self,
                                                                 GType effect_type,
                                                                 const gchar *asset_id,
                                                                 const gchar * track_id,
                                                                 const gchar *timeline_obj_id,
                                                                 GstStructure *children_properties,
                                                                 GstStructure *properties,
                                                                 const gchar *metadatas,
                                                                 GError **error);

G_GNUC_INTERNAL void ges_base_xml_formatter_add_source          (GESBaseXmlFormatter *self,
                                                                 const gchar * track_id,
                                                                 GstStructure *children_properties,
                                                                 GstStructure *properties,
                                                                 const gchar *metadatas);

G_GNUC_INTERNAL void ges_base_xml_formatter_add_group           (GESBaseXmlFormatter *self,
                                                                 const gchar *name,
                                                                 const gchar *properties,
                                                                 const gchar *metadatas);

G_GNUC_INTERNAL void ges_base_xml_formatter_last_group_add_child(GESBaseXmlFormatter *self,
                                                                 const gchar * id,
                                                                 const gchar * name);

G_GNUC_INTERNAL void ges_base_xml_formatter_add_control_binding (GESBaseXmlFormatter * self,
                                                                  const gchar * binding_type,
                                                                  const gchar * source_type,
                                                                  const gchar * property_name,
                                                                  gint mode,
                                                                  const gchar *track_id,
                                                                  GSList * timed_values);

G_GNUC_INTERNAL void ges_base_xml_formatter_set_timeline_properties(GESBaseXmlFormatter * self,
                                                                    GESTimeline *timeline,
                                                                    const gchar *properties,
                                                                    const gchar *metadatas);

G_GNUC_INTERNAL void ges_base_xml_formatter_end_current_clip       (GESBaseXmlFormatter *self);

G_GNUC_INTERNAL void ges_xml_formatter_deinit                      (void);

G_GNUC_INTERNAL gboolean set_property_foreach                   (GQuark field_id,
                                                                 const GValue * value,
                                                                 GObject * object);

G_GNUC_INTERNAL GstElement * get_element_for_encoding_profile   (GstEncodingProfile *prof,
                                                                 GstElementFactoryListType type);

/* Function to initialise GES */
G_GNUC_INTERNAL void _init_standard_transition_assets        (void);
G_GNUC_INTERNAL void _init_formatter_assets                  (void);
G_GNUC_INTERNAL void _deinit_formatter_assets                (void);

/* Utilities */
G_GNUC_INTERNAL gint element_start_compare                (GESTimelineElement * a,
                                                           GESTimelineElement * b);
G_GNUC_INTERNAL gint element_end_compare                  (GESTimelineElement * a,
                                                           GESTimelineElement * b);
G_GNUC_INTERNAL GstElementFactory *
ges_get_compositor_factory                                (void);

G_GNUC_INTERNAL void
ges_idle_add (GSourceFunc func, gpointer udata, GDestroyNotify notify);

G_GNUC_INTERNAL gboolean
ges_util_structure_get_clocktime (GstStructure *structure, const gchar *name,
                                  GstClockTime *val, GESFrameNumber *frames);

G_GNUC_INTERNAL gboolean /* From ges-xml-formatter.c */
ges_util_can_serialize_spec (GParamSpec * spec);

/****************************************************
 *              GESContainer                        *
 ****************************************************/
G_GNUC_INTERNAL void _ges_container_sort_children         (GESContainer *container);
G_GNUC_INTERNAL void _ges_container_set_height            (GESContainer * container,
                                                           guint32 height);

/****************************************************
 *                  GESClip                         *
 ****************************************************/
G_GNUC_INTERNAL void              ges_clip_set_layer              (GESClip *clip, GESLayer  *layer);
G_GNUC_INTERNAL gboolean          ges_clip_is_moving_from_layer   (GESClip *clip);
G_GNUC_INTERNAL void              ges_clip_set_moving_from_layer  (GESClip *clip, gboolean is_moving);
G_GNUC_INTERNAL GESTrackElement*  ges_clip_create_track_element   (GESClip *clip, GESTrackType type);
G_GNUC_INTERNAL GList*            ges_clip_create_track_elements  (GESClip *clip, GESTrackType type);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_inpoint_of_child (GESClip * clip, GESTrackElement * child, GstClockTime inpoint, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_max_duration_of_all_core (GESClip * clip, GstClockTime max_duration, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_max_duration_of_child (GESClip * clip, GESTrackElement * child, GstClockTime max_duration, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_active_of_child (GESClip * clip, GESTrackElement * child, gboolean active, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_priority_of_child (GESClip * clip, GESTrackElement * child, guint32 priority, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_track_of_child (GESClip * clip, GESTrackElement * child, GESTrack * tack, GError ** error);
G_GNUC_INTERNAL gboolean          ges_clip_can_set_time_property_of_child (GESClip * clip, GESTrackElement * child, GObject * prop_object, GParamSpec * pspec, const GValue * value, GError ** error);
G_GNUC_INTERNAL GstClockTime      ges_clip_duration_limit_with_new_children_inpoints (GESClip * clip, GHashTable * child_inpoints);
G_GNUC_INTERNAL GstClockTime      ges_clip_get_core_internal_time_from_timeline_time (GESClip * clip, GstClockTime timeline_time, gboolean * no_core, GError ** error);
G_GNUC_INTERNAL void              ges_clip_empty_from_track       (GESClip * clip, GESTrack * track);
G_GNUC_INTERNAL void              ges_clip_set_add_error          (GESClip * clip, GError * error);
G_GNUC_INTERNAL void              ges_clip_take_add_error         (GESClip * clip, GError ** error);
G_GNUC_INTERNAL void              ges_clip_set_remove_error       (GESClip * clip, GError * error);
G_GNUC_INTERNAL void              ges_clip_take_remove_error      (GESClip * clip, GError ** error);

/****************************************************
 *              GESLayer                            *
 ****************************************************/
G_GNUC_INTERNAL gboolean ges_layer_resync_priorities (GESLayer * layer);
G_GNUC_INTERNAL void layer_set_priority               (GESLayer * layer, guint priority, gboolean emit);

/****************************************************
 *              GESTrackElement                     *
 ****************************************************/
#define         NLE_OBJECT_TRACK_ELEMENT_QUARK                  (g_quark_from_string ("nle_object_track_element_quark"))
G_GNUC_INTERNAL gboolean  ges_track_element_set_track           (GESTrackElement * object, GESTrack * track, GError ** error);
G_GNUC_INTERNAL void ges_track_element_copy_properties          (GESTimelineElement * element,
                                                                 GESTimelineElement * elementcopy);
G_GNUC_INTERNAL void ges_track_element_set_layer_active         (GESTrackElement *element, gboolean active);

G_GNUC_INTERNAL void ges_track_element_copy_bindings (GESTrackElement *element,
                                                      GESTrackElement *new_element,
                                                      guint64 position);
G_GNUC_INTERNAL void ges_track_element_freeze_control_sources   (GESTrackElement * object,
                                                                 gboolean freeze);
G_GNUC_INTERNAL void ges_track_element_update_outpoint          (GESTrackElement * self);

G_GNUC_INTERNAL void
ges_track_element_set_creator_asset                    (GESTrackElement * self,
                                                       GESAsset *creator_asset);
G_GNUC_INTERNAL GESAsset *
ges_track_element_get_creator_asset                    (GESTrackElement * self);

G_GNUC_INTERNAL void
ges_track_element_set_has_internal_source_is_forbidden (GESTrackElement * element);

G_GNUC_INTERNAL GstElement* ges_source_create_topbin  (GESSource *source,
                                                       const gchar* bin_name,
                                                       GstElement* sub_element,
                                                       GPtrArray* elements);
G_GNUC_INTERNAL void ges_source_set_rendering_smartly (GESSource *source,
                                                       gboolean rendering_smartly);
G_GNUC_INTERNAL gboolean
ges_source_get_rendering_smartly                      (GESSource *source);

G_GNUC_INTERNAL void ges_track_set_smart_rendering     (GESTrack* track, gboolean rendering_smartly);
G_GNUC_INTERNAL GstElement * ges_track_get_composition (GESTrack *track);
G_GNUC_INTERNAL void ges_track_select_subtimeline_streams (GESTrack *track, GstStreamCollection *collection, GstElement *subtimeline);


/*********************************************
 *  GESTrackElement subclasses constructors  *
 ********************************************/
G_GNUC_INTERNAL GESAudioTestSource * ges_audio_test_source_new (void);
G_GNUC_INTERNAL GESAudioUriSource  * ges_audio_uri_source_new  (gchar *uri);
G_GNUC_INTERNAL GESVideoUriSource  * ges_video_uri_source_new  (gchar *uri);
G_GNUC_INTERNAL GESImageSource     * ges_image_source_new      (gchar *uri);
G_GNUC_INTERNAL GESTitleSource     * ges_title_source_new      (void);
G_GNUC_INTERNAL GESVideoTestSource * ges_video_test_source_new (void);

/****************************************************
 *                GES*Effect                     *
 ****************************************************/
G_GNUC_INTERNAL gchar *
ges_base_effect_get_time_property_name        (GESBaseEffect * effect,
                                               GObject * child,
                                               GParamSpec * pspec);
G_GNUC_INTERNAL GHashTable *
ges_base_effect_get_time_property_values      (GESBaseEffect * effect);
G_GNUC_INTERNAL GstClockTime
ges_base_effect_translate_source_to_sink_time (GESBaseEffect * effect,
                                               GstClockTime time,
                                               GHashTable * time_property_values);
G_GNUC_INTERNAL GstClockTime
ges_base_effect_translate_sink_to_source_time (GESBaseEffect * effect,
                                               GstClockTime time,
                                               GHashTable * time_property_values);
G_GNUC_INTERNAL GstElement *
ges_effect_from_description                   (const gchar *bin_desc,
                                               GESTrackType type,
                                               GError **error);

/****************************************************
 *              GESTimelineElement                  *
 ****************************************************/
typedef enum
{
  GES_CLIP_IS_MOVING = (1 << 0),
  GES_TIMELINE_ELEMENT_SET_SIMPLE = (1 << 1),
} GESTimelineElementFlags;

G_GNUC_INTERNAL GESTimelineElement * ges_timeline_element_peak_toplevel (GESTimelineElement * self);
G_GNUC_INTERNAL GESTimelineElement * ges_timeline_element_get_copied_from (GESTimelineElement *self);
G_GNUC_INTERNAL GESTimelineElementFlags ges_timeline_element_flags (GESTimelineElement *self);
G_GNUC_INTERNAL void                ges_timeline_element_set_flags (GESTimelineElement *self, GESTimelineElementFlags flags);
G_GNUC_INTERNAL gboolean            ges_timeline_element_add_child_property_full (GESTimelineElement *self,
                                                                                  GESTimelineElement *owner,
                                                                                  GParamSpec *pspec,
                                                                                  GObject *child);

G_GNUC_INTERNAL GObject *           ges_timeline_element_get_child_from_child_property (GESTimelineElement * self,
                                                                                        GParamSpec * pspec);
G_GNUC_INTERNAL GParamSpec **       ges_timeline_element_get_children_properties (GESTimelineElement * self,
                                                                                  guint * n_properties);

#define ELEMENT_FLAGS(obj)             (ges_timeline_element_flags (GES_TIMELINE_ELEMENT(obj)))
#define ELEMENT_SET_FLAG(obj,flag)     (ges_timeline_element_set_flags(GES_TIMELINE_ELEMENT(obj), (ELEMENT_FLAGS(obj) | (flag))))
#define ELEMENT_UNSET_FLAG(obj,flag)   (ges_timeline_element_set_flags(GES_TIMELINE_ELEMENT(obj), (ELEMENT_FLAGS(obj) & ~(flag))))
#define ELEMENT_FLAG_IS_SET(obj,flag)  ((ELEMENT_FLAGS (obj) & (flag)) == (flag))

/******************************
 *  GESMultiFile internal API *
 ******************************/
typedef struct GESMultiFileURI
{
  gchar *location;
  gint start;
  gint end;
} GESMultiFileURI;

G_GNUC_INTERNAL GESMultiFileURI * ges_multi_file_uri_new (const gchar * uri);

/******************************
 *  GESUriSource internal API *
 ******************************/
G_GNUC_INTERNAL gboolean
ges_video_uri_source_get_natural_size(GESVideoSource* source, gint* width, gint* height);

/**********************************
 *  GESTestClipAsset internal API *
 **********************************/
G_GNUC_INTERNAL gboolean ges_test_clip_asset_get_natural_size (GESAsset *self,
                                                               gint *width,
                                                               gint *height);
G_GNUC_INTERNAL gchar *ges_test_source_asset_check_id         (GType type, const gchar *id,
                                                               GError **error);

/*******************************
 *        GESMarkerList        *
 *******************************/

G_GNUC_INTERNAL GESMarker * ges_marker_list_get_closest (GESMarkerList *list, GstClockTime position);
G_GNUC_INTERNAL gchar * ges_marker_list_serialize (const GValue * v);
G_GNUC_INTERNAL gboolean ges_marker_list_deserialize (GValue *dest, const gchar *s);

/*******************************
 *       GESDiscovererManager   *
 *******************************/
G_GNUC_INTERNAL void ges_discoverer_manager_cleanup                  (void);
G_GNUC_INTERNAL gboolean ges_discoverer_manager_start_discovery      (GESDiscovererManager *self,
                                                                      const gchar *uri);
G_GNUC_INTERNAL void ges_discoverer_manager_recreate_discoverer      (GESDiscovererManager *self);

/********************
 *  Gnonlin helpers *
 ********************/

G_GNUC_INTERNAL gboolean ges_nle_composition_add_object (GstElement *comp, GstElement *object);
G_GNUC_INTERNAL gboolean ges_nle_composition_remove_object (GstElement *comp, GstElement *object);
G_GNUC_INTERNAL gboolean ges_nle_object_commit (GstElement * nlesource, gboolean recurse);

G_END_DECLS
