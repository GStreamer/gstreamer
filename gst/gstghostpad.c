/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Andy Wingo <wingo@pobox.com>
 *
 * gstghostpad.c: Proxy pads
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

/**
 * SECTION:gstghostpad
 * @short_description: Pseudo link pads
 * @see_also: #GstPad
 *
 * GhostPads are useful when organizing pipelines with #GstBin like elements.
 * The idea here is to create hierarchical element graphs. The bin element
 * contains a sub-graph. Now one would like to treat the bin-element like other
 * #GstElements. This is where GhostPads come into play. A GhostPad acts as a
 * proxy for another pad. Thus the bin can have sink and source ghost-pads that
 * are accociated with sink and source pads of the child elements.
 *
 * If the target pad is known at creation time, gst_ghost_pad_new() is the
 * function to use to get a ghost-pad. Otherwise one can use gst_ghost_pad_new_no_target()
 * to create the ghost-pad and use gst_ghost_pad_set_target() to establish the
 * accociation later on.
 *
 * Last reviewed on 2005-11-18 (0.9.5)
 */

#include "gst_private.h"

#include "gstghostpad.h"

#define GST_TYPE_PROXY_PAD		(gst_proxy_pad_get_type ())
#define GST_IS_PROXY_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PROXY_PAD))
#define GST_IS_PROXY_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PROXY_PAD))
#define GST_PROXY_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PROXY_PAD, GstProxyPad))
#define GST_PROXY_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PROXY_PAD, GstProxyPadClass))
#define GST_PROXY_PAD_TARGET(pad)	(GST_PROXY_PAD (pad)->target)


typedef struct _GstProxyPad GstProxyPad;
typedef struct _GstProxyPadClass GstProxyPadClass;

#define GST_PROXY_GET_LOCK(pad)	(GST_PROXY_PAD (pad)->proxy_lock)
#define GST_PROXY_LOCK(pad)	(g_mutex_lock (GST_PROXY_GET_LOCK (pad)))
#define GST_PROXY_UNLOCK(pad)	(g_mutex_unlock (GST_PROXY_GET_LOCK (pad)))

struct _GstProxyPad
{
  GstPad pad;

  /* with PROXY_LOCK */
  GMutex *proxy_lock;
  GstPad *target;

  /*< private > */
  gpointer _gst_reserved[1];
};

struct _GstProxyPadClass
{
  GstPadClass parent_class;

  /*< private > */
  gpointer _gst_reserved[1];
};


G_DEFINE_TYPE (GstProxyPad, gst_proxy_pad, GST_TYPE_PAD);

static GstPad *gst_proxy_pad_get_target (GstPad * pad);

static void gst_proxy_pad_dispose (GObject * object);
static void gst_proxy_pad_finalize (GObject * object);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_proxy_pad_save_thyself (GstObject * object,
    xmlNodePtr parent);
#endif


static void
gst_proxy_pad_class_init (GstProxyPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_proxy_pad_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_proxy_pad_finalize);

#ifndef GST_DISABLE_LOADSAVE
  {
    GstObjectClass *gstobject_class = (GstObjectClass *) klass;

    gstobject_class->save_thyself =
        GST_DEBUG_FUNCPTR (gst_proxy_pad_save_thyself);
  }
#endif
}

const GstQueryType *
gst_proxy_pad_do_query_type (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  const GstQueryType *res;

  g_return_val_if_fail (target != NULL, NULL);

  res = gst_pad_get_query_types (target);
  gst_object_unref (target);

  return res;
}

static gboolean
gst_proxy_pad_do_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstPad *target = gst_proxy_pad_get_target (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  res = gst_pad_send_event (target, event);
  gst_object_unref (target);

  return res;
}

static gboolean
gst_proxy_pad_do_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstPad *target = gst_proxy_pad_get_target (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  res = gst_pad_query (target, query);
  gst_object_unref (target);

  return res;
}

static GList *
gst_proxy_pad_do_internal_link (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  GList *res;

  g_return_val_if_fail (target != NULL, NULL);

  res = gst_pad_get_internal_links (target);
  gst_object_unref (target);

  return res;
}

static GstFlowReturn
gst_proxy_pad_do_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstFlowReturn result;
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstPad *peer;

  g_return_val_if_fail (target != NULL, GST_FLOW_NOT_LINKED);

  peer = gst_pad_get_peer (target);
  if (peer) {
    GST_DEBUG ("buffer alloc on %s:%s", GST_DEBUG_PAD_NAME (target));

    result = gst_pad_alloc_buffer (peer, offset, size, caps, buf);

    gst_object_unref (peer);
  } else {
    result = GST_FLOW_NOT_LINKED;
  }

  gst_object_unref (target);

  return result;
}

static GstFlowReturn
gst_proxy_pad_do_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstFlowReturn res;

  g_return_val_if_fail (target != NULL, GST_FLOW_NOT_LINKED);

  res = gst_pad_chain (target, buffer);
  gst_object_unref (target);

  return res;
}

static GstFlowReturn
gst_proxy_pad_do_getrange (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstFlowReturn res;

  g_return_val_if_fail (target != NULL, GST_FLOW_NOT_LINKED);

  res = gst_pad_get_range (target, offset, size, buffer);
  gst_object_unref (target);

  return res;
}

static gboolean
gst_proxy_pad_do_checkgetrange (GstPad * pad)
{
  gboolean result;
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstPad *peer;

  g_return_val_if_fail (target != NULL, FALSE);

  peer = gst_pad_get_peer (target);
  if (peer) {
    result = gst_pad_check_pull_range (peer);
    gst_object_unref (peer);
  } else {
    result = FALSE;
  }
  gst_object_unref (target);

  return result;
}

static GstCaps *
gst_proxy_pad_do_getcaps (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstCaps *res;

  g_return_val_if_fail (target != NULL, NULL);

  res = gst_pad_get_caps (target);
  gst_object_unref (target);

  return res;
}

static gboolean
gst_proxy_pad_do_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  gboolean res;

  g_return_val_if_fail (target != NULL, FALSE);

  res = gst_pad_accept_caps (target, caps);
  gst_object_unref (target);

  return res;
}

static void
gst_proxy_pad_do_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);

  g_return_if_fail (target != NULL);

  gst_pad_fixate_caps (target, caps);
  gst_object_unref (target);
}

static gboolean
gst_proxy_pad_do_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  gboolean res;

  g_return_val_if_fail (target != NULL, FALSE);

  res = gst_pad_set_caps (target, caps);
  gst_object_unref (target);

  return res;
}

#define SETFUNC(member, kind) \
  if (target->member) \
    gst_pad_set_##kind##_function (pad, gst_proxy_pad_do_##kind)

static gboolean
gst_proxy_pad_set_target_unlocked (GstPad * pad, GstPad * target)
{
  GstPad *oldtarget;

  GST_DEBUG ("set target %s:%s on %s:%s",
      GST_DEBUG_PAD_NAME (target), GST_DEBUG_PAD_NAME (pad));

  /* clear old target */
  if ((oldtarget = GST_PROXY_PAD_TARGET (pad))) {
    gst_object_unref (oldtarget);
    GST_PROXY_PAD_TARGET (pad) = NULL;
  }

  if (target) {
    /* set and ref new target if any */
    GST_PROXY_PAD_TARGET (pad) = gst_object_ref (target);

    /* really, all these should have default implementations so I can set them
     * in the _init() instead of here */
    SETFUNC (querytypefunc, query_type);
    SETFUNC (eventfunc, event);
    SETFUNC (queryfunc, query);
    SETFUNC (intlinkfunc, internal_link);
    SETFUNC (getcapsfunc, getcaps);
    SETFUNC (acceptcapsfunc, acceptcaps);
    SETFUNC (fixatecapsfunc, fixatecaps);
    SETFUNC (setcapsfunc, setcaps);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
      SETFUNC (bufferallocfunc, bufferalloc);
      SETFUNC (chainfunc, chain);
    } else {
      SETFUNC (getrangefunc, getrange);
      SETFUNC (checkgetrangefunc, checkgetrange);
    }
  }
  return TRUE;
}

static gboolean
gst_proxy_pad_set_target (GstPad * pad, GstPad * target)
{
  gboolean result;

  GST_PROXY_LOCK (pad);
  result = gst_proxy_pad_set_target_unlocked (pad, target);
  GST_PROXY_UNLOCK (pad);

  return result;
}

static GstPad *
gst_proxy_pad_get_target (GstPad * pad)
{
  GstPad *target;

  GST_PROXY_LOCK (pad);
  target = GST_PROXY_PAD_TARGET (pad);
  if (target)
    gst_object_ref (target);
  GST_PROXY_UNLOCK (pad);

  return target;
}

static void
gst_proxy_pad_init (GstProxyPad * pad)
{
  pad->proxy_lock = g_mutex_new ();
}

static void
gst_proxy_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  GST_PROXY_LOCK (pad);
  gst_object_replace ((GstObject **) & GST_PROXY_PAD_TARGET (pad), NULL);
  GST_PROXY_UNLOCK (pad);

  G_OBJECT_CLASS (gst_proxy_pad_parent_class)->dispose (object);
}

static void
gst_proxy_pad_finalize (GObject * object)
{
  GstProxyPad *pad = GST_PROXY_PAD (object);

  g_mutex_free (pad->proxy_lock);
  pad->proxy_lock = NULL;

  G_OBJECT_CLASS (gst_proxy_pad_parent_class)->finalize (object);
}

#ifndef GST_DISABLE_LOADSAVE
/**
 * gst_proxy_pad_save_thyself:
 * @pad: a ghost #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
static xmlNodePtr
gst_proxy_pad_save_thyself (GstObject * object, xmlNodePtr parent)
{
  xmlNodePtr self;

  g_return_val_if_fail (GST_IS_PROXY_PAD (object), NULL);

  self = xmlNewChild (parent, NULL, (xmlChar *) "ghostpad", NULL);
  xmlNewChild (self, NULL, (xmlChar *) "name",
      (xmlChar *) GST_OBJECT_NAME (object));
  xmlNewChild (self, NULL, (xmlChar *) "parent",
      (xmlChar *) GST_OBJECT_NAME (GST_OBJECT_PARENT (object)));

  /* FIXME FIXME FIXME! */

  return self;
}
#endif /* GST_DISABLE_LOADSAVE */


/***********************************************************************
 * Ghost pads, implemented as a pair of proxy pads (sort of)
 */


struct _GstGhostPad
{
  GstProxyPad pad;

  /* with PROXY_LOCK */
  GstPad *internal;
  gulong notify_id;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstGhostPadClass
{
  GstProxyPadClass parent_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};


G_DEFINE_TYPE (GstGhostPad, gst_ghost_pad, GST_TYPE_PROXY_PAD);

static gboolean gst_ghost_pad_set_internal (GstGhostPad * pad,
    GstPad * internal);

static void gst_ghost_pad_dispose (GObject * object);

/* Work around g_logv's use of G_GNUC_PRINTF because gcc chokes on %P, which we
 * use for GST_PTR_FORMAT. */
static void
gst_critical (const gchar * format, ...)
{
  va_list args;

  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, format, args);
  va_end (args);
}

static GstPad *
gst_ghost_pad_get_internal (GstPad * pad)
{
  GstPad *internal;

  GST_PROXY_LOCK (pad);
  internal = GST_GHOST_PAD (pad)->internal;
  if (internal)
    gst_object_ref (internal);
  GST_PROXY_UNLOCK (pad);

  return internal;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ghost_pad_dispose);
}

/* see gstghostpad design docs */
static gboolean
gst_ghost_pad_internal_do_activate_push (GstPad * pad, gboolean active)
{
  gboolean ret;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    GstPad *parent = GST_PAD (gst_object_get_parent (GST_OBJECT (pad)));

    if (parent) {
      g_return_val_if_fail (GST_IS_GHOST_PAD (parent), FALSE);

      ret = gst_pad_activate_push (parent, active);

      gst_object_unref (parent);
    } else {
      ret = FALSE;
    }
  } else {
    GstPad *peer = gst_pad_get_peer (pad);

    if (peer) {
      ret = gst_pad_activate_push (peer, active);
      gst_object_unref (peer);
    } else {
      ret = FALSE;
    }
  }

  return ret;
}

static gboolean
gst_ghost_pad_internal_do_activate_pull (GstPad * pad, gboolean active)
{
  gboolean ret;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    GstPad *peer = gst_pad_get_peer (pad);

    if (peer) {
      ret = gst_pad_activate_pull (peer, active);
      gst_object_unref (peer);
    } else {
      ret = FALSE;
    }
  } else {
    GstPad *parent = GST_PAD (gst_object_get_parent (GST_OBJECT (pad)));

    if (parent) {
      g_return_val_if_fail (GST_IS_GHOST_PAD (parent), FALSE);

      ret = gst_pad_activate_pull (parent, active);

      gst_object_unref (parent);
    } else {
      ret = FALSE;
    }
  }

  return ret;
}

static gboolean
gst_ghost_pad_do_activate_push (GstPad * pad, gboolean active)
{
  gboolean ret;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    GstPad *internal = gst_ghost_pad_get_internal (pad);

    if (internal) {
      ret = gst_pad_activate_push (internal, active);
      gst_object_unref (internal);
    } else {
      ret = TRUE;
    }
  } else {
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_ghost_pad_do_activate_pull (GstPad * pad, gboolean active)
{
  gboolean ret;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    GstPad *peer = gst_pad_get_peer (pad);

    if (peer) {
      ret = gst_pad_activate_pull (peer, active);
      gst_object_unref (peer);
    } else {
      ret = FALSE;
    }
  } else {
    GstPad *internal = gst_ghost_pad_get_internal (pad);

    if (internal) {
      ret = gst_pad_activate_pull (internal, active);
      gst_object_unref (internal);
    } else {
      ret = FALSE;
    }
  }

  return ret;
}

static GstPadLinkReturn
gst_ghost_pad_do_link (GstPad * pad, GstPad * peer)
{
  GstPad *internal, *target;
  GstPadLinkReturn ret;

  target = gst_proxy_pad_get_target (pad);

  g_return_val_if_fail (target != NULL, GST_PAD_LINK_NOSCHED);

  /* proxy the peer into the bin */
  internal = g_object_new (GST_TYPE_PROXY_PAD,
      "name", NULL,
      "direction", GST_PAD_DIRECTION (peer),
      "template", GST_PAD_PAD_TEMPLATE (peer), NULL);

  gst_proxy_pad_set_target (internal, peer);
  gst_ghost_pad_set_internal (GST_GHOST_PAD (pad), internal);

  if (GST_PAD_IS_SRC (internal))
    ret = gst_pad_link (internal, target);
  else
    ret = gst_pad_link (target, internal);

  /* if we are a source pad, we should call the peer link function
   * if the peer has one */
  if (GST_PAD_IS_SRC (pad)) {
    if (GST_PAD_LINKFUNC (peer) && ret == GST_PAD_LINK_OK)
      ret = GST_PAD_LINKFUNC (peer) (peer, pad);
  }

  gst_object_unref (target);

  if (ret == GST_PAD_LINK_OK)
    gst_pad_set_active (internal, GST_PAD_ACTIVATE_MODE (pad));
  else
    gst_ghost_pad_set_internal (GST_GHOST_PAD (pad), NULL);

  return ret;
}

static void
gst_ghost_pad_do_unlink (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);

  g_return_if_fail (target != NULL);

  GST_DEBUG_OBJECT (pad, "unlinking ghostpad");

  if (target->unlinkfunc)
    target->unlinkfunc (target);

  gst_ghost_pad_set_internal (GST_GHOST_PAD (pad), NULL);

  gst_object_unref (target);
}

static void
on_int_notify (GstPad * internal, GParamSpec * unused, GstGhostPad * pad)
{
  GstCaps *caps;

  g_object_get (internal, "caps", &caps, NULL);

  GST_OBJECT_LOCK (pad);
  gst_caps_replace (&(GST_PAD_CAPS (pad)), caps);
  GST_OBJECT_UNLOCK (pad);

  g_object_notify (G_OBJECT (pad), "caps");
  if (caps)
    gst_caps_unref (caps);
}

static gboolean
gst_ghost_pad_set_internal (GstGhostPad * pad, GstPad * internal)
{
  GST_PROXY_LOCK (pad);
  /* first remove old internal pad */
  if (pad->internal) {
    GstPad *intpeer;

    gst_pad_set_activatepull_function (pad->internal, NULL);
    gst_pad_set_activatepush_function (pad->internal, NULL);

    g_signal_handler_disconnect (pad->internal, pad->notify_id);

    intpeer = gst_pad_get_peer (pad->internal);
    if (intpeer) {
      if (GST_PAD_IS_SRC (pad->internal))
        gst_pad_unlink (pad->internal, intpeer);
      else
        gst_pad_unlink (intpeer, pad->internal);
      gst_object_unref (intpeer);
    }
    /* should dispose it */
    gst_object_unparent (GST_OBJECT_CAST (pad->internal));
  }

  /* then set new internal pad */
  if (internal) {
    if (!gst_object_set_parent (GST_OBJECT_CAST (internal),
            GST_OBJECT_CAST (pad)))
      goto could_not_set;

    /* could be more general here, iterating over all writable properties...
     * taking the short road for now tho */
    pad->notify_id = g_signal_connect (internal, "notify::caps",
        G_CALLBACK (on_int_notify), pad);
    on_int_notify (internal, NULL, pad);
    gst_pad_set_activatepull_function (GST_PAD (internal),
        GST_DEBUG_FUNCPTR (gst_ghost_pad_internal_do_activate_pull));
    gst_pad_set_activatepush_function (GST_PAD (internal),
        GST_DEBUG_FUNCPTR (gst_ghost_pad_internal_do_activate_push));
    /* a ref was taken by set_parent */
  }
  pad->internal = internal;

  GST_PROXY_UNLOCK (pad);

  return TRUE;

  /* ERRORS */
could_not_set:
  {
    gst_critical ("Could not set internal pad %" GST_PTR_FORMAT, internal);
    GST_PROXY_UNLOCK (pad);
    return FALSE;
  }
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  gst_pad_set_activatepull_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_activate_pull));
  gst_pad_set_activatepush_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_activate_push));
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  gst_ghost_pad_set_internal (GST_GHOST_PAD (object), NULL);

  G_OBJECT_CLASS (gst_ghost_pad_parent_class)->dispose (object);
}

/**
 * gst_ghost_pad_new_no_target:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @dir: the direction of the ghostpad
 *
 * Create a new ghostpad without a target with the given direction.
 * A target can be set on the ghostpad later with the
 * gst_ghost_pad_set_target() function.
 *
 * The created ghostpad will not have a padtemplate.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new_no_target (const gchar * name, GstPadDirection dir)
{
  GstPad *ret;

  ret = g_object_new (GST_TYPE_GHOST_PAD, "name", name, "direction", dir, NULL);

  gst_pad_set_activatepush_function (ret,
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_activate_push));
  gst_pad_set_link_function (ret, GST_DEBUG_FUNCPTR (gst_ghost_pad_do_link));
  gst_pad_set_unlink_function (ret,
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_unlink));

  return ret;
}

/**
 * gst_ghost_pad_new:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @target: the pad to ghost.
 *
 * Create a new ghostpad with @target as the target. The direction and
 * padtemplate will be taken from the target pad.
 *
 * Will ref the target.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new (const gchar * name, GstPad * target)
{
  GstPad *ret;

  g_return_val_if_fail (GST_IS_PAD (target), NULL);
  g_return_val_if_fail (!gst_pad_is_linked (target), NULL);

  if ((ret = gst_ghost_pad_new_no_target (name, GST_PAD_DIRECTION (target)))) {
    g_object_set (G_OBJECT (ret),
        "template", GST_PAD_PAD_TEMPLATE (target), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD (ret), target);
  }
  return ret;
}

/**
 * gst_ghost_pad_get_target:
 * @gpad: the #GstGhostpad
 *
 * Get the target pad of #gpad. Unref target pad after usage.
 *
 * Returns: the target #GstPad, can be NULL if the ghostpad
 * has no target set. Unref target pad after usage.
 */
GstPad *
gst_ghost_pad_get_target (GstGhostPad * gpad)
{
  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), NULL);

  return gst_proxy_pad_get_target (GST_PAD_CAST (gpad));
}

/**
 * gst_ghost_pad_set_target:
 * @gpad: the #GstGhostpad
 * @newtarget: the new pad target
 *
 * Set the new target of the ghostpad @gpad. Any existing target
 * is unlinked and links to the new target are established.
 *
 * Returns: TRUE if the new target could be set, FALSE otherwise.
 */
gboolean
gst_ghost_pad_set_target (GstGhostPad * gpad, GstPad * newtarget)
{
  GstPad *internal;
  GstPad *oldtarget;
  gboolean result;

  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), FALSE);

  GST_PROXY_LOCK (gpad);
  internal = gpad->internal;

  GST_DEBUG ("set target %s:%s on %s:%s",
      GST_DEBUG_PAD_NAME (newtarget), GST_DEBUG_PAD_NAME (gpad));

  /* clear old target */
  if ((oldtarget = GST_PROXY_PAD_TARGET (gpad))) {
    /* if we have an internal pad, unlink */
    if (internal) {
      if (GST_PAD_IS_SRC (internal))
        gst_pad_unlink (internal, oldtarget);
      else
        gst_pad_unlink (oldtarget, internal);
    }
  }

  result = gst_proxy_pad_set_target_unlocked (GST_PAD_CAST (gpad), newtarget);

  if (result && newtarget) {
    /* and link to internal pad if we have one */
    if (internal) {
      if (GST_PAD_IS_SRC (internal))
        result = gst_pad_link (internal, newtarget);
      else
        result = gst_pad_link (newtarget, internal);
    }
  }
  GST_PROXY_UNLOCK (gpad);

  return result;
}
