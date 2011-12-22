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

#ifndef _GES_TRACK_OBJECT
#define _GES_TRACK_OBJECT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-object.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_OBJECT ges_track_object_get_type()

#define GES_TRACK_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_OBJECT, GESTrackObject))

#define GES_TRACK_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_OBJECT, GESTrackObjectClass))

#define GES_IS_TRACK_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_OBJECT))

#define GES_IS_TRACK_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_OBJECT))

#define GES_TRACK_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_OBJECT, GESTrackObjectClass))

/**
 * GES_TRACK_OBJECT_START:
 * @obj: a #GESTrackObject
 *
 * The start position of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_START(obj) (((GESTrackObject*)obj)->start)

/**
 * GES_TRACK_OBJECT_INPOINT:
 * @obj: a #GESTrackObject
 *
 * The in-point of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_INPOINT(obj) (((GESTrackObject*)obj)->inpoint)

/**
 * GES_TRACK_OBJECT_DURATION:
 * @obj: a #GESTrackObject
 *
 * The duration position of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_DURATION(obj) (((GESTrackObject*)obj)->duration)

typedef struct _GESTrackObjectPrivate GESTrackObjectPrivate;

/**
 * GESTrackObject:
 *
 * The GESTrackObject base class.
 */
struct _GESTrackObject {
  GInitiallyUnowned parent;

  /*< private >*/
  guint64 start;
  guint64 inpoint;
  guint64 duration;
  guint32 priority;
  gboolean active;

  GESTrackObjectPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTrackObjectClass:
 * @gnlobject_factorytype: name of the GNonLin GStElementFactory type to use.
 * @create_gnl_object: method to create the GNonLin container object.
 * @create_element: method to return the GstElement to put in the gnlobject.
 * @start_changed: start property of gnlobject has changed
 * @media_start_changed: media-start property of gnlobject has changed
 * @duration_changed: duration property glnobject has changed
 * @gnl_priority_changed: duration property glnobject has changed
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
struct _GESTrackObjectClass {
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */
  const gchar  *gnlobject_factorytype;
  GstElement*  (*create_gnl_object)        (GESTrackObject * object);
  GstElement*  (*create_element)           (GESTrackObject * object);

  void (*start_changed)        (GESTrackObject *object, guint64 start);
  void (*media_start_changed)  (GESTrackObject *object, guint64 media_start);
  void (*gnl_priority_changed) (GESTrackObject *object, guint priority);
  void (*duration_changed)     (GESTrackObject *object, guint64 duration);
  void (*active_changed)       (GESTrackObject *object, gboolean active);

  /*< private >*/
  /* signals (currently unused) */
  void  (*changed)  (GESTrackObject * object);

  /*< public >*/
  /* virtual methods for subclasses */
  GHashTable*  (*get_props_hastable)       (GESTrackObject * object);
  GParamSpec** (*list_children_properties) (GESTrackObject * object,
              guint *n_properties);
  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING - 2];
};

GType ges_track_object_get_type               (void);

gboolean ges_track_object_set_track           (GESTrackObject * object,
              GESTrack * track);
GESTrack* ges_track_object_get_track          (GESTrackObject * object);

void ges_track_object_set_timeline_object     (GESTrackObject * object,
                                               GESTimelineObject * tlobject);
GESTimelineObject *
ges_track_object_get_timeline_object          (GESTrackObject* object);

GstElement * ges_track_object_get_gnlobject   (GESTrackObject * object);

GstElement * ges_track_object_get_element     (GESTrackObject * object);

void ges_track_object_set_locked              (GESTrackObject * object,
                                               gboolean locked);

gboolean ges_track_object_is_locked           (GESTrackObject * object);

void ges_track_object_set_start               (GESTrackObject * object,
                                               guint64 start);

void ges_track_object_set_inpoint             (GESTrackObject * object,
                                               guint64 inpoint);

void ges_track_object_set_duration            (GESTrackObject * object,
                                               guint64 duration);

void ges_track_object_set_priority            (GESTrackObject * object,
                                               guint32 priority);

gboolean ges_track_object_set_active          (GESTrackObject * object,
                                               gboolean active);

guint64 ges_track_object_get_start            (GESTrackObject * object);
guint64 ges_track_object_get_inpoint          (GESTrackObject * object);
guint64 ges_track_object_get_duration         (GESTrackObject * object);
guint32 ges_track_object_get_priority         (GESTrackObject * object);
gboolean ges_track_object_is_active           (GESTrackObject * object);

GParamSpec **
ges_track_object_list_children_properties     (GESTrackObject *object,
                                               guint *n_properties);

gboolean ges_track_object_lookup_child        (GESTrackObject *object,
                                               const gchar *prop_name,
                                               GstElement **element,
                                               GParamSpec **pspec);

void
ges_track_object_get_child_property_by_pspec (GESTrackObject * object,
                                              GParamSpec * pspec,
                                              GValue * value);

void
ges_track_object_get_child_property_valist   (GESTrackObject * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

void ges_track_object_get_child_property     (GESTrackObject *object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;

void
ges_track_object_set_child_property_valist   (GESTrackObject * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

void
ges_track_object_set_child_property_by_pspec (GESTrackObject * object,
                                              GParamSpec * pspec,
                                              GValue * value);

void ges_track_object_set_child_property     (GESTrackObject * object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;


G_END_DECLS
#endif /* _GES_TRACK_OBJECT */
