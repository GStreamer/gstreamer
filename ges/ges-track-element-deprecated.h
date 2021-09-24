#pragma once

GES_DEPRECATED_FOR(ges_track_element_get_nleobject)
GstElement * ges_track_element_get_gnlobject   (GESTrackElement * object);


GES_DEPRECATED_FOR(ges_timeline_element_list_children_properties)
GParamSpec **
ges_track_element_list_children_properties     (GESTrackElement *object,
                                               guint *n_properties);

GES_DEPRECATED_FOR(ges_timeline_element_lookup_child)
gboolean ges_track_element_lookup_child        (GESTrackElement *object,
                                               const gchar *prop_name,
                                               GstElement **element,
                                               GParamSpec **pspec);

GES_DEPRECATED_FOR(ges_timeline_element_get_child_property_valist)
void
ges_track_element_get_child_property_valist   (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

GES_DEPRECATED_FOR(ges_timeline_element_get_child_properties)
void ges_track_element_get_child_properties   (GESTrackElement *object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;

GES_DEPRECATED_FOR(ges_timeline_element_set_child_property_valist)
void
ges_track_element_set_child_property_valist   (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              va_list var_args);

GES_DEPRECATED_FOR(ges_timeline_element_set_child_property_by_spec)
void
ges_track_element_set_child_property_by_pspec (GESTrackElement * object,
                                              GParamSpec * pspec,
                                              GValue * value);

GES_DEPRECATED_FOR(ges_timeline_element_set_child_properties)
void
ges_track_element_set_child_properties       (GESTrackElement * object,
                                              const gchar * first_property_name,
                                              ...) G_GNUC_NULL_TERMINATED;

GES_DEPRECATED_FOR(ges_timeline_element_set_child_property)
gboolean ges_track_element_set_child_property (GESTrackElement *object,
                                              const gchar *property_name,
                                              GValue * value);

GES_DEPRECATED_FOR(ges_timeline_element_get_child_property)
gboolean ges_track_element_get_child_property (GESTrackElement *object,
                                              const gchar *property_name,
                                              GValue * value);

GES_DEPRECATED_FOR(ges_timeline_element_edit)
gboolean
ges_track_element_edit                        (GESTrackElement * object,
                                              GList *layers, GESEditMode mode,
                                              GESEdge edge, guint64 position);
