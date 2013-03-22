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
 * The GESTrackElement base class.
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
 * @gnlobject_factorytype: name of the GNonLin GStElementFactory type to use.
 * @create_gnl_object: method to create the GNonLin container object.
 * @create_element: method to return the GstElement to put in the gnlobject.
 * @duration_changed: duration property glnobject has changed
 * @active_changed: active property of gnlobject has changed
 * @get_props_hastable: method to list children properties that user could like
 *                      to configure. Since: 0.10.2
 * @list_children_properties: method to get children properties that user could
 *                            like to configure.
 *                            The default implementation will create an object
 *                            of type @gnlobject_factorytype and call
 *                            @create_element. Since: 0.10.2
 *
 * Subclasses can override the @create_gnl_object method to override what type
 * of GNonLin object will be created.
 */
struct _GESTrackElementClass {
  /*< private >*/
  GESTimelineElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */
  const gchar  *gnlobject_factorytype;
  GstElement*  (*create_gnl_object)        (GESTrackElement * object);
  GstElement*  (*create_element)           (GESTrackElement * object);

  void (*duration_changed)     (GESTrackElement *object, guint64 duration);
  void (*active_changed)       (GESTrackElement *object, gboolean active);

  /*< private >*/
  /* signals (currently unused) */
  void  (*changed)  (GESTrackElement * object);

  /*< public >*/
  /* virtual methods for subclasses */
  GHashTable*  (*get_props_hastable)       (GESTrackElement * object);
  GParamSpec** (*list_children_properties) (GESTrackElement * object,
              guint *n_properties);
  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

GType ges_track_element_get_type               (void);

GESTrack* ges_track_element_get_track          (GESTrackElement * object);

GESTrackType ges_track_element_get_track_type  (GESTrackElement * object);
void ges_track_element_set_track_type          (GESTrackElement * object,
                                               GESTrackType     type);

GstElement * ges_track_element_get_gnlobject   (GESTrackElement * object);

GstElement * ges_track_element_get_element     (GESTrackElement * object);

gboolean ges_track_element_set_active          (GESTrackElement * object,
                                               gboolean active);

gboolean ges_track_element_is_active           (GESTrackElement * object);

GParamSpec **
ges_track_element_list_children_properties     (GESTrackElement *object,
                                               guint *n_properties);

gboolean ges_track_element_lookup_child        (GESTrackElement *object,
                                               const gchar *prop_name,
                                               GstElement **element,
                                               GParamSpec **pspec);

void
ges_track_element_get_child_property_by_pspec (GESTrackElement * object,
                                              GParamSpec * pspec,
                                              GValue * value);

void
ges_track_element_get_child_property_valist   (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

void ges_track_element_get_child_properties   (GESTrackElement *object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;

void
ges_track_element_set_child_property_valist   (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

void
ges_track_element_set_child_property_by_pspec (GESTrackElement * object,
                                              GParamSpec * pspec,
                                              GValue * value);

void ges_track_element_set_child_properties   (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;

gboolean ges_track_element_set_child_property (GESTrackElement *object,
                                              const gchar *property_name,
                                              GValue * value);

gboolean ges_track_element_get_child_property (GESTrackElement *object,
                                              const gchar *property_name,
                                              GValue * value);

gboolean
ges_track_element_edit                        (GESTrackElement * object,
                                              GList *layers, GESEditMode mode,
                                              GESEdge edge, guint64 position);

gboolean
ges_track_element_set_control_source          (GESTrackElement *object,
                                               GstControlSource *source,
                                               const gchar *property_name,
                                               const gchar *binding_type);

GstControlBinding *
ges_track_element_get_control_binding         (GESTrackElement *object,
                                               const gchar *property_name);

G_END_DECLS
#endif /* _GES_TRACK_ELEMENT */
