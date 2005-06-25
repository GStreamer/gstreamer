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


#include "gst_private.h"

#include "gstghostpad.h"
#include "gstelement.h"
#include "gstbin.h"


#define GST_TYPE_PROXY_PAD		(gst_proxy_pad_get_type ())
#define GST_IS_PROXY_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PROXY_PAD))
#define GST_IS_PROXY_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PROXY_PAD))
#define GST_PROXY_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PROXY_PAD, GstProxyPad))
#define GST_PROXY_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PROXY_PAD, GstProxyPadClass))
#define GST_PROXY_PAD_TARGET(pad)	(GST_PROXY_PAD (pad)->target)


typedef struct _GstProxyPad GstProxyPad;
typedef struct _GstProxyPadClass GstProxyPadClass;


enum
{
  PROXY_PROP_0,
  PROXY_PROP_TARGET
};

struct _GstProxyPad
{
  GstPad pad;

  GstPad *target;

  GMutex *property_lock;

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


static void gst_proxy_pad_dispose (GObject * object);
static void gst_proxy_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_proxy_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
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
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_proxy_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_proxy_pad_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROXY_PROP_TARGET,
      g_param_spec_object ("target", "Target", "The proxy pad's target",
          GST_TYPE_PAD, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, NULL);

  return gst_pad_get_query_types (target);
}

static gboolean
gst_proxy_pad_do_event (GstPad * pad, GstEvent * event)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return gst_pad_send_event (target, event);
}

static gboolean
gst_proxy_pad_do_query (GstPad * pad, GstQuery * query)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return gst_pad_query (target, query);
}

static GList *
gst_proxy_pad_do_internal_link (GstPad * pad)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, NULL);

  return gst_pad_get_internal_links (target);
}

static GstFlowReturn
gst_proxy_pad_do_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, GST_FLOW_UNEXPECTED);

  return target->bufferallocfunc (target, offset, size, caps, buf);
}

static gboolean
gst_proxy_pad_do_activate (GstPad * pad, GstActivateMode mode)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return gst_pad_set_active (target, mode);
}

static void
gst_proxy_pad_do_loop (GstPad * pad)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_if_fail (target != NULL);

  target->loopfunc (target);
}

static GstFlowReturn
gst_proxy_pad_do_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, GST_FLOW_UNEXPECTED);

  return gst_pad_chain (target, buffer);
}

static GstFlowReturn
gst_proxy_pad_do_getrange (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, GST_FLOW_UNEXPECTED);

  return target->getrangefunc (target, offset, size, buffer);
}

static gboolean
gst_proxy_pad_do_checkgetrange (GstPad * pad)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return target->checkgetrangefunc (target);
}

static GstCaps *
gst_proxy_pad_do_getcaps (GstPad * pad)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, NULL);

  return target->getcapsfunc (target);
}

static gboolean
gst_proxy_pad_do_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return target->acceptcapsfunc (target, caps);
}

static GstCaps *
gst_proxy_pad_do_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, NULL);

  return target->fixatecapsfunc (target, caps);
}

static gboolean
gst_proxy_pad_do_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_val_if_fail (target != NULL, FALSE);

  return gst_pad_set_caps (target, caps);
}

#define SETFUNC(member, kind) \
  if (target->member) \
    gst_pad_set_##kind##_function (pad, gst_proxy_pad_do_##kind)

static void
gst_proxy_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPad *pad = GST_PAD (object);

  switch (prop_id) {
    case PROXY_PROP_TARGET:{
      GstPad *target;

      target = GST_PAD_CAST (gst_object_ref
          (GST_OBJECT_CAST (g_value_get_object (value))));
      GST_PROXY_PAD_TARGET (object) = target;

      /* really, all these should have default implementations so I can set them
       * in the _init() instead of here */
      SETFUNC (querytypefunc, query_type);
      SETFUNC (eventfunc, event);
      SETFUNC (queryfunc, query);
      SETFUNC (intlinkfunc, internal_link);
      SETFUNC (activatefunc, activate);
      SETFUNC (loopfunc, loop);
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

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_proxy_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROXY_PROP_TARGET:
      g_value_set_object (value, GST_PROXY_PAD_TARGET (object));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_proxy_pad_init (GstProxyPad * pad)
{
  pad->property_lock = g_mutex_new ();
}

static void
gst_proxy_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  if (GST_PROXY_PAD_TARGET (pad)) {
    gst_object_replace ((GstObject **) & GST_PROXY_PAD_TARGET (pad), NULL);
  }

  G_OBJECT_CLASS (gst_proxy_pad_parent_class)->dispose (object);
}

static void
gst_proxy_pad_finalize (GObject * object)
{
  GstProxyPad *pad = GST_PROXY_PAD (object);

  g_mutex_free (pad->property_lock);
  pad->property_lock = NULL;

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


enum
{
  GHOST_PROP_0,
  GHOST_PROP_INTERNAL
};

struct _GstGhostPad
{
  GstProxyPad pad;

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


static void gst_ghost_pad_dispose (GObject * object);
static void gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


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

static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ghost_pad_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), GHOST_PROP_INTERNAL,
      g_param_spec_object ("internal", "Internal",
          "The ghost pad's internal pad", GST_TYPE_PAD, G_PARAM_READWRITE));
}

static GstPadLinkReturn
gst_ghost_pad_do_link (GstPad * pad, GstPad * peer)
{
  GstPad *internal, *target;

  target = GST_PROXY_PAD_TARGET (pad);
  g_return_val_if_fail (target != NULL, GST_PAD_LINK_NOSCHED);

  /* proxy the peer into the bin */
  internal = g_object_new (GST_TYPE_PROXY_PAD,
      "name", NULL,
      "direction", GST_PAD_DIRECTION (peer),
      "template", GST_PAD_PAD_TEMPLATE (peer), "target", peer, NULL);
  g_object_set (pad, "internal", internal, NULL);

  if ((GST_PAD_IS_SRC (internal) &&
          gst_pad_link (internal, target) == GST_PAD_LINK_OK) ||
      (GST_PAD_IS_SINK (internal) &&
          (gst_pad_link (target, internal) == GST_PAD_LINK_OK))) {
    gst_pad_set_active (internal, GST_PAD_ACTIVATE_MODE (pad));
    return GST_PAD_LINK_OK;
  } else {
    g_object_set (pad, "internal", NULL, NULL);
    return GST_PAD_LINK_REFUSED;
  }
}

static void
gst_ghost_pad_do_unlink (GstPad * pad)
{
  GstPad *target = GST_PROXY_PAD_TARGET (pad);

  g_return_if_fail (target != NULL);

  if (target->unlinkfunc)
    target->unlinkfunc (target);

  /* doesn't work with the object locks in the properties dispatcher... */
  /* g_object_set (pad, "internal", NULL, NULL); */
}

static void
on_int_notify (GstPad * internal, GParamSpec * unused, GstGhostPad * pad)
{
  GstCaps *caps;

  g_object_get (internal, "caps", &caps, NULL);

  GST_LOCK (pad);
  gst_caps_replace (&(GST_PAD_CAPS (pad)), caps);
  GST_UNLOCK (pad);

  g_object_notify (G_OBJECT (pad), "caps");
  if (caps)
    gst_caps_unref (caps);
}

static void
gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGhostPad *pad = GST_GHOST_PAD (object);

  switch (prop_id) {
    case GHOST_PROP_INTERNAL:{
      GstPad *internal;

      g_mutex_lock (GST_PROXY_PAD (pad)->property_lock);

      if (pad->internal) {
        GstPad *intpeer;

        g_signal_handler_disconnect (pad->internal, pad->notify_id);

        intpeer = gst_pad_get_peer (pad->internal);
        if (intpeer) {
          if (GST_PAD_IS_SRC (pad->internal)) {
            gst_pad_unlink (pad->internal, intpeer);
          } else {
            gst_pad_unlink (intpeer, pad->internal);
          }
          gst_object_unref (GST_OBJECT (intpeer));
        }

        /* delete me, only here for testing... */
        if (GST_OBJECT_REFCOUNT_VALUE (pad->internal) != 1) {
          gst_critical ("Refcounting problem: %" GST_PTR_FORMAT, pad->internal);
        }

        /* should dispose it */
        gst_object_unparent (GST_OBJECT_CAST (pad->internal));
      }

      internal = g_value_get_object (value);    /* no extra refcount... */

      if (internal) {
        if (!gst_object_set_parent (GST_OBJECT_CAST (internal),
                GST_OBJECT_CAST (pad))) {
          gst_critical ("Could not set internal pad %" GST_PTR_FORMAT,
              internal);
          g_mutex_unlock (GST_PROXY_PAD (pad)->property_lock);
          return;
        }

        /* could be more general here, iterating over all writable properties...
         * taking the short road for now tho */
        pad->notify_id = g_signal_connect (internal, "notify::caps",
            G_CALLBACK (on_int_notify), pad);
        on_int_notify (internal, NULL, pad);

        /* a ref was taken by set_parent */
      }

      pad->internal = internal;

      g_mutex_unlock (GST_PROXY_PAD (pad)->property_lock);

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case GHOST_PROP_INTERNAL:
      g_value_set_object (value, GST_GHOST_PAD (object)->internal);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  /* noop */
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  g_object_set (object, "internal", NULL, NULL);

  G_OBJECT_CLASS (gst_ghost_pad_parent_class)->dispose (object);
}

/**
 * gst_ghost_pad_new:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @target: the pad to ghost.
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
  g_return_val_if_fail (!GST_PAD_IS_LINKED (target), NULL);

  ret = g_object_new (GST_TYPE_GHOST_PAD,
      "name", name,
      "direction", GST_PAD_DIRECTION (target),
      "template", GST_PAD_PAD_TEMPLATE (target), "target", target, NULL);

  gst_pad_set_link_function (ret, gst_ghost_pad_do_link);
  gst_pad_set_unlink_function (ret, gst_ghost_pad_do_unlink);

  return ret;
}
