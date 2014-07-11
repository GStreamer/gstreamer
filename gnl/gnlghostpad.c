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

#include "gnl.h"

GST_DEBUG_CATEGORY_STATIC (gnlghostpad);
#define GST_CAT_DEFAULT gnlghostpad

typedef struct _GnlPadPrivate GnlPadPrivate;

struct _GnlPadPrivate
{
  GnlObject *object;
  GnlPadPrivate *ghostpriv;
  GstPadDirection dir;
  GstPadEventFunction eventfunc;
  GstPadQueryFunction queryfunc;

  GstEvent *pending_seek;
};

GstEvent *
gnl_object_translate_incoming_seek (GnlObject * object, GstEvent * event)
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
          && (gnl_object_to_media_time (object, cur, &ncur)))) {
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
          && (gnl_object_to_media_time (object, stop, &nstop)))) {
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  } else {
    GST_DEBUG_OBJECT (object, "Limiting end of seek to media_stop");
    gnl_object_to_media_time (object, object->stop, &nstop);
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

  return event2;

  /* ERRORS */
invalid_format:
  {
    GST_WARNING ("GNonLin time shifting only works with GST_FORMAT_TIME");
    return event;
  }
}

static GstEvent *
translate_outgoing_seek (GnlObject * object, GstEvent * event)
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
          && (gnl_media_to_object_time (object, cur, &ncur)))) {
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
          && (gnl_media_to_object_time (object, stop, &nstop)))) {
    if (nstop > G_MAXINT64)
      GST_WARNING_OBJECT (object, "return value too big...");
    GST_LOG_OBJECT (object, "Setting stop to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (nstop));
  } else {
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
translate_outgoing_segment (GnlObject * object, GstEvent * event)
{
  const GstSegment *orig;
  GstSegment segment;
  GstEvent *event2;
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

  gnl_media_to_object_time (object, orig->time, &segment.time);

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

static GstEvent *
translate_incoming_segment (GnlObject * object, GstEvent * event)
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

  if (!gnl_object_to_media_time (object, orig->time, &segment.time)) {
    GST_DEBUG ("Can't convert media_time, using 0");
    segment.time = 0;
  };

  if (GNL_IS_OPERATION (object)) {
    segment.base = GNL_OPERATION (object)->next_base_time;
    GST_INFO_OBJECT (object, "Using operation base time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GNL_OPERATION (object)->next_base_time));
  }

  if (G_UNLIKELY (segment.time > G_MAXINT64))
    GST_WARNING_OBJECT (object, "Return value too big...");

  GST_DEBUG_OBJECT (object,
      "Sending SEGMENT %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT " // %"
      GST_TIME_FORMAT, GST_TIME_ARGS (segment.start),
      GST_TIME_ARGS (segment.stop), GST_TIME_ARGS (segment.time));

  event2 = gst_event_new_segment (&segment);
  GST_EVENT_SEQNUM (event2) = seqnum;
  gst_event_unref (event);

  if (object->seqnum) {
    GST_INFO_OBJECT (object, "Tweaking SEGMENT seqnum from %i to %i",
        gst_event_get_seqnum (event2), object->seqnum);
    gst_event_set_seqnum (event2, object->seqnum);
  }

  return event2;
}

static gboolean
internalpad_event_function (GstPad * internal, GstObject * parent,
    GstEvent * event)
{
  GnlPadPrivate *priv = gst_pad_get_element_private (internal);
  GnlObject *object = priv->object;
  gboolean res;

  GST_DEBUG_OBJECT (internal, "event:%s (seqnum::%d)",
      GST_EVENT_TYPE_NAME (event), GST_EVENT_SEQNUM (event));

  if (G_UNLIKELY (!(priv->eventfunc))) {
    GST_WARNING_OBJECT (internal,
        "priv->eventfunc == NULL !! What is going on ?");
    return FALSE;
  }

  switch (priv->dir) {
    case GST_PAD_SRC:{
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEEK:
          object->wanted_seqnum = gst_event_get_seqnum (event);
          object->seqnum = 0;
          GST_DEBUG_OBJECT (object, "Setting wanted_seqnum to %i",
              object->wanted_seqnum);
          break;
        case GST_EVENT_SEGMENT:
          if (object->wanted_seqnum == 0) {
            g_assert ("All gnlobject should be seeked at one point or another"
                " and thus we should always have a wanted_seqnum when getting"
                " a new segment" == NULL);
          }

          GST_DEBUG_OBJECT (object, "Got segment, seqnum-> %i (wanted %i)",
              gst_event_get_seqnum (event), object->wanted_seqnum);

          object->seqnum = object->wanted_seqnum;
          object->wanted_seqnum = 0;

          event = translate_outgoing_segment (object, event);
          if (object->seqnum) {
            GST_INFO_OBJECT (object, "Tweaking SEGMENT seqnum from %i to %i",
                gst_event_get_seqnum (event), object->seqnum);
            gst_event_set_seqnum (event, object->seqnum);
          }
          break;
        case GST_EVENT_EOS:
          if (object->seqnum) {
            GST_INFO_OBJECT (object, "Tweaking EOS seqnum from %i to %i",
                gst_event_get_seqnum (event), object->seqnum);
            gst_event_set_seqnum (event, object->seqnum);
          }
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
  GST_DEBUG_OBJECT (internal, "Calling priv->eventfunc %p", priv->eventfunc);
  res = priv->eventfunc (internal, parent, event);

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
translate_incoming_position_query (GnlObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur, cur2;

  gst_query_parse_position (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "position query is in a format different from time, returning without modifying values");
    goto beach;
  }

  gnl_media_to_object_time (object, (guint64) cur, (guint64 *) & cur2);

  GST_DEBUG_OBJECT (object,
      "Adjust position from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur), GST_TIME_ARGS (cur2));
  gst_query_set_position (query, GST_FORMAT_TIME, cur2);

beach:
  return TRUE;
}

static gboolean
translate_outgoing_position_query (GnlObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur, cur2;

  gst_query_parse_position (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "position query is in a format different from time, returning without modifying values");
    goto beach;
  }

  if (G_UNLIKELY (!(gnl_object_to_media_time (object, (guint64) cur,
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
translate_incoming_duration_query (GnlObject * object, GstQuery * query)
{
  GstFormat format;
  gint64 cur;

  gst_query_parse_duration (query, &format, &cur);
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_WARNING_OBJECT (object,
        "We can only handle duration queries in GST_FORMAT_TIME");
    return FALSE;
  }

  gst_query_set_duration (query, GST_FORMAT_TIME, object->duration);

  return TRUE;
}

static gboolean
internalpad_query_function (GstPad * internal, GstObject * parent,
    GstQuery * query)
{
  GnlPadPrivate *priv = gst_pad_get_element_private (internal);
  GnlObject *object = priv->object;
  gboolean ret;

  GST_DEBUG_OBJECT (internal, "querytype:%s",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  if (!(priv->queryfunc)) {
    GST_WARNING_OBJECT (internal,
        "priv->queryfunc == NULL !! What is going on ?");
    return FALSE;
  }

  if ((ret = priv->queryfunc (internal, parent, query))) {

    switch (priv->dir) {
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
  return ret;
}

static gboolean
ghostpad_event_function (GstPad * ghostpad, GstObject * parent,
    GstEvent * event)
{
  GnlPadPrivate *priv;
  GnlObject *object;
  gboolean ret = FALSE;

  priv = gst_pad_get_element_private (ghostpad);
  object = priv->object;

  GST_DEBUG_OBJECT (ghostpad, "event:%s", GST_EVENT_TYPE_NAME (event));

  if (G_UNLIKELY (priv->eventfunc == NULL))
    goto no_function;

  switch (priv->dir) {
    case GST_PAD_SRC:
    {
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_SEEK:
        {
          GstPad *target;

          event = gnl_object_translate_incoming_seek (object, event);
          object->wanted_seqnum = gst_event_get_seqnum (event);
          object->seqnum = 0;
          if (!(target = gst_ghost_pad_get_target (GST_GHOST_PAD (ghostpad)))) {
            g_assert ("Seeked a pad with not target SHOULD NOT HAPPEND");
            ret = FALSE;
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
    GST_DEBUG_OBJECT (ghostpad, "Calling priv->eventfunc");
    ret = priv->eventfunc (ghostpad, parent, event);
    GST_DEBUG_OBJECT (ghostpad, "Returned from calling priv->eventfunc : %d",
        ret);
  }

  return ret;

  /* ERRORS */
no_function:
  {
    GST_WARNING_OBJECT (ghostpad,
        "priv->eventfunc == NULL !! What's going on ?");
    return FALSE;
  }
}

static gboolean
ghostpad_query_function (GstPad * ghostpad, GstObject * parent,
    GstQuery * query)
{
  GnlPadPrivate *priv = gst_pad_get_element_private (ghostpad);
  GnlObject *object = GNL_OBJECT (parent);
  gboolean pret = TRUE;

  GST_DEBUG_OBJECT (ghostpad, "querytype:%s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      /* skip duration upstream query, we'll fill it in ourselves */
      break;
    default:
      pret = priv->queryfunc (ghostpad, parent, query);
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

/* internal pad going away */
static void
internal_pad_finalizing (GnlPadPrivate * priv, GObject * pad G_GNUC_UNUSED)
{
  g_slice_free (GnlPadPrivate, priv);
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
control_internal_pad (GstPad * ghostpad, GnlObject * object)
{
  GnlPadPrivate *priv;
  GnlPadPrivate *privghost;
  GstPad *internal;

  if (!ghostpad) {
    GST_DEBUG_OBJECT (object, "We don't have a valid ghostpad !");
    return;
  }
  privghost = gst_pad_get_element_private (ghostpad);

  GST_LOG_OBJECT (ghostpad, "overriding ghostpad's internal pad function");

  internal = get_proxy_pad (ghostpad);

  if (G_UNLIKELY (!(priv = gst_pad_get_element_private (internal)))) {
    GST_DEBUG_OBJECT (internal,
        "Creating a GnlPadPrivate to put in element_private");
    priv = g_slice_new0 (GnlPadPrivate);

    /* Remember existing pad functions */
    priv->eventfunc = GST_PAD_EVENTFUNC (internal);
    priv->queryfunc = GST_PAD_QUERYFUNC (internal);
    gst_pad_set_element_private (internal, priv);

    g_object_weak_ref ((GObject *) internal,
        (GWeakNotify) internal_pad_finalizing, priv);

    /* add query/event function overrides on internal pad */
    gst_pad_set_event_function (internal,
        GST_DEBUG_FUNCPTR (internalpad_event_function));
    gst_pad_set_query_function (internal,
        GST_DEBUG_FUNCPTR (internalpad_query_function));
  }

  priv->object = object;
  priv->ghostpriv = privghost;
  priv->dir = GST_PAD_DIRECTION (ghostpad);
  gst_object_unref (internal);

  GST_DEBUG_OBJECT (ghostpad, "Done with pad %s:%s",
      GST_DEBUG_PAD_NAME (ghostpad));
}


/**
 * gnl_object_ghost_pad:
 * @object: #GnlObject to add the ghostpad to
 * @name: Name for the new pad
 * @target: Target #GstPad to ghost
 *
 * Adds a #GstGhostPad overridding the correct pad [query|event]_function so
 * that time shifting is done correctly
 * The #GstGhostPad is added to the #GnlObject
 *
 * /!\ This function doesn't check if the existing [src|sink] pad was removed
 * first, so you might end up with more pads than wanted
 *
 * Returns: The #GstPad if everything went correctly, else NULL.
 */
GstPad *
gnl_object_ghost_pad (GnlObject * object, const gchar * name, GstPad * target)
{
  GstPadDirection dir = GST_PAD_DIRECTION (target);
  GstPad *ghost;

  GST_DEBUG_OBJECT (object, "name:%s, target:%p", name, target);

  g_return_val_if_fail (target, FALSE);
  g_return_val_if_fail ((dir != GST_PAD_UNKNOWN), FALSE);

  ghost = gnl_object_ghost_pad_no_target (object, name, dir, NULL);
  if (!ghost) {
    GST_WARNING_OBJECT (object, "Couldn't create ghostpad");
    return NULL;
  }

  if (!(gnl_object_ghost_pad_set_target (object, ghost, target))) {
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
 * gnl_object_ghost_pad_no_target:
 * /!\ Doesn't add the pad to the GnlObject....
 */
GstPad *
gnl_object_ghost_pad_no_target (GnlObject * object, const gchar * name,
    GstPadDirection dir, GstPadTemplate * template)
{
  GstPad *ghost;
  GnlPadPrivate *priv;

  /* create a no_target ghostpad */
  if (template)
    ghost = gst_ghost_pad_new_no_target_from_template (name, template);
  else
    ghost = gst_ghost_pad_new_no_target (name, dir);
  if (!ghost)
    return NULL;


  /* remember the existing ghostpad event/query/link/unlink functions */
  priv = g_slice_new0 (GnlPadPrivate);
  priv->dir = dir;
  priv->object = object;

  /* grab/replace event/query functions */
  GST_DEBUG_OBJECT (ghost, "Setting priv->eventfunc to %p",
      GST_PAD_EVENTFUNC (ghost));
  priv->eventfunc = GST_PAD_EVENTFUNC (ghost);
  priv->queryfunc = GST_PAD_QUERYFUNC (ghost);

  gst_pad_set_event_function (ghost,
      GST_DEBUG_FUNCPTR (ghostpad_event_function));
  gst_pad_set_query_function (ghost,
      GST_DEBUG_FUNCPTR (ghostpad_query_function));

  gst_pad_set_element_private (ghost, priv);
  control_internal_pad (ghost, object);

  return ghost;
}



void
gnl_object_remove_ghost_pad (GnlObject * object, GstPad * ghost)
{
  GnlPadPrivate *priv;

  GST_DEBUG_OBJECT (object, "ghostpad %s:%s", GST_DEBUG_PAD_NAME (ghost));

  priv = gst_pad_get_element_private (ghost);
  gst_ghost_pad_set_target (GST_GHOST_PAD (ghost), NULL);
  gst_element_remove_pad (GST_ELEMENT (object), ghost);
  if (priv)
    g_slice_free (GnlPadPrivate, priv);
}

gboolean
gnl_object_ghost_pad_set_target (GnlObject * object, GstPad * ghost,
    GstPad * target)
{
  GnlPadPrivate *priv = gst_pad_get_element_private (ghost);

  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (GST_IS_PAD (ghost), FALSE);

  if (target) {
    GST_DEBUG_OBJECT (object, "setting target %s:%s on %s:%s",
        GST_DEBUG_PAD_NAME (target), GST_DEBUG_PAD_NAME (ghost));
  } else {
    GST_DEBUG_OBJECT (object, "removing target from ghostpad");
    priv->pending_seek = NULL;
  }

  /* set target */
  if (!(gst_ghost_pad_set_target (GST_GHOST_PAD (ghost), target))) {
    GST_WARNING_OBJECT (priv->object, "Could not set ghost %s:%s "
        "target to: %s:%s", GST_DEBUG_PAD_NAME (ghost),
        GST_DEBUG_PAD_NAME (target));
    return FALSE;
  }

  if (target && priv->pending_seek) {
    gboolean res = gst_pad_send_event (ghost, priv->pending_seek);

    GST_INFO_OBJECT (object, "Sending our pending seek event: %" GST_PTR_FORMAT
        " -- Result is %i", priv->pending_seek, res);

    priv->pending_seek = NULL;
  }

  return TRUE;
}

void
gnl_init_ghostpad_category (void)
{
  GST_DEBUG_CATEGORY_INIT (gnlghostpad, "gnlghostpad",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin GhostPad");

}
