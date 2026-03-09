/* Gnonlin
 * Copyright (C) <2009> Edward Hervey <bilboed@bilboed.com>
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

#include "nle.h"

GST_DEBUG_CATEGORY_STATIC (nleghostpad);
#define GST_CAT_DEFAULT nleghostpad

#define NLE_TYPE_GHOST_PAD            (nle_ghost_pad_get_type())
#define NLE_GHOST_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), NLE_TYPE_GHOST_PAD, NleGhostPad))
#define NLE_GHOST_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), NLE_TYPE_GHOST_PAD, NleGhostPadClass))
#define NLE_IS_GHOST_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), NLE_TYPE_GHOST_PAD))

typedef struct _NleGhostPad NleGhostPad;
typedef struct _NleGhostPadClass NleGhostPadClass;

struct _NleGhostPad
{
  GstGhostPad parent;

  NleObject *object;
  GstPadDirection dir;

  /* Original ghost pad event/query functions (saved before override) */
  GstPadEventFunction ghostpad_eventfunc;
  GstPadQueryFunction ghostpad_queryfunc;

  /* Original internal proxy pad event/query functions */
  GstPadEventFunction internalpad_eventfunc;
  GstPadQueryFunction internalpad_queryfunc;

  GstEvent *pending_seek;
};

struct _NleGhostPadClass
{
  GstGhostPadClass parent_class;
};

GType nle_ghost_pad_get_type (void);
G_DEFINE_TYPE (NleGhostPad, nle_ghost_pad, GST_TYPE_GHOST_PAD);

static void
nle_ghost_pad_init (NleGhostPad * self)
{
}

static void
nle_ghost_pad_class_init (NleGhostPadClass * klass)
{
}

/**
 * nle_object_translate_incoming_seek:
 * @object: A #NleObject.
 * @event: (transfer full) A #GstEvent to translate
 *
 * Returns: (transfer full) new translated seek event
 */
GstEvent *
nle_object_translate_incoming_seek (NleObject * object, GstEvent * event)
{
  GstEvent *event2;
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType curtype, stoptype;
  GstSeekType ncurtype;
  gint64 cur;
  guint64 ncur;
  gint64 stop;
  guint64 nstop;
  guint32 seqnum = GST_EVENT_SEQNUM (event);

  gst_event_parse_seek (event, &rate, &format, &flags,
      &curtype, &cur, &stoptype, &stop);

  GST_DEBUG_OBJECT (object,
      "GOT SEEK rate:%f, format:%d, flags:%d, curtype:%d, stoptype:%d, %"
      GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, rate, format, flags, curtype,
      stoptype, GST_TIME_ARGS (cur), GST_TIME_ARGS (stop));

  if (G_UNLIKELY (format != GST_FORMAT_TIME))
    goto invalid_format;


  if (NLE_IS_SOURCE (object) && NLE_SOURCE (object)->reverse) {
    GST_DEBUG_OBJECT (object, "Reverse playback! %d", seqnum);
    rate = -rate;
  }

  /* convert cur */
  ncurtype = GST_SEEK_TYPE_SET;
  if (G_LIKELY ((curtype == GST_SEEK_TYPE_SET)
          && (nle_object_to_media_time (object, cur, &ncur)))) {
    /* cur is TYPE_SET and value is valid */
    if (ncur > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting cur to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (ncur));
  } else if ((curtype != GST_SEEK_TYPE_NONE)) {
    GST_DEBUG_OBJECT (object, "Limiting seek start to inpoint");
    ncur = object->inpoint;
  } else {
    GST_DEBUG_OBJECT (object, "leaving GST_SEEK_TYPE_NONE");
    ncur = cur;
    ncurtype = GST_SEEK_TYPE_NONE;
  }

  /* convert stop, we also need to limit it to object->stop */
  if (G_LIKELY ((stoptype == GST_SEEK_TYPE_SET)
          && (nle_object_to_media_time (object, stop, &nstop)))) {
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  } else {
    /* NOTE: for an element that is upstream from a time effect we do not
     * want to limit the seek to the object->stop because a time effect
     * can reasonably increase the final time.
     * In such situations, the seek stop should be set once by the
     * source pad of the most downstream object (highest priority in the
     * nlecomposition). */
    GST_DEBUG_OBJECT (object, "Limiting end of seek to media_stop");
    nle_object_to_media_time (object, object->stop, &nstop);
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  }


  /* add accurate seekflags */
  if (G_UNLIKELY (!(flags & GST_SEEK_FLAG_ACCURATE))) {
    GST_DEBUG_OBJECT (object, "Adding GST_SEEK_FLAG_ACCURATE");
    flags |= GST_SEEK_FLAG_ACCURATE;
  } else {
    GST_DEBUG_OBJECT (object,
        "event already has GST_SEEK_FLAG_ACCURATE : %d", flags);
  }



  GST_DEBUG_OBJECT (object,
      "SENDING SEEK rate:%f, format:TIME, flags:%d, curtype:%d, stoptype:SET, %"
      GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, rate, flags, ncurtype,
      GST_TIME_ARGS (ncur), GST_TIME_ARGS (nstop));

  event2 = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
      ncurtype, (gint64) ncur, GST_SEEK_TYPE_SET, (gint64) nstop);
  GST_EVENT_SEQNUM (event2) = seqnum;
  gst_event_unref (event);

  return event2;

  /* ERRORS */
invalid_format:
  {
    GST_WARNING ("GNonLin time shifting only works with GST_FORMAT_TIME");
    return event;
  }
}

static GstEvent *
translate_outgoing_seek (NleObject * object, GstEvent * event)
{
  GstEvent *event2;
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType curtype, stoptype;
  GstSeekType ncurtype;
  gint64 cur;
  guint64 ncur;
  gint64 stop;
  guint64 nstop;
  guint32 seqnum = GST_EVENT_SEQNUM (event);

  gst_event_parse_seek (event, &rate, &format, &flags,
      &curtype, &cur, &stoptype, &stop);

  GST_DEBUG_OBJECT (object,
      "GOT SEEK rate:%f, format:%d, flags:%d, curtype:%d, stoptype:%d, %"
      GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, rate, format, flags, curtype,
      stoptype, GST_TIME_ARGS (cur), GST_TIME_ARGS (stop));

  if (G_UNLIKELY (format != GST_FORMAT_TIME))
    goto invalid_format;

  /* convert cur */
  ncurtype = GST_SEEK_TYPE_SET;
  if (G_LIKELY ((curtype == GST_SEEK_TYPE_SET)
          && (nle_media_to_object_time (object, cur, &ncur)))) {
    /* cur is TYPE_SET and value is valid */
    if (ncur > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting cur to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (ncur));
  } else if ((curtype != GST_SEEK_TYPE_NONE)) {
    GST_DEBUG_OBJECT (object, "Limiting seek start to start");
    ncur = object->start;
  } else {
    GST_DEBUG_OBJECT (object, "leaving GST_SEEK_TYPE_NONE");
    ncur = cur;
    ncurtype = GST_SEEK_TYPE_NONE;
  }

  /* convert stop, we also need to limit it to object->stop */
  if (G_LIKELY ((stoptype == GST_SEEK_TYPE_SET)
          && (nle_media_to_object_time (object, stop, &nstop)))) {
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  } else {
    /* NOTE: when we have time effects, the object stop is not the
     * correct stop limit. Therefore, the seek stop time should already
     * be set at this point */
    GST_DEBUG_OBJECT (object, "Limiting end of seek to stop");
    nstop = object->stop;
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  }

  GST_DEBUG_OBJECT (object,
      "SENDING SEEK rate:%f, format:TIME, flags:%d, curtype:%d, stoptype:SET, %"
      GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, rate, flags, ncurtype,
      GST_TIME_ARGS (ncur), GST_TIME_ARGS (nstop));

  event2 = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
      ncurtype, (gint64) ncur, GST_SEEK_TYPE_SET, (gint64) nstop);
  GST_EVENT_SEQNUM (event2) = seqnum;

  gst_event_unref (event);

  return event2;

  /* ERRORS */
invalid_format:
  {
    GST_WARNING ("GNonLin time shifting only works with GST_FORMAT_TIME");
    return event;
  }
}

static GstEvent *
translate_outgoing_segment (NleObject * object, GstEvent * event)
{
  const GstSegment *orig;
  GstSegment segment;
  GstEvent *event2;
  guint32 seqnum = GST_EVENT_SEQNUM (event);

  /* only modify the streamtime */
  gst_event_parse_segment (event, &orig);

  GST_DEBUG_OBJECT (object, "Got SEGMENT %" GST_SEGMENT_FORMAT, orig);

  if (G_UNLIKELY (orig->format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "Can't translate segments with format != GST_FORMAT_TIME");
    return event;
  }

  gst_segment_copy_into (orig, &segment);

  nle_media_to_object_time (object, orig->time, &segment.time);

  if (G_UNLIKELY (segment.time > G_MAXINT64))
    GST_WARNING_OBJECT (object, "Return value too big...");

  GST_DEBUG_OBJECT (object, "Sending SEGMENT %" GST_SEGMENT_FORMAT, &segment);

  event2 = gst_event_new_segment (&segment);
  GST_EVENT_SEQNUM (event2) = seqnum;
  gst_event_unref (event);

  return event2;
}

static GstEvent *
translate_incoming_segment (NleObject * object, GstEvent * event)
{
  GstEvent *event2;
  const GstSegment *orig;
  GstSegment segment;
  guint32 seqnum = GST_EVENT_SEQNUM (event);

  /* only modify the streamtime */
  gst_event_parse_segment (event, &orig);

  GST_DEBUG_OBJECT (object,
      "Got SEGMENT %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT " // %"
      GST_TIME_FORMAT, GST_TIME_ARGS (orig->start), GST_TIME_ARGS (orig->stop),
      GST_TIME_ARGS (orig->time));

  if (G_UNLIKELY (orig->format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "Can't translate segments with format != GST_FORMAT_TIME");
    return event;
  }

  gst_segment_copy_into (orig, &segment);

  if (!nle_object_to_media_time (object, orig->time, &segment.time)) {
    GST_DEBUG ("Can't convert media_time, using 0");
    segment.time = 0;
  };

  if (G_UNLIKELY (segment.time > G_MAXINT64))
    GST_WARNING_OBJECT (object, "Return value too big...");

  GST_DEBUG_OBJECT (object,
      "Sending SEGMENT %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT " // %"
      GST_TIME_FORMAT, GST_TIME_ARGS (segment.start),
      GST_TIME_ARGS (segment.stop), GST_TIME_ARGS (segment.time));

  event2 = gst_event_new_segment (&segment);
  GST_EVENT_SEQNUM (event2) = seqnum;
  gst_event_unref (event);

  return event2;
}

/* Retrieve the NleGhostPad that owns this internal proxy pad.
 * Returns a reffed NleGhostPad, or NULL if not an NleGhostPad. */
static NleGhostPad *
get_nle_ghost_pad_from_internal (GstPad * internal)
{
  GstProxyPad *ghost;
  NleGhostPad *nle_ghost = NULL;

  ghost = gst_proxy_pad_get_internal (GST_PROXY_PAD (internal));
  if (ghost) {
    if (NLE_IS_GHOST_PAD (ghost))
      nle_ghost = NLE_GHOST_PAD (ghost);
    else
      gst_object_unref (ghost);
  }

  return nle_ghost;
}

static gboolean
internalpad_event_function (GstPad * internal, GstObject * parent,
    GstEvent * event)
{
  NleGhostPad *nle_ghost;
  NleObject *object;
  gboolean res;

  nle_ghost = get_nle_ghost_pad_from_internal (internal);
  if (G_UNLIKELY (!nle_ghost)) {
    GST_WARNING_OBJECT (internal, "No NleGhostPad owner, pad being disposed");
    gst_event_unref (event);
    return FALSE;
  }

  object = nle_ghost->object;

  GST_DEBUG_OBJECT (internal, "event:%s (seqnum::%d)",
      GST_EVENT_TYPE_NAME (event), GST_EVENT_SEQNUM (event));

  if (G_UNLIKELY (!(nle_ghost->internalpad_eventfunc))) {
    GST_WARNING_OBJECT (internal,
        "internalpad_eventfunc == NULL !! What is going on ?");
    gst_object_unref (nle_ghost);
    return FALSE;
  }

  switch (nle_ghost->dir) {
    case GST_PAD_SRC:{
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEGMENT:
          event = translate_outgoing_segment (object, event);
          break;
        case GST_EVENT_EOS:
          break;
        default:
          break;
      }

      break;
    }
    case GST_PAD_SINK:{
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEEK:
          event = translate_outgoing_seek (object, event);
          break;
        default:
          break;
      }
      break;
    }
    default:
      break;
  }
  GST_DEBUG_OBJECT (internal, "Calling internalpad_eventfunc %p",
      nle_ghost->internalpad_eventfunc);
  res = nle_ghost->internalpad_eventfunc (internal, parent, event);

  gst_object_unref (nle_ghost);

  return res;
}

/*
  translate_outgoing_position_query

  Should only be called:
  _ if the query is a GST_QUERY_POSITION
  _ after the query was sent upstream
  _ if the upstream query returned TRUE
*/

static gboolean
translate_incoming_position_query (NleObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur, cur2;

  gst_query_parse_position (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "position query is in a format different from time, returning without modifying values");
    goto beach;
  }

  nle_media_to_object_time (object, (guint64) cur, (guint64 *) & cur2);

  GST_DEBUG_OBJECT (object,
      "Adjust position from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur), GST_TIME_ARGS (cur2));
  gst_query_set_position (query, GST_FORMAT_TIME, cur2);

beach:
  return TRUE;
}

static gboolean
translate_outgoing_position_query (NleObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur, cur2;

  gst_query_parse_position (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "position query is in a format different from time, returning without modifying values");
    goto beach;
  }

  if (G_UNLIKELY (!(nle_object_to_media_time (object, (guint64) cur,
                  (guint64 *) & cur2)))) {
    GST_WARNING_OBJECT (object,
        "Couldn't get media time for %" GST_TIME_FORMAT, GST_TIME_ARGS (cur));
    goto beach;
  }

  GST_DEBUG_OBJECT (object,
      "Adjust position from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur), GST_TIME_ARGS (cur2));
  gst_query_set_position (query, GST_FORMAT_TIME, cur2);

beach:
  return TRUE;
}

static gboolean
translate_incoming_duration_query (NleObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur;

  gst_query_parse_duration (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "We can only handle duration queries in GST_FORMAT_TIME");
    return FALSE;
  }

  /* NOTE: returns the duration of the object, but this is not the same
   * as the source duration when time effects are used. Nor is it the
   * duration of the current nlecomposition stack */
  gst_query_set_duration (query, GST_FORMAT_TIME, object->duration);

  return TRUE;
}

static gboolean
internalpad_query_function (GstPad * internal, GstObject * parent,
    GstQuery * query)
{
  NleGhostPad *nle_ghost;
  NleObject *object;
  gboolean ret;

  nle_ghost = get_nle_ghost_pad_from_internal (internal);
  if (G_UNLIKELY (!nle_ghost)) {
    GST_WARNING_OBJECT (internal, "No NleGhostPad owner, pad being disposed");
    return FALSE;
  }

  object = nle_ghost->object;

  GST_DEBUG_OBJECT (internal, "querytype:%s",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  if (!(nle_ghost->internalpad_queryfunc)) {
    GST_WARNING_OBJECT (internal,
        "internalpad_queryfunc == NULL !! What is going on ?");
    gst_object_unref (nle_ghost);
    return FALSE;
  }

  if ((ret = nle_ghost->internalpad_queryfunc (internal, parent, query))) {

    switch (nle_ghost->dir) {
      case GST_PAD_SRC:
        break;
      case GST_PAD_SINK:
        switch (GST_QUERY_TYPE (query)) {
          case GST_QUERY_POSITION:
            ret = translate_outgoing_position_query (object, query);
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }

  gst_object_unref (nle_ghost);
  return ret;
}

static gboolean
ghostpad_event_function (GstPad * ghostpad, GstObject * parent,
    GstEvent * event)
{
  NleGhostPad *nle_ghost = NLE_GHOST_PAD (ghostpad);
  NleObject *object = nle_ghost->object;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (ghostpad, "event:%s", GST_EVENT_TYPE_NAME (event));

  if (G_UNLIKELY (nle_ghost->ghostpad_eventfunc == NULL))
    goto no_function;

  switch (nle_ghost->dir) {
    case GST_PAD_SRC:
    {
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEEK:
        {
          GstPad *target;

          event = nle_object_translate_incoming_seek (object, event);
          if (!(target = gst_ghost_pad_get_target (GST_GHOST_PAD (ghostpad)))) {
            g_assert ("Seeked a pad with no target SHOULD NOT HAPPEN");
            ret = FALSE;
            gst_event_unref (event);
            event = NULL;
          } else {
            gst_object_unref (target);
          }
        }
          break;
        default:
          break;
      }
    }
      break;
    case GST_PAD_SINK:{
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEGMENT:
          event = translate_incoming_segment (object, event);
          break;
        default:
          break;
      }
    }
      break;
    default:
      break;
  }

  if (event) {
    GST_DEBUG_OBJECT (ghostpad, "Calling ghostpad_eventfunc");
    ret = nle_ghost->ghostpad_eventfunc (ghostpad, parent, event);
    GST_DEBUG_OBJECT (ghostpad,
        "Returned from calling ghostpad_eventfunc : %d", ret);
  }

  return ret;

  /* ERRORS */
no_function:
  {
    GST_WARNING_OBJECT (ghostpad,
        "ghostpad_eventfunc == NULL !! What's going on ?");
    return FALSE;
  }
}

static gboolean
ghostpad_query_function (GstPad * ghostpad, GstObject * parent,
    GstQuery * query)
{
  NleGhostPad *nle_ghost = NLE_GHOST_PAD (ghostpad);
  NleObject *object = NLE_OBJECT (parent);
  gboolean pret = TRUE;

  GST_DEBUG_OBJECT (ghostpad, "querytype:%s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      /* skip duration upstream query, we'll fill it in ourselves */
      break;
    default:
      pret = nle_ghost->ghostpad_queryfunc (ghostpad, parent, query);
  }

  if (pret) {
    /* translate result */
    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_POSITION:
        pret = translate_incoming_position_query (object, query);
        break;
      case GST_QUERY_DURATION:
        pret = translate_incoming_duration_query (object, query);
        break;
      default:
        break;
    }
  }

  return pret;
}

static inline GstPad *
get_proxy_pad (GstPad * ghostpad)
{
  GValue item = { 0, };
  GstIterator *it;
  GstPad *ret = NULL;

  it = gst_pad_iterate_internal_links (ghostpad);
  g_assert (it);
  gst_iterator_next (it, &item);
  ret = g_value_dup_object (&item);
  g_value_unset (&item);
  g_assert (ret);
  gst_iterator_free (it);

  return ret;
}

static void
control_internal_pad (NleGhostPad * nle_ghost)
{
  GstPad *internal;

  GST_LOG_OBJECT (nle_ghost, "overriding ghostpad's internal pad function");

  internal = get_proxy_pad (GST_PAD (nle_ghost));

  /* Save the original internal pad functions (only once) */
  if (!nle_ghost->internalpad_eventfunc) {
    nle_ghost->internalpad_eventfunc = GST_PAD_EVENTFUNC (internal);
    nle_ghost->internalpad_queryfunc = GST_PAD_QUERYFUNC (internal);

    /* Override with our wrappers */
    gst_pad_set_event_function (internal,
        GST_DEBUG_FUNCPTR (internalpad_event_function));
    gst_pad_set_query_function (internal,
        GST_DEBUG_FUNCPTR (internalpad_query_function));
  }

  gst_object_unref (internal);

  GST_DEBUG_OBJECT (nle_ghost, "Done with pad %s:%s",
      GST_DEBUG_PAD_NAME (nle_ghost));
}


/**
 * nle_object_ghost_pad:
 * @object: #NleObject to add the ghostpad to
 * @name: Name for the new pad
 * @target: Target #GstPad to ghost
 *
 * Adds a #GstGhostPad overridding the correct pad [query|event]_function so
 * that time shifting is done correctly
 * The #GstGhostPad is added to the #NleObject
 *
 * /!\ This function doesn't check if the existing [src|sink] pad was removed
 * first, so you might end up with more pads than wanted
 *
 * Returns: The #GstPad if everything went correctly, else NULL.
 */
GstPad *
nle_object_ghost_pad (NleObject * object, const gchar * name, GstPad * target)
{
  GstPadDirection dir = GST_PAD_DIRECTION (target);
  GstPad *ghost;

  GST_DEBUG_OBJECT (object, "name:%s, target:%p", name, target);

  g_return_val_if_fail (target, FALSE);
  g_return_val_if_fail ((dir != GST_PAD_UNKNOWN), FALSE);

  ghost = nle_object_ghost_pad_no_target (object, name, dir, NULL);
  if (!ghost) {
    GST_WARNING_OBJECT (object, "Couldn't create ghostpad");
    return NULL;
  }

  if (!(nle_object_ghost_pad_set_target (object, ghost, target))) {
    GST_WARNING_OBJECT (object,
        "Couldn't set the target pad... removing ghostpad");
    gst_object_unref (ghost);
    return NULL;
  }

  GST_DEBUG_OBJECT (object, "activating ghostpad");
  /* activate pad */
  gst_pad_set_active (ghost, TRUE);
  /* add it to element */
  if (!(gst_element_add_pad (GST_ELEMENT (object), ghost))) {
    GST_WARNING ("couldn't add newly created ghostpad");
    return NULL;
  }

  return ghost;
}

/*
 * nle_object_ghost_pad_no_target:
 * /!\ Doesn't add the pad to the NleObject....
 */
GstPad *
nle_object_ghost_pad_no_target (NleObject * object, const gchar * name,
    GstPadDirection dir, GstPadTemplate * template)
{
  NleGhostPad *nle_ghost;

  /* create a no_target ghostpad using our NleGhostPad subclass */
  if (template)
    nle_ghost = g_object_new (NLE_TYPE_GHOST_PAD, "name", name,
        "direction", dir, "template", template, NULL);
  else
    nle_ghost = g_object_new (NLE_TYPE_GHOST_PAD, "name", name,
        "direction", dir, NULL);

  if (!nle_ghost)
    return NULL;

  nle_ghost->dir = dir;
  nle_ghost->object = object;

  /* Save and replace event/query functions */
  GST_DEBUG_OBJECT (nle_ghost, "Setting ghostpad_eventfunc to %p",
      GST_PAD_EVENTFUNC (nle_ghost));
  nle_ghost->ghostpad_eventfunc = GST_PAD_EVENTFUNC (nle_ghost);
  nle_ghost->ghostpad_queryfunc = GST_PAD_QUERYFUNC (nle_ghost);

  gst_pad_set_event_function (GST_PAD (nle_ghost),
      GST_DEBUG_FUNCPTR (ghostpad_event_function));
  gst_pad_set_query_function (GST_PAD (nle_ghost),
      GST_DEBUG_FUNCPTR (ghostpad_query_function));

  control_internal_pad (nle_ghost);

  return GST_PAD (nle_ghost);
}



void
nle_object_remove_ghost_pad (NleObject * object, GstPad * ghost)
{
  GST_DEBUG_OBJECT (object, "ghostpad %s:%s", GST_DEBUG_PAD_NAME (ghost));

  gst_ghost_pad_set_target (GST_GHOST_PAD (ghost), NULL);
  gst_element_remove_pad (GST_ELEMENT (object), ghost);
}

gboolean
nle_object_ghost_pad_set_target (NleObject * object, GstPad * ghost,
    GstPad * target)
{
  NleGhostPad *nle_ghost;

  g_return_val_if_fail (NLE_IS_GHOST_PAD (ghost), FALSE);

  nle_ghost = NLE_GHOST_PAD (ghost);

  if (target) {
    GST_DEBUG_OBJECT (object, "setting target %s:%s on %s:%s",
        GST_DEBUG_PAD_NAME (target), GST_DEBUG_PAD_NAME (ghost));
  } else {
    GST_DEBUG_OBJECT (object, "removing target from ghostpad");
    nle_ghost->pending_seek = NULL;
  }

  /* set target */
  if (!(gst_ghost_pad_set_target (GST_GHOST_PAD (ghost), target))) {
    GST_WARNING_OBJECT (nle_ghost->object, "Could not set ghost %s:%s "
        "target to: %s:%s", GST_DEBUG_PAD_NAME (ghost),
        GST_DEBUG_PAD_NAME (target));
    return FALSE;
  }

  if (target && nle_ghost->pending_seek) {
    gboolean res = gst_pad_send_event (ghost, nle_ghost->pending_seek);

    GST_INFO_OBJECT (object, "Sending our pending seek event: %" GST_PTR_FORMAT
        " -- Result is %i", nle_ghost->pending_seek, res);

    nle_ghost->pending_seek = NULL;
  }

  return TRUE;
}

void
nle_init_ghostpad_category (void)
{
  GST_DEBUG_CATEGORY_INIT (nleghostpad, "nleghostpad",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin GhostPad");

}
