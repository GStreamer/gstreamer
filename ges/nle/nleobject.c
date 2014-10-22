/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@gmail.com>
 *               <2004-2008> Edward Hervey <bilboed@bilboed.com>
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

#include <string.h>
#include "nle.h"

/**
 * SECTION:nleobject
 * @short_description: Base class for GNonLin elements
 *
 * <refsect2>
 * <para>
 * NleObject encapsulates default behaviour and implements standard
 * properties provided by all the GNonLin elements.
 * </para>
 * </refsect2>
 *
 */


GST_DEBUG_CATEGORY_STATIC (nleobject_debug);
#define GST_CAT_DEFAULT nleobject_debug

static GObjectClass *parent_class = NULL;

/****************************************************
 *              Helper macros                       *
 ****************************************************/
#define CHECK_AND_SET(PROPERTY, property, prop_str, print_format)            \
{                                                                            \
if (object->pending_##property != object->property)      {            \
  object->property = object->pending_##property;                             \
  GST_DEBUG_OBJECT(object, "Setting " prop_str " to %"                       \
      print_format, object->property);                                       \
} else                                                                       \
  GST_DEBUG_OBJECT(object, "Nothing to do for " prop_str);                   \
}

#define SET_PENDING_VALUE(property, property_str, type, print_format)      \
nleobject->pending_##property = g_value_get_##type (value);                \
if (nleobject->property != nleobject->pending_##property) {                \
  GST_DEBUG_OBJECT(object, "Setting pending " property_str " to %"         \
      print_format, nleobject->pending_##property);                        \
  nle_object_set_commit_needed (nleobject);                                \
} else                                                                     \
  GST_DEBUG_OBJECT(object, "Pending " property_str " did not change");

enum
{
  PROP_0,
  PROP_START,
  PROP_DURATION,
  PROP_STOP,
  PROP_INPOINT,
  PROP_PRIORITY,
  PROP_ACTIVE,
  PROP_CAPS,
  PROP_EXPANDABLE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void nle_object_dispose (GObject * object);

static void nle_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void nle_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn nle_object_change_state (GstElement * element,
    GstStateChange transition);

static gboolean nle_object_prepare_func (NleObject * object);
static gboolean nle_object_cleanup_func (NleObject * object);
static gboolean nle_object_commit_func (NleObject * object, gboolean recurse);

static GstStateChangeReturn nle_object_prepare (NleObject * object);

static void
nle_object_class_init (NleObjectClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  NleObjectClass *nleobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  nleobject_class = (NleObjectClass *) klass;
  GST_DEBUG_CATEGORY_INIT (nleobject_debug, "nleobject",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin object");
  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (nle_object_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (nle_object_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (nle_object_dispose);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (nle_object_change_state);

  nleobject_class->prepare = GST_DEBUG_FUNCPTR (nle_object_prepare_func);
  nleobject_class->cleanup = GST_DEBUG_FUNCPTR (nle_object_cleanup_func);
  nleobject_class->commit_signal_handler =
      GST_DEBUG_FUNCPTR (nle_object_commit);
  nleobject_class->commit = GST_DEBUG_FUNCPTR (nle_object_commit_func);

  /**
   * NleObject:start
   *
   * The start position relative to the parent in nanoseconds.
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The start position relative to the parent (in nanoseconds)",
      0, G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_START,
      properties[PROP_START]);

  /**
   * NleObject:duration
   *
   * The outgoing duration in nanoseconds.
   */
  properties[PROP_DURATION] = g_param_spec_int64 ("duration", "Duration",
      "Outgoing duration (in nanoseconds)", 0, G_MAXINT64, 0,
      G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_DURATION,
      properties[PROP_DURATION]);

  /**
   * NleObject:stop
   *
   * The stop position relative to the parent in nanoseconds.
   *
   * This value is computed based on the values of start and duration.
   */
  properties[PROP_STOP] = g_param_spec_uint64 ("stop", "Stop",
      "The stop position relative to the parent (in nanoseconds)",
      0, G_MAXUINT64, 0, G_PARAM_READABLE);
  g_object_class_install_property (gobject_class, PROP_STOP,
      properties[PROP_STOP]);

  /**
   * NleObject:inpoint
   *
   * The media start position in nanoseconds.
   *
   * Also called 'in-point' in video-editing, this corresponds to
   * what position in the 'contained' object we should start outputting from.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("inpoint", "Media start",
      "The media start position (in nanoseconds)", 0, G_MAXUINT64,
      GST_CLOCK_TIME_NONE, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_INPOINT,
      properties[PROP_INPOINT]);

  /**
   * NleObject:priority
   *
   * The priority of the object in the container.
   *
   * The highest priority is 0, meaning this object will be selected over
   * any other between start and stop.
   *
   * The lowest priority is G_MAXUINT32.
   *
   * Objects whose priority is (-1) will be considered as 'default' objects
   * in NleComposition and their start/stop values will be modified as to
   * fit the whole duration of the composition.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object (0 = highest priority)", 0, G_MAXUINT, 0,
      G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_PRIORITY,
      properties[PROP_PRIORITY]);

  /**
   * NleObject:active
   *
   * Indicates whether this object should be used by its container.
   *
   * Set to #TRUE to temporarily disable this object in a #NleComposition.
   */
  properties[PROP_ACTIVE] = g_param_spec_boolean ("active", "Active",
      "Use this object in the NleComposition", TRUE, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_ACTIVE,
      properties[PROP_ACTIVE]);

  /**
   * NleObject:caps
   *
   * Caps used to filter/choose the output stream.
   *
   * If the controlled object produces several stream, you can set this
   * property to choose a specific stream.
   *
   * If nothing is specified then a source pad will be chosen at random.
   */
  properties[PROP_CAPS] = g_param_spec_boxed ("caps", "Caps",
      "Caps used to filter/choose the output stream",
      GST_TYPE_CAPS, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_CAPS,
      properties[PROP_CAPS]);

  /**
   * NleObject:expandable
   *
   * Indicates whether this object should expand to the full duration of its
   * container #NleComposition.
   */
  properties[PROP_EXPANDABLE] =
      g_param_spec_boolean ("expandable", "Expandable",
      "Expand to the full duration of the container composition", FALSE,
      G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_EXPANDABLE,
      properties[PROP_EXPANDABLE]);
}

static void
nle_object_init (NleObject * object, NleObjectClass * klass)
{
  object->start = object->pending_start = 0;
  object->duration = object->pending_duration = 0;
  object->stop = 0;

  object->inpoint = object->pending_inpoint = GST_CLOCK_TIME_NONE;
  object->priority = object->pending_priority = 0;
  object->active = object->pending_active = TRUE;

  object->caps = gst_caps_new_any ();

  object->segment_rate = 1.0;
  object->segment_start = -1;
  object->segment_stop = -1;

  object->srcpad = nle_object_ghost_pad_no_target (object,
      "src", GST_PAD_SRC,
      gst_element_class_get_pad_template ((GstElementClass *) klass, "src"));

  gst_element_add_pad (GST_ELEMENT (object), object->srcpad);
}

static void
nle_object_dispose (GObject * object)
{
  NleObject *nle = (NleObject *) object;

  if (nle->caps) {
    gst_caps_unref (nle->caps);
    nle->caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * nle_object_to_media_time:
 * @object: a #NleObject
 * @objecttime: The #GstClockTime we want to convert
 * @mediatime: A pointer on a #GstClockTime to fill
 *
 * Converts a #GstClockTime from the object (container) context to the media context
 *
 * Returns: TRUE if @objecttime was within the limits of the @object start/stop time,
 * FALSE otherwise
 */
gboolean
nle_object_to_media_time (NleObject * object, GstClockTime otime,
    GstClockTime * mtime)
{
  g_return_val_if_fail (mtime, FALSE);

  GST_DEBUG_OBJECT (object, "ObjectTime : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (otime));

  GST_DEBUG_OBJECT (object,
      "Start/Stop:[%" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT "] "
      "Media start: %" GST_TIME_FORMAT, GST_TIME_ARGS (object->start),
      GST_TIME_ARGS (object->stop), GST_TIME_ARGS (object->inpoint));

  /* limit check */
  if (G_UNLIKELY ((otime < object->start))) {
    GST_DEBUG_OBJECT (object, "ObjectTime is before start");
    *mtime = (object->inpoint == GST_CLOCK_TIME_NONE) ? 0 : object->inpoint;
    return FALSE;
  }

  if (G_UNLIKELY ((otime >= object->stop))) {
    GST_DEBUG_OBJECT (object, "ObjectTime is after stop");
    if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (object->inpoint)))
      *mtime = object->inpoint + object->duration;
    else
      *mtime = object->stop - object->start;
    return FALSE;
  }

  if (G_UNLIKELY (object->inpoint == GST_CLOCK_TIME_NONE)) {
    /* no time shifting, for live sources ? */
    *mtime = otime - object->start;
  } else {
    *mtime = otime - object->start + object->inpoint;
  }

  GST_DEBUG_OBJECT (object, "Returning MediaTime : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*mtime));

  return TRUE;
}

/**
 * nle_media_to_object_time:
 * @object: The #NleObject
 * @mediatime: The #GstClockTime we want to convert
 * @objecttime: A pointer on a #GstClockTime to fill
 *
 * Converts a #GstClockTime from the media context to the object (container) context
 *
 * Returns: TRUE if @objecttime was within the limits of the @object media start/stop time,
 * FALSE otherwise
 */

gboolean
nle_media_to_object_time (NleObject * object, GstClockTime mtime,
    GstClockTime * otime)
{
  g_return_val_if_fail (otime, FALSE);

  GST_DEBUG_OBJECT (object, "MediaTime : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (mtime));

  GST_DEBUG_OBJECT (object,
      "Start/Stop:[%" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT "] "
      "inpoint  %" GST_TIME_FORMAT, GST_TIME_ARGS (object->start),
      GST_TIME_ARGS (object->stop), GST_TIME_ARGS (object->inpoint));


  /* limit check */
  if (G_UNLIKELY ((object->inpoint != GST_CLOCK_TIME_NONE)
          && (mtime < object->inpoint))) {
    GST_DEBUG_OBJECT (object, "media time is before inpoint, forcing to start");
    *otime = object->start;
    return FALSE;
  }

  if (G_LIKELY (object->inpoint != GST_CLOCK_TIME_NONE)) {
    *otime = mtime - object->inpoint + object->start;
  } else
    *otime = mtime + object->start;

  GST_DEBUG_OBJECT (object, "Returning ObjectTime : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*otime));
  return TRUE;
}

static gboolean
nle_object_prepare_func (NleObject * object)
{
  GST_DEBUG_OBJECT (object, "default prepare function, returning TRUE");

  return TRUE;
}

static GstStateChangeReturn
nle_object_prepare (NleObject * object)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (object, "preparing");

  if (!(NLE_OBJECT_GET_CLASS (object)->prepare (object)))
    ret = GST_STATE_CHANGE_FAILURE;

  GST_DEBUG_OBJECT (object, "finished preparing, returning %d", ret);

  return ret;
}

static gboolean
nle_object_cleanup_func (NleObject * object)
{
  GST_DEBUG_OBJECT (object, "default cleanup function, returning TRUE");

  return TRUE;
}

GstStateChangeReturn
nle_object_cleanup (NleObject * object)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (object, "cleaning-up");

  if (!(NLE_OBJECT_GET_CLASS (object)->cleanup (object)))
    ret = GST_STATE_CHANGE_FAILURE;

  GST_DEBUG_OBJECT (object, "finished preparing, returning %d", ret);

  return ret;
}

void
nle_object_set_caps (NleObject * object, const GstCaps * caps)
{
  if (object->caps)
    gst_caps_unref (object->caps);

  object->caps = gst_caps_copy (caps);
}

static inline void
_update_stop (NleObject * nleobject)
{
  /* check if start/duration has changed */

  if ((nleobject->pending_start + nleobject->pending_duration) !=
      nleobject->stop) {
    nleobject->stop = nleobject->pending_start + nleobject->pending_duration;

    GST_LOG_OBJECT (nleobject,
        "Updating stop value : %" GST_TIME_FORMAT " [start:%" GST_TIME_FORMAT
        ", duration:%" GST_TIME_FORMAT "]", GST_TIME_ARGS (nleobject->stop),
        GST_TIME_ARGS (nleobject->pending_start),
        GST_TIME_ARGS (nleobject->pending_duration));
    g_object_notify_by_pspec (G_OBJECT (nleobject), properties[PROP_STOP]);
  }
}

static void
update_values (NleObject * object)
{
  CHECK_AND_SET (START, start, "start", G_GUINT64_FORMAT);
  CHECK_AND_SET (INPOINT, inpoint, "inpoint", G_GUINT64_FORMAT);
  CHECK_AND_SET (DURATION, duration, "duration", G_GINT64_FORMAT);
  CHECK_AND_SET (PRIORITY, priority, "priority", G_GUINT32_FORMAT);
  CHECK_AND_SET (ACTIVE, active, "active", G_GUINT32_FORMAT);

  _update_stop (object);
}

static gboolean
nle_object_commit_func (NleObject * object, gboolean recurse)
{
  GST_INFO_OBJECT (object, "Commiting object changed");

  if (object->commit_needed == FALSE) {
    GST_INFO_OBJECT (object, "No changes to commit");

    return FALSE;
  }

  update_values (object);

  GST_INFO_OBJECT (object, "Done commiting");

  return TRUE;
}

static void
nle_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  NleObject *nleobject = (NleObject *) object;

  g_return_if_fail (NLE_IS_OBJECT (object));

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case PROP_START:
      SET_PENDING_VALUE (start, "start", uint64, G_GUINT64_FORMAT);
      break;
    case PROP_DURATION:
      SET_PENDING_VALUE (duration, "duration", int64, G_GINT64_FORMAT);
      break;
    case PROP_INPOINT:
      SET_PENDING_VALUE (inpoint, "inpoint", uint64, G_GUINT64_FORMAT);
      break;
    case PROP_PRIORITY:
      SET_PENDING_VALUE (priority, "priority", uint, G_GUINT32_FORMAT);
      break;
    case PROP_ACTIVE:
      SET_PENDING_VALUE (active, "active", boolean, G_GUINT32_FORMAT);
      break;
    case PROP_CAPS:
      nle_object_set_caps (nleobject, gst_value_get_caps (value));
      break;
    case PROP_EXPANDABLE:
      if (g_value_get_boolean (value))
        GST_OBJECT_FLAG_SET (nleobject, NLE_OBJECT_EXPANDABLE);
      else
        GST_OBJECT_FLAG_UNSET (nleobject, NLE_OBJECT_EXPANDABLE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (object);

  _update_stop (nleobject);
}

static void
nle_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  NleObject *nleobject = (NleObject *) object;

  switch (prop_id) {
    case PROP_START:
      g_value_set_uint64 (value, nleobject->pending_start);
      break;
    case PROP_DURATION:
      g_value_set_int64 (value, nleobject->pending_duration);
      break;
    case PROP_STOP:
      g_value_set_uint64 (value, nleobject->stop);
      break;
    case PROP_INPOINT:
      g_value_set_uint64 (value, nleobject->pending_inpoint);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, nleobject->pending_priority);
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, nleobject->pending_active);
      break;
    case PROP_CAPS:
      gst_value_set_caps (value, nleobject->caps);
      break;
    case PROP_EXPANDABLE:
      g_value_set_boolean (value, NLE_OBJECT_IS_EXPANDABLE (object));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
nle_object_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GstObject *parent = gst_object_get_parent (GST_OBJECT (element));

      /* Going to READY and if we are not in a composition, we need to make
       * sure that the object positioning state is properly commited  */
      if (parent) {
        if (g_strcmp0 (gst_element_get_name (GST_ELEMENT (parent)),
                "current-bin")
            && !NLE_OBJECT_IS_COMPOSITION (NLE_OBJECT (element))) {
          GST_INFO ("Adding nleobject to something that is not a composition,"
              " commiting ourself");
          nle_object_commit (NLE_OBJECT (element), FALSE);
        }

        gst_object_unref (parent);
      }
    }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (nle_object_prepare (NLE_OBJECT (element)) == GST_STATE_CHANGE_FAILURE) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto beach;
      }
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (element, "Calling parent change_state");

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  GST_DEBUG_OBJECT (element, "Return from parent change_state was %d", ret);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto beach;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* cleanup nleobject */
      if (nle_object_cleanup (NLE_OBJECT (element)) == GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

beach:
  return ret;
}

void
nle_object_set_commit_needed (NleObject * object)
{
  if (G_UNLIKELY (object->commiting)) {
    GST_WARNING_OBJECT (object,
        "Trying to set 'commit-needed' while commiting");

    return;
  }

  GST_DEBUG_OBJECT (object, "Setting 'commit_needed'");
  object->commit_needed = TRUE;
}

gboolean
nle_object_commit (NleObject * object, gboolean recurse)
{
  gboolean ret;

  GST_DEBUG_OBJECT (object, "Commiting object state");

  object->commiting = TRUE;
  ret = NLE_OBJECT_GET_CLASS (object)->commit (object, recurse);
  object->commiting = FALSE;

  return ret;

}

static void
_send_seek_event (const GValue * item, gpointer seek_event)
{
  GstElement *child = g_value_get_object (item);

  gst_element_send_event (child, gst_event_ref (seek_event));
}

void
nle_object_seek_all_children (NleObject * object, GstEvent * seek_event)
{
  GstIterator *it = gst_bin_iterate_recurse (GST_BIN (object));

  while (gst_iterator_foreach (it, _send_seek_event,
          seek_event) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);
  gst_event_unref (seek_event);
}

void
nle_object_reset (NleObject * object)
{
  GST_INFO_OBJECT (object, "Resetting child timing values to default");

  object->start = 0;
  object->duration = 0;
  object->stop = 0;
  object->inpoint = GST_CLOCK_TIME_NONE;
  object->priority = 0;
  object->active = TRUE;
}

GType
nle_object_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (NleObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) nle_object_class_init,
      NULL,
      NULL,
      sizeof (NleObject),
      0,
      (GInstanceInitFunc) nle_object_init,
    };

    _type = g_type_register_static (GST_TYPE_BIN,
        "NleObject", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}
