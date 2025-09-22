/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalyticsmeta.h
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef __GST_ANALYTICS_META_H__
#define __GST_ANALYTICS_META_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS
/**
 * GST_INF_RELATION_SPAN:
 *
 * Passes to functions asking for a relation span when the span is
 * infinite.
 *
 * Since: 1.24
 */
#define GST_INF_RELATION_SPAN -1
typedef struct _GstAnalyticsMtd GstAnalyticsMtd;

/**
 * GstAnalyticsMtdType:
 *
 * Type of analytics meta data
 *
 * Since: 1.24
 */
typedef guintptr GstAnalyticsMtdType;

/**
 * GST_ANALYTICS_MTD_TYPE_ANY:
 *
 * A wildcard matching any type of analysis
 *
 * Since: 1.24
 */

#define GST_ANALYTICS_MTD_TYPE_ANY (0)

/**
 * GST_ANALYTICS_MTD_CAST: (skip)
 *
 * Since: 1.24
 */
#define GST_ANALYTICS_MTD_CAST(mtd) \
    ((GstAnalyticsMtd *)(mtd))

/**
 * GstAnalyticsRelationMeta:
 *
 * An opaque #GstMeta that can be used to hold various types of results
 * from analysis processes.
 *
 * The content should be accessed through the API.
 *
 * {{ PY.md }}
 *
 * #### GstAnalytics.RelationMeta.\__iter__
 *
 * ``` python
 * def __iter__(self) -> Iterator:
 * ```
 *
 * Iterate over all #GstAnalyticsMtd
 *
 * #### Example:
 *
 * ``` python
 * for mtd in relation_meta:
 *     # Process each GstAnalyticsMtd in the relation meta
 *     print(f"Found metadata with id: {mtd.id}")
 * ```
 *
 * #### GstAnalytics.RelationMeta.iter_on_type
 *
 * ``` python
 * def iter_on_type(self, filter: Type[GstAnalyticsMtd]) -> Iterator:
 * ```
 *
 * Iterate over #GstAnalyticsMtd of a specific type
 *
 * #### Example:
 *
 * ``` python
 * # Iterate only over ObjectDetectionMtd metadata
 * for mtd in relation_meta.iter_on_type(GstAnalytics.ODMtd):
 *     # Process only object detection metadata
 *     bbox = mtd.get_location()
 *     print(f"Object at x={bbox.x}, y={bbox.y}")
 * ```
 *
 * {{ END_LANG.md }}
 *
 *
 * Since: 1.24
 */

typedef struct _GstAnalyticsRelationMeta GstAnalyticsRelationMeta;

/**
 * GstAnalyticsMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analysis-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_mtd API. This type
 * is generally expected to be allocated on the stack.
 *
 * {{ PY.md }}
 *
 * ### Python specific methods:
 *
 * #### GstAnalytics.Mtd.iter_direct_related
 *
 * ``` python
 * def GstAnalytics.Mtd.iter_direct_related(self, relation_type) -> Iterator[GstAnalytics.Mtd]:
 * ```
 *
 * ##### Parameters:
 *
 * - `relation_type` : A `Gst.Analytics.RelTypes` value specifying the type of
 *   relation to follow.
 *
 * **Returns**: (Iterator over #GstAnalyticsMtd): An iterator object that can be over
 * metadata items.
 *
 * **Since**: 1.28
 *
 * #### GstAnalytics.Mtd.relation_path
 *
 * ``` python
 * def GstAnalytics.Mtd.relation_path(self, mtd, max_span = 0, reltype = GstAnalytics.RelTypes.ANY) -> List[int]:
 * ```
 *
 * ##### Parameters:
 *
 * - `mtd` : A #GstAnalyticsMtd instance to find a relation path to.
 * - `max_span` : (optional) Maximum relation span to search. Default 0.
 * - `reltype` : (optional) A `Gst.Analytics.RelTypes` value specifying the type
 *   of relation to follow. Default `Gst.Analytics.RelTypes.ANY`.
 *
 * **Returns**: ( #gint ): A list of #GstAnalyticsMtd.id-s representing the path
 *
 * **Since**: 1.28
 *
 * {{ END_LANG.md }}
 *
 * Since: 1.24
 */
struct _GstAnalyticsMtd
{
  guint id;
  GstAnalyticsRelationMeta *meta;
};

/**
 * GstAnalyticsMtdImpl:
 * @name: The name of the metadata type
 * @mtd_meta_transform: A pointer to a function that will be called
 * when the containing meta is transform to potentially copy the data
 * into a new Mtd into the new meta.
 * @mtd_meta_clear: A pointer to a function that will be called when the
 * containing meta is cleared to potetially do cleanup (ex. _unref or release)
 * resources it was using.
 *
 * This structure must be provided when registering a new type of Mtd. It must
 * have a static lifetime (never be freed).
 *
 * Since: 1.24
 */

typedef struct
{
  const char *name;

gboolean (*mtd_meta_transform) (GstBuffer * transbuf,
      GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type,
      gpointer data);

  void (*mtd_meta_clear) (GstBuffer *buffer, GstAnalyticsMtd *mtd);

  /*< private >*/
  gpointer _reserved[GST_PADDING_LARGE - 1];
} GstAnalyticsMtdImpl;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_mtd_get_mtd_type (const GstAnalyticsMtd *
    instance);

GST_ANALYTICS_META_API
guint gst_analytics_mtd_get_id (const GstAnalyticsMtd * instance);

GST_ANALYTICS_META_API
gsize gst_analytics_mtd_get_size (const GstAnalyticsMtd * instance);

GST_ANALYTICS_META_API
const gchar *gst_analytics_mtd_type_get_name (GstAnalyticsMtdType type);

typedef struct _GstAnalyticsRelationMetaInitParams
GstAnalyticsRelationMetaInitParams;

/**
 * GST_ANALYTICS_RELATION_META_API_TYPE:
 *
 * The Analyics Relation Meta API type
 *
 * Since: 1.24
 */
#define GST_ANALYTICS_RELATION_META_API_TYPE \
  (gst_analytics_relation_meta_api_get_type())

/**
 * GST_ANALYTICS_RELATION_META_INFO: (skip)
 *
 * Get the meta info
 *
 * Since: 1.24
 */
#define GST_ANALYTICS_RELATION_META_INFO \
  (gst_analytics_relation_meta_get_info())

/**
 * GstAnalyticsRelTypes:
 * @GST_ANALYTICS_REL_TYPE_NONE: No relation
 * @GST_ANALYTICS_REL_TYPE_IS_PART_OF: First analysis-meta is part of second analysis-meta
 * @GST_ANALYTICS_REL_TYPE_CONTAIN: First analysis-meta contain second analysis-meta.
 * @GST_ANALYTICS_REL_TYPE_RELATE_TO: First analysis-meta relate to second analysis-meta.
 * @GST_ANALYTICS_REL_TYPE_N_TO_N: Used to express relations between two groups
 *    where each group's components correspond to the respective component in the
 *    other group. Since: 1.26
 * @GST_ANALYTICS_REL_TYPE_ANY: Only use for criteria.
 *
 * Since: 1.24
 */
typedef enum
{
  GST_ANALYTICS_REL_TYPE_NONE = 0,
  GST_ANALYTICS_REL_TYPE_IS_PART_OF = (1 << 1),
  GST_ANALYTICS_REL_TYPE_CONTAIN = (1 << 2),
  GST_ANALYTICS_REL_TYPE_RELATE_TO = (1 << 3),
  /**
   * GST_ANALYTICS_REL_TYPE_N_TO_N:
   *
   * Used to express relations between two groups where each group's components
   * correspond to the respective component in the other group.
   *
   * Since: 1.26
   */
  GST_ANALYTICS_REL_TYPE_N_TO_N = (1 << 4),
  GST_ANALYTICS_REL_TYPE_ANY = G_MAXINT
} GstAnalyticsRelTypes;


/**
 * GstAnalyticsRelationMetaInitParams:
 * @initial_relation_order: Initial relations order.
 * @initial_buf_size: Buffer size in bytes to store relatable metadata
 *
 * GstAnalyticsRelationMeta initialization parameters.
 *
 * Since: 1.24
 */
struct _GstAnalyticsRelationMetaInitParams
{
  gsize initial_relation_order;
  gsize initial_buf_size;
};

GST_ANALYTICS_META_API GType gst_analytics_relation_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo *gst_analytics_relation_meta_get_info (void);

GST_ANALYTICS_META_API
gsize
gst_analytics_relation_get_length (const GstAnalyticsRelationMeta * instance);

GST_ANALYTICS_META_API
GstAnalyticsRelTypes
gst_analytics_relation_meta_get_relation (const GstAnalyticsRelationMeta * meta,
guint an_meta_first_id, guint an_meta_second_id);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_set_relation (GstAnalyticsRelationMeta
    * meta, GstAnalyticsRelTypes type, guint an_meta_first_id,
    guint an_meta_second_id);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_exist (const GstAnalyticsRelationMeta *
    rmeta, guint an_meta_first_id, guint an_meta_second_id,
    gint max_relation_span, GstAnalyticsRelTypes cond_types,
    GArray ** relations_path);

GST_ANALYTICS_META_API
GstAnalyticsRelationMeta *
gst_buffer_add_analytics_relation_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
GstAnalyticsRelationMeta *
gst_buffer_add_analytics_relation_meta_full (GstBuffer * buffer,
    GstAnalyticsRelationMetaInitParams * init_params);

GST_ANALYTICS_META_API
GstAnalyticsRelationMeta *
gst_buffer_get_analytics_relation_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
gpointer
gst_analytics_relation_meta_add_mtd (GstAnalyticsRelationMeta * meta,
    const GstAnalyticsMtdImpl * impl, gsize size, GstAnalyticsMtd * rlt_mtd);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_get_mtd (GstAnalyticsRelationMeta *
    meta, guint an_meta_id, GstAnalyticsMtdType type, GstAnalyticsMtd * rlt);

GST_ANALYTICS_META_API
gpointer
gst_analytics_relation_meta_get_mtd_data (const GstAnalyticsRelationMeta * meta,
    guint an_meta_id);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_iterate (GstAnalyticsRelationMeta *
    meta, gpointer * state, GstAnalyticsMtdType type,
GstAnalyticsMtd * rlt_mtd);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_get_direct_related (GstAnalyticsRelationMeta *
    meta, guint an_meta_id, GstAnalyticsRelTypes relation_type,
    GstAnalyticsMtdType type, gpointer * state, GstAnalyticsMtd * rlt_mtd);

G_END_DECLS
#endif // __GST_ANALYTICS_META_H__
