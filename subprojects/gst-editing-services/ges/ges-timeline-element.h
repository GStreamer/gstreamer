/* gst-editing-services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
 *               <2013> Collabora Ltd.
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
#include "ges-enums.h"
#include "ges-types.h"

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_ELEMENT             (ges_timeline_element_get_type ())
GES_DECLARE_TYPE(TimelineElement, timeline_element, TIMELINE_ELEMENT);

/**
 * GES_TIMELINE_ELEMENT_START:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:start of @obj.
 */
#define GES_TIMELINE_ELEMENT_START(obj) (((GESTimelineElement*)obj)->start)

/**
 * GES_TIMELINE_ELEMENT_END:
 * @obj: A #GESTimelineElement
 *
 * The end position of @obj: #GESTimelineElement:start +
 * #GESTimelineElement:duration.
 */
#define GES_TIMELINE_ELEMENT_END(obj) ((((GESTimelineElement*)obj)->start) + (((GESTimelineElement*)obj)->duration))

/**
 * GES_TIMELINE_ELEMENT_INPOINT:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:in-point of @obj.
 */
#define GES_TIMELINE_ELEMENT_INPOINT(obj) (((GESTimelineElement*)obj)->inpoint)

/**
 * GES_TIMELINE_ELEMENT_DURATION:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:duration of @obj.
 */
#define GES_TIMELINE_ELEMENT_DURATION(obj) (((GESTimelineElement*)obj)->duration)

/**
 * GES_TIMELINE_ELEMENT_MAX_DURATION:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:max-duration of @obj.
 */
#define GES_TIMELINE_ELEMENT_MAX_DURATION(obj) (((GESTimelineElement*)obj)->maxduration)

/**
 * GES_TIMELINE_ELEMENT_PRIORITY:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:priority of @obj.
 */
#define GES_TIMELINE_ELEMENT_PRIORITY(obj) (((GESTimelineElement*)obj)->priority)

/**
 * GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY:
 *
 * Layer priority when a timeline element is not in any layer.
 */
#define GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY ((guint32) -1)

/**
 * GES_TIMELINE_ELEMENT_LAYER_PRIORITY:
 * @obj: The object to retrieve the layer priority from
 *
 * See #ges_timeline_element_get_layer_priority.
 */
#define GES_TIMELINE_ELEMENT_LAYER_PRIORITY(obj) (ges_timeline_element_get_layer_priority(((GESTimelineElement*)obj)))

/**
 * GES_TIMELINE_ELEMENT_PARENT:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:parent of @obj.
 */
#define GES_TIMELINE_ELEMENT_PARENT(obj) (((GESTimelineElement*)obj)->parent)

/**
 * GES_TIMELINE_ELEMENT_TIMELINE:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:timeline of @obj.
 */
#define GES_TIMELINE_ELEMENT_TIMELINE(obj) (((GESTimelineElement*)obj)->timeline)

/**
 * GES_TIMELINE_ELEMENT_NAME:
 * @obj: A #GESTimelineElement
 *
 * The #GESTimelineElement:name of @obj.
 */
#define GES_TIMELINE_ELEMENT_NAME(obj) (((GESTimelineElement*)obj)->name)

/**
 * GESTimelineElement:
 * @parent: The #GESTimelineElement:parent of the element
 * @asset: The #GESAsset from which the object has been extracted
 * @start: The #GESTimelineElement:start of the element
 * @inpoint: The #GESTimelineElement:in-point of the element
 * @duration: The #GESTimelineElement:duration of the element
 * @maxduration: The #GESTimelineElement:max-duration of the element
 * @priority: The #GESTimelineElement:priority of the element
 * @name: The #GESTimelineElement:name of the element
 * @timeline: The #GESTimelineElement:timeline of the element
 *
 * All members can be read freely, but should usually not be written to.
 * Subclasses may write to them, but should make sure to properly call
 * g_object_notify().
 */
struct _GESTimelineElement
{
  GInitiallyUnowned parent_instance;

  /*< public > */
  /*< read only >*/
  GESTimelineElement *parent;
  GESAsset *asset;
  GstClockTime start;
  GstClockTime inpoint;
  GstClockTime duration;
  GstClockTime maxduration;
  guint32 priority;
  GESTimeline *timeline;
  gchar *name;

  /*< private >*/
  GESTimelineElementPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESTimelineElementClass:
 * @set_parent: Method called just before the #GESTimelineElement:parent
 * is set.
 * @set_start: Method called just before the #GESTimelineElement:start is
 * set. This method should check whether the #GESTimelineElement:start can
 * be changed to the new value and to otherwise prepare the element in
 * response to what the new value will be. A return of %FALSE means that
 * the property should not be set. A return of %TRUE means that the
 * property should be set to the value given to the setter and a notify
 * emitted. A return of -1 means that the property should not be set but
 * the setter should still return %TRUE (normally because the method
 * already handled setting the value, potentially to a snapped value, and
 * emitted the notify signal).
 * #GESTimelineElement:duration is set. This method should check
 * whether the #GESTimelineElement:duration can be changed to the new
 * value and to otherwise prepare the element in response to what the new
 * value will be. A return of %FALSE means that the property should not be
 * set. A return of %TRUE means that the property should be set to the
 * value given to the setter and a notify emitted. A return of -1 means
 * that the property should not be set but the setter should still return
 * %TRUE (normally because the method already handled setting the value,
 * potentially to a snapped value, and emitted the notify signal).
 * @set_inpoint: Method called just before the
 * #GESTimelineElement:in-point is set to a new value. This method should
 * not set the #GESTimelineElement:in-point itself, but should check
 * whether it can be changed to the new value and to otherwise prepare the
 * element in response to what the new value will be. A return of %FALSE
 * means that the property should not be set.
 * @set_max_duration: Method called just before the
 * #GESTimelineElement:max-duration is set. This method should
 * not set the #GESTimelineElement:max-duration itself, but should check
 * whether it can be changed to the new value and to otherwise prepare the
 * element in response to what the new value will be. A return of %FALSE
 * means that the property should not be set.
 * @set_priority:  Method called just before the
 * #GESTimelineElement:priority is set.
 * @ripple: Set this method to overwrite a redirect to
 * ges_timeline_element_edit() in ges_timeline_element_ripple().
 * @ripple_end: Set this method to overwrite a redirect to
 * ges_timeline_element_edit() in ges_timeline_element_ripple_end().
 * @roll: Set this method to overwrite a redirect to
 * ges_timeline_element_edit() in ges_timeline_element_roll().
 * @roll_end: Set this method to overwrite a redirect to
 * ges_timeline_element_edit() in ges_timeline_element_roll_end().
 * @trim: Set this method to overwrite a redirect to
 * ges_timeline_element_edit() in ges_timeline_element_trim().
 * @deep_copy: Prepare @copy for pasting as a copy of @self. At least by
 * copying the children properties of @self into @copy.
 * @paste: Paste @self, which is the @copy prepared by @deep_copy, into
 * the timeline at the given @paste_position, with @ref_element as a
 * reference, which is the @self that was passed to @deep_copy.
 * @lookup-child: Method to find a child with the child property.
 * @prop_name. The default vmethod will return the first child that
 * matches. Overwrite this with a call to the parent vmethod if you wish
 * to redirect property names. Or overwrite to change which child is
 * returned if multiple children share the same child property name.
 * @get_track_types: Return a the track types for the element.
 * @list_children_properties: List the children properties that have been
 * registered for the element. The default implementation is able to fetch
 * all of these, so should be sufficient. If you overwrite this, you
 * should still call the default implementation to get the full list, and
 * then edit its content.
 * @lookup_child: Find @child, and its registered child property @pspec,
 * corresponding to the child property specified by @prop_name. The
 * default implementation will search for the first child that matches. If
 * you overwrite this, you will likely still want to call the default
 * vmethod, which has access to the registered parameter specifications.
 *
 * The #GESTimelineElement base class. Subclasses should override at least
 * @set_start @set_inpoint @set_duration @ripple @ripple_end @roll_start
 * @roll_end and @trim.
 *
 * Vmethods in subclasses should apply all the operation they need to but
 * the real method implementation is in charge of setting the proper field,
 * and emitting the notify signal.
 */
struct _GESTimelineElementClass
{
  GInitiallyUnownedClass parent_class;

  /*< public > */
  gboolean (*set_parent)       (GESTimelineElement * self, GESTimelineElement *parent);
  gboolean (*set_start)        (GESTimelineElement * self, GstClockTime start);
  gboolean (*set_inpoint)      (GESTimelineElement * self, GstClockTime inpoint);
  gboolean (*set_duration)     (GESTimelineElement * self, GstClockTime duration);
  gboolean (*set_max_duration) (GESTimelineElement * self, GstClockTime maxduration);
  gboolean (*set_priority)     (GESTimelineElement * self, guint32 priority); /* set_priority is now protected */

  gboolean (*ripple)           (GESTimelineElement *self, guint64  start);
  gboolean (*ripple_end)       (GESTimelineElement *self, guint64  end);
  gboolean (*roll_start)       (GESTimelineElement *self, guint64  start);
  gboolean (*roll_end)         (GESTimelineElement *self, guint64  end);
  gboolean (*trim)             (GESTimelineElement *self, guint64  start);
  void     (*deep_copy)        (GESTimelineElement *self, GESTimelineElement *copy);

  GESTimelineElement * (*paste) (GESTimelineElement *self,
                                   GESTimelineElement *ref_element,
                                   GstClockTime paste_position);

  GParamSpec** (*list_children_properties) (GESTimelineElement * self, guint *n_properties);
  gboolean (*lookup_child)                 (GESTimelineElement *self, const gchar *prop_name,
                                            GObject **child, GParamSpec **pspec);
  GESTrackType (*get_track_types)          (GESTimelineElement * self);

  /**
   * GESTimelineElementClass::set_child_property:
   *
   * Method for setting the child property given by
   * @pspec on @child to @value. Default implementation will use
   * g_object_set_property().
   *
   * Since: 1.16
   */
  void         (*set_child_property)       (GESTimelineElement * self,
                                            GObject *child,
                                            GParamSpec *pspec,
                                            GValue *value);

  /**
   * GESTimelineElementClass::get_layer_priority:
   *
   * Get the #GESLayer:priority of the layer that this
   * element is part of.
   *
   * Since: 1.16
   */
  guint32      (*get_layer_priority)       (GESTimelineElement *self);

  /**
   * GESTimelineElementClass::get_natural_framerate:
   * @self: A #GESTimelineElement
   * @framerate_n: The framerate numerator to retrieve
   * @framerate_d: The framerate denominator to retrieve
   *
   * Returns: %TRUE if @self has a natural framerate @FALSE otherwise.
   *
   * Since: 1.18
   */
  gboolean (*get_natural_framerate) (GESTimelineElement * self, gint *framerate_n, gint *framerate_d);

  /**
   * GESTimelineElementClass::set_child_property_full:
   *
   * Similar to @set_child_property, except setting can fail, with the @error
   * being optionally set. Default implementation will call @set_child_property
   * and return %TRUE.
   *
   * Since: 1.18
   */
  gboolean     (*set_child_property_full)  (GESTimelineElement * self,
                                            GObject *child,
                                            GParamSpec *pspec,
                                            const GValue *value,
                                            GError ** error);
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE - 6];
};

GES_API
GESTimelineElement * ges_timeline_element_get_toplevel_parent         (GESTimelineElement *self);
GES_API
GESTimelineElement * ges_timeline_element_get_parent                  (GESTimelineElement * self);
GES_API
gboolean             ges_timeline_element_set_parent                  (GESTimelineElement *self,
                                                                       GESTimelineElement *parent);
GES_API
gboolean             ges_timeline_element_set_timeline                (GESTimelineElement *self,
                                                                       GESTimeline *timeline);
GES_API
gboolean             ges_timeline_element_set_start                   (GESTimelineElement *self,
                                                                       GstClockTime start);
GES_API
gboolean             ges_timeline_element_set_inpoint                 (GESTimelineElement *self,
                                                                       GstClockTime inpoint);
GES_API
gboolean             ges_timeline_element_set_duration                (GESTimelineElement *self,
                                                                       GstClockTime duration);
GES_API
gboolean             ges_timeline_element_set_max_duration            (GESTimelineElement *self,
                                                                       GstClockTime maxduration);
GES_DEPRECATED
gboolean             ges_timeline_element_set_priority                (GESTimelineElement *self,
                                                                       guint32 priority);
GES_API
GstClockTime         ges_timeline_element_get_start                   (GESTimelineElement *self);
GES_API
GstClockTime         ges_timeline_element_get_inpoint                 (GESTimelineElement *self);
GES_API
GstClockTime         ges_timeline_element_get_duration                (GESTimelineElement *self);
GES_API
GstClockTime         ges_timeline_element_get_max_duration            (GESTimelineElement *self);
GES_API
GESTimeline *        ges_timeline_element_get_timeline                (GESTimelineElement *self);
GES_API
guint32              ges_timeline_element_get_priority                (GESTimelineElement *self);
GES_API
gboolean             ges_timeline_element_ripple                      (GESTimelineElement *self,
                                                                       GstClockTime  start);
GES_API
gboolean             ges_timeline_element_ripple_end                  (GESTimelineElement *self,
                                                                       GstClockTime  end);
GES_API
gboolean             ges_timeline_element_roll_start                  (GESTimelineElement *self,
                                                                       GstClockTime  start);
GES_API
gboolean             ges_timeline_element_roll_end                    (GESTimelineElement *self,
                                                                       GstClockTime  end);
GES_API
gboolean             ges_timeline_element_trim                        (GESTimelineElement *self,
                                                                       GstClockTime  start);
GES_API
GESTimelineElement * ges_timeline_element_copy                        (GESTimelineElement *self,
                                                                       gboolean deep);
GES_API
gchar  *             ges_timeline_element_get_name                    (GESTimelineElement *self);
GES_API
gboolean             ges_timeline_element_set_name                    (GESTimelineElement *self,
                                                                       const gchar *name);
GES_API
GParamSpec **        ges_timeline_element_list_children_properties    (GESTimelineElement *self,
                                                                       guint *n_properties);
GES_API
gboolean             ges_timeline_element_lookup_child                (GESTimelineElement *self,
                                                                       const gchar *prop_name,
                                                                       GObject  **child,
                                                                       GParamSpec **pspec);
GES_API
void                 ges_timeline_element_get_child_property_by_pspec (GESTimelineElement * self,
                                                                       GParamSpec * pspec, GValue * value);
GES_API
void                 ges_timeline_element_get_child_property_valist   (GESTimelineElement * self,
                                                                       const gchar * first_property_name,
                                                                       va_list var_args);
GES_API
void                 ges_timeline_element_get_child_properties        (GESTimelineElement *self,
                                                                       const gchar * first_property_name, ...) G_GNUC_NULL_TERMINATED;
GES_API
void                 ges_timeline_element_set_child_property_valist   (GESTimelineElement * self,
                                                                       const gchar * first_property_name,
                                                                       va_list var_args);
GES_API
void                 ges_timeline_element_set_child_property_by_pspec (GESTimelineElement * self,
                                                                       GParamSpec * pspec,
                                                                       const GValue * value);
GES_API
void                 ges_timeline_element_set_child_properties        (GESTimelineElement * self,
                                                                       const gchar * first_property_name,
                                                                       ...) G_GNUC_NULL_TERMINATED;
GES_API
gboolean             ges_timeline_element_set_child_property          (GESTimelineElement *self,
                                                                       const gchar *property_name,
                                                                       const GValue * value);
GES_API
gboolean             ges_timeline_element_set_child_property_full     (GESTimelineElement *self,
                                                                       const gchar *property_name,
                                                                       const GValue * value,
                                                                       GError ** error);
GES_API
gboolean             ges_timeline_element_get_child_property          (GESTimelineElement *self,
                                                                       const gchar *property_name,
                                                                       GValue * value);
GES_API
gboolean             ges_timeline_element_add_child_property          (GESTimelineElement * self,
                                                                       GParamSpec *pspec,
                                                                       GObject *child);
GES_API
gboolean             ges_timeline_element_remove_child_property       (GESTimelineElement * self,
                                                                       GParamSpec *pspec);
GES_API
GESTimelineElement * ges_timeline_element_paste                       (GESTimelineElement * self,
                                                                       GstClockTime paste_position);
GES_API
GESTrackType         ges_timeline_element_get_track_types             (GESTimelineElement * self);
GES_API
gboolean             ges_timeline_element_get_natural_framerate       (GESTimelineElement *self,
                                                                       gint *framerate_n,
                                                                       gint *framerate_d);

GES_API
guint32 ges_timeline_element_get_layer_priority                       (GESTimelineElement * self);

GES_API
gboolean ges_timeline_element_edit                                    (GESTimelineElement * self,
                                                                       GList * layers,
                                                                       gint64 new_layer_priority,
                                                                       GESEditMode mode,
                                                                       GESEdge edge,
                                                                       guint64 position);

GES_API
gboolean ges_timeline_element_edit_full                                (GESTimelineElement * self,
                                                                       gint64 new_layer_priority,
                                                                       GESEditMode mode,
                                                                       GESEdge edge,
                                                                       guint64 position,
                                                                       GError ** error);
G_END_DECLS
