/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-track-object.h"

static GQuark _start_quark;
static GQuark _inpoint_quark;
static GQuark _duration_quark;
static GQuark _priority_quark;

#define _do_init \
{ \
  gint i; \
  \
  _start_quark = g_quark_from_static_string ("start"); \
  _inpoint_quark = g_quark_from_static_string ("inpoint"); \
  _duration_quark = g_quark_from_static_string ("duration"); \
  _priority_quark = g_quark_from_static_string ("priority"); \
}
G_DEFINE_TYPE_WITH_CODE (GESTrackObject, ges_track_object, G_TYPE_OBJECT,
    _do_init)
#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_TRACK_OBJECT, GESTrackObjectPrivate))
     enum
     {
       PROP_0,
       PROP_START,
       PROP_INPOINT,
       PROP_DURATION,
       PROP_PRIORITY,
     }

     typedef struct _GESTrackObjectPrivate GESTrackObjectPrivate;

     struct _GESTrackObjectPrivate
     {
       int dummy;
     };

     static void
         ges_track_object_get_property (GObject * object, guint property_id,
         GValue * value, GParamSpec * pspec)
     {
       GESTrackObject *tobj = GES_TRACK_OBJECT (object);

       switch (property_id) {
         case PROP_START:
           g_value_set_uint64 (value, tobj->start);
           break;
         case PROP_INPOINT:
           g_value_set_uint64 (value, tobj->inpoint);
           break;
         case PROP_DURATION:
           g_value_set_uint64 (value, tobj->duration);
           break;
         case PROP_PRIORITY:
           g_value_set_uint (value, tobj->priority);
           break;
         default:
           G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
       }
     }

     static void
         ges_track_object_set_property (GObject * object, guint property_id,
         const GValue * value, GParamSpec * pspec)
     {
       GESTrackObject *tobj = GES_TRACK_OBJECT (object);

       switch (property_id) {
         case PROP_START:
           ges_track_object_set_start_internal (tobj,
               g_value_get_uint64 (value));
           break;
         case PROP_INPOINT:
           ges_track_object_set_inpoint_internal (tobj,
               g_value_get_uint64 (value));
           break;
         case PROP_DURATION:
           ges_track_object_set_duration_internal (tobj,
               g_value_get_uint64 (value));
           break;
         case PROP_PRIORITY:
           ges_track_object_set_priority_internal (tobj,
               g_value_get_uint (value));
           break;
         default:
           G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
       }
     }

     static void ges_track_object_dispose (GObject * object)
     {
       G_OBJECT_CLASS (ges_track_object_parent_class)->dispose (object);
     }

     static void ges_track_object_finalize (GObject * object)
     {
       G_OBJECT_CLASS (ges_track_object_parent_class)->finalize (object);
     }

     static void ges_track_object_class_init (GESTrackObjectClass * klass)
     {
       GObjectClass *object_class = G_OBJECT_CLASS (klass);

       g_type_class_add_private (klass, sizeof (GESTrackObjectPrivate));

       object_class->get_property = ges_track_object_get_property;
       object_class->set_property = ges_track_object_set_property;
       object_class->dispose = ges_track_object_dispose;
       object_class->finalize = ges_track_object_finalize;

       g_object_class_install_property (object_class, PROP_START,
           g_param_spec_uint64 ("start", "Start",
               "The position in the container", 0, G_MAXUINT64, 0,
               G_PARAM_READWRITE));
       g_object_class_install_property (object_class, PROP_INPOINT,
           g_param_spec_uint64 ("inpoint", "In-point", "The in-point", 0,
               G_MAXUINT64, 0, G_PARAM_READWRITE));
       g_object_class_install_property (object_class, PROP_DURATION,
           g_param_spec_uint64 ("duration", "Duration", "The duration to use",
               0, G_MAXUINT64, 0, G_PARAM_READWRITE));
       g_object_class_install_property (object_class, PROP_PRIORITY,
           g_param_spec_uint ("priority", "Priority",
               "The priority of the object", 0, G_MAXUINT, 0,
               G_PARAM_READWRITE));
     }

     static void ges_track_object_init (GESTrackObject * self)
     {
     }

     GESTrackObject *ges_track_object_new (GESTimelineObject * timelineobj,
         GESTrack * track)
     {
       GESTrackObject *obj;

       obj = g_object_new (GES_TYPE_TRACK_OBJECT, NULL);

       /* Set the timeline object and track */
       obj->timelineobj = timelineobj;
       obj->track = track;

       /* Create the associated GnlObject */
       ges_track_object_create_gnl_object (obj);
     }

     gboolean
         ges_track_object_set_start_internal (GESTrackObject * object,
         guint64 start) {
       g_return_val_if_fail (object->gnlobject, FALSE);

       if (G_UNLIKELY (start == object->start))
         return FALSE;

       g_object_set (object->gnlobject, "start", start, NULL);
       return TRUE;
     };

     gboolean
         ges_track_object_set_inpoint_internal (GESTrackObject * object,
         guint64 inpoint) {
       guint64 dur;

       g_return_val_if_fail (object->gnlobject, FALSE);

       if (G_UNLIKELY (inpoint == object->inpoint))
         return FALSE;

       /* Calculate new media-start/duration/media-duration */
       dur = object->inpoint - inpoint + object->duration;

       g_object_set (object->gnlobject, "media-start", inpoint, "duration", dur,
           "media-duration", dur, NULL);
       return TRUE;
     }

     gboolean
         ges_track_object_set_duration_internal (GESTrackObject * object,
         guint64 duration) {
       g_return_val_if_fail (object->gnlobject, FALSE);

       if (G_UNLIKELY (duration == object->duration))
         return FALSE;

       g_object_set (object->gnlobject, "duration", duration, "media-duration",
           duration, NULL);
       return TRUE;
     }

     gboolean
         ges_track_object_set_priority_internal (GESTrackObject * object,
         guint32 priority) {
       g_return_val_if_fail (object->gnlobject, FALSE);

       if (G_UNLIKELY (priority == object->priority))
         return FALSE;

       g_object_set (object->gnlobject, "priority", priority, NULL);
       return TRUE;
     }
