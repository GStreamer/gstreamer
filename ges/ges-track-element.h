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

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-clip.h>
#include <ges/ges-track.h>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_ELEMENT ges_track_element_get_type()
GES_DECLARE_TYPE(TrackElement, track_element, TRACK_ELEMENT)

/**
 * GES_TRACK_ELEMENT_CLASS_DEFAULT_HAS_INTERNAL_SOURCE:
 * @klass: A #GESTrackElementClass
 *
 * What the default #GESTrackElement:has-internal-source value should be
 * for new elements from this class.
 */
#define GES_TRACK_ELEMENT_CLASS_DEFAULT_HAS_INTERNAL_SOURCE(klass) \
  ((GES_TRACK_ELEMENT_CLASS (klass))->ABI.abi.default_has_internal_source)

/**
 * GESTrackElement:
 *
 * The #GESTrackElement base class.
 */
struct _GESTrackElement {
  GESTimelineElement parent;

  /*< private >*/
  gboolean active;

  GESTrackElementPrivate *priv;

  GESAsset *asset;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESTrackElementClass:
 */
struct _GESTrackElementClass {
  /*< private >*/
  GESTimelineElementClass parent_class;

  /*< public >*/
  /**
   * GESTrackElementClass::nleobject_factorytype:
   *
   * The name of the #GstElementFactory to use to create the underlying
   *   nleobject of a track element
   */
  const gchar  *nleobject_factorytype;

  /**
   * GESTrackElementClass::create_gnl_object:
   * @object: The #GESTrackElement
   *
   * Returns: (transfer floating): the #NLEObject to use in the #nlecomposition
   */
  GstElement*  (*create_gnl_object)        (GESTrackElement * object);

  /**
   * GESTrackElementClass::create_element:
   * @object: The #GESTrackElement
   *
   * Returns: (transfer floating): the #GstElement that the underlying nleobject
   * controls.
   */
  GstElement*  (*create_element)           (GESTrackElement * object);

  /**
   * GESTrackElementClass::active_changed:
   * @object: A #GESTrackElement
   * @active: Whether the element is active or not inside the #nlecomposition
   *
   * Notify when the #GESTrackElement:active property changes
   */
  void (*active_changed)       (GESTrackElement *object, gboolean active);

  /*< private >*/
  /* signals (currently unused) */
  /**
   * GESTrackElementClass::changed:
   *
   * Deprecated:
   */
  void  (*changed)  (GESTrackElement * object);

  /**
   * GESTrackElementClass::list_children_properties:
   *
   * Listing children properties is handled by
   * ges_timeline_element_list_children_properties() instead.
   *
   * Deprecated: 1.14: Use #GESTimelineElementClass::list_children_properties
   * instead
   */
  GParamSpec** (*list_children_properties) (GESTrackElement * object,
              guint *n_properties);

  /**
   * GESTrackElementClass::lookup_child:
   *
   * Deprecated: 1.14: Use #GESTimelineElementClass::lookup_child
   * instead
   */
  gboolean (*lookup_child)                 (GESTrackElement *object,
                                            const gchar *prop_name,
                                            GstElement **element,
                                            GParamSpec **pspec);
  /*< protected >*/
  union {
    gpointer _ges_reserved[GES_PADDING_LARGE];
    struct {
      gboolean default_has_internal_source;
      GESTrackType default_track_type;
    } abi;
  } ABI;
};

GES_API
GESTrack* ges_track_element_get_track          (GESTrackElement * object);

GES_API
GESTrackType ges_track_element_get_track_type  (GESTrackElement * object);
GES_API
void ges_track_element_set_track_type          (GESTrackElement * object,
                                               GESTrackType     type);

GES_API
GstElement * ges_track_element_get_nleobject   (GESTrackElement * object);

GES_API
GstElement * ges_track_element_get_element     (GESTrackElement * object);

GES_API
gboolean ges_track_element_is_core             (GESTrackElement * object);

GES_API
gboolean ges_track_element_set_active          (GESTrackElement * object,
                                               gboolean active);

GES_API
gboolean ges_track_element_is_active           (GESTrackElement * object);

GES_API gboolean
ges_track_element_set_has_internal_source      (GESTrackElement * object,
                                               gboolean has_internal_source);

GES_API
gboolean ges_track_element_has_internal_source (GESTrackElement * object);

GES_API void
ges_track_element_get_child_property_by_pspec (GESTrackElement * object,
                                              GParamSpec * pspec,
                                              GValue * value);

GES_API gboolean
ges_track_element_set_control_source          (GESTrackElement *object,
                                               GstControlSource *source,
                                               const gchar *property_name,
                                               const gchar *binding_type);

GES_API void
ges_track_element_clamp_control_source        (GESTrackElement * object,
                                               const gchar * property_name);

GES_API void
ges_track_element_set_auto_clamp_control_sources (GESTrackElement * object,
                                                  gboolean auto_clamp);
GES_API gboolean
ges_track_element_get_auto_clamp_control_sources (GESTrackElement * object);

GES_API GstControlBinding *
ges_track_element_get_control_binding         (GESTrackElement *object,
                                               const gchar *property_name);
GES_API void
ges_track_element_add_children_props          (GESTrackElement *self,
                                               GstElement *element,
                                               const gchar ** wanted_categories,
                                               const gchar **blacklist,
                                               const gchar **whitelist);
GES_API GHashTable *
ges_track_element_get_all_control_bindings    (GESTrackElement * trackelement);
GES_API gboolean
ges_track_element_remove_control_binding      (GESTrackElement * object,
                                               const gchar * property_name);

#include "ges-track-element-deprecated.h"

G_END_DECLS
