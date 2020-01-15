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

#ifndef _GES_TRACK_ELEMENT
#define _GES_TRACK_ELEMENT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-clip.h>
#include <ges/ges-track.h>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_ELEMENT ges_track_element_get_type()

#define GES_TRACK_ELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_ELEMENT, GESTrackElement))

#define GES_TRACK_ELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_ELEMENT, GESTrackElementClass))

#define GES_IS_TRACK_ELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_ELEMENT))

#define GES_IS_TRACK_ELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_ELEMENT))

#define GES_TRACK_ELEMENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_ELEMENT, GESTrackElementClass))

typedef struct _GESTrackElementPrivate GESTrackElementPrivate;

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
 * @nleobject_factorytype: The name of the #GstElementFactory to use to
 * create the underlying nleobject of a track element
 * @create_gnl_object: Method to create the underlying nleobject of the
 * track element. The default implementation will use the factory given by
 * @nleobject_factorytype to created the nleobject and will give it
 * the #GstElement returned by @create_element.
 * @create_element: Method to create the #GstElement that the underlying
 * nleobject controls.
 * @active_changed: Method to be called when the #GESTrackElement:active
 * property changes.
 * @list_children_properties: Deprecated: Listing children properties is
 * handled by ges_timeline_element_list_children_properties() instead.
 * @lookup_child: Deprecated: Use #GESTimelineElement.lookup_child()
 * instead.
 */
struct _GESTrackElementClass {
  /*< private >*/
  GESTimelineElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */
  const gchar  *nleobject_factorytype;
  GstElement*  (*create_gnl_object)        (GESTrackElement * object);
  GstElement*  (*create_element)           (GESTrackElement * object);

  void (*active_changed)       (GESTrackElement *object, gboolean active);

  /*< private >*/
  /* signals (currently unused) */
  void  (*changed)  (GESTrackElement * object);

  /*< public >*/
  /* virtual methods for subclasses */
  GParamSpec** (*list_children_properties) (GESTrackElement * object,
              guint *n_properties);
  gboolean (*lookup_child)                 (GESTrackElement *object,
                                            const gchar *prop_name,
                                            GstElement **element,
                                            GParamSpec **pspec);
  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

GES_API
GType ges_track_element_get_type               (void);

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
gboolean ges_track_element_set_active          (GESTrackElement * object,
                                               gboolean active);

GES_API
gboolean ges_track_element_is_active           (GESTrackElement * object);

GES_API void
ges_track_element_get_child_property_by_pspec (GESTrackElement * object,
                                              GParamSpec * pspec,
                                              GValue * value);

GES_API gboolean
ges_track_element_set_control_source          (GESTrackElement *object,
                                               GstControlSource *source,
                                               const gchar *property_name,
                                               const gchar *binding_type);

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
#endif /* _GES_TRACK_ELEMENT */
